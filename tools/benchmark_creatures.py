#!/usr/bin/env python3
"""Build and run the isolated cross-species construction benchmark.

Uses the repository's existing CORE_SRCS object rules without changing Makefile.
Pass benchmark arguments after `--`, e.g. `-- --species dog --runs 5`.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys


def make_value(text: str, name: str, default: str = "") -> str:
    match = re.search(rf"^{re.escape(name)}\s*[:?+]?=\s*(.*)$", text, re.M)
    return match.group(1).strip() if match else default


def core_sources(text: str) -> list[str]:
    match = re.search(r"^CORE_SRCS\s*=\s*((?:[^\n]*\\\n)*[^\n]*)", text, re.M)
    if not match:
        raise RuntimeError("Could not find CORE_SRCS in Makefile")
    value = match.group(1).replace("\\\n", " ").replace("$(SRC_DIR)/", "src/")
    return shlex.split(value)


def run(args: list[str], *, cwd: Path) -> None:
    print("+", shlex.join(args), file=sys.stderr)
    subprocess.run(args, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("benchmark_args", nargs=argparse.REMAINDER,
                        help="arguments for executable; put after --")
    opts = parser.parse_args()
    root = opts.root.resolve()
    makefile = (root / "Makefile").read_text(encoding="utf-8")
    sources = core_sources(makefile)
    objects = [f"build/{Path(source).stem}.o" for source in sources]
    cflags = shlex.split(make_value(makefile, "CFLAGS", "-Wall -Wextra -Wpedantic -O2 -Iinclude"))
    libs = shlex.split(make_value(makefile, "LIBS", "-lm -pthread"))
    executable = root / "build/benchmark_creature_construction"
    executable.parent.mkdir(parents=True, exist_ok=True)

    run(["make", "-j2", *objects], cwd=root)
    compiler = os.environ.get("CC", make_value(makefile, "CC", "gcc"))
    run([compiler, *cflags, "benchmarks/benchmark_creature_construction.c",
         *objects, *libs, "-o", str(executable)], cwd=root)

    if opts.build_only:
        print(executable)
        return 0

    meta = {
        "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "git_sha": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "git_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "cpu_count": os.cpu_count(),
        "compiler": subprocess.check_output([compiler, "--version"], cwd=root, text=True).splitlines()[0],
        "cflags": cflags,
        "libraries": libs,
        "arguments": opts.benchmark_args[1:] if opts.benchmark_args[:1] == ["--"] else opts.benchmark_args,
        "gpu_memory_bytes": None,
    }
    metadata_path = root / "build/creature-construction-metadata.json"
    metadata_path.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    print(f"metadata: {metadata_path}", file=sys.stderr)
    forwarded = opts.benchmark_args[1:] if opts.benchmark_args[:1] == ["--"] else opts.benchmark_args
    run([str(executable), *forwarded], cwd=root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
