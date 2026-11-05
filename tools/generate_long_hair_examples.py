#!/usr/bin/env python3
"""Peinados optativos sobre grupos genéricos; no sustituyen recetas de producción."""
import argparse
import copy
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def group():
    return dict(id=1,seed=17,domain=dict(kind=0,hostModuleId=1,region=2,anchors=[65537,65538],anchorCount=2,
        offset=dict(x=0,y=0,z=0),normal=dict(x=0,y=1,z=0),angle=0,angularSpread=.42,start=.02,end=.98,patchRadius=.15),
        guideCount=96,childrenPerGuide=48,direction=dict(x=1,y=-.25,z=-.12),length=1.35,lengthVariation=.32,width=.0032,
        rootSpread=.10,clumping=.48,stiffness=.18,gravityInfluence=1.25,density=1,
        rootColor=dict(r=29,g=19,b=15,a=255),tipColor=dict(r=66,g=43,b=29,a=255))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'docs/validation/long-hair-volume/after/recipes')
    parser.add_argument('--styled',action='store_true',help='activa controles de forma y raíz frondosa')
    parser.add_argument('--falling',action='store_true',help='crin y cola equinas con caída vertical')
    parser.add_argument('--rooted-mane',action='store_true',help='crin equina con folículos distribuidos, hacia el lado +X')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    for species in ('caballo','asno'):
        source=json.loads((ROOT/f'recipes/{species}.creature.json').read_text())
        for label in ('curve','patch','tail'):
            doc=copy.deepcopy(source);g=group()
            if species=='asno' and label=='curve':
                g.update(direction=dict(x=.08,y=1,z=-.18),length=.32,lengthVariation=.12,
                    rootSpread=.065,clumping=.25,stiffness=.65,gravityInfluence=.3)
            if label=='patch':
                g['domain'].update(kind=1,hostModuleId=2,anchors=[131073],anchorCount=1,
                    normal=dict(x=0,y=-1,z=0),patchRadius=.42,offset=dict(x=0,y=0,z=-.1))
                g.update(direction=dict(x=.1,y=-1,z=0),length=.55,guideCount=64,rootSpread=.065)
            if label=='tail':
                count=source['recipe']['adult']['tails'][0]['segmentCount']
                # La prueba usa el tramo expuesto; el proximal está dentro de la grupa actual.
                anchors=[(20<<16)+i for i in range(max(1,count-3),count+1)]
                g['domain'].update(hostModuleId=20,anchors=anchors,anchorCount=len(anchors),angularSpread=6.28)
                g.update(direction=dict(x=.12,y=-.35,z=-1),length=1.65,guideCount=96,rootSpread=.19,
                    lengthVariation=.4,clumping=.38,gravityInfluence=1.15)
                if species=='asno':
                    g['domain'].update(start=.78,end=1)
                    g.update(length=.65,rootSpread=.13,direction=dict(x=0,y=-.4,z=-1),gravityInfluence=.85)
            if args.styled:
                g.update(rootFullness=.8,bend=.3,wave=.25,transparency=0)
            if args.falling and species=='caballo' and label in ('curve','tail'):
                g.update(rootFullness=.8,bend=0,wave=0,verticalBias=-1,
                         direction=dict(x=-.6 if label=='curve' else .1,y=-1,z=0))
                if label=='curve':
                    g['domain'].update(angle=.85,angularSpread=.55)
            groups=[g]
            if args.falling and species=='caballo' and label=='curve':
                # Tres bandas desde la cresta hasta el mismo lado del cuello.
                g['guideCount']=64
                g.update(direction=dict(x=-.6,y=-1,z=0),verticalBias=-1)
                crest=copy.deepcopy(g);crest.update(id=2,seed=71,guideCount=64)
                crest['domain'].update(angle=.5,angularSpread=.18)
                other=copy.deepcopy(g);other.update(id=3,seed=113,guideCount=64)
                other['domain'].update(angle=1.05,angularSpread=.18)
                groups.extend((crest,other))
            if args.falling and species=='caballo' and label=='tail':
                # Una segunda banda cubre la inserción proximal del pedículo.
                proximal=copy.deepcopy(g);proximal.update(id=2,seed=71,guideCount=64,length=1.45,rootFullness=1)
                anchors=[(20<<16)+i for i in range(max(1,count-4),count)]
                proximal['domain'].update(anchors=anchors,anchorCount=len(anchors),start=.05,end=.95)
                groups.append(proximal)
            if args.rooted_mane and species=='caballo' and label=='curve':
                # Los grupos solapan una banda continua; los hijos interpolan
                # guías vecinas y proyectan su propia inserción sobre la piel.
                groups=[]
                for i in range(3):
                    strand=copy.deepcopy(g)
                    strand.update(id=i+1,seed=17+i*53,guideCount=96,childrenPerGuide=32,
                        rootSpread=.025,rootFullness=1,width=.0025,length=1.35,lengthVariation=.24,
                        clumping=.25,verticalBias=-.9,direction=dict(x=1,y=-.55,z=-.08),bend=0,wave=.12)
                    strand['domain'].update(angle=.3,angularSpread=.45)
                    groups.append(strand)
            for stage in ('seed','larva','juvenile','adult'):
                if stage in doc['recipe']:
                    doc['recipe'][stage]['surface'].update(longHair=copy.deepcopy(groups),longHairCount=len(groups))
            path=args.output/f'{species}-{label}.creature.json'
            path.write_text(json.dumps(doc,indent=2)+'\n')
            print(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path)
    doc=json.loads(subprocess.check_output([str(ROOT/'build/creature_editor_bridge'),'export','2'],cwd=ROOT))
    # El catálogo conserva el perro estático; este ejemplo optativo prueba el rig canino.
    doc['recipe']['gait']=2
    g=group();g['domain'].update(kind=1,hostModuleId=2,anchors=[131073],anchorCount=1,
        normal=dict(x=0,y=-1,z=0),patchRadius=.35,offset=dict(x=0,y=0,z=.3))
    g.update(length=.38,guideCount=64,direction=dict(x=.15,y=-1,z=0),rootSpread=.055)
    if args.styled:
        g.update(rootFullness=.8,bend=.3,wave=.25,transparency=0)
    for stage in ('seed','larva','juvenile','adult'):
        doc['recipe'][stage]['surface'].update(longHair=[copy.deepcopy(g)],longHairCount=1)
    (args.output/'perro-patch.creature.json').write_text(json.dumps(doc,indent=2)+'\n')


if __name__=='__main__':
    main()
