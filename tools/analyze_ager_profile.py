#!/usr/bin/env python3
"""Resume CSV del demo ager: tiempos, escala y ciclos completos observados.

Uso: python3 tools/analyze_ager_profile.py /tmp/ager-*.csv > resumen.json
Los percentiles interpolan linealmente entre muestras ordenadas, sin recortar
calentamiento adicional al que ya excluye el demo. gpu_ms es la última consulta
completada; cpu_ms sólo cubre preparación anatómica, no todas las llamadas GL.
"""
import argparse
import csv
import json
import math
from pathlib import Path
from statistics import mean


def summarize(path):
    with path.open() as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError(f"CSV vacío: {path}")
    result = {"frames": len(rows)}
    for column in ("frame_ms", "cpu_ms", "gpu_ms", "resolution_scale"):
        values = sorted(float(row[column]) for row in rows)
        if not all(map(math.isfinite, values)):
            raise ValueError(f"Valor no finito: {path}: {column}")

        def percentile(q):
            position = (len(values) - 1) * q / 100
            index = int(position)
            return values[index] + (values[min(index + 1, len(values) - 1)] - values[index]) * (position - index)

        result[column] = {"mean": mean(values), "p50": percentile(50),
                          "p95": percentile(95), "p99": percentile(99),
                          "max": values[-1], "min": values[0]}
    scales = [float(row["resolution_scale"]) for row in rows]
    steps = [b - a for a, b in zip(scales, scales[1:])]
    result["scale_changes"] = {"initial": scales[0], "final": scales[-1],
                               "max_drop": max([0] + [-x for x in steps]),
                               "max_rise": max([0] + steps),
                               "native_fraction": mean(x >= .99999 for x in scales)}
    cycles, adult = 0, False
    for row in rows:
        age = float(row["age"])
        if age >= .99999:
            adult = True
        elif age <= .00001 and adult:
            cycles += 1
            adult = False
    result["adult_to_larva_returns"] = cycles
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, nargs="+")
    args = parser.parse_args()
    print(json.dumps({str(path): summarize(path) for path in args.csv}, indent=2))


if __name__ == "__main__":
    main()
