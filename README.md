# Flintstone Project

A low-level C-based simulation of a file system and shell environment with systems-forward architecture. Designed for educational use, Flintstone provides real design-pattern structure for file management, ASM-backed memory primitives for disk operations, and a path toward hardware-backed persistence.

## 📚 Overview

The Flintstone Project is a modular operating systems educational project written in C. It simulates key file system components, includes an interactive shell interface, and uses design patterns (Facade, Strategy, Command, Observer, Chain of Responsibility) to keep the codebase composable and testable. An x86-64 assembly layer provides performance-critical memory primitives for sector buffers and cluster operations—a stepping stone toward real FAT-style persistence.

## ✨ Features

- **Simulated disk & file system** — Cluster-based disk with hex text format; optional ASM-backed buffer I/O
- **Design-pattern file management** — Facade (FileManagerService), Provider/Strategy (Local, InMemory), Command (undoable ops), Events, Chain of Responsibility (access checks)
- **ASM memory primitives** — `asm_mem_copy`, `asm_mem_zero`, `asm_block_fill` for hot-path operations
- **Priority queue** — Multi-priority task scheduling for deferred FS work
- **Path support** — `.` (current), `..` (parent), `./foo`, normalized path resolution
- **Path log** — In-memory log of file/dir operations (`where`, `loc`)
- **Interactive & batch shell** — Thread-pooled command execution
- **Unit testing** — CUnit-based test suite

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  UI / Shell (interpreter)                                    │
│  Thin adapters; never touches filesystem directly           │
└─────────────────────────────┬───────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────┐
│  FileManagerService (Facade)                                │
│  fm_list, fm_read_text, fm_save_text, fm_create_*, fm_*     │
│  Undo stack, event publishing, validation chain              │
└─────────────────────────────┬───────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────┐
│  Commands (Execute/Undo) │ Providers (Strategy) │ Policies   │
│  fs_cmd_*, fs_provider_*, fs_chain_*, fs_events_*           │
└─────────────────────────────┬───────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────┐
│  Disk layer (disk.c, disk_asm.c) │ dir_asm (buffer ops)     │
│  mem_asm.s — ASM primitives for cluster/dir buffers         │
└─────────────────────────────────────────────────────────────┘
```

## 📁 File Structure

| File/Folder | Description |
|-------------|-------------|
| **main.c** | Entry point, batch parsing, shell init |
| **interpreter.c / .h** | Command dispatch, thin adapter to service layer |
| **common.c / .h** | Globals, help text, `g_cwd` |
| **util.c / .h** | `resolve_path`, history, `trim_whitespace` |
| **disk.c / disk.h** | Disk I/O simulation (text hex format) |
| **disk_asm.c / .h** | ASM-backed cluster read/write/zero |
| **cluster.c / .h** | Cluster management, hex conversion |
| **mem_asm.s** | x86-64 ASM: `asm_mem_copy`, `asm_mem_zero`, `asm_block_fill` |
| **dir_asm.c / .h** | ASM-backed directory buffer ops |
| **fs.c / fs.h** | Legacy FS helpers (mkdir, rmtree, cat, redirect) |
| **fs_types.h** | Domain types: `fs_node_t`, provider/command vtables |
| **fs_provider.c / .h** | `IFileSystemProvider`: Local, InMemory |
| **fs_command.c / .h** | Undoable commands: create, delete, move, write |
| **fs_facade.c / .h** | `FileManagerService` facade |
| **fs_events.c / .h** | Event bus (FileCreated, FileSaved, etc.) |
| **fs_chain.c / .h** | Chain of Responsibility for validation |
| **fs_policy.c / .h** | Access policies (e.g. protected paths) |
| **fs_service_glue.c / .h** | Service init, `g_fm_service` |
| **path_log.c / .h** | In-memory path operation log |
| **priority_queue.c / .h** | Multi-priority task queue |
| **task_manager.c / .h** | Task manager wrapper |
| **threadpool.c / .h** | Thread pool for command execution |
| **terminal.c / .h** | Raw mode terminal (interactive) |
| **Makefile** | Build (C + ASM), test target |

## 🛠️ Build & Run

### Prerequisites

- GCC compiler
- POSIX-compliant OS (Linux/macOS)
- (Optional) CUnit for tests: `sudo apt install libcunit1 libcunit1-dev`

### Build

```bash
make
```

Builds `BPForbes_Flinstone_Shell` with `mem_asm.s` linked.

### Run

```bash
./BPForbes_Flinstone_Shell
```

Interactive mode. For batch:

```bash
./BPForbes_Flinstone_Shell help
./BPForbes_Flinstone_Shell dir . where
```

### Test

```bash
make BPForbes_Flinstone_Tests
./BPForbes_Flinstone_Tests
```

## 💬 Command Reference

### Navigation

| Command | Description |
|---------|-------------|
| `cd [path]` | Change directory (no args prints current) |

### Disk Operations

| Command | Description |
|---------|-------------|
| `createdisk <volume> <rowCount> <nibbleCount> [-y\|-n]` | Create new disk file |
| `format <disk_file> <volume> <rowCount> <nibbleCount>` | Format existing disk |
| `setdisk <disk_file>` | Set disk file to use |
| `listclusters` | List disk cluster contents |
| `printdisk` | Print disk with header |
| `writecluster <index> -t\|-h <data>` | Write to cluster |
| `delcluster <index>` | Zero out cluster |
| `update <index> -t\|-h <data>` | Delete then write cluster |
| `addcluster [-t\|-h <data>]` | Append cluster |
| `initdisk <count> <size>` | Init in-memory geometry |
| `search <text> [-t\|-h]` | Search disk |
| `du [dtl [clusters...]]` | Disk usage |
| `import <textfile> <txtfile> [clusters clusterSize]` | Import drive listing |

### Directory Operations

| Command | Description |
|---------|-------------|
| `dir [path]` | List directory contents |
| `listdirs` | List directories in cwd |
| `mkdir <path>` | Create directory (parents created if needed) |
| `rmdir <path>` | Remove empty directory |
| `rmtree <path>` | Recursively remove directory |

### File Operations

| Command | Description |
|---------|-------------|
| `make <filename>` | Create file interactively (end with `EOF`) |
| `write <file> <content>` | Write content to file |
| `cat <filename>` | Display file contents |
| `type <filename>` | Same as cat |
| `mv <src> <dst>` | Move or rename file/directory |

### Other

| Command | Description |
|---------|-------------|
| `version [-y\|-n]` | Print version |
| `help` | Display help |
| `redirect <file>` | Redirect output (`redirect off` to restore) |
| `rerun <N>` | Re-run Nth history command |
| `where [N]`, `loc [N]` | Show path log (last N ops, default 16) |
| `exit [-y\|-n]` | Exit shell |
| `clear` | Clear screen |
| `history`, `his` | Show command history |
| `cc` | Clear history |

### Path Support

- `.` — current directory
- `..` — parent directory
- `./foo` — foo in current directory
- Paths are normalized (e.g. `a/b/../c` → `a/c`)

### Batch Shortcut

```bash
./BPForbes_Flinstone_Shell <volume> <rows> <nibbles> [-y]
```

Equivalent to `createdisk` with the same parameters.

## 🖥️ Example Shell Session

```shell
cd /workspace
mkdir projects/sub
cd projects
write sub/readme.txt "Hello Flintstone!"
cat sub/readme.txt
dir .
mv sub/readme.txt readme.txt
where 5
```

## ⚖️ Legal and Ethical Notice

This project was developed in an academic setting under a legal contract and with all proper permissions. It is provided strictly for educational and research purposes. Unauthorized duplication or malicious use may violate laws such as the Computer Fraud and Abuse Act (CFAA).

## 👤 Author

**Bailey Forbes**  
Computer Science B.S., Indiana University Southeast (Alumnus)  
📧 baileyforbes@rocketmail.com

## 🪪 License

MIT License — See `LICENSE` for details.
