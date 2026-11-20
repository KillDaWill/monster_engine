#!/usr/bin/env python3
"""Exporta lotes V2/V3 y compone galerías en el mismo marco anatómico."""
import argparse
from html import escape
from itertools import combinations
import json
import math
from pathlib import Path
import statistics
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
VIEWS = (
    ("side", "Lateral"), ("front", "Frontal"), ("three-quarter", "Tres cuartos"),
    ("head", "Cabeza · tres cuartos"), ("head-side", "Cabeza · lateral"),
    ("shape-side", "Geometría · lateral"), ("shape-front", "Geometría · frontal"),
    ("shape-three-quarter", "Geometría · tres cuartos"),
    ("shape-head", "Geometría · cabeza"),
    ("shape-head-side", "Geometría · cabeza lateral"),
    ("albedo-three-quarter", "Albedo · tres cuartos"),
)
PAIRS = (
    ("rat-elephant", "Rata × elefante", "recipes/rata.creature.json", "recipes/elefante.creature.json", 5),
    ("rhino-rat", "Rinoceronte × rata", "recipes/rinoceronte.creature.json", "recipes/rata.creature.json", 5),
    ("horse-donkey", "Caballo × asno", "recipes/caballo.creature.json", "recipes/asno.creature.json", 5),
    ("pig-elephant", "Cerdo × elefante", "recipes/cerdo.creature.json", "recipes/elefante.creature.json", 5),
    ("dog-lizard", "Perro × lagarto", "@2", "@1", 5),
    ("rat-rat", "Rata × rata", "recipes/rata.creature.json", "recipes/rata.creature.json", 10),
)


def run(command, log):
    with log.open("w") as output:
        subprocess.run([str(arg) for arg in command], check=True, cwd=ROOT, stdout=output)


def export(executable, directory, parents, count, seed, repeats, *options):
    directory.mkdir(parents=True, exist_ok=True)
    run([executable, *parents, count, seed, repeats, directory, *options], directory / "export.tsv")
    return [directory / f"{i}.creature.json" for i in range(1, count + 1)]


def capture(executable, directory, files):
    directory.mkdir(parents=True, exist_ok=True)
    run([executable, "--gallery", directory, *files], directory / "distances.tsv")


def sheet(source, dest, title, labels, indexes):
    width, height, left = 238, 179, 152
    canvas = Image.new("RGB", (left + len(indexes) * width, 60 + len(VIEWS) * height), "#182126")
    draw = ImageDraw.Draw(canvas)
    text = ImageFont.truetype("DejaVuSans.ttf", 12)
    heading = ImageFont.truetype("DejaVuSans.ttf", 16)
    draw.text((12, 12), title, fill="#edf7f7", font=heading)
    for column, label in enumerate(labels):
        draw.text((left + column * width + 8, 40), label, fill="#edf7f7", font=text)
    for row, (view, label) in enumerate(VIEWS):
        y = 60 + row * height
        draw.text((8, y + 12), label, fill="#edf7f7", font=text)
        for column, index in enumerate(indexes):
            with Image.open(source / f"{view}-{index}.ppm") as frame:
                frame = frame.resize((width - 4, height - 4), Image.Resampling.LANCZOS)
                canvas.paste(frame, (left + column * width, y))
    canvas.save(dest)


def mask(path):
    with Image.open(path) as frame:
        pixels = list(frame.getdata())
    background = pixels[0]
    return {i for i, pixel in enumerate(pixels) if max(abs(a - b) for a, b in zip(pixel, background)) > 6}


def read_distances(path):
    descriptors, distances = {}, {}
    for line in path.read_text().splitlines():
        fields = line.split("\t")
        if fields[0] == "descriptor":
            descriptors[int(fields[1])] = [float(x) for x in fields[2:]]
        elif fields[0].isdigit():
            distances[int(fields[0]), int(fields[1])] = [float(x) for x in fields[2:]]
    return descriptors, distances


