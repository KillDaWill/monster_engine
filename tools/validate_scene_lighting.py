#!/usr/bin/env python3
"""Capturas y perfiles repetibles de Arena y Lab con cámaras, semillas y ticks fijos."""
import argparse
import hashlib
import json
import math
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


def sequence(world="river_valley", seed=42, creatures=(1, 1, 0), resolution=(1280, 720)):
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
    text = run(exe, cmd, log, cwd)
    expected = next((a.split("=")[1] for a in reversed(args) if a.startswith("--capture-species=")), None)
    state = next((a.split("=")[1] for a in reversed(args) if a.startswith("--capture-state=")), "overview")
    if state != "overview":
        identity = re.findall(r"capture_entity=(\d+) species=(\d+) recipe=(\d+)", text)
        if not identity or (expected and identity[-1][1] != expected):
            raise RuntimeError(f"Sujeto requerido ausente o incorrecto en {log}")
        if exe == ARENA and not re.search(r"capture_visibility .*estimated_pixels=[1-9]\d*", text):
            raise RuntimeError(f"Sin muestras GL del sujeto en {log}")
    with Image.open(ppm) as image:
        image.convert("RGB").save(ppm.with_suffix(".png"))
    ppm.unlink()
    return json.loads(profile.read_text()) if profile.exists() else None


def river_bank_camera():
    """Encuentra una orilla accesible del valle actual."""
    pattern = r"site=\d+ river/\d+ contact=([-.0-9]+),([-.0-9]+),([-.0-9]+).*outward=([-.0-9]+),([-.0-9]+)"
    result = subprocess.run([str(ROOT / "build/audit_world_surface"), "worlds/river_valley.world.json"],
                            cwd=ROOT, text=True, capture_output=True, check=True)
    positions = [tuple(map(float, row)) for row in re.findall(pattern, result.stdout)
                 if abs(float(row[0])) < 118 and abs(float(row[2])) < 118]
    if not positions:
        raise RuntimeError("No hay orilla fluvial disponible")
    x, _, z, dx, dz = min(positions, key=lambda p: math.hypot(p[0], p[2]))
    angle = math.atan2(dx, dz)
    return ["--view=three-quarter", "--zoom=11", f"--cam-x={x}", f"--cam-z={z}", f"--angle={angle}", "--terrain-lod0"]


