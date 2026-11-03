#!/usr/bin/env python3
"""Ejemplos optativos del hito 3; no sustituyen las recetas de producción."""
import argparse
import copy
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def group():
    return dict(id=1,seed=17,domain=dict(kind=0,hostModuleId=1,region=2,anchors=[65537,65538],anchorCount=2,
        offset=dict(x=0,y=0,z=0),normal=dict(x=0,y=1,z=0),angle=0,angularSpread=.08,start=.05,end=.95,patchRadius=.15),
        guideCount=96,childrenPerGuide=48,direction=dict(x=1,y=-.35,z=0),length=.85,lengthVariation=.25,width=.0025,
        rootSpread=.035,clumping=.55,stiffness=.35,gravityInfluence=.65,density=1,
        rootColor=dict(r=29,g=19,b=15,a=255),tipColor=dict(r=66,g=43,b=29,a=255))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'docs/validation/long-hair-foundation/recipes')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    for species in ('caballo','asno'):
        source=json.loads((ROOT/f'recipes/{species}.creature.json').read_text())
        for label in ('curve','patch','tail'):
            doc=copy.deepcopy(source);g=group()
            if label=='patch':
                g['domain'].update(kind=1,hostModuleId=2,anchors=[131073],anchorCount=1,
                    normal=dict(x=0,y=-1,z=0),patchRadius=.42,offset=dict(x=0,y=0,z=-.1))
                g.update(direction=dict(x=.1,y=-1,z=0),length=.55,guideCount=64)
            if label=='tail':
                count=source['recipe']['adult']['tails'][0]['segmentCount']
                # La prueba usa el tramo expuesto; el proximal está dentro de la grupa actual.
                anchors=[(20<<16)+i for i in range(max(1,count-3),count+1)]
                g['domain'].update(hostModuleId=20,anchors=anchors,anchorCount=len(anchors),angularSpread=6.28)
                g.update(direction=dict(x=0,y=-1,z=-.25),length=1.35,guideCount=96,rootSpread=.03)
            for stage in ('seed','larva','juvenile','adult'):
                if stage in doc['recipe']:
                    doc['recipe'][stage]['surface'].update(longHair=[copy.deepcopy(g)],longHairCount=1)
            path=args.output/f'{species}-{label}.creature.json'
            path.write_text(json.dumps(doc,indent=2)+'\n')
            print(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path)
    doc=json.loads(subprocess.check_output([str(ROOT/'build/creature_editor_bridge'),'export','2'],cwd=ROOT))
    g=group();g['domain'].update(kind=1,hostModuleId=2,anchors=[131073],anchorCount=1,
        normal=dict(x=0,y=-1,z=0),patchRadius=.35,offset=dict(x=0,y=0,z=.3))
    g.update(length=.38,guideCount=64,direction=dict(x=.15,y=-1,z=0))
    for stage in ('seed','larva','juvenile','adult'):
        doc['recipe'][stage]['surface'].update(longHair=[copy.deepcopy(g)],longHairCount=1)
    (args.output/'perro-patch.creature.json').write_text(json.dumps(doc,indent=2)+'\n')


if __name__=='__main__':
    main()
