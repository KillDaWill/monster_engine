#!/usr/bin/env python3
"""Fail-closed matched production benchmark for the long-hair builder."""
import argparse
import csv
import ctypes
import hashlib
import json
import math
import os
import platform
import signal
import statistics
import subprocess
import sys
from pathlib import Path


CSV_FIELDS = (
    "mode", "requested_threads", "stage", "rep", "step", "age", "ager_ms", "hair_ms",
    "process_cpu_ms", "guides", "visible", "valid_children", "stable_ids_fnv64",
    "packed_fnv64", "process_threads", "maxrss_kib",
)
OUTPUT_FIELDS = ("guides", "visible", "valid_children", "stable_ids_fnv64", "packed_fnv64")
SOURCE_FILES = (
    "src/LongHair.c", "src/LongHairScheduler.c", "src/MonsterVisual.c", "include/LongHair.h",
    "include/LongHairScheduler.h", "benchmarks/benchmark_long_hair_parallel.c",
    "tools/benchmark_long_hair_parallel.py",
)


class ValidationError(ValueError):
    """The run cannot be used as benchmark evidence."""


def stop_process_group(process):
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def parent_death_preexec(expected_parent):
    libc = ctypes.CDLL(None, use_errno=True)

    def arm():
        if libc.prctl(1, signal.SIGTERM, 0, 0, 0) != 0:
            os._exit(125)
        if os.getppid() != expected_parent:
            os.kill(os.getpid(), signal.SIGTERM)
    return arm


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_bounded(args, *, env, stderr, timeout=3600):
    process = None
    previous_handlers = {}
    def interrupted(signum, _frame):
        stop_process_group(process)
        raise ValidationError(f"received signal {signum}; benchmark process group cleaned up")
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        previous_handlers[sig] = signal.signal(sig, interrupted)
    try:
        process = subprocess.Popen(args, env=env, stdout=subprocess.DEVNULL, stderr=stderr,
                                   start_new_session=True, stdin=subprocess.DEVNULL,
                                   preexec_fn=parent_death_preexec(os.getpid())
                                   if sys.platform.startswith("linux") else None)
        returncode = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        print(f"TIMEOUT benchmark process after {timeout}s; terminating process group", flush=True)
        stop_process_group(process)
        raise ValidationError(f"benchmark timed out after {timeout}s; process group cleaned up") from exc
    finally:
        for sig, old_handler in previous_handlers.items():
            signal.signal(sig, old_handler)
    if returncode:
        raise subprocess.CalledProcessError(returncode, args)


def expected_keys(scope, samples, growth_samples):
    if scope == "adult":
        # The frozen BASE benchmark predates the adult-only CLI mode. Run its
        # compatible stages mode and project only stage 3 after full validation.
        return {("stage", str(stage), str(rep), "0")
                for stage in range(4) for rep in range(samples)}
    keys = {("stage", str(stage), str(rep), "0")
            for stage in range(4) for rep in range(samples)}
    for mode in ("forward", "reverse", "random"):
        keys.update((mode, "-1", str(rep), str(step))
                    for rep in range(growth_samples) for step in range(21))
    return keys


def expected_random_ages(growth_warmups, growth_samples):
    state = 0x6D2B79F5
    ages = {}
    for rep in range(growth_warmups + growth_samples):
        for step in range(21):
            state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
            if rep >= growth_warmups:
                ages[(rep - growth_warmups, step)] = (state >> 8) / 16777216.0
    return ages


def _integer(row, name, line, minimum=None):
    value = row.get(name)
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValidationError(f"line {line}: invalid integer {name}={value!r}") from exc
    if minimum is not None and parsed < minimum:
        raise ValidationError(f"line {line}: {name}={parsed} is below {minimum}")
    return parsed


def _finite(row, name, line, *, positive=False, nonnegative=False):
    value = row.get(name)
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ValidationError(f"line {line}: invalid number {name}={value!r}") from exc
    if not math.isfinite(parsed):
        raise ValidationError(f"line {line}: non-finite {name}={value!r}")
    if positive and parsed <= 0:
        raise ValidationError(f"line {line}: {name} must be positive")
    if nonnegative and parsed < 0:
        raise ValidationError(f"line {line}: {name} must be nonnegative")
    return parsed


