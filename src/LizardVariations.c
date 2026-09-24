/**
 * @file LizardVariations.c
 * @brief Definición de las diez variantes de lagarto mediante combinaciones de rasgos semánticos.
 * @author Monster Engine Team
 * @date 2026
 */

#include "LizardVariations.h"
#include <string.h>

static CreatureTrait Trait(CreatureTraitType type, float strength) {
    CreatureTrait t;
    memset(&t, 0, sizeof(t));
    t.type = type;
    t.strength = strength;
    return t;
}

static CreatureTrait TraitParam(CreatureTraitType type, float strength, float paramA, float paramB) {
    CreatureTrait t;
    memset(&t, 0, sizeof(t));
    t.type = type;
    t.strength = strength;
    t.paramA = paramA;
    t.paramB = paramB;
    return t;
}

static CreatureTrait TraitColor(CreatureTraitType type, float strength, Color cA, Color cB) {
    CreatureTrait t;
    memset(&t, 0, sizeof(t));
    t.type = type;
    t.strength = strength;
    t.colorA = cA;
    t.colorB = cB;
    return t;
}

static CreatureTrait TraitPattern(PigmentPattern pat, float strength, float scale, Color color) {
    CreatureTrait t;
    memset(&t, 0, sizeof(t));
    t.type = CREATURE_TRAIT_PATTERN_LAYER;
    t.strength = strength;
    t.paramA = (float)pat;
    t.paramB = scale;
    t.colorA = color;
    return t;
}

CreatureVariation LizardVariations_EmeraldClimber(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Emerald Climber", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_SLENDER, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_ELONGATED, 0.40f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_SMALL, 0.45f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_NARROW, HEAD_MUZZLE_LONG));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_FORWARD, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_SHORT_SNOUT, 0.35f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_LARGE, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_PROTRUDING, 0.80f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PUPIL_ROUND, 1.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_ARBOREAL, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_PREHENSILE, 0.90f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, 0.85f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(28, 175, 58), Color_FromRGB(195, 230, 160)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_SPOTS, 0.65f, 1.8f,
        Color_FromRGB(15, 60, 25)));
    return v;
}

CreatureVariation LizardVariations_DesertHorned(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Desert Horned", seed);
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_BODY_BROAD_LOW, 0.92f, 1.46f, 0.76f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_BODY_COMPACT, 0.78f, 0.58f, 0.0f));
    /* El cuerpo de referencia es bajo, pero tiene una sección abdominal llena;
     * el rasgo profundo actúa sobre todas las estaciones, no solo sobre el tórax. */
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_DEEP, 0.28f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_BROAD_TRIANGULAR, 0.92f, 1.15f, 1.35f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_SHIELD, HEAD_MUZZLE_WEDGE));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_LATERAL, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_DEEP, 0.28f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_LARGE, 0.18f));
    /* Hocico corto, pero con una conicidad marcada para conservar la silueta
     * triangular del desert horned en vista frontal. */
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_SHORT_SNOUT, 0.72f, 0.86f, 0.38f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_SMALL, 0.48f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PUPIL_VERTICAL, 0.80f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_SHORT, 0.65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_ROBUST, 0.70f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FEET_LARGE, 0.42f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_TAIL_TAPERED, 0.95f, 0.30f, 0.95f));
    /* Dos cuernos laterales grandes en las ranuras craneales del módulo de
     * cabeza. La distribución sigue siendo un rasgo general reutilizable. */
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HORNS, 0.92f, 0.92f, 0.21f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_CRANIAL_CROWN, 0.95f, 0.85f, 0.15f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_LATERAL_SPINES, 0.95f, 9.0f, 0.30f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, 0.85f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(198, 154, 98), Color_FromRGB(230, 218, 185)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_BANDS, 0.18f, 0.9f,
        Color_FromRGB(95, 55, 25)));
    CreatureTrait armor = Trait(CREATURE_TRAIT_ORNAMENT_FIELD, 1.0f);
    armor.hostKind = CREATURE_MODULE_AXIAL;
    armor.field = (OrnamentField){
        .row = {.ornament = {.archetype = ORNAMENT_ARCHETYPE_SPINE,
            .length = .30f, .baseRadius = .12f, .tipRadius = .012f,
            .curvature = .08f, .development = 1},
            .count = 6, .pathStart = .28f, .pathEnd = .94f,
            .sizeStart = .65f, .sizePeak = 1, .sizeEnd = .50f},
        .rows = 4, .angularSpread = 2.0f, .stagger = .5f, .sizeJitter = .22f};
    CreatureVariation_AddTrait(&v, armor);
    armor.hostKind = CREATURE_MODULE_TAIL;
    armor.field.rows = 1;
    armor.field.row.count = 6;
    armor.field.row.pathStart = .12f;
    armor.field.row.pathEnd = .90f;
    armor.field.row.ornament.length = .18f;
    armor.field.row.ornament.baseRadius = .08f;
    armor.field.row.sizeEnd = .15f;
    CreatureVariation_AddTrait(&v, armor);

    return v;
}

