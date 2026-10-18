#!/usr/bin/env python3
"""Regresión anatómica por regiones y equivalencia de historia del Ager GPU."""
import argparse,json,os,subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from validate_gpu_sdf_surface import pfm,percentiles,components
ROOT=Path(__file__).resolve().parents[1]
EXTRA_GPU_ARGS=[]
CAPTURE_FUR=False

def capture(out,mode,age,view,voxel,debug='material',history=None):
    history_tag=('history-'+('-'.join(f'{x:.4f}'for x in history)))if history else 'static'
    prefix=out/f'{mode}-a{age:.2f}-v{view}-{debug}-{history_tag}'
    cmd=[str(ROOT/'demos/demo_morph_dog_lizard'),f'--surface-mode={mode}',f'--age={age}',f'--view={view}',
         f'--gpu-voxel={voxel}',f'--capture-prefix={prefix}','--no-msaa']
    if not CAPTURE_FUR:cmd.append('--no-fur')
    if debug=='normal':cmd.append('--capture-normals')
    if debug=='flat':cmd.append('--capture-flat')
    if debug=='albedo':cmd.append('--capture-albedo')
    if debug=='semantic':cmd.append('--capture-semantic')
    if mode.startswith('gpu-'):cmd+=EXTRA_GPU_ARGS
    if history:cmd.append('--age-sequence='+','.join(map(str,history)))
    run=subprocess.run(cmd,cwd=ROOT,env=os.environ,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
    Path(str(prefix)+'.log').write_bytes(run.stdout)
    if run.returncode:raise RuntimeError(f'{prefix}: salida {run.returncode}')
    return prefix

def world_points(depth,scene):
    camera=scene['camera'];position=np.array(camera['position'],dtype=float);forward=np.array(camera['target'],dtype=float)-position
    forward/=np.linalg.norm(forward);right=np.cross(forward,camera['up']);right/=np.linalg.norm(right);up=np.cross(right,forward)
    h,w=depth.shape;yy,xx=np.mgrid[:h,:w];tan=np.tan(np.deg2rad(camera['fov'])*.5)
    rays=forward+(2*(xx[...,None]+.5)/w-1)*(w/h)*tan*right+(1-2*(yy[...,None]+.5)/h)*tan*up
    with np.errstate(invalid='ignore'):return position+rays*depth[...,None]

def regions(exact,candidate,scene,voxel,depth_voxels=2.0,max_missing_fraction=0.0):
    p=world_points(exact,scene);both=np.isfinite(exact)&np.isfinite(candidate)
    bad=~np.isfinite(candidate)
    with np.errstate(invalid='ignore'):bad|=np.abs(candidate-exact)>depth_voxels*voxel
    result=[]
    for feature in scene['features']:
        mask=np.isfinite(exact)&np.all(p>=feature['min'],axis=2)&np.all(p<=feature['max'],axis=2)
        count=int(mask.sum());missing=int((mask&bad).sum())
        visible=count>0
        result.append({'feature':feature['name'],'reference_pixels':count,'visible':visible,
            'missing_pixels':missing,'missing_connected_regions':components(mask&bad),
            'depth_world':percentiles(np.abs(exact[mask&both]-candidate[mask&both])),
            'completely_missing':bool(visible and missing>=.95*count),
            'regional_pass':bool(missing<=max_missing_fraction*count) if visible else None,
            'coverage_status':'observed' if visible else 'unobserved'})
    return result

def main():
    global CAPTURE_FUR
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,default=ROOT/'build/ager-feature-validation')
    ap.add_argument('--mode',default='gpu-sdf-dense');ap.add_argument('--ages',default='0,.1,.2,.3,.4,.5,.6,.7,.8,.9,1')
    ap.add_argument('--views',default='3,4,5,7');ap.add_argument('--voxel',type=float,default=.0175)
    ap.add_argument('--fur',action='store_true')
    ap.add_argument('--depth-voxels',type=float,default=2.0)
    ap.add_argument('--max-missing-fraction',type=float,default=0.0)
    ap.add_argument('--normal-p95-degrees',type=float,default=15.0)
    ap.add_argument('--material-mae',type=float,default=12.0)
    ap.add_argument('--history',action='store_true');ap.add_argument('--flat-only',action='store_true');ap.add_argument('--cached-normals',action='store_true');args=ap.parse_args();
    CAPTURE_FUR=args.fur
    if args.depth_voxels<=0 or not 0<=args.max_missing_fraction<1 or args.normal_p95_degrees<=0 or args.material_mae<0:
        ap.error('Tolerancias geométricas o de apariencia inválidas')
    if args.cached_normals:EXTRA_GPU_ARGS.append('--gpu-cached-normals')
    args.out.mkdir(parents=True,exist_ok=True)
    results=[]
    for age in map(float,args.ages.split(',')):
        for view in map(int,args.views.split(',')):
            exact=capture(args.out,'exact-sdf',age,view,args.voxel,'flat');mesh=capture(args.out,args.mode,age,view,args.voxel,'flat')
            scene=json.loads(Path(str(exact)+'.scene.json').read_text());de=pfm(str(exact)+'.depth.pfm');dm=pfm(str(mesh)+'.depth.pfm')
            r={'age':age,'view':view,'regions':regions(de,dm,scene,args.voxel,args.depth_voxels,args.max_missing_fraction),'geometry_gate_pass':True}
            r['geometry_gate_pass']=all(x['regional_pass'] is not False for x in r['regions'])
            if args.history:
                backward=capture(args.out,args.mode,age,view,args.voxel,'flat',[1,min(1,age+.1),age]);db=pfm(str(backward)+'.depth.pfm')
                forward=capture(args.out,args.mode,age,view,args.voxel,'flat',[0,max(0,age-.1),age]);df=pfm(str(forward)+'.depth.pfm')
                finite=np.isfinite(df)&np.isfinite(db)
                r['history']={'silhouette_different_pixels':int(np.sum(np.isfinite(df)^np.isfinite(db))),
                    'depth_world':percentiles(np.abs(df[finite]-db[finite])),'same_age_pass':bool(np.array_equal(df,db))}
                if args.mode=='gpu-sdf-sparse':
                    ff=json.loads(Path(str(forward)+'.field.json').read_text());fb=json.loads(Path(str(backward)+'.field.json').read_text())
                    r['history']['same_published_bricks']=sorted(ff['published_bricks'])==sorted(fb['published_bricks'])
                    r['history']['same_unique_edges']=ff['unique_edges']==fb['unique_edges']
                    r['history']['same_grid_origin']=ff['origin']==fb['origin']
                    r['history']['allocator_valid']=all(f.get('allocator_integrity_errors',0)==0 and f['overflow']==0 for f in (ff,fb))
                    r['history']['same_age_pass']&=r['history']['same_published_bricks'] and r['history']['same_unique_edges'] and r['history']['same_grid_origin'] and r['history']['allocator_valid']
                if not args.flat_only:
                    fn=capture(args.out,args.mode,age,view,args.voxel,'normal',[0,max(0,age-.1),age])
                    bn=capture(args.out,args.mode,age,view,args.voxel,'normal',[1,min(1,age+.1),age])
                    r['history']['same_normals']=bool(np.array_equal(np.array(Image.open(str(fn)+'.ppm')),np.array(Image.open(str(bn)+'.ppm'))))
                    r['history']['same_age_pass']&=r['history']['same_normals']
            if not args.flat_only:
                en=capture(args.out,'exact-sdf',age,view,args.voxel,'normal');mn=capture(args.out,args.mode,age,view,args.voxel,'normal')
                ce=capture(args.out,'exact-sdf',age,view,args.voxel);cm=capture(args.out,args.mode,age,view,args.voxel)
                ne=np.array(Image.open(str(en)+'.ppm'),dtype=float)/127.5-1;nm=np.array(Image.open(str(mn)+'.ppm'),dtype=float)/127.5-1
                denom=np.linalg.norm(ne,axis=2)*np.linalg.norm(nm,axis=2);valid=np.isfinite(de)&np.isfinite(dm)&(denom>.8)
                angle=np.degrees(np.arccos(np.clip(np.sum(ne*nm,axis=2)/np.maximum(denom,1e-9),-1,1)))
                color=np.abs(np.array(Image.open(str(ce)+'.ppm'),dtype=float)-np.array(Image.open(str(cm)+'.ppm'),dtype=float)).mean(axis=2)
                p=world_points(de,scene)
                for f,item in zip(scene['features'],r['regions']):
                    roi=valid&np.all(p>=f['min'],axis=2)&np.all(p<=f['max'],axis=2)
                    item['normal_degrees']=percentiles(angle[roi]);item['material_mae_255']=float(color[roi].mean()) if roi.any() else None
                    item['appearance_pass']=bool(roi.any() and np.percentile(angle[roi],95)<=args.normal_p95_degrees and color[roi].mean()<=args.material_mae) if item['visible'] else None
                r['appearance_gate_pass']=all(x['appearance_pass'] is not False for x in r['regions'])
            if args.mode=='gpu-sdf-sparse':
                r['field']=json.loads(Path(str(mesh)+'.field.json').read_text())
                r['geometry_gate_pass']&=r['field']['overflow']==0 and r['field'].get('allocator_integrity_errors',0)==0
            results.append(r);(args.out/'metrics.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps({'age':age,'view':view,'geometry_gate_pass':r['geometry_gate_pass'],'failing_regions':[{k:x[k]for k in ('feature','reference_pixels','missing_pixels','completely_missing')}for x in r['regions']if x['regional_pass'] is False],'history':r.get('history')}),flush=True)
    coverage=[]
    for age in sorted({r['age'] for r in results}):
        rows=[r for r in results if r['age']==age]
        names={f['feature'] for r in rows for f in r['regions']}
        for name in sorted(names):
            observed=any(f['visible'] for r in rows for f in r['regions'] if f['feature']==name)
            coverage.append({'age':age,'feature':name,'observed':observed})
    (args.out/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
    ok=bool(results) and all(x['observed'] for x in coverage) and all(x['geometry_gate_pass'] and x.get('appearance_gate_pass',True) and x.get('history',{}).get('same_age_pass',True) for x in results)
    print('PASS' if ok else 'FAILED: inspeccionar regiones anatómicas',flush=True);return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