def validate_csv(path, *, scope, samples, growth_samples, growth_warmups,
                 expected_threads, expected_guides, expected_valid_children):
    with open(path, newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != CSV_FIELDS:
            raise ValidationError(f"{path}: unexpected CSV header")
        rows = list(reader)
    if not rows:
        raise ValidationError(f"{path}: empty CSV")

    keys = set()
    observed_threads = set()
    random_ages = expected_random_ages(growth_warmups, growth_samples)
    for line, row in enumerate(rows, start=2):
        if None in row or any(value is None or value == "" for value in row.values()):
            raise ValidationError(f"{path}: incomplete row at line {line}")
        key = (row["mode"], row["stage"], row["rep"], row["step"])
        if key in keys:
            raise ValidationError(f"{path}: duplicate sample key {key}")
        keys.add(key)
        requested = _integer(row, "requested_threads", line, 1)
        effective = _integer(row, "process_threads", line, 1)
        observed_threads.add(effective)
        if requested != expected_threads or effective != expected_threads:
            raise ValidationError(
                f"{path}: requested/observed threads {requested}/{effective}, expected {expected_threads}")
        stage = _integer(row, "stage", line)
        rep = _integer(row, "rep", line, 0)
        step = _integer(row, "step", line, 0)
        age = _finite(row, "age", line, nonnegative=True)
        _finite(row, "ager_ms", line, nonnegative=True)
        _finite(row, "hair_ms", line, positive=True)
        _finite(row, "process_cpu_ms", line, nonnegative=True)
        _integer(row, "maxrss_kib", line, 0)
        guides = _integer(row, "guides", line, 1)
        visible = _integer(row, "visible", line, 0)
        children = _integer(row, "valid_children", line, 0)
        if (guides, visible, children) != (expected_guides, expected_guides, expected_valid_children):
            raise ValidationError(
                f"{path}: expected {expected_guides}/{expected_guides}/{expected_valid_children} "
                f"guides/visible/children, got {guides}/{visible}/{children}")
        for name in ("stable_ids_fnv64", "packed_fnv64"):
            digest = row[name]
            if len(digest) != 16:
                raise ValidationError(f"{path}: malformed {name} at line {line}")
            try:
                int(digest, 16)
            except ValueError as exc:
                raise ValidationError(f"{path}: malformed {name} at line {line}") from exc

        mode = row["mode"]
        if mode == "stage":
            if scope == "adult":
                if stage not in range(4) or step != 0 or not 0 <= rep < samples:
                    raise ValidationError(f"{path}: unexpected adult-stage sample key {key}")
            elif stage not in range(4) or step != 0 or not 0 <= rep < samples:
                raise ValidationError(f"{path}: unexpected stage sample key {key}")
            if abs(age - stage / 3.0) > 1e-7:
                raise ValidationError(f"{path}: unexpected stage age {age} for stage {stage}")
        elif mode in ("forward", "reverse", "random") and scope == "full":
            if stage != -1 or not 0 <= rep < growth_samples or step not in range(21):
                raise ValidationError(f"{path}: unexpected trajectory sample key {key}")
            target_age = (step / 20.0 if mode == "forward" else 1.0 - step / 20.0
                          if mode == "reverse" else random_ages[(rep, step)])
            if abs(age - target_age) > 1e-7:
                raise ValidationError(f"{path}: unexpected {mode} age at {key}: {age} != {target_age}")
        else:
            raise ValidationError(f"{path}: unexpected mode {mode!r}")

    wanted = expected_keys(scope, samples, growth_samples)
    if keys != wanted:
        missing = sorted(wanted - keys)[:4]
        extra = sorted(keys - wanted)[:4]
        raise ValidationError(f"{path}: sample coverage mismatch; missing={missing}, extra={extra}")
    if scope == "adult":
        rows = [row for row in rows if row["mode"] == "stage" and row["stage"] == "3"]
    return rows, {"requested": expected_threads, "observed": sorted(observed_threads)}


def correctness_key(row):
    return tuple(row[name] for name in
                 ("mode", "stage", "rep", "step", "age", *OUTPUT_FIELDS))


def summarize(rows):
    groups = {}
    for row in rows:
        sample_key = (row["mode"], row["stage"], row["rep"])
        groups[sample_key] = groups.get(sample_key, 0.0) + float(row["hair_ms"])
    series = {}
    for (mode, stage, _rep), elapsed in groups.items():
        series.setdefault((mode, stage), []).append(elapsed)
    result = {}
    for (mode, stage), values in sorted(series.items()):
        if not values or any(not math.isfinite(value) or value <= 0 for value in values):
            raise ValidationError(f"invalid timing series {mode}:{stage}")
        ordered = sorted(values)
        p95 = ordered[max(0, (95 * len(ordered) + 99) // 100 - 1)]
        result[f"{mode}:stage={stage}"] = {
            "n": len(values), "p50_ms": statistics.median(values),
            "p95_ms": p95, "max_ms": max(values), "p95_method": "nearest-rank",
        }
    return result


def create_fresh_output_dir(path):
    path = Path(path)
    if path.exists():
        raise ValidationError(f"refusing to overwrite existing benchmark directory: {path}")
    path.mkdir(parents=True)
    return path


def run(binary, recipe, output, warmups, samples, growth_warmups, growth_samples,
        threads, mode):
    env = os.environ.copy()
    env.pop("MONSTER_LONG_HAIR_THREADS", None)
    if threads is not None:
        env["MONSTER_LONG_HAIR_THREADS"] = str(threads)
    args = [str(binary), str(recipe), str(output), str(warmups), str(samples),
            str(growth_warmups), str(growth_samples), mode]
    stderr_path = output.with_suffix(".stderr.log")
    with open(stderr_path, "w", encoding="utf-8") as stderr:
        run_bounded(args, env=env, stderr=stderr)
    return args, stderr_path


def git_metadata(root):
    commit = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], check=True,
                            text=True, capture_output=True).stdout.strip()
    status = subprocess.run(["git", "-C", str(root), "status", "--porcelain"], check=True,
                            text=True, capture_output=True).stdout
    return commit, bool(status.strip()), {name: sha256(root / name) for name in SOURCE_FILES}


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--recipe", required=True, type=Path)
    parser.add_argument("--outdir", required=True, type=Path)
    parser.add_argument("--scope", choices=("full", "adult"), default="full")
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--growth-warmups", type=int, default=3)
    parser.add_argument("--growth-samples", type=int, default=5)
    parser.add_argument("--expected-guides", type=int, default=288)
    parser.add_argument("--expected-valid-children", type=int, default=9216)
    parser.add_argument("--build-flags", required=True)
    parser.add_argument("--compiler", default="gcc")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.reference = args.reference.resolve()
    args.recipe = args.recipe.resolve()
    args.outdir = args.outdir.resolve()
    if not args.binary.is_file() or not args.reference.is_file() or not args.recipe.is_file():
        raise ValidationError("binary, reference, and recipe must be existing files")
    if min(args.warmups, args.growth_warmups) < 0 or min(args.samples, args.growth_samples) < 1:
        raise ValidationError("warmups must be nonnegative and sample counts positive")
    if args.expected_guides < 1 or args.expected_valid_children < 0:
        raise ValidationError("expected creature counts are invalid")
    if args.scope == "adult" and (args.warmups != 3 or args.samples < 9):
        raise ValidationError("adult focused validation requires exactly 3 warmups and at least 9 samples")
    if args.scope == "full" and (args.samples < 5 or args.growth_samples < 5):
        raise ValidationError("full benchmark requires at least 5 samples per stage and trajectory")

    outdir = create_fresh_output_dir(args.outdir)
    mode = "stages" if args.scope == "adult" else "all"
    thread_counts = (1, 8) if args.scope == "adult" else (1, 2, 4, 8)
    ref_path = outdir / "original-serial.csv"
    ref_command, ref_stderr = run(args.reference, args.recipe, ref_path, args.warmups,
                                  args.samples, args.growth_warmups, args.growth_samples,
                                  None, mode)
    ref_rows, ref_observed = validate_csv(
        ref_path, scope=args.scope, samples=args.samples, growth_samples=args.growth_samples,
        growth_warmups=args.growth_warmups, expected_threads=1,
        expected_guides=args.expected_guides,
        expected_valid_children=args.expected_valid_children)
    ref_map = {correctness_key(row): row for row in ref_rows}
    arm_data = [{
        "arm": "original-serial", "threads_requested": 1, "threads_observed": ref_observed["observed"],
        "argv": ref_command, "csv": str(ref_path), "csv_sha256": sha256(ref_path),
        "stderr": str(ref_stderr), "stderr_sha256": sha256(ref_stderr),
        "binary_sha256": sha256(args.reference), "sample_rows": len(ref_rows),
        "summary": summarize(ref_rows), "maxrss_kib": max(int(r["maxrss_kib"]) for r in ref_rows),
    }]

    for threads in thread_counts:
        path = outdir / f"current-{threads}t.csv"
        command, stderr_path = run(args.binary, args.recipe, path, args.warmups, args.samples,
                                   args.growth_warmups, args.growth_samples, threads, mode)
        rows, observed = validate_csv(
            path, scope=args.scope, samples=args.samples, growth_samples=args.growth_samples,
            growth_warmups=args.growth_warmups, expected_threads=threads,
            expected_guides=args.expected_guides,
            expected_valid_children=args.expected_valid_children)
        current_map = {correctness_key(row): row for row in rows}
        if len(current_map) != len(rows) or current_map.keys() != ref_map.keys():
            raise ValidationError(f"current-{threads}t sample keys are duplicated or differ from original")
        for key, row in current_map.items():
            reference = ref_map[key]
            for field in OUTPUT_FIELDS:
                if row[field] != reference[field]:
                    raise ValidationError(
                        f"output mismatch at {threads} threads, key={key}, {field}: "
                        f"{row[field]} != {reference[field]}")
        arm_data.append({
            "arm": f"current-{threads}t", "threads_requested": threads,
            "threads_observed": observed["observed"], "argv": command, "csv": str(path),
            "csv_sha256": sha256(path), "stderr": str(stderr_path),
            "stderr_sha256": sha256(stderr_path), "binary_sha256": sha256(args.binary),
            "sample_rows": len(rows), "summary": summarize(rows),
            "maxrss_kib": max(int(r["maxrss_kib"]) for r in rows),
            "matched_output": "all validated rows exactly match original digests/counts",
        })

    commit, dirty, source_hashes = git_metadata(root)
    manifest = {
        "benchmark": "MonsterVisual_UpdateLongHair production recipe; combined body+jaw field",
        "scope": args.scope, "source_commit": commit, "working_tree_dirty": dirty,
        "source_sha256": source_hashes, "compiler": args.compiler, "build_flags": args.build_flags,
        "recipe": str(args.recipe), "recipe_sha256": sha256(args.recipe),
        "expected_guides": args.expected_guides,
        "expected_visible_guides": args.expected_guides,
        "expected_valid_children": args.expected_valid_children,
        "system": platform.platform(), "machine": platform.machine(), "python": platform.python_version(),
        "warmups_per_stage": args.warmups, "samples_per_stage": args.samples,
        "growth_warmups_per_path": args.growth_warmups,
        "growth_samples_per_path": args.growth_samples,
        "growth_paths": [] if args.scope == "adult" else ["forward", "reverse", "random"],
        "thread_counts_requested": list(thread_counts), "runs": arm_data,
    }
    (outdir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(outdir / "manifest.json"), "runs": arm_data}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except ValidationError as exc:
        raise SystemExit(f"benchmark validation failed: {exc}")
