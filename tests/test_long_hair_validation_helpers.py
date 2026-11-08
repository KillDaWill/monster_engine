#!/usr/bin/env python3
"""Deadline and process-group cleanup tests for the oracle/sanitizer helper."""
import importlib.util
import os
import signal
import sys
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "validate_long_hair_parallel", ROOT / "tools/validate_long_hair_parallel.py")
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)


class ValidationHelperTests(unittest.TestCase):
    def wait_pid_file(self, path):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists():
                return [int(value) for value in path.read_text(encoding="utf-8").split()]
            time.sleep(0.02)
        self.fail("fake child/grandchild did not publish their PIDs")

    def assert_pids_absent(self, pids):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            remaining = []
            for pid in pids:
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    continue
                except PermissionError:
                    remaining.append(pid)
                else:
                    remaining.append(pid)
            if not remaining:
                return
            time.sleep(0.05)
        self.fail(f"orphan process(es) remain after cleanup: {remaining}")

    def child_code(self, pid_file):
        return (
            "import os,subprocess,sys,time; "
            "g=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)']); "
            f"open({str(pid_file)!r},'w').write(str(os.getpid())+' '+str(g.pid)); "
            "time.sleep(30)"
        )

    def test_deadline_cleans_child_and_grandchild(self):
        with tempfile.TemporaryDirectory() as tmp:
            pid_file = Path(tmp) / "pids"
            started = time.monotonic()
            with self.assertRaisesRegex(RuntimeError, "process group cleaned up"):
                helper.run([sys.executable, "-c", self.child_code(pid_file)], capture=True,
                           timeout=0.2, failure_class="unit timeout")
            self.assert_pids_absent(self.wait_pid_file(pid_file))
            self.assertLess(time.monotonic() - started, 7.0)

    def test_parent_sigterm_cleans_child_and_grandchild(self):
        with tempfile.TemporaryDirectory() as tmp:
            pid_file = Path(tmp) / "pids"
            child = self.child_code(pid_file)
            wrapper_code = (
                "import importlib.util,sys; "
                f"s=importlib.util.spec_from_file_location('validator',{str(ROOT / 'tools/validate_long_hair_parallel.py')!r}); "
                "m=importlib.util.module_from_spec(s); s.loader.exec_module(m); "
                f"m.run([sys.executable,'-c',{child!r}],timeout=60,failure_class='parent interrupt')"
            )
            wrapper = subprocess.Popen([sys.executable, "-c", wrapper_code],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                        start_new_session=True)
            pids = self.wait_pid_file(pid_file)
            wrapper.send_signal(signal.SIGTERM)
            wrapper.wait(timeout=7)
            self.assertNotEqual(wrapper.returncode, 0)
            self.assert_pids_absent(pids)


if __name__ == "__main__":
    unittest.main()
