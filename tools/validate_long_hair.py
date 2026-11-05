#!/usr/bin/env python3
"""Capturas OpenGL de volumen, contacto y etapas; conserva cámaras de referencia."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import os
import shutil
from PIL import Image,ImageDraw,ImageChops

ROOT=Path(__file__).resolve().parents[1]


def report(out,baseline=None):
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
            'collision_rest':r'collision_rest samples (\d+) penetrating (\d+) max_penetration ([\deE.+-]+) envelope_error ([\deE.+-]+) max_segment_strain ([\deE.+-]+)',
            'collision_pose':r'collision_pose max_proxy_penetration ([\deE.+-]+)',
            'collision_children':r'collision_children samples (\d+) max_sdf_penetration ([\deE.+-]+)',
            'jaw_roots':r'jaw_attached_roots (\d+)',
            'collision_jaw':r'collision_jaw samples (\d+) max_posed_jaw_penetration ([\deE.+-]+)',
            'length_error':r'curve_length max_relative_error ([\deE.+-]+)',
            'follicles':r'follicles (\d+) max_skin_offset_error ([\deE.+-]+) min_parent_separation ([\deE.+-]+) guides_falling_positive_x (\d+)/(\d+)',
        }
        for key,pattern in patterns.items():
            match=re.search(pattern,text)
            if match:row[key]=[float(v) for v in match.groups()]
        cases.append(row)
    (out/'performance.json').write_text(json.dumps(dict(
        units='ms, bytes, número de elementos; columnas descritas en README.md',cases=cases),indent=2)+'\n')
    if baseline is None:return
    comparisons=[]
    for row in cases:
        old=baseline/row['case']/'capture.log'
        if not old.exists():continue
        text=old.read_text();previous={}
        for key,pattern in patterns.items():
            match=re.search(pattern,text)
            if match:previous[key]=[float(v) for v in match.groups()]
        comparisons.append(dict(case=row['case'],before=previous,after=row,
            animation_comparable=not row['case'].startswith('perro-')))
    (out/'before-after-performance.json').write_text(json.dumps(comparisons,indent=2)+'\n')
    table=['# Comparación del banco aislado','',
        'Radeon Renoir / Mesa 25.2.8 / 900 × 900 / sin MSAA. No extrapolar FPS al ecosistema.','',
        '| Adulto | GPU pelo ms antes → después | CPU pelo ms antes → después | Runtime bytes antes → después | FPS antes → después |',
        '|---|---:|---:|---:|---:|']
    for row in comparisons:
        if not row['case'].endswith('-stage-3'):continue
        a,b=row['before'],row['after']
        cpu=f"{a['pose'][0]:.3f} → {b['pose'][0]:.3f}" if row['animation_comparable'] else f"sin animación → {b['pose'][0]:.3f}"
        table.append(f"| {row['case'][:-8]} | {a['on'][2]:.3f} → {b['on'][2]:.3f} | {cpu} | {int(a['roots'][4])} → {int(b['roots'][4])} | {a['on'][1]:.1f} → {b['on'][1]:.1f} |")
    table+=['','Las filas caninas no comparan el mismo trabajo CPU: antes el paso estaba desactivado.',
        'Los tiempos de cuadro no incluyen el bucle separado de actualización CPU de pelo/animador.',
        'Los buffers GPU mantienen 80 vec4 por guía (16 de curva y 64 de folículos). Construcción, morph, colisiones y cuatro etapas: `before-after-performance.json`.','']
    (out/'benchmarks.md').write_text('\n'.join(table))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'docs/validation/long-hair-volume/after')
    parser.add_argument('--all-stages',action='store_true')
    parser.add_argument('--rooted-mane',action='store_true',help='valida la crin con inserciones independientes')
    parser.add_argument('--baseline',type=Path,default=ROOT/'docs/validation/long-hair-volume/before')
    parser.add_argument('--compare-only',action='store_true',help='recompone imágenes y métricas ya capturadas sin medir otra vez')
    parser.add_argument('--case',action='append',choices=['caballo-curve','caballo-patch','caballo-tail','asno-curve','asno-tail','perro-patch'],help='repite sólo un caso, conservando los otros registros')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if not args.compare_only:
        subprocess.run(['python3',str(ROOT/'tools/generate_long_hair_examples.py'),'--output',str(out/'recipes')]+(['--rooted-mane'] if args.rooted_mane else []),check=True,cwd=ROOT)
    previous={}
    if (args.compare_only or args.case) and (out/'captures.json').exists():
        previous={(row['case'],row['stage']):row for row in json.loads((out/'captures.json').read_text())}
    cases=args.case or ['caballo-curve','caballo-patch','caballo-tail','asno-curve','asno-tail','perro-patch']
    records=[row for row in previous.values() if row['case'] not in cases]
    for name in cases:
        for stage in (range(4) if args.all_stages else (3,)):
            target=out/f'{name}-stage-{stage}';target.mkdir(exist_ok=True)
            command=[str(ROOT/'build/capture_long_hair'),str(out/'recipes'/f'{name}.creature.json'),str(target),str(stage)]
            camera=args.baseline/f'{name}-stage-{stage}'/'camera.txt'
            env=dict(os.environ,LONG_HAIR_CAMERA=str(camera.resolve())) if camera.exists() else dict(os.environ,LONG_HAIR_CAMERA=str(target/'camera.txt'))
            if not args.compare_only:
                if camera.exists():shutil.copyfile(camera,target/'camera.txt')
                with (target/'capture.log').open('w') as log:subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
                for ppm in target.glob('*.ppm'):
                    with Image.open(ppm) as image:image.save(ppm.with_suffix('.png'))
                    ppm.unlink()
            animated=(target/'pose-0.png').exists() and (target/'pose-2.png').exists()
            if animated:
                with Image.open(target/'pose-0.png') as first,Image.open(target/'pose-2.png') as last:
                    assert ImageChops.difference(first,last).getbbox(),name+': poses idénticas'
            assert animated,name+': falta demostración animada'
            canvas=Image.new('RGB',(1800,940),'white');draw=ImageDraw.Draw(canvas)
            for i,quality in enumerate((0,3)):
                with Image.open(target/f'side-q{quality}.png') as image:canvas.paste(image,(900*i,40))
                draw.text((900*i+20,12),f'{name} / etapa {stage} / '+('sin pelo largo' if not quality else 'guias + hijos GPU'),fill='black')
            canvas.save(out/f'{name}-stage-{stage}-comparison.png')
            if camera.exists():
                for view in ('side','front','rear-three-quarter'):
                    before=camera.parent/f'{view}-q3.png'
                    if before.exists():
                        comparison=Image.new('RGB',(1800,940),'white');caption=ImageDraw.Draw(comparison)
                        for i,path in enumerate((before,target/f'{view}-q3.png')):
                            with Image.open(path) as image:comparison.paste(image,(900*i,40))
                            caption.text((900*i+20,12),name+' / '+view+' / '+('antes' if i==0 else 'después'),fill='black')
                        comparison.save(out/f'{name}-stage-{stage}-{view}-before-after.png')
            records.append((previous.get((name,stage)) if args.compare_only else None) or dict(case=name,stage=stage,command=command,log=str(target/'capture.log'),
                animated=animated,binary_sha256=hashlib.sha256((ROOT/'build/capture_long_hair').read_bytes()).hexdigest()))
    (out/'captures.json').write_text(json.dumps(records,indent=2)+'\n')
    if args.all_stages:
        stable=[]
        if args.case and (out/'root-stability.json').exists():stable=[row for row in json.loads((out/'root-stability.json').read_text()) if row['case'] not in cases]
        for name in cases:
            snapshots=[json.loads((out/f'{name}-stage-{stage}'/'roots.json').read_text()) for stage in range(4)]
            identities=lambda rows:[(r['id'],r['group'],r['module'],r['from'],r['to'],r['parameter']) for r in rows]
            assert all(identities(rows)==identities(snapshots[0]) for rows in snapshots[1:]),name+': raíces inestables'
            stable.append(dict(case=name,stages=4,roots=len(snapshots[0]),stable=True))
        (out/'root-stability.json').write_text(json.dumps(stable,indent=2)+'\n')
    report(out,args.baseline)


if __name__=='__main__':
    main()
