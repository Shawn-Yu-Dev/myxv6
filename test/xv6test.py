"""Shared test framework for xv6 testing."""

import subprocess
import time
import select

class Xv6Test:
    def __init__(self, timeout=30):
        self.proc = None
        self.timeout = timeout

    def start(self):
        self.proc = subprocess.Popen(
            ["make", "qemu"],
            cwd="/home/shawn/myxv6",
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(3)
            except:
                self.proc.kill()

    def read_until(self, timeout, *patterns):
        start = time.time()
        data = b""
        while time.time() - start < timeout:
            r, _, _ = select.select([self.proc.stdout], [], [], 0.5)
            if r:
                chunk = self.proc.stdout.read(8192)
                if chunk:
                    data += chunk
                    decoded = data.decode(errors="replace")
                    for p in patterns:
                        if p in decoded:
                            return decoded
        return data.decode(errors="replace")

    def send(self, cmd):
        self.proc.stdin.write(cmd.encode() + b"\n")
        self.proc.stdin.flush()

    def login(self):
        self.read_until(15, "login:")
        self.send("root")
        self.read_until(5, "passwd:")
        self.send("root")
        self.read_until(5, "# ")

    def run_cmd(self, cmd, wait=2):
        self.send(cmd)
        time.sleep(wait)
        data = b""
        for _ in range(20):
            r, _, _ = select.select([self.proc.stdout], [], [], 0.3)
            if r:
                chunk = self.proc.stdout.read(8192)
                if chunk:
                    data += chunk
        return data.decode(errors="replace")

    def assert_in(self, output, *patterns):
        for p in patterns:
            if p not in output:
                raise AssertionError(f"Expected '{p}' not found in output")

    def assert_not_in(self, output, *patterns):
        for p in patterns:
            if p in output:
                raise AssertionError(f"Unexpected '{p}' found in output")
