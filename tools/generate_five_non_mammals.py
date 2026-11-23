#!/usr/bin/env python3
"""Genera las cinco recetas externas no-mamíferas (IDs 1016..1020) en formato v1 compatible."""
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


def blank_limb(template, arch, role, side):
    l = copy.deepcopy(template)
    l["archetype"] = arch
    l["anatomyProfile"] = 0
    l["role"] = role
    l["side"] = side
    l["length"] = 0.6
    l["thickness"] = 0.04
    l["sprawl"] = 0.1
    l["development"] = 1.0
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
    l["clawScale"] = 0.0
    l["webbingCoverage"] = 0.0
    l["webbingThickness"] = 0.16
    l["webbingEdgeCurve"] = 0.15
    l["webbingFold"] = 0.0
    l["featherCount"] = 0
    l["wingAspect"] = 0.0
    l["wingPointedness"] = 0.0
    l["finSweep"] = 0.0
    l["finCamber"] = 0.0
    return l


def blank_flexible(length, base_r, tip_r, profile, pitch=0.0, bend=0.0, yaw=0.0, coil=0.0,
                   sucker_rows=0, sucker_count=0, sucker_scale=0.0, aspect=1.0, taper=1.15):
    return {
        "length": length,
        "baseRadius": base_r,
        "tipRadius": tip_r,
        "aspect": aspect,
        "taper": taper,
        "initialPitch": pitch,
        "restBend": bend,
        "terminal": 0,
        "profile": profile,
        "yawAngle": yaw,
        "coilBias": coil,
        "suckerRows": sucker_rows,
        "suckerCount": sucker_count,
        "suckerScale": sucker_scale,
    }


def blank_stage(base_stage):
    s = copy.deepcopy(base_stage)
    s.pop("headHybrid", None)
    s["limbs"] = []
    s["limbCount"] = 0
    s["tails"] = []
    s["tailCount"] = 0
    s["ornaments"] = []
    s["ornamentCount"] = 0
    s["flexibleAppendages"] = []
    s["flexibleAppendageCount"] = 0
    s["membranes"] = []
    s["membraneCount"] = 0
    s["repeatedAppendages"] = []
    s["repeatedAppendageCount"] = 0
    surf = s["surface"]
    surf["tufts"] = []
    surf["tuftCount"] = 0
    surf["longHair"] = []
    surf["longHairCount"] = 0
    surf["wrinkleStrength"] = 0.0
    surf["wrinkleScale"] = 1.0
    surf["wrinkleDirectionality"] = 0.0
    surf["wrinkleRegionMask"] = 0
    surf["translucency"] = 0.0
    surf["iridescence"] = 0.0
    surf["subsurfaceGlow"] = 0.0
    surf["inheritedBlend"] = False
    surf["transition"]["active"] = False
    surf["pigment"]["layers"] = []
    surf["pigment"]["layerCount"] = 0
    surf["regionalAlbedo"] = [rgba(0, 0, 0, 0) for _ in range(11)]
    surf["integument"]["plates"]["armorScale"] = 0.0
    for prof in s["axial"]["profile"]:
        prof["widthScale"] = 1.0
        prof["heightScale"] = 1.0
        prof["longitudinalOffset"] = 0.0
        prof["verticalOffset"] = 0.0
    ax = s["axial"]
    ax["contourProfile"] = 0
    ax["cervicalSegments"] = 0
    ax["cervicalPitch"] = 0.0
    ax["spineFlexion"] = 0.14
    ax["radialSymmetryOrder"] = 0
    ax["bellConcavity"] = 0.0
    ax["bellScallopAmplitude"] = 0.0
    ax["abdomenSegments"] = 0
    ax["discSweep"] = 0.0
    ax["mantleElongation"] = 0.0
    return s


def pigment_layer(pattern, color, strength, scale, sharpness, seed, region_mask=0,
                  direction=(0, 1, 0), center=(0, 0, 0), radii=(0, 0, 0), softness=0.25):
    return {
        "pattern": pattern,
        "color": color,
        "strength": strength,
        "scale": scale,
        "sharpness": sharpness,
        "direction": {"x": direction[0], "y": direction[1], "z": direction[2]},
        "seed": seed,
        "regionMask": region_mask,
        "patternRotation": 0.0,
        "domain": {
            "anchorNode": 0,
            "center": {"x": center[0], "y": center[1], "z": center[2]},
            "radii": {"x": radii[0], "y": radii[1], "z": radii[2]},
            "softness": softness,
            "irregularity": 0.12,
            "materialStrength": 0.0,
            "furCoverage": 0.0,
            "roughness": 0.5,
        },
    }


