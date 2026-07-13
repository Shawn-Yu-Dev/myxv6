import subprocess
import os
import time
import select

os.chdir("/home/shawn/myxv6")

proc = subprocess.Popen(
    ["make", "qemu"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    bufsize=0,
)

def read_until(proc, timeout, *patterns):
    """Read from proc.stdout until one of patterns is found or timeout."""
    start = time.time()
    data = b""
    while time.time() - start < timeout:
        r, _, _ = select.select([proc.stdout], [], [], 0.5)
        if r:
            chunk = proc.stdout.read(4096)
            if chunk:
                data += chunk
                decoded = data.decode(errors='replace')
                for pattern in patterns:
                    if pattern in decoded:
                        return decoded, pattern
    return data.decode(errors='replace'), None

# Wait for login prompt
print("Waiting for login prompt...")
output, matched = read_until(proc, 15, "login:")
print("Got:", repr(matched))
print("Output so far:", output[-200:] if len(output) > 200 else output)

# Send username
proc.stdin.write(b"root\n")
proc.stdin.flush()

# Wait for password prompt
output2, matched2 = read_until(proc, 5, "passwd:")
print("Got password prompt:", repr(matched2))

# Send password
proc.stdin.write(b"root\n")
proc.stdin.flush()

# Wait for shell prompt
output3, matched3 = read_until(proc, 5, "$ ", "# ")
print("Got shell prompt:", repr(matched3))

# Run c4
proc.stdin.write(b"c4 test_simple.c\n")
proc.stdin.flush()

# Wait for output
output4, matched4 = read_until(proc, 10, "exit(", "hello", "$ ")

print("\n=== C4 OUTPUT ===")
print(output4)

proc.terminate()
proc.wait()