def metrics(source, indexes, timing):
    descriptors, distances = read_distances(source / "distances.tsv")
    records = [distances[pair] for pair in combinations(indexes, 2)]
    keys = ("head_sdf", "head_landmarks", "body_stations", "limb_ratios", "pigment")
    result = {
        name: {"minimum": min(values), "mean": statistics.mean(values)}
        for name, values in zip(keys, zip(*records))
    }
    # Mismas celdas para V2 y V3. Cocientes de anchura, rostro, mandíbula, pinna y ojos.
    bins = (.10, .075, .035, .12, .075)
    result["descriptor_cells"] = len({
        tuple(math.floor(x / width) for x, width in zip(descriptors[i], bins)) for i in indexes
    })
    shape_views = ("shape-side", "shape-front", "shape-three-quarter")
    for view in shape_views:
        frame_masks = {i: mask(source / f"{view}-{i}.ppm") for i in indexes}
        scores = []
        for i, j in combinations(indexes, 2):
            a, b = frame_masks[i], frame_masks[j]
            if a or b:
                scores.append(1 - len(a & b) / len(a | b))
        result[view] = {"minimum": min(scores), "mean": statistics.mean(scores)}
    rows = [line.split("\t") for line in timing.read_text().splitlines()]
    result["median_ms"] = statistics.median(float(row[1]) for row in rows if row[0] == "seconds") * 1000
    proposals = [row for row in rows if row[0] == "selection"]
    total = sum(int(row[2]) for row in proposals) if proposals else sum(int(row[5]) for row in rows if row[0] == "offspring")
    valid = sum(int(row[3]) for row in proposals) if proposals else len(indexes)
    result["eligible_proposals"] = {"valid": valid, "total": total, "rate": valid / total}
    return result


