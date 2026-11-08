#!/usr/bin/env python3
"""Línea base W0 reproducible; ejecutar bajo el lock externo, sin otros builds."""
from __future__ import annotations
import argparse
import csv
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    opts = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = opts.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    manifest = {'root': str(root), 'started': dt.datetime.now(dt.timezone.utc).isoformat(),
                'sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                'platform': platform.platform(), 'cpu_count': os.cpu_count(),
                'compiler': subprocess.check_output(['gcc', '--version'], text=True).splitlines()[0],
                'commands': [], 'source_sha256': {}, 'binary_sha256': {},
                'timing_policy': 'W0 coordinator only, outer run-bench.sh lock; no other program builds/tests/captures',
                'quantiles': 'nearest rank; p95 with small samples is exploratory, not a robust tail estimate'}
    for pattern in ('src/*.[ch]', 'src/*.inc', 'include/*.h', 'benchmarks/*.c', 'tools/*benchmark*.sh', 'tools/benchmark_creatures.py'):
        for p in root.glob(pattern):
            manifest['source_sha256'][str(p.relative_to(root))] = hashlib.sha256(p.read_bytes()).hexdigest()

    def save() -> None:
        (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')

    def run(name: str, args: list[str], timed: bool = False) -> None:
        cmd = ['/usr/bin/time', '-v', '-o', str(out/(name+'.usage.txt')), *args] if timed else args
        entry = {'name': name, 'argv': cmd, 'started': dt.datetime.now(dt.timezone.utc).isoformat()}
        manifest['commands'].append(entry)
        save()
        with (out/(name+'.log')).open('w') as log:
            result = subprocess.run(cmd, cwd=root, stdout=log, stderr=subprocess.STDOUT)
        entry['exit_code'] = result.returncode
        entry['ended'] = dt.datetime.now(dt.timezone.utc).isoformat()
        save()
        if result.returncode:
            raise RuntimeError(f'{name} failed ({result.returncode}); see {out/(name+".log")}')

    run('environment', ['bash', '-c', 'date -Is\nuname -a\nlscpu\nfree -h\nuptime\ncat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor\n'])
    run('scheduler-build', ['gcc', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2',
        '-DLONG_HAIR_SCHEDULER_STANDALONE', '-Iinclude', '-Itests', 'src/LongHairScheduler.c',
        'src/SDFSamplingPool.c', 'tests/test_long_hair_scheduler.c', '-lm', '-pthread', '-o', str(out/'scheduler-test')])
    run('scheduler-test', [str(out/'scheduler-test')])
    run('comparator-test', ['python3', 'tools/compare_long_hair_baseline.py', '--self-test'])
    objects = subprocess.check_output(['make', '--no-print-directory', '--silent',
        '--eval=print-recovery-core:;@echo $(CORE_OBJS)', 'print-recovery-core'], cwd=root, text=True).split()
    run('core-build', ['make', '-j2', *objects])
    flags = ['-Wall', '-Wextra', '-Wpedantic', '-O2', '-Iinclude']
    for name in ('long_hair_build', 'long_hair_growth', 'creature_construction'):
        binary = root/'build'/('benchmark_'+name)
        run(name+'-link', ['gcc', *flags, 'benchmarks/benchmark_'+name+'.c', *objects, '-lm', '-pthread', '-o', str(binary)])
        manifest['binary_sha256'][name] = hashlib.sha256(binary.read_bytes()).hexdigest()
    save()
    recipe = root/'docs/validation/long-hair-styling/caballo-crin-foliculos.creature.json'
    manifest['recipe'] = {'path': str(recipe), 'sha256': hashlib.sha256(recipe.read_bytes()).hexdigest()}
    for stage in range(4):
        run(f'hair-stage-{stage}', [str(root/'build/benchmark_long_hair_build'), '--recipe', str(recipe),
            '--stage', str(stage), '--warmups', '3', '--samples', '5', '--commit', manifest['sha'],
            '--output', str(out/f'hair-stage-{stage}.json')], timed=True)
    run('production-growth', [str(root/'build/benchmark_long_hair_growth'), str(recipe),
        str(out/'production-growth.csv'), '3', '5', '1', '3', 'all'], timed=True)
    for species in ('horse', 'brumacerno', 'asno', 'dog', 'lizard'):
        args = ['--species', species] if species in ('dog', 'lizard') else ['--recipe', str(root/('recipes/'+({'horse':'caballo'}.get(species,species))+'.creature.json'))]
        run('mesher-'+species, [str(root/'build/benchmark_creature_construction'), *args,
            '--runs', '6', '--voxel', '0.18', '--output', str(out/('mesher-'+species+'.csv'))], timed=True)
    def quantiles(values: list[float]) -> dict:
        s = sorted(values)
        return {'n': len(s), 'p50_ms': s[math.ceil(.5*len(s))-1], 'p95_ms': s[math.ceil(.95*len(s))-1], 'max_ms': max(s)}
    summary = {'hair': {}, 'production_stage': {}, 'production_growth': {}, 'mesher_steady': {}}
    for stage in range(4):
        data = json.loads((out/f'hair-stage-{stage}.json').read_text())
        assert data['guide_count'] == data['visible_guides'] == 288
        assert data['valid_follicle_descriptors'] == 9216
        assert len(set(data['digest_runs'])) == 1
        summary['hair'][str(stage)] = {**quantiles(data['phase_ms']['long_hair_build']),
            'calls': data['distance_calls'], 'body': data['body_evaluations'],
            'jaw': data['jaw_snapshot_evaluations'], 'digest': data['digest_runs'][0]}
    rows = list(csv.DictReader((out/'production-growth.csv').open()))
    for stage in range(4):
        subset = [r for r in rows if r['mode']=='stage' and r['stage']==str(stage)]
        assert len(subset)==5 and len({r['packed_fnv64'] for r in subset})==1
        summary['production_stage'][str(stage)] = quantiles([float(r['hair_ms']) for r in subset])
    growth = [r for r in rows if r['mode']=='growth']
    assert len(growth)==63
    totals = [sum(float(r['hair_ms']) for r in growth if r['rep']==str(rep)) for rep in range(3)]
    summary['production_growth'] = {'total_21_updates': quantiles(totals), 'per_update': quantiles([float(r['hair_ms']) for r in growth]),
        'ager_total_ms': [sum(float(r['ager_ms']) for r in growth if r['rep']==str(rep)) for rep in range(3)],
        'includes': 'MonsterVisual_UpdateLongHair: fingerprint, SDF snapshot, hair build, attachment and pose; excludes ager and CSV/hash bookkeeping',
        'process_threads': sorted({int(r['process_threads']) for r in rows}), 'maxrss_kib': max(int(r['maxrss_kib']) for r in rows)}
    for species in ('horse', 'brumacerno', 'asno', 'dog', 'lizard'):
        r = list(csv.DictReader((out/('mesher-'+species+'.csv')).open()))
        assert len(r)==1014 and all(x['status']!='failed' for x in r)
        summary['mesher_steady'][species] = {}
        for stage in range(4):
            values=[float(x['wall_ms']) for x in r if x['phase']=='mesh_generation_steady' and x['stage']==str(stage) and int(x['rep'])>0]
            assert len(values)==5
            summary['mesher_steady'][species][str(stage)] = quantiles(values)
    (out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    manifest['completed'] = dt.datetime.now(dt.timezone.utc).isoformat()
    manifest['artifact_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.name!='manifest.json'}
    save()
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
