#!/usr/bin/env python3
"""Verifica la receta bovina externa con capturas y métricas reales de la demo."""
import hashlib
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image

OUTPUT=Path('docs/validation/editor-bull/demo')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only',nargs='*',help='Nombres de casos que se desean repetir.')
    parser.add_argument('--baseline',action='store_true',help='Usar recetas C de respaldo con el binario actual.')
    args=parser.parse_args()
    OUTPUT.mkdir(parents=True,exist_ok=True)
    binary=Path('demos/demo_ecosystem')
    cases=[
        ('adult-side',['--frames=1','--view=side']),
        ('adult-front',['--frames=1','--view=front']),
        ('adult-three-quarter',['--frames=1','--view=three-quarter']),
        ('juvenile',['--frames=1','--view=side','--initial-age=60']),
        ('newborn',['--frames=1','--view=three-quarter','--initial-age=0']),
        ('walking-gpu',['--frames=720','--view=side','--capture-state=walking']),
        ('walking-cpu',['--frames=240','--view=side','--capture-state=walking','--cpu-skinning']),
        ('drinking',['--frames=3600','--view=three-quarter','--capture-state=drinking']),
        ('feeding',['--frames=3600','--view=three-quarter','--plants=20','--focus=23','--capture-state=feeding']),
    ]
    results_path=OUTPUT/('baseline-results.json' if args.baseline else 'results.json')
    report=json.loads(results_path.read_text()) if args.only and results_path.exists() else []
    for name,options in cases:
        if args.only and name not in args.only:continue
        path=OUTPUT/(('baseline-' if args.baseline else '')+name)
        zoom='3.5' if name in ('drinking','feeding') else '2'
        command=[str(binary.resolve()),'--herbivores=1','--carnivores=0','--plants=0','--focus=3',
            '--zoom='+zoom,'--no-hud','--validate-skinning','--validate-mouth=240',*options,
            '--capture='+str(path.with_suffix('.ppm')),'--profile='+str(path.with_suffix('.json'))]
        if args.baseline:
            empty=OUTPUT/'empty-catalog';empty.mkdir(exist_ok=True)
            (empty/'catalog.txt').write_text('# Catálogo vacío para comparar la receta C de respaldo.\n')
            command.append('--recipes-dir='+str(empty))
        with path.with_suffix('.log').open('w') as log:
            result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        text=path.with_suffix('.log').read_text()
        ppm=path.with_suffix('.ppm')
        record={'case':name,'command':command,'exit_code':result.returncode,
            'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'captured':ppm.exists(),'target_reached':not ('--capture-state=' in ' '.join(options)) or 'capture_entity=' in text}
        if ppm.exists():
            with Image.open(ppm) as image:image.save(path.with_suffix('.png'))
            ppm.unlink()
        report=[r for r in report if r['case']!=name]+[record]
        results_path.write_text(json.dumps(report,indent=2)+'\n')
        print(name,record['exit_code'],record['captured'],record['target_reached'],flush=True)
    return int(any(r['exit_code'] or not r['captured'] or not r['target_reached'] for r in report))


if __name__=='__main__':
    raise SystemExit(main())