def controlled(root, exporter, renderer, seed):
    output = root / "controlled"
    parents = PAIRS[0][2:4]
    files = export(exporter, output, parents, 12, seed, 1, "--fixed-global")
    recipes = [json.loads(file.read_text()) for file in files]
    profiles = [doc["recipe"]["adult"]["headHybrid"]["regional"] for doc in recipes]

    def measurements(profile):
        surface = profile["surface"]
        radius = surface["craniumRadii"]
        return (radius["x"] / radius["z"],
                (surface["faceTip"]["z"] - surface["faceRoot"]["z"]) / radius["z"],
                surface["mandibularRadii"][3]["y"] / radius["y"],
                surface["earShape"]["y"] / radius["y"],
                profile["landmarks"]["leftEyeCenter"]["x"] / radius["x"])

    geometry = [measurements(profile) for profile in profiles]
    ranges = [max(axis) - min(axis) for axis in zip(*geometry)]

    def spread(pair):
        separation = [abs(a - b) / max(.0001, span)
                      for a, b, span in zip(geometry[pair[0]], geometry[pair[1]], ranges)]
        return min(separation) + statistics.mean(separation)

    i, j = max((p for p in combinations(range(len(files)), 2)
                if profiles[p[0]]["archetype"] == profiles[p[1]]["archetype"]), key=spread)
    (output / "fixed-global.json").write_text(json.dumps({
        "global": .5, "archetype": profiles[i]["archetype"], "candidates": [i+1, j+1],
        "ratios": dict(zip(("skull_width_depth", "muzzle_cranium_length", "jaw_cranium_depth",
                            "ear_cranium_height", "eye_cranium_width"), zip(geometry[i], geometry[j])))
    }, indent=2) + "\n")
    chosen = (files[i], files[j])
    capture(renderer, output, chosen)
    sheet(output, output / "fixed-global.png", "Rata × elefante · 50 % Y · mismo arquetipo",
          [f"V3 · {i+1}", f"V3 · {j+1}"], [0, 1])

    appearance_folder = output / "dog-lizard"
    files = export(exporter, appearance_folder, ("@2", "@1"), 8, seed, 1, "--fixed-global")
    recipes = [json.loads(file.read_text()) for file in files]
    # Se conserva el mismo cuerpo y se aplica un segundo fenotipo cromático
    # del lote. Las anclas permanecen válidas al exigir el mismo plan.
    pairs = list(combinations(range(len(files)), 2))
    def plan(index):
        return recipes[index]["recipe"]["bodyPlan"]
    def appearance(pair):
        a, b = (recipes[k]["recipe"]["adult"]["surface"]["regionalAlbedo"] for k in pair)
        return sum(abs(x[channel] - y[channel]) for x, y in zip(a, b) for channel in "rgb")
    i, j = max((p for p in pairs if plan(p[0]) == plan(p[1])), key=appearance, default=(0, 0))
    if i == j:
        raise RuntimeError("El control de apariencia requiere dos hermanos con el mismo plan")
    base, donor = recipes[i], recipes[j]
    for stage in ("seed", "larva", "juvenile", "adult"):
        destination, source = base["recipe"][stage], donor["recipe"][stage]
        for field in ("surface", "dorsalColor", "ventralColor", "unpigmentedVentralColor"):
            destination[field] = source[field]
        for field in ("irisColor", "scleraColor", "pupilColor"):
            destination["eyes"][field] = source["eyes"][field]
    file = output / "palette-variant.creature.json"
    file.write_text(json.dumps(base))
    captures = output / "appearance"
    capture(renderer, captures, [files[i], file])
    sheet(captures, output / "same-geometry.png", "Misma geometría · pigmentación y materiales heredados",
          [f"Superficie {i+1}", f"Superficie {j+1}"], [0, 1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--v2-export", type=Path, required=True)
    parser.add_argument("--v3-export", type=Path, default=ROOT / "build/export_offspring_batch")
    parser.add_argument("--renderer", type=Path, default=ROOT / "build/capture_offspring_comparison")
    parser.add_argument("--repeats", type=int, default=12)
    parser.add_argument("--seed", type=int, default=48216)
    parser.add_argument("--reuse-v2", action="store_true")
    parser.add_argument("--reuse-v3", action="store_true")
    parser.add_argument("--reuse-images", action="store_true")
    parser.add_argument("--skip-controlled", action="store_true")
    args = parser.parse_args()
    output = args.directory.resolve()
    results = {}
    matched_hybrids = (output / "v2/rat-elephant/1.creature.json", output / "v2/rhino-rat/2.creature.json")
    pairs = (*PAIRS, ("hybrids", "Dos híbridos V2 guardados", *matched_hybrids, 5))
    links = []
    for name, title, a, b, count in pairs:
        print(title, flush=True)
        parents = (a, b)
        all_files = list(parents)
        for version, exe, reuse in (("v2", args.v2_export, args.reuse_v2),
                                     ("v3", args.v3_export, args.reuse_v3)):
            folder = output / version / name
            files = [folder / f"{i}.creature.json" for i in range(1, count + 1)] if reuse else (
                export(exe.resolve(), folder, parents, count, args.seed, args.repeats)
            )
            all_files.extend(files)
        gallery = output / "galleries" / name
        if not args.reuse_images:
            capture(args.renderer.resolve(), gallery, all_files)
        results[name] = {}
        for offset, version in ((2, "v2"), (count+2, "v3")):
            indexes = list(range(offset, offset+count))
            filename = f"{version}.png"
            sheet(gallery, gallery / filename, f"{title} · {version.upper()}",
                  ["Padre X", "Padre Y", *(f"{version.upper()} · {i+1}" for i in range(count))],
                  [0, 1, *indexes])
            results[name][version] = metrics(gallery, indexes, output / version / name / "export.tsv")
        links.append(f'<li>{escape(title)} <a href="galleries/{name}/v2.png">V2</a> '
                     f'<a href="galleries/{name}/v3.png">V3</a></li>')
    generation_two = output / "v3" / "v3-hybrids"
    hybrid_parents = (output / "v3/rat-elephant/3.creature.json", output / "v3/rhino-rat/2.creature.json")
    files = [generation_two / f"{i}.creature.json" for i in range(1, 6)] if args.reuse_v3 else (
        export(args.v3_export.resolve(), generation_two, hybrid_parents, 5, args.seed, args.repeats)
    )
    gallery = output / "galleries" / "v3-hybrids"
    if not args.reuse_images:
        capture(args.renderer.resolve(), gallery, [*hybrid_parents, *files])
    sheet(gallery, gallery / "v3.png", "Segunda generación · dos híbridos V3 recargados",
          ["Padre X", "Padre Y", *(f"V3 · {i+1}" for i in range(5))], list(range(7)))
    results["v3-hybrids"] = {"v3": metrics(gallery, list(range(2, 7)), generation_two / "export.tsv")}
    links.append('<li><a href="galleries/v3-hybrids/v3.png">Segunda generación V3</a></li>')
    if not args.skip_controlled:
        controlled(output, args.v3_export.resolve(), args.renderer.resolve(), args.seed)
        links.extend(('<li><a href="controlled/fixed-global.png">Cráneo y orejas con 50 % de aporte Y</a></li>',
                      '<li><a href="controlled/same-geometry.png">Pigmentación sobre la misma anatomía</a></li>'))
    (output / "metrics.json").write_text(json.dumps(results, indent=2) + "\n")
    (output / "index.html").write_text(
        '<!doctype html><html lang="es"><meta charset="utf-8"><title>CreatureOffspring V3</title>'
        '<h1>CreatureOffspring V3</h1><ul>' + "".join(links) + '</ul>'
        '<a href="metrics.json">Medidas</a></html>\n'
    )


if __name__ == "__main__":
    main()
