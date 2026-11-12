import csv
import json
import math
from pathlib import Path
import statistics
import subprocess

p = Path('build/ager-index-demo')
common = ['--surface-mode=sdf-raymarch', '--fixed-timestep=0.25', '--view=3']
for variant in ['baseline', 'patched']:
    with (p / ('warmup-' + variant + '.log')).open('w') as log:
        subprocess.run([str(p / ('demo-' + variant)), *common, '--frames=12'],
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
summary = {}
for label, variant in [('A1', 'baseline'), ('B1', 'patched'),
                       ('B2', 'patched'), ('A2', 'baseline')]:
    with (p / (label + '.log')).open('w') as log:
        subprocess.run([str(p / ('demo-' + variant)), *common, '--frames=100',
                        '--gpu-trace=' + str(p / (label + '.csv'))],
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
    rows = list(csv.DictReader((p / (label + '.csv')).open()))
    # Omitir arranque de contexto/shaders, idéntico prefijo en las cuatro pasadas.
    rows = rows[8:]
    summary[label] = {'variant': variant, 'frames': len(rows)}
    for col in ['frame_wall_ms', 'sdf_build_cpu_wall_ms', 'render_gpu_ms',
                'cpu_thread_ms', 'ager_cpu_wall_ms', 'geometry_ager_cpu_wall_ms']:
        values = sorted(float(row[col]) for row in rows
                        if col != 'render_gpu_ms' or int(row['timing_generation']) > 0)
        assert all(math.isfinite(v) and v >= 0 for v in values)
        summary[label][col] = {'median': statistics.median(values),
                              'p95': values[math.ceil(.95 * len(values)) - 1],
                              'max': max(values)}
    print(label, summary[label], flush=True)
(p / 'abba-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
