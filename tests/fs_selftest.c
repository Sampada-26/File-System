#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern FILE *disk;

#define main fusefs_mount_main
#include "../main.c"
#undef main

static void expect_ok(const char *label, int rc) {
    if (rc < 0) {
        fprintf(stderr, "%s failed: %d\n", label, rc);
        exit(1);
    }
}

static void expect_eq(const char *label, int got, int want) {
    if (got != want) {
        fprintf(stderr, "%s failed: got %d, want %d\n", label, got, want);
        exit(1);
    }
}

int main(void) {
    struct fuse_file_info fi;
    struct stat st;
    char buf[64];

    memset(&fi, 0, sizeof(fi));

    unlink("disk.img");
    create_disk();
    init_fs();

    expect_ok("mkdir /docs", my_mkdir("/docs", 0755));
    expect_ok("create /docs/todo.txt", my_create("/docs/todo.txt", 0644, &fi));

    expect_eq("write /docs/todo.txt",
              my_write("/docs/todo.txt", "hello", 5, 0, &fi), 5);

    memset(buf, 0, sizeof(buf));
    expect_eq("read /docs/todo.txt", my_read("/docs/todo.txt", buf, 5, 0, &fi), 5);
    if (memcmp(buf, "hello", 5) != 0) {
        fprintf(stderr, "read content mismatch\n");
        return 1;
    }

    expect_ok("getattr /docs/todo.txt", my_getattr("/docs/todo.txt", &st, &fi));
    expect_eq("file size", (int)st.st_size, 5);

    expect_ok("rename /docs/todo.txt -> /notes.txt",
              my_rename("/docs/todo.txt", "/notes.txt", 0));
    expect_ok("truncate /notes.txt", my_truncate("/notes.txt", 2, &fi));

    memset(buf, 0, sizeof(buf));
    expect_eq("read /notes.txt", my_read("/notes.txt", buf, sizeof(buf), 0, &fi), 2);
    if (memcmp(buf, "he", 2) != 0) {
        fprintf(stderr, "truncated content mismatch\n");
        return 1;
    }

    expect_ok("unlink /notes.txt", my_unlink("/notes.txt"));
    expect_ok("rmdir /docs", my_rmdir("/docs"));

    fclose(disk);
    puts("filesystem self-test passed");
    return 0;
}
