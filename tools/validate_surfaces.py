#!/usr/bin/env python3
"""Capturas reproducibles de superficies y regresiones sobre malla animada."""
import argparse
from pathlib import Path
import subprocess
import json
import re
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=root / 'build/surface-validation')
parser.add_argument('--quick', action='store_true', help='Sólo adulto y mandíbula')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
cases = [
    ('adult', ['--mode=static', '--zoom=.85']),
    ('jaw', ['--mode=jaw']),
    ('walk', ['--mode=walk']),
    ('appearance_edits', ['--mode=walk', '--edit-sweep']),
    ('juvenile', ['--mode=walk', '--age=0']),
    ('seed1', ['--mode=jaw', '--seed=1']),
    ('seed5', ['--mode=jaw', '--seed=5']),
    ('small_round', ['--mode=jaw', '--size=.07', '--roundness=.95', '--roughness=.8']),
    ('large_keeled', ['--mode=jaw', '--size=.21', '--roundness=.2', '--aspect=1.5', '--keel=.8', '--roughness=.3']),
    ('flat', ['--mode=jaw', '--relief=0']),
    ('smooth', ['--mode=jaw', '--smooth']),
    ('far', ['--mode=static', '--zoom=2.5']),
    ('medium', ['--mode=static', '--zoom=1.3']),
] + [('debug' + str(i), ['--mode=jaw', '--surface-debug=' + str(i)]) for i in range(1, 10)]
if args.quick:
    cases = cases[:2]
results = []
for name, options in cases:
    frames = 120 if name in ('walk', 'appearance_edits') else 30
    command = [str(root / 'demos/demo_lizard_surface'), '--no-grid', f'--frames={frames}',
               '--capture=' + str(args.output / (name + '.ppm'))] + options
    if name == 'walk':
        command += ['--capture-prefix=' + str(args.output / 'walk')]
    result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    (args.output / (name + '.log')).write_text(result.stdout)
    valid = result.returncode == 0 and 'superficie_inmutable=1' in result.stdout and 'rebuilds_animación=0' in result.stdout
    metrics = dict(re.findall(r'(\w+_ms)=([\d.]+)', result.stdout))
    results.append(dict(case=name, passed=valid, command=command, metrics=metrics))
    print(name, 'PASS' if valid else 'FAIL', metrics, flush=True)
    if not valid:
        print(result.stdout)
        break
(args.output / 'results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False))
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
