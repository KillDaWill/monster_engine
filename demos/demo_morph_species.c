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
#include "MathUtils.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "MonsterVisualAsync.h"
#include "LongHair.h"
#include "OpenGLRenderer.h"
#include "SurfaceMapper.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CATALOG_RECIPES 16
#define MAX_CYCLE_SAMPLES 11

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
} DemoOptions;

typedef struct MorphRuntime {
    Monster source;
    Monster target;
    Monster reference;
    MonsterAger ager;
    MonsterVisualAsync* visual;
    LongHairSet longHair;
    LongHairAsync* longHairAsync;
    float lastHairExactAge;
    size_t lastHairGroupCount;
    bool interactiveScrub;
    GpuSDFSurfaceGrid gpuGrid;
    GpuSDFSurfaceCache* gpuCache;
    bool gpuGridValid;
    bool hasVisual;
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
    else if (EqualsIgnoreCase(spec, "explorer")) spec = "Explorador";

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
    MonsterAger_SetGeometrySnapshotEnabled(&rt->ager, !opt->legacyLag);

    if (opt->surfaceMode == SURFACE_MODE_LEGACY_HANDOFF) {
        MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
        cfg.enableLocalMorphPatches = true;
        cfg.enableChunkedRemeshing = true;
        if (opt->chunkSize > 0u) cfg.chunkSize = (float)opt->chunkSize * 0.03f;
        rt->visual = MonsterVisualAsync_Create(cfg);
        if (!rt->visual) return false;
        rt->hasVisual = true;
        MonsterVisualAsync_SetMorphMode(rt->visual, true);
        MonsterVisualAsync_SetMorphEndpoints(rt->visual, &rt->source, &rt->target);
        if (opt->workerDelayMs > 0) MonsterVisualAsync_SetWorkerDelay(rt->visual, opt->workerDelayMs);
    }
    return true;
}

static void DestroyMorphRuntime(MorphRuntime* rt) {
    if (rt->gpuCache) {
        GpuSDFSurfaceCache_Destroy(rt->gpuCache);
        rt->gpuCache = NULL;
    }
    if (rt->hasVisual && rt->visual) {
        MonsterVisualAsync_Free(rt->visual);
        rt->visual = NULL;
        rt->hasVisual = false;
    }
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
    MonsterAger_SetGeometrySnapshotEnabled(&rt->ager, geomSnap);
    if (geomSteps) MonsterAger_SetGeometrySteps(&rt->ager, geomSteps);

    if (rt->hasVisual && rt->visual) {
        MonsterVisualAsync_SetMorphEndpoints(rt->visual, &rt->source, &rt->target);
    }
    if (rt->gpuCache) {
        GpuSDFSurfaceCache_Destroy(rt->gpuCache);
        rt->gpuCache = NULL;
    }
    rt->gpuGridValid = false;
    return true;
}

static void ComputeMonsterFraming(const Monster* m, const MonsterSDF* sdf, Vector3* outCenter, float* outRadius,
    Vector3* outHeadCenter, Vector3* outTailCenter) {
    Vector3 minP = Vec3_Create(1e6f, 1e6f, 1e6f), maxP = Vec3_Create(-1e6f, -1e6f, -1e6f);
    bool valid = false;

    if (sdf && sdf->hasSurface && isfinite(sdf->bounds.start.x) && isfinite(sdf->bounds.end.x)) {
        minP = sdf->bounds.start;
        maxP = sdf->bounds.end;
        valid = true;
    } else if (m && m->hasAnatomyGraph && m->anatomyGraph.nodeCount > 0) {
        for (size_t i = 0; i < m->anatomyGraph.nodeCount; ++i) {
            const AnatomyNode* n = &m->anatomyGraph.nodes[i];
            if (n->development <= 1e-4f && n->widthRadius <= 2e-4f) continue;
            float rx = fmaxf(n->widthRadius, n->envelopeRadii.x);
            float ry = fmaxf(n->heightRadius, n->envelopeRadii.y);
            float rz = fmaxf(rx, n->envelopeRadii.z);
            minP.x = fminf(minP.x, n->center.x - rx); maxP.x = fmaxf(maxP.x, n->center.x + rx);
            minP.y = fminf(minP.y, n->center.y - ry); maxP.y = fmaxf(maxP.y, n->center.y + ry);
            minP.z = fminf(minP.z, n->center.z - rz); maxP.z = fmaxf(maxP.z, n->center.z + rz);
            valid = true;
        }
    }

    if (!valid) {
        *outCenter = Vec3_Create(0.0f, 1.8f, 0.0f);
        *outRadius = 3.5f;
    } else {
        *outCenter = Vec3_Scale(Vec3_Add(minP, maxP), 0.5f);
        Vector3 ext = Vec3_Scale(Vec3_Sub(maxP, minP), 0.5f);
        *outRadius = fmaxf(1.5f, sqrtf(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z));
    }

    *outHeadCenter = *outCenter;
    *outTailCenter = *outCenter;
    if (m && m->hasAnatomyGraph) {
        const AnatomyNode* head = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_HEAD);
        if (head) *outHeadCenter = head->center;
        const AnatomyNode* tail = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_TAIL);
        if (!tail) tail = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_PELVIS);
        if (tail) *outTailCenter = tail->center;
    }
}

