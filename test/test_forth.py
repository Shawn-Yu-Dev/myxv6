"""Test Forth interpreter."""
from xv6test import Xv6Test
import time, select

def test_forth():
    t = Xv6Test()
    t.start()
    t.login()

    t.send("forth")
    time.sleep(2)
    t.send("3 4 + .")
    time.sleep(2)
    t.send("bye")
    time.sleep(2)

    data = b""
    for _ in range(20):
        r, _, _ = select.select([t.proc.stdout], [], [], 0.3)
        if r:
            chunk = t.proc.stdout.read(8192)
            if chunk:
                data += chunk
    output = data.decode(errors="replace")
    t.assert_in(output, "7")
    print("test_forth: PASS")
    t.stop()

if __name__ == "__main__":
    test_forth()
    print("\n=== ALL FORTH TESTS PASSED ===")
