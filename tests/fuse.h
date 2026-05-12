#ifndef TEST_FUSE_H
#define TEST_FUSE_H

#include <sys/types.h>

struct stat;

struct fuse_file_info {
    int flags;
};

enum fuse_fill_dir_flags {
    FUSE_FILL_DIR_PLUS = 1
};

enum fuse_readdir_flags {
    FUSE_READDIR_PLUS = 1
};

typedef int (*fuse_fill_dir_t)(void *buf, const char *name,
                               const struct stat *stbuf, off_t off,
                               enum fuse_fill_dir_flags flags);

struct fuse_operations {
    int (*getattr)(const char *, struct stat *, struct fuse_file_info *);
    int (*readdir)(const char *, void *, fuse_fill_dir_t, off_t,
                   struct fuse_file_info *, enum fuse_readdir_flags);
    int (*mkdir)(const char *, mode_t);
    int (*create)(const char *, mode_t, struct fuse_file_info *);
    int (*open)(const char *, struct fuse_file_info *);
    int (*read)(const char *, char *, size_t, off_t, struct fuse_file_info *);
    int (*write)(const char *, const char *, size_t, off_t,
                 struct fuse_file_info *);
    int (*truncate)(const char *, off_t, struct fuse_file_info *);
    int (*utimens)(const char *, const struct timespec tv[2],
                   struct fuse_file_info *);
    int (*rename)(const char *, const char *, unsigned int);
    int (*unlink)(const char *);
    int (*rmdir)(const char *);
};

static inline int fuse_main(int argc, char *argv[],
                            const struct fuse_operations *op, void *user_data) {
    (void)argc;
    (void)argv;
    (void)op;
    (void)user_data;
    return 0;
}

#endif