# =============================================================================
# 1. HALCÓN PEREGRINO (Falco peregrinus) — ID 1016
# =============================================================================
def build_falcon(base):
    limb_tpl = base["recipe"]["adult"]["limbs"][0]
    tail_tpl = base["recipe"]["adult"]["tails"][0]

    def make_stage(scale, dev_app, dev_ceph, maturity):
        s = blank_stage(base["recipe"]["adult"])
        ax = s["axial"]
        ax["archetype"] = 3  # AXIAL_ARCHETYPE_AVIAN
        ax["totalScale"] = scale
        ax["withersHeight"] = 0.72
        ax["trunkLength"] = 0.64
        ax["neckLength"] = 0.13
        ax["neckWidth"] = 0.28
        ax["shoulderWidth"] = 0.38
        ax["thoraxWidth"] = 0.44
        ax["thoraxHeight"] = 0.44
        ax["abdomenWidth"] = 0.30
        ax["abdomenHeight"] = 0.32
        ax["pelvicWidth"] = 0.22
        ax["pelvicHeight"] = 0.22
        ax["brisketDepth"] = 0.22
        ax["limbAttachmentLateral"] = 0.86
        ax["bodyFlattening"] = 0.90

        # Cabeza de halcón: cráneo compacto redondeado, pico corto ganchudo y grandes ojos oscuros
        hp = s["head"]
        hp["archetype"] = 2  # HEAD_ARCHETYPE_AVIAN
        hp["skullWidth"] = 0.58
        hp["skullHeight"] = 0.72
        hp["skullLength"] = 0.52
        hp["beakLength"] = 0.32 + 0.08 * maturity
        hp["beakDepth"] = 0.56
        hp["beakTaper"] = 0.92
        hp["beakCurvature"] = 0.95
        hp["eyeSize"] = 0.74
        hp["eyeLaterality"] = 0.52
        hp["eyeForwardness"] = 0.56
        hp["eyeDorsality"] = 0.52
        hp["eyeExposure"] = 0.22
        hp["eyelidCoverage"] = 0.20
        hp["browProminence"] = 0.44
        hp["earSize"] = 0.0
        hp["noseScale"] = 0.12
        hp["nostrilPosition"] = 0.36
        hp["jawLength"] = 0.48
        hp["jawDepth"] = 0.24
        hp["jawStrength"] = 0.46
        hp["cranialForm"] = 0
        hp["muzzleForm"] = 0
        hp["eyeLayout"] = 0
        hp["nasalColor"] = rgba(226, 184, 48)  # Cera amarilla de halcón

        s["headEnvelope"] = {
            "scale": 0.25 + 0.05 * (1.0 - dev_ceph),
            "widthScale": 0.96,
            "heightScale": 1.02,
            "lengthScale": 0.90,
        }
        s["eyes"] = {
            "size": 0.74,
            "protrusion": 0.66,
            "irisScale": 0.88,
            "pupilScale": 0.60,
            "pupilAspect": 1.0,
            "pupilShape": 0,
            "scleraColor": rgba(42, 34, 24),    # Oscuro rapaz con leve anillo orbital
            "irisColor": rgba(26, 18, 12),      # Ojos oscuros profundos de halcón
            "pupilColor": rgba(8, 8, 10),
        }

        # Alas aviares largas y puntiagudas (primarias, secundarias y coberteras)
        for side_idx in (0, 1):
            w = blank_limb(limb_tpl, 2, 4, side_idx)  # LIMB_ARCHETYPE_AVIAN, LIMB_WING
            w["length"] = 1.28 * (0.45 + 0.55 * dev_app)
            w["thickness"] = 0.044
            w["sprawl"] = 0.08  # Extendidas en planeo/vuelo para mostrar rémiges y silueta falcada
            w["development"] = 0.35 + 0.65 * dev_app
            w["proximalScale"] = 0.96
            w["middleScale"] = 1.05
            w["distalScale"] = 1.12
            w["attachmentInset"] = 0.22
            w["featherCount"] = 10
            w["wingAspect"] = 2.45
            w["wingPointedness"] = 0.88
            s["limbs"].append(w)

        # Patas prensiles rapaces con 3 dedos delanteros + 1 hallux trasero y garras curvas
        for side_idx in (0, 1):
            leg = blank_limb(limb_tpl, 2, 1, side_idx)  # LIMB_ARCHETYPE_AVIAN, LIMB_HIND
            leg["length"] = 0.44
            leg["thickness"] = 0.028
            leg["development"] = 0.65 + 0.35 * dev_app
            leg["digitCount"] = 4
            leg["digitPhalanges"] = [3, 3, 3, 2, 0, 0, 0, 0]
            leg["digitLengths"] = [0.85, 1.12, 0.90, 0.78, 1.0, 1.0, 1.0, 1.0]
            leg["digitSpread"] = 0.88
            leg["footScale"] = 1.08
            leg["clawScale"] = 1.95
            s["limbs"].append(leg)
        s["limbCount"] = 4

        # Cola en abanico de rectrices superpuestas (pigostilo óseo corto + largas plumas rectrices)
        tail = copy.deepcopy(tail_tpl)
        tail["archetype"] = 8  # TAIL_ARCHETYPE_FEATHER_FAN
        tail["length"] = 0.26 * (0.45 + 0.55 * dev_app)
        tail["baseWidth"] = 0.085
        tail["baseHeight"] = 0.052
        tail["tipWidth"] = 0.032
        tail["tipHeight"] = 0.020
        tail["taperCurve"] = 1.25
        tail["curvature"] = 0.30
        tail["droop"] = 0.04
        tail["tipLift"] = 0.0
        tail["rootMatchStrength"] = 0.62
        tail["segmentCount"] = 4
        tail["development"] = 0.40 + 0.60 * dev_app
        s["tails"] = [tail]
        s["tailCount"] = 1

        # Plumaje: dorso gris azulado pizarra, pecho barrado pálido, cabeza/bigotera oscura, patas amarillas
        dorsal = rgba(90, 108, 128) if maturity > 0.5 else rgba(126, 108, 92)
        ventral = rgba(236, 228, 212)
        s["dorsalColor"] = dorsal
        s["ventralColor"] = ventral
        s["unpigmentedVentralColor"] = ventral

        surf = s["surface"]
        surf["integument"]["type"] = 3  # INTEGUMENT_FEATHERS
        surf["integument"]["coverage"] = 0.95
        surf["pigment"]["baseColor"] = dorsal
        surf["pigment"]["secondaryColor"] = rgba(52, 62, 76)
        surf["pigment"]["ventralColor"] = ventral
        surf["pigment"]["dorsalDarkening"] = 0.16
        surf["pigment"]["ventralLightening"] = 0.92
        surf["pigment"]["patternStrength"] = 0.32
        surf["pigment"]["patternScale"] = 6.5
        surf["pigment"]["bands"] = 0.85
        surf["pigment"]["seed"] = 1016
        # Capa 0: barrado transversal oscuro en pecho/vientre; Capa 1: capirote/bigotera oscura en cabeza
        surf["pigment"]["layers"] = [
            pigment_layer(2, rgba(56, 52, 48), 0.58 * maturity, 11.0, 0.72, 10161,
                          region_mask=(1 << 4), direction=(0, 1, 0)),
            pigment_layer(0, rgba(32, 36, 44), 0.78 * maturity, 2.0, 0.85, 10162,
                          region_mask=(1 << 1), direction=(0, 1, 0)),
        ]
        surf["pigment"]["layerCount"] = 2
        # Patas y dedos amarillos de halcón peregrino
        surf["regionalAlbedo"][6] = rgba(226, 184, 48, 230)  # SURFACE_REGION_HINDLIMB
        surf["regionalAlbedo"][8] = rgba(228, 186, 46, 240)  # SURFACE_REGION_DIGIT

        s["development"] = {
            "appendages": dev_app,
            "cephalic": dev_ceph,
            "pigmentation": 0.45 + 0.55 * maturity,
            "integument": 0.40 + 0.60 * maturity,
            "colorMaturity": maturity,
        }
        return s

    doc = {
        "version": 1,
        "recipe": {
            "id": 1016,
            "name": "Halcon Peregrino",
            "bodyPlan": {
                "root": {"moduleInstanceId": 1, "localNodeId": 3},
                "modules": [
                    mod_inst(1, 0, 0, 0, 0),    # AXIAL
                    mod_inst(2, 1, 1, 1, 0),    # HEAD -> AXIAL_SLOT_CERVICAL
                    mod_inst(10, 2, 1, 2, 0),   # WING LEFT -> AXIAL_SLOT_PECTORAL_LEFT
                    mod_inst(11, 2, 1, 3, 1),   # WING RIGHT -> AXIAL_SLOT_PECTORAL_RIGHT
                    mod_inst(12, 2, 1, 4, 2),   # LEG LEFT -> AXIAL_SLOT_PELVIC_LEFT
                    mod_inst(13, 2, 1, 5, 3),   # LEG RIGHT -> AXIAL_SLOT_PELVIC_RIGHT
                    mod_inst(20, 3, 1, 6, 0),   # TAIL FAN -> AXIAL_SLOT_CAUDAL
                ],
                "moduleCount": 7,
            },
            "seed": make_stage(0.45, 0.35, 0.72, 0.15),
            "larva": make_stage(0.68, 0.58, 0.84, 0.42),
            "juvenile": make_stage(0.88, 0.85, 0.94, 0.74),
            "adult": make_stage(1.00, 1.00, 1.00, 1.00),
            "gait": 7,  # CREATURE_GAIT_AVIAN_FLIGHT
            "offspringSeed": 0,
            "offspringVersion": 0,
        },
    }
    return doc


