#!/usr/bin/env python3
"""Construye dos documentos de ejemplo con los mismos contratos anatómicos C.

No es el generador estocástico del motor: los valores son decisiones artísticas,
no mediciones biológicas. El puente normaliza y valida cada documento v1.
"""
import copy
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / 'build/creature_editor_bridge'


def color(r, g, b):
    return dict(r=r, g=g, b=b, a=255)


def equid(base, donkey=False):
    doc = copy.deepcopy(base)
    r = doc['recipe']
    r.update(id=1006 if donkey else 1005, name='Asno' if donkey else 'Caballo', gait=3)
    p = copy.deepcopy(r['adult'])
    p['axial'].update(withersHeight=4.5, trunkLength=4.6, neckLength=1.8,
        neckWidth=1.12, shoulderWidth=1.65, thoraxWidth=1.9, thoraxHeight=2.3,
        abdomenWidth=1.8, abdomenHeight=2.05, pelvicWidth=1.8, pelvicHeight=2.2,
        cervicalDorsalMass=1.1, cervicalMidNarrowing=.82, withersElevation=.03)
    p['axial']['profile'][0].update(verticalOffset=.60)
    # Dorso suavemente cóncavo, barril torácico y grupa: secciones semánticas,
    # no masas adicionales codificadas en el renderer.
    p['axial']['profile'][2].update(verticalOffset=-.08, heightScale=1.02)
    p['axial']['profile'][3].update(verticalOffset=-.10, heightScale=1.08, widthScale=1.05)
    p['axial']['profile'][4].update(verticalOffset=-.03, heightScale=1.06)
    p['axial']['profile'][5].update(verticalOffset=.03, heightScale=1.04)
    p['headEnvelope'].update(scale=.58, widthScale=.88, heightScale=1.3, lengthScale=.92)
    p['head'].update(archetype=4, skullWidth=.42, skullHeight=.72, skullLength=.70,
        muzzleLength=.86, muzzleWidth=.72, muzzleTaper=.25,
        rostrumDepth=.74, rostrumDorsalSlope=.70, temporalWidth=.62, temporalDepth=.68,
        eyeSize=.36, eyeLaterality=.56, eyeForwardness=.12, eyeDorsality=.50,
        eyelidCoverage=.52, eyeCompression=.48, eyeExposure=.12, browProminence=.42,
        snoutBluntness=.82, jawLength=.88, jawDepth=.68, jawStrength=.72,
        noseScale=.68, nostrilPosition=.86, orbitDepth=.46,
        earSize=.42, earBaseWidth=.72, earWidthRatio=1.25, earHeightRatio=.75,
        earThickness=.22, earConcavity=.70, earOutward=.18, earForward=.16,
        earPointiness=.66, earTipRoundness=.40, cheekMass=.70,
        maxillaryWidth=.42, mouthWidth=.70, cranialForm=3, muzzleForm=2, eyeLayout=0)
    if donkey:
        p['axial'].update(withersHeight=4.1, trunkLength=4.2, neckLength=1.45,
            shoulderWidth=1.5, thoraxWidth=1.7, abdomenWidth=1.65, pelvicWidth=1.65)
        p['head'].update(earSize=.90, earHeightRatio=1.35, earWidthRatio=1.55,
            muzzleLength=.78, rostrumDorsalSlope=.76)
    for i, limb in enumerate(p['limbs']):
        hind = i >= 2
        limb.update(archetype=6, digitCount=1, development=1, thickness=.28,
            sprawl=.015, footScale=1.15, footWidthScale=1, footHeightScale=1,
            proximalScale=1, middleScale=1, distalScale=1.2,
            rootThicknessScale=2.0 if hind else 1.55, proximalThicknessScale=1.25,
            middleThicknessScale=.9, distalThicknessScale=.75, digitThicknessScale=1,
            attachmentInset=.7, proximalSweep=.02, distalSweep=0,
            hoofWallSlope=.30, hoofToeRoundness=.95, hoofHeelHeight=.65, dewclawSize=0)
        station = p['axial']['profile'][5 if hind else 1]
        height = p['axial']['pelvicHeight' if hind else 'thoraxHeight'] * .5 * station['heightScale']
        limb['length'] = p['axial']['withersHeight'] - height + station['verticalOffset'] - .28 * .55 * limb['footScale']
        limb['digitLengths'] = [1] + [0] * 7
        limb['digitPhalanges'] = [3] + [0] * 7
        limb['digitAngles'] = [0] * 8
    p['tails'][0].update(length=2.6, baseWidth=.24, baseHeight=.24,
        tipWidth=.065, tipHeight=.065, droop=.88, segmentCount=10)
    pigment = p['surface']['pigment']
    pigment.update(baseColor=color(125, 69, 35), secondaryColor=color(67, 36, 23),
        ventralColor=color(139, 89, 53), patternStrength=0, layerCount=0, layers=[])
    if donkey:
        pigment.update(baseColor=color(117, 111, 100), secondaryColor=color(61, 56, 48),
            ventralColor=color(163, 155, 137))
    p['surface']['integument'].update(coverage=1)
    p['eyes'].update(size=.36, pupilAspect=2.5, pupilShape=2,
        irisColor=color(65, 44, 27), pupilColor=color(5, 4, 3))
    p.update(dorsalColor=pigment['secondaryColor'], ventralColor=pigment['ventralColor'],
        unpigmentedVentralColor=pigment['baseColor'])
    # Etapas del contrato del motor: precursor, neonatal, juvenil, adulto;
    # no se interpreta «larva» como una larva biológica de mamífero.
    for stage, scale, slender in (('seed', .3, .85), ('larva', .5, .85),
                                 ('juvenile', .75, .93), ('adult', 1, 1)):
        q = copy.deepcopy(p)
        q['axial']['totalScale'] = scale
        q['axial']['thoraxWidth'] *= slender
        q['axial']['abdomenWidth'] *= slender
        q['head']['muzzleLength'] *= slender
        r[stage] = q
    return doc


def main():
    base = json.loads(subprocess.check_output([str(BRIDGE), 'export', '2'], cwd=ROOT))
    for name, donkey in (('caballo', False), ('asno', True)):
        doc = equid(base, donkey)
        with tempfile.NamedTemporaryFile(suffix='.creature.json') as file:
            file.write(json.dumps(doc, allow_nan=False).encode())
            file.flush()
            normalized = json.loads(subprocess.check_output(
                [str(BRIDGE), 'validate', file.name], cwd=ROOT))
        (ROOT / 'recipes' / f'{name}.creature.json').write_text(
            json.dumps(normalized, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
