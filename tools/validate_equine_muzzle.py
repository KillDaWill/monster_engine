#!/usr/bin/env python3
"""Captura el hocico real en GL y conserva pruebas CPU/GPU, sin imágenes sintéticas."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def verify_geometry(baseline):
    """Compara posiciones/normales/índices y exige pigmento visible en el puente."""
    import numpy as np
    sys.path.insert(0, str(ROOT/'tools/creature_editor'))
    from render_preview import read_mesh
    rows = []
    for name in ('caballo','asno'):
        for stage in range(4):
            data = []
            for root in (baseline, ROOT):
                data.append(subprocess.check_output([str(root/'build/creature_editor_bridge'),
                    'preview',str(root/f'recipes/{name}.creature.json'),str(stage),'draft'],cwd=root))
            meta = [json.loads(blob[4:4+struct.unpack_from('<I',blob)[0]]) for blob in data]
            assert meta[0]['meshes']==meta[1]['meshes'], 'Cambian conteos/materiales'
            changed = []
            for (a,ia),(b,ib) in zip(read_mesh(data[0]),read_mesh(data[1])):
                assert np.array_equal(ia,ib), 'Cambian índices'
                assert np.array_equal(a[:,:6],b[:,:6]), 'Cambian posiciones o normales'
                mask=np.any(a[:,6:]!=b[:,6:],axis=1)
                changed.extend(b[mask,:3].tolist())
            assert changed, 'El preview no muestra los dominios localizados'
            points=np.array(changed)
            rows.append(dict(species=name,stage=stage,max_geometry_delta=0,indices_exact=True,
                changed_color_vertices=len(changed),color_change_bounds=[points.min(axis=0).tolist(),points.max(axis=0).tolist()]))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/validation/equine-muzzle')
    parser.add_argument('--all-stages', action='store_true')
    parser.add_argument('--baseline-root',type=Path,help='Snapshot anterior con puente compilado y recetas originales.')
    parser.add_argument('--geometry-only',action='store_true',help='Sólo comparar geometría/preview con --baseline-root.')
    args = parser.parse_args()
    if args.geometry_only and not args.baseline_root:
        parser.error('--geometry-only requiere --baseline-root')
    if args.baseline_root:
        rows=verify_geometry(args.baseline_root.resolve());args.output.mkdir(parents=True,exist_ok=True)
        (args.output/'equine-geometry-regression.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('Ocho mallas équidas idénticas; pigmento visible en el preview',flush=True)
    if args.geometry_only:
        return
    binary = ROOT/'build/capture_equine_muzzle'
    records = []
    for name in ('caballo', 'asno'):
        for stage in (range(4) if args.all_stages else (3,)):
            case = name if stage == 3 else f'{name}-stage{stage}'
            directory = args.output/'after'/case
            directory.mkdir(parents=True, exist_ok=True)
            command = [str(binary), str(ROOT/f'recipes/{name}.creature.json'), str(directory), str(stage)]
            with (directory/'capture.log').open('w') as log:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            for ppm in directory.glob('*.ppm'):
                with Image.open(ppm) as image:
                    image.save(ppm.with_suffix('.png'))
                ppm.unlink()
            records.append(dict(case=case, stage=stage, command=command,
                binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
            print(case, 'PASS', flush=True)
        for kind in ('appearance', 'pigment', 'mask', 'fur', 'roughness'):
            sources = [(args.output/'before'/name/f'{kind}-three-quarter.png', 'Antes'),
                       (args.output/'after'/name/f'{kind}-three-quarter.png', 'Después')]
            if not all(file.exists() for file, _ in sources):
                continue
            canvas = Image.new('RGB', (1200, 574), '#0d0d14')
            draw = ImageDraw.Draw(canvas)
            for i, (file, title) in enumerate(sources):
                image = Image.open(file).convert('RGB').resize((600, 540))
                canvas.paste(image, (i*600, 34));draw.text((i*600+12, 10), title, fill='white')
            canvas.save(args.output/f'{name}-{kind}-comparison.png')
    (args.output/'captures.json').write_text(json.dumps(records, indent=2)+'\n')


if __name__ == '__main__':
    main()