static void SetupViewCamera(ICamera* cam, int viewMode, float orbitYaw, float orbitPitch, float zoomScale,
    Vector3 bodyCenter, float bodyRadius, Vector3 headCenter, Vector3 tailCenter) {
    float dist = bodyRadius * 2.05f * Math_Clamp(zoomScale, 0.35f, 3.0f);
    Vector3 focus = bodyCenter;
    float yaw = orbitYaw, pitch = orbitPitch;

    if (viewMode == 1) {
        /* Lateral */
        yaw = 1.570796f + orbitYaw * 0.15f;
        pitch = 0.08f;
    } else if (viewMode == 2) {
        /* Frontal */
        yaw = 0.05f + orbitYaw * 0.15f;
        pitch = 0.10f;
    } else if (viewMode == 4) {
        /* Primer plano cabeza */
        focus = headCenter;
        dist = fmaxf(2.0f, bodyRadius * 0.65f * Math_Clamp(zoomScale, 0.35f, 3.0f));
        yaw = 0.65f + orbitYaw * 0.35f;
        pitch = 0.18f;
    } else if (viewMode == 5) {
        /* Vista caudal */
        focus = tailCenter;
        dist = fmaxf(2.5f, bodyRadius * 0.85f * Math_Clamp(zoomScale, 0.35f, 3.0f));
        yaw = 2.65f + orbitYaw * 0.35f;
        pitch = 0.22f;
    }

    float cp = cosf(pitch);
    cam->target = focus;
    cam->position = Vec3_Add(focus, Vec3_Create(
        sinf(yaw) * cp * dist,
        sinf(pitch) * dist + bodyRadius * 0.06f,
        cosf(yaw) * cp * dist));
    cam->up = Vec3_Create(0.0f, 1.0f, 0.0f);
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
        .surfaceMode = SURFACE_MODE_SDF_RAYMARCH
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
        else if (strcmp(arg, "--capture-normals") == 0) opt.surfaceDebugMode = 1;
        else if (strcmp(arg, "--capture-flat") == 0) opt.surfaceDebugMode = 2;
        else if (strcmp(arg, "--capture-albedo") == 0) opt.surfaceDebugMode = 3;
        else if (strcmp(arg, "--capture-semantic") == 0) opt.surfaceDebugMode = 4;
    }
    return opt;
}

