#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <dirent.h>
#include "fs.h"

// External functions from disk.c
extern void open_disk();
extern void create_disk();
extern void init_fs();
extern void read_block(int block, void *buffer);
extern void write_block(int block, void *buffer);
extern void read_inode(uint32_t inode_num, inode_t *inode);
extern void write_inode(uint32_t inode_num, inode_t *inode);
extern int alloc_block();
extern void free_block(int block_num);
extern uint32_t alloc_inode();
extern void free_inode(uint32_t inode_num);

// Helper functions
uint32_t path_to_inode(const char *path);
int add_dir_entry(uint32_t dir_inode, const char *name, uint32_t inode_num);
int remove_dir_entry(uint32_t dir_inode, const char *name);
uint32_t find_dir_entry(uint32_t dir_inode, const char *name);
int read_file_data(inode_t *inode, char *buf, size_t size, off_t offset);
int write_file_data(inode_t *inode, const char *buf, size_t size, off_t offset);

// ---------------- GET ATTR ----------------
static int my_getattr(const char *path, struct stat *stbuf) {
    printf("GETATTR: %s\n", path);

    memset(stbuf, 0, sizeof(struct stat));
    
    uint32_t inode_num = path_to_inode(path);
    if (inode_num == (uint32_t)-1) {
        return -ENOENT;
    }
    
    inode_t inode;
    read_inode(inode_num, &inode);
    
    if (inode.type == FT_DIR) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = inode.nlinks;
    } else if (inode.type == FT_REG) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = inode.nlinks;
        stbuf->st_size = inode.size;
    } else {
        return -ENOENT;
    }
    
    stbuf->st_atime = inode.accessed;
    stbuf->st_mtime = inode.modified;
    stbuf->st_ctime = inode.created;
    
    return 0;
}

// ---------------- READDIR ----------------
static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi) {
    printf("READDIR: %s\n", path);

    uint32_t dir_inode_num = path_to_inode(path);
    if (dir_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }
    
    inode_t dir_inode;
    read_inode(dir_inode_num, &dir_inode);
    if (dir_inode.type != FT_DIR) {
        return -ENOTDIR;
    }
    
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    
    // Read directory entries
    for (uint32_t i = 0; i < dir_inode.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode.direct_blocks[i], block_data);
        
        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num != 0) {
                filler(buf, entries[j].name, NULL, 0);
            }
        }
    }
    
    return 0;
}

// ---------------- MKDIR ----------------
static int my_mkdir(const char *path, mode_t mode) {
    printf("MKDIR: %s\n", path);
    
    // Extract parent path and directory name
    char *path_copy = strdup(path);
    char *last_slash = strrchr(path_copy, '/');
    char *dir_name;
    char *parent_path;
    
    if (last_slash == path_copy) {
        // Root level directory
        parent_path = "/";
        dir_name = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent_path = path_copy;
        dir_name = last_slash + 1;
    }
    
    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOENT;
    }
    
    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    if (parent_inode.type != FT_DIR) {
        free(path_copy);
        return -ENOTDIR;
    }
    
    // Check if directory already exists
    if (find_dir_entry(parent_inode_num, dir_name) != (uint32_t)-1) {
        free(path_copy);
        return -EEXIST;
    }
    
    // Allocate inode for new directory
    uint32_t new_inode_num = alloc_inode();
    if (new_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOSPC;
    }
    
    // Initialize new directory inode
    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.type = FT_DIR;
    new_inode.size = 0;
    new_inode.blocks = 0;
    new_inode.created = time(NULL);
    new_inode.modified = time(NULL);
    new_inode.accessed = time(NULL);
    new_inode.nlinks = 2; // . and ..
    
    write_inode(new_inode_num, &new_inode);
    
    // Add . and .. entries to new directory
    add_dir_entry(new_inode_num, ".", new_inode_num);
    add_dir_entry(new_inode_num, "..", parent_inode_num);
    
    // Add entry to parent directory
    add_dir_entry(parent_inode_num, dir_name, new_inode_num);
    
    // Update parent directory timestamps
    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    write_inode(parent_inode_num, &parent_inode);
    
    free(path_copy);
    return 0;
}

