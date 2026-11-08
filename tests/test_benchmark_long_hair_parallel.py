#!/usr/bin/env python3
"""Negative and contract tests for the fail-closed benchmark CSV validator."""
import csv
import importlib.util
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "benchmark_long_hair_parallel", ROOT / "tools/benchmark_long_hair_parallel.py")
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)


def rows_for(scope="full", samples=5, growth_samples=5, growth_warmups=3, threads=4):
    rows = []

    def row(mode, stage, rep, step, age):
        return {
            "mode": mode, "requested_threads": str(threads), "stage": str(stage),
            "rep": str(rep), "step": str(step), "age": f"{age:.9g}", "ager_ms": "0.1",
            "hair_ms": "10.0", "process_cpu_ms": "12.0", "guides": "288", "visible": "288",
            "valid_children": "9216", "stable_ids_fnv64": "0123456789abcdef",
            "packed_fnv64": "fedcba9876543210", "process_threads": str(threads),
            "maxrss_kib": "4096",
        }

    stage_ids = range(4)
    for stage in stage_ids:
        for rep in range(samples):
            rows.append(row("stage", stage, rep, 0, stage / 3.0))
    if scope == "full":
        random_ages = benchmark.expected_random_ages(growth_warmups, growth_samples)
        for mode in ("forward", "reverse", "random"):
            for rep in range(growth_samples):
                for step in range(21):
                    age = (step / 20.0 if mode == "forward" else 1 - step / 20.0
                           if mode == "reverse" else random_ages[(rep, step)])
                    rows.append(row(mode, -1, rep, step, age))
    return rows


class BenchmarkValidatorTests(unittest.TestCase):
    def write_csv(self, path, rows):
        with open(path, "w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=benchmark.CSV_FIELDS)
            writer.writeheader()
            writer.writerows(rows)

    def validate(self, path, rows, **overrides):
        self.write_csv(path, rows)
        params = dict(scope="full", samples=5, growth_samples=5, growth_warmups=3,
                      expected_threads=4, expected_guides=288, expected_valid_children=9216)
        params.update(overrides)
        return benchmark.validate_csv(path, **params)

    def test_complete_full_and_adult_sample_shapes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "full.csv"
            rows, observed = self.validate(path, rows_for())
            self.assertEqual(len(rows), 335)
            self.assertEqual(observed, {"requested": 4, "observed": [4]})
            adult = Path(tmp) / "adult.csv"
            self.write_csv(adult, rows_for(scope="adult", samples=9, threads=8))
            parsed, observed = benchmark.validate_csv(
                adult, scope="adult", samples=9, growth_samples=1, growth_warmups=0,
                expected_threads=8, expected_guides=288, expected_valid_children=9216)
            self.assertEqual(len(parsed), 9)
            self.assertEqual(observed["observed"], [8])

    def test_empty_file_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "empty.csv"
            self.write_csv(path, [])
            with self.assertRaisesRegex(benchmark.ValidationError, "empty CSV"):
                self.validate(path, [])

    def test_duplicate_sample_key_rejected(self):
        rows = rows_for()
        rows.append(dict(rows[0]))
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(benchmark.ValidationError, "duplicate sample key"):
                self.validate(Path(tmp) / "duplicate.csv", rows)

    def test_missing_coverage_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(benchmark.ValidationError, "coverage mismatch"):
                self.validate(Path(tmp) / "short.csv", rows_for()[:-1])

    def test_nonfinite_and_nonpositive_timings_rejected(self):
        for value in ("nan", "inf", "-inf", "0", "-0.1"):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as tmp:
                rows = rows_for()
                rows[0]["hair_ms"] = value
                with self.assertRaises(benchmark.ValidationError):
                    self.validate(Path(tmp) / "bad-time.csv", rows)

    def test_nonfinite_age_and_cpu_rejected(self):
        for field in ("age", "ager_ms", "process_cpu_ms"):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as tmp:
                rows = rows_for()
                rows[0][field] = "nan"
                with self.assertRaisesRegex(benchmark.ValidationError, "non-finite"):
                    self.validate(Path(tmp) / "bad-number.csv", rows)

    def test_fallback_observed_thread_count_rejected(self):
        rows = rows_for(threads=4)
        rows[0]["process_threads"] = "1"
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(benchmark.ValidationError, "requested/observed threads"):
                self.validate(Path(tmp) / "fallback.csv", rows)

    def test_wrong_official_horse_counts_rejected(self):
        rows = rows_for()
        rows[0]["valid_children"] = "0"
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(benchmark.ValidationError, "expected 288/288/9216"):
                self.validate(Path(tmp) / "wrong-creature.csv", rows)

    def test_outdir_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            existing = Path(tmp) / "existing"
            existing.mkdir()
            (existing / "keep.csv").write_text("keep", encoding="utf-8")
            with self.assertRaisesRegex(benchmark.ValidationError, "refusing to overwrite"):
                benchmark.create_fresh_output_dir(existing)
            self.assertEqual((existing / "keep.csv").read_text(encoding="utf-8"), "keep")

    def test_subprocess_timeout_kills_process_group(self):
        with tempfile.TemporaryDirectory() as tmp:
            pid_file = Path(tmp) / "pids"
            code = self.child_code(pid_file)
            started = time.monotonic()
            with self.assertRaisesRegex(benchmark.ValidationError, "process group cleaned up"):
                benchmark.run_bounded([sys.executable, "-c", code], env=os.environ.copy(),
                                      stderr=subprocess.DEVNULL, timeout=0.2)
            self.assert_pids_absent(self.wait_pid_file(pid_file))
            self.assertLess(time.monotonic() - started, 7.0)

    def test_parent_sigterm_kills_benchmark_child_group(self):
        with tempfile.TemporaryDirectory() as tmp:
            pid_file = Path(tmp) / "pids"
            child = self.child_code(pid_file)
            wrapper_code = (
                "import importlib.util,os,subprocess,sys; "
                f"s=importlib.util.spec_from_file_location('bench',{str(ROOT / 'tools/benchmark_long_hair_parallel.py')!r}); "
                "m=importlib.util.module_from_spec(s);s.loader.exec_module(m); "
                "e=open(os.devnull,'w'); "
                f"m.run_bounded([sys.executable,'-c',{child!r}],env=os.environ.copy(),stderr=e,timeout=60)"
            )
            wrapper = subprocess.Popen([sys.executable, "-c", wrapper_code],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                        start_new_session=True)
            pids = self.wait_pid_file(pid_file)
            wrapper.send_signal(signal.SIGTERM)
            wrapper.wait(timeout=7)
            self.assertNotEqual(wrapper.returncode, 0)
            self.assert_pids_absent(pids)

    def child_code(self, pid_file):
        return ("import os,subprocess,sys,time; "
                "g=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)']); "
                f"open({str(pid_file)!r},'w').write(str(os.getpid())+' '+str(g.pid)); "
                "time.sleep(30)")

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
                else:
                    remaining.append(pid)
            if not remaining:
                return
            time.sleep(0.05)
        self.fail(f"orphan process(es) remain after cleanup: {remaining}")


if __name__ == "__main__":
    unittest.main()
