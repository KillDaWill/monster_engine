#!/usr/bin/env python3
"""Captura anatomía y acciones reales dentro de la demo ecológica."""
import argparse
import json
import hashlib
from pathlib import Path
import subprocess
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('docs/validation/ecosystem-creatures'))
    parser.add_argument('--actions', action='store_true')
    parser.add_argument('--species', choices=['all', 'brumacerno', 'velkar'], default='all')
    parser.add_argument('--details', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    cases = []
    for name, herb, carn in [('brumacerno', 1, 0), ('velkar', 0, 1)]:
        if args.species not in ('all', name):
            continue
        for view in ['side', 'front', 'three-quarter']:
            cases.append((f'{name}-adult-{view}', [f'--herbivores={herb}', f'--carnivores={carn}', '--plants=0', '--focus=3', '--frames=1', f'--view={view}']))
        for stage, age in [('juvenile', 60), ('newborn', 0)]:
            cases.append((f'{name}-{stage}-side', [f'--herbivores={herb}', f'--carnivores={carn}', '--plants=0', '--focus=3', '--frames=1', '--view=side', f'--initial-age={age}']))
        if args.details:
            for region, zoom in [('head', 1.2), ('feet', 1), ('neck', 1.6)]:
                for view in ['side', 'front', 'three-quarter']:
                    cases.append((f'{name}-{region}-{view}', [f'--herbivores={herb}', f'--carnivores={carn}', '--plants=0', '--focus=3', '--frames=1', f'--view={view}', f'--inspect={region}', f'--zoom={zoom}']))
        if args.actions:
            for action in ['walking', 'drinking']:
                cases.append((f'{name}-{action}', [f'--herbivores={herb}', f'--carnivores={carn}', '--plants=0', '--focus=3', '--frames=3600', '--view=three-quarter', f'--capture-state={action}']))
    if args.actions and args.species == 'all':
        cases.extend([
            ('brumacerno-feeding', ['--herbivores=1', '--carnivores=0', '--focus=23', '--frames=2400', '--capture-state=feeding']),
            ('velkar-hunting', ['--focus=31', '--frames=2400', '--capture-state=hunting']),
            ('velkar-attacking', ['--focus=31', '--frames=6000', '--capture-state=attacking']),
            ('velkar-feeding', ['--scenario=C', '--capture-species=2', '--speed=4', '--frames=12000', '--capture-state=feeding']),
            ('brumacerno-fleeing', ['--scenario=E', '--capture-species=1', '--speed=4', '--frames=6000', '--capture-state=fleeing']),
            ('overview', ['--frames=180', '--zoom=25', '--view=three-quarter']),
        ])
    for name, options in cases:
        binary_hash = hashlib.sha256(Path('demos/demo_ecosystem').read_bytes()).hexdigest()
        recipe_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in [Path('recipes/catalog.txt'), *sorted(Path('recipes').glob('*.creature.json'))]}
        ppm = args.output / f'{name}.ppm'
        command = ['./demos/demo_ecosystem', '--no-hud', '--inspect=body', '--zoom=3', '--validate-mouth=240', '--validate-skinning', *options,
                   f'--capture={ppm}', f'--profile={args.output / (name + ".json")}']
        with (args.output / (name + '.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0 and '--focus=3' in options:
            ready = 'capture_mesh_ready=1 tier=2' in (args.output / (name + '.log')).read_text()
            if not ready:
                raise RuntimeError(f'{name}: falta prueba de malla NEAR residente')
        if hashlib.sha256(Path('demos/demo_ecosystem').read_bytes()).hexdigest() != binary_hash:
            raise RuntimeError('El ejecutable cambió durante una captura; repetir con el build terminado.')
        record = {'recipe_sha256': recipe_hashes, 'name': name, 'command': command, 'exit_code': result.returncode, 'binary_sha256': binary_hash}
        if result.returncode == 0 and ppm.exists():
            with Image.open(ppm) as image:
                image.save(args.output / (name + '.png'))
            ppm.unlink()
        records.append(record)
        (args.output / 'captures.json').write_text(json.dumps(records, indent=2) + '\n')
        print(f'{name}: {"PASS" if result.returncode == 0 else "FAIL"}', flush=True)
    return int(any(record['exit_code'] for record in records))


if __name__ == '__main__':
    raise SystemExit(main())
