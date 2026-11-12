#!/usr/bin/env python3
"""Construye A/B con los mismos objetos salvo MonsterSDF.c; no altera el árbol."""
import hashlib
from pathlib import Path
import shlex
import subprocess


BASELINE = "0eb37d709433aa08ef6c3416cd76edf1a26108f0"
ROOT = Path(__file__).resolve().parents[1]
OUT = Path("build/ager-index-demo")


def run(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, check=True, **kwargs)


def main():
    (ROOT / OUT).mkdir(parents=True, exist_ok=True)
    run(["make", "-j4", "demos/demo_morph_dog_lizard"])
    # Obtener la orden canónica de enlace sin reconstruir ni tocar las fuentes.
    plan = run(["make", "-n", "-W", "demos/demo_morph_dog_lizard.c",
                "demos/demo_morph_dog_lizard"], capture_output=True, text=True).stdout
    link = shlex.split(next(line for line in reversed(plan.splitlines())
                            if " -o demos/demo_morph_dog_lizard" in line))
    source = run(["git", "show", BASELINE + ":src/MonsterSDF.c"],
                 capture_output=True).stdout
    baseline_source = OUT / "MonsterSDF.baseline.c"
    (ROOT / baseline_source).write_bytes(source)
    baseline_object = OUT / "MonsterSDF.baseline.o"
    # Reutilizar compilador y CFLAGS del enlace del Makefile actual.
    first_object = next(i for i, arg in enumerate(link) if arg.endswith(".o"))
    flags = [arg for arg in link[1:first_object]
             if not arg.startswith("-DMONSTER_BUILD_REVISION=")]
    run([link[0], *flags, "-Isrc", "-c", str(baseline_source), "-o", str(baseline_object)])
    for variant in ("baseline", "patched"):
        command = link.copy()
        if variant == "baseline":
            command[command.index("build/MonsterSDF.o")] = str(baseline_object)
        command[command.index("-o") + 1] = str(OUT / ("demo-" + variant))
        run(command)
    manifest = {"baseline_commit": BASELINE}
    for name in ("src/MonsterSDF.c", "demos/demo_morph_dog_lizard.c",
                 str(OUT / "demo-baseline"), str(OUT / "demo-patched")):
        manifest[name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    import json
    (ROOT / OUT / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Demo A/B lista en", OUT)


if __name__ == "__main__":
    main()