# =============================================================================
# 2. MANTARRAYA GIGANTE OCEÁNICA (Mobula birostris) — ID 1017
# =============================================================================
def build_manta(base):
    limb_tpl = base["recipe"]["adult"]["limbs"][0]
    tail_tpl = base["recipe"]["adult"]["tails"][0]

    def make_stage(scale, dev_app, dev_ceph, maturity):
        s = blank_stage(base["recipe"]["adult"])
        ax = s["axial"]
        ax["archetype"] = 5  # AXIAL_ARCHETYPE_DISC_RAY
        ax["totalScale"] = scale
        ax["withersHeight"] = 0.65
        ax["trunkLength"] = 1.42
        ax["neckLength"] = 0.18
        ax["neckWidth"] = 0.68
        ax["shoulderWidth"] = 0.92
        ax["thoraxWidth"] = 1.06
        ax["thoraxHeight"] = 0.52
        ax["abdomenWidth"] = 0.64
        ax["abdomenHeight"] = 0.36
        ax["pelvicWidth"] = 0.32
        ax["pelvicHeight"] = 0.24
        ax["bodyFlattening"] = 0.38
        ax["discSweep"] = 0.36
        ax["limbAttachmentLateral"] = 0.80

        # Cabeza batoidea ancha y aplanada con boca terminal frontal y ojos laterales
        hp = s["head"]
        hp["archetype"] = 11  # HEAD_ARCHETYPE_BATOID
        hp["skullWidth"] = 0.86
        hp["skullHeight"] = 0.30
        hp["skullLength"] = 0.44
        hp["muzzleLength"] = 0.14
        hp["muzzleWidth"] = 0.88
        hp["muzzleTaper"] = 0.12
        hp["rostrumDepth"] = 0.34
        hp["eyeSize"] = 0.28
        hp["eyeLaterality"] = 0.92
        hp["eyeForwardness"] = 0.40
        hp["eyeDorsality"] = 0.56
        hp["eyeExposure"] = 0.16
        hp["earSize"] = 0.0
        hp["jawLength"] = 0.42
        hp["jawDepth"] = 0.22
        hp["jawStrength"] = 0.32
        hp["muzzleForm"] = 1  # BLUNT

        s["headEnvelope"] = {"scale": 0.42, "widthScale": 1.18, "heightScale": 0.58, "lengthScale": 0.66}
        s["eyes"] = {
            "size": 0.28, "protrusion": 0.48, "irisScale": 0.78, "pupilScale": 0.46,
            "pupilAspect": 1.0, "pupilShape": 0,
            "scleraColor": rgba(42, 48, 56), "irisColor": rgba(26, 32, 38), "pupilColor": rgba(8, 10, 12),
        }

        # Aletas pectorales gigantes en diamante continuo (LIMB_ARCHETYPE_FLIPPER = 4)
        for side_idx in (0, 1):
            fin = blank_limb(limb_tpl, 4, 0, side_idx)  # LIMB_FORE -> pectoral fin
            fin["length"] = 1.88 * (0.72 + 0.28 * dev_app)
            fin["thickness"] = 0.095
            fin["development"] = 0.55 + 0.45 * dev_app
            fin["finSweep"] = 0.34
            fin["finCamber"] = 0.16
            fin["wingAspect"] = 2.4
            s["limbs"].append(fin)

        # Pequeñas aletas pélvicas posteriores
        for side_idx in (0, 1):
            pel = blank_limb(limb_tpl, 4, 1, side_idx)  # LIMB_HIND -> pelvic fin
            pel["length"] = 0.26
            pel["thickness"] = 0.032
            pel["development"] = 0.60 + 0.40 * dev_app
            pel["finSweep"] = 0.52
            pel["finCamber"] = 0.06
            s["limbs"].append(pel)
        s["limbCount"] = 4

        # Lóbulos cefálicos pareados anchos en pala proyectados hacia adelante a ambos lados de la boca
        lobe_l = blank_flexible(0.56 * (0.65 + 0.35 * dev_app), 0.095, 0.036, 2,
                                pitch=-0.02, bend=-0.06, yaw=-0.12, coil=-0.52, aspect=0.60, taper=0.85)
        lobe_r = blank_flexible(0.56 * (0.65 + 0.35 * dev_app), 0.095, 0.036, 2,
                                pitch=-0.02, bend=-0.06, yaw=0.12, coil=0.52, aspect=0.60, taper=0.85)
        s["flexibleAppendages"] = [lobe_l, lobe_r]
        s["flexibleAppendageCount"] = 2

        # Pequeña aleta dorsal en la base caudal
        s["ornaments"] = [{
            "archetype": 4,  # ORNAMENT_ARCHETYPE_CREST
            "length": 0.18, "baseRadius": 0.11, "tipRadius": 0.020,
            "curvature": -0.15, "development": 0.60 + 0.40 * dev_app,
            "growthDirection": {"x": 0.0, "y": 1.0, "z": -0.35},
            "lateralSweep": 0.0,
        }]
        s["ornamentCount"] = 1

        # Cola larga y muy fina tipo látigo (sin aguijón venenoso)
        tail = copy.deepcopy(tail_tpl)
        tail["archetype"] = 1  # TAIL_ARCHETYPE_WHIP
        tail["length"] = 1.62 * (0.68 + 0.32 * dev_app)
        tail["baseWidth"] = 0.052
        tail["baseHeight"] = 0.044
        tail["tipWidth"] = 0.008
        tail["tipHeight"] = 0.008
        tail["taperCurve"] = 1.65
        tail["curvature"] = 0.06
        tail["droop"] = 0.04
        tail["tipLift"] = 0.0
        tail["rootMatchStrength"] = 0.45
        tail["segmentCount"] = 10
        tail["development"] = 0.60 + 0.40 * dev_app
        s["tails"] = [tail]
        s["tailCount"] = 1

        dorsal = rgba(34, 42, 52)
        shoulder_mark = rgba(190, 200, 208)
        ventral = rgba(236, 238, 236)
        s["dorsalColor"] = dorsal
        s["ventralColor"] = ventral
        s["unpigmentedVentralColor"] = ventral

        surf = s["surface"]
        surf["integument"]["type"] = 0  # INTEGUMENT_SMOOTH_SKIN
        surf["integument"]["coverage"] = 1.0
        surf["pigment"]["baseColor"] = dorsal
        surf["pigment"]["secondaryColor"] = shoulder_mark
        surf["pigment"]["ventralColor"] = ventral
        surf["pigment"]["dorsalDarkening"] = 0.10
        surf["pigment"]["ventralLightening"] = 0.96
        surf["pigment"]["patternStrength"] = 0.18
        surf["pigment"]["patternScale"] = 2.6
        surf["pigment"]["bands"] = 0.0
        surf["pigment"]["seed"] = 1017
        # Motas oscuras ventrales características de Mobula birostris
        surf["pigment"]["layers"] = [
            pigment_layer(1, rgba(64, 72, 82), 0.34 * maturity, 4.8, 0.65, 10171,
                          region_mask=(1 << 4), direction=(0, 1, 0)),
        ]
        surf["pigment"]["layerCount"] = 1

        s["development"] = {
            "appendages": dev_app, "cephalic": dev_ceph,
            "pigmentation": 0.55 + 0.45 * maturity,
            "integument": 1.0, "colorMaturity": maturity,
        }
        return s

    return {
        "version": 1,
        "recipe": {
            "id": 1017,
            "name": "Mantarraya Gigante",
            "bodyPlan": {
                "root": {"moduleInstanceId": 1, "localNodeId": 3},
                "modules": [
                    mod_inst(1, 0, 0, 0, 0),    # AXIAL DISC RAY
                    mod_inst(2, 1, 1, 1, 0),    # HEAD BATOID
                    mod_inst(10, 2, 1, 2, 0),   # PECTORAL FIN L
                    mod_inst(11, 2, 1, 3, 1),   # PECTORAL FIN R
                    mod_inst(12, 2, 1, 4, 2),   # PELVIC FIN L
                    mod_inst(13, 2, 1, 5, 3),   # PELVIC FIN R
                    mod_inst(20, 3, 1, 6, 0),   # WHIP TAIL
                    mod_inst(25, 4, 1, 7, 0),   # SMALL DORSAL FIN
                    mod_inst(30, 5, 1, 9, 0),   # CEPHALIC LOBE L
                    mod_inst(31, 5, 1, 10, 1),  # CEPHALIC LOBE R
                ],
                "moduleCount": 10,
            },
            "seed": make_stage(0.42, 0.55, 0.80, 0.25),
            "larva": make_stage(0.65, 0.74, 0.88, 0.52),
            "juvenile": make_stage(0.86, 0.90, 0.96, 0.80),
            "adult": make_stage(1.00, 1.00, 1.00, 1.00),
            "gait": 8,  # CREATURE_GAIT_RAY_SWIM
            "offspringSeed": 0,
            "offspringVersion": 0,
        },
    }