CreatureVariation LizardVariations_BlueRunner(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Blue Runner", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_ELONGATED, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_SLENDER, 0.90f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_LONG_SNOUT, 0.90f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_NARROW, HEAD_MUZZLE_LONG));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_FORWARD, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PUPIL_HORIZONTAL, 0.75f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_CURSORIAL, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_WHIPPED, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, 0.90f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(18, 48, 145), Color_FromRGB(170, 205, 235)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_STRIPES, 0.85f, 1.2f,
        Color_FromRGB(40, 215, 235)));
    return v;
}

CreatureVariation LizardVariations_FatTailedGecko(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Fat-Tailed Gecko", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FEET_LARGE, .85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_COMPACT, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_LARGE, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_SHORT_SNOUT, 0.75f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_BLOCK, HEAD_MUZZLE_BLUNT));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_HIGH_LATERAL, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_LARGE, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_PROTRUDING, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PUPIL_VERTICAL, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_SHORT, 0.65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_ROBUST, 0.60f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_HEAVY, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, 0.70f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(235, 222, 192), Color_FromRGB(248, 245, 232)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_BLOTCHES, 0.80f, 0.85f,
        Color_FromRGB(220, 115, 30)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_SPOTS, 0.65f, 2.2f,
        Color_FromRGB(35, 25, 20)));
    return v;
}

CreatureVariation LizardVariations_VolcanicSpiny(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Volcanic Spiny", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_ROBUST, 0.90f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_LARGE, 0.65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_HEAVY_JAW, 0.85f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_BLOCK, HEAD_MUZZLE_TAPERED));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_CASQUE, 0.45f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PUPIL_VERTICAL, 0.90f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_ROBUST, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_HEAVY, 0.55f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_DORSAL_SPINES, 0.95f, 9.0f, 0.65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAVY_SCALES, 0.95f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(28, 28, 32), Color_FromRGB(60, 35, 30)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_GRADIENT, 0.85f, 0.5f,
        Color_FromRGB(225, 75, 20)));
    return v;
}

CreatureVariation LizardVariations_SandBurrower(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Sand Burrower", seed);
    /* Tronco alargado y ovalado; no confundir un habitante de arena con una
     * pala aplanada de miembros excavadores hipertrofiados. */
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_ELONGATED, .48f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_SLENDER, .32f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_DEEP, .10f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_NECK_CONTINUOUS, 1));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_STREAMLINED, .95f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1,
        HEAD_CRANIAL_STANDARD, HEAD_MUZZLE_TAPERED));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_SMALL, 1));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_APERTURE, 1, .85f, 0));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_EYE_PALETTE, 1,
        Color_FromRGB(104, 89, 57), Color_FromRGB(56, 49, 32)));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PUPIL_ROUND, 1));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_GRACILE, .90f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_SHORT, .30f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FEET_NARROW, .90f));
    CreatureTrait hindStance = Trait(CREATURE_TRAIT_LIMBS_RETRACTED, 1);
    hindStance.limbTarget = CREATURE_LIMBS_HIND;
    CreatureVariation_AddTrait(&v, hindStance);
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_TAIL_TAPERED, 1, 1.02f, .88f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_TAIL_CURVED, 1, .95f, 0));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, .95f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1,
        Color_FromRGB(105, 91, 62), Color_FromRGB(192, 180, 144)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_STRIPES, .50f, .85f,
        Color_FromRGB(53, 45, 31)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_SPOTS, .35f, 24.0f,
        Color_FromRGB(216, 205, 166)));
    return v;
}

CreatureVariation LizardVariations_StoneArmored(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Stone Armored", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_ROBUST, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_COMPACT, 0.60f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_DEEP, 0.75f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_SMALL, 0.65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_HEAVY_JAW, 0.90f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_BLOCK, HEAD_MUZZLE_BLUNT));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_LOW_LATERAL, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_SMALL, 0.70f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_ROBUST, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_SHORT, 0.75f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_HEAVY, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PLATES, 0.95f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(105, 115, 95), Color_FromRGB(85, 90, 80)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_BLOTCHES, 0.75f, 0.75f,
        Color_FromRGB(45, 52, 40)));
    return v;
}

