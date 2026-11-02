#!/usr/bin/env python3
"""Captura cuatro vistas y etapas de équidos usando exclusivamente mallas de C."""
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/equine-validation')
    parser.add_argument('--adult-only', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    for name in ('caballo', 'asno'):
        path = ROOT / 'recipes' / f'{name}.creature.json'
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
                designed_proportions={key: recipe[('seed', 'larva', 'juvenile', 'adult')[stage]]['axial'][key]
                    for key in ('withersHeight', 'trunkLength', 'neckLength', 'thoraxHeight', 'thoraxWidth')}))
        sheet.save(args.output / f'{name}.png')
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(report)} casos finitos con casco y cuatro vistas; {args.output}')


if __name__ == '__main__':
    main()
