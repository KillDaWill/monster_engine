#!/usr/bin/env python3
"""Validador reproducible de experimentos headless y capturas visuales para Evolution Arena."""
import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
import json
import os
from pathlib import Path
import subprocess
import sys


def run_cmd(cmd, log_path, timeout=300):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w") as log:
        res = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    return res.returncode


def _run_one_experiment(task):
    name, seed, days, flags, out_dir_str = task
    exe = "./demos/demo_evolution_arena_headless"
    run_dir = Path(out_dir_str) / name / f"seed_{seed}"
    run_dir.mkdir(parents=True, exist_ok=True)
    summary = run_dir / "summary.json"
    history = run_dir / "history.csv"
    brains_dir = run_dir / "brains"
    cmd = [exe, f"--seed={seed}", f"--days={days}", *flags,
           f"--summary={summary}", f"--history={history}", f"--export-brains={brains_dir}"]
    rc = run_cmd(cmd, run_dir / "run.log", timeout=900)
    if rc != 0:
        raise RuntimeError(f"Experiment {name} seed={seed} failed (rc={rc})")
    data = json.loads(summary.read_text())
    return name, seed, data


def run_experiments(out_dir, days, seeds, jobs=4):
    out_dir.mkdir(parents=True, exist_ok=True)
    configs = [
        ("utility_baseline", ["--controller=utility", "--brumacernos=16", "--velkares=6", "--plants=36", "--obstacles=5"]),
        ("random_founders", ["--controller=neural", "--founders=random", "--brumacernos=16", "--velkares=6", "--plants=36", "--obstacles=5"]),
        ("neural_no_mutation", ["--controller=neural", "--founders=seeded", "--mutation-rate=0", "--replace-rate=0", "--no-physical-mutation", "--brumacernos=16", "--velkares=6", "--plants=36", "--obstacles=5"]),
        ("neural_with_mutation", ["--controller=neural", "--founders=seeded", "--mutation-rate=0.05", "--mutation-sigma=0.15", "--replace-rate=0.005", "--no-physical-mutation", "--brumacernos=16", "--velkares=6", "--plants=36", "--obstacles=5"]),
        ("herbivore_only", ["--controller=neural", "--founders=seeded", "--mutation-rate=0.05", "--mutation-sigma=0.15", "--brumacernos=16", "--velkares=0", "--plants=36", "--obstacles=5"]),
        ("two_species_arena", ["--controller=neural", "--founders=seeded", "--mutation-rate=0.05", "--mutation-sigma=0.15", "--brumacernos=16", "--velkares=6", "--plants=36", "--obstacles=5"]),
        ("cross_species_transfer", ["--controller=neural", "--founders=imported", "--brumacerno-brain=brains/hunter_carnivore.brain.json", "--velkar-brain=brains/forager_herbivore.brain.json", "--brumacernos=12", "--velkares=6", "--plants=36", "--obstacles=5"]),
    ]
    tasks = []
    for name, flags in configs:
        for seed in seeds:
            tasks.append((name, seed, days, flags, str(out_dir)))

    grouped = {name: {} for name, _ in configs}
    with ProcessPoolExecutor(max_workers=max(1, jobs)) as pool:
        futs = [pool.submit(_run_one_experiment, t) for t in tasks]
        for fut in as_completed(futs):
            name, seed, data = fut.result()
            grouped[name][seed] = data
            print(f"[EXP] {name:22s} seed={seed:3d} pop={data['population']:3d} "
                  f"births={data['births']:3d} deaths={data['deaths']:3d} kills={data['kills']:2d} "
                  f"max_gen={data['maximum_generation']:2d} div={data['neural_diversity']:.4f} "
                  f"fdist={data['average_founder_distance']:.4f} fail_int={data['failed_interactions']}",
                  flush=True)

    report = {}
    for name, _ in configs:
        runs = [grouped[name][s] for s in seeds]
        n = len(runs)
        report[name] = {
            "seeds": seeds,
            "days": days,
            "mean_final_population": sum(r["population"] for r in runs) / n,
            "mean_births": sum(r["births"] for r in runs) / n,
            "mean_deaths": sum(r["deaths"] for r in runs) / n,
            "mean_kills": sum(r["kills"] for r in runs) / n,
            "max_generation_observed": max(r["maximum_generation"] for r in runs),
            "mean_max_generation": sum(r["maximum_generation"] for r in runs) / n,
            "mean_neural_diversity": sum(r["neural_diversity"] for r in runs) / n,
            "mean_founder_distance": sum(r["average_founder_distance"] for r in runs) / n,
            "mean_failed_interactions": sum(r["failed_interactions"] for r in runs) / n,
            "runs": runs,
        }
    (out_dir / "experiments_summary.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def run_visual_captures(out_dir):
    from PIL import Image
    exe = "./demos/demo_evolution_arena"
    out_dir.mkdir(parents=True, exist_ok=True)
    exported_brain = out_dir / "exported_brumacerno.brain.json"
    if exported_brain.exists():
        exported_brain.unlink()

    # IDs en la arena por defecto (2 estanques=1..2, 5 obstáculos=3..7, 36 plantas=8..43,
    # 16 Brumacernos=44..59, 6 Velkares=60..65).
    cases = [
        ("01_arena_overview", ["--seed=42", "--frames=120", "--zoom=30", "--view=three-quarter"]),
        ("02_brumacerno_selection", ["--seed=42", "--frames=90", "--focus=44", "--zoom=6", "--view=three-quarter"]),
        ("03_velkar_selection", ["--seed=42", "--frames=90", "--focus=60", "--zoom=5.5", "--view=three-quarter"]),
        ("04_neural_inspector", ["--seed=42", "--frames=90", "--focus=44", "--zoom=12", "--show-neural"]),
        ("05_feeding", ["--seed=42", "--brumacernos=6", "--velkares=0", "--plants=24", "--obstacles=3", "--capture-species=1", "--capture-state=feeding", "--frames=3600", "--speed=4", "--zoom=5"]),
        ("06_drinking", ["--seed=42", "--brumacernos=6", "--velkares=0", "--plants=12", "--obstacles=3", "--capture-species=1", "--capture-state=drinking", "--frames=3600", "--speed=4", "--zoom=5"]),
        ("07_predator_pursuit", ["--seed=42", "--capture-species=2", "--capture-state=hunting", "--frames=2400", "--speed=2", "--zoom=9"]),
        ("08_escape_behavior", ["--seed=42", "--capture-species=1", "--capture-state=fleeing", "--frames=2400", "--speed=2", "--zoom=9"]),
        ("09_reproduction", ["--seed=42", "--brumacernos=12", "--velkares=0", "--plants=36", "--obstacles=3", "--capture-species=1", "--capture-state=mating", "--frames=4800", "--speed=4", "--zoom=7"]),
        ("10_newborn_creature", ["--seed=42", "--brumacernos=2", "--velkares=1", "--plants=6", "--obstacles=2", "--initial-age=0", "--focus=14", "--frames=30", "--zoom=4", "--view=three-quarter"]),
        ("11_brain_export", ["--seed=42", "--frames=60", "--focus=44", "--zoom=8", "--show-neural", f"--export-brain={exported_brain}"]),
        ("12_brain_import", ["--seed=42", "--frames=60", "--focus=44", "--zoom=8", "--show-neural", "--import-brain=brains/hunter_carnivore.brain.json"]),
        ("13_cross_species_exchange", ["--seed=42", "--frames=90", "--focus=44", "--zoom=10", "--show-neural", "--swap-brains=44:60"]),
    ]

    manifest = []
    for name, flags in cases:
        ppm = out_dir / f"{name}.ppm"
        png = out_dir / f"{name}.png"
        prof = out_dir / f"{name}.json"
        cmd = [exe, *flags, f"--capture={ppm}", f"--profile={prof}"]
        rc = run_cmd(cmd, out_dir / f"{name}.log", timeout=240)
        ok = (rc == 0) and ppm.exists()
        if ppm.exists():
            with Image.open(ppm) as img:
                img.save(png)
            ppm.unlink()
        rec = {"name": name, "returncode": rc, "ok": ok, "png": str(png) if png.exists() else None, "command": cmd}
        manifest.append(rec)
        print(f"[VIS] {name:26s} -> {'PASS' if ok else 'FAIL'} (rc={rc})", flush=True)
    (out_dir / "visual_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiments", action="store_true")
    parser.add_argument("--visual", action="store_true")
    parser.add_argument("--days", type=float, default=10.0)
    parser.add_argument("--seeds", type=int, nargs="+", default=[42, 101, 202])
    parser.add_argument("--jobs", type=int, default=min(6, os.cpu_count() or 2))
    parser.add_argument("--out", type=Path, default=Path("build/evolution-validation"))
    args = parser.parse_args()

    if not args.experiments and not args.visual:
        args.experiments = True

    if args.experiments:
        run_experiments(args.out / "experiments", args.days, args.seeds, args.jobs)
    if args.visual:
        manifest = run_visual_captures(args.out / "captures")
        if any(not m["ok"] for m in manifest):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
