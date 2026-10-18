#!/usr/bin/env python3
"""Resume perfiles morph sin duplicar consultas GPU ni mezclar sus generaciones."""
import argparse
import csv
import json
import math
from pathlib import Path


def stats(values,budget):
    values=sorted(values)
    if not values:return None
    if not all(map(math.isfinite,values)):raise ValueError('Tiempos no finitos')
    def p(q):
        t=(len(values)-1)*q;i=int(t)
        return values[i]+(values[min(i+1,len(values)-1)]-values[i])*(t-i)
    return {'count':len(values),'p50':p(.5),'p95':p(.95),'p99':p(.99),'max':values[-1],
            'over_budget':sum(x>budget for x in values)}


def summarize(path,budget=1000/30,startup=0):
    with path.open() as file:rows=list(csv.DictReader(file))
    if not rows:raise ValueError(f'Perfil vacío: {path}')
    generations={int(r['generation']):r for r in rows if int(r['generation'])>0}
    timings={int(r['timing_generation']):r for r in rows if int(r['timing_generation'])>0}
    samples=[]
    for generation,timing in sorted(timings.items()):
        request=generations.get(generation)
        samples.append({'generation':generation,'age':float(request['age']) if request else None,
            'samples':int(request['samples']) if request else None,
            'dirty_bricks':int(request['dirty_bricks']) if request else None,
            **{column:float(timing[column]) for column in ('sampling_gpu_ms','classification_gpu_ms',
                'emission_gpu_ms','render_gpu_ms','total_gpu_ms')}})
    adult=False;cycles=0
    for row in rows:
        if float(row['age'])>=.99999:adult=True
        elif float(row['age'])<=.00001 and adult:cycles+=1;adult=False
    result={'frames':len(rows),'budget_ms':budget,'excluded_startup_frames':startup,
        'sampled_returns_to_dog':cycles,'wall_all_ms':stats([float(r['frame_wall_ms']) for r in rows],budget),
        'wall_after_startup_ms':stats([float(r['frame_wall_ms']) for r in rows[startup:]],budget),
        'startup_wall_ms':stats([float(r['frame_wall_ms']) for r in rows[:startup]],budget),
        'gpu_unique_ms':stats([r['total_gpu_ms'] for r in samples],budget),
        'missing_gpu_timings':sorted(set(generations)-set(timings)),
        'stable_30fps_wall':all(float(r['frame_wall_ms'])<=budget for r in rows[startup:]) if rows[startup:] else False,
        'gpu_samples_by_generation':samples}
    metadata=Path(str(path)+'.meta.json')
    if metadata.exists():result['metadata']=json.loads(metadata.read_text())
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('csv',type=Path,nargs='+');ap.add_argument('--budget-ms',type=float,default=1000/30)
    ap.add_argument('--startup-frames',type=int,default=0)
    args=ap.parse_args()
    if args.budget_ms<=0 or args.startup_frames<0:ap.error('Presupuesto o arranque inválido')
    print(json.dumps({str(p):summarize(p,args.budget_ms,args.startup_frames) for p in args.csv},indent=2))


if __name__=='__main__':main()
