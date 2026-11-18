#!/usr/bin/env python3
"""Ejecuta cargas reproducibles y resume los registros CSV de la demo."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time

PAIRS = ("Perro:Lagarto", "Caballo:Cerdo", "Caballo:Elefante",
         "Cerdo:Elefante", "Elefante:Lagarto", "Brumacerno:Elefante")
MODES = ("sdf-raymarch", "gpu-sdf-dense", "gpu-sdf-sparse", "legacy-handoff")
SCENARIOS = ("static-0", "static-0.5", "static-1", "forward", "reverse", "orbit", "scrub", "switch")


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def summarize(path):
    frames, gpu, allocations = [], {}, {}
    columns, graphics, startup, warmup = None, None, None, None
    compute = []
    with path.open(errors="replace") as handle:
        for row in csv.reader(handle):
            if not row:
                continue
            if row[0] == "PROFILE_GL":
                graphics = row[1:]
            if row[0] == "PROFILE_STARTUP":
                startup = float(row[1])
            if row[0] == "PROFILE_WARMUP":
                warmup = float(row[1])
            if row[0] == "PROFILE_COMPUTE":
                compute.append(list(map(float, row[2:])))
            if row[0] == "PROFILE_COLUMNS":
                columns = row[1:]
            if columns and row[0] == "PROFILE_FRAME" and len(row[1:]) == len(columns):
                frames.append(dict(zip(columns, map(float, row[1:]))))
            if row[0] == "PROFILE_ALLOCATIONS" and len(row) == 3:
                allocations[int(row[1])] = int(row[2])
            if row[0] == "PROFILE_GPU" and len(row) == 3:
                gpu[int(row[1])] = float(row[2])
    wall = [f["wall_ms"] for f in frames]
    summary = {"graphics": graphics, "startup_ms": startup, "warmup_ms": warmup, "frames": len(frames),
               "fps_throughput": 1000 / statistics.mean(wall) if wall else None,
               "gpu_frames": len(gpu),
               "fps_median": 1000 / statistics.median(wall) if wall else None,
               "fps_1pct_low": (1000 / statistics.mean(sorted(wall, reverse=True)[:math.ceil(len(wall) / 100)])
                                if wall else None),
               "gpu_ms_p50": percentile(list(gpu.values()), .5),
               "gpu_ms_p95": percentile(list(gpu.values()), .95)}
    if compute:
        for i, step in enumerate(("sampling", "classification", "emission", "render")):
            summary[f"compute_{step}_gpu_ms_p50"] = percentile([row[i] for row in compute], .5)
    if allocations:
        points = [allocations[int(f["frame"])] for f in frames if int(f["frame"]) in allocations]
        summary["allocation_calls_p50"] = percentile(points, .5)
        summary["allocation_calls_p95"] = percentile(points, .95)
    if frames:
        for name in frames[0].keys() - {"frame", "age"}:
            values = [f[name] for f in frames]
            for suffix, fraction in (("p50", .50), ("p95", .95), ("p99", .99)):
                summary[f"{name}_{suffix}"] = percentile(values, fraction)
        summary["peak_rss_kib"] = max(f["peak_rss_kib"] for f in frames)
    return summary


def scenario_args(name):
    if name.startswith("static-"):
        return [f"--age={name[7:]}"]
    if name == "orbit":
        return ["--age=0.5", "--stress-motion"]
    if name == "switch":
        return ["--profile-switch"]
    ages = [i / 32 for i in range(33)]
    if name == "reverse":
        ages.reverse()
    elif name == "scrub":
        ages = [0.15, .2, .25, .3, .35, .4, .45, .5, .55, .6,
                .65, .7, .75, .8, .85, .8, .65, .5, .35, .2]
    return ["--age-sequence=" + ",".join(map(str, ages))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", action="append", required=True,
                        help="Ruta del binario con --profile. Repetir para medir en orden A/B/…")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--pair", action="append", default=[])
    parser.add_argument("--mode", choices=MODES, action="append", default=[])
    parser.add_argument("--scenario", choices=SCENARIOS, action="append", default=[])
    parser.add_argument("--warmup", type=int, default=16)
    parser.add_argument("--samples", type=int, default=96)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--native-resolution", action="store_true")
    parser.add_argument("--count-allocations", action="store_true",
                        help="Sonda separada de malloc/calloc/realloc por cuadro en Linux/glibc")
    parser.add_argument("--unpaced", action="append", type=int, default=[],
                        help="Índice de binario (desde 0) que se ejecuta con --profile-unpaced")
    parser.add_argument("--cwd", action="append", type=Path, default=[],
                        help="Directorio de trabajo por binario, para usar su catálogo histórico")
    args = parser.parse_args()
    if args.warmup < 0 or args.samples < 1 or args.timeout <= 0:
        parser.error("El calentamiento debe ser positivo o cero, y las muestras y el límite mayores que cero")
    if args.cwd and len(args.cwd) != len(args.binary):
        parser.error("Proporcione un directorio por cada --binary")
    if any(index < 0 or index >= len(args.binary) for index in args.unpaced):
        parser.error("Cada --unpaced debe identificar un --binary existente")
    args.out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if args.count_allocations:
        probe = (args.out / "morph-allocations.so").resolve()
        source = Path(__file__).with_name("morph_profile_allocations.c")
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", str(source),
                        "-o", str(probe), "-ldl"], check=True)
        env["LD_PRELOAD"] = " ".join(filter(None, (str(probe), env.get("LD_PRELOAD"))))
    results = []
    for pair in args.pair or PAIRS:
        source, target = pair.split(":")
        for mode in args.mode or MODES:
            for scenario in args.scenario or SCENARIOS:
                for i, binary in enumerate(args.binary):
                    output = args.out / f"{source}-{target}-{mode}-{scenario}-{i}.csv"
                    command = [str(Path(binary).resolve()), f"--from={source}", f"--to={target}",
                               "--seed=42", f"--surface-mode={mode}", "--fixed-timestep=0.016666667",
                               f"--profile={args.warmup}", f"--frames={args.warmup + args.samples}",
                               *scenario_args(scenario)]
                    if args.native_resolution:
                        command.append("--native-resolution")
                    if i in args.unpaced:
                        command.append("--profile-unpaced")
                    cwd = args.cwd[i] if args.cwd else Path.cwd()
                    start = time.monotonic()
                    with output.open("w") as log:
                        try:
                            status = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                                    timeout=args.timeout, check=False, cwd=cwd, env=env).returncode
                        except subprocess.TimeoutExpired:
                            status = "timeout"
                    result = dict(summarize(output), command=command, cwd=str(cwd.resolve()), status=status, pair=pair,
                                  mode=mode, scenario=scenario, elapsed_s=time.monotonic() - start,
                                  raw=output.name)
                    results.append(result)
                    print(pair, mode, scenario, i, status, result["frames"], flush=True)
                    (args.out / "summary.json").write_text(json.dumps({
                        "host": list(os.uname()), "results": results
                    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