static bool EnsureGpuSurfaceGrid(MorphRuntime* rt, const DemoOptions* opt, float restoreAge) {
    if (rt->gpuGridValid) return true;
    AABB3D bounds = AABB_Empty();
    MonsterSDF sdf = MonsterSDF_Create();
    const float ages[3] = {0.0f, 0.5f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        MonsterAger_SetPerc(&rt->ager, ages[i]);
        if (MonsterSDF_Build(&sdf, MonsterAger_GetResultConst(&rt->ager), MonsterSDF_DefaultConfig())) {
            AABB_ExpandPoint(&bounds, sdf.bounds.start);
            AABB_ExpandPoint(&bounds, sdf.bounds.end);
        }
    }
    MonsterAger_SetPerc(&rt->ager, restoreAge);
    MonsterSDF_Free(&sdf);
    bounds.start = Vec3_Sub(bounds.start, Vec3_Create(0.6f, 0.6f, 0.6f));
    bounds.end = Vec3_Add(bounds.end, Vec3_Create(0.6f, 0.6f, 0.6f));

    if (!GpuSDFSurfaceGrid_Configure(&rt->gpuGrid, bounds, opt->gpuVoxelStep, opt->gpuRefinement)) {
        return false;
    }
    rt->gpuGridValid = true;
    if (opt->surfaceMode == SURFACE_MODE_GPU_SDF_SPARSE) {
        if (rt->gpuCache) GpuSDFSurfaceCache_Destroy(rt->gpuCache);
        rt->gpuCache = GpuSDFSurfaceCache_Create(&rt->gpuGrid, opt->gpuBrickCells);
        if (!rt->gpuCache) return false;
        rt->gpuCache->cachedNormals = opt->gpuCachedNormals;
    }
    return true;
}

