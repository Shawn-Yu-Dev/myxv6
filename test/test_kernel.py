"""Test kernel stability: fork, exec, pipes, sbrk, etc."""
from xv6test import Xv6Test

def test_fork_basic():
    t = Xv6Test()
    t.start()
    t.login()

    # Use kerneltests to run specific kernel tests
    out = t.run_cmd("kerneltests copyin", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests copyout", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests pipe1", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests forktest", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests preempt", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests reparent", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests twochildren", 5)
    t.assert_in(out, "OK")

    t.stop()
    print("test_fork_basic: PASS")

def test_memory():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("kerneltests mem", 10)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests sbrkbasic", 10)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests sbrkarg", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests kernmem", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests bsstest", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests stacktest", 5)
    t.assert_in(out, "OK")

    t.stop()
    print("test_memory: PASS")

def test_lazy_allocation():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("kerneltests lazy_alloc", 10)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests lazy_unmap", 10)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests lazy_copy", 10)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests lazy_sbrk", 10)
    t.assert_in(out, "OK")

    t.stop()
    print("test_lazy_allocation: PASS")

def test_syscall_stress():
    t = Xv6Test()
    t.start()
    t.login()

    out = t.run_cmd("kerneltests killstatus", 8)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests bigargtest", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests argptest", 5)
    t.assert_in(out, "OK")

    out = t.run_cmd("kerneltests validatetest", 5)
    t.assert_in(out, "OK")

    t.stop()
    print("test_syscall_stress: PASS")

if __name__ == "__main__":
    test_fork_basic()
    test_memory()
    test_lazy_allocation()
    test_syscall_stress()
    print("\n=== ALL KERNEL TESTS PASSED ===")
