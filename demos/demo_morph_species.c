#include "MorphViewCamera.h"
/**
 * @file demo_morph_species.c
 * @brief Demo interactiva de metamorfosis universal X -> Y mediante MonsterAger.
 *
 * Inicia mostrando una criatura aleatoria generada con variaciones compatibles
 * y permite mutarla suavemente hacia cualquier receta del catálogo con las
 * teclas numéricas (1..9), o hacia una nueva criatura aleatoria con (0).
 */

#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureRecipes.h"
#include "CreatureRecipeRegistry.h"
#include "CreatureVariation.h"
#include "GpuSDFSurfaceCache.h"
#include "ArmorScales.h"
#include "Membrane.h"
#include "SupplementaryMesh.h"
#include "MathUtils.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "MonsterVisualAsync.h"
#include "LongHair.h"
#include "OpenGLRenderer.h"
#include "SurfaceMapper.h"
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>
#include <sys/resource.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CATALOG_RECIPES 32
#define MAX_CYCLE_SAMPLES 11
#define MORPH_ZOOM_MIN 0.55f
#define MORPH_ZOOM_MAX 1.85f

typedef enum SurfaceMode {
    SURFACE_MODE_SDF_RAYMARCH = 0,
    SURFACE_MODE_EXACT_SDF,
    SURFACE_MODE_GPU_SDF_DENSE,
    SURFACE_MODE_GPU_SDF_SPARSE,
    SURFACE_MODE_LEGACY_HANDOFF
} SurfaceMode;

typedef struct SpeciesCatalogEntry {
    CreatureRecipeId id;
    const CreatureRecipe* recipe;
    char label[48];
} SpeciesCatalogEntry;

typedef struct DemoOptions {
    float fixedAge;
    bool hasFixedAge;
    float ageSequence[64];
    size_t ageSequenceCount;
    const char* fromSpec;
    const char* toSpec;
    uint32_t seed;
    bool hasSeed;
    bool listOnly;
    const char* capturePrefix;
    const char* validateCyclePrefix;
    int maxFrames;
    int profileWarmup;
    bool profile;
    bool profileSwitch;
    bool profileBackends;
    bool profileUnpaced;
    bool nativeResolution;
    int viewMode;
    bool noMsaa;
    bool noFur;
    bool stressMotion;
    bool chunkWireframe;
    bool fallbackRegionDebug;
    bool ownershipDebug;
    bool handoffDebug;
    bool legacyLag;
    bool gpuTrace;
    bool gpuSignature;
    bool gpuCachedNormals;
    bool validateCompute;
    bool localPatches;
    bool realtime;
    float fixedTimestep;
    unsigned workerDelayMs;
    unsigned chunkSize;
    unsigned gpuBrickCells;
    unsigned gpuRefinement;
    float gpuVoxelStep;
    float referenceMaxStep;
    unsigned referenceSteps;
    int surfaceDebugMode;
    SurfaceMode surfaceMode;
    float initialZoom;
} DemoOptions;

typedef struct MorphRuntime {
    Monster source;
    Monster target;
    Monster reference;
    MonsterAger ager;
    MonsterVisualAsync* visual;
    MonsterVisualEye* eyes;
    size_t eyeCapacity;
    float eyeAge;
    LongHairSet longHair;
    LongHairAsync* longHairAsync;
    float lastHairExactAge;
    size_t lastHairGroupCount;
    bool interactiveScrub;
    double sdfMs, fallbackMs, meshMs, eyeMs;
    GpuSDFSurfaceGrid gpuGrid;
    GpuSDFSurfaceCache* gpuCache;
    bool gpuGridValid;
    MonsterSDF frameSdf;
    Mesh membranes, armor, detail, translucent;
    float fieldAge;
    bool fieldValid;
    bool materialValid;
    bool rayFrameValid;
    ICamera rayCamera;
    int raySurfaceDebug;
    bool rayNoFur, rayWire;
    char sourceLabel[48];
    char targetLabel[48];
} MorphRuntime;

static uint32_t NextRng(uint32_t* state) {
    uint32_t x = *state ? *state : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float NextRng01(uint32_t* state) {
    return (float)(NextRng(state) & 0x00ffffffu) / (float)0x01000000u;
}

static void NormalizeAsciiLabel(const char* src, char* dst, size_t cap) {
    if (!cap) return;
    if (!src || !src[0]) {
        snprintf(dst, cap, "CRIATURA");
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < cap; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '/' || c == ':' || c == '.') {
            dst[j++] = (char)toupper(c);
        } else if (c == 0xc3 && src[i + 1]) {
            /* Simplificar vocales acentuadas UTF-8 habituales a ASCII para DrawTextPanel. */
            unsigned char n = (unsigned char)src[i + 1];
            char rep = ' ';
            if (n == 0xa1 || n == 0x81) rep = 'A';
            else if (n == 0xa9 || n == 0x89) rep = 'E';
            else if (n == 0xad || n == 0x8d) rep = 'I';
            else if (n == 0xb3 || n == 0x93) rep = 'O';
            else if (n == 0xba || n == 0x9a) rep = 'U';
            else if (n == 0xb1 || n == 0x91) rep = 'N';
            if (rep != ' ') {
                dst[j++] = rep;
                ++i;
            }
        }
    }
    dst[j] = '\0';
}