# =============================================================================
# 3. PULPO GIGANTE DEL PACÍFICO (Enteroctopus dofleini) — ID 1018
# =============================================================================
def build_octopus(base):
    def make_stage(scale, dev_app, dev_ceph, maturity):
        s = blank_stage(base["recipe"]["adult"])
        ax = s["axial"]
        ax["archetype"] = 6  # AXIAL_ARCHETYPE_CEPHALOPOD
        ax["totalScale"] = scale
        ax["withersHeight"] = 0.56
        ax["trunkLength"] = 1.05
        ax["neckLength"] = 0.16
        ax["neckWidth"] = 0.44
        ax["shoulderWidth"] = 0.46
        ax["thoraxWidth"] = 0.68
        ax["thoraxHeight"] = 0.64
        ax["abdomenWidth"] = 0.76
        ax["abdomenHeight"] = 0.70
        ax["pelvicWidth"] = 0.52
        ax["pelvicHeight"] = 0.48
        ax["radialSymmetryOrder"] = 8
        ax["mantleElongation"] = 1.08

        # Cabeza muscular corta con dos ojos laterales prominentes (pupila horizontal) sin hocico
        hp = s["head"]
        hp["archetype"] = 12  # HEAD_ARCHETYPE_CEPHALOPOD
        hp["skullWidth"] = 0.72
        hp["skullHeight"] = 0.64
        hp["skullLength"] = 0.56
        hp["muzzleLength"] = 0.10
        hp["muzzleWidth"] = 0.62
        hp["eyeSize"] = 0.66
        hp["eyeLaterality"] = 0.82
        hp["eyeForwardness"] = 0.44
        hp["eyeDorsality"] = 0.74
        hp["eyeExposure"] = 0.22
        hp["browProminence"] = 0.42
        hp["earSize"] = 0.0
        hp["muzzleForm"] = 1
        hp["eyeLayout"] = 2

        s["headEnvelope"] = {"scale": 0.30, "widthScale": 1.02, "heightScale": 0.90, "lengthScale": 0.82}
        s["eyes"] = {
            "size": 0.66, "protrusion": 0.76, "irisScale": 0.82, "pupilScale": 0.42,
            "pupilAspect": 0.42, "pupilShape": 1,  # PUPIL_HORIZONTAL (típica de pulpo)
            "scleraColor": rgba(196, 118, 76), "irisColor": rgba(218, 174, 108), "pupilColor": rgba(14, 10, 10),
        }

        # Ocho brazos hidrostáticos independientes alrededor de la corona circumoral (slots 24..31)
        # + 1 sifón tubular lateroventral (slot 11)
        arm_specs = [
            # (len_factor, pitch, rest_bend, yaw, coil)
            (1.06, -0.34, 0.38, -0.06,  0.65),  # L1 anterior-dorsal
            (1.02, -0.38, 0.44,  0.04,  0.82),  # L2 lateral-anterior
            (0.96, -0.40, 0.48,  0.08,  0.96),  # L3 lateral-posterior
            (0.90, -0.36, 0.42,  0.10, -0.78),  # L4 posterior-izquierdo
            (0.90, -0.36, 0.42, -0.10,  0.78),  # R4 posterior-derecho
            (0.96, -0.40, 0.48, -0.08, -0.96),  # R3 lateral-posterior
            (1.02, -0.38, 0.44, -0.04, -0.82),  # R2 lateral-anterior
            (1.06, -0.34, 0.38,  0.06, -0.65),  # R1 anterior-dorsal
        ]
        for lf, pitch, bend, yaw, coil in arm_specs:
            arm_len = 1.78 * lf * (0.42 + 0.58 * dev_app)
            s["flexibleAppendages"].append(blank_flexible(
                length=arm_len, base_r=0.118, tip_r=0.015, profile=1,  # FLEXIBLE_PROFILE_MUSCULAR_ARM
                pitch=pitch, bend=bend, yaw=yaw, coil=coil * (0.45 + 0.55 * dev_app),
                sucker_rows=2, sucker_count=18, sucker_scale=0.72, aspect=0.92, taper=1.12,
            ))
        # Sifón direccional bien visible bajo el collar manto-cabeza
        s["flexibleAppendages"].append(blank_flexible(
            length=0.36, base_r=0.076, tip_r=0.044, profile=3,  # FLEXIBLE_PROFILE_SIPHON
            pitch=0.06, bend=0.06, yaw=0.0, coil=0.0,
            sucker_rows=0, sucker_count=0, sucker_scale=0.0, aspect=0.96, taper=0.85,
        ))
        s["flexibleAppendageCount"] = 9

        dorsal = rgba(178, 72, 46)
        mottle = rgba(124, 38, 24)
        ventral = rgba(226, 184, 158)
        s["dorsalColor"] = dorsal
        s["ventralColor"] = ventral
        s["unpigmentedVentralColor"] = ventral

        surf = s["surface"]
        surf["integument"]["type"] = 0  # INTEGUMENT_SMOOTH_SKIN (piel desnuda con papilas suaves)
        surf["integument"]["coverage"] = 1.0
        surf["wrinkleStrength"] = 0.24
        surf["wrinkleScale"] = 2.2
        surf["wrinkleDirectionality"] = 0.15
        surf["pigment"]["baseColor"] = dorsal
        surf["pigment"]["secondaryColor"] = mottle
        surf["pigment"]["ventralColor"] = ventral
        surf["pigment"]["dorsalDarkening"] = 0.18
        surf["pigment"]["ventralLightening"] = 0.65
        surf["pigment"]["patternStrength"] = 0.45
        surf["pigment"]["patternScale"] = 3.8
        surf["pigment"]["bands"] = 0.18
        surf["pigment"]["seed"] = 1018
        surf["pigment"]["layers"] = [
            pigment_layer(1, rgba(212, 118, 74), 0.40 * maturity, 5.2, 0.52, 10181, region_mask=0),
            pigment_layer(3, rgba(108, 34, 22), 0.36 * maturity, 3.2, 0.44, 10182, region_mask=(1 << 3)),
        ]
        surf["pigment"]["layerCount"] = 2

        s["development"] = {
            "appendages": dev_app, "cephalic": dev_ceph,
            "pigmentation": 0.48 + 0.52 * maturity,
            "integument": 1.0, "colorMaturity": maturity,
        }
        return s

    modules = [
        mod_inst(1, 0, 0, 0, 0),   # AXIAL CEPHALOPOD
        mod_inst(2, 1, 1, 1, 0),   # HEAD CEPHALOPOD
    ]
    for arm_i in range(8):
        modules.append(mod_inst(10 + arm_i, 5, 1, 24 + arm_i, arm_i))  # AXIAL_SLOT_RADIAL_BASE + arm_i
    modules.append(mod_inst(20, 5, 1, 11, 8))  # AXIAL_SLOT_SIPHON

    return {
        "version": 1,
        "recipe": {
            "id": 1018,
            "name": "Pulpo Gigante",
            "bodyPlan": {
                "root": {"moduleInstanceId": 1, "localNodeId": 2},
                "modules": modules,
                "moduleCount": len(modules),
            },
            "seed": make_stage(0.36, 0.34, 0.78, 0.20),
            "larva": make_stage(0.58, 0.58, 0.88, 0.48),
            "juvenile": make_stage(0.82, 0.84, 0.95, 0.78),
            "adult": make_stage(1.00, 1.00, 1.00, 1.00),
            "gait": 9,  # CREATURE_GAIT_OCTOPUS_CRAWL
            "offspringSeed": 0,
            "offspringVersion": 0,
        },
    }


