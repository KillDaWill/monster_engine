#!/usr/bin/env python3
"""Build and run the frozen-source oracle or fully instrumented horse sanitizer case."""
import argparse
import ctypes
import hashlib
import json
import os
import re
import shlex
import signal
import shutil
import subprocess
import sys
import time
from pathlib import Path

BASE_SHA = "92c7c8feff89f9a0ee852b7140e0313ac4c19e45"
PUBLIC_SYMBOLS = (
    "LongHairGroup_Default", "LongHairGroup_Normalize", "LongHairGroups_Fingerprint",
    "LongHairGroup_Envelope", "LongHairSet_Free", "LongHairSet_Collide",
    "LongHairSet_Build", "LongHairSet_Pose", "LongHairGuide_Evaluate", "LongHairSet_Pack",
    "LongHairSet_BuildParallel", "LongHairSet_BuildParallelProfiled",
)
SANITIZER_FLAGS = {
    "address": ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-O1", "-g"],
    "thread": ["-fsanitize=thread", "-fno-omit-frame-pointer", "-O1", "-g"],
}


class RunInterrupted(RuntimeError):
    """A validation helper received a termination signal and cleaned its job group."""


def _parent_death_preexec(expected_parent):
    """Arm Linux PR_SET_PDEATHSIG in the exec child; handler also kills its group."""
    libc = ctypes.CDLL(None, use_errno=True)
    pr_set_pdeathsig = 1

    def arm():
        if libc.prctl(pr_set_pdeathsig, signal.SIGTERM, 0, 0, 0) != 0:
            os._exit(125)
        if os.getppid() != expected_parent:
            os.kill(os.getpid(), signal.SIGTERM)
    return arm


def _stop_process_group(process, grace=2):
    """Terminate every process still in the owned session, even after leader exit."""
    if process is None:
        return
    pgid = process.pid  # start_new_session=True makes the child the session/group leader.
    try:
        os.killpg(pgid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    deadline = time.monotonic() + grace
    while True:
        try:
            os.killpg(pgid, 0)
        except ProcessLookupError:
            break
        if time.monotonic() >= deadline:
            try:
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            break
        try:
            process.wait(timeout=min(0.05, max(0.0, deadline - time.monotonic())))
        except subprocess.TimeoutExpired:
            pass
        time.sleep(0.01)
    kill_deadline = time.monotonic() + grace
    while True:
        try:
            os.killpg(pgid, 0)
        except ProcessLookupError:
            break
        if time.monotonic() >= kill_deadline:
            raise RuntimeError(f"process group {pgid} survived SIGKILL cleanup")
        time.sleep(0.01)
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"child leader {pgid} was not reaped after group cleanup") from exc


def _drain_captured_process(process, timeout=1):
    """Drain capture pipes briefly, then close them if an escaped holder persists."""
    if process is None or (process.stdout is None and process.stderr is None):
        return
    try:
        process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        for stream in (process.stdout, process.stderr):
            if stream is not None:
                stream.close()
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"child leader {process.pid} remained alive after cleanup") from exc


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(argv, *, cwd=None, env=None, capture=False, timeout=120, failure_class="command"):
    print("+", shlex.join([str(arg) for arg in argv]), flush=True)
    proc = None
    pending_signal = None
    previous_handlers = {}
    def interrupted(signum, _frame):
        nonlocal pending_signal
        if proc is None:
            pending_signal = signum
            return
        for handled_signal in previous_handlers:
            signal.signal(handled_signal, signal.SIG_IGN)
        try:
            _stop_process_group(proc)
            if capture:
                _drain_captured_process(proc)
        except Exception as exc:
            raise RunInterrupted(f"received signal {signum}; cleanup failed: {exc}") from exc
        raise RunInterrupted(f"received signal {signum}; child process group cleaned up")
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        previous_handlers[sig] = signal.signal(sig, interrupted)
    try:
        proc = subprocess.Popen(argv, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE if capture else None,
                                stderr=subprocess.STDOUT if capture else None,
                                start_new_session=True, stdin=subprocess.DEVNULL,
                                preexec_fn=_parent_death_preexec(os.getpid()) if sys.platform.startswith("linux") else None)
        if pending_signal is not None:
            interrupted(pending_signal, None)
        try:
            output, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f"TIMEOUT after {timeout}s ({failure_class}); terminating process group", file=sys.stderr, flush=True)
            for handled_signal in previous_handlers:
                signal.signal(handled_signal, signal.SIG_IGN)
            _stop_process_group(proc)
            if capture:
                _drain_captured_process(proc)
            raise RuntimeError(f"{failure_class} timed out after {timeout}s; process group cleaned up")
    finally:
        for sig, old_handler in previous_handlers.items():
            signal.signal(sig, old_handler)
    result = subprocess.CompletedProcess(argv, proc.returncode, output if capture else None)
    if capture and result.stdout:
        print(result.stdout, end="", flush=True)
    if result.returncode:
        raise RuntimeError(f"command returned {result.returncode}: {shlex.join(map(str, argv))}")
    return result.stdout if capture else ""


