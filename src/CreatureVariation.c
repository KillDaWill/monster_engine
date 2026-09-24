/**
 * @file CreatureVariation.c
 * @brief Implementación del sistema reutilizable de variación morfológica y rasgos.
 * @author Monster Engine Team
 * @date 2026
 */

#include "CreatureVariation.h"
#include "HeadModule.h"
#include "AxialBodySprawlingTetrapod.h"
#include "AttachmentPath.h"
#include "MathUtils.h"
#include "Creature.h"
#include <string.h>
#include <math.h>

typedef struct VariationRng {
    uint32_t state;
} VariationRng;

static inline float Rng_NextFloat(VariationRng* rng, float minVal, float maxVal) {
    rng->state = rng->state * 1664525u + 1013904223u;
    float norm = (float)(rng->state & 0x00ffffffu) * (1.0f / 16777216.0f);
    return minVal + norm * (maxVal - minVal);
}

CreatureVariation CreatureVariation_Create(const char* name, uint32_t seed) {
    CreatureVariation v;
    memset(&v, 0, sizeof(v));
    v.name = name ? name : "Variacion";
    v.seed = seed;
    return v;
}

bool CreatureVariation_AddTrait(CreatureVariation* v, CreatureTrait trait) {
    if (!v || v->traitCount >= CREATURE_MAX_TRAITS) return false;
    v->traits[v->traitCount++] = trait;
    return true;
}

bool CreatureVariation_IsTraitCompatible(
    const CreatureTrait* trait,
    const CreatureRecipe* recipe,
    const CreaturePhenotype* phenotype) {
    if (!trait || !recipe || !phenotype || !isfinite(trait->strength) ||
        !isfinite(trait->paramA) || !isfinite(trait->paramB)) return false;

    switch (trait->type) {
    case CREATURE_TRAIT_ORNAMENT_FIELD:
        return trait->hostKind == CREATURE_MODULE_AXIAL || trait->hostKind == CREATURE_MODULE_TAIL;
    case CREATURE_TRAIT_LIMBS_LONG:
    case CREATURE_TRAIT_LIMBS_SHORT:
    case CREATURE_TRAIT_LIMBS_ROBUST:
    case CREATURE_TRAIT_LIMBS_ARBOREAL:
    case CREATURE_TRAIT_LIMBS_CURSORIAL:
    case CREATURE_TRAIT_LIMBS_DIGGING:
    case CREATURE_TRAIT_FEET_LARGE:
    case CREATURE_TRAIT_LIMBS_GRACILE:
    case CREATURE_TRAIT_FEET_NARROW:
    case CREATURE_TRAIT_LIMBS_RETRACTED:
        return phenotype->limbCount > 0;

    case CREATURE_TRAIT_TAIL_CURVED:
    case CREATURE_TRAIT_TAIL_TAPERED:
    case CREATURE_TRAIT_TAIL_WHIPPED:
    case CREATURE_TRAIT_TAIL_HEAVY:
    case CREATURE_TRAIT_TAIL_PREHENSILE:
        return phenotype->tailCount > 0;
    case CREATURE_TRAIT_TAIL_FINNED:
        return phenotype->tailCount > 0;
    case CREATURE_TRAIT_PLATES:
        return recipe->bodyPlan.moduleCount+5<=CREATURE_MAX_MODULES &&
            phenotype->ornamentCount+5<=CREATURE_MAX_ORNAMENTS;

    case CREATURE_TRAIT_HEAD_CASQUE:
        return recipe->bodyPlan.moduleCount+1<=CREATURE_MAX_MODULES && phenotype->ornamentCount+1<=CREATURE_MAX_ORNAMENTS;
    case CREATURE_TRAIT_HORNS:
        return recipe->bodyPlan.moduleCount + 2 <= CREATURE_MAX_MODULES &&
               phenotype->ornamentCount + 2 <= CREATURE_MAX_ORNAMENTS;
    case CREATURE_TRAIT_CRANIAL_CROWN:
        return recipe->bodyPlan.moduleCount + 6 <= CREATURE_MAX_MODULES &&
               phenotype->ornamentCount + 6 <= CREATURE_MAX_ORNAMENTS;

    case CREATURE_TRAIT_DORSAL_SPINES:
    case CREATURE_TRAIT_DORSAL_SAIL: {
        if(trait->paramA > 14 || trait->paramA < 0) return false;
        unsigned count = trait->paramA > 0 ? (unsigned)trait->paramA :
            trait->type == CREATURE_TRAIT_DORSAL_SAIL ? 10 : 8;
        return count >= 2 && count <= 14 &&
            recipe->bodyPlan.moduleCount + count <= CREATURE_MAX_MODULES &&
            phenotype->ornamentCount + count <= CREATURE_MAX_ORNAMENTS;
    }
    case CREATURE_TRAIT_LATERAL_SPINES: {
        if (trait->paramA > 10 || trait->paramA < 0) return false;
        unsigned count = trait->paramA > 0 ? (unsigned)trait->paramA : 6;
        return count >= 2 && count <= 10 &&
               recipe->bodyPlan.moduleCount + count * 2 <= CREATURE_MAX_MODULES &&
               phenotype->ornamentCount + count * 2 <= CREATURE_MAX_ORNAMENTS;
    }

    default:
        return true;
    }
}

