"""Una comparación acotada A/B en la GPU disponible; ejecutar desde raíz tras build A/B."""
import csv
import hashlib
import json
import math
from pathlib import Path
import shlex
import statistics
import subprocess
from PIL import Image, ImageDraw

OUT = Path('docs/validation/main-gpu')
BUILD = Path('build/ager-index-demo')
COMMON = ['--surface-mode=sdf-raymarch', '--view=3']


def run(variant, label, args):
    command = ['python3', 'tools/run_monster_engine_gpu.py', '--gpu', 'auto', '--',
               str(BUILD / ('demo-' + variant)), *COMMON, *args]
    with (OUT / (label + '.log')).open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)


def main():
    # Comparación de distancias y SDFSample completos, objetos comunes salvo MonsterSDF.o.
    plan = subprocess.check_output(['make', '-n', '-W', 'demos/demo_morph_dog_lizard.c',
                                    'demos/demo_morph_dog_lizard'], text=True)
    link = shlex.split(next(x for x in reversed(plan.splitlines())
                            if ' -o demos/demo_morph_dog_lizard' in x))
    for variant in ('baseline', 'patched'):
        cmd = link.copy()
        cmd[cmd.index('demos/demo_morph_dog_lizard.c')] = str(OUT / 'verify_sdf_output.c')
        if variant == 'baseline':
            cmd[cmd.index('build/MonsterSDF.o')] = str(BUILD / 'MonsterSDF.baseline.o')
        exe = BUILD / ('verify-' + variant)
        cmd[cmd.index('-o') + 1] = str(exe)
        subprocess.run(cmd, check=True)
        with (BUILD / (variant + '.txt')).open('wb') as output:
            subprocess.run([str(exe)], stdout=output, check=True)
    a = (BUILD / 'baseline.txt').read_bytes()
    assert a == (BUILD / 'patched.txt').read_bytes()
    (OUT / 'equivalence.txt').write_text('PASS 17175 finite full samples + distances; byte-identical.\nSHA256 '
                                        + hashlib.sha256(a).hexdigest() + '\n')
    images = []
    for age in ('0', '0.5', '1'):
        pair = []
        for variant in ('baseline', 'patched'):
            prefix = BUILD / (variant + '-age' + age)
            run(variant, variant + '-age' + age,
                ['--age=' + age, '--fixed-timestep=0.016666667', '--frames=3',
                 '--capture-prefix=' + str(prefix)])
            pair.append(Image.open(str(prefix) + '.ppm').convert('RGB'))
        assert pair[0].tobytes() == pair[1].tobytes()
        assert Path(str(BUILD / ('baseline-age' + age)) + '.scene.json').read_bytes() == Path(
            str(BUILD / ('patched-age' + age)) + '.scene.json').read_bytes()
        pair[1].save(OUT / ('age' + age + '.png'))
        images.append(pair[1])
    sheet = Image.new('RGB', (990, 270), '#17202a')
    for index, image in enumerate(images):
        sheet.paste(image.resize((330, 246)), (330 * index, 24))
        ImageDraw.Draw(sheet).text((330 * index + 5, 5), 'age ' + ('0', '0.5', '1')[index], fill='white')
    sheet.save(OUT / 'stages.png')
    (OUT / 'pixels.txt').write_text('PASS ages 0,0.5,1: RGB pixels and scene metadata byte-identical.\n')
    run('patched', 'cycle', ['--fixed-timestep=0.5', '--frames=100',
                             '--validate-cycle=' + str(BUILD / 'cycle')])
    frames = [Image.open(path).convert('RGB').resize((550, 410))
              for path in sorted(BUILD.glob('cycle-*.ppm'))]
    assert len(frames) == 13
    frames[0].save(OUT / 'cycle.gif', save_all=True, append_images=frames[1:], duration=350, loop=0)
    # No trabajo de build/tests/capturas en paralelo con estas dos pasadas.
    for variant in ('baseline', 'patched'):
        run(variant, 'warmup-' + variant, ['--fixed-timestep=0.25', '--frames=12'])
    summary = {}
    ages = []
    for variant in ('baseline', 'patched'):
        trace = OUT / (variant + '.csv')
        run(variant, 'timing-' + variant, ['--fixed-timestep=0.25', '--frames=100',
                                          '--gpu-trace=' + str(trace)])
        rows = list(csv.DictReader(trace.open()))[8:]
        ages.append([row['age'] for row in rows])
        summary[variant] = dict(frames=len(rows))
        for column in ('frame_wall_ms', 'render_gpu_ms', 'cpu_thread_ms', 'sdf_build_cpu_wall_ms',
                       'ager_cpu_wall_ms', 'geometry_ager_cpu_wall_ms'):
            values = sorted(float(row[column]) for row in rows
                            if column != 'render_gpu_ms' or int(row['timing_generation']) > 0)
            assert values and all(math.isfinite(value) and value >= 0 for value in values)
            summary[variant][column] = dict(median=statistics.median(values),
                                            p95=values[math.ceil(.95 * len(values)) - 1])
    assert ages[0] == ages[1]
    (OUT / 'timing-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (OUT / 'build-manifest.json').write_bytes((BUILD / 'build-manifest.json').read_bytes())
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