static bool EqualsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static size_t LoadSpeciesCatalog(SpeciesCatalogEntry* out, size_t capacity) {
    char error[256] = {0};
    if (!CreatureRecipeRegistry_LoadDirectory("recipes", error, sizeof(error))) {
        fprintf(stderr, "[AVISO] No se pudo cargar catálogo externo recipes/: %s\n", error);
    }

    size_t count = 0;
    for (CreatureRecipeId id = 1; id <= 4096 && count < capacity; ++id) {
        const CreatureRecipe* r = CreatureRecipes_Find(id);
        if (!r) continue;
        bool duplicate = false;
        for (size_t k = 0; k < count; ++k) {
            if (out[k].id == r->id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        out[count].id = r->id;
        out[count].recipe = r;
        NormalizeAsciiLabel(r->name, out[count].label, sizeof(out[count].label));
        ++count;
    }
    return count;
}

static const SpeciesCatalogEntry* FindCatalogEntry(const SpeciesCatalogEntry* catalog, size_t count, const char* spec) {
    if (!spec || !spec[0]) return NULL;
    if (EqualsIgnoreCase(spec, "dog")) spec = "Perro";
    else if (EqualsIgnoreCase(spec, "lizard")) spec = "Lagarto";
    else if (EqualsIgnoreCase(spec, "horse")) spec = "Caballo";
    else if (EqualsIgnoreCase(spec, "donkey")) spec = "Asno";
    else if (EqualsIgnoreCase(spec, "rhino") || EqualsIgnoreCase(spec, "white-rhino")) spec = "Rinoceronte";
    else if (EqualsIgnoreCase(spec, "rat") || EqualsIgnoreCase(spec, "norway-rat")) spec = "Rata";
    else if (EqualsIgnoreCase(spec, "elephant") || EqualsIgnoreCase(spec, "african-elephant")) spec = "Elefante";
    else if (EqualsIgnoreCase(spec, "pig") || EqualsIgnoreCase(spec, "swine")) spec = "Cerdo";
    else if (EqualsIgnoreCase(spec, "explorer")) spec = "Explorador";
    else if (EqualsIgnoreCase(spec, "giraffe")) spec = "Jirafa";
    else if (EqualsIgnoreCase(spec, "platypus")) spec = "Ornitorrinco";
    else if (EqualsIgnoreCase(spec, "pangolin")) spec = "1013";
    else if (EqualsIgnoreCase(spec, "kangaroo") || EqualsIgnoreCase(spec, "canguro")) spec = "1014";
    else if (EqualsIgnoreCase(spec, "bat") || EqualsIgnoreCase(spec, "murcielago")) spec = "1015";
    else if (EqualsIgnoreCase(spec, "falcon") || EqualsIgnoreCase(spec, "halcon") || EqualsIgnoreCase(spec, "peregrine")) spec = "1016";
    else if (EqualsIgnoreCase(spec, "manta") || EqualsIgnoreCase(spec, "mantarraya") || EqualsIgnoreCase(spec, "ray")) spec = "1017";
    else if (EqualsIgnoreCase(spec, "octopus") || EqualsIgnoreCase(spec, "pulpo")) spec = "1018";
    else if (EqualsIgnoreCase(spec, "dragonfly") || EqualsIgnoreCase(spec, "libelula")) spec = "1019";
    else if (EqualsIgnoreCase(spec, "jellyfish") || EqualsIgnoreCase(spec, "medusa") || EqualsIgnoreCase(spec, "nettle")) spec = "1020";

    char* end = NULL;
    unsigned long num = strtoul(spec, &end, 10);
    if (end && *end == '\0') {
        for (size_t i = 0; i < count; ++i) {
            if (catalog[i].id == (CreatureRecipeId)num) return &catalog[i];
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (EqualsIgnoreCase(catalog[i].label, spec) ||
            (catalog[i].recipe && EqualsIgnoreCase(catalog[i].recipe->name, spec))) {
            return &catalog[i];
        }
    }
    return NULL;
}

static Color RandomHueColor(uint32_t* rng, float minV, float maxV) {
    float r = NextRng01(rng), g = NextRng01(rng), b = NextRng01(rng);
    float s = Math_Lerp(minV, maxV, NextRng01(rng));
    return Color_FromRGB(
        (uint8_t)Math_Clamp((0.25f + 0.75f * r) * s * 255.0f, 18.0f, 245.0f),
        (uint8_t)Math_Clamp((0.25f + 0.75f * g) * s * 255.0f, 18.0f, 245.0f),
        (uint8_t)Math_Clamp((0.25f + 0.75f * b) * s * 255.0f, 18.0f, 245.0f));
}

static bool BuildRandomCreature(uint32_t seed, const SpeciesCatalogEntry* catalog, size_t count,
    Monster* outMonster, char* outLabel, size_t labelCap, size_t* outBaseIdx) {
    if (!catalog || !count || !outMonster) return false;
    uint32_t rng = seed ? seed : 1u;
    size_t baseIdx = (size_t)(NextRng(&rng) % (uint32_t)count);
    if (outBaseIdx) *outBaseIdx = baseIdx;
    const CreatureRecipe* base = catalog[baseIdx].recipe;

    CreatureVariation var = CreatureVariation_Create("Aleatoria", seed);
    const CreatureTraitType bodyPool[] = {
        CREATURE_TRAIT_BODY_SLENDER, CREATURE_TRAIT_BODY_ROBUST,
        CREATURE_TRAIT_BODY_COMPACT, CREATURE_TRAIT_BODY_ELONGATED,
        CREATURE_TRAIT_BODY_FLATTENED, CREATURE_TRAIT_BODY_DEEP
    };
    const CreatureTraitType headPool[] = {
        CREATURE_TRAIT_HEAD_SMALL, CREATURE_TRAIT_HEAD_LARGE,
        CREATURE_TRAIT_HEAD_LONG_SNOUT, CREATURE_TRAIT_HEAD_SHORT_SNOUT,
        CREATURE_TRAIT_HEAD_HEAVY_JAW, CREATURE_TRAIT_HEAD_STREAMLINED
    };
    const CreatureTraitType limbPool[] = {
        CREATURE_TRAIT_LIMBS_LONG, CREATURE_TRAIT_LIMBS_SHORT,
        CREATURE_TRAIT_LIMBS_ROBUST, CREATURE_TRAIT_LIMBS_CURSORIAL,
        CREATURE_TRAIT_LIMBS_GRACILE, CREATURE_TRAIT_FEET_LARGE
    };
    const CreatureTraitType tailPool[] = {
        CREATURE_TRAIT_TAIL_TAPERED, CREATURE_TRAIT_TAIL_WHIPPED,
        CREATURE_TRAIT_TAIL_HEAVY, CREATURE_TRAIT_TAIL_CURVED
    };

    CreatureTrait picks[7];
    memset(picks, 0, sizeof(picks));
    picks[0] = (CreatureTrait){
        .type = bodyPool[NextRng(&rng) % (sizeof(bodyPool) / sizeof(bodyPool[0]))],
        .strength = 0.45f + 0.50f * NextRng01(&rng)
    };
    picks[1] = (CreatureTrait){
        .type = headPool[NextRng(&rng) % (sizeof(headPool) / sizeof(headPool[0]))],
        .strength = 0.40f + 0.55f * NextRng01(&rng)
    };
    picks[2] = (CreatureTrait){
        .type = limbPool[NextRng(&rng) % (sizeof(limbPool) / sizeof(limbPool[0]))],
        .strength = 0.40f + 0.50f * NextRng01(&rng)
    };
    picks[3] = (CreatureTrait){
        .type = tailPool[NextRng(&rng) % (sizeof(tailPool) / sizeof(tailPool[0]))],
        .strength = 0.45f + 0.45f * NextRng01(&rng),
        .paramA = 0.7f + 0.6f * NextRng01(&rng),
        .paramB = 0.2f * NextRng01(&rng)
    };
    picks[4] = (CreatureTrait){
        .type = CREATURE_TRAIT_COLOR_PALETTE,
        .strength = 0.85f,
        .colorA = RandomHueColor(&rng, 0.35f, 0.78f),
        .colorB = RandomHueColor(&rng, 0.60f, 0.95f)
    };
    picks[5] = (CreatureTrait){
        .type = CREATURE_TRAIT_PATTERN_LAYER,
        .strength = 0.45f + 0.45f * NextRng01(&rng),
        .paramA = (float)(NextRng(&rng) % 4u),
        .paramB = 0.35f + 0.45f * NextRng01(&rng),
        .colorA = RandomHueColor(&rng, 0.18f, 0.55f)
    };

    bool addOrnaments = (NextRng(&rng) % 3u) != 0u;
    size_t pickCount = 6;
    if (addOrnaments && base->bodyPlan.moduleCount + 10 <= CREATURE_MAX_MODULES &&
        base->adult.ornamentCount + 6 <= CREATURE_MAX_ORNAMENTS) {
        picks[6] = (CreatureTrait){
            .type = (NextRng(&rng) & 1u) ? CREATURE_TRAIT_HORNS : CREATURE_TRAIT_DORSAL_SPINES,
            .strength = 0.50f + 0.45f * NextRng01(&rng),
            .paramA = 0.55f + 0.45f * NextRng01(&rng),
            .paramB = 0.12f + 0.08f * NextRng01(&rng)
        };
        pickCount = 7;
    }

    for (size_t i = 0; i < pickCount; ++i) {
        if (CreatureVariation_IsTraitCompatible(&picks[i], base, &base->adult)) {
            CreatureVariation_AddTrait(&var, picks[i]);
        }
    }

    CreatureVariant variant = {0};
    if (CreatureVariation_Apply(base, &base->adult, &var, &variant) &&
        Creature_BuildMonster(outMonster, &variant.recipe, &variant.phenotype)) {
        snprintf(outLabel, labelCap, "AZAR_%.40s", catalog[baseIdx].label);
        return true;
    }

    /* Respaldo limpio al adulto base si alguna combinación de rasgos no converge. */
    CreaturePhenotype fallback = base->adult;
    if (Creature_BuildMonster(outMonster, base, &fallback)) {
        snprintf(outLabel, labelCap, "AZAR_%.40s", catalog[baseIdx].label);
        return true;
    }
    return false;
}

static void PruneCollapsedDormantModules(Monster* m) {
    if (!m || !m->hasAnatomyGraph) return;
    AnatomyGraph* g = &m->anatomyGraph;
    bool keepModule[65536];
    memset(keepModule, 0, sizeof(keepModule));

    for (size_t i = 0; i < g->nodeCount; ++i) {
        const AnatomyNode* n = &g->nodes[i];
        uint32_t mod = n->moduleInstanceId & 0xffffu;
        if (n->region <= ANATOMY_REGION_PELVIS || n->role == ANATOMY_ROLE_AXIAL ||
            n->region == ANATOMY_REGION_HEAD || n->development > 1e-4f ||
            n->widthRadius > 2e-4f || n->heightRadius > 2e-4f) {
            keepModule[mod] = true;
        }
    }
    for (size_t i = 0; i < g->connectionCount; ++i) {
        if (!g->dormantConnections[i] || g->connections[i].development > 1e-4f) {
            keepModule[g->connections[i].moduleInstanceId & 0xffffu] = true;
        }
    }
    /* No podar módulos que sirvan de anfitrión a un módulo conservado. */
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < g->connectionCount; ++i) {
            const AnatomyNode* from = AnatomyGraph_FindNode(g, g->connections[i].fromId);
            const AnatomyNode* to = AnatomyGraph_FindNode(g, g->connections[i].toId);
            if (!from || !to) continue;
            uint32_t mFrom = from->moduleInstanceId & 0xffffu;
            uint32_t mTo = to->moduleInstanceId & 0xffffu;
            if (keepModule[mTo] && !keepModule[mFrom]) {
                keepModule[mFrom] = true;
                changed = true;
            }
        }
    }

    AnatomyGraph pruned;
    AnatomyGraph_Init(&pruned);
    pruned.axialProfileCount = g->axialProfileCount;
    if (g->axialProfileCount) {
        memcpy(pruned.axialProfile, g->axialProfile, g->axialProfileCount * sizeof(pruned.axialProfile[0]));
    }
    for (size_t i = 0; i < g->nodeCount; ++i) {
        if (keepModule[g->nodes[i].moduleInstanceId & 0xffffu]) {
            AnatomyGraph_AddNode(&pruned, g->nodes[i]);
        }
    }
    for (size_t i = 0; i < g->connectionCount; ++i) {
        const BodyConnection* e = &g->connections[i];
        if (!keepModule[e->moduleInstanceId & 0xffffu]) continue;
        if (!AnatomyGraph_FindNode(&pruned, e->fromId) || !AnatomyGraph_FindNode(&pruned, e->toId)) continue;
        if (AnatomyGraph_Connect(&pruned, *e)) {
            pruned.dormantConnections[pruned.connectionCount - 1] = g->dormantConnections[i];
        }
    }
    if (AnatomyGraph_Validate(&pruned)) {
        *g = pruned;
    }
}

static bool InitMorphRuntime(MorphRuntime* rt, const Monster* initialSource, const Monster* initialTarget,
    const char* srcLabel, const char* dstLabel, const DemoOptions* opt) {
    memset(rt, 0, sizeof(*rt));
    rt->eyeAge = -1.0f;
    rt->lastHairExactAge = -1.0f;
    rt->source = Monster_Clone(initialSource);
    rt->target = Monster_Clone(initialTarget);
    rt->reference = Monster_Create();
    if (!CreatureMorph_Interpolate(&rt->source, &rt->target, 0.5f, &rt->reference)) {
        Monster_CopyInto(&rt->reference, &rt->source);
    }
    snprintf(rt->sourceLabel, sizeof(rt->sourceLabel), "%s", srcLabel ? srcLabel : "ORIGEN");
    snprintf(rt->targetLabel, sizeof(rt->targetLabel), "%s", dstLabel ? dstLabel : "DESTINO");

    rt->ager = MonsterAger_Create(&rt->source, &rt->target, opt->hasFixedAge ? opt->fixedAge : 0.0f);
    MonsterAger_SetGeometrySnapshotEnabled(&rt->ager,
        opt->surfaceMode == SURFACE_MODE_LEGACY_HANDOFF && !opt->legacyLag);

    rt->frameSdf = MonsterSDF_Create();
    rt->membranes = Mesh_Create();
    rt->armor = Mesh_Create();
    rt->detail = Mesh_Create();
    rt->translucent = Mesh_Create();
    return true;
}

static bool EnsureHandoff(MorphRuntime* rt, const DemoOptions* opt) {
    if (rt->visual) return true;
    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.enableLocalMorphPatches = true;
    cfg.enableChunkedRemeshing = true;
    if (opt->chunkSize > 0u) cfg.chunkSize = (float)opt->chunkSize * 0.03f;
    rt->visual = MonsterVisualAsync_Create(cfg);
    if (!rt->visual) return false;
    MonsterVisualAsync_SetMorphMode(rt->visual, true);
    if (!rt->visual->threadRunning ||
        !MonsterVisualAsync_SetMorphEndpoints(rt->visual, &rt->source, &rt->target)) {
        MonsterVisualAsync_Free(rt->visual);
        rt->visual = NULL;
        return false;
    }
    MonsterVisualAsync_SetWorkerDelay(rt->visual, opt->workerDelayMs);
    return true;
}

static void InvalidateMorphPair(MorphRuntime* rt) {
    rt->fieldValid = rt->materialValid = rt->rayFrameValid = false;
    rt->eyeAge = -1.0f;
    rt->gpuGridValid = false;
    if (rt->visual && !MonsterVisualAsync_SetMorphEndpoints(rt->visual, &rt->source, &rt->target)) {
        MonsterVisualAsync_Free(rt->visual);
        rt->visual = NULL;
    }
}

static const MonsterSDF* PrepareFrameField(MorphRuntime* rt, const DemoOptions* opt) {
    if (!rt->fieldValid || rt->fieldAge != rt->ager.perc) {
        double start = opt->profile ? OpenGLDemoWindow_Time() : 0;
        const Monster* current = MonsterAger_GetResultConst(&rt->ager);
        rt->fieldValid = MonsterSDF_Build(&rt->frameSdf, current, MonsterSDF_DefaultConfig());
        if (rt->fieldValid) {
            if (!Membrane_BuildMesh(current, &rt->membranes)) Mesh_Clear(&rt->membranes);
            if (!ArmorScales_BuildMesh(current, &rt->frameSdf, &rt->armor)) Mesh_Clear(&rt->armor);
            if (!SupplementaryMesh_Build(current, &rt->detail, &rt->translucent)) {
                Mesh_Clear(&rt->detail);
                Mesh_Clear(&rt->translucent);
            }
        }
        rt->fieldAge = rt->ager.perc;
        rt->materialValid = rt->rayFrameValid = false;
        if (opt->profile) rt->sdfMs += (OpenGLDemoWindow_Time() - start) * 1000;
    }
    return rt->fieldValid ? &rt->frameSdf : NULL;
}

static void DestroyMorphRuntime(MorphRuntime* rt) {
    if (rt->gpuCache) {
        GpuSDFSurfaceCache_Destroy(rt->gpuCache);
        rt->gpuCache = NULL;
    }
    if (rt->visual) {
        MonsterVisualAsync_Free(rt->visual);
        rt->visual = NULL;
    }
    MonsterSDF_Free(&rt->frameSdf);
    Mesh_Free(&rt->membranes);
    Mesh_Free(&rt->armor);
    Mesh_Free(&rt->detail);
    Mesh_Free(&rt->translucent);
    for (size_t i = 0; i < rt->eyeCapacity; ++i) Mesh_Free(&rt->eyes[i].globe);
    free(rt->eyes);
    LongHairAsync_Destroy(rt->longHairAsync);
    rt->longHairAsync = NULL;
    LongHairSet_Free(&rt->longHair);
    rt->lastHairExactAge = -1.0f;
    rt->lastHairGroupCount = 0;
    MonsterAger_Free(&rt->ager);
    Monster_Free(&rt->reference);
    Monster_Free(&rt->target);
    Monster_Free(&rt->source);
}

static bool SwitchMorphTarget(MorphRuntime* rt, const Monster* newTarget, const char* newTargetLabel, float currentAge) {
    Monster nextSource = Monster_Create();
    const Monster* current = MonsterAger_GetResultConst(&rt->ager);
    if (currentAge >= 0.9995f) {
        if (!Monster_CopyInto(&nextSource, &rt->target)) {
            Monster_Free(&nextSource);
            return false;
        }
        snprintf(rt->sourceLabel, sizeof(rt->sourceLabel), "%s", rt->targetLabel);
    } else if (currentAge <= 0.0005f) {
        if (!Monster_CopyInto(&nextSource, &rt->source)) {
            Monster_Free(&nextSource);
            return false;
        }
    } else {
        if (!Monster_CopyInto(&nextSource, current)) {
            Monster_Free(&nextSource);
            return false;
        }
        PruneCollapsedDormantModules(&nextSource);
        char blendLabel[48];
        snprintf(blendLabel, sizeof(blendLabel), "MIX_%d", (int)roundf(currentAge * 100.0f));
        snprintf(rt->sourceLabel, sizeof(rt->sourceLabel), "%s", blendLabel);
    }

    Monster_Free(&rt->source);
    rt->source = nextSource;

    Monster_Free(&rt->target);
    rt->target = Monster_Clone(newTarget);
    snprintf(rt->targetLabel, sizeof(rt->targetLabel), "%s", newTargetLabel ? newTargetLabel : "DESTINO");

    Monster_Free(&rt->reference);
    rt->reference = Monster_Create();
    if (!CreatureMorph_Interpolate(&rt->source, &rt->target, 0.5f, &rt->reference)) {
        Monster_CopyInto(&rt->reference, &rt->source);
    }

    bool geomSnap = rt->ager.geometrySnapshotEnabled;
    unsigned geomSteps = rt->ager.geometrySteps;
    LongHairSet_Free(&rt->longHair);
    rt->lastHairExactAge = -1.0f;
    rt->lastHairGroupCount = 0;
    MonsterAger_Free(&rt->ager);
    rt->ager = MonsterAger_Create(&rt->source, &rt->target, 0.0f);
    rt->eyeAge = -1.0f;
    MonsterAger_SetGeometrySnapshotEnabled(&rt->ager, geomSnap);
    if (geomSteps) MonsterAger_SetGeometrySteps(&rt->ager, geomSteps);

    InvalidateMorphPair(rt);
    return true;
}

static void DrawAnatomyOverlay(const AnatomyGraph* g) {
    if (!g) return;
    for (size_t i = 0; i < g->connectionCount; ++i) {
        if (g->dormantConnections[i]) continue;
        const BodyConnection* e = &g->connections[i];
        const AnatomyNode* a = AnatomyGraph_FindNode(g, e->fromId);
        const AnatomyNode* b = AnatomyGraph_FindNode(g, e->toId);
        if (!a || !b) continue;
        Color col = e->kind == BODY_CONNECTION_AXIAL_LOFT ? Color_FromRGB(90, 220, 255) :
            e->kind == BODY_CONNECTION_HOOF_CAPSULE ? Color_FromRGB(255, 195, 70) :
            e->kind == BODY_CONNECTION_ORNAMENT_SEGMENT ? Color_FromRGB(255, 110, 175) :
            Color_FromRGB(140, 255, 140);
        OpenGLRenderer_DebugLine(a->center, b->center, col);
    }
}

static void DrawGridFloor(float y) {
    Color major = Color_FromRGB(74, 82, 92);
    Color minor = Color_FromRGB(52, 58, 66);
    for (int i = -10; i <= 10; ++i) {
        Color c = (i % 5 == 0) ? major : minor;
        OpenGLRenderer_DebugLine(Vec3_Create((float)i, y, -10.0f), Vec3_Create((float)i, y, 10.0f), c);
        OpenGLRenderer_DebugLine(Vec3_Create(-10.0f, y, (float)i), Vec3_Create(10.0f, y, (float)i), c);
    }
}

static const char* SurfaceModeName(SurfaceMode m) {
    switch (m) {
        case SURFACE_MODE_SDF_RAYMARCH: return "SDF-RAYMARCH";
        case SURFACE_MODE_EXACT_SDF: return "EXACT-SDF";
        case SURFACE_MODE_GPU_SDF_DENSE: return "GPU-SDF-DENSE";
        case SURFACE_MODE_GPU_SDF_SPARSE: return "GPU-SDF-SPARSE";
        case SURFACE_MODE_LEGACY_HANDOFF: return "LEGACY-HANDOFF";
        default: return "SDF";
    }
}

static bool ParseSurfaceMode(const char* val, SurfaceMode* out) {
    if (!val || !out) return false;
    if (strcmp(val, "sdf-raymarch") == 0) *out = SURFACE_MODE_SDF_RAYMARCH;
    else if (strcmp(val, "exact-sdf") == 0) *out = SURFACE_MODE_EXACT_SDF;
    else if (strcmp(val, "gpu-sdf-dense") == 0) *out = SURFACE_MODE_GPU_SDF_DENSE;
    else if (strcmp(val, "gpu-sdf-sparse") == 0) *out = SURFACE_MODE_GPU_SDF_SPARSE;
    else if (strcmp(val, "legacy-handoff") == 0) *out = SURFACE_MODE_LEGACY_HANDOFF;
    else return false;
    return true;
}

static DemoOptions ParseOptions(int argc, char** argv) {
    DemoOptions opt = {
        .fixedAge = -1.0f,
        .hasFixedAge = false,
        .viewMode = 3,
        .chunkSize = 16u,
        .gpuBrickCells = 16u,
        .gpuRefinement = 1u,
        .gpuVoxelStep = 0.08f,
        .surfaceMode = SURFACE_MODE_SDF_RAYMARCH,
        .initialZoom = 1.0f
    };

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strncmp(arg, "--age=", 6) == 0) {
            opt.fixedAge = Math_Clamp01((float)atof(arg + 6));
            opt.hasFixedAge = true;
        } else if (strncmp(arg, "--age-sequence=", 15) == 0) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s", arg + 15);
            char* tok = strtok(buf, ",");
            while (tok && opt.ageSequenceCount < 64) {
                opt.ageSequence[opt.ageSequenceCount++] = Math_Clamp01((float)atof(tok));
                tok = strtok(NULL, ",");
            }
        } else if (strncmp(arg, "--from=", 7) == 0) {
            opt.fromSpec = arg + 7;
        } else if (strncmp(arg, "--to=", 5) == 0) {
            opt.toSpec = arg + 5;
        } else if (strncmp(arg, "--seed=", 7) == 0) {
            opt.seed = (uint32_t)strtoul(arg + 7, NULL, 10);
            opt.hasSeed = true;
        } else if (strcmp(arg, "--list") == 0) {
            opt.listOnly = true;
        } else if (strncmp(arg, "--capture-prefix=", 17) == 0) {
            opt.capturePrefix = arg + 17;
        } else if (strncmp(arg, "--validate-cycle=", 17) == 0) {
            opt.validateCyclePrefix = arg + 17;
        } else if (strcmp(arg, "--profile-switch") == 0) {
            opt.profileSwitch = true;
        } else if (strcmp(arg, "--profile-unpaced") == 0) {
            opt.profileUnpaced = true;
        } else if (strcmp(arg, "--native-resolution") == 0) {
            opt.nativeResolution = true;
        } else if (strcmp(arg, "--profile-backends") == 0) {
            opt.profileBackends = true;
        } else if (strncmp(arg, "--profile=", 10) == 0) {
            opt.profile = true;
            opt.profileWarmup = atoi(arg + 10);
        } else if (strncmp(arg, "--frames=", 9) == 0) {
            opt.maxFrames = atoi(arg + 9);
        } else if (strncmp(arg, "--view=", 7) == 0) {
            opt.viewMode = atoi(arg + 7);
        } else if (strncmp(arg, "--worker-delay=", 15) == 0) {
            opt.workerDelayMs = (unsigned)atoi(arg + 15);
        } else if (strncmp(arg, "--chunk-size=", 13) == 0) {
            opt.chunkSize = (unsigned)atoi(arg + 13);
        } else if (strncmp(arg, "--fixed-timestep=", 17) == 0) {
            opt.fixedTimestep = (float)atof(arg + 17);
        } else if (strncmp(arg, "--surface-mode=", 15) == 0) {
            ParseSurfaceMode(arg + 15, &opt.surfaceMode);
        } else if (strcmp(arg, "--sdf") == 0) {
            opt.surfaceMode = SURFACE_MODE_SDF_RAYMARCH;
        } else if (strcmp(arg, "--exact-sdf") == 0) {
            opt.surfaceMode = SURFACE_MODE_EXACT_SDF;
        } else if (strncmp(arg, "--reference-max-step=", 21) == 0) {
            opt.referenceMaxStep = (float)atof(arg + 21);
        } else if (strncmp(arg, "--reference-steps=", 18) == 0) {
            opt.referenceSteps = (unsigned)atoi(arg + 18);
        } else if (strncmp(arg, "--gpu-voxel=", 12) == 0) {
            opt.gpuVoxelStep = fmaxf(0.02f, (float)atof(arg + 12));
        } else if (strncmp(arg, "--gpu-brick-cells=", 18) == 0) {
            opt.gpuBrickCells = (unsigned)atoi(arg + 18);
        } else if (strncmp(arg, "--gpu-refinement=", 17) == 0) {
            opt.gpuRefinement = (unsigned)atoi(arg + 17);
        } else if (strcmp(arg, "--no-msaa") == 0) opt.noMsaa = true;
        else if (strcmp(arg, "--no-fur") == 0) opt.noFur = true;
        else if (strcmp(arg, "--stress-motion") == 0) opt.stressMotion = true;
        else if (strcmp(arg, "--chunk-wireframe") == 0) opt.chunkWireframe = true;
        else if (strcmp(arg, "--fallback-region") == 0) opt.fallbackRegionDebug = true;
        else if (strcmp(arg, "--ownership-debug") == 0) opt.ownershipDebug = true;
        else if (strcmp(arg, "--handoff-debug") == 0) opt.handoffDebug = true;
        else if (strcmp(arg, "--legacy-lag") == 0) opt.legacyLag = true;
        else if (strcmp(arg, "--realtime") == 0) opt.realtime = true;
        else if (strcmp(arg, "--gpu-trace") == 0) opt.gpuTrace = true;
        else if (strcmp(arg, "--gpu-signature") == 0) opt.gpuSignature = true;
        else if (strcmp(arg, "--gpu-cached-normals") == 0) opt.gpuCachedNormals = true;
        else if (strcmp(arg, "--validate-compute") == 0) opt.validateCompute = true;
        else if (strcmp(arg, "--local-patches") == 0) opt.localPatches = true;
        else if (strncmp(arg, "--zoom=", 7) == 0) opt.initialZoom = (float)atof(arg + 7);
        else if (strcmp(arg, "--capture-normals") == 0) opt.surfaceDebugMode = 1;
        else if (strcmp(arg, "--capture-flat") == 0) opt.surfaceDebugMode = 2;
        else if (strcmp(arg, "--capture-albedo") == 0) opt.surfaceDebugMode = 3;
        else if (strcmp(arg, "--capture-semantic") == 0) opt.surfaceDebugMode = 4;
    }
    return opt;
}

