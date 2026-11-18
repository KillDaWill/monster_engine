#!/usr/bin/env python3
"""Regenera el atlas integrado a partir de Fira Code Regular (requiere Pillow)."""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("font", type=Path, help="ruta de FiraCode-Regular.ttf")
args = parser.parse_args()
font = ImageFont.truetype(str(args.font), 32)
atlas = Image.new("L", (512, 512))
draw = ImageDraw.Draw(atlas)
codepoints = [*range(32, 127), *range(160, 384), 0xfffd]
missing = bytes(font.getmask(chr(0x10ffff)))
for index, cp in enumerate(codepoints):
    x, y = index % 25 * 20, index // 25 * 38
    char = chr(cp)
    if bytes(font.getmask(char)) == missing:
        char = "\ufffd"
    draw.text((x, y + 29), char, 255, font=font, anchor="ls")
atlas.paste(255, (508, 508, 512, 512))

runs = []
previous, count = 0, 0
for alpha in atlas.getdata():
    level = (alpha * 15 + 127) // 255
    if count and (level != previous or count == 16):
        runs.append((previous << 4) | (count - 1))
        count = 0
    previous = level
    count += 1
runs.append((previous << 4) | (count - 1))
output = Path(__file__).resolve().parents[1] / "assets/interface/fonts/InterfaceAtlas.inc"
with output.open("w", encoding="ascii") as stream:
    stream.write("/* Generado por tools/generate_interface_atlas.py. Licencia: OFL.txt. */\n")
    for i in range(0, len(runs), 24):
        stream.write("    " + ", ".join(f"0x{byte:02x}" for byte in runs[i:i+24]) + ",\n")
