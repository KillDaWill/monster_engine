#!/usr/bin/env python3
"""Capturas y comprobaciones reproducibles del cuerpo bovino en el motor real."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=Path('docs/validation/bovine-body-hooves/after'))
    ap.add_argument('--anatomy-only', action='store_true')
    args = ap.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    records = []

    def run(name, command, timeout=600):
        with (out / (name + '.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        records.append({'name': name, 'command': command, 'returncode': result.returncode,
                        'binary_sha256': hashlib.sha256(Path(command[0]).read_bytes()).hexdigest()})
        (out / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(f'{name}: revisar {out / (name + ".log")}')
        print(name + ': PASS', flush=True)

    for stage, name in [(3, 'adult'), (2, 'juvenile'), (1, 'newborn')]:
        folder = out / name
        folder.mkdir(exist_ok=True)
        run('anatomy-' + name, ['./build/capture_bovine_anatomy', 'recipes/brumacerno.creature.json', str(folder), str(stage)])
        for path in folder.glob('*.ppm'):
            Image.open(path).save(path.with_suffix('.png'))
            path.unlink()
    if not args.anatomy_only:
        for stage in range(4):
            run(f'gpu-stage-{stage}', ['./build/validate_recipe_gpu', 'recipes/brumacerno.creature.json', str(stage)])
        gpu = out / 'gpu-mesh-adult.ppm'
        run('gpu-mesh-adult', ['./build/validate_recipe_gpu', 'recipes/brumacerno.creature.json', '3', str(gpu)])
        Image.open(gpu).save(gpu.with_suffix('.png'))
        gpu.unlink()
        cases = [
            ('adult-side', ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=1', '--view=side']),
            ('adult-front', ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=1', '--view=front']),
            ('adult-three-quarter', ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=1']),
            ('juvenile', ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=1', '--initial-age=60']),
            ('newborn', ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=1', '--initial-age=0']),
        ]
        for action in ['walking', 'turning', 'drinking']:
            cases.append((action, ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=3600', '--capture-state=' + action]))
        cases.extend([
            ('feeding', ['--herbivores=1', '--carnivores=0', '--focus=23', '--frames=2400', '--capture-state=feeding']),
            ('fleeing', ['--scenario=E', '--capture-species=1', '--speed=4', '--frames=6000', '--capture-state=fleeing']),
            ('lod-cycle', ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=720', '--lod-cycle']),
            ('cpu-skinning', ['--herbivores=1', '--carnivores=0', '--plants=0', '--focus=3', '--frames=240', '--cpu-skinning']),
            ('velkar', ['--herbivores=0', '--carnivores=1', '--plants=0', '--focus=3', '--frames=240']),
        ])
        for name, options in cases:
            ppm = out / ('ecosystem-' + name + '.ppm')
            run('ecosystem-' + name, ['./demos/demo_ecosystem', '--no-hud', '--inspect=body', '--zoom=3',
                '--validate-mouth=240', '--validate-skinning', *options, '--capture=' + str(ppm),
                '--profile=' + str(ppm.with_suffix('.json'))])
            Image.open(ppm).save(ppm.with_suffix('.png'))
            ppm.unlink()
    records.append({'recipes': {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in sorted(Path('recipes').glob('*.creature.json'))}})
    (out / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
