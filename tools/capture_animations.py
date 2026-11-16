#!/usr/bin/env python3
"""Captura rápidamente todas las animaciones por especie sin usar demo_ecosystem."""
import argparse
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw

DEFAULT_SPECIES = ["lizard", "dog", "brumacerno", "velkar", "caballo", "asno"]


def build_contact_sheet(images, labels, cols=4, tile_size=(420, 420), out_path=None):
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tile_size[0], rows * tile_size[1]), (22, 24, 28))
    draw = ImageDraw.Draw(sheet)
    for idx, (img_path, label) in enumerate(zip(images, labels)):
        r, c = divmod(idx, cols)
        x0, y0 = c * tile_size[0], r * tile_size[1]
        with Image.open(img_path) as im:
            im_resized = im.convert("RGB").resize(tile_size, Image.Resampling.LANCZOS)
            sheet.paste(im_resized, (x0, y0))
        draw.rectangle([x0 + 6, y0 + 6, x0 + 6 + 8 * len(label) + 10, y0 + 28], fill=(12, 14, 18))
        draw.text((x0 + 11, y0 + 10), label, fill=(235, 240, 245))
    if out_path:
        sheet.save(out_path)
    return sheet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species", default="all",
                        help="Lista separada por comas de especies (lizard,dog,brumacerno,velkar,caballo,asno, id o ruta .creature.json) o 'all'")
    parser.add_argument("--anims", default="all",
                        help="Lista separada por comas de animaciones o 'all'")
    parser.add_argument("--views", default="side,three-quarter",
                        help="Vistas: side,three-quarter,front,top,rear")
    parser.add_argument("--focus", choices=["body", "neck", "head"], default="body")
    parser.add_argument("--stage", type=int, default=3, choices=[0, 1, 2, 3])
    parser.add_argument("--flat", action="store_true", help="Renderizado gris neutro para silueta")
    parser.add_argument("--size", type=int, default=800)
    parser.add_argument("--output", type=Path, default=Path("/tmp/monster_animations"))
    parser.add_argument("--no-sheet", action="store_true", help="No generar hoja de contacto")
    args = parser.parse_args()

    subprocess.run(["make", "-j4", "build/capture_animations"], check=True)
    species_list = DEFAULT_SPECIES if args.species == "all" else [s.strip() for s in args.species.split(",") if s.strip()]

    summary = {}
    for sp in species_list:
        safe_name = Path(sp).stem.replace(".creature", "")
        sp_dir = args.output / safe_name
        sp_dir.mkdir(parents=True, exist_ok=True)
        cmd = [
            "./build/capture_animations",
            sp,
            str(sp_dir),
            f"--stage={args.stage}",
            f"--anims={args.anims}",
            f"--views={args.views}",
            f"--focus={args.focus}",
            f"--width={args.size}",
            f"--height={args.size}",
        ]
        if args.flat:
            cmd.append("--flat")
        print(f"=== Capturando {safe_name} ===", flush=True)
        subprocess.run(cmd, check=True)

        pngs_by_view = {}
        for ppm in sorted(sp_dir.glob("*.ppm")):
            png = ppm.with_suffix(".png")
            with Image.open(ppm) as im:
                im.save(png)
            ppm.unlink()
            stem = png.stem
            view = stem.rsplit("_", 1)[-1]
            pngs_by_view.setdefault(view, []).append((png, stem.rsplit("_", 1)[0]))

        if not args.no_sheet:
            for view, items in pngs_by_view.items():
                sheet_path = sp_dir / f"_sheet_{safe_name}_{view}.png"
                build_contact_sheet([p for p, _ in items], [lbl for _, lbl in items], cols=4, out_path=sheet_path)
                print(f"Hoja de contacto: {sheet_path}", flush=True)

        metrics_file = sp_dir / "metrics.json"
        if metrics_file.exists():
            summary[safe_name] = json.loads(metrics_file.read_text())

    if summary:
        (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        print(f"Resumen guardado en {args.output / 'summary.json'}")


if __name__ == "__main__":
    main()
