#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
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

static int split_parent_child(const char *path, char *parent, size_t parent_sz,
                              char *name, size_t name_sz) {
    if (!path || path[0] != '/' || strcmp(path, "/") == 0) {
        return -EINVAL;
    }

    const char *last_slash = strrchr(path, '/');
    if (!last_slash || *(last_slash + 1) == '\0') {
        return -EINVAL;
    }

    size_t name_len = strlen(last_slash + 1);
    if (name_len == 0 || name_len > MAX_NAME_LEN || name_len >= name_sz) {
        return -ENAMETOOLONG;
    }

    strncpy(name, last_slash + 1, name_sz - 1);
    name[name_sz - 1] = '\0';

    if (last_slash == path) {
        if (parent_sz < 2) {
            return -ENAMETOOLONG;
        }
        strcpy(parent, "/");
        return 0;
    }

    size_t parent_len = (size_t)(last_slash - path);
    if (parent_len >= parent_sz) {
        return -ENAMETOOLONG;
    }

    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';
    return 0;
}

static int dir_is_empty(uint32_t dir_inode_num) {
    inode_t dir_inode;
    read_inode(dir_inode_num, &dir_inode);

    if (dir_inode.type != FT_DIR) {
        return 0;
    }

    for (uint32_t i = 0; i < dir_inode.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode.direct_blocks[i], block_data);

        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num == 0) {
                continue;
            }
            if (strcmp(entries[j].name, ".") == 0 || strcmp(entries[j].name, "..") == 0) {
                continue;
            }
            return 0;
        }
    }

    return 1;
}

static void release_inode_data(inode_t *inode) {
    for (uint32_t i = 0; i < inode->blocks && i < 12; i++) {
        free_block(inode->direct_blocks[i]);
        inode->direct_blocks[i] = 0;
    }
    inode->blocks = 0;
    inode->size = 0;
}

static int truncate_inode(uint32_t inode_num, inode_t *inode, off_t size) {
    if (size < 0) {
        return -EINVAL;
    }

    size_t new_size = (size_t)size;
    if (new_size > 12 * BLOCK_SIZE) {
        return -ENOSPC;
    }

    size_t old_size = inode->size;
    uint32_t old_blocks = inode->blocks;
    uint32_t needed_blocks = (new_size == 0) ? 0 : (uint32_t)((new_size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    if (needed_blocks > 12) {
        return -ENOSPC;
    }

    while (inode->blocks < needed_blocks) {
        int new_block = alloc_block();
        if (new_block == -1) {
            while (inode->blocks > old_blocks) {
                inode->blocks--;
                free_block(inode->direct_blocks[inode->blocks]);
                inode->direct_blocks[inode->blocks] = 0;
            }
            return -ENOSPC;
        }
        inode->direct_blocks[inode->blocks] = (uint32_t)new_block;
        inode->blocks++;
    }

    while (inode->blocks > needed_blocks) {
        inode->blocks--;
        free_block(inode->direct_blocks[inode->blocks]);
        inode->direct_blocks[inode->blocks] = 0;
    }

    // Ensure gaps created by extension are zeroed in partial edge blocks.
    if (new_size > old_size) {
        size_t pos = old_size;
        while (pos < new_size) {
            size_t block_index = pos / BLOCK_SIZE;
            size_t offset_in_block = pos % BLOCK_SIZE;
            size_t to_zero = BLOCK_SIZE - offset_in_block;
            if (to_zero > new_size - pos) {
                to_zero = new_size - pos;
            }

            char block_data[BLOCK_SIZE];
            read_block(inode->direct_blocks[block_index], block_data);
            memset(block_data + offset_in_block, 0, to_zero);
            write_block(inode->direct_blocks[block_index], block_data);
            pos += to_zero;
        }
    }

    inode->size = (uint32_t)new_size;
    inode->modified = time(NULL);
    inode->accessed = time(NULL);
    write_inode(inode_num, inode);

    return 0;
}

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
    (void)offset;
    (void)fi;

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

    for (uint32_t i = 0; i < dir_inode.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode.direct_blocks[i], block_data);

        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num == 0) {
                continue;
            }
            if (strcmp(entries[j].name, ".") == 0 || strcmp(entries[j].name, "..") == 0) {
                continue;
            }
            filler(buf, entries[j].name, NULL, 0);
        }
    }

    return 0;
}

