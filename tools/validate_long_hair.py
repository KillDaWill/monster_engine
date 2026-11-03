#!/usr/bin/env python3
"""Capturas C/OpenGL del hito 3; compara apagado, guías y tres calidades."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
from PIL import Image,ImageDraw,ImageChops

ROOT=Path(__file__).resolve().parents[1]


def report(out):
    cases=[]
    for log in sorted(out.glob('*-stage-*/capture.log')):
        text=log.read_text();row=dict(case=log.parent.name)
        patterns={
            'roots':r'roots (\d+) visible (\d+) root_build_ms ([\d.]+) max_root_error ([\deE.+-]+) runtime_bytes (\d+) gpu_bytes (\d+)',
            'off':r'draw quality 0 frame_ms ([\d.]+) fps ([\d.]+)',
            'on':r'draw quality 3 frame_ms ([\d.]+) fps ([\d.]+) hair_gpu_ms ([\d.]+)',
            'pose':r'guide_pose_ms ([\d.]+) animation_ms ([\d.]+)',
            'render':r'render guides (\d+) strands (\d+) segments (\d+)',
            'parity':r'guide_gpu_parity (\d+) max_error ([\deE.+-]+)',
            'morph':r'morph_cpu_ms ([\d.]+) morph_hair_update_ms ([\d.]+)',
        }
        for key,pattern in patterns.items():
            match=re.search(pattern,text)
            if match:row[key]=[float(v) for v in match.groups()]
        cases.append(row)
    (out/'performance.json').write_text(json.dumps(dict(
        units='ms, bytes, número de elementos; columnas descritas en README.md',cases=cases),indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'docs/validation/long-hair-foundation')
    parser.add_argument('--all-stages',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    subprocess.run(['python3',str(ROOT/'tools/generate_long_hair_examples.py'),'--output',str(out/'recipes')],check=True,cwd=ROOT)
    records=[]
    cases=['caballo-curve','caballo-patch','caballo-tail','asno-curve','asno-tail','perro-patch']
    for name in cases:
        for stage in (range(4) if args.all_stages and name in ('caballo-curve','asno-tail') else (3,)):
            target=out/f'{name}-stage-{stage}';target.mkdir(exist_ok=True)
            command=[str(ROOT/'build/capture_long_hair'),str(out/'recipes'/f'{name}.creature.json'),str(target),str(stage)]
            with (target/'capture.log').open('w') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
            for ppm in target.glob('*.ppm'):
                with Image.open(ppm) as image:image.save(ppm.with_suffix('.png'))
                ppm.unlink()
            animated=(target/'pose-0.png').exists() and (target/'pose-2.png').exists()
            if animated:
                with Image.open(target/'pose-0.png') as first,Image.open(target/'pose-2.png') as last:
                    assert ImageChops.difference(first,last).getbbox(),name+': poses idénticas'
            assert animated or name=='perro-patch',name+': falta demostración animada'
            canvas=Image.new('RGB',(1800,940),'white');draw=ImageDraw.Draw(canvas)
            for i,quality in enumerate((0,3)):
                with Image.open(target/f'side-q{quality}.png') as image:canvas.paste(image,(900*i,40))
                draw.text((900*i+20,12),f'{name} / etapa {stage} / '+('sin pelo largo' if not quality else 'guias + hijos GPU'),fill='black')
            canvas.save(out/f'{name}-stage-{stage}-comparison.png')
            records.append(dict(case=name,stage=stage,command=command,log=str(target/'capture.log'),
                animated=animated,binary_sha256=hashlib.sha256((ROOT/'build/capture_long_hair').read_bytes()).hexdigest()))
    (out/'captures.json').write_text(json.dumps(records,indent=2)+'\n')
    if args.all_stages:
        stable=[]
        for name in ('caballo-curve','asno-tail'):
            snapshots=[json.loads((out/f'{name}-stage-{stage}'/'roots.json').read_text()) for stage in range(4)]
            identities=lambda rows:[(r['id'],r['group'],r['module'],r['from'],r['to'],r['parameter']) for r in rows]
            assert all(identities(rows)==identities(snapshots[0]) for rows in snapshots[1:]),name+': raíces inestables'
            stable.append(dict(case=name,stages=4,roots=len(snapshots[0]),stable=True))
        (out/'root-stability.json').write_text(json.dumps(stable,indent=2)+'\n')
    report(out)


if __name__=='__main__':
    main()
