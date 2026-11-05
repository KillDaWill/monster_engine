#!/usr/bin/env python3
"""Compone evidencia real del renderer; no retoca el pelo ni la iluminación."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
ADULT = ROOT / 'resultado/caballo-curve-stage-3'


def comparison(before, after, output, crop=None):
    panels = []
    for path in (before, after):
        with Image.open(path) as source:
            image = source.convert('RGB')
            if crop:
                image = image.crop(crop).resize((1000, 600))
            panels.append(image)
    width, height = panels[0].size
    canvas = Image.new('RGB', (width * 2, height + 40), 'white')
    draw = ImageDraw.Draw(canvas)
    for i, (label, panel) in enumerate(zip(('Antes: referencia original', 'Resultado: folículos sobre la piel'), panels)):
        canvas.paste(panel, (i * width, 40))
        draw.text((i * width + 16, 12), label, fill='black')
    canvas.save(ROOT / output)


for view in ('side', 'front', 'rear-three-quarter'):
    comparison(ROOT / f'crin-{view}.png', ADULT / f'{view}-q3.png', f'comparacion-crin-{view}.png')
comparison(ROOT / 'crin-side.png', ADULT / 'side-q3.png', 'comparacion-raiz.png', (200, 230, 700, 530))
frames = [Image.open(ADULT / f'pose-{i}.png').convert('RGB') for i in range(3)]
frames[0].save(ROOT / 'crin-poses.gif', save_all=True, append_images=frames[1:], duration=650, loop=0)
