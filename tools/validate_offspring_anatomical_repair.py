#!/usr/bin/env python3
"""Repite la matriz archivada V3 y captura comparaciones con una cámara común."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

from PIL import Image, ImageDraw, ImageFont
from offspring_v3_validation import read_distances

ROOT = Path(__file__).resolve().parents[1]
SEED = 48216
BASE_REVISION = "6043b9499aedb3893dff62e79fd0b79767f04a8b"
BUILTINS = {"lagarto": "@1", "perro": "@2", "velkar": "@4"}
TARGETS = {
    "lagarto_x_rinoceronte": (1, 3), "rinoceronte_x_rata": (4, 9),
    "asno_x_rinoceronte": (2, 3), "cerdo_x_elefante": (2, 4, 9),
    "elefante_x_rata": (1, 5), "caballo_x_elefante": (2, 4),
    "lagarto_x_asno": (2, 5, 7), "lagarto_x_perro": (2, 3, 5),
    "caballo_x_rata": (2, 3), "cerdo_x_rata": (2,), "caballo_x_cerdo": (2,),
    "lagarto_x_cerdo": (8,),
}
APPENDAGES = {"lagarto_x_rinoceronte", "rinoceronte_x_rata", "asno_x_rinoceronte",
              "cerdo_x_elefante", "elefante_x_rata", "caballo_x_elefante"}


def log_run(command, out, log):
    out.mkdir(parents=True, exist_ok=True)
    with (out / f"{log}.tsv").open("w") as stdout, (out / f"{log}.log").open("w") as stderr:
        return subprocess.run([str(arg) for arg in command], cwd=ROOT, stdout=stdout, stderr=stderr).returncode


def export_batch(exe, row, out, repeats):
    return log_run([exe, *parents(row), 10, SEED, repeats, out], out, "export")


def parents(row):
    names = row["parent_a"], row["parent_b"]
    return [BUILTINS.get(name, ROOT / f"recipes/{name}.creature.json") for name in names]


def batch_statistics(directory):
    entries = [line.split("\t") for line in (directory/"export.tsv").read_text().splitlines()]
    timings = [float(r[1])*1000 for r in entries if r[0] == "seconds"]
    offspring = [r for r in entries if r[0] == "offspring"]
    selection = [r for r in entries if r[0] == "selection"]
    coherence = [float(r[2]) for r in entries if r[0] == "anatomy"]
    error = " ".join((directory/"export.log").read_text().splitlines())
    return {
        "offspring": len(offspring),
        "median_ms": round(statistics.median(timings), 3) if offspring else "",
        "eligible": sum(int(r[3]) for r in selection),
        "evaluated": sum(int(r[2]) for r in selection),
        **{name: round(statistics.mean(float(r[index]) for r in selection), 6) if selection else ""
           for index, name in enumerate(("shape", "appearance", "quality"), 4)},
        "coherence": round(statistics.mean(coherence), 6) if coherence else "",
        "repairs": sum(int(r[6]) for r in offspring),
        "error": error,
    }


def batch_summary(rows, out, repaired):
    result = []
    if not all((repaired/row["combo_id"]/"export.log").exists() for row in rows):
        return
    for row in rows:
        combo = row["combo_id"]
        data = {"combination": combo}
        for label, directory in (("baseline", out/"baseline"), ("repaired", repaired)):
            if (directory/combo/"export.tsv").exists():
                data.update({f"{label}_{k}": v for k, v in batch_statistics(directory/combo).items()})
        result.append(data)
    with (out/"matrix.csv").open("w") as output:
        writer = csv.DictWriter(output, fieldnames=list(result[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(result)


def capture(exe, mode, out, files):
    status = log_run([exe, mode, out, *files], out, mode[2:])
    if status:
        raise RuntimeError(f"{mode} falló para {out}. Consulte {mode[2:]}.log")


def metadata(files, out, revision):
    data = []
    for file in files:
        content = file.read_bytes()
        recipe = json.loads(content)["recipe"]
        source_revision = BASE_REVISION if "offspring_report" in file.parts else revision
        data.append({"path": str(file), "seed": recipe["offspringSeed"],
                     "batch_seed": SEED, "sha256": hashlib.sha256(content).hexdigest(),
                     "recipe_revision": source_revision, "capture_revision": revision})
    (out / "inputs.json").write_text(json.dumps(data, indent=2)+"\n")


def panels(out, name, header, views, columns):
    font = ImageFont.truetype("DejaVuSans.ttf", 13)
    views = [view for view in views if any((out / f"{view}-{i}.ppm").exists() for i, _ in columns)]
    width, height, margin = 242, 182, 152
    canvas = Image.new("RGB", (margin+len(columns)*width, 54+len(views)*height), "#131b20")
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 6), header, font=font, fill="white")
    for k, (index, label) in enumerate(columns):
        draw.text((margin+k*width+4, 30), label, font=font, fill="white")
        for j, view in enumerate(views):
            top = 54+j*height
            draw.text((8, top+6), view, font=font, fill="white")
            frame = out / f"{view}-{index}.ppm"
            if frame.exists():
                with Image.open(frame) as img:
                    canvas.paste(img.resize((width-4, height-4), Image.Resampling.LANCZOS),
                                 (margin+k*width, top))
    canvas.save(out / name, optimize=True)


def compare_batch(rows, archive, repaired, out, render, revision, resume):
    result = []
    for row in rows:
        combo = row["combo_id"]
        previous = sorted((archive / "recipes" / combo).glob("*.creature.json"),
                          key=lambda path: int(path.name.split(".")[0]))
        after = sorted((repaired / combo).glob("*.creature.json"),
                       key=lambda path: int(path.name.split(".")[0]))
        if len(after) != 10:
            result.append({"combination": combo, "offspring": len(after)})
            continue
        folder = out / combo
        files = previous + after
        if not (resume and (folder / "repaired-6.png").exists()):
            capture(render, "--overview", folder, files)
            metadata(files, folder, revision)
        _, distances = read_distances(folder / "overview.tsv")
        siblings = [distances[i, j] for i in range(len(previous), len(files))
                    for j in range(i+1, len(files))]
        result.append({"combination": combo, "offspring": len(after),
                       "mean_distances": [statistics.mean(axis) for axis in zip(*siblings)]})
        for start in (0, 5):
            views = ("side", "three-quarter", "head-side")
            if previous:
                panels(folder, f"original-{start+1}.png", f"{combo} · archivo original · lote {SEED}",
                       views, [(i, f"#{i+1}") for i in range(start, start+5)])
            panels(folder, f"repaired-{start+1}.png", f"{combo} · lote regenerado · {SEED}",
                   views, [(len(previous)+i, f"#{i+1}") for i in range(start, start+5)])
        print(combo, flush=True)
    (out / "distances-summary.json").write_text(json.dumps(result, indent=2)+"\n")


def targeted(archive, repaired, out, exporter, renderer, revision, selected=()):
    with (archive / "metadata/offspring.csv").open() as source:
        records = {(r["combo_id"], int(r["attempt_index"])): r for r in csv.DictReader(source)}
    for combo, numbers in TARGETS.items():
        if selected and combo not in selected:
            continue
        for i in numbers:
            folder = out / combo / str(i)
            folder.mkdir(parents=True, exist_ok=True)
            original = archive / "recipes" / combo / f"{i}.creature.json"
            row = records[combo, i]
            after = folder / "replayed.creature.json"
            status = log_run([exporter, "--replay", *parents(row), 10, SEED, i,
                              row["attempts"], after], folder, "replay")
            if status == 0:
                if json.loads(after.read_text())["recipe"]["offspringSeed"] != int(row["seed"]):
                    raise RuntimeError(f"La propuesta repetida difiere para {combo} #{i}")
                labels = [(0, f"Archivo #{i}"), (1, "Misma semilla · reparado")]
            elif combo in APPENDAGES:
                after = folder / "adapted.creature.json"
                subprocess.run([exporter, "--adapt-saved", original, after], check=True, cwd=ROOT)
                labels = [(0, f"Archivo #{i}"), (1, "Mismo individuo adaptado")]
            else:
                after = repaired / combo / f"{i}.creature.json"
                labels = [(0, f"Archivo #{i}"), (1, f"Plaza regenerada #{i}")]
            files = [original, after]
            capture(renderer, "--gallery", folder, files)
            capture(renderer, "--anatomy", folder, files)
            metadata(files, folder, revision)
            panels(folder, "compare.png", f"{combo} · lote {SEED}", (
                "side", "head-side", "insertion-neutral", "insertion-normals",
                "limb-chain", "tail-root", "section-closed", "section-open"), labels)
            panels(folder, "anatomy.png", f"{combo} · lote {SEED}", (
                "insertion", "insertion-wire", "landmarks", "graph",
                "jaw-open", "walk", "pose-curled", "pose-reaching", "pose-feeding"), labels)
            print(combo, i, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="Directorio offspring_report extraído")
    parser.add_argument("out", type=Path)
    parser.add_argument("--exporter", type=Path, default=ROOT/"build/export_offspring_batch")
    parser.add_argument("--renderer", type=Path, default=ROOT/"build/capture_offspring_comparison")
    parser.add_argument("--baseline-exporter", type=Path)
    parser.add_argument("--batches", type=Path, help="Directorio de recetas regeneradas")
    parser.add_argument("--reuse-batches", action="store_true")
    parser.add_argument("--resume-galleries", action="store_true")
    parser.add_argument("--target", choices=tuple(TARGETS), action="append", default=[])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--views", choices=("none", "matrix", "targeted", "all"), default="all")
    args = parser.parse_args()
    archive, out = args.archive.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    exporter, renderer = args.exporter.resolve(), args.renderer.resolve()
    revision = subprocess.check_output(["git", "describe", "--always", "--dirty"], cwd=ROOT, text=True).strip()
    with (archive / "metadata/combinations.csv").open() as source:
        rows = list(csv.DictReader(source))
    repaired = args.batches.resolve() if args.batches else out / "final"
    if not args.reuse_batches:
        for row in rows:
            combo = row["combo_id"]
            if args.baseline_exporter:
                export_batch(args.baseline_exporter.resolve(), row, out/"baseline"/combo, args.repeats)
            status = export_batch(exporter, row, repaired/combo, args.repeats)
            print(combo, status, flush=True)
    (out / "revisions.json").write_text(json.dumps({"source_revision": BASE_REVISION,
        "capture_revision": revision, "batch_seed": SEED,
        "renderer_sha256": hashlib.sha256(renderer.read_bytes()).hexdigest(),
        "exporter_sha256": hashlib.sha256(exporter.read_bytes()).hexdigest()}, indent=2)+"\n")
    batch_summary(rows, out, repaired)
    if args.views in ("all", "targeted"):
        targeted(archive, repaired, out/"targets", exporter, renderer, revision, args.target)
    if args.views in ("all", "matrix"):
        compare_batch(rows, archive, repaired, out/"galleries", renderer, revision, args.resume_galleries)


if __name__ == "__main__":
    main()
