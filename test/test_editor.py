"""Test ed line editor: basic editing, search/replace."""
from xv6test import Xv6Test
import time

def test_ed_create():
    t = Xv6Test()
    t.start()
    t.login()

    t.send("ed /edtest.txt")
    time.sleep(2)
    t.send("a")
    time.sleep(1)
    t.send("line one")
    time.sleep(1)
    t.send("line two")
    time.sleep(1)
    t.send("line three")
    time.sleep(1)
    t.send(".")
    time.sleep(1)
    t.send("w")
    time.sleep(2)
    t.send("q")
    time.sleep(1)

    out = t.run_cmd("cat /edtest.txt", 3)
    t.assert_in(out, "line one")
    t.assert_in(out, "line three")
    out = t.run_cmd("rm /edtest.txt", 2)
    print("test_ed_create: PASS")
    t.stop()

def test_ed_substitute():
    t = Xv6Test()
    t.start()
    t.login()

    t.send("ed /edsub.txt")
    time.sleep(2)
    t.send("a")
    time.sleep(1)
    t.send("hello world")
    time.sleep(1)
    t.send(".")
    time.sleep(1)
    t.send("s/world/xv6/")
    time.sleep(1)
    t.send("w")
    time.sleep(1)
    t.send("q")
    time.sleep(1)

    out = t.run_cmd("cat /edsub.txt", 3)
    t.assert_in(out, "hello xv6")
    out = t.run_cmd("rm /edsub.txt", 2)
    print("test_ed_substitute: PASS")
    t.stop()

if __name__ == "__main__":
    test_ed_create()
    test_ed_substitute()
    print("\n=== ALL EDITOR TESTS PASSED ===")
