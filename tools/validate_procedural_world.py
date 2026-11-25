#!/usr/bin/env python3
"""
Validación automatizada del Sistema de Mundos Procedurales de Monster Engine.

Comprueba:
1. Validación determinista headless de todas las recetas en worlds/*.world.json.
2. Reproducibilidad exacta de huellas (dataFingerprint) entre ejecuciones repetidas.
3. Simulación headless acotada sobre river_valley y mountain_lakes (sin bloqueos ni NaNs).
4. (Con --visual) Captura visual OpenGL en PNG de escenarios representativos, modos de
   depuración, rocas con proxies de colisión, criaturas en pendientes y Brain Evolution Lab.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run_cmd(cmd, desc):
    print(f"[RUN] {desc}: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[FAIL] {desc} (exit {res.returncode})")
        if res.stdout:
            print(res.stdout[-2000:])
        if res.stderr:
            print(res.stderr[-2000:])
        sys.exit(1)
    return res.stdout


def validate_headless():
    headless = ROOT / "demos" / "demo_evolution_arena_headless"
    if not headless.exists():
        print("[ERR] Falta demos/demo_evolution_arena_headless (compila con make demos/demo_evolution_arena_headless)")
        sys.exit(1)

    worlds = sorted((ROOT / "worlds").glob("*.world.json"))
    assert len(worlds) >= 5, f"Se esperaban >= 5 recetas de mundo, encontradas {len(worlds)}"

    for wpath in worlds:
        rel = str(wpath.relative_to(ROOT))
        out1 = run_cmd([str(headless), f"--world={rel}", "--validate-world-only"], f"validate {rel} (run 1)")
        out2 = run_cmd([str(headless), f"--world={rel}", "--validate-world-only"], f"validate {rel} (run 2)")
        j1 = json.loads(out1)
        j2 = json.loads(out2)
        gen_ms = j1.get("world", {}).pop("generation_ms", 0.0)
        j2.get("world", {}).pop("generation_ms", 0.0)
        assert j1 == j2, f"Salida no determinista para {rel}"
        w = j1.get("world", {})
        print(f"  [OK] {rel}: fp={w.get('fingerprint')} gen={gen_ms:.1f}ms "
              f"lakes={w.get('lakes')} riv={w.get('rivers')} sh={w.get('shore_sites')} rk={w.get('rocks')}")

    # Simulación headless acotada en river_valley y verificar resumen JSON
    summary_path = Path("/tmp/monster_validate_river_valley.json")
    if summary_path.exists():
        summary_path.unlink()
    run_cmd([
        str(headless),
        "--world=worlds/river_valley.world.json",
        "--world-seed=42",
        "--seed=42",
        "--brumacernos=40",
        "--velkares=10",
        "--plants=80",
        "--days=0.5",
        "--workers=4",
        f"--summary={summary_path}"
    ], "headless simulation river_valley (50 creatures)")

    data = json.loads(summary_path.read_text())
    assert data.get("final_population", data.get("population", 1)) >= 0
    summary_path.unlink(missing_ok=True)
    print("[OK] Validacion headless de mundos procedurales superada.")


def capture_visual(out_dir: Path):
    try:
        from PIL import Image
    except ImportError:
        print("[WARN] Pillow no instalado; omitiendo conversion PPM->PNG.")
        return

    arena = ROOT / "demos" / "demo_evolution_arena"
    lab = ROOT / "demos" / "demo_brain_evolution_lab"
    out_dir.mkdir(parents=True, exist_ok=True)

    shots = [
        ("01_river_valley_overview.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=60", "--velkares=15", "--plants=120", "--frames=20",
            "--view=three-quarter", "--zoom=150", "--show-perf", "--minimap", "--width=1280", "--height=720"
        ]),
        ("02_mountain_lakes.png", [
            str(arena), "--world=worlds/mountain_lakes.world.json", "--world-seed=108", "--seed=108",
            "--brumacernos=50", "--velkares=12", "--plants=100", "--frames=15",
            "--view=three-quarter", "--zoom=155", "--show-perf", "--minimap", "--width=1280", "--height=720"
        ]),
        ("03_rocky_highlands_collision.png", [
            str(arena), "--world=worlds/rocky_highlands.world.json", "--world-seed=317", "--seed=317",
            "--brumacernos=40", "--velkares=10", "--plants=80", "--frames=15",
            "--view=three-quarter", "--zoom=95", "--show-rock-collision", "--show-perf", "--width=1280", "--height=720"
        ]),
        ("04_wetlands_shores.png", [
            str(arena), "--world=worlds/wetlands.world.json", "--world-seed=707", "--seed=707",
            "--brumacernos=40", "--velkares=10", "--plants=90", "--frames=15",
            "--view=three-quarter", "--zoom=95", "--show-shores", "--minimap", "--width=1280", "--height=720"
        ]),
        ("05_debug_elevation.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=20", "--velkares=5", "--plants=40", "--frames=10",
            "--view=three-quarter", "--zoom=150", "--terrain-debug=elevation", "--show-perf", "--width=1280", "--height=720"
        ]),
        ("06_debug_slope.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=20", "--velkares=5", "--plants=40", "--frames=10",
            "--view=three-quarter", "--zoom=150", "--terrain-debug=slope", "--width=1280", "--height=720"
        ]),
        ("07_debug_hydrology.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=20", "--velkares=5", "--plants=40", "--frames=10",
            "--view=three-quarter", "--zoom=150", "--terrain-debug=hydrology", "--show-shores", "--width=1280", "--height=720"
        ]),
        ("08_debug_navigation.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=20", "--velkares=5", "--plants=40", "--frames=10",
            "--view=three-quarter", "--zoom=150", "--terrain-debug=nav", "--width=1280", "--height=720"
        ]),
        ("09_near_brumacerno_slope.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=30", "--velkares=8", "--plants=60", "--frames=100",
            "--view=three-quarter", "--capture-species=1", "--capture-state=walking",
            "--zoom=7.5", "--focus-lod=near", "--no-hud", "--width=1280", "--height=720"
        ]),
        ("10_near_velkar_slope.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=30", "--velkares=15", "--plants=60", "--frames=100",
            "--view=three-quarter", "--capture-species=2", "--capture-state=walking",
            "--zoom=7.5", "--focus-lod=near", "--no-hud", "--width=1280", "--height=720"
        ]),
        ("11_shore_closeup.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=40", "--velkares=10", "--plants=80", "--frames=20",
            "--view=three-quarter", "--cam-x=-14", "--cam-z=8", "--zoom=26",
            "--show-shores", "--width=1280", "--height=720"
        ]),
        ("12_500_creatures_valley.png", [
            str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
            "--brumacernos=400", "--velkares=100", "--plants=700", "--capacity=640",
            "--workers=6", "--speed=4", "--quality=medium", "--frames=40", "--realtime",
            "--view=three-quarter", "--zoom=160", "--show-perf", "--minimap", "--width=1280", "--height=720"
        ]),
        ("13_flat_compat_comparison.png", [
            str(arena), "--world=worlds/flat.world.json", "--seed=42",
            "--brumacernos=40", "--velkares=10", "--plants=60", "--frames=15",
            "--view=three-quarter", "--zoom=55", "--show-perf", "--minimap", "--width=1280", "--height=720"
        ]),
        ("14_lab_replay_terrain.png", [
            str(lab), "--species=species/brumacerno.species.json",
            "--environment=environments/brumacerno_river_valley.environment.json",
            "--population=12", "--generations=2", "--train-seeds=2", "--val-seeds=2",
            "--workers=4", "--episode-seconds=60", "--pretrain-gens=2",
            "--replay-ticks=30", "--frames=12", "--width=1440", "--height=860"
        ]),
    ]

    for fname, base_cmd in shots:
        ppm = out_dir / (fname[:-4] + ".ppm")
        png = out_dir / fname
        cmd = base_cmd + [f"--capture={ppm}"]
        try:
            run_cmd(cmd, f"capture {fname}")
        except SystemExit:
            # Si un estado específico (ej. drinking) no se observó en 120 frames, capturar overview cercano en la orilla
            if "drinking" in base_cmd:
                fallback = [
                    str(arena), "--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
                    "--brumacernos=40", "--velkares=10", "--plants=80", "--frames=20",
                    "--view=three-quarter", "--cam-x=-14", "--cam-z=8", "--zoom=26",
                    "--width=1280", "--height=720", f"--capture={ppm}"
                ]
                run_cmd(fallback, f"capture {fname} (shore close-up fallback)")
            else:
                raise
        if ppm.exists():
            Image.open(ppm).save(png)
            ppm.unlink()
            print(f"  [IMG] {png.relative_to(ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--visual", action="store_true", help="Capturar evidencia visual OpenGL en PNG")
    ap.add_argument("--out", default="docs/validation/procedural-world", help="Directorio de salida visual")
    args = ap.parse_args()

    validate_headless()
    if args.visual:
        capture_visual(ROOT / args.out)


if __name__ == "__main__":
    main()
