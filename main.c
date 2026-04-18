#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define FILE_PATH "/hello.txt"
#define FILE_CONTENT "Hello from FUSE filesystem!\n"

// Get file attributes
static int my_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } 
    else if (strcmp(path, FILE_PATH) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(FILE_CONTENT);
    } 
    else {
        return -ENOENT;
    }

    return 0;
}

// Read directory
static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi) {

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, "hello.txt", NULL, 0);

    return 0;
}

// Open file
static int my_open(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, FILE_PATH) != 0)
        return -ENOENT;

    return 0;
}

// Read file
static int my_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {

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

// Operations structure
static struct fuse_operations operations = {
    .getattr = my_getattr,
    .readdir = my_readdir,
    .open = my_open,
    .read = my_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &operations, NULL);
}