static void RenderCurrentFrame(MorphRuntime* rt, Renderer3D* renderer, ICamera* cam, int w, int h,
    const DemoOptions* opt, float currentAge, bool wire, bool showSkeleton, bool noFur,
    const char* hudText) {
    const Monster* current = MonsterAger_GetResultConst(&rt->ager);
    MonsterSDF sdf = MonsterSDF_Create();
    MonsterSDFConfig cfg = MonsterSDF_DefaultConfig();
    bool hasSdf = MonsterSDF_Build(&sdf, current, cfg);

    OpenGLRenderer_SetWireframe(renderer, wire);
    OpenGLRenderer_SetMorphSurfaceDomain(renderer, current, &rt->reference);
    OpenGLRenderer_SetFurQuality(renderer, !noFur, !noFur, 0);
    OpenGLRenderer_SetLongHairQuality(renderer, noFur ? 0 : 3);
    bool drewAsyncLongHair = false;

    renderer->beginFrame(renderer);

    if (opt->surfaceMode == SURFACE_MODE_SDF_RAYMARCH || opt->surfaceMode == SURFACE_MODE_EXACT_SDF) {
        OpenGLRenderer_SetSDFReference(renderer, opt->surfaceMode == SURFACE_MODE_EXACT_SDF,
            opt->referenceMaxStep, opt->referenceSteps);
        if (hasSdf && OpenGLRenderer_BeginSDFFrame(renderer, cam, w, h,
                opt->surfaceMode == SURFACE_MODE_SDF_RAYMARCH && !opt->capturePrefix && !opt->validateCyclePrefix)) {
            OpenGLRenderer_RenderSDF(renderer, &sdf, cam, w, h);
            OpenGLRenderer_EndSDFFrame(renderer, w, h);
        }
    } else if (opt->surfaceMode == SURFACE_MODE_GPU_SDF_DENSE && hasSdf) {
        if (EnsureGpuSurfaceGrid(rt, opt, currentAge)) {
            OpenGLRenderer_SetupCamera(cam, w, h);
            OpenGLRenderer_RenderGpuSDFSurface(renderer, &sdf, &rt->gpuGrid, cam);
        }
    } else if (opt->surfaceMode == SURFACE_MODE_GPU_SDF_SPARSE && hasSdf) {
        if (EnsureGpuSurfaceGrid(rt, opt, currentAge) && rt->gpuCache) {
            OpenGLRenderer_SetupCamera(cam, w, h);
            OpenGLRenderer_RenderGpuSparseSDFSurface(renderer, &sdf, rt->gpuCache, cam);
        }
    } else if (opt->surfaceMode == SURFACE_MODE_LEGACY_HANDOFF && rt->hasVisual) {
        const Monster* geomMonster = MonsterAger_GetGeometryResultConst(&rt->ager);
        MonsterVisualAsync_UpdateWithAppearance(rt->visual, geomMonster, current, 0.016f);
        MorphFallbackRegion regions[64];
        size_t regionCount = MonsterVisualAsync_GetFallbackRegions(rt->visual, hasSdf ? &sdf : NULL,
            &current->anatomyGraph, currentAge, 0.08f, regions, 64);
        size_t masked = 0;
        const Mesh* ownedMesh = MonsterVisualAsync_GetOwnedMesh(rt->visual, regions, regionCount, &masked);
        OpenGLRenderer_SetupCamera(cam, w, h);
        AABB3D clipBoxes[32];
        size_t clipCount = regionCount < 32 ? regionCount : 32;
        for (size_t i = 0; i < clipCount; ++i) clipBoxes[i] = regions[i].bounds;
        OpenGLRenderer_SetMeshClipRegions(renderer, clipBoxes, clipCount);
        if (ownedMesh) {
            SurfacePhenotype resolvedSurface = current->surface;
            SurfacePhenotype_CopyAnchoredDomains(&resolvedSurface, &current->phenotype.surface);
            SurfaceMapper_ResolvePigmentDomains(&resolvedSurface, &rt->reference.anatomyGraph, &rt->reference.surfaceMapping);
            SurfaceRecipe compiledSurface = SurfaceRecipe_CompileScaled(&resolvedSurface, current->surfaceMapping.unitScale);
            OpenGLRenderer_SetMeshSurfaceOverride(renderer, &compiledSurface);
            renderer->renderMesh(renderer, ownedMesh);
        } else {
            OpenGLRenderer_SetMeshSurfaceOverride(renderer, NULL);
            MonsterVisualAsync_Render(rt->visual, renderer);
            drewAsyncLongHair = true;
        }
        OpenGLRenderer_SetMeshClipRegions(renderer, NULL, 0);
        if (clipCount > 0 && hasSdf) {
            OpenGLRenderer_RenderSDFRegions(renderer, &sdf, cam, w, h, clipBoxes, clipCount);
        }
        MonsterVisualAsync_RecordFallbackFrame(rt->visual, regions, regionCount, clipCount, ownedMesh != NULL, masked);
    }

    OpenGLRenderer_SetupCamera(cam, w, h);
    if (!drewAsyncLongHair && !noFur && renderer->renderLongHair) {
        bool useAsyncHair = !opt->capturePrefix && !opt->validateCyclePrefix && !opt->ageSequenceCount;
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

    if (opt->validateCompute && hasSdf) {
        Vector3 pts[64];
        for (int i = 0; i < 64; ++i) {
            float fx = ((i & 3) - 1.5f) * 0.8f;
            float fy = (((i >> 2) & 3) * 0.9f) + 0.3f;
            float fz = (((i >> 4) & 3) - 1.5f) * 1.1f;
            pts[i] = Vec3_Create(fx, fy, fz);
        }
        float maxErr = 0.0f;
        OpenGLRenderer_ValidateComputeSDF(renderer, &sdf, pts, 64, &maxErr);
        printf("[COMPUTE] edad=%.2f maxErr=%.5f\n", currentAge, maxErr);
    }

    MonsterSDF_Free(&sdf);
    renderer->endFrame(renderer);
}

int main(int argc, char** argv) {
    DemoOptions opt = ParseOptions(argc, argv);

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

    printf("\n=== DEMO METAMORFOSIS UNIVERSAL X -> Y (seed=%u) ===\n", seed);
    printf("  Inicio: %s  (Objetivo inicial: %s)\n", rt.sourceLabel, rt.targetLabel);
    printf("  Controles numéricos para mutar la criatura actual hacia:\n");
    printf("    [0] Nueva criatura ALEATORIA\n");
    for (size_t i = 0; i < catalogCount && i < 9; ++i) {
        printf("    [%zu] %s (ID %u)\n", i + 1, catalog[i].recipe->name, (unsigned)catalog[i].id);
    }
    printf("  Otros controles: ESPACIO=auto ida/vuelta | IZQ/DER=edad manual | W=invertir\n");
    printf("    F1..F5=cámara | R=rotación orbital | A=esqueleto | E=modo render | G=wire | ESC=salir\n\n");

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
    float orbitYaw = 0.72f, orbitPitch = 0.22f, zoomScale = 1.0f;
    bool dragging = false;

    /* Modo captura estática (--capture-prefix) */
    if (opt.capturePrefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s.ppm", opt.capturePrefix);
        MonsterAger_SetPerc(&rt.ager, age);
        const Monster* cur = MonsterAger_GetResultConst(&rt.ager);
        MonsterSDF sdf = MonsterSDF_Create();
        MonsterSDF_Build(&sdf, cur, MonsterSDF_DefaultConfig());
        Vector3 bc, hc, tc;
        float br = 3.5f;
        ComputeMonsterFraming(cur, &sdf, &bc, &br, &hc, &tc);
        MonsterSDF_Free(&sdf);
        SetupViewCamera(&cam, opt.viewMode, orbitYaw, orbitPitch, zoomScale, bc, br, hc, tc);
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
            MonsterSDF sdf = MonsterSDF_Create();
            MonsterSDF_Build(&sdf, cur, MonsterSDF_DefaultConfig());
            Vector3 bc, hc, tc;
            float br = 3.5f;
            ComputeMonsterFraming(cur, &sdf, &bc, &br, &hc, &tc);
            MonsterSDF_Free(&sdf);
            SetupViewCamera(&cam, opt.viewMode, orbitYaw, orbitPitch, zoomScale, bc, br, hc, tc);
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
    char titleBuf[160], hudBuf[512];

    while (running) {
        double now = OpenGLDemoWindow_Time();
        float dt = opt.fixedTimestep > 0.0f ? opt.fixedTimestep : (float)fmin(0.05, now - lastTime);
        lastTime = now;

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
                zoomScale = Math_Clamp(zoomScale * (ev.wheel.y > 0 ? 0.90f : 1.11f), 0.35f, 3.0f);
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                SDL_Keycode sym = ev.key.keysym.sym;
                if (sym == SDLK_ESCAPE) {
                    running = false;
                } else if (sym >= SDLK_1 && sym <= SDLK_9) {
                    size_t idx = (size_t)(sym - SDLK_1);
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
                    opt.surfaceMode = (SurfaceMode)(((int)opt.surfaceMode + 1) % 4);
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
        if (keys[SDL_SCANCODE_UP]) zoomScale = Math_Clamp(zoomScale - 0.7f * dt, 0.35f, 3.0f);
        if (keys[SDL_SCANCODE_DOWN]) zoomScale = Math_Clamp(zoomScale + 0.7f * dt, 0.35f, 3.0f);

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

        MonsterAger_SetPerc(&rt.ager, age);
        const Monster* cur = MonsterAger_GetResultConst(&rt.ager);
        MonsterSDF frameSdf = MonsterSDF_Create();
        MonsterSDF_Build(&frameSdf, cur, MonsterSDF_DefaultConfig());
        Vector3 bc, hc, tc;
        float br = 3.5f;
        ComputeMonsterFraming(cur, &frameSdf, &bc, &br, &hc, &tc);
        MonsterSDF_Free(&frameSdf);

        SetupViewCamera(&cam, opt.viewMode, orbitYaw, orbitPitch, zoomScale, bc, br, hc, tc);

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
            "%s\n"
            "ESPACIO:OSCILAR  IZQ/DER:EDAD  R:ORBITA  A:ESQUELETO",
            SurfaceModeName(opt.surfaceMode), rt.sourceLabel, rt.targetLabel, pct, keysLine);

        rt.interactiveScrub = !opt.capturePrefix && !opt.validateCyclePrefix && opt.ageSequenceCount == 0 &&
            (transitionPlaying || pingPong || keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT]);
        RenderCurrentFrame(&rt, &renderer, &cam, width, height, &opt, age, wire, showSkeleton, noFur, hudBuf);
        OpenGLDemoWindow_Swap(window);
        OpenGLDemoWindow_LimitFPS(now, 60.0);

        ++frameCount;
        if (opt.maxFrames > 0 && frameCount >= opt.maxFrames) running = false;
    }

    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    DestroyMorphRuntime(&rt);
    return 0;
}
