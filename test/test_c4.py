"""Test c4 compiler: compile, run, bytecode save/load."""
from xv6test import Xv6Test

def test_c4_hello():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("c4 hello.c", 8)
    t.assert_in(out, "hello, world!")
    t.assert_in(out, "exit(0)")

    t.stop()
    print("test_c4_hello: PASS")

def test_c4_bytecode():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("c4 -s /hello.s hello.c", 5)
    t.assert_in(out, "hello, world!")

    out = t.run_cmd("c4 /hello.s", 5)
    t.assert_in(out, "hello, world!")
    t.assert_in(out, "exit(0)")

    t.stop()
    print("test_c4_bytecode: PASS")

def test_c4_test_c89():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("c4 test_c89.c", 15)
    t.assert_in(out, "C89 Tests")
    t.assert_in(out, "All tests done")

    t.stop()
    print("test_c4_test_c89: PASS")

def test_c4_debug():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("c4 -d hello.c", 8)
    t.assert_in(out, "JSR")
    t.assert_in(out, "EXIT")
    t.assert_in(out, "hello, world!")

    t.stop()
    print("test_c4_debug: PASS")

if __name__ == "__main__":
    test_c4_hello()
    test_c4_bytecode()
    test_c4_test_c89()
    test_c4_debug()
    print("\n=== ALL C4 TESTS PASSED ===")
