/**
 * @file VisualVariation.c
 * @brief Implementación pura de perfiles visuales y cubetas morfológicas.
 */

#include "VisualVariation.h"
#include "SurfacePresets.h"
#include <math.h>

static float VisualVariation_ClampFinite(float value, float fallback, float minimum, float maximum) {
    if (!isfinite(value)) return fallback;
    return fmaxf(minimum, fminf(maximum, value));
}

static uint32_t VisualVariation_Hash32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static float VisualVariation_Unit(uint32_t value) {
    return (float)(value >> 8) * (1.0f / 16777216.0f);
}

static float VisualVariation_Maturity(float maturity) {
    return VisualVariation_ClampFinite(maturity, 1.0f, 0.0f, 1.0f);
}

CreatureVisualProfile VisualVariation_Profile(const CreatureGenome* genome, uint32_t entitySeed) {
    CreatureGenome neutral = CreatureGenome_Default();
    const CreatureGenome* source = genome ? genome : &neutral;
    CreatureVisualProfile profile = {0};
    float size = VisualVariation_ClampFinite(source->size, 1.0f, 0.50f, 1.60f);
    float speed = VisualVariation_ClampFinite(source->speed, 1.0f, 0.50f, 1.60f);
    float efficiency = VisualVariation_ClampFinite(source->efficiency, 1.0f, 0.70f, 1.30f);
    float perception = VisualVariation_ClampFinite(source->perception, 1.0f, 0.50f, 1.60f);
    float pigment = VisualVariation_ClampFinite(source->pigment, 0.50f, 0.0f, 1.0f);
    float morphology = VisualVariation_ClampFinite(source->morphology, 1.0f, 0.80f, 1.20f);
    /* morphology se cuantiza de forma determinista en tres intervalos uniformes [0.80,1.20]. */
    float morphologyUnit = (morphology - 0.80f) * 2.50f;
    uint32_t pigmentBits = (uint32_t)floorf(pigment * 65535.0f + 0.5f);
    uint32_t colorSeed = VisualVariation_Hash32(entitySeed ^
                                                VisualVariation_Hash32(pigmentBits + 0x53454544u));

    profile.bucket = (uint32_t)(morphologyUnit * (float)VISUAL_MORPHOLOGY_BUCKETS);
    if (profile.bucket >= VISUAL_MORPHOLOGY_BUCKETS) profile.bucket = VISUAL_MORPHOLOGY_BUCKETS - 1u;
    /* renderScale queda limitado a [0.50,1.60] para impedir tamaños no físicos. */
    profile.renderScale = size;
    /* gaitCadence queda limitado a [0.50,1.60] para conservar pasos estables. */
    profile.gaitCadence = speed;
    /* postureLean queda limitado a [-1,1] y convierte eficiencia en sesgo postural. */
    profile.postureLean = VisualVariation_ClampFinite((1.0f - efficiency) * 2.5f, 0.0f, -1.0f, 1.0f);
    /* headScanRate queda limitado a [0.50,1.60] para evitar frecuencias extremas. */
    profile.headScanRate = perception;
    profile.pigmentSeed = VisualVariation_Hash32(entitySeed ^ pigmentBits ^ 0xa53c9e17u);

    float primaryWeight = fminf(1.0f, 0.75f * pigment + 0.25f * VisualVariation_Unit(colorSeed));
    float secondaryWeight = fminf(1.0f, 0.65f * (1.0f - pigment) +
                                         0.35f * VisualVariation_Unit(VisualVariation_Hash32(colorSeed)));
    profile.primary = Color_Lerp(Color_FromRGB(48, 78, 42), Color_FromRGB(174, 132, 68), primaryWeight);
    profile.secondary = Color_Lerp(Color_FromRGB(25, 32, 28), Color_FromRGB(112, 58, 42), secondaryWeight);
    return profile;
}

CreatureVariation VisualVariation_BucketVariation(uint32_t bucket, uint32_t pigmentSeed) {
    uint32_t selectedBucket = bucket < VISUAL_MORPHOLOGY_BUCKETS ? bucket : VISUAL_MORPHOLOGY_BUCKETS - 1u;
    const char* name = selectedBucket == 0u ? "Robusto compacto" :
                       selectedBucket == 1u ? "Esbelto" : "Alto alargado";
    CreatureVariation variation = CreatureVariation_Create(name, pigmentSeed);
    CreatureTrait trait = {0};

    switch (selectedBucket) {
    case 0u:
        trait.type = CREATURE_TRAIT_BODY_ROBUST;
        trait.strength = 0.82f;
        CreatureVariation_AddTrait(&variation, trait);
        trait = (CreatureTrait){0};
        trait.type = CREATURE_TRAIT_BODY_COMPACT;
        trait.strength = 0.76f;
        trait.paramA = 0.58f;
        CreatureVariation_AddTrait(&variation, trait);
        break;
    case 1u:
        trait.type = CREATURE_TRAIT_BODY_SLENDER;
        trait.strength = 0.86f;
        CreatureVariation_AddTrait(&variation, trait);
        break;
    case 2u:
    default:
        trait.type = CREATURE_TRAIT_BODY_ELONGATED;
        trait.strength = 0.84f;
        CreatureVariation_AddTrait(&variation, trait);
        trait = (CreatureTrait){0};
        trait.type = CREATURE_TRAIT_LIMBS_LONG;
        trait.limbTarget = CREATURE_LIMBS_ALL;
        trait.strength = 0.74f;
        CreatureVariation_AddTrait(&variation, trait);
        break;
    }
    return variation;
}

