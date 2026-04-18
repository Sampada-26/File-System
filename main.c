#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "fs.h"

// Disk functions
extern void open_disk();
extern void read_block(int block, void *buffer);
extern void write_block(int block, void *buffer);

#define FILE_PATH "/hello.txt"
#define FILE_CONTENT "Hello from FUSE filesystem!\n"

// ---------------- GET ATTR ----------------
static int my_getattr(const char *path, struct stat *stbuf) {
    printf("GETATTR: %s\n", path);

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } 
    else if (strcmp(path, FILE_PATH) == 0) {
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(FILE_CONTENT);
    } 
    else {
        return -ENOENT;
    }

    return 0;
}

// ---------------- READDIR ----------------
static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi) {

    printf("READDIR: %s\n", path);

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    // Correct FUSE 2 syntax (4 args)
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, "hello.txt", NULL, 0);

    return 0;
}

// ---------------- OPEN ----------------
static int my_open(const char *path, struct fuse_file_info *fi) {
    printf("OPEN: %s\n", path);

    if (strcmp(path, FILE_PATH) != 0)
        return -ENOENT;

    return 0;
}

// ---------------- READ ----------------
static int my_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {

    printf("READ: %s\n", path);

    if (strcmp(path, FILE_PATH) != 0)
        return -ENOENT;

    size_t len = strlen(FILE_CONTENT);

    if (offset < len) {
        if (offset + size > len)
            size = len - offset;

        memcpy(buf, FILE_CONTENT + offset, size);
    } else {
        size = 0;
    }

    return size;
}

// ---------------- CREATE ----------------
static int my_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    printf("CREATE: %s\n", path);
    return 0;
}

// ---------------- WRITE ----------------
static int my_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {

    printf("WRITE: %s\n", path);
    printf("DATA: %.*s\n", (int)size, buf);

    // 🔥 Write to disk (block 0 for now - temporary)
    char block[512] = {0};
    memcpy(block, buf, size);
    write_block(0, block);

    return size;
}

// ---------------- OPERATIONS ----------------
static struct fuse_operations operations = {
    .getattr = my_getattr,
    .readdir = my_readdir,
    .open = my_open,
    .read = my_read,
    .write = my_write,
    .create = my_create,
};

// ---------------- MAIN ----------------
int main(int argc, char *argv[]) {
    open_disk();  // 🔥 initialize disk

    return fuse_main(argc, argv, &operations, NULL);
}