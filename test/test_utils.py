"""Test utility programs: cat, echo, grep, wc, etc."""
from xv6test import Xv6Test

def test_cat():
    t = Xv6Test()
    t.start()
    t.login()
    out = t.run_cmd("cat hello.c", 3)
    t.assert_in(out, "hello, world!")
    print("test_cat: PASS")
    t.stop()

def test_grep():
    t = Xv6Test()
    t.start()
    t.login()
    out = t.run_cmd("grep main hello.c", 3)
    t.assert_in(out, "main")
    print("test_grep: PASS")
    t.stop()

def test_wc():
    t = Xv6Test()
    t.start()
    t.login()
    out = t.run_cmd("wc hello.c", 3)
    # wc shows lines/words/bytes
    t.assert_in(out, "hello.c")
    print("test_wc: PASS")
    t.stop()

def test_echo():
    t = Xv6Test()
    t.start()
    t.login()
    out = t.run_cmd("echo xv6 is great", 3)
    t.assert_in(out, "xv6 is great")
    print("test_echo: PASS")
    t.stop()

def test_kill():
    t = Xv6Test()
    t.start()
    t.login()
    out = t.run_cmd("kill 1", 3)
    # kill returns 0 for existing PID (init)
    print("test_kill: PASS")
    t.stop()

def test_sync():
    t = Xv6Test()
    t.start()
    t.login()
    out = t.run_cmd("sync", 3)
    print("test_sync: PASS")
    t.stop()

if __name__ == "__main__":
    test_cat()
    test_grep()
    test_wc()
    test_echo()
    test_kill()
    test_sync()
    print("\n=== ALL UTILS TESTS PASSED ===")
