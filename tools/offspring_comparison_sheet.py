#!/usr/bin/env python3
"""Compone vistas alineadas y mide el solapamiento con el morph de referencia."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


def silhouette(image):
    background = Image.new("RGB", image.size, image.getpixel((0, 0)))
    return ImageChops.difference(image, background).convert("L").point(lambda v: 255 if v > 5 else 0)


def overlap(a, b):
    intersection = ImageChops.multiply(a, b).histogram()[255]
    union = ImageChops.lighter(a, b).histogram()[255]
    return intersection / union if union else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("title")
    parser.add_argument("--roundtrip", action="store_true")
    args = parser.parse_args()
    columns = ["Parent X", "CreatureMorph", "Live CO V2" if args.roundtrip else "CO V1",
               "Reloaded CO V2" if args.roundtrip else "CO V2", "Parent Y"]
    views = [("side", "Side"), ("front", "Front"), ("three-quarter", "Three-quarter"),
             ("head", "Head, three-quarter"), ("head-side", "Head, side")]
    width, height, left = 344, 258, 118
    canvas = Image.new("RGB", (left + 5 * width, 72 + len(views) * height), "#182126")
    draw = ImageDraw.Draw(canvas)
    draw.text((18, 14), args.title, fill="#edf7f7")
    for column, name in enumerate(columns):
        draw.text((left + column * width + 10, 49), name, fill="#deeeee")
    result = {}
    for row, (view, label) in enumerate(views):
        y = 72 + row * height
        draw.text((9, y + 12), label, fill="#deeeee")
        frames = []
        for column in range(len(columns)):
            with Image.open(args.directory / f"{view}-{column}.ppm") as source:
                image = source.convert("RGB")
            frames.append(image)
            canvas.paste(image.resize((width - 4, height - 4), Image.Resampling.LANCZOS),
                         (left + column * width, y))
        target = silhouette(frames[1])
        result[view] = {"v1": overlap(silhouette(frames[2]), target),
                        "v2": overlap(silhouette(frames[3]), target)}
        if args.roundtrip:
            result[view]["live_equals_reloaded"] = ImageChops.difference(frames[2], frames[3]).getbbox() is None
    canvas.save(args.directory / "comparison.png")
    (args.directory / "silhouettes.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
