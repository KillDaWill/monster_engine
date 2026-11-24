#!/usr/bin/env python3
"""Validador de rendimiento y escalabilidad de Evolution Arena (500+ criaturas).

Ejecuta benchmarks headless (22..1000 criaturas, barrido de celdas, escalado de hilos,
congestión de estanque), calibración biológica multi-semilla a 500 criaturas y
perfilado visual en 1080p/800p a velocidades 1x/4x/16x/64x/MAX.
"""
import argparse
import json
import pathlib
import subprocess
import sys


def run_cmd(cmd, check=True):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and proc.returncode != 0:
        print(f"[ERROR] {' '.join(cmd)} (rc={proc.returncode})", file=sys.stderr)
        if proc.stderr:
            print(proc.stderr[-1000:], file=sys.stderr)
        raise RuntimeError(f"Command failed: {' '.join(cmd)}")
    return proc


def run_headless_scalability(quick=False, workers=0):
    cmd = ["./benchmarks/benchmark_evolution_scalability", f"--workers={workers}"]
    if quick:
        cmd.append("--quick")
    proc = run_cmd(cmd)
    rows = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            rows.append(json.loads(line))
    return rows


def run_large_arena_calibration(seeds, days):
    results = []
    out_dir = pathlib.Path("build/evolution-calibration")
    out_dir.mkdir(parents=True, exist_ok=True)
    for seed in seeds:
        summary_path = out_dir / f"large_500_seed{seed}_{days}d.json"
        cmd = [
            "./demos/demo_evolution_arena_headless",
            "--preset=large",
            f"--seed={seed}",
            f"--days={days}",
            f"--summary={summary_path}",
        ]
        proc = run_cmd(cmd)
        data = json.loads(summary_path.read_text(encoding="utf-8"))
        data["seed"] = seed
        data["stdout_tail"] = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""
        results.append(data)
    return results


def run_visual_benchmarks(frames=180):
    out_dir = pathlib.Path("build/evolution-visual-bench")
    out_dir.mkdir(parents=True, exist_ok=True)
    cases = [
        ("overview_500_1080p_1x", ["--preset=large", "--seed=42", "--speed=1", "--width=1920", "--height=1080", "--show-perf", "--minimap"]),
        ("overview_500_1080p_4x", ["--preset=large", "--seed=42", "--speed=4", "--width=1920", "--height=1080", "--show-perf", "--minimap"]),
        ("overview_500_1080p_16x", ["--preset=large", "--seed=42", "--speed=16", "--width=1920", "--height=1080", "--show-perf", "--minimap"]),
        ("overview_500_1080p_64x", ["--preset=large", "--seed=42", "--speed=64", "--width=1920", "--height=1080", "--show-perf", "--minimap"]),
        ("overview_500_1080p_max", ["--preset=large", "--seed=42", "--speed=max", "--width=1920", "--height=1080", "--show-perf", "--minimap"]),
        ("closeup_500_1080p_1x", ["--preset=large", "--seed=42", "--speed=1", "--width=1920", "--height=1080", "--zoom=16", "--show-perf"]),
        ("neural_500_1080p_1x", ["--preset=large", "--seed=42", "--speed=1", "--width=1920", "--height=1080", "--show-neural", "--show-perf"]),
    ]
    results = []
    for name, extra in cases:
        prof = out_dir / f"{name}.json"
        cmd = ["./demos/demo_evolution_arena", f"--frames={frames}", "--realtime", f"--profile={prof}"] + extra
        run_cmd(cmd)
        data = json.loads(prof.read_text(encoding="utf-8"))
        data["case"] = name
        results.append(data)
        print(
            f"[VIS-PERF] {name:<24} mean={data['mean_ms']:.2f}ms p95={data['p95_ms']:.2f}ms "
            f"p99={data['p99_ms']:.2f}ms fps={1000.0/max(data['mean_ms'],1e-3):.1f} "
            f"draws={data['draw_calls_mean']:.1f} ach={data['achieved_speed']:.1f}x "
            f"sim={data['sim_tick_mean_ms']:.2f}ms rss={data['peak_rss_kib']/1024.0:.1f}MB"
        )
    return results


def main():
    parser = argparse.ArgumentParser(description="Validación de rendimiento y escalabilidad Evolution Arena")
    parser.add_argument("--headless", action="store_true", help="Ejecutar benchmark de escalabilidad headless")
    parser.add_argument("--calibration", action="store_true", help="Ejecutar calibración biológica 500 criaturas")
    parser.add_argument("--visual", action="store_true", help="Ejecutar benchmarks visuales 1080p")
    parser.add_argument("--all", action="store_true", help="Ejecutar todas las fases")
    parser.add_argument("--quick", action="store_true", help="Modo rápido")
    parser.add_argument("--days", type=int, default=5, help="Días para calibración biológica")
    parser.add_argument("--frames", type=int, default=180, help="Frames por caso visual")
    parser.add_argument("--out", default="build/evolution_performance_report.json", help="Archivo JSON de salida")
    args = parser.parse_args()

    if not (args.headless or args.calibration or args.visual or args.all):
        args.all = True

    report = {}
    if args.all or args.headless:
        print("=== Headless Scalability Benchmark ===")
        report["headless"] = run_headless_scalability(quick=args.quick, workers=0)
        for r in report["headless"]:
            if r.get("workload") == "sustained":
                print(
                    f"[HEADLESS] pop={r['target_pop']:4d} mean={r['mean_tick_ms']:.4f}ms "
                    f"p95={r['p95_ms']:.4f}ms p99={r['p99_ms']:.4f}ms "
                    f"speed={r['achieved_speed_x']:.2f}x rss={r['peak_rss_kib']/1024.0:.1f}MB"
                )
            elif r.get("workload") == "worker_scaling":
                print(
                    f"[WORKERS]  pop={r['pop']:4d} w={r['actual_workers']} "
                    f"mean={r['mean_tick_ms']:.4f}ms p95={r['p95_ms']:.4f}ms speed={r['achieved_speed_x']:.2f}x"
                )
            elif r.get("workload") == "cell_sweep":
                print(
                    f"[CELLS]    pop={r['pop']:4d} cell={r['cell_size']:.1f} "
                    f"mean={r['mean_tick_ms']:.4f}ms p95={r['p95_ms']:.4f}ms"
                )
            elif r.get("workload") == "congestion":
                print(
                    f"[CONGEST]  drinkers={r['unique_drinkers']}/40 contacts={r['drink_contact_ticks']} "
                    f"wait={r['mean_wait_seconds']:.2f}s dehyd_deaths={r['dehydration_deaths']}"
                )

    if args.all or args.calibration:
        print("=== Large Arena (500) Multi-Seed Biological Calibration ===")
        seeds = [42, 101] if args.quick else [42, 101, 777]
        report["calibration"] = run_large_arena_calibration(seeds, args.days)
        for c in report["calibration"]:
            print(
                f"[CALIB] seed={c['seed']} final_pop={c.get('final_population', '?')} "
                f"births={c.get('births', '?')} deaths={c.get('deaths', '?')} "
                f"max_gen={c.get('max_generation', '?')} div={c.get('neural_diversity', 0):.4f}"
            )

    if args.all or args.visual:
        print("=== Visual 1080p Performance Benchmarks (500 Creatures) ===")
        report["visual"] = run_visual_benchmarks(frames=args.frames)

    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Report saved to {out_path}")


if __name__ == "__main__":
    main()
