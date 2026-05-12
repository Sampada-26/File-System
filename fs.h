#ifndef FS_H
#define FS_H

#include <time.h>
#include <stdint.h>

#define BLOCK_SIZE 512
#define MAX_NAME_LEN 255
#define MAX_FILES 1024
#define MAX_DIRENTRIES_PER_BLOCK (BLOCK_SIZE / sizeof(dir_entry))

// File types
#define FT_REG 1  // Regular file
#define FT_DIR 2  // Directory

typedef struct {
    uint32_t inode_num;
    char name[MAX_NAME_LEN + 1];
} dir_entry;

static inline int dir_entry_is_empty(const dir_entry *entry) {
    return entry->name[0] == '\0';
}

typedef struct {
    uint32_t type;           // File type (FT_REG or FT_DIR)
    uint32_t size;           // File size in bytes
    uint32_t blocks;         // Number of data blocks
    uint32_t direct_blocks[12]; // Direct block pointers
    uint32_t indirect_block; // Single indirect block pointer
    time_t created;          // Creation time
    time_t modified;         // Modification time
    time_t accessed;         // Access time
    uint32_t nlinks;         // Number of hard links
} inode_t;

typedef struct {
    uint32_t magic;          // Filesystem magic number
    uint32_t block_count;    // Total blocks in filesystem
    uint32_t inode_count;    // Total inodes
    uint32_t free_blocks;    // Number of free blocks
    uint32_t free_inodes;    // Number of free inodes
    uint32_t data_start;     // Block where data starts
    uint32_t inode_start;    // Block where inodes start
    uint32_t root_inode;     // Root directory inode number
} superblock_t;

#define MAGIC_NUM 0xDEADBEEF
#define INODE_SIZE sizeof(inode_t)
#define INODES_PER_BLOCK (BLOCK_SIZE / INODE_SIZE)

#endif