# =============================================================================
# 4. LIBÉLULA EMPERADOR (Anax imperator) — ID 1019
# =============================================================================
def build_dragonfly(base):
    limb_tpl = base["recipe"]["adult"]["limbs"][0]

    def make_stage(scale, dev_app, wing_dev, abd_len_scale, maturity):
        s = blank_stage(base["recipe"]["adult"])
        ax = s["axial"]
        ax["archetype"] = 7  # AXIAL_ARCHETYPE_ARTHROPOD
        ax["totalScale"] = scale
        ax["withersHeight"] = 0.55
        ax["trunkLength"] = 1.95 * abd_len_scale
        ax["neckLength"] = 0.08
        ax["neckWidth"] = 0.14
        ax["shoulderWidth"] = 0.28
        ax["thoraxWidth"] = 0.34
        ax["thoraxHeight"] = 0.36
        ax["abdomenWidth"] = 0.135 * (1.28 - 0.28 * maturity)  # Ninfa más ancha, adulto muy esbelto
        ax["abdomenHeight"] = 0.125 * (1.24 - 0.24 * maturity)
        ax["pelvicWidth"] = 0.095
        ax["pelvicHeight"] = 0.090
        ax["abdomenSegments"] = 10

        # Cabeza ancha de insecto con grandes ojos compuestos verdosos y piezas bucales compactas
        hp = s["head"]
        hp["archetype"] = 13  # HEAD_ARCHETYPE_ARTHROPOD
        hp["skullWidth"] = 0.84
        hp["skullHeight"] = 0.76
        hp["skullLength"] = 0.48
        hp["muzzleLength"] = 0.12
        hp["muzzleWidth"] = 0.36
        hp["eyeSize"] = 0.96
        hp["eyeLaterality"] = 0.74
        hp["eyeForwardness"] = 0.58
        hp["eyeDorsality"] = 0.62
        hp["eyeExposure"] = 0.24
        hp["earSize"] = 0.0
        hp["muzzleForm"] = 3
        hp["eyeLayout"] = 1

        s["headEnvelope"] = {"scale": 0.22, "widthScale": 1.12, "heightScale": 1.00, "lengthScale": 0.74}
        s["eyes"] = {
            "size": 1.0, "protrusion": 0.90, "irisScale": 0.94, "pupilScale": 0.06,
            "pupilAspect": 1.0, "pupilShape": 0,
            "scleraColor": rgba(48, 152, 118), "irisColor": rgba(66, 184, 134), "pupilColor": rgba(42, 134, 102),
        }

        # 6 patas torácicas articuladas (coxa, trocánter, fémur, tibia, tarso, uñas)
        leg_sweeps = [(0.55, 0.35, 0.44), (0.05, 0.10, 0.52), (-0.52, -0.30, 0.62)]
        for pair_idx, (p_sw, d_sw, l_len) in enumerate(leg_sweeps):
            for side_idx in (0, 1):
                leg = blank_limb(limb_tpl, 7, 3, side_idx)  # LIMB_ARCHETYPE_ARTHROPOD_LEG, LIMB_LEG
                leg["length"] = l_len * (0.72 + 0.28 * dev_app)
                leg["thickness"] = 0.017
                leg["sprawl"] = 0.56 + 0.08 * pair_idx
                leg["proximalSweep"] = p_sw
                leg["distalSweep"] = d_sw
                leg["development"] = 0.78 + 0.22 * dev_app
                s["limbs"].append(leg)

        # 4 alas membranosas independientes con venación (2 anteriores + 2 posteriores más anchas en base)
        # Hemimetabolismo: en ninfa (wing_dev <= 0.22) se generan esbozos dorsales sin alas adultas.
        for pair_idx in (0, 1):
            for side_idx in (0, 1):
                w = blank_limb(limb_tpl, 8, 4, side_idx)  # LIMB_ARCHETYPE_INSECT_WING, LIMB_WING
                w["length"] = (1.36 if pair_idx == 0 else 1.30)
                w["thickness"] = 0.008
                w["development"] = wing_dev
                w["wingAspect"] = 4.55 if pair_idx == 0 else 3.35
                w["footWidthScale"] = 0.94 if pair_idx == 0 else 1.38
                w["finSweep"] = 0.01 if pair_idx == 0 else 0.06
                w["finCamber"] = 0.08 if pair_idx == 0 else 0.05
                s["limbs"].append(w)
        s["limbCount"] = 10

        # Antenas cortas craneales + cercos anales al final del somito 10
        s["ornaments"] = [
            {"archetype": 1, "length": 0.09, "baseRadius": 0.008, "tipRadius": 0.003,
             "curvature": 0.18, "development": 0.8, "growthDirection": {"x": 0.35, "y": 0.45, "z": 0.85}, "lateralSweep": 0.15},
            {"archetype": 1, "length": 0.09, "baseRadius": 0.008, "tipRadius": 0.003,
             "curvature": 0.18, "development": 0.8, "growthDirection": {"x": -0.35, "y": 0.45, "z": 0.85}, "lateralSweep": -0.15},
            {"archetype": 1, "length": 0.11, "baseRadius": 0.014, "tipRadius": 0.004,
             "curvature": -0.05, "development": 0.5 + 0.5 * maturity,
             "growthDirection": {"x": 0.16, "y": 0.04, "z": 1.0}, "lateralSweep": 0.08},
        ]
        s["ornamentCount"] = 3

        # Macho adulto Anax imperator: tórax verde vivo, abdomen azul brillante con banda dorsal negra
        # Ninfa acuática temprana: pardo-verdoso críptico
        thorax_green = rgba(84, 178, 66) if maturity > 0.55 else rgba(102, 118, 76)
        abd_blue = rgba(52, 152, 226) if maturity > 0.55 else rgba(110, 122, 82)
        ventral = rgba(76, 134, 96) if maturity > 0.55 else rgba(132, 134, 98)
        s["dorsalColor"] = abd_blue
        s["ventralColor"] = ventral
        s["unpigmentedVentralColor"] = ventral

        surf = s["surface"]
        surf["integument"]["type"] = 5  # INTEGUMENT_CHITIN
        surf["integument"]["coverage"] = 1.0
        surf["iridescence"] = 0.52 * maturity
        surf["pigment"]["baseColor"] = abd_blue
        surf["pigment"]["secondaryColor"] = rgba(24, 30, 38)
        surf["pigment"]["ventralColor"] = ventral
        surf["pigment"]["dorsalDarkening"] = 0.12
        surf["pigment"]["ventralLightening"] = 0.25
        surf["pigment"]["patternStrength"] = 0.15
        surf["pigment"]["patternScale"] = 4.0
        surf["pigment"]["bands"] = 0.20
        surf["pigment"]["seed"] = 1019
        # Tórax verde (zona anterior z > -0.35) + banda dorsal oscura longitudinal + patas oscuras
        surf["pigment"]["layers"] = [
            pigment_layer(0, thorax_green, 0.96, 1.0, 0.90, 10191, region_mask=0,
                          center=(0.0, 0.55, 0.12), radii=(0.55, 0.55, 0.58), softness=0.14),
            pigment_layer(2, rgba(22, 28, 36), 0.72 * maturity, 10.5, 0.78, 10192,
                          region_mask=(1 << 3), direction=(0, 0, 1)),
        ]
        surf["pigment"]["layerCount"] = 2
        surf["regionalAlbedo"][1] = rgba(88, 176, 86, 220)   # HEAD verde
        surf["regionalAlbedo"][5] = rgba(36, 40, 38, 240)    # FORELIMB / patas oscuras
        surf["regionalAlbedo"][6] = rgba(36, 40, 38, 240)    # HINDLIMB / patas oscuras

        s["development"] = {
            "appendages": 1.0, "cephalic": 0.85 + 0.15 * maturity,
            "pigmentation": 0.45 + 0.55 * maturity,
            "integument": 1.0, "colorMaturity": maturity,
        }
        return s

    modules = [
        mod_inst(1, 0, 0, 0, 0),    # AXIAL ARTHROPOD
        mod_inst(2, 1, 1, 1, 0),    # HEAD ARTHROPOD
        # 6 patas torácicas (slots 12..17)
        mod_inst(10, 2, 1, 12, 0), mod_inst(11, 2, 1, 13, 1),
        mod_inst(12, 2, 1, 14, 2), mod_inst(13, 2, 1, 15, 3),
        mod_inst(14, 2, 1, 16, 4), mod_inst(15, 2, 1, 17, 5),
        # 4 alas independientes (slots 18..21)
        mod_inst(20, 2, 1, 18, 6), mod_inst(21, 2, 1, 19, 7),
        mod_inst(22, 2, 1, 20, 8), mod_inst(23, 2, 1, 21, 9),
        # Antenas cortas + cercos terminales
        mod_inst(30, 4, 2, 2, 0),
        mod_inst(31, 4, 2, 3, 1),
        mod_inst(32, 4, 1, 6, 2),
    ]

    return {
        "version": 1,
        "recipe": {
            "id": 1019,
            "name": "Libelula Emperador",
            "bodyPlan": {
                "root": {"moduleInstanceId": 1, "localNodeId": 3},
                "modules": modules,
                "moduleCount": len(modules),
            },
            # Hemimetabolismo: ninfa acuática temprana -> ninfa avanzada con esbozos -> teneral -> adulto alado
            "seed": make_stage(0.48, 0.70, 0.14, 0.62, 0.12),
            "larva": make_stage(0.66, 0.85, 0.22, 0.74, 0.34),
            "juvenile": make_stage(0.88, 0.96, 0.72, 0.92, 0.68),
            "adult": make_stage(1.00, 1.00, 1.00, 1.00, 1.00),
            "gait": 10,  # CREATURE_GAIT_INSECT_FLIGHT
            "offspringSeed": 0,
            "offspringVersion": 0,
        },
    }


