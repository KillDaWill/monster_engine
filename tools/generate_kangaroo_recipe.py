#!/usr/bin/env python3
"""Genera la receta procedural del Canguro Rojo (Osphranter rufus) v1."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "recipes" / "ornitorrinco.creature.json"
HEAD_REF_PATH = ROOT / "recipes" / "jirafa.creature.json"
OUT_PATH = ROOT / "recipes" / "canguro.creature.json"


def rgba(r, g, b, a=255):
    return {"r": r, "g": g, "b": b, "a": a}


def configure_stage(stage, head_ref, total_scale, dev_app, dev_ceph, dev_pig, dev_int, mat):
    s = copy.deepcopy(stage)

    # Morfología axial semi-erguida en forma de pera (tórax esbelto elevado, pelvis baja profunda)
    ax = s["axial"]
    ax["archetype"] = 1  # AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD
    ax["contourProfile"] = 0
    ax["totalScale"] = total_scale
    ax["trunkLength"] = 0.54
    ax["withersHeight"] = 0.72
    ax["neckLength"] = 0.17 + 0.03 * dev_app
    ax["cervicalCurvature"] = 0.30
    ax["cervicalDorsalMass"] = 0.72
    ax["cervicalMidNarrowing"] = 0.78
    ax["withersElevation"] = 0.03
    ax["withersRoundness"] = 0.55
    ax["dewlapDepth"] = 0.0
    ax["brisketDepth"] = 0.02
    ax["neckWidth"] = 0.16
    ax["shoulderWidth"] = 0.21
    ax["thoraxWidth"] = 0.24
    ax["thoraxHeight"] = 0.28
    ax["abdomenWidth"] = 0.35
    ax["abdomenHeight"] = 0.40
    ax["pelvicWidth"] = 0.39
    ax["pelvicHeight"] = 0.42
    ax["limbAttachmentLateral"] = 0.74
    ax["cervicalSegments"] = 3
    ax["cervicalPitch"] = 0.52
    ax["spineFlexion"] = 0.14

    # Estaciones axiales inclinadas hacia arriba de pelvis a tórax (sin hacking de especie)
    profiles = [
        {"widthScale": 0.94, "heightScale": 0.94, "verticalOffset": 0.24, "longitudinalOffset": 0.02},   # cuello
        {"widthScale": 0.98, "heightScale": 0.96, "verticalOffset": 0.24, "longitudinalOffset": 0.00},   # pectoral
        {"widthScale": 1.04, "heightScale": 1.02, "verticalOffset": 0.14, "longitudinalOffset": 0.02},   # tórax ant.
        {"widthScale": 1.12, "heightScale": 1.08, "verticalOffset": 0.06, "longitudinalOffset": 0.04},   # tórax post.
        {"widthScale": 1.16, "heightScale": 1.16, "verticalOffset": -0.04, "longitudinalOffset": 0.03},  # abdomen
        {"widthScale": 1.12, "heightScale": 1.14, "verticalOffset": -0.12, "longitudinalOffset": 0.00},  # pelvis
    ]
    for i, prof in enumerate(profiles):
        ax["profile"][i] = prof

    # Cabeza herbívora esbelta cerrada con grandes orejas móviles macrópodas (HEAD_ARCHETYPE_EQUINE = 4)
    s["head"] = copy.deepcopy(head_ref["head"])
    hp = s["head"]
    hp["archetype"] = 4  # HEAD_ARCHETYPE_EQUINE (rostro herbívoro fino cerrado, sin rictus)
    hp["skullWidth"] = 0.56
    hp["skullHeight"] = 0.64
    hp["skullLength"] = 0.66
    hp["muzzleLength"] = 0.48
    hp["muzzleWidth"] = 0.46
    hp["muzzleTaper"] = 0.56
    hp["rostrumDepth"] = 0.58
    hp["rostrumDorsalSlope"] = 0.34
    hp["stopProminence"] = 0.30
    hp["snoutBluntness"] = 0.48
    hp["maxillaryWidth"] = 0.40
    hp["cheekMass"] = 0.40
    hp["zygomaticWidth"] = 0.46
    hp["jawDepth"] = 0.50
    hp["jawLength"] = 0.68
    hp["jawStrength"] = 0.52
    hp["mouthWidth"] = 0.40
    hp["eyeSize"] = 0.50 + 0.06 * (1.0 - dev_ceph)
    hp["eyeForwardness"] = 0.12
    hp["eyeDorsality"] = 0.54
    hp["eyeLaterality"] = 0.72
    hp["eyeExposure"] = 0.18
    hp["orbitDepth"] = 0.42
    hp["browProminence"] = 0.36
    hp["eyelidCoverage"] = 0.28
    hp["eyeCompression"] = 0.06
    hp["noseScale"] = 0.54
    hp["nostrilPosition"] = 0.80
    hp["earSize"] = 0.90 + 0.08 * dev_ceph
    hp["earBaseWidth"] = 0.58
    hp["earThickness"] = 0.28
    hp["earPointiness"] = 0.22
    hp["earConcavity"] = 0.90
    hp["earWidthRatio"] = 1.08
    hp["earHeightRatio"] = 1.52
    hp["earLongitudinalCurve"] = 0.56
    hp["earMarginBow"] = 0.78
    hp["earRootRoll"] = 0.50
    hp["earTipRoundness"] = 0.68
    hp["earFold"] = 0.10
    hp["earOutward"] = 0.34
    hp["earForward"] = 0.23
    hp["nasalColor"] = rgba(46, 34, 30)

    s["headEnvelope"]["scale"] = 0.185 + 0.015 * (1.0 - dev_ceph)
    s["headEnvelope"]["widthScale"] = 0.94
    s["headEnvelope"]["heightScale"] = 0.98
    s["headEnvelope"]["lengthScale"] = 0.82
    s.pop("headHybrid", None)

    # Extremidades: anteriores cortas con garras prensiles, posteriores largas macrópodas
    for idx in (0, 1):
        fl = s["limbs"][idx]
        fl["archetype"] = 1  # LIMB_ARCHETYPE_MAMMAL
        fl["anatomyProfile"] = 4  # LIMB_PROFILE_PLANTIGRADE
        fl["role"] = 0
        fl["side"] = idx
        fl["length"] = 0.34
        fl["thickness"] = 0.044
        fl["proximalScale"] = 0.92
        fl["middleScale"] = 1.05
        fl["distalScale"] = 0.95
        fl["proximalSweep"] = -0.14
        fl["distalSweep"] = 0.28
        fl["sprawl"] = 0.12
        fl["attachmentInset"] = 0.18
        fl["rootThicknessScale"] = 1.08
        fl["middleThicknessScale"] = 0.88
        fl["distalThicknessScale"] = 0.72
        fl["proximalThicknessScale"] = 1.00
        fl["footScale"] = 0.92
        fl["footWidthScale"] = 0.86
        fl["footHeightScale"] = 0.72
        fl["footYaw"] = 0.05
        fl["digitCount"] = 5
        fl["digitPhalanges"] = [2, 2, 2, 2, 2, 0, 0, 0]
        fl["digitLengths"] = [0.65, 0.88, 1.02, 0.95, 0.74, 0.0, 0.0, 0.0]
        fl["digitSpread"] = 0.30
        fl["digitAngles"] = [-0.40, -0.18, 0.0, 0.18, 0.38, 0.0, 0.0, 0.0]
        fl["digitThicknessScale"] = 0.76
        fl["clawScale"] = 0.85
        fl["nailCount"] = 0
        fl["webbingCoverage"] = 0.0
        fl["webbingThickness"] = 0.0
        fl["webbingEdgeCurve"] = 0.0
        fl["webbingFold"] = 0.0
        fl["flexionScale"] = 0.95
        fl["development"] = 1.0

    # Patas posteriores con metatarso largo y planta horizontal.
    for idx in (2, 3):
        hl = s["limbs"][idx]
        hl["archetype"] = 1  # LIMB_ARCHETYPE_MAMMAL
        hl["anatomyProfile"] = 5  # LIMB_PROFILE_MACROPOD
        hl["role"] = 1
        hl["side"] = idx - 2
        hl["length"] = 0.455
        hl["thickness"] = 0.088
        hl["proximalScale"] = 1.02
        hl["middleScale"] = 1.24
        hl["distalScale"] = 0.95
        hl["proximalSweep"] = 0.08
        hl["distalSweep"] = -0.04
        hl["sprawl"] = 0.14
        hl["attachmentInset"] = 0.46
        hl["rootThicknessScale"] = 1.27
        hl["middleThicknessScale"] = 0.96
        hl["distalThicknessScale"] = 0.68
        hl["proximalThicknessScale"] = 0.91
        hl["footScale"] = 1.72 + 0.17 * dev_app
        hl["footWidthScale"] = 0.57
        hl["footHeightScale"] = 0.72
        hl["footYaw"] = 0.02
        hl["digitCount"] = 3
        hl["digitPhalanges"] = [2, 3, 2, 0, 0, 0, 0, 0]
        hl["digitLengths"] = [0.46, 1.48, 0.74, 0.0, 0.0, 0.0, 0.0, 0.0]
        hl["digitSpread"] = 0.18
        hl["digitAngles"] = [-0.22, 0.0, 0.20, 0.0, 0.0, 0.0, 0.0, 0.0]
        hl["digitThicknessScale"] = 0.92
        hl["clawScale"] = 1.20
        hl["nailCount"] = 0
        hl["webbingCoverage"] = 0.0
        hl["webbingThickness"] = 0.0
        hl["webbingEdgeCurve"] = 0.0
        hl["webbingFold"] = 0.0
        hl["flexionScale"] = 1.25
        hl["development"] = 1.0

    # Alinear la suela con el suelo trasladando las estaciones axiales. El
    # anclaje cervical hereda el traslado desde el pectoral. La altura de
    # referencia sigue describiendo las proporciones que hereda la cría.
    foot = s["limbs"][2]
    pelvis = ax["profile"][5]
    sole_offset = foot["thickness"] * foot["footHeightScale"] * (0.28 * foot["footScale"] - 0.24)
    floor_offset = (foot["length"] + sole_offset
                    + 0.5 * ax["pelvicHeight"] * pelvis["heightScale"] - pelvis["verticalOffset"]
                    - ax["withersHeight"])
    for station in ax["profile"][1:]:
        station["verticalOffset"] += floor_offset

    # Cola larga musculosa de equilibrio y apoyo macrópodo
    tail = s["tails"][0]
    tail["archetype"] = 0  # TAIL_ARCHETYPE_TAPERED
    tail["length"] = 0.92 + 0.12 * dev_app
    tail["baseWidth"] = 0.125
    tail["baseHeight"] = 0.135
    tail["tipWidth"] = 0.019
    tail["tipHeight"] = 0.021
    tail["taperCurve"] = 0.90
    tail["droop"] = 0.34
    tail["tipLift"] = 0.36
    tail["segmentCount"] = 8
    tail["development"] = 1.0

    # Superficie mamífera: pelaje corto aterciopelado rufo-rojizo con vientre crema
    dorsal = rgba(176, 84, 52) if mat > 0.4 else rgba(154, 118, 98)
    ventral = rgba(228, 212, 184)
    s["dorsalColor"] = dorsal
    s["ventralColor"] = ventral
    s["unpigmentedVentralColor"] = ventral
    s["membraneCount"] = 0
    s["membranes"] = []

    surf = s["surface"]
    surf["integument"]["type"] = 2  # INTEGUMENT_FUR
    surf["integument"]["coverage"] = 1.0
    surf["integument"]["plates"]["armorScale"] = 0.0
    fur = surf["integument"]["fur"]
    fur["density"] = 0.86
    fur["length"] = 0.010
    fur["thickness"] = 0.44
    fur["clumping"] = 0.16
    fur["lay"] = 0.94
    fur["undercoat"] = 0.78
    fur["roughness"] = 0.54
    fur["sheen"] = 0.30
    fur["rootDarkening"] = 0.18
    fur["tipLightening"] = 0.12
    surf["wrinkleStrength"] = 0.10
    if "regionalAlbedo" in surf:
        surf["regionalAlbedo"] = [rgba(0, 0, 0, 0) for _ in surf["regionalAlbedo"]]

    # Pigmentación macrópoda: espalda rufo-rojiza y vientre/pecho crema
    pig = surf["pigment"]
    pig["baseColor"] = dorsal
    pig["secondaryColor"] = rgba(126, 56, 36)
    pig["ventralColor"] = ventral
    pig["patternStrength"] = 0.28 * dev_pig
    pig["ventralLightening"] = 0.85
    pig["dorsalDarkening"] = 0.20 * dev_pig
    pig["layerCount"] = 0
    pig["layers"] = []

    s["development"] = {
        "appendages": dev_app,
        "cephalic": dev_ceph,
        "pigmentation": dev_pig,
        "integument": dev_int,
        "colorMaturity": mat,
    }
    return s


def main():
    doc = json.loads(BASE_PATH.read_text())
    head_ref = json.loads(HEAD_REF_PATH.read_text())["recipe"]["adult"]
    rec = doc["recipe"]
    rec["id"] = 1014
    rec["name"] = "Canguro Rojo"
    rec["gait"] = 6  # CREATURE_GAIT_BIPEDAL_HOP

    # Usar etapas base limpias y ajustar dimensiones de las 4 etapas macrópodas
    rec["seed"] = configure_stage(rec["adult"], head_ref, 0.28, 0.72, 0.74, 0.55, 0.70, 0.25)
    rec["larva"] = configure_stage(rec["adult"], head_ref, 0.52, 0.84, 0.85, 0.75, 0.85, 0.55)
    rec["juvenile"] = configure_stage(rec["adult"], head_ref, 0.82, 0.94, 0.94, 0.90, 0.94, 0.82)
    rec["adult"] = configure_stage(rec["adult"], head_ref, 1.18, 1.00, 1.00, 1.00, 1.00, 1.00)

    OUT_PATH.write_text(json.dumps(doc, indent=2) + "\n")
    print(f"Wrote {OUT_PATH}")


if __name__ == "__main__":
    main()