// ---------------- CREATE ----------------
static int my_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    printf("CREATE: %s\n", path);
    
    // Extract parent path and file name
    char *path_copy = strdup(path);
    char *last_slash = strrchr(path_copy, '/');
    char *file_name;
    char *parent_path;
    
    if (last_slash == path_copy) {
        // Root level file
        parent_path = "/";
        file_name = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent_path = path_copy;
        file_name = last_slash + 1;
    }
    
    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOENT;
    }
    
    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    if (parent_inode.type != FT_DIR) {
        free(path_copy);
        return -ENOTDIR;
    }
    
    // Check if file already exists
    if (find_dir_entry(parent_inode_num, file_name) != (uint32_t)-1) {
        free(path_copy);
        return -EEXIST;
    }
    
    // Allocate inode for new file
    uint32_t new_inode_num = alloc_inode();
    if (new_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOSPC;
    }
    
    // Initialize new file inode
    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.type = FT_REG;
    new_inode.size = 0;
    new_inode.blocks = 0;
    new_inode.created = time(NULL);
    new_inode.modified = time(NULL);
    new_inode.accessed = time(NULL);
    new_inode.nlinks = 1;
    
    write_inode(new_inode_num, &new_inode);
    
    // Add entry to parent directory
    add_dir_entry(parent_inode_num, file_name, new_inode_num);
    
    // Update parent directory timestamps
    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    write_inode(parent_inode_num, &parent_inode);
    
    free(path_copy);
    return 0;
}

// ---------------- OPEN ----------------
static int my_open(const char *path, struct fuse_file_info *fi) {
    printf("OPEN: %s\n", path);

    uint32_t inode_num = path_to_inode(path);
    if (inode_num == (uint32_t)-1) {
        return -ENOENT;
    }
    
    inode_t inode;
    read_inode(inode_num, &inode);
    if (inode.type != FT_REG) {
        return -EISDIR;
    }
    
    // Update access time
    inode.accessed = time(NULL);
    write_inode(inode_num, &inode);
    
    return 0;
}

// ---------------- READ ----------------
static int my_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    printf("READ: %s\n", path);

    uint32_t inode_num = path_to_inode(path);
    if (inode_num == (uint32_t)-1) {
        return -ENOENT;
    }
    
    inode_t inode;
    read_inode(inode_num, &inode);
    if (inode.type != FT_REG) {
        return -EISDIR;
    }
    
    // Update access time
    inode.accessed = time(NULL);
    write_inode(inode_num, &inode);
    
    return read_file_data(&inode, buf, size, offset);
}

// ---------------- WRITE ----------------
static int my_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    printf("WRITE: %s\n", path);

    uint32_t inode_num = path_to_inode(path);
    if (inode_num == (uint32_t)-1) {
        return -ENOENT;
    }
    
    inode_t inode;
    read_inode(inode_num, &inode);
    if (inode.type != FT_REG) {
        return -EISDIR;
    }
    
    int written = write_file_data(&inode, buf, size, offset);
    if (written > 0) {
        inode.modified = time(NULL);
        inode.accessed = time(NULL);
        write_inode(inode_num, &inode);
    }
    
    return written;
}

// ---------------- UNLINK ----------------
static int my_unlink(const char *path) {
    printf("UNLINK: %s\n", path);
    
    // Extract parent path and file name
    char *path_copy = strdup(path);
    char *last_slash = strrchr(path_copy, '/');
    char *file_name;
    char *parent_path;
    
    if (last_slash == path_copy) {
        // Root level file
        parent_path = "/";
        file_name = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent_path = path_copy;
        file_name = last_slash + 1;
    }
    
    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOENT;
    }
    
    uint32_t file_inode_num = find_dir_entry(parent_inode_num, file_name);
    if (file_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOENT;
    }
    
    inode_t file_inode;
    read_inode(file_inode_num, &file_inode);
    if (file_inode.type != FT_REG) {
        free(path_copy);
        return -EISDIR;
    }
    
    // Remove directory entry
    remove_dir_entry(parent_inode_num, file_name);
    
    // Free file blocks
    for (uint32_t i = 0; i < file_inode.blocks && i < 12; i++) {
        free_block(file_inode.direct_blocks[i]);
    }
    
    // Free inode
    free_inode(file_inode_num);
    
    // Update parent directory timestamps
    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    write_inode(parent_inode_num, &parent_inode);
    
    free(path_copy);
    return 0;
}

