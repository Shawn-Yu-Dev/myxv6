
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
| **`c4`** | A tiny C compiler — parses and executes a useful subset of C, enough to be self-hosting. Split into three modules (`cvm.c` / `ast.c` / `run.c`) for clarity: the **AST parser** builds a syntax tree and emits relocatable bytecodes; the **VM** executes them. Bytecode can be saved to a `.s` file for later execution without recompilation. Ported from Robert Swierczek's c4. |
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

Credentials are read from `/passwd` on the filesystem (format: `username:password`), not hardcoded in the binary. Login spawns the shell; exiting the shell returns to the login prompt.

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
│   ├── c4/         # C compiler (split into cvm.c / ast.c / run.c)
│   │   ├── cvm.h    # Shared types, opcodes, and declarations
│   │   ├── cvm.c    # VM base registers, tokenizer, opcode table
│   │   ├── ast.c    # Parser, AST walker, code generator, main()
│   │   └── run.c    # Bytecode interpreter + .s file loader
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

**Compile / save / run separately:**

```bash
c4 hello.c                # Compile and run (backward compatible)
c4 -s hello.s hello.c     # Compile to .s bytecode file (no execution)
c4 hello.s                # Load and run from .s file (no recompilation)
c4 -d hello.c             # Debug mode: print every executed instruction
```

**Compilation is now separate from execution.** Compile once to a
human-readable `.s` bytecode file and run it many times:

```
c4 -s my.s test_simple.c
c4 my.s
```

The `.s` file is a plain-text, assembly-like format you can inspect:

```
.entry 22
.data 24
8031924123371070824 748764258399186034 ...
.code 25
ENT 1
LEA -1
PSH
IMM 42
...
JSR 0
EXIT
```

**Architecture:** Split into three modules under `user/c4/`:

| Module | File | Role |
|--------|------|------|
| **Syntax & VM** | `cvm.c` + `cvm.h` | Opcode/token definitions, tokenizer (`next()`), VM base registers and sandbox bounds |
| **Parser & Codegen** | `ast.c` | Builds full AST, emits relocatable bytecodes, `.s` file saver; supports local var initializers, compound assignments, break/continue fixups |
| **Interpreter** | `run.c` | Stack-based VM executing bytecodes, `.s` file loader; includes memory/stack/jump bounds checking |

**Supported syntax:**

| Category | Features |
|----------|----------|
| **Types** | `int`, `char`, `void`, pointers (`int*`, `char**`, ...) |
| **Statements** | `if`/`else`, `while`, `do-while`, `for`, `return`, `switch`/`case`/`default`, `break`, `continue`, `goto`/labels, compound `{ }`, expression stmts |
| **Operators** | Complete precedence: `=` `?:` `\|\|` `&&` `\|` `^` `&` `==` `!=` `<` `>` `<=` `>=` `<<` `>>` `+` `-` `*` `/` `%` `++` `--` `*`(deref) `&`(addr) `!` `~` `sizeof` `(type)` `a[i]`, compound assignment (`+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `\|=` `^=`), comma operator |
| **Declarations** | Global/local variables with initializers, arrays, function params, `enum` |
| **Built-ins** | `printf()`, `open()`, `read()`, `close()`, `malloc()`, `free()`, `memset()`, `memcmp()`, `exit()` |

**Not supported:** `struct`/`union`, `float`/`double`.

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

---

## 📋 Changelog

### 2026-07 — Bug & Security Fixes

**c4 compiler / VM fixes:**

| Fix | File | Description |
|-----|------|-------------|
| Escape sequences | `cvm.c` | Fixed `\t`, `\r`, `\\`, `\'`, `\"` handling |
| `/=` tokenizer | `cvm.c` | Added missing `return` after DivAssign |
| Local init code | `ast.c` | Init emitted before `ENT` — unreachable; now chained as AST nodes |
| Negative nlocals | `ast.c` | Fixed `fn->ival = i - loc` sign error |
| Local/param offsets | `ast.c` | Proper `addr` field (`ADDR_GLOBAL`/`ADDR_LOCAL`/`ADDR_PARAM`) instead of broken `ival<0` checks |
| Compound assignment | `ast.c` | `lvalue_copy` missing `addr` field |
| Break/continue | `ast.c` | Added fixup arrays for break/continue in while/for/do/switch |
| Switch break | `ast.c` | Fixed saved_break_sp and patching in switch walker |
| xv6 printf compat | `ast.c` | Replaced `%8.4s` and `%.*s` (unsupported by xv6) with manual formatting |
| **Sandbox bounds** | `run.c` | LI/LC/SI/SC memory access validated; JMP/JSR/BZ/BNZ targets checked; PSH/ENT/ADJ/LEV stack bounds enforced |
| **Format string** | `run.c` | PRTF format string pointer verified to be in data segment |
| **Div by zero** | `run.c` | DIV/MOD return 0 instead of crashing |
| **Loader overflow** | `run.c` | 32-bit integer overflow in bytecode size computation fixed |

**User program fixes:**

| Fix | File | Description |
|-----|------|-------------|
| **Hardcoded credentials** | `login.c` | Replaced `#define PASSWORD "root"` with file-based auth via `/passwd` |
| **Tape pointer overflow** | `bf.c` | Added bounds checks to `>` and `<` (was unlimited `p++`/`p--`) |
| **Stack buffer overflows** | `ed.c` | Added `k < sizeof(pat)-1` checks to all 6 pattern/replacement parsers |

**Kernel fixes:**

| Fix | File | Description |
|-----|------|-------------|
| **TRAPFRAME overlap** | `exec.c` | Added check preventing stack allocation from reaching `TRAPFRAME` (was causing `kernel panic`) |
