#!/usr/bin/env python3
"""Run all xv6 tests."""

import subprocess
import sys
import os

os.chdir("/home/shawn/myxv6")

# Build first
print("=== Building xv6 ===")
build = subprocess.run(["make", "fs.img"], capture_output=True, text=True)
if build.returncode != 0:
    print("BUILD FAILED:", build.stderr)
    sys.exit(1)
print("Build OK\n")

tests = [
    ("Kernel",       "test_kernel"),
    ("C4 Compiler",  "test_c4"),
    ("Shell",        "test_shell"),
    ("Login",        "test_login"),
    ("Editor",       "test_editor"),
    ("Brainfuck",    "test_brainfuck"),
    ("Forth",        "test_forth"),
    ("TTY",          "test_tty"),
    ("Filesystem",   "test_filesystem"),
    ("Utilities",    "test_utils"),
]

passed = 0
failed = 0

for name, module in tests:
    print(f"\n{'='*60}")
    print(f"RUNNING: {name} Tests")
    print(f"{'='*60}")
    result = subprocess.run(
        [sys.executable, f"test/{module}.py"],
        capture_output=True, text=True, timeout=120
    )
    if result.returncode == 0:
        print(f"  ✓ {name} ALL PASSED")
        passed += 1
    else:
        print(f"  ✗ {name} FAILED")
        print(f"    {result.stdout[-500:]}")
        print(f"    {result.stderr[-500:]}")
        failed += 1

print(f"\n{'='*60}")
print(f"RESULTS: {passed} passed, {failed} failed, {passed+failed} total")
print(f"{'='*60}")
sys.exit(1 if failed > 0 else 0)