def transport_suite(out, baseline):
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = subprocess.run(["git", "diff", "--quiet", "HEAD"], cwd=ROOT).returncode != 0
    (out / "capture-run.json").write_text(json.dumps({
        "starting_revision": revision, "working_tree_dirty": dirty,
        "baseline_executable": str(baseline / ARENA.name) if baseline else None,
    }, indent=2) + "\n")
    captures = out / "captures"
    creature = sequence(creatures=(8, 4, 35), resolution=(1000, 650))
    subjects = {}
    paired = []
    for species in (1, 2):
        for stage, extra in (("adult", []), ("juvenile", ["--initial-age=55"])):
            args = creature + ["--frames=120", f"--capture-species={species}", f"--capture-state={stage}",
                               "--zoom=3.5", "--view=three-quarter", *extra]
            name = f"species_{species}_{stage}"
            data = capture(name, ARENA, args, captures / "after")
            paired.append(name)
            if baseline:
                assert_match(capture(name, baseline / ARENA.name, args, captures / "before"), data)
            key = hashlib.sha256((captures / "after" / f"{name}.png").read_bytes()).digest()
            if key in subjects:
                raise RuntimeError(f"Capturas idénticas de {subjects[key]} y {name}")
            subjects[key] = name
            if stage == "adult":
                for mode, switches in (
                    ("albedo", ["--creature-lighting=albedo"]),
                    ("normal", ["--creature-lighting=normal"]),
                    ("visibility", ["--creature-lighting=visibility"]),
                    ("direct", ["--creature-lighting=direct"]),
                    ("specular", ["--creature-lighting=specular"]),
                    ("ao", ["--creature-lighting=ao"]),
                    ("indirect_local", ["--creature-lighting=indirect"]),
                    ("indirect_global", ["--creature-lighting=indirect", "--global-fill"]),
                    ("orbit", ["--subject-angle=.85"]),
                    ("sun", ["--sun=25,33"]),
                    ("mid", ["--focus-lod=mid"]),
                    ("far", ["--focus-lod=far"]),
                    ("cpu", ["--cpu-skinning"]),
                ):
                    child = capture(f"{name}_{mode}", ARENA, args + switches, captures / "diagnostics")
                    assert_match(data, child)
    sheet(captures, "creatures", paired)

    terrain = {
        "river_valley": (42, ["--view=three-quarter", "--zoom=160"]),
        "mountain_lakes": (108, ["--view=three-quarter", "--zoom=160"]),
        "rocky_highlands": (317, ["--view=three-quarter", "--zoom=155"]),
        "wetlands": (707, ["--view=three-quarter", "--zoom=120"]),
        "lab_valley_small": (42, ["--view=three-quarter", "--zoom=44"]),
        "river_bank": (42, river_bank_camera()),
    }
    runs = [("after", ARENA)]
    if baseline:
        runs.insert(0, ("before", baseline / ARENA.name))
    for name, (seed, camera) in terrain.items():
        world = name if name != "river_bank" else "river_valley"
        for label, executable in runs:
            args = sequence(world, seed, resolution=(1000, 650)) + camera
            capture(name, executable, args, out / "terrain" / label)
            if name == "river_bank":
                for mode in ("slope", "albedo", "normal", "depth"):
                    switches = ["--hide-water"] if mode != "depth" else []
                    if mode == "slope":
                        switches.append("--wireframe")
                    capture(f"{name}_{mode}", executable, args + [*switches, f"--terrain-debug={mode}"],
                            out / "terrain" / label)
    sheet(out / "terrain", "worlds", terrain)


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
        "wetlands": sequence("wetlands", 707) + ["--view=overview", "--cam-x=-7.5", "--cam-z=53", "--zoom=15"],
        "lake": sequence("mountain_lakes", 108) + ["--perspective=47", "--camera-position=-16,17.4,63",
                                                          "--camera-target=-34,12.8,45", "--zoom=12"],
        "highlands": sequence("rocky_highlands", 317) + ["--zoom=95"],
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

    env = ["--species=species/brumacerno.species.json", "--environment=environments/brumacerno_river_valley.environment.json",
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
        flags = sequence("lab_valley_small", creatures=population) + [
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
    args = ["--world=worlds/river_valley.world.json", "--seed=42", "--world-seed=42", "--workers=4",
            "--brumacernos=16", "--velkares=6", "--plants=64", "--metabolism=legacy"]
    path = out / "headless.json"
    run(HEADLESS, args + ["--days=0.0166666666666667", f"--summary={path}"], path.with_suffix(".log"))
    headless = json.loads(path.read_text())
    headless["world_fingerprint"] = headless["world"]["fingerprint"]
    base = ["--frames=120", "--step-lock", "--width=960", "--height=540", "--no-hud", "--check-gl"]
    modes = [
        ("low", ["--quality=low", "--shadows=off", "--no-decoration", "--no-contacts"]),
        ("high", ["--quality=high", "--shadows=medium", "--zoom=32", "--perspective=46", "--msaa=4"]),
    ]
    for label, flags in modes:
        profile = out / f"arena-{label}.json"
        run(ARENA, args + base + flags + [f"--profile={profile}"], profile.with_suffix(".log"))
        assert_match(headless, json.loads(profile.read_text()))
    if baseline:
        profile = out / "baseline.json"
        run(baseline / ARENA.name, args + base + [f"--profile={profile}"], profile.with_suffix(".log"))
        assert_match(headless, json.loads(profile.read_text()))
    print(f"River Valley tick={headless['tick']} hash={headless['event_hash']} fp={headless['world_fingerprint']}", flush=True)


def performance(out, baseline):
    def profile(exe, key, n, speed=4, quality="medium", zoom=160, switches=()):
        path = out / f"{key}.json"
        b = n * 4 // 5
        flags = ["--world=worlds/river_valley.world.json", "--world-seed=42", "--seed=42",
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


def transport_profile(out, baseline):
    common = ["--world-seed=42", "--seed=42", "--brumacernos=400", "--velkares=100",
              "--plants=700", "--capacity=640", "--workers=6", "--speed=4", "--quality=medium",
              "--shadows=medium", "--show-perf", "--minimap", "--width=1920", "--height=1080",
              "--view=three-quarter", "--zoom=160", "--msaa=0", "--frames=456",
              "--profile-warmup=36", "--check-gl"]
    def run_profile(label, executable, version, extra=()):
        path = out / f"{label}.json"
        run(executable, common + [f"--world=worlds/river_valley_v{version}.world.json",
                                  f"--profile={path}", *extra], path.with_suffix(".log"))
        return json.loads(path.read_text())

    for i in range(1, 4):
        if baseline:
            exe = baseline / ARENA.name
            run_profile(f"v2-baseline-realtime-{i}", exe, 2, ("--realtime",))
            old = run_profile(f"v2-baseline-exact-{i}", exe, 2, ("--step-ticks=4",))
        run_profile(f"v2-current-realtime-{i}", ARENA, 2, ("--realtime",))
        current = run_profile(f"v2-current-exact-{i}", ARENA, 2, ("--step-ticks=4",))
        if baseline:
            assert_match(old, current)
        run_profile(f"v3-current-realtime-{i}", ARENA, 3, ("--realtime",))
    run_profile("v2-current-global-fill", ARENA, 2, ("--realtime", "--global-fill"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/validation/scene-lighting/final")
    parser.add_argument("--baseline", type=Path, help="Directorio de ejecutables guardados del commit de referencia")
    parser.add_argument("--mode", choices=("captures", "contacts", "determinism", "performance", "transport", "transport-profile"), required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    baseline = args.baseline.resolve() if args.baseline else None
    out.mkdir(parents=True, exist_ok=True)
    modes = {"captures": capture_suite, "contacts": contact_audit, "determinism": determinism,
             "performance": performance, "transport": transport_suite, "transport-profile": transport_profile}
    modes[args.mode](out, baseline)


if __name__ == "__main__":
    main()