uint64_t CreaturePhenotype_Fingerprint(const CreaturePhenotype* p) {
    if (!p) return 0;
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;
    uint64_t hash = FNV_OFFSET;

    const unsigned char* bytes = (const unsigned char*)p;
    for (size_t i = 0; i < sizeof(CreaturePhenotype); ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static bool CreatureVariation_ApplyInto(
    const CreatureRecipe* baseRecipe,
    const CreaturePhenotype* basePhenotype,
    const CreatureVariation* variation,
    CreatureVariant* out) {
    if (!baseRecipe || !basePhenotype || !variation || !out) return false;

    out->recipe = *baseRecipe;
    out->phenotype = *basePhenotype;
    CreaturePhenotype_Normalize(&out->phenotype);

    VariationRng rng = {.state = variation->seed ? variation->seed : 1u};

    for (size_t i = 0; i < variation->traitCount; ++i) {
        const CreatureTrait* t = &variation->traits[i];
        if (!CreatureVariation_IsTraitCompatible(t, &out->recipe, &out->phenotype)) {
            return false;
        }

        float s = Math_Clamp01(t->strength);
        if (t->type < CREATURE_TRAIT_FINE_SCALES || t->type == CREATURE_TRAIT_HORNS ||
            t->type == CREATURE_TRAIT_DORSAL_SPINES || t->type == CREATURE_TRAIT_DORSAL_SAIL)
            s *= Rng_NextFloat(&rng, .92f, 1.0f);

        switch (t->type) {
        /* Proporciones Corporales Axiales */
        case CREATURE_TRAIT_BODY_SLENDER: {
            float f = Math_Lerp(1.0f, Rng_NextFloat(&rng, 0.48f, 0.58f), s);
            out->phenotype.axial.thoraxWidth *= f;
            out->phenotype.axial.thoraxHeight *= sqrtf(f);
            out->phenotype.axial.abdomenHeight *= sqrtf(f);
            out->phenotype.axial.pelvicHeight *= sqrtf(f);
            out->phenotype.axial.abdomenWidth *= f;
            out->phenotype.axial.pelvicWidth *= f;
            out->phenotype.axial.shoulderWidth *= f;
            out->phenotype.axial.neckWidth *= f;
            break;
        }
        case CREATURE_TRAIT_BODY_ROBUST: {
            float f = Math_Lerp(1.0f, Rng_NextFloat(&rng, 1.25f, 1.45f), s);
            out->phenotype.axial.thoraxWidth *= f;
            out->phenotype.axial.thoraxHeight *= Math_Lerp(1.0f, 1.25f, s);
            out->phenotype.axial.abdomenWidth *= f;
            out->phenotype.axial.pelvicWidth *= f;
            out->phenotype.axial.shoulderWidth *= f;
            out->phenotype.axial.neckWidth *= Math_Lerp(1.0f, 1.28f, s);
            break;
        }
        case CREATURE_TRAIT_BODY_COMPACT: {
            /* paramA permite que una receta elija la compactación exacta;
             * sin él se conserva la dispersión determinista histórica. */
            float target = (t->paramA > 0.0f) ? Math_Clamp(t->paramA, .35f, .90f) :
                Rng_NextFloat(&rng, 0.52f, 0.64f);
            float f = Math_Lerp(1.0f, target, s);
            out->phenotype.axial.trunkLength *= f;
            out->phenotype.axial.profile[4].widthScale *= Math_Lerp(1, 1.20f, s);
            out->phenotype.axial.profile[4].heightScale *= Math_Lerp(1, 1.15f, s);
            out->phenotype.axial.neckLength *= Math_Lerp(1.0f, 0.80f, s);
            break;
        }
        case CREATURE_TRAIT_BODY_ELONGATED: {
            float f = Math_Lerp(1.0f, Rng_NextFloat(&rng, 1.45f, 1.70f), s);
            out->phenotype.axial.trunkLength *= f;
            out->phenotype.axial.neckLength *= Math_Lerp(1.0f, 1.15f, s);
            break;
        }
        case CREATURE_TRAIT_BODY_FLATTENED: {
            out->phenotype.axial.profile[1].widthScale *= Math_Lerp(1, 1.25f, s);
            out->phenotype.axial.profile[0].heightScale *= Math_Lerp(1, .60f, s);
            out->phenotype.axial.bodyFlattening = Math_Lerp(out->phenotype.axial.bodyFlattening, 0.45f, s);
            out->phenotype.axial.thoraxHeight *= Math_Lerp(1.0f, 0.65f, s);
            out->phenotype.axial.abdomenHeight *= Math_Lerp(1.0f, 0.62f, s);
            out->phenotype.axial.pelvicHeight *= Math_Lerp(1.0f, 0.65f, s);
            out->phenotype.axial.thoraxWidth *= Math_Lerp(1.0f, 1.35f, s);
            out->phenotype.axial.abdomenWidth *= Math_Lerp(1.0f, 1.30f, s);
            break;
        }

        case CREATURE_TRAIT_BODY_DEEP: {
            out->phenotype.axial.thoraxHeight *= Math_Lerp(1,1.60f,s);
            out->phenotype.axial.abdomenHeight *= Math_Lerp(1,1.45f,s);
            out->phenotype.axial.pelvicHeight *= Math_Lerp(1,1.40f,s);
            out->phenotype.axial.neckWidth *= Math_Lerp(1,1.50f,s);
            break;
        }
        case CREATURE_TRAIT_BODY_BROAD_LOW: {
            float breadth = t->paramA > 0.0f ? Math_Clamp(t->paramA, 1.05f, 1.80f) : 1.42f;
            float low = t->paramB > 0.0f ? Math_Clamp(t->paramB, .55f, .95f) : .79f;
            out->phenotype.axial.shoulderWidth *= Math_Lerp(1.0f, 1.18f, s);
            out->phenotype.axial.thoraxWidth *= Math_Lerp(1.0f, breadth * .90f, s);
            out->phenotype.axial.abdomenWidth *= Math_Lerp(1.0f, breadth, s);
            out->phenotype.axial.pelvicWidth *= Math_Lerp(1.0f, 1.16f, s);

            out->phenotype.axial.thoraxHeight *= Math_Lerp(1.0f, low * 1.22f, s);
            out->phenotype.axial.abdomenHeight *= Math_Lerp(1.0f, low * 1.22f, s);
            out->phenotype.axial.pelvicHeight *= Math_Lerp(1.0f, low * 1.22f, s);
            out->phenotype.axial.bodyFlattening = Math_Lerp(out->phenotype.axial.bodyFlattening, low, s);

            out->phenotype.axial.profile[0].widthScale *= Math_Lerp(1.0f, 1.15f, s);
            out->phenotype.axial.profile[1].widthScale *= Math_Lerp(1.0f, 1.22f, s);
            out->phenotype.axial.profile[2].widthScale *= Math_Lerp(1.0f, 1.38f, s);
            out->phenotype.axial.profile[3].widthScale *= Math_Lerp(1.0f, 1.55f, s);
            out->phenotype.axial.profile[4].widthScale *= Math_Lerp(1.0f, 1.22f, s);
            out->phenotype.axial.profile[5].widthScale *= Math_Lerp(1.0f, 1.12f, s);

            out->phenotype.axial.profile[1].heightScale *= Math_Lerp(1.0f, 0.95f, s);
            out->phenotype.axial.profile[2].heightScale *= Math_Lerp(1.0f, 0.92f, s);
            out->phenotype.axial.profile[3].heightScale *= Math_Lerp(1.0f, 0.92f, s);
            out->phenotype.axial.profile[4].heightScale *= Math_Lerp(1.0f, 0.92f, s);
            out->phenotype.axial.profile[5].heightScale *= Math_Lerp(1.0f, 0.95f, s);

            /* Arco dorsal sutil en el perfil lateral */
            out->phenotype.axial.profile[1].verticalOffset += Math_Lerp(0.0f, 0.05f, s);
            out->phenotype.axial.profile[2].verticalOffset += Math_Lerp(0.0f, 0.09f, s);
            out->phenotype.axial.profile[3].verticalOffset += Math_Lerp(0.0f, 0.08f, s);
            out->phenotype.axial.profile[4].verticalOffset += Math_Lerp(0.0f, 0.04f, s);
            out->phenotype.axial.profile[5].verticalOffset += Math_Lerp(0.0f, -0.01f, s);

            out->phenotype.axial.neckLength *= Math_Lerp(1.0f, 0.70f, s);
            out->phenotype.axial.neckWidth *= Math_Lerp(1.0f, 1.35f, s);
            break;
        }
        case CREATURE_TRAIT_NECK_CONTINUOUS: {
            /* La sección cervical sigue la cintura adyacente, no una escala
             * absoluta ni un identificador de especie. Aplicar tras los rasgos corporales. */
            float proportion = t->paramA > 0 ? Math_Clamp(t->paramA, .80f, 1.10f) : .96f;
            out->phenotype.axial.neckWidth = Math_Lerp(out->phenotype.axial.neckWidth,
                out->phenotype.axial.shoulderWidth * proportion, s);
            break;
        }
        /* Morfología Cefálica */
        case CREATURE_TRAIT_HEAD_STREAMLINED: {
            HeadPhenotype* h = &out->phenotype.head;
            out->phenotype.headEnvelope.widthScale *= Math_Lerp(1, .80f, s);
            out->phenotype.headEnvelope.heightScale *= Math_Lerp(1, 1.40f, s);
            out->phenotype.headEnvelope.lengthScale *= Math_Lerp(1, 1.08f, s);
            h->skullWidth = Math_Lerp(h->skullWidth, .48f, s);
            h->skullHeight = Math_Lerp(h->skullHeight, .62f, s);
            h->muzzleLength = Math_Lerp(h->muzzleLength, .10f, s);
            h->muzzleWidth = Math_Lerp(h->muzzleWidth, .48f, s);
            h->muzzleTaper = Math_Lerp(h->muzzleTaper, .58f, s);
            h->snoutBluntness = Math_Lerp(h->snoutBluntness, .72f, s);
            h->rostrumDorsalSlope = Math_Lerp(h->rostrumDorsalSlope, .42f, s);
            h->cheekMass = Math_Lerp(h->cheekMass, .28f, s);
            h->rostrumDepth = Math_Lerp(h->rostrumDepth, .82f, s);
            h->jawLength = Math_Lerp(h->jawLength, .45f, s);
            h->jawDepth = Math_Lerp(h->jawDepth, .34f, s);
            h->jawStrength = Math_Lerp(h->jawStrength, .38f, s);
            h->noseScale = Math_Lerp(h->noseScale, .08f, s);
            h->eyeDorsality = Math_Lerp(h->eyeDorsality, .55f, s);
            h->eyeLaterality = Math_Lerp(h->eyeLaterality, .62f, s);
            break;
        }

        case CREATURE_TRAIT_HEAD_FORM:
            if (t->paramA < HEAD_CRANIAL_STANDARD || t->paramA > HEAD_CRANIAL_WEDGE ||
                t->paramB < HEAD_MUZZLE_TAPERED || t->paramB > HEAD_MUZZLE_WEDGE)
                return false;
            out->phenotype.head.cranialForm = (HeadCranialForm)(int)t->paramA;
            out->phenotype.head.muzzleForm = (HeadMuzzleForm)(int)t->paramB;
            if ((int)t->paramA == HEAD_CRANIAL_SHIELD || (int)t->paramA == HEAD_CRANIAL_BLOCK)
                out->phenotype.head.eyeCompression = Math_Lerp(out->phenotype.head.eyeCompression, .55f, s);
            else if ((int)t->paramA == HEAD_CRANIAL_NARROW)
                out->phenotype.head.eyeCompression = Math_Lerp(out->phenotype.head.eyeCompression, .30f, s);
            break;
        case CREATURE_TRAIT_EYE_LAYOUT:
            if (t->paramA < HEAD_EYES_LATERAL || t->paramA > HEAD_EYES_LOW_LATERAL)
                return false;
            out->phenotype.head.eyeLayout = (HeadEyeLayout)(int)t->paramA;
            break;
        case CREATURE_TRAIT_HEAD_DEEP: {
            out->phenotype.headEnvelope.heightScale *= Math_Lerp(1, 1.65f, s);
            out->phenotype.head.skullHeight = Math_Lerp(out->phenotype.head.skullHeight, .65f, s);
            break;
        }
        case CREATURE_TRAIT_HEAD_SMALL: {
            out->phenotype.headEnvelope.scale *= Math_Lerp(1.0f, .65f, s);
            break;
        }
        case CREATURE_TRAIT_HEAD_LARGE: {
            out->phenotype.headEnvelope.scale *= Math_Lerp(1.0f, 1.35f, s);
            out->phenotype.headEnvelope.widthScale *= Math_Lerp(1.0f, 1.20f, s);
            break;
        }
        case CREATURE_TRAIT_HEAD_LONG_SNOUT: {
            out->phenotype.headEnvelope.lengthScale *= Math_Lerp(1, 1.35f, s);
            out->phenotype.headEnvelope.widthScale *= Math_Lerp(1, .75f, s);
            out->phenotype.head.muzzleLength = Math_Lerp(out->phenotype.head.muzzleLength, 0.90f, s);
            out->phenotype.head.snoutBluntness = Math_Lerp(out->phenotype.head.snoutBluntness, 0.20f, s);
            out->phenotype.head.jawLength = Math_Lerp(out->phenotype.head.jawLength, 0.90f, s);
            out->phenotype.head.skullWidth = Math_Lerp(out->phenotype.head.skullWidth, 0.25f, s);
            break;
        }
        case CREATURE_TRAIT_HEAD_SHORT_SNOUT: {
            float taper = t->paramA > 0.0f ? Math_Clamp01(t->paramA) : .72f;
            float width = t->paramB > 0.0f ? Math_Clamp01(t->paramB) : .82f;
            out->phenotype.head.muzzleLength = Math_Lerp(out->phenotype.head.muzzleLength, 0.25f, s);
            out->phenotype.head.snoutBluntness = Math_Lerp(out->phenotype.head.snoutBluntness, out->phenotype.head.muzzleForm == HEAD_MUZZLE_WEDGE ? .30f : .78f, s);
            out->phenotype.head.jawLength = Math_Lerp(out->phenotype.head.jawLength, 0.28f, s);
            out->phenotype.head.muzzleWidth = Math_Lerp(out->phenotype.head.muzzleWidth, width, s);
            out->phenotype.head.muzzleTaper = Math_Lerp(out->phenotype.head.muzzleTaper, taper, s);
            break;
        }
        case CREATURE_TRAIT_HEAD_HEAVY_JAW: {
            float depth = t->paramA > 0.0f ? Math_Clamp01(t->paramA) : .90f;
            float strength = t->paramB > 0.0f ? Math_Clamp01(t->paramB) : .90f;
            out->phenotype.head.jawDepth = Math_Lerp(out->phenotype.head.jawDepth, depth, s);
            out->phenotype.head.jawStrength = Math_Lerp(out->phenotype.head.jawStrength, strength, s);
            out->phenotype.head.cheekMass = Math_Lerp(out->phenotype.head.cheekMass, Math_Max(depth, strength), s);
            break;
        }
        case CREATURE_TRAIT_HEAD_WEDGE: {
            out->phenotype.headEnvelope.heightScale *= Math_Lerp(1, .55f, s);
            out->phenotype.head.skullHeight = Math_Lerp(out->phenotype.head.skullHeight, 0.25f, s);
            out->phenotype.head.rostrumDepth = Math_Lerp(out->phenotype.head.rostrumDepth, 0.25f, s);
            out->phenotype.head.rostrumDorsalSlope = Math_Lerp(out->phenotype.head.rostrumDorsalSlope, 0.78f, s);
            out->phenotype.head.snoutBluntness = Math_Lerp(out->phenotype.head.snoutBluntness, 0.32f, s);
            break;
        }
        case CREATURE_TRAIT_HEAD_CASQUE: {
            uint32_t host=0;
            for(size_t j=0;j<out->recipe.bodyPlan.moduleCount;++j)
                if(out->recipe.bodyPlan.modules[j].kind==CREATURE_MODULE_HEAD)host=out->recipe.bodyPlan.modules[j].instanceId;
            if(!host)return false;
            size_t index=out->phenotype.ornamentCount++;
            out->phenotype.ornaments[index]=(OrnamentPhenotype){.archetype=ORNAMENT_ARCHETYPE_CREST,
                .length=.75f*s,.baseRadius=.38f,.tipRadius=.075f,.development=1};
            CreatureModuleInstance crest={.instanceId=CreatureRecipe_NextFreeModuleId(&out->recipe),
                .kind=CREATURE_MODULE_ORNAMENT,.attachment=AttachmentRef_Create(host,HEAD_SLOT_CRANIAL_DORSAL),.phenotypeIndex=(unsigned)index};
            out->recipe.bodyPlan.modules[out->recipe.bodyPlan.moduleCount++]=crest;
            out->phenotype.headEnvelope.heightScale *= Math_Lerp(1, 1.65f, s);
            out->phenotype.head.skullHeight = Math_Lerp(out->phenotype.head.skullHeight, 0.90f, s);
            out->phenotype.head.browProminence = Math_Lerp(out->phenotype.head.browProminence, 0.90f, s);
            out->phenotype.head.temporalDepth = Math_Lerp(out->phenotype.head.temporalDepth, 0.90f, s);
            out->phenotype.head.temporalWidth = Math_Lerp(out->phenotype.head.temporalWidth, 0.90f, s);
            break;
        }
        case CREATURE_TRAIT_HEAD_BROAD_TRIANGULAR: {
            /* La anchura posterior y el volumen craneal son independientes:
             * triangular describe la transición facial, no un achatamiento. */
            float width = t->paramA > 0.0f ? Math_Clamp(t->paramA, 1.05f, 1.80f) : 1.15f;
            float height = t->paramB > 0.0f ? Math_Clamp(t->paramB, .55f, 1.80f) : 1.35f;
            out->phenotype.headEnvelope.widthScale *= Math_Lerp(1.0f, width, s);
            out->phenotype.headEnvelope.heightScale *= Math_Lerp(1.0f, height, s);
            out->phenotype.headEnvelope.lengthScale *= Math_Lerp(1.0f, .94f, s);
            out->phenotype.head.skullWidth = Math_Lerp(out->phenotype.head.skullWidth, .80f, s);
            out->phenotype.head.skullHeight = Math_Lerp(out->phenotype.head.skullHeight, .75f, s);
            out->phenotype.head.temporalWidth = Math_Lerp(out->phenotype.head.temporalWidth, .65f, s);
            out->phenotype.head.temporalDepth = Math_Lerp(out->phenotype.head.temporalDepth, .70f, s);
            out->phenotype.head.muzzleLength = Math_Lerp(out->phenotype.head.muzzleLength, .32f, s);
            out->phenotype.head.muzzleWidth = Math_Lerp(out->phenotype.head.muzzleWidth, .38f, s);
            out->phenotype.head.muzzleTaper = Math_Lerp(out->phenotype.head.muzzleTaper, .86f, s);
            out->phenotype.head.snoutBluntness = Math_Lerp(out->phenotype.head.snoutBluntness, .30f, s);
            out->phenotype.head.browProminence = Math_Lerp(out->phenotype.head.browProminence, .62f, s);
            out->phenotype.head.cheekMass = Math_Lerp(out->phenotype.head.cheekMass, .35f, s);
            out->phenotype.head.jawDepth = Math_Lerp(out->phenotype.head.jawDepth, .42f, s);
            out->phenotype.head.jawStrength = Math_Lerp(out->phenotype.head.jawStrength, .48f, s);
            out->phenotype.head.eyeLaterality = Math_Lerp(out->phenotype.head.eyeLaterality, .55f, s);
            out->phenotype.head.eyeDorsality = Math_Lerp(out->phenotype.head.eyeDorsality, .80f, s);
            out->phenotype.head.eyeExposure = Math_Lerp(out->phenotype.head.eyeExposure, .10f, s);
            break;
        }

        case CREATURE_TRAIT_EYE_APERTURE:
            out->phenotype.head.eyeCompression = Math_Lerp(out->phenotype.head.eyeCompression, Math_Clamp01(t->paramA), s);
            break;
        case CREATURE_TRAIT_EYE_PALETTE:
            out->phenotype.eyes.irisColor = Color_Lerp(out->phenotype.eyes.irisColor, t->colorA, s);
            out->phenotype.eyes.scleraColor = Color_Lerp(out->phenotype.eyes.scleraColor, t->colorB, s);
            break;
        /* Sistema Ocular */
        case CREATURE_TRAIT_EYES_SMALL: {
            /* Reducir órbita y globo juntos evita un ojo diminuto dentro de
             * una cavidad negra que conserva el tamaño del ojo grande. */
            out->phenotype.head.eyeSize = Math_Lerp(out->phenotype.head.eyeSize, .18f, s);
            out->phenotype.eyes.size = Math_Lerp(out->phenotype.eyes.size, .18f, s);
            out->phenotype.eyes.protrusion = Math_Lerp(out->phenotype.eyes.protrusion, 0.55f, s);
            break;
        }
        case CREATURE_TRAIT_EYES_LARGE: {
            out->phenotype.eyes.size = Math_Lerp(out->phenotype.eyes.size, 1.55f, s);
            break;
        }
        case CREATURE_TRAIT_EYES_PROTRUDING: {
            out->phenotype.eyes.protrusion = Math_Lerp(out->phenotype.eyes.protrusion, 1.75f, s);
            break;
        }
        case CREATURE_TRAIT_PUPIL_ROUND: {
            out->phenotype.eyes.pupilShape = PUPIL_ROUND;
            out->phenotype.eyes.pupilAspect = 1.0f;
            break;
        }
        case CREATURE_TRAIT_PUPIL_VERTICAL: {
            out->phenotype.eyes.pupilShape = PUPIL_VERTICAL;
            out->phenotype.eyes.pupilAspect = Math_Lerp(0.35f, 0.16f, s);
            break;
        }
        case CREATURE_TRAIT_PUPIL_HORIZONTAL: {
            out->phenotype.eyes.pupilShape = PUPIL_HORIZONTAL;
            out->phenotype.eyes.pupilAspect = Math_Lerp(0.35f, 0.18f, s);
            break;
        }
        case CREATURE_TRAIT_PUPIL_DIAMOND: {
            out->phenotype.eyes.pupilShape = PUPIL_DIAMOND;
            out->phenotype.eyes.pupilAspect = Math_Lerp(0.65f, 0.48f, s);
            break;
        }

        /* Extremidades */
        case CREATURE_TRAIT_LIMBS_LONG: {
            for (size_t l = 0; l < out->phenotype.limbCount; ++l) {
                LimbRole role = out->phenotype.limbs[l].role;
                if ((t->limbTarget == CREATURE_LIMBS_FORE && role != LIMB_FORE) ||
                    (t->limbTarget == CREATURE_LIMBS_HIND && role != LIMB_HIND)) continue;
                out->phenotype.limbs[l].length *= Math_Lerp(1.0f, 1.35f, s);
            }
            break;
        }
        case CREATURE_TRAIT_LIMBS_SHORT: {
            for (size_t l = 0; l < out->phenotype.limbCount; ++l) {
                LimbRole role = out->phenotype.limbs[l].role;
                if ((t->limbTarget == CREATURE_LIMBS_FORE && role != LIMB_FORE) ||
                    (t->limbTarget == CREATURE_LIMBS_HIND && role != LIMB_HIND)) continue;
                out->phenotype.limbs[l].length *= Math_Lerp(1.0f, 0.62f, s);
            }
            break;
        }
        case CREATURE_TRAIT_LIMBS_ROBUST: {
            for (size_t l = 0; l < out->phenotype.limbCount; ++l) {
                LimbRole role = out->phenotype.limbs[l].role;
                if ((t->limbTarget == CREATURE_LIMBS_FORE && role != LIMB_FORE) ||
                    (t->limbTarget == CREATURE_LIMBS_HIND && role != LIMB_HIND)) continue;
                out->phenotype.limbs[l].thickness *= Math_Lerp(1.0f, 1.48f, s);
            }
            break;
        }
        case CREATURE_TRAIT_LIMBS_ARBOREAL: {
            for (size_t l = 0; l < out->phenotype.limbCount; ++l) {
                LimbRole role = out->phenotype.limbs[l].role;
                if ((t->limbTarget == CREATURE_LIMBS_FORE && role != LIMB_FORE) ||
                    (t->limbTarget == CREATURE_LIMBS_HIND && role != LIMB_HIND)) continue;
                LimbPhenotype* limb = &out->phenotype.limbs[l];
                limb->length *= Math_Lerp(1.0f, 1.22f, s);
                limb->thickness *= Math_Lerp(1.0f, 0.62f, s);
                limb->footScale *= Math_Lerp(1.0f, 1.70f, s);
                limb->digitSpread *= Math_Lerp(1.0f, 1.45f, s);
                for (unsigned d = 0; d < limb->digitCount; ++d) {
                    limb->digitLengths[d] *= Math_Lerp(1.0f, 1.32f, s);
                }
            }
            break;
        }
        case CREATURE_TRAIT_LIMBS_CURSORIAL: {
            for (size_t l = 0; l < out->phenotype.limbCount; ++l) {
                LimbRole role = out->phenotype.limbs[l].role;
                if ((t->limbTarget == CREATURE_LIMBS_FORE && role != LIMB_FORE) ||
                    (t->limbTarget == CREATURE_LIMBS_HIND && role != LIMB_HIND)) continue;
                LimbPhenotype* limb = &out->phenotype.limbs[l];
                if (limb->role == LIMB_HIND) {
                    limb->length *= Math_Lerp(1.0f, 1.95f, s);
                    limb->middleScale *= Math_Lerp(1.0f, 1.18f, s);
                    limb->distalScale *= Math_Lerp(1.0f, 1.25f, s);
                } else {
                    limb->length *= Math_Lerp(1.0f, 0.65f, s);
                }
                limb->thickness *= Math_Lerp(1.0f, 0.85f, s);
                limb->footScale *= Math_Lerp(1.0f, 0.88f, s);
            }
            break;
        }
        case CREATURE_TRAIT_LIMBS_DIGGING: {
            for (size_t l = 0; l < out->phenotype.limbCount; ++l) {
                LimbRole role = out->phenotype.limbs[l].role;
                if ((t->limbTarget == CREATURE_LIMBS_FORE && role != LIMB_FORE) ||
                    (t->limbTarget == CREATURE_LIMBS_HIND && role != LIMB_HIND)) continue;
                LimbPhenotype* limb = &out->phenotype.limbs[l];
                if (limb->role == LIMB_FORE) {
                    limb->length *= Math_Lerp(1.0f, 0.65f, s);
                    limb->thickness *= Math_Lerp(1.0f, 1.90f, s);
                    limb->footScale *= Math_Lerp(1.0f, 1.45f, s);
                    for (unsigned d = 0; d < limb->digitCount; ++d) {
                        limb->digitLengths[d] *= Math_Lerp(1.0f, 0.85f, s);
                    }
                } else {
                    limb->length *= Math_Lerp(1.0f, 0.62f, s);
                    limb->thickness *= Math_Lerp(1.0f, 1.25f, s);
                }
            }
            break;
        }

        case CREATURE_TRAIT_FEET_LARGE: {
            for(size_t l=0;l<out->phenotype.limbCount;++l) {
                LimbPhenotype* limb=&out->phenotype.limbs[l];
                if((t->limbTarget==CREATURE_LIMBS_FORE && limb->role!=LIMB_FORE) ||
                   (t->limbTarget==CREATURE_LIMBS_HIND && limb->role!=LIMB_HIND))continue;
                limb->footScale*=Math_Lerp(1,1.60f,s);
                limb->distalScale*=Math_Lerp(1,1.25f,s);
            }
            break;
        }
        case CREATURE_TRAIT_LIMBS_GRACILE: {
            for (size_t l = 0; l < out->phenotype.limbCount; ++l) {
                LimbRole role = out->phenotype.limbs[l].role;
                if ((t->limbTarget == CREATURE_LIMBS_FORE && role != LIMB_FORE) ||
                    (t->limbTarget == CREATURE_LIMBS_HIND && role != LIMB_HIND)) continue;
                LimbPhenotype* limb = &out->phenotype.limbs[l];
                float thickMod = (role == LIMB_HIND) ? 0.78f : 0.72f;
                limb->thickness *= Math_Lerp(1.0f, thickMod, s);
                limb->rootThicknessScale = Math_Lerp(limb->rootThicknessScale, 1.00f, s);
                limb->middleThicknessScale = Math_Lerp(limb->middleThicknessScale, 0.85f, s);
                limb->distalThicknessScale = Math_Lerp(limb->distalThicknessScale, 0.72f, s);
                limb->footWidthScale = Math_Lerp(limb->footWidthScale, 1.08f, s);
                limb->footHeightScale = Math_Lerp(limb->footHeightScale, 0.95f, s);
                limb->digitThicknessScale = Math_Lerp(limb->digitThicknessScale, 1.25f, s);
                limb->footScale = Math_Lerp(limb->footScale, 1.00f, s);
                limb->attachmentInset = Math_Lerp(limb->attachmentInset, 0.45f, s);
            }
            break;
        }
        case CREATURE_TRAIT_FEET_NARROW: {
            for (size_t l=0; l<out->phenotype.limbCount; ++l) {
                LimbPhenotype* limb = &out->phenotype.limbs[l];
                if ((t->limbTarget==CREATURE_LIMBS_FORE && limb->role!=LIMB_FORE) ||
                    (t->limbTarget==CREATURE_LIMBS_HIND && limb->role!=LIMB_HIND)) continue;
                limb->footWidthScale *= Math_Lerp(1, .65f, s);
                limb->footHeightScale *= Math_Lerp(1, .70f, s);
                limb->digitThicknessScale *= Math_Lerp(1, .64f, s);
                limb->digitSpread *= Math_Lerp(1, .78f, s);
                for (unsigned d=0; d<limb->digitCount; ++d)
                    limb->digitLengths[d] *= Math_Lerp(1, 1.12f, s);
            }
            break;
        }
        case CREATURE_TRAIT_LIMBS_RETRACTED: {
            for (size_t l=0; l<out->phenotype.limbCount; ++l) {
                LimbPhenotype* limb = &out->phenotype.limbs[l];
                if ((t->limbTarget==CREATURE_LIMBS_FORE && limb->role!=LIMB_FORE) ||
                    (t->limbTarget==CREATURE_LIMBS_HIND && limb->role!=LIMB_HIND)) continue;
                limb->proximalSweep = Math_Lerp(limb->proximalSweep, -.34f, s);
                limb->distalSweep = Math_Lerp(limb->distalSweep, -.17f, s);
                limb->footYaw = Math_Lerp(limb->footYaw, -.40f, s);
            }
            break;
        }
        case CREATURE_TRAIT_TAIL_CURVED:
            for (size_t k=0; k<out->phenotype.tailCount; ++k) {
                out->phenotype.tails[k].curvature = Math_Lerp(out->phenotype.tails[k].curvature, t->paramA, s);
                out->phenotype.tails[k].segmentCount = 14;
            }
            break;
        /* Arquetipos Caudales */
        case CREATURE_TRAIT_TAIL_TAPERED: {
            if (out->phenotype.tailCount > 0) {
                out->phenotype.tails[0].archetype = TAIL_ARCHETYPE_TAPERED;
                if (t->paramA > 0) out->phenotype.tails[0].length *= Math_Lerp(1.0f, t->paramA, s);
                if (t->paramB > 0) out->phenotype.tails[0].rootMatchStrength = Math_Lerp(out->phenotype.tails[0].rootMatchStrength, t->paramB, s);
                out->phenotype.tails[0].segmentCount = 6;
            }
            break;
        }
        case CREATURE_TRAIT_TAIL_WHIPPED: {
            if (out->phenotype.tailCount > 0) {
                out->phenotype.tails[0].archetype = TAIL_ARCHETYPE_WHIP;
                out->phenotype.tails[0].length *= Math_Lerp(1.0f, 1.85f, s);
                out->phenotype.tails[0].taperCurve = Math_Lerp(1.35f, 2.3f, s);
                out->phenotype.tails[0].segmentCount = 14;
            }
            break;
        }
        case CREATURE_TRAIT_TAIL_HEAVY: {
            if (out->phenotype.tailCount > 0) {
                out->phenotype.tails[0].archetype = TAIL_ARCHETYPE_HEAVY;
                out->phenotype.tails[0].length *= Math_Lerp(1.0f, 0.72f, s);
                out->phenotype.tails[0].baseWidth *= Math_Lerp(1.0f, 1.65f, s);
                out->phenotype.tails[0].baseHeight *= Math_Lerp(1.0f, 1.45f, s);
                out->phenotype.tails[0].segmentCount = 10;
            }
            break;
        }
        case CREATURE_TRAIT_TAIL_PREHENSILE: {
            if (out->phenotype.tailCount > 0) {
                out->phenotype.tails[0].archetype = TAIL_ARCHETYPE_PREHENSILE;
                out->phenotype.tails[0].length *= Math_Lerp(1.0f, 1.28f, s);
                out->phenotype.tails[0].curvature = Math_Lerp(out->phenotype.tails[0].curvature, 1.85f, s);
                out->phenotype.tails[0].segmentCount = 14;
            }
            break;
        }
        case CREATURE_TRAIT_TAIL_FINNED: {
            if (out->phenotype.tailCount > 0) {
                /* La aleta caudal pertenece a la envolvente de la cola. Añadir
                 * otra Membrane_Instantiate aquí superponía una segunda lámina
                 * de estaciones discretas y producía triángulos sobre la aleta
                 * de natación. */
                out->phenotype.tails[0].archetype = TAIL_ARCHETYPE_FINNED;
                out->phenotype.tails[0].baseHeight *= Math_Lerp(1.0f, 1.15f, s);
                out->phenotype.tails[0].baseWidth *= Math_Lerp(1.0f, 0.55f, s);
                out->phenotype.tails[0].segmentCount = 12;
            }
            break;
        }

        /* Cobertura Superficial y Placas */
        case CREATURE_TRAIT_FINE_SCALES: {
            out->phenotype.surface.integument.type = INTEGUMENT_SCALES;
            out->phenotype.surface.integument.coverage = 1.0f;
            out->phenotype.surface.integument.scales.size = Math_Lerp(0.12f, 0.045f, s);
            out->phenotype.surface.integument.scales.relief = Math_Lerp(0.012f, 0.006f, s);
            break;
        }
        case CREATURE_TRAIT_HEAVY_SCALES: {
            out->phenotype.surface.integument.type = INTEGUMENT_SCALES;
            out->phenotype.surface.integument.coverage = 1.0f;
            out->phenotype.surface.integument.scales.size = Math_Lerp(0.12f, 0.20f, s);
            out->phenotype.surface.integument.scales.relief = Math_Lerp(0.012f, 0.028f, s);
            out->phenotype.surface.integument.scales.keelStrength = Math_Lerp(0.0f, 1.0f, s);
            break;
        }
        case CREATURE_TRAIT_PLATES: {
            out->phenotype.surface.integument.type = INTEGUMENT_PLATES;
            out->phenotype.surface.integument.coverage = 1.0f;
            out->phenotype.surface.integument.plates.size = Math_Lerp(0.20f, 0.45f, s);
            out->phenotype.surface.integument.plates.relief = Math_Lerp(0.02f, 0.055f, s);
            out->phenotype.surface.integument.plates.bevel = 0.40f;
            out->phenotype.surface.integument.plates.thickness = 0.05f;
            out->phenotype.surface.integument.scales.size = 0.35f;
            out->phenotype.surface.integument.scales.relief = 0.05f;
            AnatomyGraph graph;
            if (!Creature_ResolveAnatomy(&out->recipe, &out->phenotype, &graph)) return false;
            uint32_t host = out->recipe.bodyPlan.root.moduleInstanceId;
            AttachmentPath path = AttachmentPath_FromAxialDorsal(&graph, host);
            OrnamentArray plates = {.ornament = {.archetype=ORNAMENT_ARCHETYPE_PLATE,
                .length=.22f*s, .baseRadius=.70f, .tipRadius=.45f, .development=1},
                .count=5, .pathStart=.12f, .pathEnd=.90f, .sizeStart=.7f, .sizePeak=1, .sizeEnd=.7f};
            if (!OrnamentArray_Instantiate(&plates,&path,0,AXIAL_SPRAWLING_SLOT_DORSAL_ARRAY_BASE,host,
                &out->recipe,&out->phenotype)) return false;
            break;
        }

        /* Ornamentos y Estructuras Distribuidas */
        case CREATURE_TRAIT_HORNS: {
            if (out->recipe.bodyPlan.moduleCount + 2 <= CREATURE_MAX_MODULES &&
                out->phenotype.ornamentCount + 2 <= CREATURE_MAX_ORNAMENTS) {
                float hornLen = (t->paramA > 0.0f ? t->paramA : 0.65f) * s;
                float hornBaseR = (t->paramB > 0.0f ? t->paramB : 0.15f) * s;

                OrnamentPhenotype hornL = {
                    .archetype = ORNAMENT_ARCHETYPE_HORN,
                    .length = hornLen,
                    .baseRadius = hornBaseR,
                    .tipRadius = hornBaseR * 0.28f,
                    .curvature = 0.25f,
                    .development = 1.0f
                };
                OrnamentPhenotype hornR = hornL;

                size_t idxL = out->phenotype.ornamentCount++;
                size_t idxR = out->phenotype.ornamentCount++;
                out->phenotype.ornaments[idxL] = hornL;
                out->phenotype.ornaments[idxR] = hornR;

                uint32_t headHost = 0;
                for (size_t j = 0; j < out->recipe.bodyPlan.moduleCount; ++j)
                    if (out->recipe.bodyPlan.modules[j].kind == CREATURE_MODULE_HEAD)
                        headHost = out->recipe.bodyPlan.modules[j].instanceId;
                if (!headHost) return false;
                CreatureModuleInstance modL = {
                    .instanceId = CreatureRecipe_NextFreeModuleId(&out->recipe),
                    .kind = CREATURE_MODULE_ORNAMENT,
                    .attachment = AttachmentRef_Create(headHost, HEAD_SLOT_HORN_LEFT),
                    .phenotypeIndex = (unsigned)idxL
                };
                CreatureModuleInstance modR = {
                    .instanceId = 0,
                    .kind = CREATURE_MODULE_ORNAMENT,
                    .attachment = AttachmentRef_Create(headHost, HEAD_SLOT_HORN_RIGHT),
                    .phenotypeIndex = (unsigned)idxR
                };

                out->recipe.bodyPlan.modules[out->recipe.bodyPlan.moduleCount++] = modL;
                modR.instanceId = CreatureRecipe_NextFreeModuleId(&out->recipe);
                out->recipe.bodyPlan.modules[out->recipe.bodyPlan.moduleCount++] = modR;
            }
            break;
        }
        case CREATURE_TRAIT_DORSAL_SPINES: {
            unsigned count = (t->paramA > 0.0f ? (unsigned)t->paramA : 8);
            if (count > 12) count = 12;
            OrnamentArray spines;
            spines.ornament.archetype = ORNAMENT_ARCHETYPE_SPINE;
            spines.ornament.length = (t->paramB > 0.0f ? t->paramB : 0.48f) * s;
            spines.ornament.baseRadius = 0.10f * s;
            spines.ornament.tipRadius = 0.02f;
            spines.ornament.curvature = 0.0f;
            spines.ornament.development = 1.0f;
            spines.count = count;
            spines.pathStart = 0.10f;
            spines.pathEnd = 0.90f;
            spines.sizeStart = 0.50f;
            spines.sizePeak = 1.0f;
            spines.sizeEnd = 0.45f;
            spines.lateralOffset = 0.0f;

            AnatomyGraph graph;
            if (!Creature_ResolveAnatomy(&out->recipe, &out->phenotype, &graph)) return false;
            uint32_t host = out->recipe.bodyPlan.root.moduleInstanceId;
            AttachmentPath path = AttachmentPath_FromAxialDorsal(&graph, host);
            if (!OrnamentArray_Instantiate(&spines, &path, 0, AXIAL_SPRAWLING_SLOT_DORSAL_ARRAY_BASE, host,
                                     &out->recipe, &out->phenotype)) return false;
            break;
        }
        case CREATURE_TRAIT_DORSAL_SAIL: {
            unsigned stationCount = (t->paramA > 0.0f ? (unsigned)t->paramA : 10);
            if (stationCount > 14) stationCount = 14;
            MembranePhenotype sail;
            sail.height = (t->paramB > 0.0f ? t->paramB : 1.25f) * s;
            sail.thickness = 0.045f * s;
            sail.startT = 0.05f;
            sail.endT = 0.95f;
            sail.peakT = 0.45f;
            sail.transparency = 0.0f;

            AnatomyGraph graph;
            if (!Creature_ResolveAnatomy(&out->recipe, &out->phenotype, &graph)) return false;
            uint32_t host = out->recipe.bodyPlan.root.moduleInstanceId;
            AttachmentPath path = AttachmentPath_FromAxialDorsal(&graph, host);
            if (!Membrane_Instantiate(&sail, &path, 0, AXIAL_SPRAWLING_SLOT_DORSAL_ARRAY_BASE, host,
                                stationCount, &out->recipe, &out->phenotype)) return false;
            break;
        }
        case CREATURE_TRAIT_CRANIAL_CROWN: {
            if (out->recipe.bodyPlan.moduleCount + 6 <= CREATURE_MAX_MODULES &&
                out->phenotype.ornamentCount + 6 <= CREATURE_MAX_ORNAMENTS) {
                uint32_t headHost = 0;
                for (size_t j = 0; j < out->recipe.bodyPlan.moduleCount; ++j) {
                    if (out->recipe.bodyPlan.modules[j].kind == CREATURE_MODULE_HEAD) {
                        headHost = out->recipe.bodyPlan.modules[j].instanceId;
                        break;
                    }
                }
                if (!headHost) return false;

                float baseLen = (t->paramA > 0.0f ? t->paramA : 0.60f) * s;
                float baseR = (t->paramB > 0.0f ? t->paramB : 0.13f) * s;

                const float lenScales[3] = {1.0f, 0.70f, 0.45f};
                const float radScales[3] = {1.0f, 0.78f, 0.62f};
                const AttachmentSlotId slotPairs[3][2] = {
                    {HEAD_SLOT_OCCIPITAL_LEFT, HEAD_SLOT_OCCIPITAL_RIGHT},
                    {HEAD_SLOT_POSTEROLATERAL_LEFT, HEAD_SLOT_POSTEROLATERAL_RIGHT},
                    {HEAD_SLOT_TEMPORAL_LEFT, HEAD_SLOT_TEMPORAL_RIGHT}
                };

                for (unsigned pair = 0; pair < 3; ++pair) {
                    float hornLen = baseLen * lenScales[pair];
                    float hornRad = baseR * radScales[pair];
                    OrnamentPhenotype horn = {
                        .archetype = ORNAMENT_ARCHETYPE_HORN,
                        .length = hornLen,
                        .baseRadius = hornRad,
                        .tipRadius = hornRad * 0.28f,
                        .curvature = 0.20f,
                        .development = 1.0f
                    };
                    for (unsigned side = 0; side < 2; ++side) {
                        size_t idx = out->phenotype.ornamentCount++;
                        out->phenotype.ornaments[idx] = horn;
                        CreatureModuleInstance mod = {
                            .instanceId = CreatureRecipe_NextFreeModuleId(&out->recipe),
                            .kind = CREATURE_MODULE_ORNAMENT,
                            .attachment = AttachmentRef_Create(headHost, slotPairs[pair][side]),
                            .phenotypeIndex = (unsigned)idx
                        };
                        out->recipe.bodyPlan.modules[out->recipe.bodyPlan.moduleCount++] = mod;
                    }
                }
            }
            break;
        }
        case CREATURE_TRAIT_LATERAL_SPINES: {
            unsigned countPerSide = (t->paramA > 0.0f ? (unsigned)t->paramA : 6);
            if (countPerSide > 10) countPerSide = 10;
            if (countPerSide < 2) countPerSide = 2;
            if (out->recipe.bodyPlan.moduleCount + countPerSide * 2 > CREATURE_MAX_MODULES ||
                out->phenotype.ornamentCount + countPerSide * 2 > CREATURE_MAX_ORNAMENTS) {
                return false;
            }

            AnatomyGraph graph;
            if (!Creature_ResolveAnatomy(&out->recipe, &out->phenotype, &graph)) return false;
            uint32_t host = out->recipe.bodyPlan.root.moduleInstanceId;

            float spineLen = (t->paramB > 0.0f ? t->paramB : 0.24f) * s;
            float spineRad = 0.075f * s;

            OrnamentArray fringe;
            fringe.ornament.archetype = ORNAMENT_ARCHETYPE_SPINE;
            fringe.ornament.length = spineLen;
            fringe.ornament.baseRadius = spineRad;
            fringe.ornament.tipRadius = 0.015f;
            fringe.ornament.curvature = 0.0f;
            fringe.ornament.development = 1.0f;
            fringe.count = countPerSide;
            fringe.pathStart = 0.12f;
            fringe.pathEnd = 0.88f;
            fringe.sizeStart = 0.55f;
            fringe.sizePeak = 1.0f;
            fringe.sizeEnd = 0.50f;
            fringe.lateralOffset = 0.0f;

            AttachmentPath pathL = AttachmentPath_FromAxialLateral(&graph, host, true);
            if (!OrnamentArray_Instantiate(&fringe, &pathL, 0, 1, host, &out->recipe, &out->phenotype)) return false;

            AttachmentPath pathR = AttachmentPath_FromAxialLateral(&graph, host, false);
            if (!OrnamentArray_Instantiate(&fringe, &pathR, 0, 1, host, &out->recipe, &out->phenotype)) return false;

            break;
        }

        case CREATURE_TRAIT_ORNAMENT_FIELD: {
            uint32_t host = 0;
            unsigned index = 0;
            for (size_t j = 0; j < out->recipe.bodyPlan.moduleCount; ++j) {
                const CreatureModuleInstance* module = &out->recipe.bodyPlan.modules[j];
                if (module->kind == t->hostKind && index++ == t->hostIndex) {
                    host = module->instanceId;
                    break;
                }
            }
            if (!host) return false;
            AnatomyGraph graph;
            if (!Creature_ResolveAnatomy(&out->recipe, &out->phenotype, &graph)) return false;
            AttachmentPath path = t->hostKind == CREATURE_MODULE_TAIL ?
                AttachmentPath_FromTail(&graph, host) : AttachmentPath_FromAxialDorsal(&graph, host);
            OrnamentField field = t->field;
            field.seed ^= variation->seed;
            field.row.ornament.development *= s;
            if (!OrnamentField_Instantiate(&field, &path, host, &out->recipe, &out->phenotype)) return false;
            break;
        }

        /* Color y Pigmentación */
        case CREATURE_TRAIT_COLOR_PALETTE: {
            out->phenotype.dorsalColor = t->colorA;
            out->phenotype.ventralColor = t->colorB;
            out->phenotype.surface.pigment.baseColor = t->colorA;
            out->phenotype.surface.pigment.ventralColor = t->colorB;
            break;
        }
        case CREATURE_TRAIT_PATTERN_LAYER: {
            if (out->phenotype.surface.pigment.layerCount < SURFACE_MAX_PIGMENT_LAYERS) {
                PigmentLayer l;
                l.pattern = (PigmentPattern)(int)t->paramA;
                l.color = t->colorA;
                l.strength = s;
                l.scale = t->paramB > 0.0f ? t->paramB : 1.0f;
                l.sharpness = 0.65f;
                l.direction = Vec3_Create(0.0f, -1.0f, 0.0f);
                l.seed = variation->seed + 31u * (uint32_t)out->phenotype.surface.pigment.layerCount;

                out->phenotype.surface.pigment.layers[out->phenotype.surface.pigment.layerCount++] = l;
            }
            break;
        }

        default:
            break;
        }
    }

    CreaturePhenotype_Normalize(&out->phenotype);
    return CreatureRecipe_Validate(&out->recipe, &out->phenotype);
}

bool CreatureVariation_Apply(const CreatureRecipe* recipe,const CreaturePhenotype* phenotype,
    const CreatureVariation* variation,CreatureVariant* out) {
    if(!out)return false;
    CreatureVariant candidate;
    if(!CreatureVariation_ApplyInto(recipe,phenotype,variation,&candidate))return false;
    *out=candidate;
    return true;
}
