/**
 * @file test_creature_visual_variation.c
 * @brief Pruebas de perfiles visuales y cubetas morfológicas deterministas.
 */

#include "test_utils.h"
#include "VisualVariation.h"
#include "CreatureRecipes.h"
#include <math.h>
#include <stdio.h>

static void TestProfileEqual(const CreatureVisualProfile* a,
                             const CreatureVisualProfile* b) {
    TEST_ASSERT(a->bucket == b->bucket, "La cubeta del perfil es determinista");
    TEST_ASSERT(FLOAT_NEAR(a->renderScale, b->renderScale), "La escala visual es determinista");
    TEST_ASSERT(FLOAT_NEAR(a->gaitCadence, b->gaitCadence), "La cadencia es determinista");
    TEST_ASSERT(FLOAT_NEAR(a->postureLean, b->postureLean), "La inclinación es determinista");
    TEST_ASSERT(FLOAT_NEAR(a->headScanRate, b->headScanRate), "El escaneo cefálico es determinista");
    TEST_ASSERT(a->pigmentSeed == b->pigmentSeed, "La semilla de pigmento es determinista");
    TEST_ASSERT(Color_Equals(a->primary, b->primary), "El color primario es determinista");
    TEST_ASSERT(Color_Equals(a->secondary, b->secondary), "El color secundario es determinista");
}

