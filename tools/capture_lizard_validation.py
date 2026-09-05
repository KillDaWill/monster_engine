#!/usr/bin/env python3
"""Captura cinco edades y cuatro aperturas mediante los demos OpenGL reales.

Requiere una sesión gráfica, los demos compilados y Pillow para los paneles PNG.
Uso: python3 tools/capture_lizard_validation.py /tmp/validacion-lagarto
"""
import argparse
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
AGES = ('0', '.25', '.5', '.75', '1')
VIEWS = ('whole', 'body-lateral', 'body-front', 'body-dorsal',
         'head-oblique', 'head-lateral', 'head-frontal', 'head-dorsal')


def run_capture(command, log):
    with log.open('w') as stream:
        subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=120)


def panel(files, labels, destination, columns=3):
    result = Image.new('RGB', (512 * columns, 384 * ((len(files)+columns-1)//columns)), 'white')
    draw = ImageDraw.Draw(result)
    for i, (path, label) in enumerate(zip(files, labels)):
        with Image.open(path) as source:
            source.thumbnail((512, 360))
            x, y = i % columns * 512, i // columns * 384
            result.paste(source, (x, y + 24))
            draw.text((x+6, y+5), label, fill='black')
    result.save(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    output = parser.parse_args().output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for age in AGES:
        run_capture([str(ROOT/'demos/demo_ager_3d'), age, f'--capture-prefix={output}/age-{age}'],
                    output/f'age-{age}.log')
    for view in VIEWS:
        panel([output/f'age-{age}-{view}.ppm' for age in AGES],
              [f'Edad {age} - {view}' for age in AGES], output/f'{view}.png')
    apertures = ('0', '.1', '.5', '1')
    for aperture in apertures:
        run_capture([str(ROOT/'demos/demo_mouth_animation'), aperture,
                     f'--capture={output}/mouth-{aperture}.ppm'], output/f'mouth-{aperture}.log')
    panel([output/f'mouth-{a}.ppm' for a in apertures],
          [f'Apertura {a}' for a in apertures], output/'mouth-apertures.png', 2)
    print(output)


if __name__ == '__main__':
    main()
