"""Test TTY multiplexer."""
from xv6test import Xv6Test
import time

def test_tty_multiplexer():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("tty", 4)
    t.assert_in(out, "0*")  # status bar shows tty 0 active
    t.assert_in(out, "1]")
    t.assert_in(out, "4]")

    # tty1 external switch
    out = t.run_cmd("tty1", 3)
    t.assert_in(out, "0*")

    # tty (check) - should show TTY number
    out = t.run_cmd("tty", 3)
    t.assert_in(out, "TTY:")

    print("test_tty_multiplexer: PASS")
    t.stop()

if __name__ == "__main__":
    test_tty_multiplexer()
    print("\n=== ALL TTY TESTS PASSED ===")
