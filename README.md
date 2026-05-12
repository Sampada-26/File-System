# Simple FUSE File System (C)

## Overview

This project implements a small block-based filesystem in userspace with FUSE.
You can now manage files and directories manually from the Linux shell like a normal filesystem:

- Create files and directories
- Write and update file content
- Truncate files (`>` redirection)
- Rename/move files and directories
- Delete files and empty directories

## Prerequisites (Ubuntu)

```bash
sudo apt update
sudo apt install -y libfuse-dev fuse pkg-config build-essential
```

## Build

```bash
cd fuse-fs
make
```

## Run

1. Create a mount directory:

```bash
mkdir -p mountdir
```

2. Start filesystem (foreground):

```bash
./fusefs mountdir
```

3. Open another terminal and use it:

```bash
cd fuse-fs/mountdir
```

4. Disconnect/unmount filesystem (when done):

```bash
# run this from outside mountdir
fusermount -u mountdir
```

If you get a "target is busy" error, first leave the mount directory and close files using it, then retry:

```bash
cd ..
fusermount -u mountdir
```

## Manual File Operations (Linux-like)

### 1. Create files and directories

```bash
touch notes.txt
mkdir docs
touch docs/todo.txt
```

### 2. Write and update files

```bash
echo "first line" > notes.txt
cat notes.txt

echo "second line" >> notes.txt
cat notes.txt

echo "replace content" > notes.txt
cat notes.txt
```

### 3. Rename / move

```bash
mv notes.txt notes_old.txt
mv docs/todo.txt todo_root.txt
mv todo_root.txt docs/todo.txt
```

### 4. Delete

```bash
rm docs/todo.txt
rmdir docs
rm notes_old.txt
```

### 5. Quick full test sequence

```bash
touch a.txt
echo "hello" > a.txt
echo "world" >> a.txt
cat a.txt
mv a.txt b.txt
rm b.txt
```

## Unmount

From outside `mountdir`:

```bash
fusermount -u mountdir
```

## Reset filesystem state (optional)

The filesystem stores data in `disk.img`. To start from a clean disk:

```bash
fusermount -u mountdir
rm -f disk.img
./fusefs mountdir
```

## Notes

- `./fusefs mountdir` runs in foreground by default.
- If metadata layout from an old `disk.img` is incompatible, the filesystem auto-formats the image.
- Current implementation supports up to 12 direct data blocks per file.
