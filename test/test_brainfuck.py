"""Test Brainfuck interpreter."""
from xv6test import Xv6Test

def test_brainfuck():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd('echo "+++.>" > /test.bf', 2)
    out = t.run_cmd("bf /test.bf", 3)
    t.assert_in(out, "C")
    out = t.run_cmd("rm /test.bf", 2)
    print("test_brainfuck: PASS")
    t.stop()

if __name__ == "__main__":
    test_brainfuck()
    print("\n=== ALL BF TESTS PASSED ===")
