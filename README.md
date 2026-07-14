
# XV6  --OS KERNEL

***write by Shawn-Yu-Dev***

**My Custom Xv6** — a feature-packed fork of MIT's xv6 teaching OS for RISC-V.
It extends the classic kernel with **on-board language interpreters**, a text
editor, a login system, and quality-of-life enhancements — all running
directly in the OS, no cross-compiler needed.

---

## ✨ What's New

### 🧠 On-Board Language Interpreters

| Program | Description |
|---------|-------------|
| **`c4`** | A tiny C compiler in 4 functions — parses and executes a large subset of C, enough to be self-hosting. Uses **AST-based code generation** (two-pass: parse → AST → bytecode walk). Ported from Robert Swierczek's c4. |
| **`forth`** | A complete Forth interpreter with data/return stacks, dictionary, memory access (`@`, `!`, `c@`, `c!`), I/O port operations, and interactive debugging. |
| **`bf`** | A Brainfuck interpreter — load and run any `.bf` program on a 30,000-cell tape. |

### 📝 Tools

| Program | Description |
|---------|-------------|
| **`ed`** | The classic line-oriented Unix text editor — edit files right from the shell. |
| **`logo`** | Prints the X V6 ASCII banner. |
| **`clear`** | Clears the terminal screen. |

### 🔐 Login System

The shell is protected by a built-in login module. On boot:

```
login: root
passwd: [redacted]
```

Login spawns the shell; exiting the shell returns to the login prompt.

### ⚙️ Kernel Enhancements

- **`sync` syscall** — Flush the filesystem log to disk for data durability. A userspace `sync` command is provided.
- **`printk`** — Kernel print function renamed from `printf` to avoid name collision with userspace `printf`, following Linux kernel convention.
- **Custom boot banner** — Displays the X V6 logo on startup.
- **`sbrk` + `sbrklazy`** — Both standard and lazy memory allocation available to user programs.

### 🧪 Testing

| Program | Description |
|---------|-------------|
| **`usertests`** | The full xv6 user-space test suite. |
| **`logstress`** | Stress-tests the journaling filesystem with concurrent writers. |
| **`forphan` / `dorphan`** | Creates orphaned files/directories to test filesystem recovery. |
| **`test-xv6.py`** | Automated QEMU-based testing script. |

---

## 🏗️ Project Structure

```
├── kernel/        # Operating system kernel
│   ├── proc.c     # Process management
│   ├── vm.c       # Virtual memory
│   ├── fs.c       # File system
│   ├── log.c      # Journaling layer (+ sys_sync)
│   ├── syscall.c  # System call dispatch
│   ├── trap.c     # Trap handling
│   └── ...
├── user/          # User-space programs
│   ├── c4.c       # C compiler
│   ├── forth.c    # Forth interpreter
│   ├── bf.c       # Brainfuck interpreter
│   ├── ed.c       # Line editor
│   ├── login.c    # Login authentication
│   ├── sh.c       # Shell
│   ├── usertests.c  # Test suite
│   └── ...
├── mkfs/          # Filesystem image builder
├── test-xv6.py    # Automated testing
└── Makefile       # Build system
```

---

## 🚀 Getting Started

### Prerequisites

- **RISC-V toolchain** — `riscv64-unknown-elf-` or `riscv64-linux-gnu-` from
  [riscv-gnu-toolchain](https://github.com/riscv/riscv-gnu-toolchain)
- **QEMU** — `qemu-system-riscv64` (minimum v7.2)

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
| `make fs.img` | Build filesystem image only |
| `make kernel/kernel` | Build kernel binary only |
| `make fmt` | Format all source code with clang-format |

### Automated Testing

```bash
./test-xv6.py usertests      # Run full user test suite
./test-xv6.py -q usertests   # Quick tests only
./test-xv6.py crash          # Crash / recovery tests
./test-xv6.py log            # Log crash test
```

---

## 🧑‍💻 Using the Interpreters

### C4 — Run C programs inside xv6

```bash
c4 hello.c         # Run a C source file
c4 -s hello.c      # Show source + opcode listing
c4 -d hello.c      # Debug mode: print every executed instruction
```

c4 compiles and executes a useful subset of C directly — no cross-compiler
needed on your host. It features **AST-based code generation**: a two-pass
compiler that builds a full AST, then walks it to emit bytecodes. Supports
`if`/`else`, `while`, `return`, compound statements, local/global variables,
pointers, arrays (`a[i]`), `sizeof`, `enum`, function calls, and nested
expressions with correct precedence.

### Forth — Interactive low-level playground

```bash
forth
```

Drop into an interactive Forth environment. Define words, inspect memory,
and manipulate I/O ports.

```forth
: square dup * ;
3 square .
```

### Brainfuck

```bash
bf program.bf
```

Loads and runs a Brainfuck program on a 30,000-cell tape.

---

## 📚 Background

**Xv6** is a modern re-implementation of Dennis Ritchie's and Ken Thompson's
**Unix Version 6** (v6), written in ANSI C for RISC-V multiprocessors.
Created at MIT for the 6.1810 / 6.S081 operating systems engineering course,
it loosely follows the structure and style of the original v6 and is inspired
by John Lions' *Commentary on UNIX 6th Edition*.

This fork builds on the
[official xv6-riscv](https://github.com/mit-pdos/xv6-riscv) by adding
language runtimes, developer tools, and quality-of-life improvements while
keeping the kernel lean enough to serve as a teaching platform.

---

## 🙏 Acknowledgments

This project is based on MIT's xv6, created by Frans Kaashoek, Robert Morris,
and the 6.1810 staff.

The `c4` compiler is by Robert Swierczek, ported to xv6-riscv.

For more information: <https://pdos.csail.mit.edu/6.1810/>

---

## 📬 Issues & Contributions

Found a bug or have an idea? Open an issue or send a pull request.