# =============================================================================
# 5. MEDUSA ORTIGA DEL PACÍFICO (Chrysaora fuscescens) — ID 1020
# =============================================================================
def build_jellyfish(base):
    def make_stage(scale, dev_app, maturity):
        s = blank_stage(base["recipe"]["adult"])
        ax = s["axial"]
        ax["archetype"] = 8  # AXIAL_ARCHETYPE_MEDUSA_RADIAL
        ax["totalScale"] = scale
        ax["withersHeight"] = 1.45
        ax["trunkLength"] = 0.80
        ax["neckLength"] = 0.20
        ax["neckWidth"] = 0.50
        ax["shoulderWidth"] = 1.05
        ax["thoraxWidth"] = 1.18  # Radio de campana ~0.59
        ax["thoraxHeight"] = 0.82 # Altura de cúpula ~0.46
        ax["abdomenWidth"] = 0.80
        ax["abdomenHeight"] = 0.40
        ax["pelvicWidth"] = 0.30
        ax["pelvicHeight"] = 0.25
        ax["radialSymmetryOrder"] = 8   # 8 radios -> 24 lóbulos marginales
        ax["bellConcavity"] = 0.74
        ax["bellScallopAmplitude"] = 0.15

        # 24 tentáculos marginales finos rojo-pardos + 4 brazos orales anchos rizados translúcidos
        s["repeatedAppendages"] = [
            {
                "groupId": 10201,
                "hostModuleInstanceId": 1,
                "hostNodeId": 3,
                "kind": 1,  # REPEATED_APPENDAGE_FILAMENT
                "count": 24,
                "ringRadius": 0.56,
                "ringOffsetY": -0.10,
                "arcStart": 0.0,
                "arcSpan": 6.2831853,
                "length": 1.65 * (0.28 + 0.72 * dev_app),
                "lengthVariation": 0.24,
                "baseRadius": 0.011,
                "tipRadius": 0.0025,
                "taper": 1.18,
                "ribbonWidth": 0.0,
                "ruffleAmplitude": 0.0,
                "ruffleFrequency": 2.0,
                "curlAmplitude": 0.14,
                "waveFrequency": 2.2,
                "phaseSpread": 0.52,
                "development": 0.35 + 0.65 * dev_app,
                "baseColor": rgba(138, 36, 22, 235),
                "tipColor": rgba(88, 22, 16, 210),
                "opacity": 0.90,
                "seed": 10201,
            },
            {
                "groupId": 10202,
                "hostModuleInstanceId": 1,
                "hostNodeId": 4,
                "kind": 2,  # REPEATED_APPENDAGE_FRILLED_RIBBON
                "count": 4,
                "ringRadius": 0.11,
                "ringOffsetY": -0.02,
                "arcStart": 0.78539816,
                "arcSpan": 6.2831853,
                "length": 2.02 * (0.24 + 0.76 * dev_app),
                "lengthVariation": 0.10,
                "baseRadius": 0.04,
                "tipRadius": 0.01,
                "taper": 0.85,
                "ribbonWidth": 0.25 * (0.55 + 0.45 * dev_app),
                "ruffleAmplitude": 0.46,
                "ruffleFrequency": 4.0,
                "curlAmplitude": 0.18,
                "waveFrequency": 1.6,
                "phaseSpread": 1.5707963,
                "development": 0.32 + 0.68 * dev_app,
                "baseColor": rgba(238, 166, 98, 215),
                "tipColor": rgba(212, 114, 64, 155),
                "opacity": 0.78,
                "seed": 10202,
            },
        ]
        s["repeatedAppendageCount"] = 2

        amber = rgba(224, 132, 42)
        rim_red = rgba(154, 52, 22)
        pale_amber = rgba(244, 192, 118)
        s["dorsalColor"] = amber
        s["ventralColor"] = pale_amber
        s["unpigmentedVentralColor"] = pale_amber

        surf = s["surface"]
        surf["integument"]["type"] = 6  # INTEGUMENT_GELATINOUS
        surf["integument"]["coverage"] = 1.0
        surf["translucency"] = 0.68
        surf["subsurfaceGlow"] = 0.65
        surf["iridescence"] = 0.12
        surf["pigment"]["baseColor"] = amber
        surf["pigment"]["secondaryColor"] = rim_red
        surf["pigment"]["ventralColor"] = pale_amber
        surf["pigment"]["dorsalDarkening"] = 0.14
        surf["pigment"]["ventralLightening"] = 0.45
        surf["pigment"]["patternStrength"] = 0.30
        surf["pigment"]["patternScale"] = 3.2
        surf["pigment"]["bands"] = 0.35
        surf["pigment"]["seed"] = 1020
        surf["pigment"]["layers"] = [
            pigment_layer(2, rim_red, 0.52 * maturity, 4.5, 0.58, 10203, region_mask=0, direction=(0, 1, 0)),
        ]
        surf["pigment"]["layerCount"] = 1

        s["development"] = {
            "appendages": dev_app, "cephalic": 0.0,
            "pigmentation": 0.45 + 0.55 * maturity,
            "integument": 1.0, "colorMaturity": maturity,
        }
        return s

    return {
        "version": 1,
        "recipe": {
            "id": 1020,
            "name": "Medusa Ortiga",
            "bodyPlan": {
                "root": {"moduleInstanceId": 1, "localNodeId": 3},
                "modules": [
                    mod_inst(1, 0, 0, 0, 0),  # AXIAL MEDUSA RADIAL (sin cabeza ni extremidades vertebradas)
                ],
                "moduleCount": 1,
            },
            # Secuencia ontogenética post-éfira / medusa (documentada explícitamente, sin simular pólipo sésil)
            "seed": make_stage(0.38, 0.28, 0.20),
            "larva": make_stage(0.60, 0.54, 0.48),
            "juvenile": make_stage(0.84, 0.82, 0.78),
            "adult": make_stage(1.00, 1.00, 1.00),
            "gait": 11,  # CREATURE_GAIT_MEDUSA_PULSE
            "offspringSeed": 0,
            "offspringVersion": 0,
        },
    }


def main():
    base = json.loads(BASE_PATH.read_text(encoding="utf-8"))
    outputs = [
        ("halcon_peregrino.creature.json", build_falcon(base)),
        ("mantarraya.creature.json", build_manta(base)),
        ("pulpo_gigante.creature.json", build_octopus(base)),
        ("libelula_emperador.creature.json", build_dragonfly(base)),
        ("medusa_ortiga.creature.json", build_jellyfish(base)),
    ]
    for filename, doc in outputs:
        path = ROOT / "recipes" / filename
        path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Wrote {path} (id={doc['recipe']['id']}, name={doc['recipe']['name']})")


if __name__ == "__main__":
    main()
