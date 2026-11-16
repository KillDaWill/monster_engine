#!/usr/bin/env python3
"""Valida etapas y captura vistas diagnósticas de équidos con mallas C y OpenGL."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from validate_anatomy_dependencies import compare, metadata
sys.path.insert(0, str(ROOT / 'tools/creature_editor'))
from render_preview import read_mesh, render

def capture_opengl(recipe, output, material=False):
    """Captura geometría C real y conserva vistas, métricas y hoja reproducible."""
    output.mkdir(parents=True, exist_ok=True)
    command = [str(ROOT / 'build/capture_bovine_anatomy'), str(recipe), str(output), '3']
    if material:
        command.append('surface')
    subprocess.run(command, cwd=ROOT, check=True)
    make_sheet(output)


def make_sheet(output):
    views = ['side', 'front', 'three-quarter', 'rear', 'top', 'head-side',
             'head-front', 'head-three-quarter', 'closed', 'partial', 'open',
             'upper-head', 'jaw-alone', 'jaw-envelope', 'jaw-unclipped', 'head-section',
             'neck', 'shoulder-chest', 'hip-hock', 'foot-0-side']
    sheet = Image.new('RGB', (1800, ((len(views)+2)//3)*355), 'white')
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(views):
        source = output / f'{name}.ppm'
        if not source.exists():
            source = output / f'{name}.png'
        with Image.open(source) as image:
            image.save(output / f'{name}.png')
            image.thumbnail((600, 330))
            sheet.paste(image, (i % 3*600, i // 3*355))
        draw.text((i % 3*600+10, i // 3*355+335), name, fill='black')
    sheet.save(output / 'sheet.png')
    # Los PPM son intermediarios de esta captura, no fuentes del proyecto.
    for source in output.glob('*.ppm'):
        with Image.open(source) as image:
            image.save(source.with_suffix('.png'))
        source.unlink()

def comparison_sheet(before, after, destination):
    """Compara capturas existentes sin alterar su geometría ni iluminación."""
    views = [name for name in ('head-side', 'jaw-alone', 'closed', 'side', 'front',
             'shoulder-chest', 'hip-hock') if (before / f'{name}.png').exists()]
    sheet = Image.new('RGB', (1200, len(views)*475), 'white')
    draw = ImageDraw.Draw(sheet)
    for row, name in enumerate(views):
        for column, (directory, label) in enumerate(((before, 'ANTES'), (after, 'DESPUES'))):
            with Image.open(directory / f'{name}.png') as image:
                image.thumbnail((600, 450))
                sheet.paste(image, (column*600, row*475))
            draw.text((column*600+10, row*475+455), f'{label} | {name}', fill='black')
    sheet.save(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/equine-validation')
    parser.add_argument('--adult-only', action='store_true')
    parser.add_argument('--opengl', action='store_true', help='Capturar también arcilla y materiales en OpenGL')
    parser.add_argument('--compare-with', type=Path, help='Carpeta con before/ y before-asno/ de la revisión base')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    for name in ('caballo', 'asno'):
        path = ROOT / 'recipes' / f'{name}.creature.json'
        if args.opengl:
            for material in (False, True):
                capture_opengl(path, args.output / f'{name}-{"surface" if material else "clay"}', material)
        recipe = json.loads(path.read_text())['recipe']
        stages = (3,) if args.adult_only else range(4)
        sheet = Image.new('RGB', (2000, 360*len(stages)), (245, 246, 241))
        for row, stage in enumerate(stages):
            command = [str(ROOT / 'build/creature_editor_bridge'), 'preview', str(path), str(stage), 'fine']
            start = time.perf_counter()
            data = subprocess.check_output(command, cwd=ROOT)
            seconds = time.perf_counter()-start
            info, meshes = metadata(data), read_mesh(data)
            assert all(np.isfinite(v).all() for v, _ in meshes)
            assert sum(m['materialCounts']['hoof'] for m in info['meshes']) > 0
            if not args.adult_only:
                compare(data, subprocess.check_output(command, cwd=ROOT))
            for column, (view, yaw, pitch) in enumerate((('lateral', np.pi/2, 0),
                    ('frontal', 0, 0), ('tres cuartos', .95, .22), ('superior', 0, np.pi/2))):
                sheet.paste(render(meshes, (500, 330), yaw, pitch), (500*column, 360*row))
                ImageDraw.Draw(sheet).text((500*column+10, 360*row+337),
                    f'{name} | etapa {stage} | {view}', fill=(36, 61, 62))
            positions = np.concatenate([v[:, :3] for v, _ in meshes])
            report.append(dict(recipe=name, stage=stage, seconds=seconds,
                vertices=sum(len(v) for v, _ in meshes), triangles=sum(len(i) for _, i in meshes),
                minimum=positions.min(axis=0).tolist(), maximum=positions.max(axis=0).tolist(),
                materials=[m['materialCounts'] for m in info['meshes']], nodes=len(info['nodes']),
                resolved_nodes=info['nodes'],
                mesher=info.get('mesher'),
                designed_proportions={key: recipe[('seed', 'larva', 'juvenile', 'adult')[stage]]['axial'][key]
                    for key in ('withersHeight', 'trunkLength', 'neckLength', 'thoraxHeight', 'thoraxWidth')}))
        sheet.save(args.output / f'{name}.png')
        if args.compare_with:
            before = args.compare_with / ('before-asno' if name == 'asno' else 'before')
            comparison_sheet(before, args.output / f'{name}-clay', args.output / f'{name}-comparison.png')
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(report)} casos finitos con casco y cuatro vistas; {args.output}')


if __name__ == '__main__':
    main()