bool VisualVariation_ApplyBucket(const CreatureRecipe* baseRecipe, const CreaturePhenotype* basePhenotype,
                                 uint32_t bucket, uint32_t pigmentSeed,
                                 CreatureRecipe* outRecipe, CreaturePhenotype* outPhenotype) {
    if (!baseRecipe || !basePhenotype || !outRecipe || !outPhenotype) return false;

    if (baseRecipe->id == 3u || baseRecipe->id == 4u) {
        /* La variación hereditaria conserva la silueta; las variantes extremas
         * del catálogo son mutaciones de diseño, no diferencias individuales. */
        float t=(float)(bucket<VISUAL_MORPHOLOGY_BUCKETS?bucket:1u)-1.0f;
        *outRecipe=*baseRecipe;
        *outPhenotype=*basePhenotype;
        outPhenotype->axial.trunkLength*=1.0f+.065f*t;
        outPhenotype->axial.thoraxWidth*=1.0f-.055f*t;
        outPhenotype->axial.shoulderWidth*=1.0f-.045f*t;
        outPhenotype->head.muzzleWidth*=1.0f-.035f*t;
        for(unsigned i=0;i<outPhenotype->limbCount;++i) {
            outPhenotype->limbs[i].length*=1.0f+.035f*t;
            outPhenotype->limbs[i].thickness*=1.0f-.045f*t;
        }
        for(unsigned i=0;i<outPhenotype->ornamentCount;++i)outPhenotype->ornaments[i].length*=1.0f+.08f*t;
        for(unsigned i=0;i<outPhenotype->tailCount;++i)outPhenotype->tails[i].length*=1.0f+.045f*t;
        CreaturePhenotype_Normalize(outPhenotype);
        return CreatureRecipe_Validate(outRecipe,outPhenotype);
    }

    CreatureVariation source = VisualVariation_BucketVariation(bucket, pigmentSeed);
    CreatureVariation compatible = CreatureVariation_Create(source.name, source.seed);
    for (size_t i = 0; i < source.traitCount; ++i) {
        if (CreatureVariation_IsTraitCompatible(&source.traits[i], baseRecipe, basePhenotype) &&
            !CreatureVariation_AddTrait(&compatible, source.traits[i])) {
            return false;
        }
    }

    CreatureVariant result;
    if (!CreatureVariation_Apply(baseRecipe, basePhenotype, &compatible, &result)) return false;
    *outRecipe = result.recipe;
    *outPhenotype = result.phenotype;
    return true;
}

bool VisualVariation_Surface(const CreatureRecipe* recipe, const CreaturePhenotype* phenotype,
                             const CreatureVisualProfile* profile, float maturity,
                             SurfacePhenotype* outSurface) {
    if (!recipe || !phenotype || !profile || !outSurface) return false;

    /* La receta ya contiene cobertura y desarrollo. Un preset elegido por ID
     * borraba la pintura y el pelaje editados en los archivos externos. */
    SurfacePhenotype surface=phenotype->surface;
    float t = VisualVariation_Maturity(maturity);
    {
        float tone=((float)profile->primary.r-100.0f)/400.0f*(.85f+.15f*t);
        Color tint=tone<0?COLOR_BLACK:COLOR_WHITE;
        surface.pigment.baseColor=Color_Lerp(surface.pigment.baseColor,tint,fabsf(tone));
        surface.pigment.secondaryColor=Color_Lerp(surface.pigment.secondaryColor,tint,fabsf(tone)*.6f);
    }
    surface.pigment.seed = profile->pigmentSeed;
    SurfacePhenotype_Normalize(&surface);
    *outSurface = surface;
    return true;
}

uint64_t VisualVariation_CacheKey(uint32_t recipeId, uint32_t stage, uint32_t bucket, uint32_t tier) {
    const uint64_t offset = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    const uint32_t fields[] = {recipeId, stage, bucket, tier};
    uint64_t hash = offset;
    for (size_t field = 0; field < sizeof(fields) / sizeof(fields[0]); ++field) {
        for (unsigned byte = 0; byte < sizeof(fields[field]); ++byte) {
            hash ^= (uint8_t)(fields[field] >> (byte * 8u));
            hash *= prime;
        }
    }
    return hash;
}