// ---------------- MKDIR ----------------
static int my_mkdir(const char *path, mode_t mode) {
    (void)mode;
    printf("MKDIR: %s\n", path);

    char parent_path[MAX_NAME_LEN * 4];
    char dir_name[MAX_NAME_LEN + 1];

    int split_rc = split_parent_child(path, parent_path, sizeof(parent_path),
                                      dir_name, sizeof(dir_name));
    if (split_rc != 0) {
        return split_rc;
    }

    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    if (parent_inode.type != FT_DIR) {
        return -ENOTDIR;
    }

    if (find_dir_entry(parent_inode_num, dir_name) != (uint32_t)-1) {
        return -EEXIST;
    }

    uint32_t new_inode_num = alloc_inode();
    if (new_inode_num == (uint32_t)-1) {
        return -ENOSPC;
    }

    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.type = FT_DIR;
    new_inode.created = time(NULL);
    new_inode.modified = time(NULL);
    new_inode.accessed = time(NULL);
    new_inode.nlinks = 2;

    write_inode(new_inode_num, &new_inode);

    if (add_dir_entry(new_inode_num, ".", new_inode_num) != 0 ||
        add_dir_entry(new_inode_num, "..", parent_inode_num) != 0 ||
        add_dir_entry(parent_inode_num, dir_name, new_inode_num) != 0) {
        release_inode_data(&new_inode);
        free_inode(new_inode_num);
        return -ENOSPC;
    }

    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    parent_inode.nlinks++;
    write_inode(parent_inode_num, &parent_inode);

    return 0;
}

// ---------------- CREATE ----------------
static int my_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)mode;
    (void)fi;

    printf("CREATE: %s\n", path);

    char parent_path[MAX_NAME_LEN * 4];
    char file_name[MAX_NAME_LEN + 1];

    int split_rc = split_parent_child(path, parent_path, sizeof(parent_path),
                                      file_name, sizeof(file_name));
    if (split_rc != 0) {
        return split_rc;
    }

    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    if (parent_inode.type != FT_DIR) {
        return -ENOTDIR;
    }

    if (find_dir_entry(parent_inode_num, file_name) != (uint32_t)-1) {
        return -EEXIST;
    }

    uint32_t new_inode_num = alloc_inode();
    if (new_inode_num == (uint32_t)-1) {
        return -ENOSPC;
    }

    inode_t new_inode;
    memset(&new_inode, 0, sizeof(inode_t));
    new_inode.type = FT_REG;
    new_inode.created = time(NULL);
    new_inode.modified = time(NULL);
    new_inode.accessed = time(NULL);
    new_inode.nlinks = 1;

    write_inode(new_inode_num, &new_inode);

    if (add_dir_entry(parent_inode_num, file_name, new_inode_num) != 0) {
        free_inode(new_inode_num);
        return -ENOSPC;
    }

    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    write_inode(parent_inode_num, &parent_inode);

    return 0;
}

// ---------------- OPEN ----------------
static int my_open(const char *path, struct fuse_file_info *fi) {
    (void)fi;

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

    inode.accessed = time(NULL);
    write_inode(inode_num, &inode);

    return 0;
}

// ---------------- READ ----------------
static int my_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    (void)fi;

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

    inode.accessed = time(NULL);
    write_inode(inode_num, &inode);

    return read_file_data(&inode, buf, size, offset);
}

// ---------------- WRITE ----------------
static int my_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    (void)fi;

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

