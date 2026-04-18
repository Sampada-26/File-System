# Simple FUSE File System (C)

## 🚀 Overview

This project demonstrates a basic user-space filesystem using FUSE (Filesystem in Userspace).
It mounts a virtual filesystem with one file, `hello.txt`, and supports simple read operations.

## 📁 Project Structure

```
./
├── disk.c
├── fs.h
├── fusefs
├── main.c
├── Makefile
├── README.md
└── mountdir/       # mount point directory for testing
```

## 🧱 Prerequisites (Ubuntu)

Install the required packages before building and running the project:

```bash
sudo apt update
sudo apt install -y libfuse-dev fuse pkg-config build-essential
```

> On Ubuntu, `build-essential` provides `gcc`, `make`, and other common build tools.

## ⚙️ Build and Run

1. Build the filesystem binary:

```bash
make
```

2. Create the mount point directory if it does not exist:

```bash
mkdir -p mountdir
```

3. Run the FUSE filesystem:

```bash
./fusefs mountdir
```

4. Open a new terminal or background the process, then access the mounted filesystem:

```bash
cd mountdir
ls
cat hello.txt
```

## 🧹 Unmount

When you are done, unmount the filesystem:

```bash
fusermount -u mountdir
```

## 📌 Expected Output

```bash
hello.txt
Hello from FUSE filesystem!
```

## 💡 Notes

* `fusefs` must run with FUSE available on the system.
* If `./fusefs mountdir` does not return, it is running in foreground and keeping the filesystem mounted.
* Use `fusermount -u mountdir` to cleanly unmount before deleting `mountdir`.

## ⭐ Future Improvements

* Add write support
* Add multiple files
* Add directory hierarchy
* Implement deletion and rename operations
