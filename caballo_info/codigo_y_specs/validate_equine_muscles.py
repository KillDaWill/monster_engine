#!/usr/bin/env python3
"""Captura la cohesión muscular real en OpenGL, sin sustituir mallas del motor."""
import argparse
import json
from pathlib import Path
import subprocess
import time

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
VIEWS = ('full-side', 'full-front', 'full-three-quarter', 'shoulder-side',
         'shoulder-front', 'shoulder-three-quarter', 'hip-side', 'hip-rear',
         'hip-three-quarter')


def capture(binary, recipe, folder, stage):
    """Conserva el registro original y convierte únicamente los PPM capturados."""
    folder.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    result = subprocess.run([str(binary), str(recipe), str(folder), str(stage), 'muscle'],
                            cwd=ROOT, capture_output=True, text=True)
    (folder / 'capture.log').write_text(result.stdout + result.stderr)
    result.check_returncode()
    for source in folder.glob('*.ppm'):
        with Image.open(source) as image:
            image.save(source.with_suffix('.png'))
        source.unlink()
    return dict(seconds=time.perf_counter() - start,
                walking='walk=CAPTURED' in result.stdout)


def comparison(before, after, destination):
    """Mismos encuadres regionales; la vista completa ajusta su caja corporal."""
    sheet = Image.new('RGB', (1200, len(VIEWS) * 475), 'white')
    draw = ImageDraw.Draw(sheet)
    for row, view in enumerate(VIEWS):
        for col, (folder, label) in enumerate(((before, 'ANTES'), (after, 'DESPUÉS'))):
            with Image.open(folder / f'{view}.png') as image:
                image.thumbnail((600, 450))
                sheet.paste(image, (col * 600, row * 475))
            draw.text((col * 600 + 10, row * 475 + 455), f'{label} | {view}', fill='black')
    sheet.save(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/equine-muscles')
    parser.add_argument('--baseline-capture', type=Path,
                        help='Capturador nuevo enlazado con el núcleo local anterior')
    parser.add_argument('--all-stages', action='store_true')
    args = parser.parse_args()
    args.output = args.output.resolve()
    subprocess.run(['make', 'build/capture_bovine_anatomy'], cwd=ROOT, check=True)
    results = []
    for name in ('caballo', 'asno'):
        recipe = ROOT / 'recipes' / f'{name}.creature.json'
        for stage in (range(4) if args.all_stages else (3,)):
            suffix = name if stage == 3 else f'{name}-stage-{stage}'
            after = args.output / 'after' / suffix
            row = dict(recipe=name, stage=stage,
                       after=capture(ROOT / 'build/capture_bovine_anatomy', recipe, after, stage))
            if args.baseline_capture:
                before = args.output / 'before' / suffix
                row['before'] = capture(args.baseline_capture.resolve(), recipe, before, stage)
                comparison(before, after, args.output / f'{suffix}-comparison.png')
            results.append(row)
    (args.output / 'captures.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'PASS: {len(results)} capturas anatómicas; {args.output}')


if __name__ == '__main__':
    main()
