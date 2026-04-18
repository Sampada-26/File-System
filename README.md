# Simple FUSE File System (C)

## 🚀 Overview

This project demonstrates a basic file system using FUSE (Filesystem in Userspace).

## 📁 Features

* Custom virtual filesystem
* One file: `hello.txt`
* Supports:

  * `ls`
  * `cat`

## 🧱 Tech Stack

* C
* FUSE (libfuse)

## ⚙️ Setup

```bash
sudo apt install libfuse-dev fuse pkg-config
make
mkdir mountdir
./fusefs mountdir
```

## 🧪 Usage

```bash
cd mountdir
ls
cat hello.txt
```

## 🧹 Unmount

```bash
fusermount -u mountdir
```

## 📌 Output

```
hello.txt
Hello from FUSE filesystem!
```

## 📚 Learnings

* FUSE architecture
* File system operations (getattr, read, readdir)
* Kernel-user space interaction

## ⭐ Future Improvements

* Add write support
* Add multiple files
* Add directory hierarchy