def source_at_base(repo, relative):
    return subprocess.run(["git", "-C", str(repo), "show", f"{BASE_SHA}:{relative}"],
                          check=True, stdout=subprocess.PIPE).stdout


def freeze_original(repo, destination):
    raw = source_at_base(repo, "src/LongHair.c")
    text = raw.decode("utf-8")
    for symbol in PUBLIC_SYMBOLS:
        text = re.sub(rf"\b{re.escape(symbol)}\b", "Original_" + symbol, text)
    header_aliases = "\n".join(f"#define {symbol} Original_{symbol}" for symbol in PUBLIC_SYMBOLS)
    text = header_aliases + "\n" + text
    destination.write_text(text, encoding="utf-8")
    return hashlib.sha256(raw).hexdigest()


def common_flags(args):
    flags = shlex.split(args.build_flags)
    for required in ("-Iinclude", "-Itests"):
        if required not in flags:
            flags.append(required)
    return flags


def compile_one(args, source, output, extra=()):
    cmd = [args.compiler, *common_flags(args), *extra, "-c", str(source), "-o", str(output)]
    run(cmd, cwd=args.repo, timeout=180, failure_class=f"compile {source}")


def write_manifest(path, payload):
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def build_full(args, outdir):
    frozen = outdir / "LongHair.original.c"
    original_sha = freeze_original(args.repo, frozen)
    original_obj = outdir / "LongHair.original.o"
    current_obj = outdir / "LongHair.current.o"
    driver_obj = outdir / "oracle.o"
    compile_one(args, frozen, original_obj)
    compile_one(args, args.repo / "src/LongHair.c", current_obj)
    compile_one(args, args.repo / "tests/test_long_hair_concurrency_oracle.c", driver_obj)
    binary = outdir / "long_hair_oracle"
    link_flags = shlex.split(args.build_flags)
    libs = [flag for flag in link_flags if flag in ("-lm", "-pthread")]
    run([args.compiler, *map(str, args.core_objects), str(current_obj), str(original_obj),
         str(driver_obj), *libs, "-o", str(binary)], cwd=args.repo, timeout=180, failure_class="oracle link")
    command = [str(binary), str(args.horse_recipe), str(args.dog_recipe)]
    run(command, cwd=args.repo, timeout=3600, failure_class="116-scenario differential oracle")
    return {
        "scope": "full", "base_sha": BASE_SHA, "frozen_source_sha256": original_sha,
        "frozen_source_generated_from_git_show": True,
        "renamed_public_symbols": list(PUBLIC_SYMBOLS),
        "current_core_objects": [str(p) for p in args.core_objects],
        "current_long_hair_source": str(args.repo / "src/LongHair.c"),
        "command": command, "binary_sha256": sha256(binary),
        "recipes": {"horse": {"path": str(args.horse_recipe), "sha256": sha256(args.horse_recipe)},
                    "dog": {"path": str(args.dog_recipe), "sha256": sha256(args.dog_recipe)}},
        "result": "PASS: the binary returned zero after reporting its 116 scenario matrix",
    }


