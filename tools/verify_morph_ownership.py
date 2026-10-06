#!/usr/bin/env python3
"""Comprueba propiedad visible comparando siluetas PPM del morph y SDF exacto."""

import argparse
from pathlib import Path


def read_ppm(path: Path):
    data = path.read_bytes()
    offset = 0

    def token():
        nonlocal offset
        while offset < len(data):
            if data[offset] == 35:
                while offset < len(data) and data[offset] not in (10, 13):
                    offset += 1
            elif data[offset] in (9, 10, 13, 32):
                offset += 1
            else:
                break
        start = offset
        while offset < len(data) and data[offset] not in (9, 10, 13, 32):
            offset += 1
        return data[start:offset]

    if token() != b"P6":
        raise ValueError(f"{path}: se esperaba PPM binario P6")
    width, height, maximum = (int(token()) for _ in range(3))
    if maximum != 255 or width <= 0 or height <= 0:
        raise ValueError(f"{path}: cabecera PPM no compatible")
    if offset >= len(data) or data[offset] not in (9, 10, 13, 32):
        raise ValueError(f"{path}: falta el separador de la cabecera")
    offset += 1
    pixels = data[offset:]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: tamaño de píxeles incorrecto")
    return width, height, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path, help="captura SDF exacta")
    parser.add_argument("candidate", type=Path, help="captura del morph asíncrono")
    parser.add_argument("--maximum-extra-ratio", type=float, default=0.10)
    parser.add_argument("--maximum-missing-ratio", type=float, default=0.10)
    args = parser.parse_args()
    ref_width, ref_height, reference = read_ppm(args.reference)
    width, height, candidate = read_ppm(args.candidate)
    if (width, height) != (ref_width, ref_height):
        raise ValueError("Las capturas tienen dimensiones diferentes")
    background = reference[:3]
    reference_count = extra = missing = 0
    for index in range(0, len(reference), 3):
        present_ref = max(abs(reference[index + c] - background[c]) for c in range(3)) > 20
        present_candidate = max(abs(candidate[index + c] - background[c]) for c in range(3)) > 20
        reference_count += present_ref
        extra += present_candidate and not present_ref
        missing += present_ref and not present_candidate
    if not reference_count:
        raise ValueError("La captura de referencia está vacía")
    extra_ratio = extra / reference_count
    missing_ratio = missing / reference_count
    print(f"referencia={reference_count} extra={extra} ({extra_ratio:.3f}) "
          f"faltante={missing} ({missing_ratio:.3f})")
    if extra_ratio > args.maximum_extra_ratio or missing_ratio > args.maximum_missing_ratio:
        raise SystemExit("[FAIL] Propiedad visual del morph fuera del límite")
    print("[PASS] Silueta sin cuerpo obsoleto visible")


if __name__ == "__main__":
    main()
