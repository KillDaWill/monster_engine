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


def vector(x, y, z):
    return dict(x=x, y=y, z=z)


def tuft(region, center, extent, direction, length, density=1, width=.004, cone=-1):
    return dict(region=region, center=vector(*center), halfExtents=vector(*extent),
        direction=vector(*direction), normal=vector(0, 1, 0), length=length,
        density=density, width=width, minNormalDot=cone, color=color(28, 20, 15))


def pigment_domain(center, radii, tint, coverage, roughness, material=1, seed=71):
    """Máscara artística en semiejes de la estación cefálica estable 2:1."""
    return dict(pattern=0, color=color(*tint), strength=.96, scale=1, sharpness=.5,
        direction=vector(0, 0, 1), seed=seed, regionMask=0,
        domain=dict(anchorNode=131073, center=vector(*center), radii=vector(*radii),
                    softness=.38, irregularity=.30, materialStrength=material,
                    furCoverage=coverage, roughness=roughness))


def equid(base, donkey=False):
    doc = copy.deepcopy(base)
    r = doc['recipe']
    r.update(id=1006 if donkey else 1005, name='Asno' if donkey else 'Caballo', gait=3)
    p = copy.deepcopy(r['adult'])
    p['axial'].update(withersHeight=4.5, trunkLength=3.65, neckLength=1.85,
        neckWidth=1.25, shoulderWidth=1.45, thoraxWidth=2.00, thoraxHeight=2.35,
        abdomenWidth=1.72, abdomenHeight=1.88, pelvicWidth=1.58, pelvicHeight=1.78,
        cervicalDorsalMass=1.65, cervicalCurvature=.90, cervicalMidNarrowing=.72,
        withersElevation=.075, contourProfile=1)
    p['axial']['profile'][0].update(verticalOffset=.28)
    # Dorso suavemente cóncavo, barril torácico y grupa: secciones semánticas,
    # no masas adicionales codificadas en el renderer.
    p['axial']['profile'][2].update(verticalOffset=-.11, heightScale=1.02)
    p['axial']['profile'][3].update(verticalOffset=-.12, heightScale=1.02, widthScale=1.02)
    p['axial']['profile'][4].update(verticalOffset=-.02, heightScale=1.02)
    p['axial']['profile'][5].update(verticalOffset=-.04, heightScale=1.0)
    p['headEnvelope'].update(scale=.56, widthScale=1, heightScale=1.25, lengthScale=.85)
    p['head'].update(archetype=4, skullWidth=.50, skullHeight=.72, skullLength=.62,
        muzzleLength=.82, muzzleWidth=.72, muzzleTaper=.30,
        rostrumDepth=.74, rostrumDorsalSlope=.70, temporalWidth=.62, temporalDepth=.68,
        eyeSize=.36, eyeLaterality=.88, eyeForwardness=.04, eyeDorsality=.58,
        eyelidCoverage=.58, eyeCompression=.48, eyeExposure=.16, browProminence=.42,
        snoutBluntness=.80, jawLength=1.05, jawDepth=.86, jawStrength=.90,
        noseScale=.68, nostrilPosition=.86, orbitDepth=.42,
        earSize=.38, earBaseWidth=.72, earWidthRatio=.85, earHeightRatio=.95,
        earThickness=.22, earConcavity=.70, earOutward=.18, earForward=.16,
        earPointiness=.66, earTipRoundness=.40, cheekMass=.85, masseterMass=.88,
        maxillaryWidth=.42, mouthWidth=.70, cranialForm=0, muzzleForm=2, eyeLayout=0)
    if donkey:
        p['axial'].update(withersHeight=3.8, trunkLength=3.25, neckLength=1.55,
            shoulderWidth=1.30, thoraxWidth=1.65, abdomenWidth=1.6, pelvicWidth=1.45,
            thoraxHeight=2.0, pelvicHeight=1.82, withersElevation=.03)
        p['head'].update(earSize=.90, earHeightRatio=1.30, earWidthRatio=1.25,
            muzzleLength=.80, rostrumDorsalSlope=.76, cheekMass=.58)
    for i, limb in enumerate(p['limbs']):
        hind = i >= 2
        limb.update(archetype=6, anatomyProfile=1, digitCount=1, development=1,
            thickness=.30 if donkey else .36, sprawl=.015, footScale=1.16 if donkey else 1.22,
            footWidthScale=1, footHeightScale=1.28,
            proximalScale=1, middleScale=1, distalScale=1,
            rootThicknessScale=1.25 if hind else 1.35, proximalThicknessScale=1.18 if hind else 1.20,
            middleThicknessScale=1.22, distalThicknessScale=1.10, digitThicknessScale=1,
            attachmentInset=.60, proximalSweep=0, distalSweep=0,
            hoofWallSlope=.42, hoofToeRoundness=.95, hoofHeelHeight=.72, dewclawSize=0)
        limb['digitLengths'] = [1] + [0] * 7
        limb['digitPhalanges'] = [3] + [0] * 7
        limb['digitAngles'] = [0] * 8
    p['tails'][0].update(length=2.15 if donkey else 1.95, baseWidth=.22, baseHeight=.22,
        tipWidth=.075, tipHeight=.075, droop=.98, segmentCount=10)
    pigment = p['surface']['pigment']
    pigment.update(baseColor=color(125, 69, 35), secondaryColor=color(67, 36, 23),
        ventralColor=color(139, 89, 53), patternStrength=0, layerCount=0, layers=[])
    if donkey:
        pigment.update(baseColor=color(117, 111, 100), secondaryColor=color(61, 56, 48),
            ventralColor=color(163, 155, 137))
    p['surface']['integument'].update(coverage=1)
    p['surface']['integument']['fur'].update(length=.035, density=1.2, undercoat=.35)
    p['eyes'].update(size=.36, pupilAspect=2.5, pupilShape=2,
        irisColor=color(65, 44, 27), pupilColor=color(5, 4, 3))
    p.update(dorsalColor=pigment['secondaryColor'], ventralColor=pigment['ventralColor'],
        unpigmentedVentralColor=pigment['baseColor'])
    # Etapas del contrato del motor: precursor, neonatal, juvenil, adulto;
    # no se interpreta «larva» como una larva biológica de mamífero.
    for stage, scale, maturity in (('seed', .40, .40), ('larva', .62, .55),
                                  ('juvenile', .82, .80), ('adult', 1, 1)):
        q = copy.deepcopy(p)
        q['axial']['totalScale'] = scale
        q['axial']['trunkLength'] *= .78+.22*maturity
        q['axial']['neckLength'] *= .78+.22*maturity
        for field in ('thoraxWidth', 'abdomenWidth', 'pelvicWidth'):
            q['axial'][field] *= .65+.35*maturity
        for field in ('thoraxHeight', 'abdomenHeight', 'pelvicHeight'):
            q['axial'][field] *= .65+.35*maturity
        q['headEnvelope']['scale'] *= 1+.20*(1-maturity)
        q['head']['muzzleLength'] *= .72+.28*maturity
        q['head']['cheekMass'] *= .65+.35*maturity
        # Dos dominios superpuestos: manto perinasal y borde labial, sin trufa
        # de cánido ni cambio del material SDF. El asno conserva pelo pálido.
        q['surface']['pigment']['layers'] = [
            pigment_domain((0, -1.55, 1.65), (.94, .92, .75),
                           (188, 179, 158) if donkey else (70, 54, 46),
                           .70 if donkey else .08, .55 if donkey else .48,
                           .30 if donkey else 1),
            pigment_domain((0, -2.10, 1.72), (.90, .35, .58),
                           (65, 60, 53) if donkey else (47, 37, 32), .02, .43, seed=93)
        ]
        q['surface']['pigment']['layerCount'] = 2
        for i, limb in enumerate(q['limbs']):
            limb['thickness'] *= .78+.22*maturity
            limb['rootThicknessScale'] *= .75+.25*maturity
            hind = i >= 2
            station = q['axial']['profile'][5 if hind else 1]
            height = q['axial']['pelvicHeight' if hind else 'thoraxHeight']*.5*station['heightScale']
            sole = limb['thickness']*.55*limb['footScale']*limb['footHeightScale']
            limb['length'] = q['axial']['withersHeight']-height+station['verticalOffset']-sole
        a = q['axial']
        neck_z = a['neckLength']*.85
        tail_y = a['withersHeight']-.5*a['pelvicHeight'] + a['profile'][5]['verticalOffset'] + a['pelvicHeight']*.275
        tail_z = -a['trunkLength']-a['pelvicWidth']*.325
        q['surface']['tufts'] = [
            tuft(2, (0, a['withersHeight']+.40, neck_z*.5), (.25, 1.0, neck_z*.60),
                 (0, 1, -.15) if donkey else (.72, -1, -.10),
                 (.22 if donkey else .95)*maturity, cone=.35),
            tuft(7, (0, tail_y-(1.65 if donkey else .45), tail_z-.30), (.45, .65, .80),
                 (0, -1, -.03), (.65 if donkey else 2.15)*maturity),
            tuft(1, (0, a['withersHeight']+a['neckLength']*.16+.66, neck_z+.60),
                 (.22, .30, .40), (.12, -.70, .50), (.15 if donkey else .55)*maturity, cone=.20)
        ]
        q['surface']['tuftCount'] = len(q['surface']['tufts'])
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
