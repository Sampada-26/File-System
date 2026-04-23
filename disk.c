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
    int block = sb.inode_start + (inode_num * sizeof(inode_t)) / BLOCK_SIZE;
    int offset = (inode_num * sizeof(inode_t)) % BLOCK_SIZE;
    
    char buffer[BLOCK_SIZE];
    read_block(block, buffer);
    memcpy(inode, buffer + offset, sizeof(inode_t));
}

// Write inode
void write_inode(uint32_t inode_num, inode_t *inode) {
    int block = sb.inode_start + (inode_num * sizeof(inode_t)) / BLOCK_SIZE;
    int offset = (inode_num * sizeof(inode_t)) % BLOCK_SIZE;
    
    char buffer[BLOCK_SIZE];
    read_block(block, buffer);
    memcpy(buffer + offset, inode, sizeof(inode_t));
    write_block(block, buffer);
}

// Format the disk with filesystem structures
void format_disk() {
    // Initialize superblock
    sb.magic = MAGIC_NUM;
    sb.block_count = BLOCK_COUNT;
    sb.inode_count = MAX_FILES;
    sb.free_blocks = BLOCK_COUNT - 1 - ((MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE) - 2; // Reserve blocks for superblock, inodes, and bitmaps
    sb.free_inodes = MAX_FILES - 1; // Reserve root inode
    sb.data_start = 1 + ((MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE) + 2; // Superblock + inodes + bitmaps
    sb.inode_start = 1; // Block after superblock
    sb.root_inode = 0; // Root directory inode
    
    write_block(0, &sb);
    
    // Initialize bitmaps (all free except used blocks)
    int bitmap_size = (BLOCK_COUNT + 7) / 8;
    block_bitmap = calloc(bitmap_size, 1);
    inode_bitmap = calloc((MAX_FILES + 7) / 8, 1);
    
    // Mark superblock, inode blocks, and bitmap blocks as used
    int inode_blocks = (MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int total_reserved = 1 + inode_blocks + 2; // superblock + inodes + bitmaps
    
    for (int i = 0; i < total_reserved && i < BLOCK_COUNT; i++) {
        block_bitmap[i / 8] |= (1 << (i % 8));
    }
    
    // Mark root inode as used
    inode_bitmap[0] |= 1;
    
    write_block(1 + inode_blocks, block_bitmap);
    write_block(1 + inode_blocks + 1, inode_bitmap);
    
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
    
    free(block_bitmap);
    free(inode_bitmap);
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
    for (int i = 0; i < BLOCK_COUNT; i++) {
        fwrite(zeros, BLOCK_SIZE, 1, disk);
    }
    free(zeros);
    
    // Initialize filesystem
    format_disk();
}

// Allocate a free block
int alloc_block() {
    if (sb.free_blocks == 0) return -1;
    
    int bitmap_block = 1 + (MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    read_block(bitmap_block, block_bitmap);
    
    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (!(block_bitmap[i / 8] & (1 << (i % 8)))) {
            block_bitmap[i / 8] |= (1 << (i % 8));
            write_block(bitmap_block, block_bitmap);
            sb.free_blocks--;
            write_block(0, &sb);
            return i;
        }
    }
    return -1;
}

// Free a block
void free_block(int block_num) {
    int bitmap_block = 1 + (MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    read_block(bitmap_block, block_bitmap);
    
    block_bitmap[block_num / 8] &= ~(1 << (block_num % 8));
    write_block(bitmap_block, block_bitmap);
    sb.free_blocks++;
    write_block(0, &sb);
}

// Allocate a free inode
uint32_t alloc_inode() {
    if (sb.free_inodes == 0) return -1;
    
    int bitmap_block = 1 + (MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE + 1;
    read_block(bitmap_block, inode_bitmap);
    
    for (uint32_t i = 0; i < MAX_FILES; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inode_bitmap[i / 8] |= (1 << (i % 8));
            write_block(bitmap_block, inode_bitmap);
            sb.free_inodes--;
            write_block(0, &sb);
            return i;
        }
    }
    return -1;
}

// Free an inode
void free_inode(uint32_t inode_num) {
    int bitmap_block = 1 + (MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE + 1;
    read_block(bitmap_block, inode_bitmap);
    
    inode_bitmap[inode_num / 8] &= ~(1 << (inode_num % 8));
    write_block(bitmap_block, inode_bitmap);
    sb.free_inodes++;
    write_block(0, &sb);
}

// Initialize filesystem (load superblock and bitmaps)
void init_fs() {
    read_block(0, &sb);
    if (sb.magic != MAGIC_NUM) {
        printf("Disk not formatted. Formatting...\n");
        format_disk();
        return;
    }
    
    int inode_blocks = (MAX_FILES * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int bitmap_size = (BLOCK_COUNT + 7) / 8;
    block_bitmap = malloc(bitmap_size);
    inode_bitmap = malloc((MAX_FILES + 7) / 8);
    
    read_block(1 + inode_blocks, block_bitmap);
    read_block(1 + inode_blocks + 1, inode_bitmap);
}