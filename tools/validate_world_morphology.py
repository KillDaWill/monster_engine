#!/usr/bin/env python3
"""Comparación física del generador original y la receta actual sobre los mismos parámetros."""
import argparse
import csv
import json
import re
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PRESET_SEEDS = {
    "river_valley": 42, "mountain_lakes": 108, "rocky_highlands": 317,
    "wetlands": 707, "lab_valley_small": 42,
}
SEEDS = (42, 108, 317, 707, 5, 13, 19, 23, 31, 47, 61, 79, 97, 113, 227, 461)


def read_dump(path):
    data = path.read_bytes()
    side, step, ox, oz = np.frombuffer(data, np.float32, 4)
    side = int(side)
    shape = side, side
    planes = np.frombuffer(data, np.float32, 8 * side * side, 16).reshape(8, *shape)
    domain, wet = np.frombuffer(data, np.uint16, offset=16 + planes.nbytes).reshape(2, *shape)
    assert np.isfinite(planes[:7]).all()
    assert np.all(domain[wet > 0] == wet[wet > 0])
    assert np.all(planes[2][wet > 0] > planes[1][wet > 0])
    return planes, domain, wet, step, ox, oz


def percentiles(field):
    if not field.size:
        return None
    return list(map(float, np.percentile(field, [50, 95, 99, 100])))


def diagnostics(data, lakes):
    planes, domain, wet, step, _, _ = data
    base, height, water, depth, fx, fz, _, water_distance = planes
    dz, dx = np.gradient(height, step)
    slope = np.degrees(np.arctan(np.hypot(dx, dz)))
    mid = height[1:-1, 1:-1]
    curvature = np.abs(height[1:-1, :-2] + height[1:-1, 2:] +
                       height[:-2, 1:-1] + height[2:, 1:-1] - 4 * mid) / (step * step)
    fields = {"cut_volume": float(-np.minimum(height - base, 0).sum() * step * step),
              "fill_volume": float(np.maximum(height - base, 0).sum() * step * step)}
    for lower, upper in ((0, 1), (1, 3), (3, 8)):
        band = (wet == 0) & (water_distance >= lower) & (water_distance < upper)
        fields[f"shore_slope_{lower}_{upper}"] = percentiles(slope[band])
        fields[f"shore_curvature_{lower}_{upper}"] = percentiles(curvature[band[1:-1, 1:-1]])
        fields[f"steep_bank_area_{lower}_{upper}"] = float(np.count_nonzero(band & (slope > 30)) * step * step)

    # Exceso de pendiente introducido por el generador en una arista de
    # agua/tierra o entre masas de agua distintas, respecto al terreno base.
    added = []
    for axis in (0, 1):
        boundary = np.diff(wet, axis=axis) != 0
        slope_change = (np.abs(np.diff(height, axis=axis)) - np.abs(np.diff(base, axis=axis))) / step
        added.append(slope_change[boundary])
    fields["added_steep_shore_edges"] = int(np.count_nonzero(np.concatenate(added) > .5))

    # Derivada longitudinal raster sólo en pares húmedos del mismo río.
    # Esto permite detectar un mal encuentro visible aunque el grafo descienda.
    grades = []
    for axis, flow in ((0, fz), (1, fx)):
        a = (slice(None, -1), slice(None)) if axis == 0 else (slice(None), slice(None, -1))
        b = (slice(1, None), slice(None)) if axis == 0 else (slice(None), slice(1, None))
        same_river = (wet[a] > lakes) & (wet[a] == wet[b])
        aligned = np.abs(flow[a]) >= .55 * np.hypot(fx[a], fz[a])
        selected = same_river & aligned
        diff = (water[b] - water[a]) / step
        grades.append((diff * np.sign(flow[a]))[selected])
    grades = np.concatenate(grades)
    fields["raster_along_flow_absolute_grade"] = percentiles(np.abs(grades))
    fields["raster_along_flow_uphill_pairs"] = int(np.count_nonzero(grades > .005))
    fields["raster_along_flow_pairs"] = int(grades.size)
    fields["lake_cells_by_owner"] = list(map(int, np.bincount(wet.ravel(), minlength=lakes + 1)[1:lakes + 1]))
    return fields, slope


