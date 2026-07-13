╔══ ╸X V 6 ╸ ══╗
║  [OS KERNEL]  ║
╚════════════════╝

Welcome to **My Custom Xv6** — a feature-packed fork of MIT's xv6 teaching
operating system for RISC-V. Built on the classic Unix V6 re-implementation,
this fork extends the kernel with **on-board language interpreters**, a text
editor, a login system, and more, all running directly in the OS.

---

## ✨ What's New

### 🧠 On-Board Language Interpreters

| Program | Description |
|---------|-------------|
| **`c4`** | A tiny **C compiler in 4 functions** — parses and executes a large subset of C, enough to be self-hosting. Now uses **AST-based code generation** (two-pass: parse → AST → bytecode walk). Ported from Robert Swierczek's c4. |
| **`forth`** | A complete **Forth interpreter** with data/return stacks, a dictionary, memory access (`@`, `!`, `c@`, `c!`), I/O port operations, and interactive debugging. |
| **`bf`** | A **Brainfuck interpreter** — load and run any `.bf` program on a 30,000-cell tape. |

### 📝 Editing & Tools

| Program | Description |
|---------|-------------|
| **`ed`** | A line-oriented text editor in the classic Unix `ed` tradition — open, edit, and save files right from the shell. |
| **`logo`** | Prints the X V6 ASCII logo banner to the terminal. |
| **`clear`** | Clears the terminal screen. |

### 🔐 Login System

The shell is protected by a built-in **login module** (`login.c`). When the
system boots, you are prompted for credentials:

```
login: root
passwd: root
```

Login succeeds → spawns the shell. Shell exits → login prompt loops again.

### ⚙️ Kernel Enhancements

- **`sync` syscall** — Flush the filesystem log to disk, ensuring data
  durability. A userspace `sync` command is provided.
- **`printk` renamed from `printf`** — Kernel print function renamed to
  `printk` to avoid name collision with the userspace `printf`, following
  Linux kernel convention.
- **Custom boot banner** — Displays the X V6 logo on startup.
- **Both `sbrk` and `sbrklazy`** — Standard and lazy memory allocation
  available to user programs.

### 🧪 Testing & Stress

| Program | Description |
|---------|-------------|
| **`usertests`** | The full xv6 user-space test suite (66,903 lines). |
| **`logstress`** | Stress-tests the journaling filesystem by having multiple processes write concurrently. |
| **`forphan`** | Creates an orphaned file and tests filesystem recovery. |
| **`dorphan`** | Creates an orphaned directory and tests filesystem recovery. |
| **`crash tests`** | Automated QEMU-based crash/recovery testing via `test-xv6.py`. |

---

## 🏗️ Project Structure

```
├── kernel/        # The operating system kernel
│   ├── proc.c     # Process management
│   ├── vm.c       # Virtual memory
│   ├── fs.c       # File system
│   ├── log.c      # Journaling layer (+ sys_sync)
│   ├── syscall.c  # System call dispatch
│   ├── trap.c     # Trap handling
│   └── ...
├── user/          # User-space programs
│   ├── c4.c       # C compiler in 4 functions
│   ├── forth.c    # Forth interpreter
│   ├── bf.c       # Brainfuck interpreter
│   ├── ed.c       # Text editor
│   ├── login.c    # Login authentication
│   ├── sh.c       # Shell
│   ├── ls.c       # Directory listing
│   ├── cat.c      # File concatenation
│   ├── grep.c     # Text search
│   ├── usertests.c # Comprehensive test suite
│   └── ...
├── mkfs/          # Filesystem image builder
├── test-xv6.py    # Automated QEMU testing script
└── Makefile       # Build system
```

---

## 🚀 Getting Started

### Prerequisites

- **RISC-V toolchain** — either `riscv64-unknown-elf-` or
  `riscv64-linux-gnu-` from
  [riscv-gnu-toolchain](https://github.com/riscv/riscv-gnu-toolchain)
- **QEMU** — `qemu-system-riscv64` (minimum version 7.2)

### Build & Run

```bash
make qemu
```

This compiles the kernel and user programs, builds the filesystem image, and
boots xv6 inside QEMU.

### Other Commands

| Command | Description |
|---------|-------------|
| `make clean` | Remove build artifacts |
| `make fs.img` | Build the filesystem image only |
| `make kernel/kernel` | Build the kernel binary only |
| `make fmt` | Format all source code with clang-format |

### Automated Testing

```bash
./test-xv6.py usertests    # Run the full user test suite
./test-xv6.py -q usertests  # Run quick tests only
./test-xv6.py crash         # Run crash / recovery tests
./test-xv6.py log           # Run log crash test
```

---

## 🧑‍💻 Using the Language Interpreters

### C4 — Run C programs inside xv6

```bash
c4 hello.c        # Run a C source file
c4 -s hello.c     # Show source + opcode listing
c4 -d hello.c     # Debug mode: print every executed instruction
```

c4 can compile and execute a useful subset of C directly — no cross-compiler
needed on your host. It features **AST-based code generation**: a two-pass
compiler that builds a full AST first, then walks it to emit bytecodes.
Supports `if`/`else`, `while`, `return`, compound statements, local/global
variables, pointers, arrays (`a[i]`), `sizeof`, `enum`, function calls,
and nested expressions with correct precedence.

### Forth — Interactive hardware debugger

```bash
forth
```

Drops you into an interactive Forth environment. You can define words,
inspect memory, and manipulate I/O ports — useful for low-level
experimentation.

```forth
: square dup * ;
3 square .
```

### Brainfuck

```bash
bf program.bf
```

Loads a Brainfuck source file and runs it on a 30,000-cell tape.

---

## 📚 Background

**Xv6** is a modern re-implementation of Dennis Ritchie's and Ken Thompson's
**Unix Version 6**, written in ANSI C for RISC-V multiprocessors. It was
created at MIT for the 6.1810 / 6.S081 operating systems engineering course.
It loosely follows the structure and style of the original v6 and is inspired
by John Lions' *Commentary on UNIX 6th Edition*.

This fork builds on the [official xv6-riscv](https://github.com/mit-pdos/xv6-riscv)
by adding language runtimes, developer tools, and quality-of-life improvements
while keeping the kernel lean enough to serve as a teaching platform.

---

## 🙏 Acknowledgments

This project is based on MIT's xv6, created by Frans Kaashoek, Robert Morris,
and the 6.1810 staff. Special thanks to all xv6 contributors.

The c4 interpreter is by Robert Swierczek, ported to xv6-riscv.

For more information, see <https://pdos.csail.mit.edu/6.1810/>.

---

## 📬 Issues & Contributions

Found a bug or have an idea? Open an issue or send a pull request.
