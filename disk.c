#include <stdio.h>
#include <stdlib.h>

#define BLOCK_SIZE 512

FILE *disk;

// Open disk file
void open_disk() {
    disk = fopen("disk.img", "r+");
    if (!disk) {
        perror("Error opening disk");
        exit(1);
    }
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