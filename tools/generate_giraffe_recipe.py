#!/usr/bin/env python3
"""Genera la receta de la jirafa (Giraffa camelopardalis, ID 1011) en cuatro etapas."""
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


def tuft(region, center, extent, direction, length, density=1.0, width=0.004, cone=-1.0, col=(45, 28, 16)):
    return dict(
        region=region,
        center=vector(*center),
        halfExtents=vector(*extent),
        direction=vector(*direction),
        normal=vector(0, 1, 0),
        length=length,
        density=density,
        width=width,
        minNormalDot=cone,
        color=color(*col),
    )


def build_giraffe(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r.update(id=1011, name="Jirafa", gait=3)
    # Conservar módulo axial (1), cabeza (2), 4 extremidades (3..6), cola (7) y 2 osiconos (8, 9).
    modules = [m for m in r["bodyPlan"]["modules"] if m["kind"] != 4]
    modules.append(dict(
        instanceId=8, kind=4,
        attachment=dict(hostModuleInstanceId=2, slotId=2),
        phenotypeIndex=0, pathAttached=False, pathNodeA=0, pathNodeB=0,
        pathU=0, lateralOffset=0, pathNormal=vector(0, 0, 0), membranePrevious=0,
    ))
    modules.append(dict(
        instanceId=9, kind=4,
        attachment=dict(hostModuleInstanceId=2, slotId=3),
        phenotypeIndex=1, pathAttached=False, pathNodeA=0, pathNodeB=0,
        pathU=0, lateralOffset=0, pathNormal=vector(0, 0, 0), membranePrevious=0,
    ))
    r["bodyPlan"]["modules"] = modules
    r["bodyPlan"]["moduleCount"] = len(modules)

    base_p = copy.deepcopy(r["adult"])
    for index, (stage, scale, maturity) in enumerate((
        ("seed", 0.34, 0.28),
        ("larva", 0.54, 0.52),
        ("juvenile", 0.80, 0.80),
        ("adult", 1.00, 1.00),
    )):
        p = copy.deepcopy(base_p)
        neck_growth = 0.52 + 0.48 * maturity
        a = p["axial"]
        a.update(
            totalScale=scale,
            withersHeight=4.98,
            trunkLength=2.30 + 0.40 * maturity,
            neckLength=5.10 * neck_growth,
            neckWidth=0.93,
            shoulderWidth=1.38,
            thoraxWidth=1.52,
            thoraxHeight=2.00,
            abdomenWidth=1.32,
            abdomenHeight=1.42,
            pelvicWidth=1.24,
            pelvicHeight=1.38,
            limbAttachmentLateral=0.78,
            cervicalCurvature=0.64,
            cervicalDorsalMass=0.97,
            cervicalMidNarrowing=0.81,
            withersElevation=0.05,
            withersRoundness=0.85,
            dewlapDepth=0.0,
            brisketDepth=0.04,
            contourProfile=1,
            cervicalSegments=6,
            cervicalPitch=1.00 + 0.14 * maturity,
            spineFlexion=0.16,
        )
        # Espalda descendente suave desde la cruz alta hacia la grupa baja.
        a["profile"] = [
            dict(longitudinalOffset=0.0, verticalOffset=0.0, widthScale=0.86, heightScale=0.90),
            dict(longitudinalOffset=0.0, verticalOffset=0.02, widthScale=0.96, heightScale=1.04),
            dict(longitudinalOffset=0.0, verticalOffset=-0.08, widthScale=0.98, heightScale=1.02),
            dict(longitudinalOffset=0.0, verticalOffset=-0.24, widthScale=0.96, heightScale=0.96),
            dict(longitudinalOffset=0.0, verticalOffset=-0.40, widthScale=0.94, heightScale=0.92),
            dict(longitudinalOffset=0.0, verticalOffset=-0.52, widthScale=0.92, heightScale=0.92),
        ]

        # Cabeza pequeña y alargada con rostro estrecho y orejas laterales grandes.
        p["headEnvelope"].update(
            scale=0.46 + 0.08 * (1.0 - maturity),
            widthScale=0.92,
            heightScale=1.15,
            lengthScale=0.92,
        )
        p["head"].update(
            archetype=4,
            skullWidth=0.46,
            skullHeight=0.68,
            skullLength=0.64,
            muzzleLength=0.48 * (0.76 + 0.24 * maturity),
            muzzleWidth=0.58,
            muzzleTaper=0.38,
            rostrumDepth=0.64,
            rostrumDorsalSlope=0.27,
            temporalWidth=0.56,
            temporalDepth=0.64,
            eyeSize=0.71,
            eyeLaterality=0.88,
            eyeForwardness=0.08,
            eyeDorsality=0.62,
            eyelidCoverage=0.54,
            eyeCompression=0.38,
            eyeExposure=0.37,
            browProminence=0.46,
            snoutBluntness=0.68,
            jawLength=1.00,
            jawDepth=0.72,
            jawStrength=0.76,
            noseScale=0.60,
            nostrilPosition=0.85,
            orbitDepth=0.44,
            earSize=0.52,
            earBaseWidth=0.70,
            earWidthRatio=1.15,
            earHeightRatio=0.92,
            earThickness=0.22,
            earConcavity=0.68,
            earOutward=0.58,
            earForward=0.14,
            earPointiness=0.58,
            earTipRoundness=0.48,
            cheekMass=0.64,
            masseterMass=0.66,
            maxillaryWidth=0.38,
            mouthWidth=0.56,
            cranialForm=3,
            muzzleForm=2,
            eyeLayout=0,
        )
        p["eyes"].update(
            size=0.58,
            irisScale=0.82,
            pupilScale=0.58,
            pupilAspect=1.0,
            pupilShape=0,
            irisColor=color(52, 33, 20),
            pupilColor=color(5, 4, 3),
        )

        # Extremidades largas unguladas de pezuña hendida (2 dedos).
        for i, limb in enumerate(p["limbs"]):
            hind = i >= 2
            station = a["profile"][5 if hind else 1]
            host_h = a["pelvicHeight" if hind else "thoraxHeight"] * 0.5 * station["heightScale"]
            thickness = 0.30 * (0.80 + 0.20 * maturity)
            foot_scale = 0.96
            foot_h_scale = 1.08
            sole = thickness * 0.55 * foot_scale * foot_h_scale
            length = a["withersHeight"] - host_h + station["verticalOffset"] - sole
            limb.update(
                archetype=6,
                anatomyProfile=1,
                digitCount=2,
                development=1.0,
                length=length,
                thickness=thickness,
                sprawl=0.010,
                footScale=foot_scale,
                footWidthScale=0.80,
                footHeightScale=foot_h_scale,
                proximalScale=1.02 if hind else 0.96,
                middleScale=1.05,
                distalScale=1.06,
                rootThicknessScale=1.20 if hind else 1.26,
                proximalThicknessScale=1.10 if hind else 1.08,
                middleThicknessScale=1.02,
                distalThicknessScale=0.92,
                digitThicknessScale=0.90,
                attachmentInset=0.58,
                proximalSweep=0.0,
                distalSweep=0.0,
                hoofWallSlope=0.48,
                hoofToeRoundness=0.58,
                hoofHeelHeight=0.68,
                dewclawSize=0.0,
                digitLengths=[1.0, 1.0] + [0.0] * 6,
                digitPhalanges=[3, 3] + [0] * 6,
                digitAngles=[0.0] * 8,
            )

        # Cola delgada con caída natural y pincel terminal.
        p["tails"][0].update(
            archetype=0,
            length=1.85,
            baseWidth=0.14,
            baseHeight=0.14,
            tipWidth=0.045,
            tipHeight=0.045,
            taperCurve=1.25,
            curvature=0.0,
            droop=0.96,
            tipLift=0.0,
            rootMatchStrength=0.25,
            segmentCount=10,
            development=1.0,
        )

        # Dos osiconos cubiertos de piel y pelo en la bóveda craneal.
        oss_dev = 0.25 + 0.75 * maturity
        p["ornaments"] = [
            dict(
                archetype=7,
                length=0.24,
                baseRadius=0.076,
                tipRadius=0.061,
                curvature=0.02,
                development=oss_dev,
                growthDirection=vector(0.08, 0.99, -0.04),
                lateralSweep=0.03,
            ),
            dict(
                archetype=7,
                length=0.24,
                baseRadius=0.076,
                tipRadius=0.061,
                curvature=0.02,
                development=oss_dev,
                growthDirection=vector(-0.08, 0.99, -0.04),
                lateralSweep=-0.03,
            ),
        ]
        p["ornamentCount"] = 2

        p["development"].update(
            appendages=1.0, cephalic=1.0, pigmentation=1.0, integument=1.0, colorMaturity=maturity
        )

        # Pelaje reticulado estable en dominio anatómico de reposo.
        cream = color(232, 206, 162)
        chestnut = color(132, 68, 28)
        dark_mane = color(58, 34, 18)
        pale_leg = color(236, 218, 185)
        p.update(dorsalColor=cream, ventralColor=pale_leg, unpigmentedVentralColor=cream)

        surface = p["surface"]
        skin_domain = dict(
            anchorNode=0, center=vector(0, 0, 0), radii=vector(0, 0, 0),
            softness=0.2, irregularity=0.0, materialStrength=0.0, furCoverage=1.0, roughness=0.72,
        )
        surface["pigment"].update(
            baseColor=cream,
            secondaryColor=chestnut,
            ventralColor=pale_leg,
            dorsalDarkening=0.05,
            ventralLightening=0.35,
            patternStrength=0.0,
            patternScale=1.0,
            bands=0.0,
            seed=1011,
            layers=[
                # Parches poligonales reticulados en cuello, tronco, flancos y parte alta de patas.
                dict(
                    pattern=8,
                    color=chestnut,
                    strength=0.94,
                    scale=3.1,
                    sharpness=0.60,
                    direction=vector(0, 1, 0),
                    seed=1011,
                    patternRotation=0.0,
                    regionMask=0b00011111110,  # head..hind + tail(128)
                    domain=skin_domain,
                ),
                # Aclarado en tercio distal de extremidades (calcetines pálidos).
                dict(
                    pattern=7,
                    color=pale_leg,
                    strength=0.82,
                    scale=1.6,
                    sharpness=0.5,
                    direction=vector(0, -1, 0),
                    seed=2011,
                    patternRotation=0.0,
                    regionMask=(1 << 5) | (1 << 6),  # forelimb | hindlimb
                    domain=skin_domain,
                ),
                # Oscurecimiento en puntas de osiconos y cola.
                dict(
                    pattern=0,
                    color=dark_mane,
                    strength=0.75,
                    scale=1.0,
                    sharpness=0.5,
                    direction=vector(0, 1, 0),
                    seed=3011,
                    patternRotation=0.0,
                    regionMask=(1 << 10),  # SURFACE_REGION_ORNAMENT
                    domain=skin_domain,
                ),
            ],
            layerCount=3,
        )
        surface["integument"].update(type=2, coverage=1.0)
        surface["integument"]["fur"].update(
            length=0.016, density=1.35, thickness=0.68, lay=0.80, stiffness=0.78,
            clumping=0.18, undercoat=0.45, guardDensity=0.35, roughness=0.65, sheen=0.22,
            rootDarkening=0.05, tipLightening=0.08,
        )
        for region in surface["furRegions"]:
            for field in region:
                region[field] = 1.0
        # Cabeza y osiconos con pelo muy corto.
        surface["furRegions"][1]["lengthMultiplier"] = 0.45
        surface["furRegions"][10]["lengthMultiplier"] = 0.55

        # Crin cervical corta y pincel caudal en reposo.
        pec_y = a["withersHeight"] - a["thoraxHeight"] * 0.5 * a["profile"][1]["heightScale"] + a["profile"][1]["verticalOffset"]
        neck_tip_y = pec_y + a["neckLength"] * __import__("math").sin(a["cervicalPitch"])
        neck_tip_z = a["neckLength"] * __import__("math").cos(a["cervicalPitch"])
        tail_y = a["withersHeight"] - 0.5 * a["pelvicHeight"] + a["profile"][5]["verticalOffset"] + a["pelvicHeight"] * 0.22
        tail_z = -a["trunkLength"] - a["pelvicWidth"] * 0.30
        surface["tufts"] = [
            tuft(
                2,
                (0.0, (pec_y + neck_tip_y) * 0.5 + 0.25, neck_tip_z * 0.42 - 0.35),
                (0.18, a["neckLength"] * 0.40, a["neckLength"] * 0.34),
                (0.0, 0.35, -0.94),
                0.20 * maturity,
                density=1.1,
                width=0.005,
                cone=0.52,
                col=(54, 30, 16),
            ),
            tuft(
                7,
                (0.0, tail_y - 1.55, tail_z - 0.20),
                (0.35, 0.55, 0.45),
                (0.0, -1.0, -0.05),
                0.75 * maturity,
                density=1.2,
                width=0.004,
                cone=-1.0,
                col=(32, 20, 12),
            ),
        ]
        surface["tuftCount"] = len(surface["tufts"])
        r[stage] = p
    return doc


def main():
    base = json.loads(subprocess.check_output([str(BRIDGE), "export", "2"], cwd=ROOT))
    doc = build_giraffe(base)
    with tempfile.NamedTemporaryFile(suffix=".creature.json") as file:
        file.write(json.dumps(doc, allow_nan=False).encode())
        file.flush()
        normalized = json.loads(subprocess.check_output([str(BRIDGE), "validate", file.name], cwd=ROOT))
    out_path = ROOT / "recipes" / "jirafa.creature.json"
    out_path.write_text(json.dumps(normalized, ensure_ascii=False, indent=2) + "\n")
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