// ---------------- RMDIR ----------------
static int my_rmdir(const char *path) {
    printf("RMDIR: %s\n", path);
    
    // Extract parent path and directory name
    char *path_copy = strdup(path);
    char *last_slash = strrchr(path_copy, '/');
    char *dir_name;
    char *parent_path;
    
    if (last_slash == path_copy) {
        // Root level directory
        parent_path = "/";
        dir_name = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent_path = path_copy;
        dir_name = last_slash + 1;
    }
    
    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOENT;
    }
    
    uint32_t dir_inode_num = find_dir_entry(parent_inode_num, dir_name);
    if (dir_inode_num == (uint32_t)-1) {
        free(path_copy);
        return -ENOENT;
    }
    
    inode_t dir_inode;
    read_inode(dir_inode_num, &dir_inode);
    if (dir_inode.type != FT_DIR) {
        free(path_copy);
        return -ENOTDIR;
    }
    
    // Check if directory is empty (only . and ..)
    int entry_count = 0;
    for (uint32_t i = 0; i < dir_inode.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode.direct_blocks[i], block_data);
        
        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num != 0) {
                entry_count++;
            }
        }
    }
    
    if (entry_count > 2) { // . and ..
        free(path_copy);
        return -ENOTEMPTY;
    }
    
    // Remove directory entry from parent
    remove_dir_entry(parent_inode_num, dir_name);
    
    // Free directory blocks
    for (uint32_t i = 0; i < dir_inode.blocks && i < 12; i++) {
        free_block(dir_inode.direct_blocks[i]);
    }
    
    // Free inode
    free_inode(dir_inode_num);
    
    // Update parent directory timestamps
    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    write_inode(parent_inode_num, &parent_inode);
    
    free(path_copy);
    return 0;
}

// ---------------- OPERATIONS ----------------
static struct fuse_operations operations = {
    .getattr = my_getattr,
    .readdir = my_readdir,
    .mkdir = my_mkdir,
    .create = my_create,
    .open = my_open,
    .read = my_read,
    .write = my_write,
    .unlink = my_unlink,
    .rmdir = my_rmdir,
};

// Helper function implementations
uint32_t path_to_inode(const char *path) {
    if (strcmp(path, "/") == 0) {
        return 0; // Root inode
    }
    
    char *path_copy = strdup(path);
    char *token = strtok(path_copy + 1, "/"); // Skip leading /
    uint32_t current_inode = 0;
    
    while (token) {
        current_inode = find_dir_entry(current_inode, token);
        if (current_inode == (uint32_t)-1) {
            free(path_copy);
            return -1;
        }
        token = strtok(NULL, "/");
    }
    
    free(path_copy);
    return current_inode;
}

int add_dir_entry(uint32_t dir_inode, const char *name, uint32_t inode_num) {
    inode_t dir_inode_data;
    read_inode(dir_inode, &dir_inode_data);
    
    // Find a free slot in existing blocks
    for (uint32_t i = 0; i < dir_inode_data.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode_data.direct_blocks[i], block_data);
        
        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num == 0) {
                entries[j].inode_num = inode_num;
                strcpy(entries[j].name, name);
                write_block(dir_inode_data.direct_blocks[i], block_data);
                return 0;
            }
        }
    }
    
    // Need to allocate a new block
    if (dir_inode_data.blocks >= 12) {
        return -ENOSPC; // No more direct blocks
    }
    
    int new_block = alloc_block();
    if (new_block == -1) {
        return -ENOSPC;
    }
    
    dir_inode_data.direct_blocks[dir_inode_data.blocks] = new_block;
    dir_inode_data.blocks++;
    dir_inode_data.size += BLOCK_SIZE;
    
    char block_data[BLOCK_SIZE] = {0};
    dir_entry *entries = (dir_entry *)block_data;
    entries[0].inode_num = inode_num;
    strcpy(entries[0].name, name);
    
    write_block(new_block, block_data);
    write_inode(dir_inode, &dir_inode_data);
    
    return 0;
}