static uint32_t TestNext(uint32_t* state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static CreatureGenome TestGenome(uint32_t* state) {
    const float unit = (float)(TestNext(state) & 0x00ffffffu) / 16777215.0f;
    const float unit2 = (float)(TestNext(state) & 0x00ffffffu) / 16777215.0f;
    return (CreatureGenome){
        .size = 0.65f + 0.75f * unit,
        .speed = 0.70f + 0.60f * unit2,
        .efficiency = 0.70f + 0.60f * unit,
        .perception = 0.70f + 0.60f * unit2,
        .pigment = unit,
        .morphology = 0.80f + 0.40f * unit2,
        .maturation = 0.80f + 0.40f * unit
    };
}

static void TestDeterminism(void) {
    CreatureGenome genome = {
        .size = 1.17f, .speed = 0.91f, .efficiency = 1.08f,
        .perception = 1.23f, .pigment = 0.63f, .morphology = 1.14f,
        .maturation = 1.01f
    };
    CreatureVisualProfile first = VisualVariation_Profile(&genome, 0x12345678u);
    CreatureVisualProfile second = VisualVariation_Profile(&genome, 0x12345678u);
    TestProfileEqual(&first, &second);

    const CreatureRecipe* recipe = CreatureRecipes_Find(1);
    const CreaturePhenotype* base = CreatureRecipe_GetStage(recipe, CREATURE_STAGE_ADULT);
    CreaturePhenotype firstPhenotype, secondPhenotype;
    CreatureRecipe firstRecipe, secondRecipe;
    TEST_ASSERT(VisualVariation_ApplyBucket(recipe, base, first.bucket, first.pigmentSeed,
                                            &firstRecipe, &firstPhenotype),
                "La primera aplicación del perfil es válida");
    TEST_ASSERT(VisualVariation_ApplyBucket(recipe, base, second.bucket, second.pigmentSeed,
                                            &secondRecipe, &secondPhenotype),
                "La segunda aplicación del perfil es válida");
    TEST_ASSERT(CreaturePhenotype_Fingerprint(&firstPhenotype) ==
                    CreaturePhenotype_Fingerprint(&secondPhenotype),
                "La huella fenotípica de la cubeta es determinista");
}

static void TestBucketsAreBounded(void) {
    uint32_t state = 0x9e3779b9u;
    for (unsigned i = 0; i < 1000; ++i) {
        CreatureGenome genome = TestGenome(&state);
        CreatureVisualProfile profile = VisualVariation_Profile(&genome, state);
        TEST_ASSERT(profile.bucket < VISUAL_MORPHOLOGY_BUCKETS, "La cubeta está dentro del rango");
        TEST_ASSERT(isfinite(profile.renderScale) && profile.renderScale >= 0.5f && profile.renderScale <= 1.6f,
                    "La escala visual está saneada");
        TEST_ASSERT(isfinite(profile.gaitCadence) && profile.gaitCadence >= 0.5f && profile.gaitCadence <= 1.6f,
                    "La cadencia está saneada");
        TEST_ASSERT(isfinite(profile.headScanRate) && profile.headScanRate >= 0.5f && profile.headScanRate <= 1.6f,
                    "El escaneo cefálico está saneado");
        TEST_ASSERT(isfinite(profile.postureLean) && profile.postureLean >= -1.0f && profile.postureLean <= 1.0f,
                    "La inclinación está saneada");
    }
}

static void TestDistinctBuckets(void) {
    const CreatureRecipe* recipes[] = {CreatureRecipes_Find(3), CreatureRecipes_Find(4)};
    for (size_t r = 0; r < sizeof(recipes) / sizeof(recipes[0]); ++r) {
        const CreaturePhenotype* base = CreatureRecipe_GetStage(recipes[r], CREATURE_STAGE_ADULT);
        uint64_t fingerprints[VISUAL_MORPHOLOGY_BUCKETS];
        for (uint32_t bucket = 0; bucket < VISUAL_MORPHOLOGY_BUCKETS; ++bucket) {
            CreatureRecipe variedRecipe;
            CreaturePhenotype variedPhenotype;
            TEST_ASSERT(VisualVariation_ApplyBucket(recipes[r], base, bucket, 0x55667788u,
                                                    &variedRecipe, &variedPhenotype),
                        "Cada cubeta es aplicable a las recetas ganadera y felina");
            fingerprints[bucket] = CreaturePhenotype_Fingerprint(&variedPhenotype);
        }
        TEST_ASSERT(fingerprints[0] != fingerprints[1] && fingerprints[0] != fingerprints[2] &&
                        fingerprints[1] != fingerprints[2],
                    "Las cubetas producen fenotipos distintos");
    }
}

static void TestAllRecipeStages(void) {
    for (uint32_t id = 1; id <= 4; ++id) {
        const CreatureRecipe* recipe = CreatureRecipes_Find(id);
        for (uint32_t stage = CREATURE_STAGE_SEED; stage <= CREATURE_STAGE_ADULT; ++stage) {
            const CreaturePhenotype* base = CreatureRecipe_GetStage(recipe, (CreatureStage)stage);
            for (uint32_t bucket = 0; bucket < VISUAL_MORPHOLOGY_BUCKETS; ++bucket) {
                CreatureRecipe variedRecipe;
                CreaturePhenotype variedPhenotype;
                TEST_ASSERT(VisualVariation_ApplyBucket(recipe, base, bucket,
                                                        0x1000u + id + stage + bucket,
                                                        &variedRecipe, &variedPhenotype),
                            "La cubeta es válida en todas las recetas y etapas");
            }
        }
    }
}

static void TestCacheKeyFields(void) {
    uint64_t base = VisualVariation_CacheKey(3, 1, 2, VISUAL_TIER_NEAR);
    TEST_ASSERT(base != VisualVariation_CacheKey(4, 1, 2, VISUAL_TIER_NEAR),
                "La receta participa en la clave");
    TEST_ASSERT(base != VisualVariation_CacheKey(3, 2, 2, VISUAL_TIER_NEAR),
                "La etapa participa en la clave");
    TEST_ASSERT(base != VisualVariation_CacheKey(3, 1, 1, VISUAL_TIER_NEAR),
                "La cubeta participa en la clave");
    TEST_ASSERT(base != VisualVariation_CacheKey(3, 1, 2, VISUAL_TIER_FAR),
                "El nivel participa en la clave");
}

void run_creature_visual_variation_tests(void) {
    TestDeterminism();
    TestBucketsAreBounded();
    TestDistinctBuckets();
    TestAllRecipeStages();
    TestCacheKeyFields();
    printf("SONDA OK: variación visual determinista y GL-free\n");
}