static bool EnsureGpuSurfaceGrid(MorphRuntime* rt, Renderer3D* renderer, const DemoOptions* opt) {
    if (rt->gpuGridValid) return true;
    AABB3D bounds = AABB_Empty();
    MonsterSDF sdf = MonsterSDF_Create();
    Monster sample = Monster_Create();
    const float ages[3] = {0.0f, 0.5f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        MonsterAger_Interpolate(&rt->ager.monster1, &rt->ager.monster2, ages[i], &sample);
        if (MonsterSDF_Build(&sdf, &sample, MonsterSDF_DefaultConfig())) {
            AABB_ExpandPoint(&bounds, sdf.bounds.start);
            AABB_ExpandPoint(&bounds, sdf.bounds.end);
        }
    }
    Monster_Free(&sample);
    MonsterSDF_Free(&sdf);
    bounds.start = Vec3_Sub(bounds.start, Vec3_Create(0.6f, 0.6f, 0.6f));
    bounds.end = Vec3_Add(bounds.end, Vec3_Create(0.6f, 0.6f, 0.6f));

    if (!GpuSDFSurfaceGrid_Configure(&rt->gpuGrid, bounds, opt->gpuVoxelStep, opt->gpuRefinement)) {
        return false;
    }
    OpenGLRenderer_InvalidateGpuSurface(renderer);
    if (opt->surfaceMode == SURFACE_MODE_GPU_SDF_SPARSE) {
        if (rt->gpuCache) GpuSDFSurfaceCache_Destroy(rt->gpuCache);
        rt->gpuCache = GpuSDFSurfaceCache_Create(&rt->gpuGrid, opt->gpuBrickCells);
        if (!rt->gpuCache) return false;
        rt->gpuCache->cachedNormals = opt->gpuCachedNormals;
    }
    rt->gpuGridValid = true;
    return true;
}

