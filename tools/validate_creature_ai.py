#!/usr/bin/env python3
"""Valida inteligencia, perfiles, mutación y regresiones de la escena de forma secuencial."""
import argparse
import csv
import json
import pathlib
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('build/creature-ai'))
    parser.add_argument('--frames', type=int, default=3600)
    parser.add_argument('--variants', action='store_true', help='Recorrer las diez variantes completas')
    parser.add_argument('--regressions', action='store_true', help='Añadir navegación y paridad CPU/GPU')
    parser.add_argument('--captures', action='store_true', help='Guardar capturas de acciones y mutación')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = './demos/demo_game_scene'
    common = [executable, '--headless', '--validate-ai', '--seed=42',
              '--fixed-dt=0.016666667', '--benchmark-uncapped', f'--frames={args.frames}']
    cases = [
        ('balanced', ['--cycles=0', '--dog-brain=balanced', '--lizard-brain=balanced']),
        ('waterless-lizard', ['--cycles=0', '--lizard-brain=waterless']),
        ('waterless-dog', ['--cycles=0', '--dog-brain=waterless']),
        ('mutate-approach', ['--ai-mutate=approach', '--target=1']),
        ('mutate-drink', ['--ai-mutate=drink', '--target=7']),
        ('deterministic-repeat', ['--cycles=0', '--dog-brain=balanced', '--lizard-brain=balanced']),
        ('natural-long', ['--cycles=0', '--ai-natural', '--seed=137', '--frames=7200']),
    ]
    if args.variants:
        cases.extend((f'variant-{i}', ['--ai-mutate=approach', f'--target={i}']) for i in range(10))
    results = {}
    for name, options in cases:
        command = common + options + [f'--profile={args.output / (name + ".csv")}']
        if args.captures and name in {'balanced', 'mutate-approach', 'mutate-drink'}:
            command += [f'--capture-prefix={args.output / name}']
        with (args.output / (name + '.log')).open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        text = (args.output / (name + '.log')).read_text()
        success = result.returncode == 0 and '[ERROR]' not in text
        summaries = re.findall(r'^\[AI RESULT\].*$', text, re.M)
        success = success and len(summaries) == 2
        stationary = {}
        if name.startswith('mutate') or name.startswith('variant'):
            success = success and '[AI MUTATION]' in text and 'Metamorfosis completada' in text
        timing = re.findall(r'^\[AI PERFORMANCE\].*$', text, re.M)
        samples = list(csv.DictReader((args.output / (name + '.csv')).open()))
        if name == 'natural-long':
            by_frame = {int(row['frame']): row for row in samples}
            anchors = {}
            for line in text.splitlines():
                match = re.match(r'\[AI\] frame=(\d+) id=(\d+).* action=(\w+)', line)
                if not match:
                    continue
                frame, actor, action = int(match[1]), int(match[2]), match[3]
                prefix = 'dog' if actor == 2 else 'lizard'
                row = by_frame[frame]
                point = (float(row[prefix+'_x']), float(row[prefix+'_z']))
                anchor, since = anchors.get(actor, (point, frame))
                if action in {'drink', 'rest'} or sum((a-b)**2 for a,b in zip(point, anchor)) > .04:
                    anchor, since = point, frame
                stationary[actor] = max(stationary.get(actor, 0), (frame-since)/60)
                anchors[actor] = anchor, since
            success = success and all(seconds < 45 for seconds in stationary.values())
        ai_times = sorted(float(row['ai_ms']) for row in samples[30:])
        results[name] = {'passed': success, 'exit_code': result.returncode, 'command': command,
                         'actors': summaries, 'timing': timing,
                         'ai_p95_ms': ai_times[int(.95 * (len(ai_times)-1))] if ai_times else None}
        if stationary:
            results[name]['stationary_max_seconds'] = stationary
        print(f'{name}: {"PASS" if success else "FAIL"}', flush=True)
        for line in summaries + timing:
            print(line, flush=True)
    trace = lambda name: [line for line in (args.output / (name+'.log')).read_text().splitlines()
                          if line.startswith('[AI] ') or line.startswith('[AI EVENT] ')]
    deterministic = trace('balanced') == trace('deterministic-repeat')
    results['determinism'] = {'passed': deterministic}
    print(f'determinism: {"PASS" if deterministic else "FAIL"}', flush=True)
    if args.regressions:
        regression_cases = [
            ('navigation', ['--validate-navigation', '--navigation-orbit-stress', '--cycles=0',
                            '--frames=1200', '--seed=137', '--fixed-dt=.016666667']),
            ('boundary-gpu', ['--boundary-stress', '--validate-gpu', '--validate-skinning',
                              '--frames=240', '--target=2', '--seed=42']),
            ('boundary-cpu', ['--boundary-stress', '--cpu-skinning', '--validate-gpu',
                              '--frames=240', '--target=2', '--seed=42']),
        ]
        for name, options in regression_cases:
            command = [executable, '--headless', '--benchmark-uncapped'] + options
            with (args.output / (name+'.log')).open('w') as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
            success = result.returncode == 0
            results[name] = {'passed': success, 'exit_code': result.returncode, 'command': command}
            print(f'{name}: {"PASS" if success else "FAIL"}', flush=True)
    (args.output / 'summary.json').write_text(json.dumps(results, indent=2, ensure_ascii=False)+'\n')
    return 0 if all(case['passed'] for case in results.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
