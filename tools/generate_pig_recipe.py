#!/usr/bin/env python3
"""Genera la receta suina completa a partir del esquema cuadrúpedo existente."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def color(r, g, b):
    return dict(r=r, g=g, b=b, a=255)


def recipe():
    source = json.loads((ROOT / "recipes/brumacerno.creature.json").read_text())
    r = source["recipe"]
    r.update(id=1007, name="Cerdo")
    r["bodyPlan"]["modules"] = [m for m in r["bodyPlan"]["modules"] if m["kind"] != 4]
    r["bodyPlan"]["moduleCount"] = 7
    base = copy.deepcopy(r["adult"])
    for index, stage in enumerate(("seed", "larva", "juvenile", "adult")):
        p = copy.deepcopy(base)
        u = index / 3
        a = p["axial"]
        a.update(totalScale=(.23, .39, .67, 1)[index], neckLength=.58,
                 neckWidth=1.86, shoulderWidth=2.12, thoraxWidth=2.24,
                 thoraxHeight=2.06, abdomenWidth=2.32, abdomenHeight=2.22,
                 pelvicWidth=2.08, pelvicHeight=1.94, trunkLength=2.54+.98*u,
                 withersHeight=3.05, limbAttachmentLateral=.73,
                 cervicalCurvature=.40, cervicalDorsalMass=1.12,
                 cervicalMidNarrowing=.96, withersElevation=.01,
                 dewlapDepth=0, brisketDepth=.025, contourProfile=0)
        a["profile"] = [
            dict(longitudinalOffset=z, verticalOffset=y, widthScale=w, heightScale=h)
            for z, y, w, h in ((.015, -.86, 1, .99), (-.10, .02, .92, 1.06),
                               (0, .070, .98, 1.03), (-.06, .085, 1.07, 1.20),
                               (.03, .005, 1.01, 1.01), (.12, -.17, .94, .95))
        ]
        p["head"].update(
            archetype=5, skullWidth=.72, skullHeight=.60, skullLength=.53,
            muzzleLength=.32+.12*u, muzzleWidth=.65, muzzleTaper=.32,
            rostrumDepth=.62, rostrumDorsalSlope=.42, temporalWidth=.64,
            temporalDepth=.64, eyeSize=.19, eyeLaterality=.78, eyeForwardness=.25,
            eyeDorsality=.38, eyelidCoverage=.32, eyeCompression=.18,
            eyeExposure=.20, orbitDepth=.65, browProminence=.12, snoutBluntness=.90,
            jawLength=.55, jawDepth=.40, jawStrength=.50, noseScale=.70,
            earSize=.59, earBaseWidth=.79, earWidthRatio=1.23,
            earHeightRatio=1.04, earThickness=.49, earConcavity=.46,
            earOutward=.34, earForward=.64, earLongitudinalCurve=.55,
            earRootRoll=.46, earTipRoundness=.43, earFold=.19, earPointiness=.82,
            cheekMass=.68, maxillaryWidth=.68, mouthWidth=.45, cephalicIndex=.50,
            stopProminence=.40, zygomaticWidth=.65, masseterMass=.66,
            earRootFlare=.62, earMarginBow=.50, earMarginAsymmetry=.60,
            nostrilPosition=.88, cranialForm=4, muzzleForm=1, eyeLayout=0,
            nasalColor=color(204, 134, 131))
        p["headEnvelope"].update(scale=.87-.10*u, widthScale=1.02,
                                 heightScale=1.80, lengthScale=.94)
        p["eyes"].update(size=.19, irisScale=.86, pupilScale=.68, pupilAspect=1,
                         pupilShape=0, scleraColor=color(42, 30, 27),
                         irisColor=color(58, 36, 24), pupilColor=color(8, 6, 5))
        for limb in p["limbs"]:
            fore = limb["role"] == 0
            attachment = a["profile"][1 if fore else 5]
            host_height = a["thoraxHeight" if fore else "pelvicHeight"]
            # Conserva el plano de suela al desplazar las inserciones del tronco.
            center_y = (a["withersHeight"] - host_height * attachment["heightScale"] / 2
                        + attachment["verticalOffset"])
            thickness = .38 if fore else .45
            sole = thickness * .55 * .70 * .80
            limb.update(length=center_y - sole, thickness=thickness, anatomyProfile=2,
                        footScale=.70, footWidthScale=.70, footHeightScale=.80,
                        rootThicknessScale=1.35, proximalThicknessScale=1.28 if fore else 1.60,
                        middleThicknessScale=1.05, distalThicknessScale=1.05,
                        proximalScale=1.05, middleScale=.85, distalScale=.85,
                        attachmentInset=.84 if fore else .72, digitSpread=.16, dewclawSize=.22)
        p["tails"][0].update(archetype=6, length=1.35+.25*u, baseWidth=.065,
                             baseHeight=.065, tipWidth=.018, tipHeight=.018,
                             taperCurve=.85, curvature=.80, rootMatchStrength=0,
                             segmentCount=16, droop=0)
        p["ornaments"] = []
        p["ornamentCount"] = 0
        p["development"].update(appendages=1, cephalic=1, pigmentation=1,
                                 integument=1, colorMaturity=.7+.3*u)
        p.update(dorsalColor=color(232, 186, 175), ventralColor=color(241, 198, 190),
                 unpigmentedVentralColor=color(241, 198, 190))
        surface = p["surface"]
        surface["pigment"].update(baseColor=color(232, 186, 175),
                                  secondaryColor=color(218, 158, 151),
                                  ventralColor=color(241, 198, 190),
                                  dorsalDarkening=.025, ventralLightening=.035,
                                  patternStrength=.025, layers=[], layerCount=0)
        # Los dominios sólidos seleccionan la respuesta mate. Las dos capas
        # de ruido posteriores sólo modulan el color, también en el disco.
        skin_domain = dict(anchorNode=0, center=dict(x=0, y=0, z=0),
                           radii=dict(x=100, y=100, z=100), softness=.2,
                           irregularity=0, materialStrength=1,
                           furCoverage=1, roughness=.82)
        surface["pigment"]["layers"] = [
            # Todos los territorios salvo cabeza y tejidos orales.
            dict(pattern=0, color=color(241, 206, 194), strength=1,
                 scale=1, sharpness=.5, direction=dict(x=0, y=0, z=0),
                 seed=1007, regionMask=1533, domain=skin_domain),
            dict(pattern=0, color=color(237, 193, 185), strength=.98,
                 scale=1, sharpness=.5, direction=dict(x=0, y=0, z=0),
                 seed=1009, regionMask=2,
                 domain=dict(skin_domain, anchorNode=131073,
                             center=dict(x=0, y=0, z=-.25),
                             radii=dict(x=2, y=3, z=1.25), softness=.12)),
            dict(pattern=1, color=color(209, 153, 145), strength=.30,
                 scale=2.6, sharpness=.5, direction=dict(x=0, y=0, z=0),
                 seed=1008, regionMask=0, domain=dict(skin_domain, materialStrength=0)),
            dict(pattern=1, color=color(166, 112, 107), strength=.16,
                 scale=30, sharpness=.5, direction=dict(x=0, y=0, z=0),
                 seed=1010, regionMask=0, domain=dict(skin_domain, materialStrength=0)),
        ]
        surface["pigment"]["layerCount"] = len(surface["pigment"]["layers"])
        surface["integument"].update(type=2, coverage=.06)
        surface["integument"]["fur"].update(
            length=.012, density=.09, thickness=.14, lay=.45, stiffness=.90,
            clumping=0, undercoat=0, guardDensity=.12, rootDarkening=0,
            tipLightening=.12, roughness=.80, irregularity=.15)
        surface["integument"]["scales"].update(relief=0, microColorVariation=0)
        for region in surface["furRegions"]:
            for field in region:
                region[field] = 1
        surface.update(tufts=[], tuftCount=0, longHair=[], longHairCount=0)
        r[stage] = p
    return source


if __name__ == "__main__":
    (ROOT / "recipes/cerdo.creature.json").write_text(
        json.dumps(recipe(), indent=2) + "\n")