/* La superficie SDF reserva las órbitas; los globos usan la misma ruta
 * texturizada que el visor de malla y la profundidad de la superficie. */
static void RenderCurrentEyes(MorphRuntime* rt, Renderer3D* renderer, const Monster* current) {
    if (!current->eyeCount) return;
    if (current->eyeCount > rt->eyeCapacity) {
        MonsterVisualEye* eyes = realloc(rt->eyes, current->eyeCount * sizeof(*eyes));
        if (!eyes) return;
        for (size_t i = rt->eyeCapacity; i < current->eyeCount; ++i) {
            eyes[i] = (MonsterVisualEye){.globe = Mesh_Create()};
        }
        rt->eyes = eyes;
        rt->eyeCapacity = current->eyeCount;
    }
    if (rt->eyeAge != rt->ager.perc) {
        bool ok = renderer->renderEye ?
            MonsterVisual_UpdateEyeInstances(rt->eyes, rt->eyeCapacity, current) :
            MonsterVisual_UpdateEyes(rt->eyes, rt->eyeCapacity, current);
        if (!ok) return;
        rt->eyeAge = rt->ager.perc;
    }
    for (size_t i = 0; i < current->eyeCount; ++i) {
        if (renderer->renderEye) renderer->renderEye(renderer, &rt->eyes[i]);
        else renderer->renderMesh(renderer, &rt->eyes[i].globe);
    }
}

static bool SameCamera(const ICamera* a, const ICamera* b) {
    return Vec3_LengthSq(Vec3_Sub(a->position, b->position)) == 0 &&
           Vec3_LengthSq(Vec3_Sub(a->target, b->target)) == 0 &&
           Vec3_LengthSq(Vec3_Sub(a->up, b->up)) == 0 &&
           a->fov == b->fov && a->aspectRatio == b->aspectRatio &&
           a->nearPlane == b->nearPlane && a->farPlane == b->farPlane;
}

