#!/usr/bin/env python3
"""Genera las cinco recetas externas acuáticas (IDs 1021..1025) en formato v1 compatible."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "recipes" / "ornitorrinco.creature.json"


def rgba(r, g, b, a=255):
    return {"r": int(r), "g": int(g), "b": int(b), "a": int(a)}


def mod_inst(instance_id, kind, host_id, slot_id, phen_idx=0):
    return {
        "instanceId": instance_id,
        "kind": kind,
        "attachment": {"hostModuleInstanceId": host_id, "slotId": slot_id},
        "phenotypeIndex": phen_idx,
        "pathAttached": False,
        "pathNodeA": 0,
        "pathNodeB": 0,
        "pathU": 0.0,
        "lateralOffset": 0.0,
        "pathNormal": {"x": 0.0, "y": 1.0, "z": 0.0},
        "membranePrevious": 0,
    }


def mod_path(instance_id, kind, node_a, node_b, path_u, normal_vec, phen_idx=0, lateral=0.0):
    return {
        "instanceId": instance_id,
        "kind": kind,
        "attachment": {"hostModuleInstanceId": 0, "slotId": 0},
        "phenotypeIndex": phen_idx,
        "pathAttached": True,
        "pathNodeA": node_a,
        "pathNodeB": node_b,
        "pathU": float(path_u),
        "lateralOffset": float(lateral),
        "pathNormal": {"x": float(normal_vec[0]), "y": float(normal_vec[1]), "z": float(normal_vec[2])},
        "membranePrevious": 0,
    }


def make_fin(family, role, side, base_chord, span, thickness, root_width,
             sweep=0.35, leading=0.25, trailing=0.25, free_rear_tip=0.0, tip_shape=0.25, camber=0.0,
             dihedral=0.0, cant=0.0, ray_count=0, finlet_count=0, series_len=0.0,
             dev=1.0, base_col=None, tip_col=None):
    return {
        "family": int(family),
        "role": int(role),
        "side": int(side),
        "baseChord": float(base_chord),
        "span": float(span),
        "thickness": float(thickness),
        "rootWidth": float(root_width),
        "sweep": float(sweep),
        "leadingCurve": float(leading),
        "trailingCurve": float(trailing),
        "freeRearTip": float(free_rear_tip),
        "tipShape": float(tip_shape),
        "camber": float(camber),
        "dihedral": float(dihedral),
        "cant": float(cant),
        "rayCount": int(ray_count),
        "finletCount": int(finlet_count),
        "seriesLength": float(series_len if series_len > 0 else base_chord),
        "development": float(dev),
        "baseColor": base_col if base_col else rgba(0, 0, 0, 0),
        "tipColor": tip_col if tip_col else rgba(0, 0, 0, 0),
    }


def blank_shell():
    return {
        "development": 0.0,
        "length": 0.0,
        "width": 0.0,
        "height": 0.0,
        "dorsalConvexity": 0.55,
        "heartTaper": 0.0,
        "plastronWidth": 0.0,
        "plastronLength": 0.0,
        "plastronDepth": 0.0,
        "bridgeWidth": 0.0,
        "shellThickness": 0.05,
        "costalScutePairs": 0,
        "vertebralScutes": 0,
        "marginalScutes": 0,
        "scuteRelief": 0.0,
        "cervicalNotch": 0.0,
        "limbClearance": 0.0,
        "carapaceColor": rgba(0, 0, 0, 0),
        "scuteBorderColor": rgba(0, 0, 0, 0),
        "plastronColor": rgba(0, 0, 0, 0),
    }


def blank_marine_flipper(template, role, side, length, thickness, aspect, sweep, camber=0.08, sprawl=0.85, dev=1.0, claw_scale=0.0,
                         flipper_surface=0, scale_size=0.0, scale_irreg=0.0, border_contrast=0.0, roughness=0.0):
    l = copy.deepcopy(template)
    l["archetype"] = 4   # LIMB_ARCHETYPE_FLIPPER
    l["anatomyProfile"] = 6  # LIMB_PROFILE_MARINE_FLIPPER
    l["role"] = int(role)
    l["side"] = int(side)
    l["length"] = float(length)
    l["thickness"] = float(thickness)
    l["sprawl"] = float(sprawl)
    l["development"] = float(dev)
    l["digitCount"] = 0
    l["digitLengths"] = [1.0] * 8
    l["digitPhalanges"] = [2] * 8
    l["digitAngles"] = [0.0] * 8
    l["digitSpread"] = 0.8
    l["footScale"] = 1.0
    l["proximalScale"] = 1.0
    l["middleScale"] = 1.0
    l["distalScale"] = 1.0
    l["rootThicknessScale"] = 1.0
    l["proximalThicknessScale"] = 1.0
    l["middleThicknessScale"] = 1.0
    l["distalThicknessScale"] = 1.0
    l["footWidthScale"] = 1.0
    l["footHeightScale"] = 1.0
    l["digitThicknessScale"] = 1.0
    l["attachmentInset"] = 0.25
    l["proximalSweep"] = 0.0
    l["distalSweep"] = 0.0
    l["footYaw"] = 0.0
    l["flexionScale"] = 1.0
    l["hoofWallSlope"] = 0.22
    l["hoofToeRoundness"] = 0.8
    l["hoofHeelHeight"] = 0.75
    l["dewclawSize"] = 0.0
    l["nailCount"] = 0
    l["clawScale"] = float(claw_scale)
    l["webbingCoverage"] = 0.0
    l["webbingThickness"] = 0.16
    l["webbingEdgeCurve"] = 0.15
    l["webbingFold"] = 0.0
    l["featherCount"] = 0
    l["wingAspect"] = float(aspect)
    l["wingPointedness"] = 0.65
    l["finSweep"] = float(sweep)
    l["finCamber"] = float(camber)
    l["flipperSurface"] = int(flipper_surface)
    l["flipperScaleSize"] = float(scale_size)
    l["flipperScaleIrregularity"] = float(scale_irreg)
    l["flipperBorderContrast"] = float(border_contrast)
    l["flipperRoughness"] = float(roughness)
    return l


def make_tail(arch, length, base_w, base_h, tip_w, tip_h, segs, taper=1.25,
              curv=0.0, droop=0.0, root_match=0.92, dev=1.0,
              caudal_type=0, caudal_span=0.0, caudal_chord=0.0,
              caudal_ratio=1.0, caudal_notch=0.4, caudal_sweep=0.45,
              caudal_thick=0.03, caudal_keel=0.0, caudal_col=None):
    return {
        "droop": float(droop),
        "archetype": int(arch),
        "length": float(length),
        "baseWidth": float(base_w),
        "baseHeight": float(base_h),
        "tipWidth": float(tip_w),
        "tipHeight": float(tip_h),
        "taperCurve": float(taper),
        "curvature": float(curv),
        "development": float(dev),
        "rootMatchStrength": float(root_match),
        "segmentCount": int(segs),
        "tipLift": 0.0,
        "caudalFinType": int(caudal_type),
        "caudalSpan": float(caudal_span),
        "caudalChord": float(caudal_chord),
        "caudalUpperLobeRatio": float(caudal_ratio),
        "caudalNotchDepth": float(caudal_notch),
        "caudalSweep": float(caudal_sweep),
        "caudalThickness": float(caudal_thick),
        "caudalKeelStrength": float(caudal_keel),
        "caudalColor": caudal_col if caudal_col else rgba(0, 0, 0, 0),
    }


def make_pigment_layer(pattern, color, scale=2.0, sharpness=0.5, strength=0.5,
                       direction=(0.0, -1.0, 0.0), seed=1, region_mask=0):
    return {
        "pattern": int(pattern),
        "color": color,
        "scale": float(scale),
        "sharpness": float(sharpness),
        "strength": float(strength),
        "direction": {"x": float(direction[0]), "y": float(direction[1]), "z": float(direction[2])},
        "seed": int(seed),
        "regionMask": int(region_mask),
        "patternRotation": 0.0,
    }


def clean_surface(surf, integ_type, base_col, sec_col, vent_col, seed,
                  coverage=0.85, roughness=0.22, sheen=0.25, scale_size=0.04,
                  scale_relief=0.12, layers=None):
    surf["integument"]["type"] = int(integ_type)
    surf["integument"]["coverage"] = float(coverage)
    surf["integument"]["fur"]["density"] = 0.0
    surf["integument"]["fur"]["length"] = 0.0
    surf["integument"]["plates"]["armorScale"] = 0.0
    surf["integument"]["scales"]["size"] = float(scale_size)
    surf["integument"]["scales"]["relief"] = float(scale_relief)
    surf["integument"]["scales"]["roughness"] = float(roughness)
    surf["integument"]["scales"]["seed"] = int(seed)
    surf["pigment"]["baseColor"] = base_col
    surf["pigment"]["secondaryColor"] = sec_col
    surf["pigment"]["ventralColor"] = vent_col
    surf["pigment"]["dorsalDarkening"] = 0.34
    surf["pigment"]["ventralLightening"] = 0.42
    surf["pigment"]["patternStrength"] = 0.30
    surf["pigment"]["patternScale"] = 2.4
    surf["pigment"]["bands"] = 0.0
    surf["pigment"]["seed"] = int(seed)
    surf["pigment"]["layers"] = layers if layers else []
    surf["pigment"]["layerCount"] = len(surf["pigment"]["layers"])
    surf["tufts"] = []
    surf["tuftCount"] = 0
    surf["longHair"] = []
    surf["longHairCount"] = 0
    surf["wrinkleStrength"] = 0.0
    surf["translucency"] = 0.0
    surf["iridescence"] = float(sheen)
    surf["subsurfaceGlow"] = 0.0
    surf["regionalAlbedo"] = [rgba(0, 0, 0, 0) for _ in range(11)]


def reset_axial_extras(ax):
    ax["contourProfile"] = 0
    ax["withersRoundness"] = 0.0
    ax["dewlapDepth"] = 0.0
    ax["brisketDepth"] = 0.0
    ax["withersElevation"] = 0.0
    ax["cervicalSegments"] = 0
    ax["cervicalPitch"] = 0.0
    ax["spineFlexion"] = 0.25
    ax["radialSymmetryOrder"] = 0
    ax["bellConcavity"] = 0.0
    ax["bellScallopAmplitude"] = 0.0
    ax["abdomenSegments"] = 0
    ax["discSweep"] = 0.0
    ax["mantleElongation"] = 0.0
    ax["profile"] = [
        {"longitudinalOffset": 0.0, "verticalOffset": 0.0, "widthScale": 1.0, "heightScale": 1.0}
        for _ in range(6)
    ]


def clear_optional_arrays(st):
    st["limbs"] = []
    st["limbCount"] = 0
    st["tails"] = []
    st["tailCount"] = 0
    st["ornaments"] = []
    st["ornamentCount"] = 0
    st["flexibleAppendages"] = []
    st["flexibleAppendageCount"] = 0
    st["membranes"] = []
    st["membraneCount"] = 0
    st["repeatedAppendages"] = []
    st["repeatedAppendageCount"] = 0
    st["fins"] = []
    st["finCount"] = 0
    st["shell"] = blank_shell()
    st["headHybrid"]["active"] = False
    st["headHybrid"]["hasRegionalProfile"] = False


# =============================================================================
# 1. ATLANTIC BLUEFIN TUNA (Thunnus thynnus) — ID 1021
# =============================================================================
def build_tuna(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r["id"] = 1021
    r["name"] = "Atun Rojo"
    r["gait"] = 12  # CREATURE_GAIT_THUNNIFORM_SWIM
    r["offspringSeed"] = 0
    r["offspringVersion"] = 0

    # Axial=1, Head=2, Tail=3, Fins=4..11
    # Path nodes on axial module 1: (1<<16)|nodeId
    n_thxP = (1 << 16) | 4
    n_abd  = (1 << 16) | 5
    n_pel  = (1 << 16) | 6
    r["bodyPlan"] = {
        "root": {"moduleInstanceId": 1, "localNodeId": 2},
        "modules": [
            mod_inst(1, 0, 0, 0, 0),   # AXIAL
            mod_inst(2, 1, 1, 1, 0),   # HEAD -> CERVICAL
            mod_inst(3, 3, 1, 6, 0),   # TAIL -> CAUDAL
            mod_inst(4, 6, 1, 7, 0),   # FIN 0: 1st dorsal
            mod_path(5, 6, n_thxP, n_abd, 0.25, (0.0, 1.0, 0.0), 1),  # FIN 1: 2nd dorsal
            mod_inst(6, 6, 1, 48, 2),  # FIN 2: dorsal finlet series (9)
            mod_inst(7, 6, 1, 2, 3),   # FIN 3: left pectoral
            mod_inst(8, 6, 1, 3, 4),   # FIN 4: right pectoral
            mod_inst(9, 6, 1, 4, 5),   # FIN 5: left pelvic
            mod_inst(10, 6, 1, 5, 6),  # FIN 6: right pelvic
            mod_inst(11, 6, 1, 49, 7), # FIN 7: anal fin
            mod_path(12, 6, n_abd, n_pel, 0.20, (0.0, -1.0, 0.0), 8), # FIN 8: anal finlet series (8)
        ],
        "moduleCount": 12,
    }

    stage_cfg = [
        ("seed",     0.28, 0.62, 0.72, 0.78),
        ("larva",    0.46, 0.78, 0.84, 0.88),
        ("juvenile", 0.74, 0.92, 0.94, 0.95),
        ("adult",    1.00, 1.00, 1.00, 1.00),
    ]
    navy = rgba(22, 42, 78)
    steel = rgba(64, 96, 138)
    silver = rgba(236, 242, 248)
    fin_dark = rgba(34, 48, 68)
    pec_dark = rgba(28, 40, 58)
    fin_amber = rgba(164, 134, 62)
    finlet_yel = rgba(224, 190, 56)

    for sname, scale, depth_m, fin_m, col_m in stage_cfg:
        st = r[sname]
        clear_optional_arrays(st)
        ax = st["axial"]
        reset_axial_extras(ax)
        ax["archetype"] = 9  # AXIAL_ARCHETYPE_AQUATIC_FUSIFORM
        ax["totalScale"] = scale
        ax["withersHeight"] = 1.65
        ax["trunkLength"] = 3.65
        ax["neckLength"] = 0.45
        ax["neckWidth"] = 0.88 * depth_m
        ax["shoulderWidth"] = 1.08 * depth_m
        ax["thoraxWidth"] = 1.12 * depth_m
        ax["thoraxHeight"] = 1.78 * depth_m
        ax["abdomenWidth"] = 0.92 * depth_m
        ax["abdomenHeight"] = 1.54 * depth_m
        ax["pelvicWidth"] = 0.22
        ax["pelvicHeight"] = 0.22
        ax["bodyFlattening"] = 0.96
        ax["limbAttachmentLateral"] = 0.86

        hp = st["head"]
        hp["archetype"] = 14  # HEAD_ARCHETYPE_TELEOST
        hp["skullWidth"] = 0.56
        hp["skullHeight"] = 0.80
        hp["skullLength"] = 0.66
        hp["muzzleLength"] = 0.52
        hp["muzzleWidth"] = 0.46
        hp["muzzleTaper"] = 0.86
        hp["rostrumDepth"] = 0.54
        hp["eyeSize"] = 0.36 if sname in ("seed", "larva") else 0.28
        hp["eyeLaterality"] = 0.88
        hp["eyeForwardness"] = 0.46
        hp["eyeDorsality"] = 0.54
        hp["eyeExposure"] = 0.11
        hp["earSize"] = 0.0
        hp["noseScale"] = 0.02
        st["headEnvelope"] = {"scale": 0.90, "widthScale": 0.86, "heightScale": 0.96, "lengthScale": 0.94}

        st["eyes"]["size"] = hp["eyeSize"]
        st["eyes"]["irisColor"] = rgba(198, 206, 214)
        st["eyes"]["pupilColor"] = rgba(12, 16, 24)
        st["eyes"]["scleraColor"] = rgba(226, 232, 238)
        st["eyes"]["pupilShape"] = 0

        st["tails"] = [make_tail(
            arch=0, length=0.68, base_w=0.14, base_h=0.14, tip_w=0.022, tip_h=0.022,
            segs=4, taper=1.35, root_match=0.95,
            caudal_type=1,  # CAUDAL_FIN_HOMOCERCAL_LUNATE
            caudal_span=1.84 * fin_m, caudal_chord=0.46 * fin_m,
            caudal_ratio=1.0, caudal_notch=0.84, caudal_sweep=0.56,
            caudal_thick=0.042, caudal_keel=0.92, caudal_col=fin_dark
        )]
        st["tailCount"] = 1

        st["fins"] = [
            # 0: 1st dorsal fin (tall triangular/swept)
            make_fin(1, 2, 0, 0.78, 0.58 * fin_m, 0.032, 0.055, sweep=0.42, leading=0.18, trailing=0.28,
                     tip_shape=0.18, ray_count=14, base_col=fin_dark, tip_col=fin_amber),
            # 1: 2nd dorsal fin (falcate)
            make_fin(1, 3, 0, 0.36, 0.62 * fin_m, 0.028, 0.045, sweep=0.58, leading=0.30, trailing=0.52,
                     tip_shape=0.12, ray_count=12, base_col=fin_amber, tip_col=fin_dark),
            # 2: dorsal finlet series (9 finlets)
            make_fin(1, 6, 0, 0.14, 0.12 * fin_m, 0.012, 0.016, sweep=0.75, finlet_count=9,
                     series_len=1.08, base_col=finlet_yel, tip_col=fin_dark),
            # 3-4: short dark swept pectoral fins (Thunnus thynnus: <80% head length)
            make_fin(0, 0, 1, 0.32, 0.72 * fin_m, 0.024, 0.040, sweep=0.74, leading=0.20, trailing=0.32,
                     tip_shape=0.12, dihedral=-0.26, ray_count=16, base_col=pec_dark, tip_col=fin_dark),
            make_fin(0, 0, 2, 0.32, 0.72 * fin_m, 0.024, 0.040, sweep=0.74, leading=0.20, trailing=0.32,
                     tip_shape=0.12, dihedral=-0.26, ray_count=16, base_col=pec_dark, tip_col=fin_dark),
            # 5-6: small pelvic fins
            make_fin(0, 1, 1, 0.22, 0.30 * fin_m, 0.016, 0.026, sweep=0.62, dihedral=-0.72,
                     ray_count=8, base_col=silver, tip_col=fin_dark),
            make_fin(0, 1, 2, 0.22, 0.30 * fin_m, 0.016, 0.026, sweep=0.62, dihedral=-0.72,
                     ray_count=8, base_col=silver, tip_col=fin_dark),
            # 7: anal fin (falcate)
            make_fin(2, 4, 0, 0.32, 0.52 * fin_m, 0.026, 0.042, sweep=0.60, leading=0.28, trailing=0.50,
                     tip_shape=0.14, ray_count=12, base_col=fin_amber, tip_col=fin_dark),
            # 8: anal finlet series (8 finlets)
            make_fin(2, 6, 0, 0.13, 0.11 * fin_m, 0.011, 0.015, sweep=0.75, finlet_count=8,
                     series_len=0.96, base_col=finlet_yel, tip_col=fin_dark),
        ]
        st["finCount"] = len(st["fins"])

        st["dorsalColor"] = navy
        st["ventralColor"] = silver
        st["unpigmentedVentralColor"] = silver
        st["development"] = {
            "appendages": 1.0, "cephalic": 1.0, "pigmentation": col_m,
            "integument": 1.0, "colorMaturity": col_m,
        }
        clean_surface(st["surface"], 1, navy, steel, silver, seed=1021,
                      coverage=0.32, roughness=0.14, sheen=0.48, scale_size=0.008, scale_relief=0.012)
        st["surface"]["pigment"]["ventralLightening"] = 0.94
        st["surface"]["pigment"]["dorsalDarkening"] = 0.44
        st["surface"]["pigment"]["patternStrength"] = 0.18
    return doc


# =============================================================================
# 2. SCALLOPED HAMMERHEAD SHARK (Sphyrna lewini) — ID 1022
# =============================================================================
def build_hammerhead(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r["id"] = 1022
    r["name"] = "Tiburon Martillo"
    r["gait"] = 13  # CREATURE_GAIT_CARANGIFORM_SHARK_SWIM
    r["offspringSeed"] = 0
    r["offspringVersion"] = 0

    r["bodyPlan"] = {
        "root": {"moduleInstanceId": 1, "localNodeId": 2},
        "modules": [
            mod_inst(1, 0, 0, 0, 0),   # AXIAL
            mod_inst(2, 1, 1, 1, 0),   # HEAD -> CERVICAL
            mod_inst(3, 3, 1, 6, 0),   # TAIL -> CAUDAL
            mod_inst(4, 6, 1, 7, 0),   # FIN 0: 1st dorsal (tall falcate)
            mod_inst(5, 6, 1, 48, 1),  # FIN 1: 2nd dorsal (small)
            mod_inst(6, 6, 1, 2, 2),   # FIN 2: left pectoral
            mod_inst(7, 6, 1, 3, 3),   # FIN 3: right pectoral
            mod_inst(8, 6, 1, 4, 4),   # FIN 4: left pelvic
            mod_inst(9, 6, 1, 5, 5),   # FIN 5: right pelvic
            mod_inst(10, 6, 1, 49, 6), # FIN 6: anal fin
        ],
        "moduleCount": 10,
    }

    stage_cfg = [
        ("seed",     0.32, 0.84, 0.76, 0.75),
        ("larva",    0.50, 0.90, 0.85, 0.85),
        ("juvenile", 0.76, 0.96, 0.94, 0.95),
        ("adult",    1.00, 1.00, 1.00, 1.00),
    ]
    bronze = rgba(104, 108, 98)
    slate = rgba(74, 78, 72)
    belly = rgba(238, 236, 228)
    fin_tip = rgba(58, 62, 60)
    pelvic_tip = rgba(134, 138, 128)

    for sname, scale, hammer_m, fin_m, col_m in stage_cfg:
        st = r[sname]
        clear_optional_arrays(st)
        ax = st["axial"]
        reset_axial_extras(ax)
        ax["archetype"] = 9  # AXIAL_ARCHETYPE_AQUATIC_FUSIFORM
        ax["totalScale"] = scale
        ax["withersHeight"] = 1.65
        ax["trunkLength"] = 4.55
        ax["neckLength"] = 0.55
        ax["neckWidth"] = 0.72
        ax["shoulderWidth"] = 0.88
        ax["thoraxWidth"] = 0.92
        ax["thoraxHeight"] = 1.02
        ax["abdomenWidth"] = 0.74
        ax["abdomenHeight"] = 0.82
        ax["pelvicWidth"] = 0.28
        ax["pelvicHeight"] = 0.30
        ax["bodyFlattening"] = 0.92
        ax["limbAttachmentLateral"] = 0.88

        hp = st["head"]
        hp["archetype"] = 15  # HEAD_ARCHETYPE_ELASMOBRANCH
        hp["skullWidth"] = 0.95
        hp["skullHeight"] = 0.32
        hp["skullLength"] = 0.56
        hp["muzzleLength"] = 0.48
        hp["muzzleWidth"] = 0.96 * hammer_m
        hp["muzzleTaper"] = 0.14
        hp["rostrumDepth"] = 0.26
        hp["eyeSize"] = 0.36
        hp["eyeLaterality"] = 0.98
        hp["eyeForwardness"] = 0.58
        hp["eyeDorsality"] = 0.50
        hp["eyeExposure"] = 0.22
        hp["earSize"] = 0.0
        hp["noseScale"] = 0.04
        st["headEnvelope"] = {
            "scale": 0.96, "widthScale": 0.94 * hammer_m, "heightScale": 0.84, "lengthScale": 1.00
        }

        st["eyes"]["size"] = hp["eyeSize"]
        st["eyes"]["irisColor"] = rgba(168, 162, 132)
        st["eyes"]["pupilColor"] = rgba(10, 12, 16)
        st["eyes"]["scleraColor"] = rgba(196, 194, 184)
        st["eyes"]["pupilShape"] = 0

        st["tails"] = [make_tail(
            arch=0, length=1.12, base_w=0.15, base_h=0.17, tip_w=0.024, tip_h=0.028,
            segs=5, taper=1.35, root_match=0.94,
            caudal_type=2,  # CAUDAL_FIN_HETEROCERCAL
            caudal_span=2.05 * fin_m, caudal_chord=0.52 * fin_m,
            caudal_ratio=1.74, caudal_notch=0.68, caudal_sweep=0.72,
            caudal_thick=0.048, caudal_col=bronze
        )]
        st["tailCount"] = 1

        st["fins"] = [
            # 0: 1st dorsal (tall, prominent, slightly falcate)
            make_fin(3, 2, 0, 0.72, 1.08 * fin_m, 0.048, 0.085, sweep=0.52, leading=0.26, trailing=0.46,
                     tip_shape=0.18, ray_count=0, base_col=bronze, tip_col=fin_tip),
            # 1: 2nd dorsal (low with long free rear tip — diagnostic of Sphyrna lewini)
            make_fin(3, 3, 0, 0.28, 0.17 * fin_m, 0.018, 0.032, sweep=0.82, leading=0.18, trailing=0.44,
                     free_rear_tip=1.10, tip_shape=0.14, ray_count=0, base_col=bronze, tip_col=fin_tip),
            # 2-3: broad swept pectoral fins
            make_fin(3, 0, 1, 0.74, 1.56 * fin_m, 0.048, 0.082, sweep=0.56, leading=0.24, trailing=0.36,
                     tip_shape=0.18, dihedral=-0.22, ray_count=0, base_col=bronze, tip_col=fin_tip),
            make_fin(3, 0, 2, 0.74, 1.56 * fin_m, 0.048, 0.082, sweep=0.56, leading=0.24, trailing=0.36,
                     tip_shape=0.18, dihedral=-0.22, ray_count=0, base_col=bronze, tip_col=fin_tip),
            # 4-5: pelvic fins (gradual natural coloration, no artificial white tips)
            make_fin(3, 1, 1, 0.34, 0.38 * fin_m, 0.022, 0.036, sweep=0.58, dihedral=-0.32,
                     ray_count=0, base_col=bronze, tip_col=pelvic_tip),
            make_fin(3, 1, 2, 0.34, 0.38 * fin_m, 0.022, 0.036, sweep=0.58, dihedral=-0.32,
                     ray_count=0, base_col=bronze, tip_col=pelvic_tip),
            # 6: anal fin (slightly larger than 2nd dorsal with moderate free rear tip)
            make_fin(3, 4, 0, 0.32, 0.24 * fin_m, 0.020, 0.034, sweep=0.68, leading=0.20, trailing=0.46,
                     free_rear_tip=0.62, tip_shape=0.16, ray_count=0, base_col=bronze, tip_col=fin_tip),
        ]
        st["finCount"] = len(st["fins"])

        st["dorsalColor"] = bronze
        st["ventralColor"] = belly
        st["unpigmentedVentralColor"] = belly
        st["development"] = {
            "appendages": 1.0, "cephalic": 1.0, "pigmentation": col_m,
            "integument": 1.0, "colorMaturity": col_m,
        }
        clean_surface(st["surface"], 0, bronze, slate, belly, seed=1022,
                      coverage=0.80, roughness=0.34, sheen=0.14, scale_size=0.015, scale_relief=0.04)
        st["surface"]["pigment"]["ventralLightening"] = 0.92
        st["surface"]["pigment"]["dorsalDarkening"] = 0.34
        st["surface"]["pigment"]["patternStrength"] = 0.16
    return doc


# =============================================================================
# 3. LONGSNOUT SEAHORSE (Hippocampus reidi) — ID 1023
# =============================================================================
def build_seahorse(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r["id"] = 1023
    r["name"] = "Caballito de Mar"
    r["gait"] = 14  # CREATURE_GAIT_SEAHORSE_SWIM
    r["offspringSeed"] = 0
    r["offspringVersion"] = 0

    r["bodyPlan"] = {
        "root": {"moduleInstanceId": 1, "localNodeId": 2},
        "modules": [
            mod_inst(1, 0, 0, 0, 0),   # AXIAL (CURVED_VERTICAL)
            mod_inst(2, 1, 1, 1, 0),   # HEAD -> CERVICAL (angled forward)
            mod_inst(3, 3, 1, 6, 0),   # TAIL -> CAUDAL (ventrally curled prehensile)
            mod_inst(4, 6, 1, 7, 0),   # FIN 0: dorsal fin (on posterior back)
            mod_inst(5, 6, 1, 2, 1),   # FIN 1: left pectoral fin
            mod_inst(6, 6, 1, 3, 2),   # FIN 2: right pectoral fin
        ],
        "moduleCount": 6,
    }

    # Post-release stages: early juvenile (slight temporary caudal finlet) -> adult (curled tail, no caudal fin)
    stage_cfg = [
        ("seed",     0.30, 0.55, 0.12, 0.72, 4),
        ("larva",    0.48, 0.72, 0.05, 0.84, 4),
        ("juvenile", 0.76, 0.88, 0.00, 0.94, 0),
        ("adult",    1.00, 0.98, 0.00, 1.00, 0),
    ]
    gold = rgba(218, 152, 46)
    amber = rgba(162, 92, 26)
    cream = rgba(234, 196, 118)
    fin_tr = rgba(226, 186, 102)

    for sname, scale, curl_m, juv_caudal, col_m, c_type in stage_cfg:
        st = r[sname]
        clear_optional_arrays(st)
        ax = st["axial"]
        reset_axial_extras(ax)
        ax["archetype"] = 10  # AXIAL_ARCHETYPE_CURVED_VERTICAL
        ax["totalScale"] = scale
        ax["withersHeight"] = 2.65
        ax["trunkLength"] = 2.45
        ax["neckLength"] = 0.55
        ax["neckWidth"] = 0.42
        ax["shoulderWidth"] = 0.52
        ax["thoraxWidth"] = 0.64
        ax["thoraxHeight"] = 0.76
        ax["abdomenWidth"] = 0.66
        ax["abdomenHeight"] = 0.82
        ax["pelvicWidth"] = 0.34
        ax["pelvicHeight"] = 0.36
        ax["bodyFlattening"] = 0.68
        ax["cervicalCurvature"] = 0.84
        ax["cervicalPitch"] = 0.78
        ax["abdomenSegments"] = 11  # ~11 trunk bony rings

        hp = st["head"]
        hp["archetype"] = 16  # HEAD_ARCHETYPE_SYNGNATHID
        hp["skullWidth"] = 0.44
        hp["skullHeight"] = 0.64
        hp["skullLength"] = 0.52
        hp["muzzleLength"] = 0.72 if sname == "seed" else (0.84 if sname == "larva" else 0.94)
        hp["muzzleWidth"] = 0.16
        hp["muzzleTaper"] = 0.82
        hp["rostrumDepth"] = 0.18
        hp["eyeSize"] = 0.44 if sname in ("seed", "larva") else 0.38
        hp["eyeLaterality"] = 0.86
        hp["eyeForwardness"] = 0.52
        hp["eyeDorsality"] = 0.66
        hp["eyeExposure"] = 0.18
        hp["earSize"] = 0.0
        hp["noseScale"] = 0.02
        st["headEnvelope"] = {"scale": 0.94, "widthScale": 0.76, "heightScale": 0.94, "lengthScale": 1.15}

        st["eyes"]["size"] = hp["eyeSize"]
        st["eyes"]["irisColor"] = rgba(212, 168, 68)
        st["eyes"]["pupilColor"] = rgba(14, 12, 10)
        st["eyes"]["scleraColor"] = rgba(232, 206, 134)
        st["eyes"]["pupilShape"] = 0

        st["tails"] = [make_tail(
            arch=3,  # TAIL_ARCHETYPE_PREHENSILE
            length=2.55, base_w=0.14, base_h=0.15, tip_w=0.025, tip_h=0.025,
            segs=12, taper=1.18, curv=0.92 * curl_m, root_match=0.92,
            caudal_type=c_type, caudal_span=juv_caudal, caudal_chord=juv_caudal * 0.85,
            caudal_thick=0.010, caudal_col=fin_tr
        )]
        st["tailCount"] = 1

        st["fins"] = [
            # 0: Delicate rayed dorsal fan fin on back (17 rays)
            make_fin(1, 2, 0, 0.42, 0.30, 0.006, 0.011, sweep=0.04, leading=0.32, trailing=-0.16,
                     tip_shape=0.88, ray_count=17, base_col=amber, tip_col=fin_tr),
            # 1-2: Small delicate cheek/pectoral fins
            make_fin(0, 0, 1, 0.14, 0.18, 0.005, 0.009, sweep=0.18, leading=0.26, trailing=-0.10,
                     tip_shape=0.85, dihedral=-0.16, ray_count=14, base_col=amber, tip_col=fin_tr),
            make_fin(0, 0, 2, 0.14, 0.18, 0.005, 0.009, sweep=0.18, leading=0.26, trailing=-0.10,
                     tip_shape=0.85, dihedral=-0.16, ray_count=14, base_col=amber, tip_col=fin_tr),
        ]
        st["finCount"] = len(st["fins"])

        st["dorsalColor"] = gold
        st["ventralColor"] = cream
        st["unpigmentedVentralColor"] = cream
        st["development"] = {
            "appendages": 1.0, "cephalic": 1.0, "pigmentation": col_m,
            "integument": 1.0, "colorMaturity": col_m,
        }
        sh_layers = [
            make_pigment_layer(2, rgba(96, 52, 16), scale=7.6, sharpness=0.74, strength=0.60 * col_m, seed=10231),
            make_pigment_layer(2, rgba(244, 232, 198), scale=13.5, sharpness=0.86, strength=0.45 * col_m, seed=10237),
        ]
        clean_surface(st["surface"], 0, gold, amber, cream, seed=1023,
                      coverage=0.85, roughness=0.38, sheen=0.14, scale_size=0.015, scale_relief=0.0,
                      layers=sh_layers)
        st["surface"]["pigment"]["ventralLightening"] = 0.35
        st["surface"]["pigment"]["dorsalDarkening"] = 0.24
    return doc


# =============================================================================
# 4. GREEN SEA TURTLE (Chelonia mydas) — ID 1024
# =============================================================================
def build_sea_turtle(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r["id"] = 1024
    r["name"] = "Tortuga Marina"
    r["gait"] = 15  # CREATURE_GAIT_TURTLE_SWIM
    r["offspringSeed"] = 0
    r["offspringVersion"] = 0

    tmpl_limb = base["recipe"]["adult"]["limbs"][0]

    r["bodyPlan"] = {
        "root": {"moduleInstanceId": 1, "localNodeId": 2},
        "modules": [
            mod_inst(1, 0, 0, 0, 0),   # AXIAL (SPRAWLING_TETRAPOD)
            mod_inst(2, 1, 1, 1, 0),   # HEAD -> CERVICAL
            mod_inst(3, 2, 1, 2, 0),   # LIMB 0: left foreflipper
            mod_inst(4, 2, 1, 3, 1),   # LIMB 1: right foreflipper
            mod_inst(5, 2, 1, 4, 2),   # LIMB 2: left hindflipper
            mod_inst(6, 2, 1, 5, 3),   # LIMB 3: right hindflipper
            mod_inst(7, 3, 1, 6, 0),   # TAIL -> CAUDAL
        ],
        "moduleCount": 7,
    }

    stage_cfg = [
        # (name, scale, flip_m, shell_m, col_m, cara_base, cara_dark, cara_amber, suture, plas_base, plas_seam, skin_base, mottle, radial)
        ("seed",     0.28, 1.18, 0.85, 0.72,
         rgba(44, 46, 36), rgba(30, 26, 20), rgba(98, 76, 48), rgba(28, 24, 18),
         rgba(224, 215, 184), rgba(172, 162, 134), rgba(50, 54, 40), 0.30, 0.20),
        ("larva",    0.46, 1.10, 0.90, 0.82,
         rgba(60, 56, 38), rgba(44, 34, 23), rgba(136, 104, 60), rgba(40, 34, 25),
         rgba(220, 209, 174), rgba(168, 156, 124), rgba(60, 62, 44), 0.48, 0.42),
        ("juvenile", 0.74, 1.04, 0.96, 0.93,
         rgba(88, 74, 44), rgba(60, 44, 28), rgba(182, 140, 80), rgba(52, 44, 31),
         rgba(218, 206, 166), rgba(164, 152, 118), rgba(70, 72, 50), 0.68, 0.66),
        ("adult",    1.00, 1.00, 1.00, 1.00,
         rgba(78, 74, 46), rgba(58, 44, 29), rgba(172, 134, 80), rgba(50, 44, 32),
         rgba(218, 206, 166), rgba(162, 150, 116), rgba(68, 72, 50), 0.74, 0.58),
    ]

    for (sname, scale, flip_m, shell_m, col_m,
         cara_base, cara_dark, cara_amber, suture_col,
         plas_base, plas_seam, skin_ol, mottle_str, radial_str) in stage_cfg:
        st = r[sname]
        clear_optional_arrays(st)
        ax = st["axial"]
        reset_axial_extras(ax)
        ax["archetype"] = 0  # AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD
        ax["totalScale"] = scale
        ax["withersHeight"] = 1.25
        ax["trunkLength"] = 2.65
        ax["neckLength"] = 0.46
        ax["neckWidth"] = 0.38
        ax["shoulderWidth"] = 1.32
        ax["thoraxWidth"] = 1.56
        ax["thoraxHeight"] = 0.50
        ax["abdomenWidth"] = 1.44
        ax["abdomenHeight"] = 0.40
        ax["pelvicWidth"] = 0.96
        ax["pelvicHeight"] = 0.30
        ax["bodyFlattening"] = 0.65
        ax["limbAttachmentLateral"] = 0.96

        hp = st["head"]
        hp["archetype"] = 17  # HEAD_ARCHETYPE_TESTUDINE
        hp["skullWidth"] = 0.56
        hp["skullHeight"] = 0.64
        hp["skullLength"] = 0.58
        hp["muzzleLength"] = 0.32
        hp["muzzleWidth"] = 0.52
        hp["muzzleTaper"] = 0.58
        hp["rostrumDepth"] = 0.56
        hp["eyeSize"] = 0.40 if sname in ("seed", "larva") else 0.34
        hp["eyeLaterality"] = 0.80
        hp["eyeForwardness"] = 0.52
        hp["eyeDorsality"] = 0.56
        hp["eyeExposure"] = 0.14
        hp["earSize"] = 0.0
        hp["noseScale"] = 0.04
        st["headEnvelope"] = {"scale": 0.66, "widthScale": 0.84, "heightScale": 0.92, "lengthScale": 0.92}

        st["eyes"]["size"] = hp["eyeSize"]
        st["eyes"]["irisColor"] = rgba(76, 62, 42)
        st["eyes"]["pupilColor"] = rgba(12, 10, 8)
        st["eyes"]["scleraColor"] = rgba(186, 178, 152)
        st["eyes"]["pupilShape"] = 0

        # Aletas marinas con mosaico poligonal reptiliano explícito (flipperSurface=2)
        st["limbs"] = [
            blank_marine_flipper(tmpl_limb, 0, 0, 2.15 * flip_m, 0.085, aspect=3.4, sweep=0.52, camber=0.10, sprawl=0.88, claw_scale=0.85,
                                 flipper_surface=2, scale_size=1.0, scale_irreg=0.72, border_contrast=0.44, roughness=0.52),
            blank_marine_flipper(tmpl_limb, 0, 1, 2.15 * flip_m, 0.085, aspect=3.4, sweep=0.52, camber=0.10, sprawl=0.88, claw_scale=0.85,
                                 flipper_surface=2, scale_size=1.0, scale_irreg=0.72, border_contrast=0.44, roughness=0.52),
            blank_marine_flipper(tmpl_limb, 1, 0, 1.06 * flip_m, 0.062, aspect=2.1, sweep=0.68, camber=0.06, sprawl=0.85, claw_scale=0.55,
                                 flipper_surface=2, scale_size=1.0, scale_irreg=0.72, border_contrast=0.44, roughness=0.52),
            blank_marine_flipper(tmpl_limb, 1, 1, 1.06 * flip_m, 0.062, aspect=2.1, sweep=0.68, camber=0.06, sprawl=0.85, claw_scale=0.55,
                                 flipper_surface=2, scale_size=1.0, scale_irreg=0.72, border_contrast=0.44, roughness=0.52),
        ]
        st["limbCount"] = 4

        st["tails"] = [make_tail(
            arch=0, length=0.56, base_w=0.16, base_h=0.14, tip_w=0.04, tip_h=0.04,
            segs=3, taper=1.25, root_match=0.85, caudal_type=0
        )]
        st["tailCount"] = 1

        st["shell"] = {
            "development": 1.0,
            "length": 3.25 * shell_m,
            "width": 2.68 * shell_m,
            "height": 0.72 * shell_m,
            "dorsalConvexity": 0.56,
            "heartTaper": 0.42,
            "plastronWidth": 1.98 * shell_m,
            "plastronLength": 2.48 * shell_m,
            "plastronDepth": 0.36 * shell_m,
            "bridgeWidth": 1.26 * shell_m,
            "shellThickness": 0.08,
            "costalScutePairs": 4,  # Chelonia mydas: exactamente 4 pares de escudos costales
            "vertebralScutes": 5,
            "marginalScutes": 11,
            "scuteRelief": 0.46,
            "cervicalNotch": 0.14,
            "limbClearance": 0.28,
            "carapaceColor": cara_base,
            "scuteBorderColor": suture_col,
            "plastronColor": plas_base,
            "carapaceDarkColor": cara_dark,
            "carapaceAmberColor": cara_amber,
            "plastronSeamColor": plas_seam,
            "carapaceMottling": mottle_str,
            "carapaceRadialStrength": radial_str,
            "carapaceRoughness": 0.42,
            "sutureContrast": 0.62,
            "plastronSeamSoftness": 0.80,
            "plastronMottling": 0.18,
            "plastronRoughness": 0.48,
            "patternSeed": 1024,
        }

        st["dorsalColor"] = skin_ol
        st["ventralColor"] = plas_base
        st["unpigmentedVentralColor"] = plas_base
        st["development"] = {
            "appendages": 1.0, "cephalic": 1.0, "pigmentation": col_m,
            "integument": 1.0, "colorMaturity": col_m,
        }
        # Piel cefálica/cervical coherente con placas y aletas: escamas más amplias y suaves,
        # sin patrón diminuto repetitivo de alto contraste.
        tur_layers = [
            make_pigment_layer(1, cara_dark, scale=3.2, sharpness=0.34, strength=0.34 * col_m, seed=10241),
        ]
        clean_surface(st["surface"], 1, skin_ol, cara_dark, rgba(168, 158, 126), seed=1024,
                      coverage=0.30, roughness=0.52, sheen=0.08, scale_size=0.062, scale_relief=0.011,
                      layers=tur_layers)
        st["surface"]["integument"]["scales"]["irregularity"] = 0.74
        st["surface"]["integument"]["scales"]["edgeDepth"] = 0.13
        st["surface"]["integument"]["scales"]["edgeWidth"] = 0.08
        st["surface"]["integument"]["scales"]["microColorVariation"] = 0.14
        st["surface"]["pigment"]["dorsalDarkening"] = 0.24
        st["surface"]["pigment"]["ventralLightening"] = 0.72
    return doc


# =============================================================================
# 5. COMMON BOTTLENOSE DOLPHIN (Tursiops truncatus) — ID 1025
# =============================================================================
def build_dolphin(base):
    doc = copy.deepcopy(base)
    r = doc["recipe"]
    r["id"] = 1025
    r["name"] = "Delfin Mular"
    r["gait"] = 16  # CREATURE_GAIT_CETACEAN_SWIM
    r["offspringSeed"] = 0
    r["offspringVersion"] = 0

    tmpl_limb = base["recipe"]["adult"]["limbs"][0]

    r["bodyPlan"] = {
        "root": {"moduleInstanceId": 1, "localNodeId": 2},
        "modules": [
            mod_inst(1, 0, 0, 0, 0),   # AXIAL (AQUATIC_FUSIFORM)
            mod_inst(2, 1, 1, 1, 0),   # HEAD -> CERVICAL (CETACEAN)
            mod_inst(3, 2, 1, 2, 0),   # LIMB 0: left pectoral flipper
            mod_inst(4, 2, 1, 3, 1),   # LIMB 1: right pectoral flipper
            mod_inst(5, 6, 1, 7, 0),   # FIN 0: curved dorsal fin (flexible tissue)
            mod_inst(6, 3, 1, 6, 0),   # TAIL -> CAUDAL (horizontal flukes)
        ],
        "moduleCount": 6,
    }

    # Calf proportions: shorter rostrum, rounder forehead/melon, smaller dorsal fin
    stage_cfg = [
        ("seed",     0.38, 0.56, 0.72, 0.80),
        ("larva",    0.56, 0.72, 0.84, 0.88),
        ("juvenile", 0.78, 0.88, 0.94, 0.96),
        ("adult",    1.00, 1.00, 1.00, 1.00),
    ]
    gray_d = rgba(102, 114, 126)
    gray_m = rgba(144, 154, 164)
    belly = rgba(234, 228, 228)
    fin_col = rgba(86, 98, 110)

    for sname, scale, rost_m, fin_m, col_m in stage_cfg:
        st = r[sname]
        clear_optional_arrays(st)
        ax = st["axial"]
        reset_axial_extras(ax)
        ax["archetype"] = 9  # AXIAL_ARCHETYPE_AQUATIC_FUSIFORM
        ax["totalScale"] = scale
        ax["withersHeight"] = 1.65
        ax["trunkLength"] = 4.25
        ax["neckLength"] = 0.34
        ax["neckWidth"] = 1.14
        ax["shoulderWidth"] = 1.16
        ax["thoraxWidth"] = 1.16
        ax["thoraxHeight"] = 1.24
        ax["abdomenWidth"] = 0.86
        ax["abdomenHeight"] = 0.96
        ax["pelvicWidth"] = 0.24
        ax["pelvicHeight"] = 0.30
        ax["bodyFlattening"] = 0.96
        ax["limbAttachmentLateral"] = 0.84

        hp = st["head"]
        hp["archetype"] = 18  # HEAD_ARCHETYPE_CETACEAN
        hp["skullWidth"] = 0.68
        hp["skullHeight"] = 0.82
        hp["skullLength"] = 0.64
        hp["muzzleLength"] = 0.62 * rost_m
        hp["muzzleWidth"] = 0.36
        hp["muzzleTaper"] = 0.64
        hp["rostrumDepth"] = 0.38
        hp["eyeSize"] = 0.28 if sname in ("seed", "larva") else 0.23
        hp["eyeLaterality"] = 0.86
        hp["eyeForwardness"] = 0.42
        hp["eyeDorsality"] = 0.34
        hp["eyeExposure"] = 0.10
        hp["earSize"] = 0.0
        hp["noseScale"] = 0.02
        st["headEnvelope"] = {"scale": 0.80, "widthScale": 0.84, "heightScale": 0.88, "lengthScale": 0.96}

        st["eyes"]["size"] = hp["eyeSize"]
        st["eyes"]["irisColor"] = rgba(46, 38, 32)
        st["eyes"]["pupilColor"] = rgba(10, 10, 12)
        st["eyes"]["scleraColor"] = rgba(168, 174, 180)
        st["eyes"]["pupilShape"] = 0

        st["limbs"] = [
            blank_marine_flipper(tmpl_limb, 0, 0, 1.32 * fin_m, 0.085, aspect=2.65, sweep=0.56, camber=0.06, sprawl=0.72, claw_scale=0.0, flipper_surface=1),
            blank_marine_flipper(tmpl_limb, 0, 1, 1.32 * fin_m, 0.085, aspect=2.65, sweep=0.56, camber=0.06, sprawl=0.72, claw_scale=0.0, flipper_surface=1),
        ]
        st["limbCount"] = 2

        st["fins"] = [
            # Curved falcate dorsal fin (flexible fibrous tissue, no fin rays)
            make_fin(4, 2, 0, 0.78, 0.68 * fin_m, 0.052, 0.095, sweep=0.72, leading=0.34, trailing=0.56,
                     tip_shape=0.26, ray_count=0, base_col=fin_col, tip_col=gray_d),
        ]
        st["finCount"] = 1

        st["tails"] = [make_tail(
            arch=0, length=1.12, base_w=0.16, base_h=0.22, tip_w=0.024, tip_h=0.028,
            segs=5, taper=1.35, root_match=0.95,
            caudal_type=3,  # CAUDAL_FIN_HORIZONTAL_FLUKES
            caudal_span=1.78 * fin_m, caudal_chord=0.58 * fin_m,
            caudal_ratio=1.0, caudal_notch=0.68, caudal_sweep=0.46,
            caudal_thick=0.044, caudal_col=fin_col
        )]
        st["tailCount"] = 1

        st["dorsalColor"] = gray_d
        st["ventralColor"] = belly
        st["unpigmentedVentralColor"] = belly
        st["development"] = {
            "appendages": 1.0, "cephalic": 1.0, "pigmentation": col_m,
            "integument": 1.0, "colorMaturity": col_m,
        }
        clean_surface(st["surface"], 0, gray_d, gray_m, belly, seed=1025,
                      coverage=0.78, roughness=0.16, sheen=0.36, scale_size=0.02, scale_relief=0.0)
        st["surface"]["pigment"]["ventralLightening"] = 0.92
        st["surface"]["pigment"]["dorsalDarkening"] = 0.34
        st["surface"]["pigment"]["patternStrength"] = 0.14
    return doc


def main():
    base = json.loads(BASE_PATH.read_text(encoding="utf-8"))
    outputs = [
        ("atun_rojo.creature.json", build_tuna(base)),
        ("tiburon_martillo.creature.json", build_hammerhead(base)),
        ("caballito_mar.creature.json", build_seahorse(base)),
        ("tortuga_marina.creature.json", build_sea_turtle(base)),
        ("delfin_mular.creature.json", build_dolphin(base)),
    ]
    for fname, data in outputs:
        out_path = ROOT / "recipes" / fname
        out_path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Generada {out_path.relative_to(ROOT)} (ID {data['recipe']['id']} - {data['recipe']['name']})")


if __name__ == "__main__":
    main()
