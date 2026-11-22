#!/usr/bin/env python3
"""Genera la receta procedural del Murciélago Orejudo Pardo (Plecotus auritus) v1."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "recipes" / "ornitorrinco.creature.json"
HEAD_REF_PATH = ROOT / "recipes" / "rata.creature.json"
OUT_PATH = ROOT / "recipes" / "murcielago.creature.json"


def rgba(r, g, b, a=255):
    return {"r": r, "g": g, "b": b, "a": a}


def make_panel(mod_a, start_a, mod_a_end, end_a, mod_b, start_b, mod_b_end, end_b,
               cov, thick, concav, camber, tension, dev, col):
    return {
        "moduleInstanceId": 0,
        "moduleAInstanceId": mod_a,
        "chainAStartNodeId": start_a,
        "moduleAEndInstanceId": mod_a_end,
        "chainAEndNodeId": end_a,
        "moduleBInstanceId": mod_b,
        "chainBStartNodeId": start_b,
        "moduleBEndInstanceId": mod_b_end,
        "chainBEndNodeId": end_b,
        "coverage": cov,
        "thickness": thick,
        "edgeConcavity": concav,
        "camber": camber,
        "tension": tension,
        "development": dev,
        "color": col,
    }


def configure_stage(stage, head_ref, total_scale, dev_app, dev_ceph, dev_pig, dev_int, mat):
    s = copy.deepcopy(stage)

    # Cuerpo compacto quiroptero: tórax profundo muscular, cintura estrecha, pelvis fina
    ax = s["axial"]
    ax["archetype"] = 1  # AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD
    ax["contourProfile"] = 0
    ax["totalScale"] = total_scale
    ax["trunkLength"] = 0.44
    ax["withersHeight"] = 0.42
    ax["neckLength"] = 0.075
    ax["cervicalCurvature"] = 0.36
    ax["cervicalDorsalMass"] = 0.85
    ax["cervicalMidNarrowing"] = 0.84
    ax["withersElevation"] = 0.03
    ax["withersRoundness"] = 0.60
    ax["dewlapDepth"] = 0.0
    ax["brisketDepth"] = 0.04
    ax["neckWidth"] = 0.17
    ax["shoulderWidth"] = 0.25
    ax["thoraxWidth"] = 0.30
    ax["thoraxHeight"] = 0.28
    ax["abdomenWidth"] = 0.20
    ax["abdomenHeight"] = 0.21
    ax["pelvicWidth"] = 0.18
    ax["pelvicHeight"] = 0.19
    ax["limbAttachmentLateral"] = 0.82
    ax["cervicalSegments"] = 0
    ax["cervicalPitch"] = 0.25
    ax["spineFlexion"] = 0.12
    for prof in ax["profile"]:
        prof["widthScale"] = 1.0
        prof["heightScale"] = 1.0
        prof["verticalOffset"] = 0.0
        prof["longitudinalOffset"] = 0.0
    # Ligerísima inclinación dorsal anterior típica del vuelo/descanso
    ax["profile"][1]["verticalOffset"] = 0.03
    ax["profile"][5]["verticalOffset"] = -0.02

    # Cabeza pequeña con hocico corto y enormes orejas curvadas de Plecotus auritus
    s["head"] = copy.deepcopy(head_ref["head"])
    hp = s["head"]
    hp["archetype"] = 8  # HEAD_ARCHETYPE_RODENT (hocico corto redondeado microquiroptero)
    hp["skullWidth"] = 0.52
    hp["temporalWidth"] = 0.39
    hp["zygomaticWidth"] = 0.42
    hp["maxillaryWidth"] = 0.44
    hp["skullHeight"] = 0.68
    hp["skullLength"] = 0.66
    hp["muzzleLength"] = 0.095
    hp["muzzleWidth"] = 0.46
    hp["muzzleTaper"] = 0.42
    hp["rostrumDepth"] = 0.56
    hp["snoutBluntness"] = 0.68
    hp["eyeSize"] = 0.43
    hp["eyeExposure"] = 0.54
    hp["orbitDepth"] = 0.06
    hp["browProminence"] = 0.04
    hp["eyelidCoverage"] = 0.0
    hp["eyeCompression"] = 0.0
    hp["noseScale"] = 0.52
    hp["earSize"] = 1.62 + 0.18 * dev_ceph
    hp["earBaseWidth"] = 0.60
    hp["earThickness"] = 0.20
    hp["earPointiness"] = 0.20
    hp["earConcavity"] = 0.94
    hp["earWidthRatio"] = 1.57
    hp["earHeightRatio"] = 1.76
    hp["earLongitudinalCurve"] = 0.64
    hp["earMarginBow"] = 0.82
    hp["earRootFlare"] = 0.72
    hp["earTipRoundness"] = 0.78
    hp["earFold"] = 0.10
    hp["earOutward"] = 0.28
    hp["earForward"] = 0.30
    hp["nasalColor"] = rgba(76, 56, 50)

    s["headEnvelope"]["scale"] = 0.23 + 0.025 * (1.0 - dev_ceph)
    s["headEnvelope"]["widthScale"] = 0.94
    s["headEnvelope"]["heightScale"] = 1.00
    s["headEnvelope"]["lengthScale"] = 0.96
    s.pop("headHybrid", None)

    # Alas quiropteras (miembros 0 y 1): LIMB_ARCHETYPE_WING = 3
    for idx in (0, 1):
        w = s["limbs"][idx]
        w["archetype"] = 3  # LIMB_ARCHETYPE_WING
        w["anatomyProfile"] = 0
        w["role"] = 0
        w["side"] = idx
        w["length"] = 1.48 * (0.65 + 0.35 * dev_app)
        w["thickness"] = 0.032
        w["proximalScale"] = 0.92
        w["middleScale"] = 1.06
        w["distalScale"] = 1.15
        w["proximalSweep"] = 0.0
        w["distalSweep"] = 0.0
        w["sprawl"] = 0.08
        w["attachmentInset"] = 0.18
        w["rootThicknessScale"] = 1.18
        w["middleThicknessScale"] = 0.92
        w["distalThicknessScale"] = 0.82
        w["proximalThicknessScale"] = 1.05
        w["footScale"] = 0.95
        w["footWidthScale"] = 1.0
        w["footHeightScale"] = 1.0
        w["footYaw"] = 0.0
        w["digitCount"] = 5
        w["digitPhalanges"] = [1, 2, 3, 2, 2, 0, 0, 0]
        # Pulgar libre corto + abanico II..V
        w["digitLengths"] = [0.74, 0.57, 1.04, 0.92, 0.82, 0.0, 0.0, 0.0]
        w["digitSpread"] = 0.0
        w["digitAngles"] = [0.0, 0.04, -0.06, -0.18, -0.26, 0.0, 0.0, 0.0]
        w["digitThicknessScale"] = 0.92
        w["clawScale"] = 1.50
        w["nailCount"] = 0
        w["webbingCoverage"] = 1.0
        w["webbingThickness"] = 0.16
        w["webbingEdgeCurve"] = 0.22
        w["webbingFold"] = 0.0
        w["flexionScale"] = 1.10
        w["development"] = 1.0

    # Patas posteriores cortas con garras de suspensión/apoyo (misma altura base que pelvis)
    for idx in (2, 3):
        hl = s["limbs"][idx]
        hl["archetype"] = 1  # LIMB_ARCHETYPE_MAMMAL
        hl["anatomyProfile"] = 4  # LIMB_PROFILE_PLANTIGRADE
        hl["role"] = 1
        hl["side"] = idx - 2
        hl["length"] = 0.33
        hl["thickness"] = 0.028
        hl["proximalScale"] = 0.95
        hl["middleScale"] = 1.08
        hl["distalScale"] = 0.92
        hl["proximalSweep"] = -0.10
        hl["distalSweep"] = -0.12
        hl["sprawl"] = 0.28
        hl["attachmentInset"] = 0.14
        hl["rootThicknessScale"] = 1.08
        hl["middleThicknessScale"] = 0.88
        hl["distalThicknessScale"] = 0.72
        hl["proximalThicknessScale"] = 1.0
        hl["footScale"] = 1.05
        hl["footWidthScale"] = 0.88
        hl["footHeightScale"] = 0.68
        hl["footYaw"] = 0.18
        hl["digitCount"] = 5
        hl["digitPhalanges"] = [2, 2, 2, 2, 2, 0, 0, 0]
        hl["digitLengths"] = [0.75, 0.92, 0.98, 0.94, 0.82, 0.0, 0.0, 0.0]
        hl["digitSpread"] = 0.36
        hl["digitAngles"] = [-0.36, -0.18, 0.0, 0.18, 0.36, 0.0, 0.0, 0.0]
        hl["digitThicknessScale"] = 0.72
        hl["clawScale"] = 1.25
        hl["nailCount"] = 0
        hl["webbingCoverage"] = 0.0
        hl["webbingThickness"] = 0.0
        hl["webbingEdgeCurve"] = 0.0
        hl["webbingFold"] = 0.0
        hl["flexionScale"] = 1.05
        hl["development"] = 1.0

    # Cola fina contenida dentro del uropatagio interfemoral
    tail = s["tails"][0]
    tail["archetype"] = 0  # TAIL_ARCHETYPE_TAPERED
    tail["length"] = 0.30
    tail["baseWidth"] = 0.032
    tail["baseHeight"] = 0.032
    tail["tipWidth"] = 0.011
    tail["tipHeight"] = 0.011
    tail["taperCurve"] = 1.15
    tail["droop"] = 0.08
    tail["tipLift"] = 0.06
    tail["rootMatchStrength"] = 0.48
    tail["segmentCount"] = 5
    tail["development"] = 1.0

    # Paneles de membrana alar explícitos compartiendo el sistema genérico Membrane:
    # 0,1: Plagiopatagio (tórax/abdomen/pelvis/tarso <-> hombro/codo/carpo/dedo V)
    # 2,3: Propatagio anterior (cuello <-> hombro/codo/carpo)
    # 4,5: Uropatagio interfemoral (cola base->punta <-> cadera/tarso)
    # Limb V phalanges=2 -> última falange local = Limb_DigitLocalId(4, 2) = 66
    wing_col = rgba(64, 48, 42)
    patag_col = rgba(58, 43, 38)
    s["membraneCount"] = 6
    s["membranes"] = [
        # Plagiopatagio izquierdo / derecho
        make_panel(1, 2, 12, 3, 10, 1, 10, 66, 1.0, 0.008, 0.13, 0.016, 0.90, dev_app, wing_col),
        make_panel(1, 2, 13, 3, 11, 1, 11, 66, 1.0, 0.008, 0.13, 0.016, 0.90, dev_app, wing_col),
        # Propatagio izquierdo / derecho desde la base cervical hasta hombro, codo y carpo
        make_panel(1, 1, 10, 1, 10, 1, 10, 4, 1.0, 0.007, 0.05, 0.010, 0.92, dev_app, patag_col),
        make_panel(1, 1, 11, 1, 11, 1, 11, 4, 1.0, 0.007, 0.05, 0.010, 0.92, dev_app, patag_col),
        # Uropatagio izquierdo / derecho
        make_panel(20, 1, 20, 5, 12, 1, 12, 4, 1.0, 0.007, 0.18, 0.010, 0.88, dev_app, patag_col),
        make_panel(20, 1, 20, 5, 13, 1, 13, 4, 1.0, 0.007, 0.18, 0.010, 0.88, dev_app, patag_col),
    ]

    # Pelaje pardo-grisáceo sedoso dorsal y gris-pardo pálido ventral; membranas coriáceas oscuras
    dorsal = rgba(96, 74, 56) if mat > 0.35 else rgba(110, 96, 86)
    ventral = rgba(154, 138, 120)
    s["dorsalColor"] = dorsal
    s["ventralColor"] = ventral
    s["unpigmentedVentralColor"] = ventral

    surf = s["surface"]
    surf["integument"]["type"] = 2  # INTEGUMENT_FUR
    surf["integument"]["coverage"] = 1.0
    surf["integument"]["plates"]["armorScale"] = 0.0
    fur = surf["integument"]["fur"]
    fur["density"] = 1.45
    fur["length"] = 0.016
    fur["thickness"] = 0.48
    fur["clumping"] = 0.18
    fur["lay"] = 0.88
    fur["undercoat"] = 0.82
    fur["roughness"] = 0.56
    fur["sheen"] = 0.28
    fur["rootDarkening"] = 0.22
    fur["tipLightening"] = 0.10
    surf["wrinkleStrength"] = 0.12
    if "regionalAlbedo" in surf:
        surf["regionalAlbedo"] = [rgba(0, 0, 0, 0) for _ in surf["regionalAlbedo"]]
        if len(surf["regionalAlbedo"]) > 6:
            surf["regionalAlbedo"][6] = wing_col  # SURFACE_REGION_DIGIT / extremidad distal oscura

    pig = surf["pigment"]
    pig["baseColor"] = dorsal
    pig["secondaryColor"] = rgba(66, 50, 42)
    pig["ventralColor"] = ventral
    pig["patternStrength"] = 0.24 * dev_pig
    pig["ventralLightening"] = 0.72
    pig["dorsalDarkening"] = 0.18 * dev_pig
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
    rec["id"] = 1015
    rec["name"] = "Murcielago Orejudo"
    rec["gait"] = 2  # CREATURE_GAIT_UPRIGHT_QUADRUPED (patas traseras terrestres, alas libres)

    rec["seed"] = configure_stage(rec["adult"], head_ref, 0.14, 0.70, 0.72, 0.55, 0.72, 0.25)
    rec["larva"] = configure_stage(rec["adult"], head_ref, 0.24, 0.84, 0.84, 0.72, 0.85, 0.55)
    rec["juvenile"] = configure_stage(rec["adult"], head_ref, 0.34, 0.94, 0.94, 0.88, 0.94, 0.82)
    rec["adult"] = configure_stage(rec["adult"], head_ref, 0.44, 1.00, 1.00, 1.00, 1.00, 1.00)

    OUT_PATH.write_text(json.dumps(doc, indent=2) + "\n")
    print(f"Wrote {OUT_PATH}")


if __name__ == "__main__":
    main()