int remove_dir_entry(uint32_t dir_inode, const char *name) {
    inode_t dir_inode_data;
    read_inode(dir_inode, &dir_inode_data);
    
    for (uint32_t i = 0; i < dir_inode_data.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode_data.direct_blocks[i], block_data);
        
        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num != 0 && strcmp(entries[j].name, name) == 0) {
                entries[j].inode_num = 0;
                memset(entries[j].name, 0, sizeof(entries[j].name));
                write_block(dir_inode_data.direct_blocks[i], block_data);
                return 0;
            }
        }
    }
    
    return -ENOENT;
}

uint32_t find_dir_entry(uint32_t dir_inode, const char *name) {
    inode_t dir_inode_data;
    read_inode(dir_inode, &dir_inode_data);
    
    for (uint32_t i = 0; i < dir_inode_data.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode_data.direct_blocks[i], block_data);
        
        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num != 0 && strcmp(entries[j].name, name) == 0) {
                return entries[j].inode_num;
            }
        }
    }
    
    return -1;
}

int read_file_data(inode_t *inode, char *buf, size_t size, off_t offset) {
    if (offset >= inode->size) {
        return 0;
    }
    
    if (offset + size > inode->size) {
        size = inode->size - offset;
    }
    
    size_t bytes_read = 0;
    size_t block_offset = offset / BLOCK_SIZE;
    size_t byte_offset = offset % BLOCK_SIZE;
    
    while (bytes_read < size && block_offset < inode->blocks) {
        char block_data[BLOCK_SIZE];
        read_block(inode->direct_blocks[block_offset], block_data);
        
        size_t to_read = BLOCK_SIZE - byte_offset;
        if (to_read > size - bytes_read) {
            to_read = size - bytes_read;
        }
        
        memcpy(buf + bytes_read, block_data + byte_offset, to_read);
        bytes_read += to_read;
        block_offset++;
        byte_offset = 0;
    }
    
    return bytes_read;
}

int write_file_data(inode_t *inode, const char *buf, size_t size, off_t offset) {
    size_t new_size = offset + size;
    size_t needed_blocks = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    // Allocate new blocks if needed
    while (inode->blocks < needed_blocks && inode->blocks < 12) {
        int new_block = alloc_block();
        if (new_block == -1) {
            return -ENOSPC;
        }
        inode->direct_blocks[inode->blocks] = new_block;
        inode->blocks++;
    }
    
    if (needed_blocks > 12) {
        return -ENOSPC; // Would need indirect blocks
    }
    
    size_t bytes_written = 0;
    size_t block_offset = offset / BLOCK_SIZE;
    size_t byte_offset = offset % BLOCK_SIZE;
    
    while (bytes_written < size && block_offset < inode->blocks) {
        char block_data[BLOCK_SIZE];
        if (byte_offset == 0 && size - bytes_written >= BLOCK_SIZE) {
            // Write full block
            memcpy(block_data, buf + bytes_written, BLOCK_SIZE);
            write_block(inode->direct_blocks[block_offset], block_data);
            bytes_written += BLOCK_SIZE;
        } else {
            // Read-modify-write partial block
            read_block(inode->direct_blocks[block_offset], block_data);
            size_t to_write = BLOCK_SIZE - byte_offset;
            if (to_write > size - bytes_written) {
                to_write = size - bytes_written;
            }
            memcpy(block_data + byte_offset, buf + bytes_written, to_write);
            write_block(inode->direct_blocks[block_offset], block_data);
            bytes_written += to_write;
        }
        block_offset++;
        byte_offset = 0;
    }
    
    if (new_size > inode->size) {
        inode->size = new_size;
    }
    
    return bytes_written;
}

// ---------------- MAIN ----------------
int main(int argc, char *argv[]) {
    if (access("disk.img", F_OK) == -1) {
        printf("Creating new disk image...\n");
        create_disk();
    } else {
        open_disk();
        init_fs();
    }
    
    return fuse_main(argc, argv, &operations, NULL);
}