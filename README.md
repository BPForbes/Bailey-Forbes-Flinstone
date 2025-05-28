# Flintstone Project

A low-level C-based simulation of a file system and shell environment. Designed for educational use, the Flintstone Project mimics disk operations, command parsing, and file I/O behaviors from scratch.

## 📚 Overview

The Flintstone Project is a modular operating systems educational project written in C. It simulates key file system components and includes an interactive shell interface. This sandbox is ideal for learning how disks, file systems, and shells interact at the systems programming level.

## ✨ Features

- 🗂️ Simulated Disk & File System  
- 💻 Interactive Shell  
- 🧵 Lightweight Thread Pool  
- 🧪 Unit Testing Framework  
- 🔧 Modular Utilities

## 📁 File Structure

| File/Folder                 | Description                               |
|----------------------------|-------------------------------------------|
| main.c                     | Entry point                               |
| disk.c / disk.h            | Disk I/O simulation                       |
| fs.c / fs.h                | File system functions                     |
| interpreter.c / .h         | Command parsing and dispatch              |
| terminal.c / .h            | Shell interface                           |
| cluster.c / .h             | Cluster & block management                |
| threadpool.c / .h          | Lightweight thread pool                   |
| common.c / .h              | Common shared utilities                   |
| util.c / .h                | Miscellaneous helpers                     |
| Makefile                   | Build automation                          |
| shell_history.txt          | Shell command history                     |
| drive.txt, test_drive_disk.txt | Sample disk files                    |
| BPForbes_Flinstone_Shell   | Compiled interactive shell binary         |
| BPForbes_Flinstone_Tests   | Compiled unit test binary                 |

## 🛠️ Build & Run

### Prerequisites

- GCC compiler  
- POSIX-compliant OS (Linux/macOS)

### Build

```bash
make
```

### Run

```bash
./BPForbes_Flinstone_Shell
```

### Test

```bash
./BPForbes_Flinstone_Tests
```

## 💬 Command Reference

### Batch/Flag Commands

| Command                                         | Description                                              |
|------------------------------------------------|----------------------------------------------------------|
| -v, -V                                          | Show version info and exit                               |
| -?, -h, help                                    | Show help message                                        |
| -l                                              | List all clusters (excluding header)                     |
| -s `<text>` -t / -h                             | Search disk for text in text (`-t`) or hex (`-h`) mode   |
| -w `<index>` -t / -h `<data>`                   | Write data to cluster at index                           |
| -d `<index>`                                    | Zero out a specific cluster                              |
| -up `<index>` -t / -h `<data>`                  | Delete and overwrite a cluster                           |
| -f `<disk_file>`                                | Specify disk file to operate on                          |
| -cd `<volume>` `<rows>` `<nibbles>` [-y &#124; -n]   | Create new disk (with optional interactive flag)   |
| -F `<file>` `<vol>` `<rows>` `<nibbles>`        | Format existing disk with random content                 |
| -D `<directory>`                                | Create a new directory                                   |
| -L                                              | List local directories                                   |
| -R `<directory>`                                | Recursively delete a directory                           |
| -du [-dtl `<clusters>`...]                      | Show disk metadata (optionally detailed)                 |
| -O `<filename>`                                 | Redirect output (use `-O off` to disable)                |
| -init `<cluster_count>` `<cluster_size>`        | Initialize memory disk geometry                          |
| -uc `<N>`                                       | Re-run Nth history command                               |
| -import `<txtfile>` `<diskfile>`                | Import textual drive listing                             |
| -print                                          | Print disk contents with header and addressing           |

### Word-Based Commands

| Command                        | Description                                     |
|--------------------------------|-------------------------------------------------|
| make `<filename>`             | Create file; end input with line `EOF`         |
| exit [-y &#124; -n]           | Exit shell (batch assumes -n if not given)     |
| clear                         | Clear the terminal                             |
| history, his                  | Display command history                        |
| cc                            | Clear history                                  |
| cat `<filename>`              | Display file contents                          |
| dir [path]                    | List directory contents                        |

### Batch Shortcut

Instead of using `-cd`, you may directly run:

```bash
./BPForbes_Flinstone_Shell <volume> <rows> <nibbles> [-y | -n]
```

Example:

```bash
./BPForbes_Flinstone_Shell mydisk 8 4 -y
```

Equivalent to: `-cd mydisk 8 4 -y`

## 🖥️ Example Shell Session

```shell
mkdir projects
cd projects
touch file.txt
write file.txt "Hello Flintstone!"
read file.txt
ls
```

## ⚖️ Legal and Ethical Notice

This project was developed in an academic setting under a legal contract and with all proper permissions. It is provided strictly for educational and research purposes. Unauthorized duplication or malicious use may violate laws such as the Computer Fraud and Abuse Act (CFAA).

## 👤 Author

**Bailey Forbes**  
Computer Science B.S., Indiana University Southeast (Alumnus)  
📧 baileyforbes@rocketmail.com

## 🪪 License

MIT License — See `LICENSE` for details.
