#!/usr/bin/env python3
"""Compara extracción densa y SDF exacto; las descargas son sólo diagnósticas."""
import argparse, json, os, subprocess, time
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]

def pfm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'Pf'
        w, h = map(int, f.readline().split()); scale = float(f.readline())
        a = np.frombuffer(f.read(), dtype='<f4' if scale < 0 else '>f4').reshape(h, w)
    return np.flipud(a)

def percentiles(a):
    return dict(zip(('p50', 'p95', 'p99', 'max'), map(float, np.percentile(a, [50,95,99,100])))) if a.size else None

def components(mask):
    # Cuenta regiones faltantes de ocho píxeles o más, sin confundir ruido aislado.
    from scipy.ndimage import label
    labels, _ = label(mask)
    counts = np.bincount(labels.ravel())[1:]
    return int(np.sum(counts >= 8))

def capture(out, mode, age, view, voxel, refinement, normal=False, reference_step=0., reference_steps=512, msaa=False):
    stem = out / f'{mode}-a{age:.2f}-v{view}{"-normal" if normal else ""}'
    cmd = [str(ROOT/'demos/demo_morph_dog_lizard'), f'--surface-mode={mode}',
           f'--age={age}', f'--view={view}', '--no-fur', f'--capture-prefix={stem}',
           f'--gpu-voxel={voxel}', f'--gpu-refinement={refinement}']
    if not msaa: cmd.append('--no-msaa')
    if mode == 'exact-sdf':
        cmd += [f'--reference-max-step={reference_step}', f'--reference-steps={reference_steps}']
    if normal: cmd.append('--capture-normals')
    elif mode == 'gpu-sdf-mesh': cmd.append('--validate-compute')
    t = time.monotonic()
    run = subprocess.run(cmd, cwd=ROOT, env={**os.environ, 'DISPLAY': os.getenv('DISPLAY', ':0')},
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    Path(str(stem)+'.log').write_bytes(run.stdout)
    if run.returncode:
        (out/'failure.json').write_text(json.dumps({'mode':mode,'age':age,'view':view,'voxel':voxel,
            'refinement':refinement,'returncode':run.returncode,'log':str(stem)+'.log'},indent=2)+'\n')
        raise RuntimeError(f'{stem}: salida {run.returncode}; consultar log')
    Image.open(str(stem)+'.ppm').save(str(stem)+'.png')
    return stem, time.monotonic()-t

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=ROOT/'build/gpu-sdf-validation')
    ap.add_argument('--ages', default='0,.1,.2,.35,.5,.65,.8,.9,1')
    ap.add_argument('--views', default='1,2,3,4,5,6,7,8,9')
    ap.add_argument('--voxel', type=float, default=.07)
    ap.add_argument('--refinement', type=int, default=2)
    ap.add_argument('--msaa',action='store_true',help='Capturas con 4 muestras; la profundidad resuelta no corresponde al centro del píxel')
    ap.add_argument('--reference-max-step',type=float,default=0.)
    ap.add_argument('--reference-steps',type=int,default=512)
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    results=[]
    for age in map(float,args.ages.split(',')):
        for view in map(int,args.views.split(',')):
            exact,et=capture(args.out,'exact-sdf',age,view,args.voxel,args.refinement,reference_step=args.reference_max_step,reference_steps=args.reference_steps,msaa=args.msaa)
            mesh,mt=capture(args.out,'gpu-sdf-mesh',age,view,args.voxel,args.refinement,reference_step=args.reference_max_step,reference_steps=args.reference_steps,msaa=args.msaa)
            en,_=capture(args.out,'exact-sdf',age,view,args.voxel,args.refinement,True,args.reference_max_step,args.reference_steps,args.msaa)
            mn,_=capture(args.out,'gpu-sdf-mesh',age,view,args.voxel,args.refinement,True,args.reference_max_step,args.reference_steps,args.msaa)
            de,dm=pfm(str(exact)+'.depth.pfm'),pfm(str(mesh)+'.depth.pfm')
            se,sm=np.isfinite(de),np.isfinite(dm);both=se&sm;union=se|sm
            depth=percentiles(np.abs(de[both]-dm[both]))
            ne=np.asarray(Image.open(str(en)+'.ppm'),dtype=float)/127.5-1
            nm=np.asarray(Image.open(str(mn)+'.ppm'),dtype=float)/127.5-1
            le,lm=np.linalg.norm(ne,axis=2),np.linalg.norm(nm,axis=2)
            normalMask=both&(le>.9)&(le<1.1)&(lm>.9)&(lm<1.1)
            cos=np.sum(ne*nm,axis=2)/(np.maximum(le*lm,1e-10))
            angle=percentiles(np.degrees(np.arccos(np.clip(cos[normalMask],-1,1))))
            ce=np.asarray(Image.open(str(exact)+'.ppm'),dtype=float)
            cm=np.asarray(Image.open(str(mesh)+'.ppm'),dtype=float)
            xor=100*float(np.sum(se^sm))/max(int(np.sum(union)),1)
            r={'age':age,'view':view,'voxel':args.voxel,'refinement':args.refinement,'msaa_samples':4 if args.msaa else 0,
               'silhouette_xor_percent':xor,'depth_world':depth,'normal_degrees':angle,
               'missing_regions':components(se&~sm),'extra_regions':components(sm&~se),
               'missing_pixels':int(np.sum(se&~sm)),'extra_pixels':int(np.sum(sm&~se)),
               'color_mae_255':float(np.mean(np.abs(ce[both]-cm[both]))),
               'exact_capture_wall_s':et,'mesh_capture_wall_s':mt,
               'gate_pixel_pass':bool(xor<2 and depth and depth['p95']<=args.voxel)}
            results.append(r)
            (args.out/'metrics.json').write_text(json.dumps(results,indent=2)+'\n')
            print(json.dumps(r),flush=True)
    print('PASS métricas píxel' if all(r['gate_pixel_pass'] for r in results) else 'FAILED gate A: revisar geometría',flush=True)
    return 0 if all(r['gate_pixel_pass'] for r in results) else 1

if __name__=='__main__': raise SystemExit(main())