CreatureVariation LizardVariations_Sailback(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Sailback", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_ELONGATED, 0.80f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_LONG_SNOUT, 0.50f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_NARROW, HEAD_MUZZLE_LONG));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_FORWARD, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_LONG, 0.65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_TAPERED, 0.90f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_DORSAL_SAIL, 0.95f, 10.0f, 2.30f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, 0.70f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(24, 112, 125), Color_FromRGB(180, 220, 215)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_BANDS, 0.55f, 1.1f,
        Color_FromRGB(15, 75, 85)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_GRADIENT, 0.85f, 0.6f,
        Color_FromRGB(235, 140, 25)));
    return v;
}

CreatureVariation LizardVariations_JewelChameleon(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Jewel Chameleon", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_COMPACT, 0.85f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_CASQUE, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_SHORT_SNOUT, 0.70f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_BLOCK, HEAD_MUZZLE_BLUNT));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_HIGH_LATERAL, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_LARGE, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_PROTRUDING, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_PUPIL_DIAMOND, 0.90f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_ARBOREAL, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_PREHENSILE, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, 0.90f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(22, 168, 148), Color_FromRGB(195, 245, 210)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_OCELLI, 0.85f, 1.4f,
        Color_FromRGB(240, 215, 30)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_SPOTS, 0.70f, 2.4f,
        Color_FromRGB(25, 95, 205)));
    return v;
}

CreatureVariation LizardVariations_AquaticFinTail(uint32_t seed) {
    CreatureVariation v = CreatureVariation_Create("Aquatic Fin-Tail", seed);
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_SMALL, .65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_ELONGATED, 0.75f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_BODY_SLENDER, 0.55f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_LONG_SNOUT, 0.55f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_HEAD_WEDGE, 0.60f));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_HEAD_FORM, 1.0f,
        HEAD_CRANIAL_WEDGE, HEAD_MUZZLE_TAPERED));
    CreatureVariation_AddTrait(&v, TraitParam(CREATURE_TRAIT_EYE_LAYOUT, 1.0f,
        HEAD_EYES_FORWARD, 0.0f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_EYES_SMALL, 0.45f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_LIMBS_SHORT, 0.65f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_TAIL_FINNED, 0.95f));
    CreatureVariation_AddTrait(&v, Trait(CREATURE_TRAIT_FINE_SCALES, 0.85f));
    CreatureVariation_AddTrait(&v, TraitColor(CREATURE_TRAIT_COLOR_PALETTE, 1.0f,
        Color_FromRGB(70, 95, 125), Color_FromRGB(225, 235, 245)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_GRADIENT, 0.60f, 0.45f,
        Color_FromRGB(40, 60, 85)));
    CreatureVariation_AddTrait(&v, TraitPattern(PIGMENT_PATTERN_SPOTS, 0.45f, 1.8f,
        Color_FromRGB(120, 165, 195)));
    return v;
}

size_t LizardVariations_GetCount(void) {
    return LIZARD_VARIATIONS_COUNT;
}

CreatureVariation LizardVariations_Get(size_t index, uint32_t seed) {
    switch (index % LIZARD_VARIATIONS_COUNT) {
    case LIZARD_VARIANT_EMERALD_CLIMBER: return LizardVariations_EmeraldClimber(seed);
    case LIZARD_VARIANT_DESERT_HORNED:   return LizardVariations_DesertHorned(seed);
    case LIZARD_VARIANT_BLUE_RUNNER:     return LizardVariations_BlueRunner(seed);
    case LIZARD_VARIANT_FAT_TAILED_GECKO:return LizardVariations_FatTailedGecko(seed);
    case LIZARD_VARIANT_VOLCANIC_SPINY:  return LizardVariations_VolcanicSpiny(seed);
    case LIZARD_VARIANT_SAND_BURROWER:   return LizardVariations_SandBurrower(seed);
    case LIZARD_VARIANT_STONE_ARMORED:   return LizardVariations_StoneArmored(seed);
    case LIZARD_VARIANT_SAILBACK:        return LizardVariations_Sailback(seed);
    case LIZARD_VARIANT_JEWEL_CHAMELEON: return LizardVariations_JewelChameleon(seed);
    case LIZARD_VARIANT_AQUATIC_FIN_TAIL:
    default:                             return LizardVariations_AquaticFinTail(seed);
    }
}

const char* LizardVariations_GetName(size_t index) {
    static const char* const names[LIZARD_VARIATIONS_COUNT] = {
        "Emerald Climber",
        "Desert Horned",
        "Blue Runner",
        "Fat-Tailed Gecko",
        "Volcanic Spiny",
        "Sand Burrower",
        "Stone Armored",
        "Sailback",
        "Jewel Chameleon",
        "Aquatic Fin-Tail"
    };
    return names[index % LIZARD_VARIATIONS_COUNT];
}
