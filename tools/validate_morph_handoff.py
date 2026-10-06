#!/usr/bin/env python3
"""Ejecuta capturas reales, conserva CSV y genera una hoja de revisión por ciclo.

No certifica continuidad a partir de siluetas ni sustituye la inspección visual.
Las ejecuciones GPU son secuenciales para no contaminar sus tiempos.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw


def percentile(values, fraction):
    values = sorted(values)
    index = (len(values) - 1) * fraction
    left = int(index)
    return values[left] + (values[min(left + 1, len(values) - 1)] - values[left]) * (index - left)


def summarize(prefix):
    result = {"prefix": str(prefix)}
    csv_path = prefix.with_suffix(".csv")
    if csv_path.exists():
        rows = list(csv.DictReader(csv_path.open()))
        for name in ("render_gpu_ms", "render_cpu_ms", "presentation_ms", "exact_sdf_submit_ms"):
            values = [float(row[name]) for row in rows if float(row[name]) > 0]
            if values:
                result[name] = dict(zip(("p50", "p95", "p99", "max"),
                    (percentile(values, .5), percentile(values, .95), percentile(values, .99), max(values))))
        result["frames"] = len(rows)
        result["maximum_regions"] = max((int(row["sdf_regions_rendered"]) for row in rows), default=0)
    paths = sorted(prefix.parent.glob(prefix.name + "-*.ppm"))
    result["captures"] = len(paths)
    if paths:
        sheet = Image.new("RGB", (4 * 330, ((len(paths) + 3) // 4) * 270), "#17171e")
        draw = ImageDraw.Draw(sheet)
        for index, path in enumerate(paths):
            picture = Image.open(path)
            picture.thumbnail((330, 245))
            x, y = index % 4 * 330, index // 4 * 270
            sheet.paste(picture, (x, y))
            draw.text((x + 8, y + 246), path.stem, fill="white")
        sheet.save(prefix.with_name(prefix.name + "-review.png"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--delays", nargs="+", type=int, default=[0, 500, 3000])
    parser.add_argument("--views", nargs="+", type=int, default=[2, 9, 1, 8, 3])
    parser.add_argument("--no-fur", action="store_true")
    parser.add_argument("--stress", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    runs = [(delay, view, "cycle") for delay in args.delays for view in args.views]
    if args.stress:
        runs += [(delay, 3, mode) for delay in args.delays for mode in ("burst", "scrub")]
    for delay, view, mode in runs:
        prefix = args.output / f"{mode}-delay{delay}-view{view}"
        command = ["demos/demo_morph_dog_lizard", f"--validate-cycle={prefix}",
            f"--worker-delay={delay}", f"--view={view}"]
        if args.no_fur:
            command.append("--no-fur")
        if mode != "cycle":
            command += [f"--stress-motion={mode}", "--frames=160"]
        print("Ejecutando " + " ".join(command), flush=True)
        try:
            with prefix.with_suffix(".log").open("w") as log:
                completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                    timeout=150, env={**os.environ, "DISPLAY": os.environ.get("DISPLAY", ":0")})
            result = summarize(prefix)
            result["returncode"] = completed.returncode
        except subprocess.TimeoutExpired:
            result = summarize(prefix)
            result["timeout"] = True
        results.append(result)
        (args.output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps(result), flush=True)
    if any(item.get("returncode") != 0 for item in results):
        raise SystemExit("Alguna ejecución falló; revisar logs conservados.")


if __name__ == "__main__":
    main()
