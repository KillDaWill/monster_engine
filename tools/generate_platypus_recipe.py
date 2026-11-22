#!/usr/bin/env python3
"""Genera la receta del ornitorrinco (Ornithorhynchus anatinus, ID 1012) en cuatro etapas."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "build/creature_editor_bridge"


def color(r, g, b):
    return dict(r=r, g=g, b=b, a=255)


def vector(x, y, z):
    return dict(x=x, y=y, z=z)


def build_platypus(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r.update(id=1012, name="Ornitorrinco", gait=2)  # CREATURE_GAIT_UPRIGHT_QUADRUPED

    base_p = copy.deepcopy(r["adult"])
    for stage, scale, maturity in (
        ("seed", 0.34, 0.25),
        ("larva", 0.54, 0.50),
        ("juvenile", 0.78, 0.78),
        ("adult", 1.00, 1.00),
    ):
        p = copy.deepcopy(base_p)
        a = p["axial"]
        a.update(
            totalScale=0.64 * scale,
            withersHeight=1.32,
            trunkLength=2.38 + 0.28 * maturity,
            neckLength=0.55,
            neckWidth=0.75,
            shoulderWidth=1.42,
            thoraxWidth=1.56,
            thoraxHeight=1.15,
            abdomenWidth=1.72,
            abdomenHeight=1.22,
            pelvicWidth=1.50,
            pelvicHeight=1.04,
            bodyFlattening=0.84,
            limbAttachmentLateral=0.74,
            cervicalCurvature=0.10,
            cervicalDorsalMass=1.42,
            cervicalMidNarrowing=0.98,
            withersElevation=0.0,
            dewlapDepth=0.0,
            brisketDepth=0.02,
            contourProfile=1,
            cervicalSegments=3,
            cervicalPitch=0.28,
            spineFlexion=0.22,
        )
        a["profile"] = [
            dict(longitudinalOffset=0.0, verticalOffset=-0.06, widthScale=0.98, heightScale=0.94),
            dict(longitudinalOffset=0.0, verticalOffset=-0.02, widthScale=1.00, heightScale=1.02),
            dict(longitudinalOffset=0.0, verticalOffset=0.02, widthScale=1.02, heightScale=1.04),
            dict(longitudinalOffset=0.0, verticalOffset=0.03, widthScale=1.04, heightScale=1.05),
            dict(longitudinalOffset=0.0, verticalOffset=0.0, widthScale=0.98, heightScale=0.98),
            dict(longitudinalOffset=0.0, verticalOffset=-0.06, widthScale=0.94, heightScale=0.94),
        ]

        # Cabeza hidrodinámica con pico flexible ancho en espátula y sin pabellón auricular.
        p["headEnvelope"].update(
            scale=0.62 + 0.08 * (1.0 - maturity),
            widthScale=1.02,
            heightScale=1.12,
            lengthScale=1.04,
        )
        p["head"].update(
            archetype=9,  # HEAD_ARCHETYPE_MONOTREME
            skullWidth=0.62,
            skullHeight=0.67,
            skullLength=0.58,
            muzzleLength=0.49 * (0.70 + 0.30 * maturity),
            muzzleWidth=0.90,
            muzzleTaper=0.08,
            rostrumDepth=0.47,
            rostrumDorsalSlope=0.28,
            temporalWidth=0.52,
            temporalDepth=0.46,
            eyeSize=0.24,
            eyeLaterality=0.62,
            eyeForwardness=0.54,
            eyeDorsality=0.52,
            eyelidCoverage=0.28,
            eyeCompression=0.18,
            eyeExposure=0.42,
            browProminence=0.22,
            snoutBluntness=0.92,
            jawLength=0.80 * (0.70 + 0.30 * maturity),
            jawDepth=0.26,
            jawStrength=0.30,
            noseScale=0.42,
            nostrilPosition=0.64,
            orbitDepth=0.34,
            earSize=0.0,  # Sin pabellón auricular externo.
            cheekMass=0.35,
            masseterMass=0.30,
            maxillaryWidth=0.60,
            mouthWidth=0.72,
            cranialForm=0,
            muzzleForm=1,
            eyeLayout=0,
            nasalColor=color(82, 80, 92),
        )
        p["eyes"].update(
            size=0.26,
            irisScale=0.80,
            pupilScale=0.54,
            pupilAspect=1.0,
            pupilShape=0,
            irisColor=color(36, 26, 20),
            pupilColor=color(5, 5, 6),
        )

        # Extremidades cortas plantígradas de 5 dedos con membrana interdigital y garras.
        for i, limb in enumerate(p["limbs"]):
            hind = i >= 2
            st_idx = 5 if hind else 1
            st = a["profile"][st_idx]
            host_h = a["pelvicHeight" if hind else "thoraxHeight"] * 0.5 * st["heightScale"]
            thickness = 0.24 * (0.82 + 0.18 * maturity)
            foot_scale = 1.38 if not hind else 1.12
            foot_h_scale = 0.42
            sole = thickness * 0.54 * foot_scale * foot_h_scale * 0.7
            length = max(0.38, a["withersHeight"] - host_h + st["verticalOffset"] - sole)
            limb.update(
                archetype=1,
                anatomyProfile=4,  # LIMB_PROFILE_PLANTIGRADE
                digitCount=5,
                development=1.0,
                length=length,
                thickness=thickness,
                sprawl=0.36 if not hind else 0.29,
                footScale=foot_scale,
                footWidthScale=1.38 if not hind else 1.06,
                footHeightScale=foot_h_scale,
                proximalScale=1.18,
                middleScale=0.86,
                distalScale=0.95,
                rootThicknessScale=1.26,
                proximalThicknessScale=0.92,
                middleThicknessScale=0.82,
                distalThicknessScale=0.70,
                digitThicknessScale=0.52,
                attachmentInset=0.76,
                proximalSweep=-0.12 if not hind else 0.08,
                distalSweep=0.06,
                footYaw=0.18 if not hind else 0.12,
                flexionScale=1.08,
                clawScale=(1.35 if not hind else 1.05) * (0.7 + 0.3 * maturity),
                digitLengths=[0.74, 0.96, 1.06, 0.94, 0.70, 0.0, 0.0, 0.0],
                digitPhalanges=[3, 3, 3, 3, 3, 0, 0, 0],
                digitAngles=[-0.72, -0.34, 0.0, 0.34, 0.72, 0.0, 0.0, 0.0],
                digitSpread=1.42 if not hind else 1.12,
                webbingCoverage=(0.95 if not hind else 0.65) * (0.65 + 0.35 * maturity),
                webbingThickness=0.22,
                webbingEdgeCurve=0.085 if not hind else 0.24,
                webbingFold=0.04 if not hind else 0.14,
            )

        # Cola ancha en pala aplanada dorsoventralmente (TAIL_ARCHETYPE_PADDLE = 7).
        p["tails"][0].update(
            archetype=7,
            length=1.50 * (0.78 + 0.22 * maturity),
            baseWidth=0.61,
            baseHeight=0.17,
            tipWidth=0.29,
            tipHeight=0.064,
            taperCurve=0.92,
            curvature=0.0,
            droop=0.0,
            tipLift=0.0,
            rootMatchStrength=0.14,
            segmentCount=12,
            development=1.0,
        )

        p["development"].update(
            appendages=1.0, cephalic=1.0, pigmentation=1.0, integument=1.0, colorMaturity=maturity
        )

        chocolate = color(88, 56, 34)
        dark_umber = color(58, 36, 22)
        tawny_belly = color(168, 132, 90)
        bill_slate = color(82, 80, 92)
        p.update(dorsalColor=chocolate, ventralColor=tawny_belly, unpigmentedVentralColor=tawny_belly)

        surface = p["surface"]
        surface["pigment"].update(
            baseColor=chocolate,
            secondaryColor=dark_umber,
            ventralColor=tawny_belly,
            dorsalDarkening=0.18,
            ventralLightening=0.58,
            patternStrength=0.12,
            patternScale=2.4,
            bands=0.0,
            seed=1012,
            layers=[],
            layerCount=0,
        )
        # Piel coriácea de pico/boca (SURFACE_REGION_ORAL=9) y membrana/patas oscuras (DIGIT=8).
        surface.setdefault("regionalAlbedo", [dict(r=0, g=0, b=0, a=0) for _ in range(11)])
        surface["regionalAlbedo"][8] = bill_slate
        surface["regionalAlbedo"][9] = bill_slate
        surface["integument"].update(type=2, coverage=1.0)
        surface["integument"]["fur"].update(
            length=0.020, density=1.80, thickness=0.56, lay=0.94, stiffness=0.55,
            clumping=0.12, undercoat=0.86, guardDensity=0.45, roughness=0.42, sheen=0.40,
            rootDarkening=0.22, tipLightening=0.06,
        )
        for region in surface["furRegions"]:
            for field in region:
                region[field] = 1.0
        surface["furRegions"][1]["lengthMultiplier"] = 0.55  # Cabeza corta aterciopelada
        surface["furRegions"][8]["lengthMultiplier"] = 0.12  # Dedos casi lampiños
        surface["tufts"] = []
        surface["tuftCount"] = 0
        surface["longHair"] = []
        surface["longHairCount"] = 0
        r[stage] = p
    return doc


def main():
    base = json.loads((ROOT / "recipes/rata.creature.json").read_text())
    doc = build_platypus(base)
    with tempfile.NamedTemporaryFile(suffix=".creature.json") as file:
        file.write(json.dumps(doc, allow_nan=False).encode())
        file.flush()
        normalized = json.loads(subprocess.check_output([str(BRIDGE), "validate", file.name], cwd=ROOT))
    out = ROOT / "recipes/ornitorrinco.creature.json"
    out.write_text(json.dumps(normalized, ensure_ascii=False, indent=2) + "\n")
    print(f"Wrote {out}")


if __name__ == "__main__":
    main()