def render_maps(path, data, slope):
    planes, domain, wet, _, _, _ = data
    base, height, water, depth, _, _, accum, _ = planes
    delta = np.clip((height - base) / 3.0, -1, 1)
    delta_image = np.stack((1 + np.minimum(delta, 0), 1 - np.abs(delta), 1 - np.maximum(delta, 0)), -1)
    rgb_owner = ((wet[..., None].astype(np.uint32) * np.array([7919, 1543, 1681])) % 191 + 63)
    rgb_owner[wet == 0] = 24
    panels = (
        ("Relieve tras erosión", (base - base.min()) / max(np.ptp(base), 1e-5)),
        ("Relieve final", (height - base.min()) / max(np.ptp(base), 1e-5)),
        ("Corte azul / relleno rojo", delta_image),
        ("Pendiente (0 a 45 grados)", slope / 45.0),
        ("Acumulación de drenaje", np.log1p(accum) / max(1, np.log1p(accum.max()))),
        ("Profundidad mojada (0 a 2 m)", depth / 2.0),
        ("Identidad húmeda", rgb_owner / 255.0),
        ("Profundidad firmada (+/- 1 m)", np.where(domain > 0, .5 + .5 * (water - height), 0)),
    )
    side = 250
    sheet = Image.new("RGB", (4 * side, 2 * (side + 23)), "#1b222c")
    draw = ImageDraw.Draw(sheet)
    for i, (title, values) in enumerate(panels):
        image = Image.fromarray(np.round(np.clip(values, 0, 1) * 255).astype(np.uint8))
        x, y = i % 4 * side, i // 4 * (side + 23)
        sheet.paste(image.resize((side, side)), (x, y + 23))
        draw.text((x + 5, y + 5), title, fill="white")
    sheet.save(path)


