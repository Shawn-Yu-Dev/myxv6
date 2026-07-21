"""Test shell: commands, pipes, redirection, background."""
from xv6test import Xv6Test

def test_shell_basic_commands():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("ls", 3)
    t.assert_in(out, "sh")
    t.assert_in(out, "cat")
    t.assert_in(out, "ls")

    out = t.run_cmd("echo hello shell", 3)
    t.assert_in(out, "hello shell")

    out = t.run_cmd("cat hello.c", 3)
    t.assert_in(out, "hello, world!")

    t.stop()
    print("test_shell_basic_commands: PASS")

def test_shell_pipes():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("ls | cat", 3)
    t.assert_in(out, "sh")
    t.assert_in(out, "cat")

    out = t.run_cmd("ls | grep sh", 3)
    t.assert_in(out, "sh")

    t.stop()
    print("test_shell_pipes: PASS")

def test_shell_redirect():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("echo test123 > /testfile", 3)
    out = t.run_cmd("cat /testfile", 3)
    t.assert_in(out, "test123")

    out = t.run_cmd("rm /testfile", 3)

    t.stop()
    print("test_shell_redirect: PASS")

if __name__ == "__main__":
    test_shell_basic_commands()
    test_shell_pipes()
    test_shell_redirect()
    print("\n=== ALL SHELL TESTS PASSED ===")
