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
output, matched = read_until(proc, 15, "login:")
print("Got:", repr(matched))

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

# Run c4 with debug flag and a tiny test
proc.stdin.write(b"c4 -d test_simple.c\n")
proc.stdin.flush()

# Wait for output
output4, matched4 = read_until(proc, 10, "exit(", "# ")

print("\n=== C4 OUTPUT (debug) ===")
print(output4)

proc.terminate()
proc.wait()