def cross_sections(path, data, lakes):
    planes, _, wet, step, ox, oz = data
    base, ground, water, _, fx, fz, accum, _ = planes
    captures = []
    for kind, eligible in (("river", wet > lakes), ("lake", (wet > 0) & (wet <= lakes))):
        # Elegir muestras interiores y espaciarlas en unidades de mundo.
        candidates = np.argwhere(eligible)
        if not candidates.size:
            continue
        sampled = candidates[::max(1, len(candidates) // 1000)]
        scores = accum[sampled[:, 0], sampled[:, 1]]
        for z, x in sampled[np.argsort(-scores, kind="stable")]:
            if min(x, z, ground.shape[0] - 1 - x, ground.shape[0] - 1 - z) * step < 7:
                continue
            if any(kind == row[0] and np.hypot(z - row[1], x - row[2]) * step < 8 for row in captures):
                continue
            captures.append((kind, z, x))
            if sum(row[0] == kind for row in captures) == 10:
                break
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("kind", "section", "offset", "x", "z", "base", "final", "water", "wet_owner"))
        for i, (kind, z, x) in enumerate(captures):
            vx, vz = float(fx[z, x]), float(fz[z, x])
            if np.hypot(vx, vz) < .01:
                vx, vz = 0, 1
            norm = np.hypot(vx, vz)
            for offset in np.linspace(-6.0, 6.0, 49):
                px = int(round(x - vz * offset / (step * norm)))
                pz = int(round(z + vx * offset / (step * norm)))
                writer.writerow((kind, i, offset, ox + px * step, oz + pz * step,
                                 float(base[pz, px]), float(ground[pz, px]),
                                 float(water[pz, px]) if wet[pz, px] else "", int(wet[pz, px])))


def graph_metrics(prefix, data):
    planes, _, _, step, ox, oz = data
    shape = planes[0].shape
    def field(name, dtype):
        return np.fromfile(f"{prefix}-{name}.bin", dtype=dtype)
    parent = field("drainage_parent_i32", np.int32)
    channel = field("channel_u8", np.uint8).astype(bool)
    x = field("channel_x_f32", np.float32)
    z = field("channel_z_f32", np.float32)
    water = field("channel_surface_f32", np.float32)
    routing = field("routing_dem_f32", np.float32).reshape(shape)
    filled = field("filled_dem_f32", np.float32).reshape(shape)
    carved = field("carved_ground_f32", np.float32).reshape(shape)
    winner = field("segment_winner_u32", np.uint32).reshape(shape)
    inc = np.flatnonzero(channel & (parent >= 0) & channel[np.maximum(parent, 0)])
    downstream = parent[inc]
    run = step * np.hypot(x[inc] - x[downstream], z[inc] - z[downstream])
    valid = run > 1e-6
    inc, downstream, run = inc[valid], downstream[valid], run[valid]
    grade = (water[inc] - water[downstream]) / run
    grade_at = np.full(len(parent), np.nan)
    grade_at[inc] = grade
    slope_change = (grade - grade_at[downstream]) / run
    assert np.all(grade > -1e-5), f"Perfil invertido en {prefix}"
    with Path(f"{prefix}-profile.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("node", "parent", "x", "z", "surface", "length", "downhill_grade", "grade_change"))
        for i, p, length, value, change in zip(inc, downstream, run, grade, slope_change):
            writer.writerow((i, p, ox + x[i] * step, oz + z[i] * step, water[i], length, value, change))
    score = (winner.astype(np.uint64) * 104729) % 231 + 24
    score[winner == 0] = 0
    sheet = Image.new("RGB", (1000, 275), "#1b222c")
    draw = ImageDraw.Draw(sheet)
    for i, (title, pixels) in enumerate((
        ("DEM de drenaje", (routing - routing.min()) / max(1e-5, np.ptp(routing))),
        ("Depresión hasta vertedero", (filled - routing) / 2),
        ("Segmento de referencia", score / 255),
        ("Cambio al relajar (+/- .25 m)", .5 + 2 * (planes[1] - carved)),
    )):
        sheet.paste(Image.fromarray(np.round(255 * np.clip(pixels, 0, 1)).astype(np.uint8)).resize((250, 250)),
                    (250 * i, 25))
        draw.text((250 * i + 5, 6), title, fill="white")
    sheet.save(f"{prefix}-stages.png")
    return {"graph_grade": percentiles(grade), "graph_grade_change": percentiles(np.abs(slope_change[np.isfinite(slope_change)])),
            "graph_rapid_edges_gt_0_20": int(np.count_nonzero(grade > .20)),
            "graph_length": float(run.sum())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/validation/world-morphology")
    parser.add_argument("--seeds", type=int, nargs="+", default=SEEDS)
    parser.add_argument("--large", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    jobs = [(name, seed, None) for name in PRESET_SEEDS for seed in args.seeds]
    if args.large:
        jobs.append(("river_valley", 42, 1025))
    for name, seed, resolution in jobs:
        for version in (1, 4):
            key = f"{name}_v{version}"
            binary = out / "current.bin"
            command = [str(ROOT / "build/audit_world_surface"), f"worlds/{name}.world.json",
                       f"--seed={seed}", f"--dump={binary}"]
            if version == 1:
                command.append("--generator-version=1")
            representative = seed == PRESET_SEEDS[name] and resolution is None
            trace = out / key
            if representative and version == 4:
                command.append(f"--trace={trace}")
            if resolution:
                command.append(f"--resolution={resolution}")
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=True)
            values = dict(re.findall(r"(\w+)=([^\s]+)", result.stdout))
            lakes = int(values["lakes"])
            data = read_dump(binary)
            metrics, slope = diagnostics(data, lakes)
            if representative and version == 4:
                metrics.update(graph_metrics(trace, data))
            rows.append({**values, **metrics, "name": name, "version": version,
                         "seed": seed, "resolution": data[1].shape[0]})
            if representative:
                render_maps(out / f"{key}.png", data, slope)
                cross_sections(out / f"{key}-sections.csv", data, lakes)
            binary.unlink()
            print(f"{key} seed={seed} fp={values['fingerprint']} gen={values['generation_ms']} ms", flush=True)
    (out / "metrics.json").write_text(json.dumps(rows, indent=2) + "\n")


if __name__ == "__main__":
    main()
