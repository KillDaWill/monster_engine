#!/usr/bin/env python3
"""Genera la receta del pangolín chino (Manis pentadactyla, ID 1013) en cuatro etapas."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "build/creature_editor_bridge"


def color(r, g, b):
    return dict(r=r, g=g, b=b, a=255)


def build_pangolin(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r.update(id=1013, name="PangolinChino", gait=2)  # CREATURE_GAIT_UPRIGHT_QUADRUPED

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
            totalScale=0.72 * scale,
            withersHeight=1.52,
            trunkLength=2.65 + 0.35 * maturity,
            neckLength=0.50,
            neckWidth=1.08,
            shoulderWidth=1.42,
            thoraxWidth=1.62,
            thoraxHeight=1.34,
            abdomenWidth=1.84,
            abdomenHeight=1.50,
            pelvicWidth=1.66,
            pelvicHeight=1.38,
            bodyFlattening=0.92,
            limbAttachmentLateral=0.72,
            cervicalCurvature=0.18,
            cervicalDorsalMass=1.24,
            cervicalMidNarrowing=0.94,
            withersElevation=0.04,
            dewlapDepth=0.0,
            brisketDepth=0.02,
            contourProfile=1,
            cervicalSegments=4,
            cervicalPitch=0.10,
            spineFlexion=0.52,  # Columna muy flexible para enrollamiento defensivo.
        )
        # Dorso ligeramente arqueado y grupa robusta continua hacia la cola gruesa.
        a["profile"] = [
            dict(longitudinalOffset=0.0, verticalOffset=-0.06, widthScale=0.94, heightScale=0.92),
            dict(longitudinalOffset=0.0, verticalOffset=0.02, widthScale=0.98, heightScale=1.00),
            dict(longitudinalOffset=0.0, verticalOffset=0.12, widthScale=1.05, heightScale=1.08),
            dict(longitudinalOffset=0.0, verticalOffset=0.25, widthScale=1.08, heightScale=1.10),
            dict(longitudinalOffset=0.0, verticalOffset=0.05, widthScale=1.02, heightScale=1.02),
            dict(longitudinalOffset=0.0, verticalOffset=-0.08, widthScale=0.96, heightScale=0.98),
        ]

        # Cabeza pequeña cónica alargada (HEAD_ARCHETYPE_PHOLIDOTE = 10), con pabellones pequeños.
        p["headEnvelope"].update(
            scale=0.58 + 0.08 * (1.0 - maturity),
            widthScale=0.90,
            heightScale=0.88,
            lengthScale=1.10,
        )
        p["head"].update(
            archetype=10,  # HEAD_ARCHETYPE_PHOLIDOTE
            skullWidth=0.44,
            skullHeight=0.46,
            skullLength=0.58,
            muzzleLength=0.57 * (0.72 + 0.28 * maturity),
            muzzleWidth=0.46,
            muzzleTaper=0.57,
            rostrumDepth=0.46,
            rostrumDorsalSlope=0.52,
            temporalWidth=0.42,
            temporalDepth=0.42,
            eyeSize=0.22,
            eyeLaterality=0.74,
            eyeForwardness=0.44,
            eyeDorsality=0.52,
            eyelidCoverage=0.35,
            eyeCompression=0.16,
            eyeExposure=0.38,
            browProminence=0.28,
            snoutBluntness=0.34,
            jawLength=0.74 * (0.72 + 0.28 * maturity),
            jawDepth=0.22,
            jawStrength=0.30,
            noseScale=0.36,
            nostrilPosition=0.88,
            orbitDepth=0.32,
            earSize=0.26,  # Pabellón pequeño y redondeado.
            earBaseWidth=0.60,
            earHeightRatio=0.72,
            earWidthRatio=1.16,
            earOutward=0.35,
            earTipRoundness=0.75,
            cheekMass=0.28,
            masseterMass=0.26,
            maxillaryWidth=0.40,
            mouthWidth=0.42,
            cranialForm=0,
            muzzleForm=0,
            eyeLayout=0,
            nasalColor=color(56, 46, 38),
        )
        p["eyes"].update(
            size=0.22,
            irisScale=0.82,
            pupilScale=0.56,
            pupilAspect=1.0,
            pupilShape=0,
            irisColor=color(28, 22, 16),
            pupilColor=color(4, 4, 4),
        )

        # Extremidades robustas plantígradas con garras excavadoras muy largas en manos.
        for i, limb in enumerate(p["limbs"]):
            hind = i >= 2
            st_idx = 5 if hind else 1
            st = a["profile"][st_idx]
            host_h = a["pelvicHeight" if hind else "thoraxHeight"] * 0.5 * st["heightScale"]
            thickness = 0.34 * (0.82 + 0.18 * maturity)
            foot_scale = 1.06 if not hind else 1.02
            foot_h_scale = 0.52
            sole = thickness * 0.54 * foot_scale * foot_h_scale * 0.72
            length = max(0.45, a["withersHeight"] - host_h + st["verticalOffset"] - sole)
            limb.update(
                archetype=1,
                anatomyProfile=4,  # LIMB_PROFILE_PLANTIGRADE
                digitCount=5,
                development=1.0,
                length=length,
                thickness=thickness,
                sprawl=0.09 if not hind else 0.08,
                footScale=foot_scale,
                footWidthScale=1.04,
                footHeightScale=foot_h_scale,
                proximalScale=1.18,
                middleScale=0.88,
                distalScale=0.96,
                rootThicknessScale=1.28,
                proximalThicknessScale=0.96,
                middleThicknessScale=0.88,
                distalThicknessScale=0.74,
                digitThicknessScale=0.56,
                attachmentInset=0.74,
                proximalSweep=-0.10 if not hind else 0.08,
                distalSweep=0.05,
                footYaw=0.05,
                flexionScale=1.04,
                clawScale=(2.45 if not hind else 1.40) * (0.65 + 0.35 * maturity),
                digitLengths=[0.62, 0.96, 1.24, 0.94, 0.60, 0.0, 0.0, 0.0],
                digitPhalanges=[3, 3, 3, 3, 3, 0, 0, 0],
                digitAngles=[-0.52, -0.24, 0.0, 0.24, 0.52, 0.0, 0.0, 0.0],
                digitSpread=0.92,
                webbingCoverage=0.0,
            )

        # Cola pesada, ancha y musculosa acoplada suavemente a la pelvis.
        p["tails"][0].update(
            archetype=3,  # TAIL_ARCHETYPE_PREHENSILE
            length=2.65 * (0.72 + 0.28 * maturity),
            baseWidth=0.45,
            baseHeight=0.40,
            tipWidth=0.085,
            tipHeight=0.062,
            taperCurve=1.15,
            curvature=0.18,
            droop=0.05,
            tipLift=-0.04,
            rootMatchStrength=0.27,
            segmentCount=14,
            development=1.0,
        )

        p["development"].update(
            appendages=1.0, cephalic=1.0, pigmentation=1.0, integument=1.0, colorMaturity=maturity
        )

        skin_brown = color(118, 92, 70)
        soft_belly = color(188, 156, 132)
        keratin_dark = color(92, 68, 46)
        keratin_tip = color(152, 126, 94)
        p.update(dorsalColor=skin_brown, ventralColor=soft_belly, unpigmentedVentralColor=soft_belly)

        surface = p["surface"]
        surface["pigment"].update(
            baseColor=skin_brown,
            secondaryColor=keratin_dark,
            ventralColor=soft_belly,
            dorsalDarkening=0.16,
            ventralLightening=0.45,
            patternStrength=0.18,
            patternScale=3.2,
            bands=0.0,
            seed=1013,
            layers=[],
            layerCount=0,
        )
        surface["integument"].update(type=4, coverage=1.0)  # INTEGUMENT_PLATES = 4
        surface["integument"]["plates"].update(
            size=0.25,
            aspect=1.24,
            thickness=0.058,
            bevel=0.48,
            overlap=0.52,
            irregularity=0.56,
            relief=0.045,
            armorScale=1.0 * (0.65 + 0.35 * maturity),
            scaleCurvature=0.72,
            tipTaper=0.56,
            ventralCoverage=0.06,
            scaleColor=keratin_dark,
            scaleTipColor=keratin_tip,
        )
        surface["tufts"] = []
        surface["tuftCount"] = 0
        surface["longHair"] = []
        surface["longHairCount"] = 0
        r[stage] = p
    return doc


def main():
    base = json.loads((ROOT / "recipes/rata.creature.json").read_text())
    doc = build_pangolin(base)
    with tempfile.NamedTemporaryFile(suffix=".creature.json") as file:
        file.write(json.dumps(doc, allow_nan=False).encode())
        file.flush()
        normalized = json.loads(subprocess.check_output([str(BRIDGE), "validate", file.name], cwd=ROOT))
    out = ROOT / "recipes/pangolin.creature.json"
    out.write_text(json.dumps(normalized, ensure_ascii=False, indent=2) + "\n")
    print(f"Wrote {out}")


if __name__ == "__main__":
    main()
