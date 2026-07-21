"""Test login system: authentication via /passwd."""
from xv6test import Xv6Test

def test_login_success():
    t = Xv6Test()
    t.start()
    t.login()  # uses root/root, should succeed
    print("test_login_success: PASS")
    t.stop()

def test_login_fail():
    t = Xv6Test()
    t.start()
    t.read_until(15, "login:")
    t.send("wrong")
    t.read_until(5, "passwd:")
    t.send("wrong")
    out = t.read_until(5, "login:")
    t.assert_in(out, "Login incorrect")
    print("test_login_fail: PASS")
    t.stop()

def test_passwd_file():
    t = Xv6Test()
    t.start()
    t.login()
    out = t.run_cmd("cat /passwd", 3)
    t.assert_in(out, "root:root")
    print("test_passwd_file: PASS")
    t.stop()

if __name__ == "__main__":
    test_login_success()
    test_login_fail()
    test_passwd_file()
    print("\n=== ALL LOGIN TESTS PASSED ===")
