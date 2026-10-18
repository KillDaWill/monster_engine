#!/usr/bin/env python3
"""Comprueba las salidas reproducibles de interacciones, sin dependencias externas."""
import argparse
import math
import re
from pathlib import Path


def validate(directory):
    expression = re.compile(r"\[INTERACTION RESULT\].*id=(\d+).*meals=(\d+) drinks=(\d+) failures=(\d+) contact=([\d.]+) error=([\d.]+)")
    for case in ("bowl", "ground", "raised", "high", "moving", "blocked"):
        content = (directory / (case + ".log")).read_text()
        records = expression.findall(content)
        if len(records) != 2 or "[ERROR]" in content or "Simulación finalizada tras 720 frames" not in content:
            raise AssertionError(f"{case}: ejecución incompleta o inválida")
        for identity, meals, drinks, failures, contact, error in records:
            values = list(map(float, (meals, drinks, failures, contact, error)))
            if not all(map(math.isfinite, values)):
                raise AssertionError(f"{case}/{identity}: dato no finito")
            meals, drinks, failures = map(int, (meals, drinks, failures))
            if case == "bowl" and drinks < 1:
                raise AssertionError(f"{case}/{identity}: no completó bebida")
            if case in ("ground", "raised", "moving") and meals < 1:
                raise AssertionError(f"{case}/{identity}: no completó comida")
            if case in ("high", "blocked") and (meals or drinks):
                raise AssertionError(f"{case}/{identity}: consumo imposible")
            if case == "high" and failures < 1:
                raise AssertionError(f"{case}/{identity}: no terminó el intento imposible")
        print(f"[PASS] {case}: dos criaturas, contacto o rechazo según el caso")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    validate(parser.parse_args().directory)
