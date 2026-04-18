#ifndef FS_H
#define FS_H

#include <time.h>

#define MAX_FILES 100
#define BLOCK_SIZE 512

typedef struct {
    char name[50];
    int size;
    int start_block;
    time_t created;
} inode;

typedef struct {
    inode files[MAX_FILES];
} superblock;

#endif
