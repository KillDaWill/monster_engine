#!/usr/bin/env python3
"""Matched production benchmark: frozen serial reference vs opt-in thread counts."""
import argparse
import csv
import hashlib
import json
import os
import platform
import statistics
import subprocess
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(binary, recipe, output, warmups, samples, growth_warmups, growth_samples, threads):
    env = os.environ.copy()
    env.pop("MONSTER_LONG_HAIR_THREADS", None)
    if threads is not None:
        env["MONSTER_LONG_HAIR_THREADS"] = str(threads)
    args = [str(binary), str(recipe), str(output), str(warmups), str(samples),
            str(growth_warmups), str(growth_samples), "all"]
    with open(output.with_suffix(".stderr.log"), "w", encoding="utf-8") as stderr:
        subprocess.run(args, env=env, stdout=subprocess.DEVNULL, stderr=stderr, check=True)
    return args


def load(path):
    with open(path, newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def correctness_key(row):
    return tuple(row[name] for name in ("mode", "stage", "rep", "step", "age", "guides",
                                        "visible", "valid_children", "stable_ids_fnv64", "packed_fnv64"))


def summarize(rows):
    groups = {}
    for row in rows:
        sample_key = (row["mode"], row["stage"], row["rep"])
        groups[sample_key] = groups.get(sample_key, 0.0) + float(row["hair_ms"])
    by_series = {}
    for (mode, stage, _rep), elapsed_ms in groups.items():
        by_series.setdefault((mode, stage), []).append(elapsed_ms)
    result = {}
    for (mode, stage), samples in sorted(by_series.items()):
        ordered = sorted(samples)
        p95 = ordered[max(0, (95 * len(ordered) + 99) // 100 - 1)]
        result[f"{mode}:stage={stage}"] = {"n": len(samples), "p50_ms": statistics.median(samples),
                                             "p95_ms": p95, "max_ms": max(samples),
                                             "p95_label": "nearest-rank; n=5 p95 equals max" if len(samples) == 5 else "nearest-rank"}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path, help="current production benchmark binary")
    parser.add_argument("--reference", required=True, type=Path, help="frozen BASE_SHA production benchmark binary")
    parser.add_argument("--recipe", required=True, type=Path)
    parser.add_argument("--outdir", required=True, type=Path)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--growth-warmups", type=int, default=3)
    parser.add_argument("--growth-samples", type=int, default=5)
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    runs = []
    reference_csv = args.outdir / "original-serial.csv"
    ref_args = run(args.reference, args.recipe, reference_csv, args.warmups, args.samples,
                   args.growth_warmups, args.growth_samples, None)
    ref_rows = load(reference_csv)
    runs.append({"arm": "original-serial", "threads_requested": 1, "argv": ref_args,
                 "csv": str(reference_csv), "binary_sha256": sha256(args.reference),
                 "samples": len(ref_rows), "summary": summarize(ref_rows)})
    ref_map = {correctness_key(row): row for row in ref_rows}
    if len(ref_map) != len(ref_rows):
        raise RuntimeError("duplicate sample key in original reference CSV")

    for threads in (1, 2, 4, 8):
        path = args.outdir / f"current-{threads}t.csv"
        command = run(args.binary, args.recipe, path, args.warmups, args.samples,
                      args.growth_warmups, args.growth_samples, threads)
        rows = load(path)
        current_map = {correctness_key(row): row for row in rows}
        if current_map.keys() != ref_map.keys():
            raise RuntimeError(f"sample-key mismatch at {threads} threads")
        for key, row in current_map.items():
            reference = ref_map[key]
            for field in ("guides", "visible", "valid_children", "stable_ids_fnv64", "packed_fnv64"):
                if row[field] != reference[field]:
                    raise RuntimeError(f"output mismatch at {threads} threads, {key}, field={field}: "
                                       f"{row[field]} != {reference[field]}")
        runs.append({"arm": f"current-{threads}t", "threads_requested": threads, "argv": command,
                     "csv": str(path), "binary_sha256": sha256(args.binary), "samples": len(rows),
                     "summary": summarize(rows), "matched_output": "all sample digests and counts match original"})

    manifest = {"benchmark": "MonsterVisual_UpdateLongHair production recipe; combined body+jaw field",
                "recipe": str(args.recipe), "recipe_sha256": sha256(args.recipe),
                "system": platform.platform(), "machine": platform.machine(), "python": platform.python_version(),
                "warmups_per_stage": args.warmups, "samples_per_stage": args.samples,
                "growth_warmups_per_path": args.growth_warmups, "growth_samples_per_path": args.growth_samples,
                "growth_paths": ["forward", "reverse", "random"], "runs": runs}
    manifest_path = args.outdir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(manifest_path), "runs": runs}, indent=2))


if __name__ == "__main__":
    main()