def build_sanitized(args, outdir):
    sanitizer_flags = SANITIZER_FLAGS[args.sanitizer]
    frozen = outdir / "LongHair.original.c"
    original_sha = freeze_original(args.repo, frozen)
    sources = [Path(s) if Path(s).is_absolute() else args.repo / s for s in args.sources]
    if not sources or not all(source.is_file() for source in sources):
        raise ValueError("--sources must list every existing core .c file")
    object_dir = outdir / "instrumented-objects"
    object_dir.mkdir()
    objects = []
    source_records = []
    for source in sources:
        obj = object_dir / (source.stem + ".o")
        compile_one(args, source, obj, sanitizer_flags)
        objects.append(obj)
        source_records.append({"source": str(source.relative_to(args.repo)), "sha256": sha256(source),
                               "object": str(obj), "instrumented": True})
    for source, name in ((frozen, "LongHair.original.o"),
                         (args.repo / "tests/test_long_hair_concurrency_oracle.c", "oracle.o")):
        obj = object_dir / name
        compile_one(args, source, obj, sanitizer_flags)
        objects.append(obj)
        source_records.append({"source": str(source), "sha256": sha256(source),
                               "object": str(obj), "instrumented": True})
    binary = outdir / f"long_hair_{args.sanitizer}"
    libs = [flag for flag in shlex.split(args.build_flags) if flag in ("-lm", "-pthread")]
    run([args.compiler, *map(str, objects), *sanitizer_flags, *libs, "-o", str(binary)],
        cwd=args.repo, timeout=180, failure_class=f"{args.sanitizer} sanitizer link")
    command = [str(binary), "--bounded-horse", str(args.horse_recipe)]
    env = os.environ.copy()
    if args.sanitizer == "address":
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:strict_string_checks=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run(command, cwd=args.repo, env=env, timeout=600, failure_class="bounded adult horse ASan/UBSan test")
    else:
        env["TSAN_OPTIONS"] = "halt_on_error=1"
        setarch = shutil.which("setarch")
        if setarch:
            run([setarch, os.uname().machine, "-R", *command], cwd=args.repo, env=env,
                timeout=600, failure_class="bounded adult horse TSan test")
        else:
            run(command, cwd=args.repo, env=env, timeout=600, failure_class="bounded adult horse TSan test")
    return {
        "scope": "bounded-horse", "sanitizer": args.sanitizer,
        "sanitizer_flags": sanitizer_flags, "base_sha": BASE_SHA,
        "frozen_source_sha256": original_sha,
        "instrumented_sources": source_records,
        "instrumentation_claim": "all supplied CORE_SRCS plus oracle and frozen LongHair source were compiled with sanitizer flags",
        "command": command, "binary_sha256": sha256(binary),
        "recipe": {"path": str(args.horse_recipe), "sha256": sha256(args.horse_recipe)},
        "result": "PASS: bounded adult horse body+jaw case, thread counts 1/4/8, two independent owners",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=("full", "bounded-horse"), required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--outdir", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--compiler", default="gcc")
    parser.add_argument("--build-flags", default="-Wall -Wextra -Wpedantic -O2 -Iinclude -Itests -lm -pthread")
    parser.add_argument("--core-objects", nargs="*", type=Path, default=[])
    parser.add_argument("--sources", nargs="*", default=[])
    parser.add_argument("--horse-recipe", type=Path, required=True)
    parser.add_argument("--dog-recipe", type=Path)
    parser.add_argument("--sanitizer", choices=tuple(SANITIZER_FLAGS))
    args = parser.parse_args()
    args.repo = args.repo.resolve()
    args.horse_recipe = args.horse_recipe.resolve()
    if args.dog_recipe:
        args.dog_recipe = args.dog_recipe.resolve()
    raw_out = args.outdir if args.scope == "full" else args.build_dir
    if raw_out is None:
        parser.error("--outdir is required for full scope; --build-dir is required for bounded-horse")
    outdir = raw_out.resolve()
    outdir.mkdir(parents=True, exist_ok=False)
    if args.scope == "full":
        if not args.dog_recipe or not args.core_objects:
            parser.error("full scope requires --dog-recipe and --core-objects")
        args.core_objects = [p.resolve() for p in args.core_objects]
        result = build_full(args, outdir)
    else:
        if not args.sanitizer or not args.sources:
            parser.error("bounded-horse scope requires --sanitizer and the complete --sources list")
        result = build_sanitized(args, outdir)
    result.update({"compiler": args.compiler, "build_flags": args.build_flags,
                   "repo_head": subprocess.run(["git", "-C", str(args.repo), "rev-parse", "HEAD"],
                                                check=True, text=True, capture_output=True).stdout.strip()})
    write_manifest(outdir / "manifest.json", result)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as exc:
        print(f"long-hair validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
