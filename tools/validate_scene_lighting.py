#!/usr/bin/env python3
"""Capturas y perfiles repetibles de Arena y Lab con cámaras, semillas y ticks fijos."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ARENA = ROOT / "demos/demo_evolution_arena"
LAB = ROOT / "demos/demo_brain_evolution_lab"
HEADLESS = ROOT / "demos/demo_evolution_arena_headless"
ENV = dict(os.environ, __GL_SYNC_TO_VBLANK="0")
MATCH_FIELDS = ("tick", "event_hash", "world_fingerprint")


def run(executable, args, log, cwd=ROOT):
    log.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(executable), *map(str, args)]
    with log.open("w") as stream:
        stream.write("$ " + subprocess.list2cmdline(cmd) + "\n")
        stream.flush()
        subprocess.run(cmd, cwd=cwd, env=ENV, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=350)
    print(log.relative_to(ROOT) if log.is_relative_to(ROOT) else log, flush=True)
    text = log.read_text()
    if re.search(r"\[OpenGL\].*error|\[GLSL\]|shader compile error|Error compilando shader", text, re.I):
        raise RuntimeError(f"Error gráfico en {log}")
    return text


def sequence(world="river_valley_v2", seed=42, creatures=(1, 1, 0), resolution=(1280, 720)):
    herbivores, predators, plants = creatures
    width, height = resolution
    return [f"--world={ROOT / 'worlds' / (world + '.world.json')}", f"--world-seed={seed}", f"--seed={seed}",
            f"--brumacernos={herbivores}", f"--velkares={predators}", f"--plants={plants}",
            f"--width={width}", f"--height={height}", "--quality=medium", "--water-time=.75",
            "--check-gl", "--no-hud", "--step-lock", "--frames=1"]


def capture(key, exe, args, out, cwd=ROOT):
    ppm = out / f"{key}.ppm"
    profile = out / f"{key}.json"
    log = out / f"{key}.log"
    cmd = [*args, f"--capture={ppm}"]
    if "demo_evolution_arena" in exe.name:
        cmd.append(f"--profile={profile}")
    run(exe, cmd, log, cwd)
    with Image.open(ppm) as image:
        image.convert("RGB").save(ppm.with_suffix(".png"))
    ppm.unlink()
    return json.loads(profile.read_text()) if profile.exists() else None


def assert_match(before, after):
    for field in MATCH_FIELDS:
        if before[field] != after[field]:
            raise AssertionError(f"{field}: {before[field]} != {after[field]}")


def sheet(out, group, names):
    panel = Image.new("RGB", (880, 267 * len(names)), "#1a1f24")
    draw = ImageDraw.Draw(panel)
    for index, title in enumerate(names):
        for col, stage in enumerate(("before", "after")):
            path = out / stage / f"{title}.png"
            if not path.exists():
                continue
            with Image.open(path) as source:
                source.thumbnail((432, 242))
                panel.paste(source, (4 + 440 * col, 267 * index + 20))
            draw.text((5 + 440 * col, 267 * index + 4), f"{title} / {stage}", fill="white")
    panel.save(out / f"{group}.jpg", quality=94)


def capture_suite(out, baseline):
    valley = sequence()
    grazing = ["--perspective=47", "--camera-position=8,10.8,72", "--camera-target=-1.5,7.2,89", "--zoom=10"]
    overview500 = ["--capacity=640", "--workers=6", "--frames=60", "--view=three-quarter", "--zoom=160"]
    overview500_base = sequence(creatures=(400, 100, 700), resolution=(1920, 1080))
    cases = {
        "overview_500": overview500_base + overview500,
        "overview_500_hud": [a for a in overview500_base if a != "--no-hud"] + overview500 + ["--show-perf", "--minimap"],
        "medium": sequence(creatures=(45, 10, 90)) + ["--cam-x=-14", "--cam-z=8", "--zoom=26"],
        "grazing_msaa4": valley + grazing + ["--msaa=4"],
        "overhead": valley + ["--view=overview", "--cam-x=0", "--cam-z=76", "--zoom=20"],
        "bank_rock": valley + ["--view=front", "--cam-x=-37", "--cam-z=-67", "--zoom=5.5", "--angle=2.4"],
        "river_v1": sequence("river_valley") + grazing,
        "wetlands": sequence("wetlands_v2", 707) + ["--view=overview", "--cam-x=-7.5", "--cam-z=53", "--zoom=15"],
        "lake": sequence("mountain_lakes_v2", 108) + ["--perspective=47", "--camera-position=-16,17.4,63",
                                                          "--camera-target=-34,12.8,45", "--zoom=12"],
        "highlands": sequence("rocky_highlands_v2", 317) + ["--zoom=95"],
        "wireframe": valley + ["--wireframe", "--show-shores", "--zoom=140"],
    }
    for species in (1, 2):
        for state, extra in (("walking", []), ("juvenile", ["--initial-age=55", "--inspect=feet"])):
            cases[f"species_{species}_{state}"] = sequence(creatures=(8, 4, 35)) + [
                "--frames=120", f"--capture-species={species}", f"--capture-state={state}", "--zoom=5", *extra]
    for name, args in cases.items():
        result = capture(name, ARENA, args, out / "after")
        if baseline:
            assert_match(capture(name, baseline / ARENA.name, args, out / "before"), result)

    env = ["--species=species/brumacerno.species.json", "--environment=environments/brumacerno_river_valley_v2.environment.json",
           "--population=12", "--generations=2", "--train-seeds=2", "--val-seeds=2", "--workers=4",
           "--episode-seconds=60", "--pretrain-gens=2", "--replay-ticks=30", "--frames=12",
           "--width=1440", "--height=860", "--water-time=.5"]
    capture("lab", LAB, env + ["--check-gl", f"--output={out / 'lab-after'}",
                                f"--action-save-ckpt={out / 'lab-after.json'}"], out / "after")
    capture("lab_clean", LAB, env + ["--check-gl", "--no-hud", f"--output={out / 'lab-clean'}"], out / "after")
    if baseline:
        capture("lab", baseline / LAB.name, env + [f"--output={out / 'lab-before'}",
                f"--action-save-ckpt={out / 'lab-before.json'}"], out / "before")
        checkpoints = [json.loads((out / f"lab-{stage}.json").read_text()) for stage in ("before", "after")]
        for checkpoint in checkpoints:
            checkpoint["state"].pop("elapsed_seconds", None)
        if checkpoints[0] != checkpoints[1]:
            raise AssertionError("El checkpoint de Lab cambió aparte del reloj de ejecución.")
        sheet(out, "environment-pairs", ("overview_500", "medium", "grazing_msaa4", "bank_rock", "wetlands", "lake", "highlands"))
        sheet(out, "creature-pairs", ("species_1_walking", "species_1_juvenile", "species_2_walking", "species_2_juvenile", "lab"))

    subject = cases["species_1_walking"]
    for label, toggle in (("directional", ["--no-contacts"]), ("contact", ["--shadows=off"]),
                          ("off", ["--no-contacts", "--shadows=off"]), ("cpu_skin", ["--cpu-skinning"])):
        assert_match(capture(label, ARENA, subject + toggle, out / "debug"),
                     json.loads((out / "after/species_1_walking.json").read_text()))

    # La diferencia a periodos enteros usa una cámara perpendicular al suelo y el modo albedo.
    patch = valley + ["--view=overview", "--cam-x=-33", "--cam-z=-64", "--zoom=7",
                      "--terrain-debug=albedo", "--no-contacts", "--no-decoration", "--shadows=off"]
    images = {}
    for mode, opts in (("stochastic", []), ("periodic", ["--periodic-materials"])):
        capture(mode, ARENA, patch + opts, out / "debug")
        with Image.open(out / "debug" / f"{mode}.png") as image:
            images[mode] = np.asarray(image.convert("L"), dtype=float)[100:620, 160:1120]
    def lagged_delta(image, shift):
        return float(np.abs(image[:, shift:] - image[:, :-shift]).mean())
    shift = round(2.7 * 720 / 7)
    metrics = {mode: {str(dx): lagged_delta(image, dx) for dx in (shift - 12, shift, shift + 12)}
               for mode, image in images.items()}
    (out / "tiling-diagnostic.json").write_text(json.dumps(metrics, indent=2) + "\n")

    for mode in ("normal", "roughness", "weights", "shadow"):
        capture(f"terrain_{mode}", ARENA, cases["bank_rock"] + [f"--terrain-debug={mode}"], out / "debug")
    for j, angle in enumerate((.15, .65, 1.15, 1.65, 2.15, 2.65, 3.15, 3.65)):
        capture(f"orbit_{j:02}", ARENA, cases["medium"] + [f"--angle={angle}"], out / "orbit")


def contact_audit(out, baseline):
    # Ticks verificados primero con el núcleo sin gráficos. Ambas tomas siguen el mismo ID.
    cases = (
        ("brumacerno_juvenil_boca_seca", (16, 5, 58), 132, 1168, 0),
        ("velkar_juvenil_boca_humeda", (14, 6, 56), 136, 3968, 0),
        ("brumacerno_adulto_bebiendo", (12, 5, 58), 125, 4928, None),
        ("velkar_adulto_bebiendo", (12, 5, 58), 137, 5568, None),
    )
    for key, population, entity, tick, age in cases:
        flags = sequence("lab_valley_small_v2", creatures=population) + [
            f"--focus={entity}", "--inspect=head", "--zoom=3", "--workers=1",
            "--controller=utility", "--metabolism=molecular", "--speed=16", f"--frames={tick // 16}",
        ]
        if age is not None:
            flags.append(f"--initial-age={age}")
        result = capture(key, ARENA, flags, out / "after")
        if result["tick"] != tick:
            raise AssertionError(f"Tick incorrecto en {key}")
        log = (out / "after" / f"{key}.log").read_text()
        if "capture_ingestion" not in log:
            raise AssertionError(f"No se observó el contacto esperado en {key}")
        if baseline:
            assert_match(capture(key, baseline / ARENA.name, flags, out / "before"), result)
    if baseline:
        sheet(out, "drinking-pairs", tuple(case[0] for case in cases))


def determinism(out, baseline):
    for version, world in ((1, "river_valley"), (2, "river_valley_v2")):
        args = [f"--world=worlds/{world}.world.json", "--seed=42", "--world-seed=42", "--workers=4",
                "--brumacernos=16", "--velkares=6", "--plants=64", "--metabolism=legacy"]
        path = out / f"headless-v{version}.json"
        run(HEADLESS, args + ["--days=0.0166666666666667", f"--summary={path}"], path.with_suffix(".log"))
        headless = json.loads(path.read_text())
        headless["world_fingerprint"] = headless["world"]["fingerprint"]
        base = ["--frames=120", "--step-lock", "--width=960", "--height=540", "--no-hud", "--check-gl"]
        modes = [
            ("low", ["--quality=low", "--shadows=off", "--no-decoration", "--no-contacts"]),
            ("high", ["--quality=high", "--shadows=medium", "--zoom=32", "--perspective=46", "--msaa=4"]),
        ]
        for label, flags in modes:
            profile = out / f"arena-v{version}-{label}.json"
            run(ARENA, args + base + flags + [f"--profile={profile}"], profile.with_suffix(".log"))
            assert_match(headless, json.loads(profile.read_text()))
        if baseline:
            profile = out / f"baseline-v{version}.json"
            run(baseline / ARENA.name, args + base + [f"--profile={profile}"], profile.with_suffix(".log"))
            assert_match(headless, json.loads(profile.read_text()))
        print(f"v{version} tick={headless['tick']} hash={headless['event_hash']} fp={headless['world_fingerprint']}", flush=True)


def performance(out, baseline):
    def profile(exe, key, n, speed=4, quality="medium", zoom=160, switches=()):
        path = out / f"{key}.json"
        b = n * 4 // 5
        flags = ["--world=worlds/river_valley_v2.world.json", "--world-seed=42", "--seed=42",
                 f"--brumacernos={b}", f"--velkares={n-b}", f"--plants={n*14//10}", f"--capacity={n+140}",
                 "--workers=6", f"--speed={speed}", f"--quality={quality}", "--realtime", "--view=three-quarter",
                 f"--zoom={zoom}", "--show-perf", "--minimap", "--width=1920", "--height=1080", "--msaa=0",
                 "--check-gl", f"--profile={path}", *switches]
        flags += ["--frames=456", "--profile-warmup=36"] if exe == ARENA else ["--frames=420"]
        run(exe, flags, path.with_suffix(".log"))

    for j in range(3):
        if baseline:
            profile(baseline / ARENA.name, f"baseline_500_{j}", 500)
        profile(ARENA, f"final_500_{j}", 500)
    for n in (22, 100, 1000):
        if baseline:
            profile(baseline / ARENA.name, f"baseline_{n}", n)
        profile(ARENA, f"final_{n}", n)
    for speed in (1, 16, "max"):
        profile(ARENA, f"final_500_speed_{speed}", 500, speed=speed)
    for tier in ("low", "high"):
        profile(ARENA, f"final_500_{tier}", 500, quality=tier)
    if baseline:
        profile(baseline / ARENA.name, "baseline_500_close", 500, zoom=16)
    profile(ARENA, "final_500_close", 500, zoom=16)
    profile(ARENA, "final_500_close_no_decoration", 500, zoom=16, switches=("--no-decoration",))
    for title, flags in (
        ("shadows_only", ("--no-contacts", "--no-decoration")),
        ("unshadowed", ("--shadows=off", "--no-contacts", "--no-decoration")),
        ("periodic", ("--periodic-materials", "--shadows=off", "--no-contacts", "--no-decoration")),
    ):
        profile(ARENA, f"final_500_{title}", 500, switches=flags)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/validation/scene-lighting/final")
    parser.add_argument("--baseline", type=Path, help="Directorio de ejecutables guardados del commit de referencia")
    parser.add_argument("--mode", choices=("captures", "contacts", "determinism", "performance"), required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    baseline = args.baseline.resolve() if args.baseline else None
    out.mkdir(parents=True, exist_ok=True)
    {"captures": capture_suite, "contacts": contact_audit, "determinism": determinism, "performance": performance}[args.mode](out, baseline)


if __name__ == "__main__":
    main()