// ---------------- TRUNCATE ----------------
static int my_truncate(const char *path, off_t size) {
    printf("TRUNCATE: %s -> %ld\n", path, (long)size);

    uint32_t inode_num = path_to_inode(path);
    if (inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t inode;
    read_inode(inode_num, &inode);
    if (inode.type != FT_REG) {
        return -EISDIR;
    }

    return truncate_inode(inode_num, &inode, size);
}

// ---------------- UTIMENS ----------------
static int my_utimens(const char *path, const struct timespec tv[2]) {
    printf("UTIMENS: %s\n", path);

    uint32_t inode_num = path_to_inode(path);
    if (inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t inode;
    read_inode(inode_num, &inode);

    time_t now = time(NULL);
    if (tv == NULL) {
        inode.accessed = now;
        inode.modified = now;
    } else {
#ifdef UTIME_OMIT
        if (tv[0].tv_nsec != UTIME_OMIT) {
#ifdef UTIME_NOW
            inode.accessed = (tv[0].tv_nsec == UTIME_NOW) ? now : tv[0].tv_sec;
#else
            inode.accessed = tv[0].tv_sec;
#endif
        }
        if (tv[1].tv_nsec != UTIME_OMIT) {
#ifdef UTIME_NOW
            inode.modified = (tv[1].tv_nsec == UTIME_NOW) ? now : tv[1].tv_sec;
#else
            inode.modified = tv[1].tv_sec;
#endif
        }
#else
        inode.accessed = tv[0].tv_sec;
        inode.modified = tv[1].tv_sec;
#endif
    }

    write_inode(inode_num, &inode);
    return 0;
}

// ---------------- UNLINK ----------------
static int my_unlink(const char *path) {
    printf("UNLINK: %s\n", path);

    if (strcmp(path, "/") == 0) {
        return -EISDIR;
    }

    char parent_path[MAX_NAME_LEN * 4];
    char file_name[MAX_NAME_LEN + 1];

    int split_rc = split_parent_child(path, parent_path, sizeof(parent_path),
                                      file_name, sizeof(file_name));
    if (split_rc != 0) {
        return split_rc;
    }

    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    uint32_t file_inode_num = find_dir_entry(parent_inode_num, file_name);
    if (file_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t file_inode;
    read_inode(file_inode_num, &file_inode);
    if (file_inode.type != FT_REG) {
        return -EISDIR;
    }

    remove_dir_entry(parent_inode_num, file_name);
    release_inode_data(&file_inode);
    free_inode(file_inode_num);

    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    write_inode(parent_inode_num, &parent_inode);

    return 0;
}

// ---------------- RMDIR ----------------
static int my_rmdir(const char *path) {
    printf("RMDIR: %s\n", path);

    if (strcmp(path, "/") == 0) {
        return -EBUSY;
    }

    char parent_path[MAX_NAME_LEN * 4];
    char dir_name[MAX_NAME_LEN + 1];

    int split_rc = split_parent_child(path, parent_path, sizeof(parent_path),
                                      dir_name, sizeof(dir_name));
    if (split_rc != 0) {
        return split_rc;
    }

    uint32_t parent_inode_num = path_to_inode(parent_path);
    if (parent_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    uint32_t dir_inode_num = find_dir_entry(parent_inode_num, dir_name);
    if (dir_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t dir_inode;
    read_inode(dir_inode_num, &dir_inode);
    if (dir_inode.type != FT_DIR) {
        return -ENOTDIR;
    }

    if (!dir_is_empty(dir_inode_num)) {
        return -ENOTEMPTY;
    }

    remove_dir_entry(parent_inode_num, dir_name);
    release_inode_data(&dir_inode);
    free_inode(dir_inode_num);

    inode_t parent_inode;
    read_inode(parent_inode_num, &parent_inode);
    parent_inode.modified = time(NULL);
    parent_inode.accessed = time(NULL);
    if (parent_inode.nlinks > 2) {
        parent_inode.nlinks--;
    }
    write_inode(parent_inode_num, &parent_inode);

    return 0;
}

// ---------------- RENAME ----------------
static int my_rename(const char *from, const char *to) {
    printf("RENAME: %s -> %s\n", from, to);

    if (strcmp(from, to) == 0) {
        return 0;
    }

    if (strcmp(from, "/") == 0 || strcmp(to, "/") == 0) {
        return -EINVAL;
    }

    // Prevent moving a directory under itself (e.g. /a -> /a/b).
    size_t from_len = strlen(from);
    if (strncmp(to, from, from_len) == 0 && to[from_len] == '/') {
        return -EINVAL;
    }

    char from_parent_path[MAX_NAME_LEN * 4];
    char from_name[MAX_NAME_LEN + 1];
    char to_parent_path[MAX_NAME_LEN * 4];
    char to_name[MAX_NAME_LEN + 1];

    int split_from_rc = split_parent_child(from, from_parent_path, sizeof(from_parent_path),
                                           from_name, sizeof(from_name));
    if (split_from_rc != 0) {
        return split_from_rc;
    }

    int split_to_rc = split_parent_child(to, to_parent_path, sizeof(to_parent_path),
                                         to_name, sizeof(to_name));
    if (split_to_rc != 0) {
        return split_to_rc;
    }

    uint32_t from_parent_inode_num = path_to_inode(from_parent_path);
    uint32_t to_parent_inode_num = path_to_inode(to_parent_path);
    if (from_parent_inode_num == (uint32_t)-1 || to_parent_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t from_parent_inode;
    inode_t to_parent_inode;
    read_inode(from_parent_inode_num, &from_parent_inode);
    read_inode(to_parent_inode_num, &to_parent_inode);

    if (from_parent_inode.type != FT_DIR || to_parent_inode.type != FT_DIR) {
        return -ENOTDIR;
    }

    uint32_t src_inode_num = find_dir_entry(from_parent_inode_num, from_name);
    if (src_inode_num == (uint32_t)-1) {
        return -ENOENT;
    }

    inode_t src_inode;
    read_inode(src_inode_num, &src_inode);

    uint32_t dst_inode_num = find_dir_entry(to_parent_inode_num, to_name);
    if (dst_inode_num != (uint32_t)-1) {
        inode_t dst_inode;
        read_inode(dst_inode_num, &dst_inode);

        if (src_inode.type == FT_DIR && dst_inode.type != FT_DIR) {
            return -ENOTDIR;
        }
        if (src_inode.type == FT_REG && dst_inode.type == FT_DIR) {
            return -EISDIR;
        }

        if (dst_inode.type == FT_DIR) {
            if (!dir_is_empty(dst_inode_num)) {
                return -ENOTEMPTY;
            }
            remove_dir_entry(to_parent_inode_num, to_name);
            release_inode_data(&dst_inode);
            free_inode(dst_inode_num);
            if (to_parent_inode.nlinks > 2) {
                to_parent_inode.nlinks--;
            }
        } else {
            remove_dir_entry(to_parent_inode_num, to_name);
            release_inode_data(&dst_inode);
            free_inode(dst_inode_num);
        }
    }

    remove_dir_entry(from_parent_inode_num, from_name);
    if (add_dir_entry(to_parent_inode_num, to_name, src_inode_num) != 0) {
        // Best-effort rollback.
        add_dir_entry(from_parent_inode_num, from_name, src_inode_num);
        return -ENOSPC;
    }

    if (src_inode.type == FT_DIR && from_parent_inode_num != to_parent_inode_num) {
        // Update '..' entry inside moved directory.
        remove_dir_entry(src_inode_num, "..");
        add_dir_entry(src_inode_num, "..", to_parent_inode_num);

        if (from_parent_inode.nlinks > 2) {
            from_parent_inode.nlinks--;
        }
        to_parent_inode.nlinks++;
    }

    from_parent_inode.modified = time(NULL);
    from_parent_inode.accessed = time(NULL);
    to_parent_inode.modified = time(NULL);
    to_parent_inode.accessed = time(NULL);
    write_inode(from_parent_inode_num, &from_parent_inode);
    write_inode(to_parent_inode_num, &to_parent_inode);

    src_inode.modified = time(NULL);
    write_inode(src_inode_num, &src_inode);

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
    .truncate = my_truncate,
    .utimens = my_utimens,
    .rename = my_rename,
    .unlink = my_unlink,
    .rmdir = my_rmdir,
};

// ---------------- HELPERS ----------------
uint32_t path_to_inode(const char *path) {
    if (strcmp(path, "/") == 0) {
        return 0;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        return (uint32_t)-1;
    }

    char *token = strtok(path_copy + 1, "/");
    uint32_t current_inode = 0;

    while (token) {
        current_inode = find_dir_entry(current_inode, token);
        if (current_inode == (uint32_t)-1) {
            free(path_copy);
            return (uint32_t)-1;
        }
        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current_inode;
}

int add_dir_entry(uint32_t dir_inode, const char *name, uint32_t inode_num) {
    inode_t dir_inode_data;
    read_inode(dir_inode, &dir_inode_data);

    if (dir_inode_data.type != FT_DIR) {
        return -ENOTDIR;
    }

    if (strlen(name) > MAX_NAME_LEN) {
        return -ENAMETOOLONG;
    }

    for (uint32_t i = 0; i < dir_inode_data.blocks && i < 12; i++) {
        char block_data[BLOCK_SIZE];
        read_block(dir_inode_data.direct_blocks[i], block_data);

        dir_entry *entries = (dir_entry *)block_data;
        for (int j = 0; j < MAX_DIRENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num == 0) {
                entries[j].inode_num = inode_num;
                strncpy(entries[j].name, name, MAX_NAME_LEN);
                entries[j].name[MAX_NAME_LEN] = '\0';
                write_block(dir_inode_data.direct_blocks[i], block_data);
                return 0;
            }
        }
    }

    if (dir_inode_data.blocks >= 12) {
        return -ENOSPC;
    }

    int new_block = alloc_block();
    if (new_block == -1) {
        return -ENOSPC;
    }

    dir_inode_data.direct_blocks[dir_inode_data.blocks] = (uint32_t)new_block;
    dir_inode_data.blocks++;
    dir_inode_data.size += BLOCK_SIZE;

    char block_data[BLOCK_SIZE] = {0};
    dir_entry *entries = (dir_entry *)block_data;
    entries[0].inode_num = inode_num;
    strncpy(entries[0].name, name, MAX_NAME_LEN);
    entries[0].name[MAX_NAME_LEN] = '\0';

    write_block(new_block, block_data);
    write_inode(dir_inode, &dir_inode_data);

    return 0;
}

int remove_dir_entry(uint32_t dir_inode, const char *name) {
    inode_t dir_inode_data;
    read_inode(dir_inode, &dir_inode_data);

    if (dir_inode_data.type != FT_DIR) {
        return -ENOTDIR;
    }

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

    if (dir_inode_data.type != FT_DIR) {
        return (uint32_t)-1;
    }

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

    return (uint32_t)-1;
}

int read_file_data(inode_t *inode, char *buf, size_t size, off_t offset) {
    if (offset < 0) {
        return -EINVAL;
    }

    if ((size_t)offset >= inode->size) {
        return 0;
    }

    if ((size_t)offset + size > inode->size) {
        size = inode->size - (size_t)offset;
    }

    size_t bytes_read = 0;
    size_t block_offset = (size_t)offset / BLOCK_SIZE;
    size_t byte_offset = (size_t)offset % BLOCK_SIZE;

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

    return (int)bytes_read;
}

int write_file_data(inode_t *inode, const char *buf, size_t size, off_t offset) {
    if (offset < 0) {
        return -EINVAL;
    }

    if ((size_t)offset > 12 * BLOCK_SIZE) {
        return -ENOSPC;
    }

    size_t new_size = (size_t)offset + size;
    if (new_size > 12 * BLOCK_SIZE) {
        return -ENOSPC;
    }

    size_t needed_blocks = (new_size == 0) ? 0 : (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (needed_blocks > 12) {
        return -ENOSPC;
    }

    uint32_t old_blocks = inode->blocks;
    while (inode->blocks < needed_blocks) {
        int new_block = alloc_block();
        if (new_block == -1) {
            while (inode->blocks > old_blocks) {
                inode->blocks--;
                free_block(inode->direct_blocks[inode->blocks]);
                inode->direct_blocks[inode->blocks] = 0;
            }
            return -ENOSPC;
        }
        inode->direct_blocks[inode->blocks] = (uint32_t)new_block;
        inode->blocks++;
    }

    // Fill any hole between old EOF and write offset with zeroes.
    if ((size_t)offset > inode->size) {
        size_t pos = inode->size;
        while (pos < (size_t)offset) {
            size_t block_index = pos / BLOCK_SIZE;
            size_t offset_in_block = pos % BLOCK_SIZE;
            size_t to_zero = BLOCK_SIZE - offset_in_block;
            if (to_zero > (size_t)offset - pos) {
                to_zero = (size_t)offset - pos;
            }

            char block_data[BLOCK_SIZE];
            read_block(inode->direct_blocks[block_index], block_data);
            memset(block_data + offset_in_block, 0, to_zero);
            write_block(inode->direct_blocks[block_index], block_data);
            pos += to_zero;
        }
    }

    size_t bytes_written = 0;
    size_t block_offset = (size_t)offset / BLOCK_SIZE;
    size_t byte_offset = (size_t)offset % BLOCK_SIZE;

    while (bytes_written < size && block_offset < inode->blocks) {
        char block_data[BLOCK_SIZE];
        if (byte_offset == 0 && size - bytes_written >= BLOCK_SIZE) {
            memcpy(block_data, buf + bytes_written, BLOCK_SIZE);
            write_block(inode->direct_blocks[block_offset], block_data);
            bytes_written += BLOCK_SIZE;
        } else {
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
        inode->size = (uint32_t)new_size;
    }

    return (int)bytes_written;
}

// ---------------- MAIN ----------------
int main(int argc, char *argv[]) {
    if (access("disk.img", F_OK) == -1) {
        printf("Creating new disk image...\n");
        create_disk();
    } else {
        open_disk();
    }

    init_fs();
    return fuse_main(argc, argv, &operations, NULL);
}