static void RenderCurrentFrame(MorphRuntime* rt, Renderer3D* renderer, ICamera* cam, int w, int h,
    const DemoOptions* opt, float currentAge, bool wire, bool showSkeleton, bool noFur,
    const char* hudText) {
    const Monster* current = MonsterAger_GetResultConst(&rt->ager);
    const MonsterSDF* sdf = PrepareFrameField(rt, opt);
    double phase = 0;

    OpenGLRenderer_SetWireframe(renderer, wire);
    if (!rt->materialValid) {
        rt->materialValid = OpenGLRenderer_SetMorphSurfaceDomain(renderer, current, &rt->reference);
    }
    OpenGLRenderer_SetSDFReference(renderer, opt->surfaceMode == SURFACE_MODE_EXACT_SDF,
        opt->referenceMaxStep, opt->referenceSteps);
    OpenGLRenderer_SetFurQuality(renderer, !noFur, !noFur, 0);
    OpenGLRenderer_SetLongHairQuality(renderer, noFur ? 0 : 3);
    bool drewAsyncVisual = false;

    renderer->beginFrame(renderer);

    if (opt->surfaceMode == SURFACE_MODE_SDF_RAYMARCH || opt->surfaceMode == SURFACE_MODE_EXACT_SDF) {
        bool rayMode = opt->surfaceMode == SURFACE_MODE_SDF_RAYMARCH;
        bool adaptive = rayMode && !opt->nativeResolution && !opt->capturePrefix && !opt->validateCyclePrefix;
        bool sameView = rayMode && sdf && rt->rayFrameValid && SameCamera(&rt->rayCamera, cam) &&
            rt->rayNoFur == noFur && rt->rayWire == wire && rt->raySurfaceDebug == opt->surfaceDebugMode;
        bool reused = sameView && OpenGLRenderer_CompositeCachedSDFFrame(renderer, w, h);
        if (!reused) {
            rt->rayFrameValid = false;
            // Una vista detenida se conserva a resolución nativa, incluso tras un paneo costoso.
            if (sdf && OpenGLRenderer_BeginSDFFrame(renderer, cam, w, h, adaptive && !sameView)) {
                bool rendered = OpenGLRenderer_RenderSDF(renderer, sdf, cam, w, h);
                rt->rayFrameValid = rayMode && rendered && rt->materialValid;
                OpenGLRenderer_EndSDFFrame(renderer, w, h);
                rt->rayCamera = *cam;
                rt->rayNoFur = noFur;
                rt->rayWire = wire;
                rt->raySurfaceDebug = opt->surfaceDebugMode;
            }
        }
    } else if (opt->surfaceMode == SURFACE_MODE_GPU_SDF_DENSE && sdf) {
        if (EnsureGpuSurfaceGrid(rt, renderer, opt)) {
            OpenGLRenderer_SetupCamera(cam, w, h);
            OpenGLRenderer_RenderGpuSDFSurface(renderer, sdf, &rt->gpuGrid, cam);
        }
    } else if (opt->surfaceMode == SURFACE_MODE_GPU_SDF_SPARSE && sdf) {
        if (EnsureGpuSurfaceGrid(rt, renderer, opt) && rt->gpuCache) {
            OpenGLRenderer_SetupCamera(cam, w, h);
            OpenGLRenderer_RenderGpuSparseSDFSurface(renderer, sdf, rt->gpuCache, cam);
        }
    } else if (opt->surfaceMode == SURFACE_MODE_LEGACY_HANDOFF && rt->visual) {
        const Monster* geomMonster = MonsterAger_GetGeometryResultConst(&rt->ager);
        if (opt->profile) phase = OpenGLDemoWindow_Time();
        MonsterVisualAsync_UpdateWithAppearance(rt->visual, geomMonster, current, 0.016f);
        if (opt->profile) rt->meshMs = (OpenGLDemoWindow_Time() - phase) * 1000;
        MorphFallbackRegion regions[MORPH_HANDOFF_MAX_INTERFACES];
        if (opt->profile) phase = OpenGLDemoWindow_Time();
        size_t regionCount = MonsterVisualAsync_GetFallbackRegions(rt->visual, sdf,
            &current->anatomyGraph, currentAge, 0.08f, regions, MORPH_HANDOFF_MAX_INTERFACES);
        if (opt->profile) rt->fallbackMs = (OpenGLDemoWindow_Time() - phase) * 1000;
        size_t masked = 0;
        const Mesh* ownedMesh = MonsterVisualAsync_GetOwnedMesh(rt->visual, regions, regionCount, &masked);
        OpenGLRenderer_SetupCamera(cam, w, h);
        AABB3D clipBoxes[MORPH_HANDOFF_MAX_INTERFACES];
        MorphHandoffSet handoffs={0};
        for (size_t i = 0; i < regionCount; ++i) {
            handoffs.interfaces[i]=regions[i].handoff;
            clipBoxes[i]=regions[i].bounds;
        }
        handoffs.count=regionCount;
        size_t clipCount=regionCount;
        bool meshReady=ownedMesh&&ownedMesh->hasSurface&&ownedMesh->indexCount>0;
        bool useMesh=meshReady&&OpenGLRenderer_SetMorphHandoffs(renderer,&handoffs,-2);
        if(!useMesh) {
            clipCount=0;
            OpenGLRenderer_SetMorphHandoffs(renderer,NULL,-1);
        }
        if (useMesh) {
            SurfacePhenotype resolvedSurface = current->surface;
            SurfacePhenotype_CopyAnchoredDomains(&resolvedSurface, &current->phenotype.surface);
            SurfaceMapper_ResolvePigmentDomains(&resolvedSurface, &rt->reference.anatomyGraph, &rt->reference.surfaceMapping);
            SurfaceRecipe compiledSurface = SurfaceRecipe_CompileScaled(&resolvedSurface, current->surfaceMapping.unitScale);
            OpenGLRenderer_SetMeshSurfaceOverride(renderer, &compiledSurface);
            renderer->renderMesh(renderer, ownedMesh);
        } else if (sdf) {
            OpenGLRenderer_SetMeshSurfaceOverride(renderer,NULL);
            OpenGLRenderer_RenderSDF(renderer,sdf,cam,w,h);
        } else {
            OpenGLRenderer_SetMeshSurfaceOverride(renderer,NULL);
            MonsterVisualAsync_Render(rt->visual,renderer);
            drewAsyncVisual=true;
        }
        if (clipCount > 0 && sdf) {
            OpenGLRenderer_RenderSDFRegions(renderer, sdf, cam, w, h, clipBoxes, clipCount);
        }
        OpenGLRenderer_SetMorphHandoffs(renderer,NULL,-1);
        MonsterVisualAsync_RecordFallbackFrame(rt->visual, regions, regionCount, clipCount, useMesh, masked);
    } else if (opt->surfaceMode == SURFACE_MODE_LEGACY_HANDOFF && sdf) {
        OpenGLRenderer_SetupCamera(cam, w, h);
        OpenGLRenderer_RenderSDF(renderer, sdf, cam, w, h);
    }

    OpenGLRenderer_SetupCamera(cam, w, h);
    if (sdf) {
        OpenGLRenderer_SetMeshSurfaceOverride(renderer,NULL);
        if (rt->membranes.indexCount) renderer->renderMesh(renderer,&rt->membranes);
        if (rt->armor.indexCount) renderer->renderMesh(renderer,&rt->armor);
        if (rt->detail.indexCount) renderer->renderMesh(renderer,&rt->detail);
        if (rt->translucent.indexCount) renderer->renderMesh(renderer,&rt->translucent);
    }
    if (opt->profile) phase = OpenGLDemoWindow_Time();
    if (!drewAsyncVisual) RenderCurrentEyes(rt, renderer, current);
    if (opt->profile) rt->eyeMs = (OpenGLDemoWindow_Time() - phase) * 1000;
    if (!drewAsyncVisual && !noFur && renderer->renderLongHair) {
        bool useAsyncHair = !opt->capturePrefix && !opt->validateCyclePrefix && (!opt->ageSequenceCount || opt->profile);
        if (useAsyncHair) {
            if (!rt->longHairAsync) {
                rt->longHairAsync = LongHairAsync_Create();
            }
            if (rt->longHairAsync) {
                LongHairAsync_Update(rt->longHairAsync, current, rt->interactiveScrub);
                const LongHairSet* asyncHair = LongHairAsync_GetDisplay(rt->longHairAsync);
                if (asyncHair && asyncHair->count > 0) {
                    renderer->renderLongHair(renderer, asyncHair);
                }
            }
        } else if (current->hasSurface && current->surface.longHairCount > 0) {
            bool needExactHair = !rt->interactiveScrub ||
                rt->longHair.count == 0 ||
                rt->lastHairExactAge < 0.0f ||
                rt->lastHairGroupCount != current->surface.longHairCount ||
                fabsf(currentAge - rt->lastHairExactAge) >= 0.025f ||
                currentAge <= 0.0f || currentAge >= 1.0f;
            if (needExactHair) {
                MonsterVisual_UpdateLongHair(&rt->longHair, current);
                rt->lastHairExactAge = currentAge;
                rt->lastHairGroupCount = current->surface.longHairCount;
            }
            if (rt->longHair.count > 0) {
                renderer->renderLongHair(renderer, &rt->longHair);
            }
        } else if (rt->longHair.count > 0) {
            MonsterVisual_UpdateLongHair(&rt->longHair, current);
            rt->lastHairExactAge = currentAge;
            rt->lastHairGroupCount = 0;
        }
    }
    if (!opt->capturePrefix && !opt->validateCyclePrefix &&
        opt->surfaceMode != SURFACE_MODE_SDF_RAYMARCH && opt->surfaceMode != SURFACE_MODE_EXACT_SDF) {
        DrawGridFloor(0.0f);
    }
    if (showSkeleton && current->hasAnatomyGraph) {
        DrawAnatomyOverlay(&current->anatomyGraph);
    }

    if (hudText && hudText[0] && !opt->capturePrefix && !opt->validateCyclePrefix) {
        OpenGLRenderer_DrawTextPanel(w, h, 14, 14, hudText);
    }

    if (opt->validateCompute && sdf) {
        Vector3 pts[64];
        for (int i = 0; i < 64; ++i) {
            float fx = ((i & 3) - 1.5f) * 0.8f;
            float fy = (((i >> 2) & 3) * 0.9f) + 0.3f;
            float fz = (((i >> 4) & 3) - 1.5f) * 1.1f;
            pts[i] = Vec3_Create(fx, fy, fz);
        }
        float maxErr = 0.0f;
        OpenGLRenderer_ValidateComputeSDF(renderer, sdf, pts, 64, &maxErr);
        printf("[COMPUTE] edad=%.2f maxErr=%.5f\n", currentAge, maxErr);
    }

    renderer->endFrame(renderer);
}

