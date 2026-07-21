"""Test file system operations."""
from xv6test import Xv6Test

def test_fs_basic():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("echo testdata > /test_fs.txt", 2)
    out = t.run_cmd("cat /test_fs.txt", 3)
    t.assert_in(out, "testdata")

    out = t.run_cmd("ls", 2)
    t.assert_in(out, "test_fs.txt")

    out = t.run_cmd("rm /test_fs.txt", 2)
    out = t.run_cmd("ls", 2)
    t.assert_not_in(out, "test_fs.txt")

    print("test_fs_basic: PASS")
    t.stop()

def test_fs_mkdir():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("mkdir /testdir", 2)
    out = t.run_cmd("ls", 2)
    t.assert_in(out, "testdir")

    out = t.run_cmd("echo hello > /testdir/file.txt", 2)
    out = t.run_cmd("cat /testdir/file.txt", 3)
    t.assert_in(out, "hello")

    out = t.run_cmd("rm /testdir/file.txt", 2)
    out = t.run_cmd("rmdir /testdir", 2)

    print("test_fs_mkdir: PASS")
    t.stop()

def test_fs_links():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("echo linkdata > /orig.txt", 2)
    out = t.run_cmd("ln /orig.txt /link.txt", 2)
    out = t.run_cmd("cat /link.txt", 3)
    t.assert_in(out, "linkdata")

    out = t.run_cmd("rm /orig.txt", 2)
    out = t.run_cmd("cat /link.txt", 3)
    t.assert_in(out, "linkdata")  # still accessible via link

    out = t.run_cmd("rm /link.txt", 2)

    print("test_fs_links: PASS")
    t.stop()

if __name__ == "__main__":
    test_fs_basic()
    test_fs_mkdir()
    test_fs_links()
    print("\n=== ALL FILESYSTEM TESTS PASSED ===")
