#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fs.h"

#define DISK_SIZE (10 * 1024 * 1024)  // 10MB disk
#define BLOCK_COUNT (DISK_SIZE / BLOCK_SIZE)

FILE *disk;
superblock_t sb;
uint8_t *block_bitmap;
uint8_t *inode_bitmap;

static int inode_blocks_count(void) {
    return (MAX_FILES * (int)sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

static int block_bitmap_size_bytes(void) {
    return (BLOCK_COUNT + 7) / 8;
}

static int inode_bitmap_size_bytes(void) {
    return (MAX_FILES + 7) / 8;
}

static int block_bitmap_blocks_count(void) {
    return (block_bitmap_size_bytes() + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

static int inode_bitmap_blocks_count(void) {
    return (inode_bitmap_size_bytes() + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

static int block_bitmap_start_block(void) {
    return 1 + inode_blocks_count();
}

static int inode_bitmap_start_block(void) {
    return block_bitmap_start_block() + block_bitmap_blocks_count();
}

static int data_start_block(void) {
    return inode_bitmap_start_block() + inode_bitmap_blocks_count();
}

static void write_superblock(void) {
    char block[BLOCK_SIZE] = {0};
    memcpy(block, &sb, sizeof(sb));
    fseek(disk, 0, SEEK_SET);
    fwrite(block, BLOCK_SIZE, 1, disk);
    fflush(disk);
}

static void read_superblock(void) {
    char block[BLOCK_SIZE];
    fseek(disk, 0, SEEK_SET);
    fread(block, BLOCK_SIZE, 1, disk);
    memcpy(&sb, block, sizeof(sb));
}

static void write_bitmap_to_disk(const uint8_t *bitmap, int bytes, int start_block) {
    int blocks = (bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int i = 0; i < blocks; i++) {
        char block[BLOCK_SIZE] = {0};
        int offset = i * BLOCK_SIZE;
        int remaining = bytes - offset;
        int to_copy = remaining > BLOCK_SIZE ? BLOCK_SIZE : (remaining > 0 ? remaining : 0);

        if (to_copy > 0) {
            memcpy(block, bitmap + offset, to_copy);
        }
        fseek(disk, (start_block + i) * BLOCK_SIZE, SEEK_SET);
        fwrite(block, BLOCK_SIZE, 1, disk);
    }
    fflush(disk);
}

static void read_bitmap_from_disk(uint8_t *bitmap, int bytes, int start_block) {
    int blocks = (bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    memset(bitmap, 0, bytes);

    for (int i = 0; i < blocks; i++) {
        char block[BLOCK_SIZE];
        int offset = i * BLOCK_SIZE;
        int remaining = bytes - offset;
        int to_copy = remaining > BLOCK_SIZE ? BLOCK_SIZE : (remaining > 0 ? remaining : 0);

        fseek(disk, (start_block + i) * BLOCK_SIZE, SEEK_SET);
        fread(block, BLOCK_SIZE, 1, disk);

        if (to_copy > 0) {
            memcpy(bitmap + offset, block, to_copy);
        }
    }
}

static void persist_block_bitmap(void) {
    write_bitmap_to_disk(block_bitmap, block_bitmap_size_bytes(), block_bitmap_start_block());
}

static void persist_inode_bitmap(void) {
    write_bitmap_to_disk(inode_bitmap, inode_bitmap_size_bytes(), inode_bitmap_start_block());
}

// Read block
void read_block(int block, void *buffer) {
    fseek(disk, block * BLOCK_SIZE, SEEK_SET);
    fread(buffer, BLOCK_SIZE, 1, disk);
}

// Write block
void write_block(int block, void *buffer) {
    fseek(disk, block * BLOCK_SIZE, SEEK_SET);
    fwrite(buffer, BLOCK_SIZE, 1, disk);
    fflush(disk);
}

// Read inode
void read_inode(uint32_t inode_num, inode_t *inode) {
    long offset = (long)sb.inode_start * BLOCK_SIZE + (long)inode_num * sizeof(inode_t);
    fseek(disk, offset, SEEK_SET);
    fread(inode, sizeof(inode_t), 1, disk);
}

// Write inode
void write_inode(uint32_t inode_num, inode_t *inode) {
    long offset = (long)sb.inode_start * BLOCK_SIZE + (long)inode_num * sizeof(inode_t);
    fseek(disk, offset, SEEK_SET);
    fwrite(inode, sizeof(inode_t), 1, disk);
    fflush(disk);
}

// Format the disk with filesystem structures
void format_disk() {
    int block_bitmap_bytes = block_bitmap_size_bytes();
    int inode_bitmap_bytes = inode_bitmap_size_bytes();

    // Initialize superblock
    sb.magic = MAGIC_NUM;
    sb.block_count = BLOCK_COUNT;
    sb.inode_count = MAX_FILES;
    sb.inode_start = 1; // Block after superblock
    sb.root_inode = 0;  // Root directory inode
    sb.data_start = data_start_block();
    sb.free_blocks = BLOCK_COUNT - sb.data_start;
    sb.free_inodes = MAX_FILES - 1; // Reserve root inode

    write_superblock();

    // (Re)allocate bitmaps
    free(block_bitmap);
    free(inode_bitmap);
    block_bitmap = calloc(block_bitmap_bytes, 1);
    inode_bitmap = calloc(inode_bitmap_bytes, 1);

    if (!block_bitmap || !inode_bitmap) {
        perror("Error allocating bitmaps");
        exit(1);
    }

    // Mark reserved metadata blocks as used
    for (uint32_t i = 0; i < sb.data_start && i < BLOCK_COUNT; i++) {
        block_bitmap[i / 8] |= (1 << (i % 8));
    }

    // Mark root inode as used
    inode_bitmap[0] |= 1;

    persist_block_bitmap();
    persist_inode_bitmap();

    // Create root directory inode
    inode_t root_inode;
    memset(&root_inode, 0, sizeof(inode_t));
    root_inode.type = FT_DIR;
    root_inode.size = 0;
    root_inode.blocks = 0;
    root_inode.created = time(NULL);
    root_inode.modified = time(NULL);
    root_inode.accessed = time(NULL);
    root_inode.nlinks = 2; // . and ..

    write_inode(0, &root_inode);
}

// Open disk file
void open_disk() {
    disk = fopen("disk.img", "r+");
    if (!disk) {
        perror("Error opening disk");
        exit(1);
    }
}

// Create and format a new disk
void create_disk() {
    disk = fopen("disk.img", "w+");
    if (!disk) {
        perror("Error creating disk");
        exit(1);
    }

    // Initialize disk with zeros
    char *zeros = calloc(BLOCK_SIZE, 1);
    if (!zeros) {
        perror("Error allocating zero buffer");
        exit(1);
    }
    for (int i = 0; i < BLOCK_COUNT; i++) {
        fwrite(zeros, BLOCK_SIZE, 1, disk);
    }
    free(zeros);

    fflush(disk);

    // Initialize filesystem
    format_disk();
}

// Allocate a free block
int alloc_block() {
    if (sb.free_blocks == 0) {
        return -1;
    }

    for (int i = sb.data_start; i < BLOCK_COUNT; i++) {
        if (!(block_bitmap[i / 8] & (1 << (i % 8)))) {
            block_bitmap[i / 8] |= (1 << (i % 8));
            sb.free_blocks--;
            persist_block_bitmap();
            write_superblock();

            // Return zeroed data for predictable reads from newly allocated blocks.
            char zero_block[BLOCK_SIZE] = {0};
            write_block(i, zero_block);
            return i;
        }
    }

    return -1;
}

// Free a block
void free_block(int block_num) {
    if (block_num < 0 || (uint32_t)block_num < sb.data_start || block_num >= BLOCK_COUNT) {
        return;
    }

    if (block_bitmap[block_num / 8] & (1 << (block_num % 8))) {
        block_bitmap[block_num / 8] &= ~(1 << (block_num % 8));
        sb.free_blocks++;
        persist_block_bitmap();
        write_superblock();
    }
}

// Allocate a free inode
uint32_t alloc_inode() {
    if (sb.free_inodes == 0) {
        return (uint32_t)-1;
    }

    for (uint32_t i = 0; i < MAX_FILES; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inode_bitmap[i / 8] |= (1 << (i % 8));
            sb.free_inodes--;
            persist_inode_bitmap();
            write_superblock();
            return i;
        }
    }

    return (uint32_t)-1;
}

// Free an inode
void free_inode(uint32_t inode_num) {
    if (inode_num >= MAX_FILES) {
        return;
    }

    if (inode_bitmap[inode_num / 8] & (1 << (inode_num % 8))) {
        inode_bitmap[inode_num / 8] &= ~(1 << (inode_num % 8));
        sb.free_inodes++;
        persist_inode_bitmap();
        write_superblock();
    }
}

// Initialize filesystem (load superblock and bitmaps)
void init_fs() {
    read_superblock();
    if (sb.magic != MAGIC_NUM) {
        printf("Disk not formatted. Formatting...\n");
        format_disk();
        return;
    }

    // Make sure old images are upgraded to current metadata layout.
    uint32_t expected_data_start = (uint32_t)data_start_block();
    if (sb.data_start != expected_data_start) {
        printf("Metadata layout mismatch detected. Reformatting disk image...\n");
        format_disk();
        return;
    }

    int block_bitmap_bytes = block_bitmap_size_bytes();
    int inode_bitmap_bytes = inode_bitmap_size_bytes();

    free(block_bitmap);
    free(inode_bitmap);
    block_bitmap = malloc(block_bitmap_bytes);
    inode_bitmap = malloc(inode_bitmap_bytes);

    if (!block_bitmap || !inode_bitmap) {
        perror("Error allocating bitmaps");
        exit(1);
    }

    read_bitmap_from_disk(block_bitmap, block_bitmap_bytes, block_bitmap_start_block());
    read_bitmap_from_disk(inode_bitmap, inode_bitmap_bytes, inode_bitmap_start_block());
}