int main(int argc, char** argv) {
    DemoOptions opt = ParseOptions(argc, argv);
    if (opt.profile) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        if (opt.maxFrames <= opt.profileWarmup || opt.profileWarmup < 0 || opt.capturePrefix ||
            opt.validateCyclePrefix || opt.surfaceMode == SURFACE_MODE_EXACT_SDF) {
            fprintf(stderr, "El perfil requiere --frames > calentamiento y un modo interactivo.\n");
            return 1;
        }
    }
    double startup = opt.profile ? OpenGLDemoWindow_Time() : 0;

    SpeciesCatalogEntry catalog[MAX_CATALOG_RECIPES];
    size_t catalogCount = LoadSpeciesCatalog(catalog, MAX_CATALOG_RECIPES);
    if (catalogCount == 0) {
        fprintf(stderr, "Error: catálogo de especies vacío.\n");
        return 1;
    }

    if (opt.listOnly) {
        printf("Catálogo de recetas (%zu):\n", catalogCount);
        for (size_t i = 0; i < catalogCount; ++i) {
            printf("  [%zu] ID=%u  %s\n", i + 1, (unsigned)catalog[i].id, catalog[i].recipe->name);
        }
        return 0;
    }

    uint32_t seed = opt.hasSeed ? opt.seed : (uint32_t)time(NULL) ^ (uint32_t)clock();
    uint32_t rng = seed ? seed : 1337u;

    Monster initSource = Monster_Create();
    Monster initTarget = Monster_Create();
    char srcLabel[48] = {0}, dstLabel[48] = {0};
    size_t initBaseIdx = 0;

    if (opt.fromSpec && !EqualsIgnoreCase(opt.fromSpec, "random")) {
        const SpeciesCatalogEntry* entry = FindCatalogEntry(catalog, catalogCount, opt.fromSpec);
        if (!entry) {
            fprintf(stderr, "Receta --from='%s' no encontrada.\n", opt.fromSpec);
            return 1;
        }
        CreaturePhenotype p = entry->recipe->adult;
        if (!Creature_BuildMonster(&initSource, entry->recipe, &p)) return 1;
        snprintf(srcLabel, sizeof(srcLabel), "%s", entry->label);
    } else {
        uint32_t srcSeed = opt.hasSeed ? opt.seed : NextRng(&rng);
        if (!BuildRandomCreature(srcSeed, catalog, catalogCount, &initSource, srcLabel, sizeof(srcLabel), &initBaseIdx)) {
            fprintf(stderr, "Error al construir criatura aleatoria inicial.\n");
            return 1;
        }
    }

    if (opt.toSpec && !EqualsIgnoreCase(opt.toSpec, "random")) {
        const SpeciesCatalogEntry* entry = FindCatalogEntry(catalog, catalogCount, opt.toSpec);
        if (!entry) {
            fprintf(stderr, "Receta --to='%s' no encontrada.\n", opt.toSpec);
            Monster_Free(&initSource);
            return 1;
        }
        CreaturePhenotype p = entry->recipe->adult;
        if (!Creature_BuildMonster(&initTarget, entry->recipe, &p)) {
            Monster_Free(&initSource);
            return 1;
        }
        snprintf(dstLabel, sizeof(dstLabel), "%s", entry->label);
    } else if (opt.toSpec && EqualsIgnoreCase(opt.toSpec, "random")) {
        size_t dummy = 0;
        if (!BuildRandomCreature(NextRng(&rng), catalog, catalogCount, &initTarget, dstLabel, sizeof(dstLabel), &dummy)) {
            Monster_Free(&initSource);
            return 1;
        }
    } else {
        size_t nextIdx = (initBaseIdx + 1) % catalogCount;
        const SpeciesCatalogEntry* entry = &catalog[nextIdx];
        CreaturePhenotype p = entry->recipe->adult;
        if (!Creature_BuildMonster(&initTarget, entry->recipe, &p)) {
            Monster_Free(&initSource);
            return 1;
        }
        snprintf(dstLabel, sizeof(dstLabel), "%s", entry->label);
    }

    MorphRuntime rt;
    if (!InitMorphRuntime(&rt, &initSource, &initTarget, srcLabel, dstLabel, &opt)) {
        fprintf(stderr, "Error al inicializar MorphRuntime.\n");
        Monster_Free(&initSource);
        Monster_Free(&initTarget);
        return 1;
    }
    Monster_Free(&initSource);
    Monster_Free(&initTarget);

    if (opt.surfaceMode == SURFACE_MODE_LEGACY_HANDOFF && !EnsureHandoff(&rt, &opt)) {
        fprintf(stderr, "No se pudo iniciar el worker de malla.\n");
        DestroyMorphRuntime(&rt);
        return 1;
    }

    printf("\n=== DEMO METAMORFOSIS UNIVERSAL X -> Y (seed=%u) ===\n", seed);
    printf("  Inicio: %s  (Objetivo inicial: %s)\n", rt.sourceLabel, rt.targetLabel);
    printf("  Controles numéricos para mutar la criatura actual hacia:\n");
    printf("    [0] Nueva criatura ALEATORIA\n");
    for (size_t i = 0; i < catalogCount && i < 18; ++i) {
        printf("    [%s%zu] %s (ID %u)\n", i < 9 ? "" : "Mayús+", i % 9 + 1,
            catalog[i].recipe->name, (unsigned)catalog[i].id);
    }
    printf("  Otros controles: ESPACIO=auto ida/vuelta | IZQ/DER=edad manual | W=invertir\n");
    printf("    F1..F5=cámara | R=rotación orbital | A=esqueleto | E=modo render | G=wire | ESC=salir\n    E: raymarch/malla asíncrona | Mayús+E: incluye extracción GPU experimental\n    Referencia diagnóstica en CLI: --surface-mode=exact-sdf\n\n");

    const int width = 1280, height = 720;
    OpenGLDemoWindow* window = OpenGLDemoWindow_CreateMSAA(
        "Monster Engine - Metamorfosis Universal X -> Y", width, height, opt.noMsaa ? 0 : 4);
    if (!window) {
        fprintf(stderr, "No se pudo crear ventana OpenGL.\n");
        DestroyMorphRuntime(&rt);
        return 1;
    }

    ICamera cam = {
        .position = Vec3_Create(6.0f, 3.5f, 6.0f),
        .target = Vec3_Create(0.0f, 1.8f, 0.0f),
        .up = Vec3_Create(0.0f, 1.0f, 0.0f),
        .fov = 45.0f,
        .aspectRatio = (float)width / (float)height,
        .nearPlane = 0.1f,
        .farPlane = 120.0f
    };
    Renderer3D renderer = OpenGLRenderer_Create(&cam);
    OpenGLRenderer_SetSurfaceDebug(&renderer, opt.surfaceDebugMode);

    float age = opt.hasFixedAge ? opt.fixedAge : 0.0f;
    bool pingPong = false;
    bool transitionPlaying = false;
    float transitionDir = 1.0f;
    bool wire = opt.chunkWireframe;
    bool showSkeleton = false;
    bool noFur = opt.noFur;
    bool autoOrbit = (opt.capturePrefix == NULL && opt.validateCyclePrefix == NULL && !opt.hasFixedAge);
    float orbitYaw = 0.72f, orbitPitch = 0.22f, zoomScale = Math_Clamp(opt.initialZoom, MORPH_ZOOM_MIN, MORPH_ZOOM_MAX);
    bool dragging = false;

    /* Modo captura estática (--capture-prefix) */
    if (opt.capturePrefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s.ppm", opt.capturePrefix);
        if (rt.ager.perc != age) MonsterAger_SetPerc(&rt.ager, age);
        const Monster* cur = MonsterAger_GetResultConst(&rt.ager);
        const MonsterSDF* sdf = PrepareFrameField(&rt, &opt);
        Vector3 bc, hc, tc;
        float br = 3.5f;
        MorphViewCamera_Framing(cur, sdf, &bc, &br, &hc, &tc);
        MorphViewCamera_Setup(&cam, opt.viewMode, orbitYaw, orbitPitch, zoomScale, sdf, bc, br, hc, tc);
        int warmup = opt.surfaceMode == SURFACE_MODE_LEGACY_HANDOFF ? 6 : 2;
        for (int f = 0; f < warmup; ++f) {
            RenderCurrentFrame(&rt, &renderer, &cam, width, height, &opt, age, wire, showSkeleton, noFur, NULL);
            if (opt.surfaceMode == SURFACE_MODE_LEGACY_HANDOFF && rt.visual) {
                MonsterVisualAsync_Flush(rt.visual);
            }
            OpenGLDemoWindow_Swap(window);
        }
        RenderCurrentFrame(&rt, &renderer, &cam, width, height, &opt, age, wire, showSkeleton, noFur, NULL);
        OpenGLRenderer_Finish();
        OpenGLRenderer_SavePPM(path, width, height);
        OpenGLDemoWindow_Swap(window);
        printf("[CAPTURA] %s (%s -> %s edad=%.2f)\n", path, rt.sourceLabel, rt.targetLabel, age);
        OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window);
        DestroyMorphRuntime(&rt);
        return 0;
    }

    /* Modo validación de ciclo completo (--validate-cycle) */
    if (opt.validateCyclePrefix) {
        const float sampleAges[MAX_CYCLE_SAMPLES] = {0.0f, 0.15f, 0.35f, 0.5f, 0.7f, 0.85f, 1.0f, 0.8f, 0.5f, 0.25f, 0.0f};
        for (int idx = 0; idx < MAX_CYCLE_SAMPLES; ++idx) {
            float targetAge = sampleAges[idx];
            MonsterAger_SetPerc(&rt.ager, targetAge);
            const Monster* cur = MonsterAger_GetResultConst(&rt.ager);
            const MonsterSDF* sdf = PrepareFrameField(&rt, &opt);
            Vector3 bc, hc, tc;
            float br = 3.5f;
            MorphViewCamera_Framing(cur, sdf, &bc, &br, &hc, &tc);
            MorphViewCamera_Setup(&cam, opt.viewMode, orbitYaw, orbitPitch, zoomScale, sdf, bc, br, hc, tc);
                for (int sub = 0; sub < 3; ++sub) {
                RenderCurrentFrame(&rt, &renderer, &cam, width, height, &opt, targetAge, wire, showSkeleton, noFur, NULL);
                OpenGLDemoWindow_Swap(window);
                if (opt.workerDelayMs == 0 && opt.surfaceMode == SURFACE_MODE_LEGACY_HANDOFF && rt.visual) {
                    MonsterVisualAsync_Flush(rt.visual);
                } else {
                    SDL_Delay(20);
                }
            }
            char path[512];
            snprintf(path, sizeof(path), "%s-%02d.ppm", opt.validateCyclePrefix, idx);
            RenderCurrentFrame(&rt, &renderer, &cam, width, height, &opt, targetAge, wire, showSkeleton, noFur, NULL);
            OpenGLRenderer_Finish();
            OpenGLRenderer_SavePPM(path, width, height);
            OpenGLDemoWindow_Swap(window);
        }
        OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window);
        DestroyMorphRuntime(&rt);
        return 0;
    }

    /* Bucle interactivo o por secuencia/frames */
    bool running = true;
    int frameCount = 0;
    double lastTime = OpenGLDemoWindow_Time();
    double loopStart = lastTime;
    char titleBuf[160], hudBuf[512];

    enum { TIMER_COUNT = 16 };
    GLuint timers[TIMER_COUNT * 2];
    int timerFrames[TIMER_COUNT];
    OpenGLEyeStats previousEye = {0};
    uint64_t computeTimingGeneration = 0;
    bool backendCheckOk = true;
    GLsync pendingRasterFrame = NULL;
    if (opt.profile) {
        glGenQueries(TIMER_COUNT * 2, timers);
        for (int i = 0; i < TIMER_COUNT; ++i) timerFrames[i] = -1;
        printf("PROFILE_STARTUP,%.6f\n", (OpenGLDemoWindow_Time() - startup) * 1000);
        printf("PROFILE_GL,%s,%s,%d,%d\n", glGetString(GL_RENDERER), glGetString(GL_VERSION), width, height);
        puts("PROFILE_COLUMNS,frame,age,wall_ms,thread_cpu_ms,submit_ms,sdf_ms,morph_ms,mesh_ms,fallback_ms,eye_ms,peak_rss_kib,ray_scale,worker_build_ms,publish_cpu_ms,geometry_lag,coalesced,cancelled,obsolete,eye_misses,eye_gen_ms,eye_upload_ms,compute_gpu_bytes,worker_request_to_display_ms,worker_pending,worker_busy");
    }
    while (running) {
        struct timespec cpuStart, cpuEnd;
        if (opt.profile) clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpuStart);
        double now = OpenGLDemoWindow_Time();
        if (pendingRasterFrame) {
            /* El swap sin vsync puede entregar varios frames de golpe y
             * bloquear el siguiente durante todo el lote. Acotar la cola
             * raster mantiene fresca la entrada en escenas GPU costosas. */
            while (glClientWaitSync(pendingRasterFrame, 0, 0) == GL_TIMEOUT_EXPIRED)
                SDL_Delay(1);
            glDeleteSync(pendingRasterFrame);
            pendingRasterFrame = NULL;
        }
        float dt = opt.fixedTimestep > 0.0f ? opt.fixedTimestep : (float)fmin(0.05, now - lastTime);
        lastTime = now;

        if (opt.profile && ((opt.profileSwitch && frameCount % 8 == 0) ||
                            (opt.profileBackends && frameCount > 0 && frameCount % 12 == 0))) {
            SDL_Event automated = {.type = SDL_KEYDOWN};
            size_t count = catalogCount < 9 ? catalogCount : 9;
            if (opt.profileBackends && frameCount > 12 && frameCount < 60) automated.key.keysym.mod = KMOD_SHIFT;
            automated.key.keysym.sym = opt.profileBackends ? SDLK_e :
                SDLK_1 + (SDL_Keycode)((size_t)frameCount / 8 % count);
            SDL_PushEvent(&automated);
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
                autoOrbit = false;
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            } else if (ev.type == SDL_MOUSEMOTION && dragging) {
                orbitYaw -= (float)ev.motion.xrel * 0.008f;
                orbitPitch = Math_Clamp(orbitPitch + (float)ev.motion.yrel * 0.006f, -0.35f, 1.15f);
            } else if (ev.type == SDL_MOUSEWHEEL) {
                zoomScale = Math_Clamp(zoomScale * (ev.wheel.y > 0 ? 0.90f : 1.11f), MORPH_ZOOM_MIN, MORPH_ZOOM_MAX);
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                SDL_Keycode sym = ev.key.keysym.sym;
                if (sym == SDLK_ESCAPE) {
                    running = false;
                } else if (sym >= SDLK_1 && sym <= SDLK_9) {
                    size_t idx = (size_t)(sym - SDLK_1);
                    if (ev.key.keysym.mod & KMOD_SHIFT) idx += 9;
                    if (idx < catalogCount) {
                        Monster nextTarget = Monster_Create();
                        CreaturePhenotype p = catalog[idx].recipe->adult;
                        if (Creature_BuildMonster(&nextTarget, catalog[idx].recipe, &p)) {
                            if (SwitchMorphTarget(&rt, &nextTarget, catalog[idx].label, age)) {
                                age = 0.0f;
                                transitionPlaying = true;
                                pingPong = false;
                                transitionDir = 1.0f;
                                printf("[MUTAR] %s -> %s (0%% -> 100%%)\n", rt.sourceLabel, rt.targetLabel);
                            }
                        }
                        Monster_Free(&nextTarget);
                    }
                } else if (sym == SDLK_0) {
                    uint32_t nextSeed = NextRng(&rng);
                    Monster nextRandom = Monster_Create();
                    char rLabel[48] = {0};
                    size_t dummy = 0;
                    if (BuildRandomCreature(nextSeed, catalog, catalogCount, &nextRandom, rLabel, sizeof(rLabel), &dummy)) {
                        if (SwitchMorphTarget(&rt, &nextRandom, rLabel, age)) {
                            age = 0.0f;
                            transitionPlaying = true;
                            pingPong = false;
                            transitionDir = 1.0f;
                            printf("[MUTAR AZAR] %s -> %s (seed=%u)\n", rt.sourceLabel, rt.targetLabel, nextSeed);
                        }
                    }
                    Monster_Free(&nextRandom);
                } else if (sym == SDLK_SPACE) {
                    pingPong = !pingPong;
                    transitionPlaying = pingPong;
                } else if (sym == SDLK_w) {
                    Monster tmp = Monster_Clone(&rt.source);
                    char lbl[48];
                    snprintf(lbl, sizeof(lbl), "%s", rt.sourceLabel);
                    Monster_CopyInto(&rt.source, &rt.target);
                    snprintf(rt.sourceLabel, sizeof(rt.sourceLabel), "%s", rt.targetLabel);
                    Monster_CopyInto(&rt.target, &tmp);
                    snprintf(rt.targetLabel, sizeof(rt.targetLabel), "%s", lbl);
                    Monster_Free(&tmp);
                    CreatureMorph_Interpolate(&rt.source, &rt.target, 0.5f, &rt.reference);
                    bool snap = rt.ager.geometrySnapshotEnabled;
                    MonsterAger_Free(&rt.ager);
                    rt.ager = MonsterAger_Create(&rt.source, &rt.target, 1.0f - age);
                    InvalidateMorphPair(&rt);
                    MonsterAger_SetGeometrySnapshotEnabled(&rt.ager, snap);
                    age = 1.0f - age;
                    rt.lastHairExactAge = -1.0f;
                } else if (sym == SDLK_r) autoOrbit = !autoOrbit;
                else if (sym == SDLK_a) showSkeleton = !showSkeleton;
                else if (sym == SDLK_g) wire = !wire;
                else if (sym == SDLK_f) noFur = !noFur;
                else if (sym == SDLK_s) {
                    opt.surfaceDebugMode = (opt.surfaceDebugMode + 1) % 5;
                    OpenGLRenderer_SetSurfaceDebug(&renderer, opt.surfaceDebugMode);
                } else if (sym == SDLK_e) {
                    bool experimental = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
                    SurfaceMode next = SURFACE_MODE_LEGACY_HANDOFF;
                    switch (opt.surfaceMode) {
                        case SURFACE_MODE_LEGACY_HANDOFF:
                            next = experimental ? SURFACE_MODE_GPU_SDF_DENSE : SURFACE_MODE_SDF_RAYMARCH;
                            break;
                        case SURFACE_MODE_GPU_SDF_DENSE:
                            if (experimental) next = SURFACE_MODE_GPU_SDF_SPARSE;
                            break;
                        case SURFACE_MODE_GPU_SDF_SPARSE:
                            if (experimental) next = SURFACE_MODE_SDF_RAYMARCH;
                            break;
                        default:
                            break;
                    }
                    if ((next == SURFACE_MODE_GPU_SDF_DENSE || next == SURFACE_MODE_GPU_SDF_SPARSE) &&
                        !OpenGLRenderer_SupportsComputeSurface(&renderer)) next = SURFACE_MODE_SDF_RAYMARCH;
                    if (next != SURFACE_MODE_LEGACY_HANDOFF || EnsureHandoff(&rt, &opt)) {
                        opt.surfaceMode = next;
                        if (opt.profile) printf("PROFILE_BACKEND,%d,%s\n", frameCount, SurfaceModeName(next));
                        rt.gpuGridValid = false;
                        rt.rayFrameValid = false;
                        MonsterAger_SetGeometrySnapshotEnabled(&rt.ager,
                            next == SURFACE_MODE_LEGACY_HANDOFF && !opt.legacyLag);
                    } else {
                        fprintf(stderr, "No se pudo iniciar el worker de malla.\n");
                    }
                } else if (sym == SDLK_F1) opt.viewMode = 1;
                else if (sym == SDLK_F2) opt.viewMode = 2;
                else if (sym == SDLK_F3) opt.viewMode = 3;
                else if (sym == SDLK_F4) opt.viewMode = 4;
                else if (sym == SDLK_F5) opt.viewMode = 5;
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_RIGHT]) {
            age = Math_Clamp01(age + 0.45f * dt);
            transitionPlaying = false;
            pingPong = false;
        }
        if (keys[SDL_SCANCODE_LEFT]) {
            age = Math_Clamp01(age - 0.45f * dt);
            transitionPlaying = false;
            pingPong = false;
        }
        if (keys[SDL_SCANCODE_UP]) zoomScale = Math_Clamp(zoomScale - 0.7f * dt, MORPH_ZOOM_MIN, MORPH_ZOOM_MAX);
        if (keys[SDL_SCANCODE_DOWN]) zoomScale = Math_Clamp(zoomScale + 0.7f * dt, MORPH_ZOOM_MIN, MORPH_ZOOM_MAX);

        if (opt.ageSequenceCount > 0) {
            age = opt.ageSequence[frameCount % opt.ageSequenceCount];
        } else if (pingPong) {
            age += transitionDir * 0.28f * dt;
            if (age >= 1.0f) { age = 1.0f; transitionDir = -1.0f; }
            else if (age <= 0.0f) { age = 0.0f; transitionDir = 1.0f; }
        } else if (transitionPlaying) {
            age += 0.28f * dt;
            if (age >= 1.0f) {
                age = 1.0f;
                transitionPlaying = false;
            }
        }

        if (autoOrbit || opt.stressMotion) {
            orbitYaw += (opt.stressMotion ? 1.2f : 0.30f) * dt;
        }

        double phase = opt.profile ? OpenGLDemoWindow_Time() : 0;
        if (rt.ager.perc != age) MonsterAger_SetPerc(&rt.ager, age);
        double morphMs = opt.profile ? (OpenGLDemoWindow_Time() - phase) * 1000 : 0;
        const Monster* cur = MonsterAger_GetResultConst(&rt.ager);
        rt.sdfMs = rt.meshMs = rt.fallbackMs = rt.eyeMs = 0;
        const MonsterSDF* frameSdf = PrepareFrameField(&rt, &opt);
        Vector3 bc, hc, tc;
        float br = 3.5f;
        MorphViewCamera_Framing(cur, frameSdf, &bc, &br, &hc, &tc);
        MorphViewCamera_Setup(&cam, opt.viewMode, orbitYaw, orbitPitch, zoomScale, frameSdf, bc, br, hc, tc);

        int pct = (int)roundf(age * 100.0f);
        snprintf(titleBuf, sizeof(titleBuf),
            "Monster Morph [%s]  %s -> %s  (%d%%)", SurfaceModeName(opt.surfaceMode),
            rt.sourceLabel, rt.targetLabel, pct);
        OpenGLDemoWindow_SetTitle(window, titleBuf);

        char keysLine[256] = "0:AZAR";
        size_t off = strlen(keysLine);
        for (size_t i = 0; i < catalogCount && i < 9 && off + 24 < sizeof(keysLine); ++i) {
            off += (size_t)snprintf(keysLine + off, sizeof(keysLine) - off, "  %zu:%s",
                i + 1, catalog[i].label);
        }
        snprintf(hudBuf, sizeof(hudBuf),
            "METAMORFOSIS UNIVERSAL X -> Y  [%s]\n"
            "ACTIVO: %s -> %s   EDAD: %3d%%\n"
            "%s%s\n"
            "ESPACIO:OSCILAR  IZQ/DER:EDAD  R:ORBITA  A:ESQUELETO",
            SurfaceModeName(opt.surfaceMode), rt.sourceLabel, rt.targetLabel, pct, keysLine,
            catalogCount > 9 ? "  MAYUS+1..9:MAS" : "");

        rt.interactiveScrub = !opt.capturePrefix && !opt.validateCyclePrefix &&
            ((opt.profile && opt.ageSequenceCount > 0) ||
             (opt.ageSequenceCount == 0 &&
              (transitionPlaying || pingPong || keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT])));
        int timer = -1;
        if (opt.profile) {
            for (int i = 0; i < TIMER_COUNT; ++i) {
                if (timerFrames[i] >= 0) {
                    GLint ready;
                    glGetQueryObjectiv(timers[2 * i + 1], GL_QUERY_RESULT_AVAILABLE, &ready);
                    if (ready) {
                        GLuint64 startNs, endNs;
                        glGetQueryObjectui64v(timers[2 * i], GL_QUERY_RESULT, &startNs);
                        glGetQueryObjectui64v(timers[2 * i + 1], GL_QUERY_RESULT, &endNs);
                        printf("PROFILE_GPU,%d,%.6f\n", timerFrames[i], (endNs - startNs) / 1e6);
                        timerFrames[i] = -1;
                    }
                }
                if (timer < 0 && timerFrames[i] < 0) timer = i;
            }
            if (frameCount < opt.profileWarmup) timer = -1;
            if (timer >= 0) glQueryCounter(timers[2 * timer], GL_TIMESTAMP);
        }
        RenderCurrentFrame(&rt, &renderer, &cam, width, height, &opt, age, wire, showSkeleton, noFur, hudBuf);
        if (timer >= 0) {
            glQueryCounter(timers[2 * timer + 1], GL_TIMESTAMP);
            timerFrames[timer] = frameCount;
        }
        if (!(opt.profile && opt.profileUnpaced) &&
            opt.surfaceMode == SURFACE_MODE_SDF_RAYMARCH)
            pendingRasterFrame = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        double submitted = opt.profile ? OpenGLDemoWindow_Time() : 0;
        OpenGLDemoWindow_Swap(window);
        if (opt.profile) {
            double finished = OpenGLDemoWindow_Time();
            if (frameCount == opt.profileWarmup - 1)
                printf("PROFILE_WARMUP,%.6f\n", (finished - loopStart) * 1000);
            if (opt.profileBackends && !OpenGLRenderer_CheckErrors()) backendCheckOk = running = false;
            OpenGLEyeStats eye = OpenGLRenderer_GetEyeStats(&renderer);
            if (frameCount < opt.profileWarmup) {
                previousEye = eye;
                ++frameCount;
                continue;
            }
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpuEnd);
            double cpuMs = (cpuEnd.tv_sec - cpuStart.tv_sec) * 1000.0 +
                           (cpuEnd.tv_nsec - cpuStart.tv_nsec) / 1e6;
            struct rusage usage = {0};
            getrusage(RUSAGE_SELF, &usage);
            MonsterVisualAsyncStats stats = {0};
            if (rt.visual && opt.surfaceMode == SURFACE_MODE_LEGACY_HANDOFF) {
                stats = MonsterVisualAsync_GetStats(rt.visual);
            }
            GpuSDFSurfaceStats compute = {0};
            if (opt.surfaceMode == SURFACE_MODE_GPU_SDF_DENSE || opt.surfaceMode == SURFACE_MODE_GPU_SDF_SPARSE)
                compute = OpenGLRenderer_GetGpuSDFSurfaceStats(&renderer);
            if (compute.timingGeneration > computeTimingGeneration) {
                computeTimingGeneration = compute.timingGeneration;
                printf("PROFILE_COMPUTE,%llu,%.6f,%.6f,%.6f,%.6f\n",
                    (unsigned long long)computeTimingGeneration, compute.samplingGPUms,
                    compute.classificationGPUms, compute.emissionGPUms, compute.renderGPUms);
            }
            printf("PROFILE_FRAME,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%ld,"
                   "%.6f,%.6f,%.6f,%.6f,%llu,%llu,%llu,%llu,%.6f,%.6f,%zu,%.6f,%d,%d\n",
                frameCount, age, (finished - now) * 1000, cpuMs, (submitted - now) * 1000,
                rt.sdfMs, morphMs, rt.meshMs, rt.fallbackMs, rt.eyeMs, usage.ru_maxrss,
                OpenGLRenderer_GetSDFResolutionScale(&renderer), stats.workerCPUms, stats.displayPublicationMs,
                isfinite(stats.geometryLag) ? stats.geometryLag : 0,
                (unsigned long long)stats.coalescedCount, (unsigned long long)stats.cancelledBuildCount,
                (unsigned long long)stats.obsoleteTopologyResultCount,
                (unsigned long long)(eye.textureMisses - previousEye.textureMisses),
                eye.generationMs - previousEye.generationMs, eye.uploadMs - previousEye.uploadMs, compute.gpuBytes,
                stats.requestToDisplayMs, rt.visual && opt.surfaceMode == SURFACE_MODE_LEGACY_HANDOFF &&
                    isfinite(stats.pendingGeometryAge), stats.isWorkerBusy);
            previousEye = eye;
        }
        if (!opt.profile) OpenGLDemoWindow_LimitFPS(now, 60.0);

        ++frameCount;
        if (opt.maxFrames > 0 && frameCount >= opt.maxFrames) running = false;
    }

    if (pendingRasterFrame) glDeleteSync(pendingRasterFrame);
    if (opt.profile) glDeleteQueries(TIMER_COUNT * 2, timers);
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    DestroyMorphRuntime(&rt);
    return backendCheckOk ? 0 : 1;
}
