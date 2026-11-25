/**
 * @file demo_evolution_arena.c
 * @brief Evolution Arena visual escalable: laboratorio interactivo desacoplado sobre
 *        EvolutionSimulationRunner (hilo dedicado + triple buffering), instancing GPU,
 *        LOD automático y diagnóstico de rendimiento para 500+ criaturas.
 */
#include <GL/gl.h>
#include "EvolutionArena.h"
#include "EvolutionSimulationRunner.h"
#include "EvolutionProfiler.h"
#include "EcosystemVisual.h"
#include "CreatureRecipeRegistry.h"
#include "EnvironmentRenderer.h"
#include "WorldCollision.h"
#include "OpenGLRenderer.h"
#include "PrimitiveMesh.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>

#define ARENA_DEFAULT_W 1280
#define ARENA_DEFAULT_H 800
#define ARENA_MAX_FPS 60.0
#define ARENA_HISTORY_LEN 180u
#define ARENA_FRAME_RING 120u

typedef enum ViewMode { VIEW_SIDE, VIEW_FRONT, VIEW_THREE_QUARTER, VIEW_OVERVIEW } ViewMode;
typedef enum CaptureState {
    CAPTURE_OVERVIEW, CAPTURE_ADULT, CAPTURE_JUVENILE, CAPTURE_NEWBORN,
    CAPTURE_WALKING, CAPTURE_DRINKING, CAPTURE_HUNTING, CAPTURE_FLEEING,
    CAPTURE_FEEDING, CAPTURE_RESTING, CAPTURE_ATTACKING, CAPTURE_TURNING,
    CAPTURE_MATING
} CaptureState;
typedef enum PromptMode { PROMPT_NONE = 0, PROMPT_EXPORT = 1, PROMPT_IMPORT = 2 } PromptMode;
typedef enum QualityPreset { QUALITY_LOW = 0, QUALITY_MEDIUM = 1, QUALITY_HIGH = 2 } QualityPreset;

typedef struct UiButton {
    float x, y, w, h;
    const char *label;
} UiButton;

typedef struct LineBatch {
    Vector3 *verts;
    Color *cols;
    size_t count; /* Número de segmentos (2 vértices por segmento). */
    size_t capacity;
} LineBatch;

typedef struct InstanceBatch {
    OpenGLMeshInstance *items;
    size_t count;
    size_t capacity;
} InstanceBatch;

typedef struct MeshGroup {
    const Mesh *mesh;
    size_t start;
    size_t count;
} MeshGroup;

static AIObjectId turningEntity = 0;
static float observedTurnRate = 0.0f;

static int compareDouble(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static const char *qualityName(QualityPreset q) {
    switch (q) {
    case QUALITY_LOW: return "LOW";
    case QUALITY_MEDIUM: return "MED";
    case QUALITY_HIGH: return "HIGH";
    default: return "MED";
    }
}

static void applyQualityBudget(EcosystemVisual *visual, QualityPreset q) {
    if (q == QUALITY_LOW) {
        EcosystemVisual_SetBudget(visual, 3u, 10u, 0.75f);
    } else if (q == QUALITY_MEDIUM) {
        EcosystemVisual_SetBudget(visual, 6u, 20u, 1.00f);
    } else {
        EcosystemVisual_SetBudget(visual, 12u, 32u, 1.15f);
    }
}

static void lineBatchFree(LineBatch *b) {
    free(b->verts);
    free(b->cols);
    memset(b, 0, sizeof(*b));
}

static void lineBatchClear(LineBatch *b) {
    b->count = 0;
}

static void lineBatchPush(LineBatch *b, Vector3 a, Vector3 p, Color c) {
    if (b->count >= b->capacity) {
        size_t nc = b->capacity ? b->capacity * 2u : 1024u;
        Vector3 *nv = realloc(b->verts, nc * 2u * sizeof(*nv));
        Color *ncl = realloc(b->cols, nc * sizeof(*ncl));
        if (!nv || !ncl) {
            if (nv) b->verts = nv;
            if (ncl) b->cols = ncl;
            return;
        }
        b->verts = nv;
        b->cols = ncl;
        b->capacity = nc;
    }
    size_t idx = b->count * 2u;
    b->verts[idx] = a;
    b->verts[idx + 1u] = p;
    b->cols[b->count] = c;
    ++b->count;
}

static void lineBatchPushRing(LineBatch *b, Vector3 p, float radius, float yOffset, unsigned segs, Color c) {
    if (segs < 8u) segs = 8u;
    float step = 6.2831853f / (float)segs;
    for (unsigned i = 0; i < segs; ++i) {
        float a = (float)i * step, bb = (float)(i + 1u) * step;
        lineBatchPush(b,
            Vec3_Add(p, Vec3_Create(sinf(a) * radius, yOffset, cosf(a) * radius)),
            Vec3_Add(p, Vec3_Create(sinf(bb) * radius, yOffset, cosf(bb) * radius)), c);
    }
}

static void lineBatchPushTerrainRing(LineBatch *b, const EnvironmentRenderer *env,
                                     Vector3 p, float radius, float yOffset, unsigned segs, Color c) {
    if (!env || !EnvironmentRenderer_IsProcedural(env)) {
        lineBatchPushRing(b, p, radius, yOffset, segs, c);
        return;
    }
    if (segs < 8u) segs = 8u;
    float step = 6.2831853f / (float)segs;
    for (unsigned i = 0; i < segs; ++i) {
        float a = (float)i * step, bb = (float)(i + 1u) * step;
        float ax = p.x + sinf(a) * radius, az = p.z + cosf(a) * radius;
        float bx = p.x + sinf(bb) * radius, bz = p.z + cosf(bb) * radius;
        float ay = EnvironmentRenderer_SampleGroundHeight(env, ax, az) + yOffset;
        float by = EnvironmentRenderer_SampleGroundHeight(env, bx, bz) + yOffset;
        lineBatchPush(b, Vec3_Create(ax, ay, az), Vec3_Create(bx, by, bz), c);
    }
}

static bool parseTerrainDebugMode(const char *s, TerrainDebugMode *out) {
    if (!s || !out) return false;
    if (!strcmp(s, "natural")) *out = TERRAIN_DEBUG_NATURAL;
    else if (!strcmp(s, "elevation") || !strcmp(s, "height")) *out = TERRAIN_DEBUG_ELEVATION;
    else if (!strcmp(s, "slope")) *out = TERRAIN_DEBUG_SLOPE;
    else if (!strcmp(s, "hydrology") || !strcmp(s, "hydro")) *out = TERRAIN_DEBUG_HYDROLOGY;
    else if (!strcmp(s, "depth") || !strcmp(s, "water")) *out = TERRAIN_DEBUG_WATER_DEPTH;
    else if (!strcmp(s, "biome") || !strcmp(s, "fertility")) *out = TERRAIN_DEBUG_BIOME;
    else if (!strcmp(s, "nav") || !strcmp(s, "navigation")) *out = TERRAIN_DEBUG_NAVIGATION;
    else if (!strcmp(s, "collision") || !strcmp(s, "rocks")) *out = TERRAIN_DEBUG_COLLISION;
    else return TerrainDebugMode_Parse(s, out);
    return true;
}

static void instanceBatchFree(InstanceBatch *b) {
    free(b->items);
    memset(b, 0, sizeof(*b));
}

static bool instanceBatchPush(InstanceBatch *b, OpenGLMeshInstance item) {
    if (b->count >= b->capacity) {
        size_t nc = b->capacity ? b->capacity * 2u : 256u;
        OpenGLMeshInstance *ni = realloc(b->items, nc * sizeof(*ni));
        if (!ni) return false;
        b->items = ni;
        b->capacity = nc;
    }
    b->items[b->count++] = item;
    return true;
}

static size_t componentRoot(size_t *parent, size_t i) {
    while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}

static void reportMeshIslands(const Mesh *mesh) {
    if (!mesh || !mesh->vertexCount) return;
    size_t *parent = malloc(mesh->vertexCount * sizeof(*parent));
    size_t *counts = calloc(mesh->vertexCount, sizeof(*counts));
    if (!parent || !counts) { free(parent); free(counts); return; }
    for (size_t i = 0; i < mesh->vertexCount; ++i) parent[i] = i;
    for (size_t i = 0; i + 2 < mesh->indexCount; i += 3)
        for (unsigned j = 1; j < 3; ++j)
            parent[componentRoot(parent, mesh->indices[i + j])] = componentRoot(parent, mesh->indices[i]);
    for (size_t i = 0; i + 2 < mesh->indexCount; i += 3) ++counts[componentRoot(parent, mesh->indices[i])];
    for (size_t root = 0; root < mesh->vertexCount; ++root) if (counts[root] && counts[root] < 128) {
        AABB3D box = AABB_Empty(); SDFMaterial material = SDF_MATERIAL_UNKNOWN;
        for (size_t i = 0; i < mesh->vertexCount; ++i)
            if (componentRoot(parent, i) == root) { AABB_ExpandPoint(&box, mesh->vertices[i].position); material = mesh->vertices[i].material; }
        printf("mesh_island triangles=%zu material=%u min=%g,%g,%g max=%g,%g,%g\n",
               counts[root], material, box.start.x, box.start.y, box.start.z, box.end.x, box.end.y, box.end.z);
    }
    free(parent); free(counts);
}

static bool hitRect(int mx, int my, float x, float y, float w, float h) {
    return (float)mx >= x && (float)mx <= x + w && (float)my >= y && (float)my <= y + h;
}

static void ensureParentDir(const char *path) {
    if (!path) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return;
    *slash = '\0';
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static bool parseView(const char *value, ViewMode *out) {
    if (!strcmp(value, "side")) *out = VIEW_SIDE;
    else if (!strcmp(value, "front")) *out = VIEW_FRONT;
    else if (!strcmp(value, "three-quarter")) *out = VIEW_THREE_QUARTER;
    else if (!strcmp(value, "overview")) *out = VIEW_OVERVIEW;
    else return false;
    return true;
}

static bool parseCaptureState(const char *value, CaptureState *out) {
    const char *names[] = {
        "overview", "adult", "juvenile", "newborn", "walking", "drinking",
        "hunting", "fleeing", "feeding", "resting", "attacking", "turning", "mating"
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!strcmp(value, names[i])) { *out = (CaptureState)i; return true; }
    return false;
}

static void drawMesh(Renderer3D *r, const Mesh *mesh, Vector3 p, float yaw, float scale) {
    if (!mesh || !mesh->vertexCount) return;
    OpenGLRenderer_ModelTransform(p, yaw * 57.29578f, scale, Vec3_Zero());
    r->renderMesh(r, mesh);
    OpenGLRenderer_PopModelMatrix();
}

static uint64_t packetGeneration(const EcosystemVisualDrawPacket *packet) {
    return packet->bodyMesh ? packet->bodyMesh->geometryGeneration : 1;
}

static const EvolutionEntitySnapshot *findSnapEntity(const EvolutionRenderSnapshot *snap, AIObjectId id) {
    if (!snap || !id) return NULL;
    for (unsigned i = 0; i < snap->entityCount; ++i) {
        if (snap->entities[i].id == id) return &snap->entities[i];
    }
    return NULL;
}

static bool parseCameraPoint(const char *text, Vector3 *point) {
    char extra;
    return sscanf(text, "%f,%f,%f%c", &point->x, &point->y, &point->z, &extra) == 3 &&
           isfinite(point->x) && isfinite(point->y) && isfinite(point->z);
}

static bool projectSnapEntity(const ICamera *camera, float orthoHeight, int width, int height,
                              const EvolutionEntitySnapshot *entity, EcosystemVisualProjection *projection) {
    Vector3 forward = Vec3_Normalize(Vec3_Sub(camera->target, camera->position));
    Vector3 right = Vec3_Normalize(Vec3_Cross(forward, camera->up));
    Vector3 up = Vec3_Normalize(Vec3_Cross(right, forward));
    float radius = fmaxf(0.18f, 0.75f * entity->developmentScale * fmaxf(0.1f, entity->genome.size));
    bool perspective = camera->fov > 0.0f;
    Vector3 relative = Vec3_Sub(entity->position, perspective ? camera->position : camera->target);
    float horizontal = fabsf(Vec3_Dot(relative, right));
    float vertical = fabsf(Vec3_Dot(relative, up));
    float depth = Vec3_Dot(Vec3_Sub(entity->position, camera->position), forward);
    float tanHalfFov = tanf(camera->fov * 0.00872664626f);
    float halfHeight = perspective ? tanHalfFov * fmaxf(camera->nearPlane, depth) : orthoHeight * 0.5f;
    float halfWidth = halfHeight * (float)width / (float)height;
    projection->visible = depth + radius >= camera->nearPlane && depth - radius <= camera->farPlane &&
                          horizontal <= halfWidth + radius && vertical <= halfHeight + radius;
    projection->projectedPixels = radius * (float)height / halfHeight;
    return isfinite(projection->projectedPixels);
}

static void setCameraPose(ICamera *camera, Vector3 target, float zoom, double angle,
                          ViewMode view, bool inspection) {
    float x, z;
    if (view == VIEW_SIDE) { x = cosf((float)angle); z = -sinf((float)angle); }
    else if (view == VIEW_FRONT) { x = sinf((float)angle); z = cosf((float)angle); }
    else if (view == VIEW_OVERVIEW) {
        camera->target = target;
        camera->position = Vec3_Add(target, Vec3_Create(0.0f, zoom * 1.25f, 0.01f));
        camera->up = Vec3_Create(0.0f, 0.0f, -1.0f);
        return;
    } else { x = sinf((float)angle); z = cosf((float)angle); }
    target.y += inspection ? 0.0f : (zoom < 8.0f ? 0.55f : 0.0f);
    camera->target = target;
    camera->position = Vec3_Add(target, Vec3_Create(
        x * zoom,
        zoom * (view == VIEW_SIDE || view == VIEW_FRONT ? (inspection ? 0.0f : 0.18f)
                                                        : (inspection ? 0.20f : 0.48f)),
        z * zoom));
    camera->up = Vec3_Create(0.0f, 1.0f, 0.0f);
}

static bool matchesCaptureSnap(const EvolutionEntitySnapshot *entity, CaptureState state) {
    if (!entity || state == CAPTURE_OVERVIEW || entity->lifeStage == LIFE_DEAD) return false;
    switch (state) {
    case CAPTURE_ADULT:     return entity->lifeStage != LIFE_JUVENILE;
    case CAPTURE_JUVENILE:  return entity->lifeStage == LIFE_JUVENILE;
    case CAPTURE_NEWBORN:   return entity->lifeStage == LIFE_JUVENILE &&
                                   entity->ageSeconds <= entity->maturityAgeSeconds * 0.25f;
    case CAPTURE_WALKING:   return entity->speed > 0.05f;
    case CAPTURE_TURNING:   return entity->id == turningEntity && entity->speed > 0.05f && fabsf(observedTurnRate) > 0.1f;
    case CAPTURE_DRINKING:  return entity->action == ACTION_DRINK && entity->reachStatus == REACH_REACHED;
    case CAPTURE_FEEDING:   return entity->action == ACTION_EAT && entity->reachStatus == REACH_REACHED;
    case CAPTURE_ATTACKING: return entity->action == ACTION_ATTACK && entity->reachStatus == REACH_REACHED;
    case CAPTURE_RESTING:   return entity->action == ACTION_REST;
    case CAPTURE_HUNTING:   return entity->action == ACTION_CHASE || entity->action == ACTION_ATTACK || entity->goal == GOAL_HUNT;
    case CAPTURE_FLEEING:   return entity->action == ACTION_ESCAPE || entity->goal == GOAL_FLEE;
    case CAPTURE_MATING:    return entity->action == ACTION_APPROACH_MATE || entity->gestating;
    default:                return false;
    }
}

static double mouthFrameErrorSnap(const EvolutionRenderSnapshot *snap, const EcosystemVisualDrawPacket *packet, bool *hasSample) {
    const EvolutionEntitySnapshot *entity = findSnapEntity(snap, packet->entityId);
    if (!entity || !packet->ready || !packet->pose || packet->headJoint < 0) return 0.0;
    *hasSample = true;
    return Vec3_Distance(packet->renderedMouth, entity->mouth);
}

/* ========================================================================== */
/* Laboratorio 2D: Barras, Paneles, Minimap, Diagnóstico e Inspector Neuronal */
/* ========================================================================== */

static void drawPhysBar(float x, float y, float w, float h, const char *label,
                        float value, Color barColor) {
    float v = fmaxf(0.0f, fminf(1.0f, value));
    OpenGLRenderer_DrawText2D(x, y + 2.0f, 1.3f, Color_Create(164, 194, 206, 255), label);
    float bx = x + 64.0f, bw = w - 108.0f;
    OpenGLRenderer_DrawRect2D(bx, y + 1.0f, bw, h - 2.0f,
                              Color_Create(14, 24, 34, 240), Color_Create(42, 82, 98, 180));
    if (v > 0.005f) {
        OpenGLRenderer_DrawRect2D(bx + 1.0f, y + 2.0f, (bw - 2.0f) * v, h - 4.0f,
                                  barColor, Color_Create(0, 0, 0, 0));
    }
    char num[16];
    snprintf(num, sizeof(num), "%3.0f%%", v * 100.0f);
    OpenGLRenderer_DrawText2D(bx + bw + 6.0f, y + 2.0f, 1.3f, Color_Create(224, 238, 244, 255), num);
}

static void drawButton(const UiButton *b, bool active, bool accent) {
    Color bg = active ? Color_Create(26, 94, 110, 235)
                      : (accent ? Color_Create(18, 52, 66, 230) : Color_Create(16, 28, 38, 230));
    Color border = active ? Color_Create(86, 230, 244, 255) : Color_Create(48, 134, 152, 200);
    Color text = active ? Color_Create(240, 252, 255, 255) : Color_Create(206, 232, 240, 255);
    OpenGLRenderer_DrawRect2D(b->x, b->y, b->w, b->h, bg, border);
    OpenGLRenderer_DrawText2D(b->x + 6.0f, b->y + 6.0f, 1.25f, text, b->label);
}

static Color weightColor(float w, float alphaScale, bool recurrent) {
    float mag = fminf(1.0f, fabsf(w) / 3.0f);
    unsigned char a = (unsigned char)fmaxf(28.0f, fminf(240.0f, (40.0f + 200.0f * mag) * alphaScale));
    if (recurrent) {
        return w >= 0.0f ? Color_Create(246, 202, 76, a) : Color_Create(228, 126, 54, a);
    }
    return w >= 0.0f ? Color_Create(58, 214, 228, a) : Color_Create(232, 92, 86, a);
}

static Color activationFill(float val) {
    float mag = fminf(1.0f, fabsf(val));
    if (val >= 0.0f) {
        return Color_Create((unsigned char)(18 + 44 * mag),
                              (unsigned char)(42 + 178 * mag),
                              (unsigned char)(54 + 186 * mag), 245);
    }
    return Color_Create((unsigned char)(56 + 184 * mag),
                          (unsigned char)(28 + 64 * mag),
                          (unsigned char)(32 + 54 * mag), 245);
}

static Color colorFromRgbFloat(const float rgb[3]) {
    unsigned char r = (unsigned char)fmaxf(0.0f, fminf(255.0f, rgb[0] * 255.0f));
    unsigned char g = (unsigned char)fmaxf(0.0f, fminf(255.0f, rgb[1] * 255.0f));
    unsigned char b = (unsigned char)fmaxf(0.0f, fminf(255.0f, rgb[2] * 255.0f));
    return Color_Create(r, g, b, 255);
}

static Color moleculeBarColor(const char *name) {
    if (!name) return Color_Create(120, 178, 198, 235);
    if (strstr(name, "water") || strstr(name, "Water")) return Color_Create(72, 170, 240, 235);
    if (strstr(name, "toxin") || strstr(name, "Toxin")) return Color_Create(228, 72, 138, 240);
    if (strstr(name, "decay") || strstr(name, "Decay")) return Color_Create(184, 108, 196, 235);
    if (strstr(name, "protein") || strstr(name, "Protein")) return Color_Create(236, 118, 92, 235);
    if (strstr(name, "lipid") || strstr(name, "Lipid")) return Color_Create(240, 196, 78, 235);
    if (strstr(name, "carb") || strstr(name, "Carb")) return Color_Create(158, 224, 96, 235);
    if (strstr(name, "cellul") || strstr(name, "Cellul")) return Color_Create(92, 198, 126, 235);
    if (strstr(name, "fiber") || strstr(name, "Fiber")) return Color_Create(154, 134, 98, 235);
    return Color_Create(120, 206, 214, 235);
}

static const char *phaseLabel(SubstancePhase p) {
    switch (p) {
    case SUBSTANCE_PHASE_SOLID: return "SOLID";
    case SUBSTANCE_PHASE_LIQUID: return "LIQUID";
    case SUBSTANCE_PHASE_SEMI_SOLID: return "SEMI-SOLID";
    default: return "UNKNOWN";
    }
}

static const char *regenPolicyLabel(ResourceRegenPolicy p) {
    switch (p) {
    case RESOURCE_REGEN_NONE: return "NONE";
    case RESOURCE_REGEN_LINEAR: return "LINEAR";
    case RESOURCE_REGEN_INFINITE: return "INFINITE";
    case RESOURCE_REGEN_DEPLETE_RELOCATE: return "RELOCATE";
    default: return "NONE";
    }
}

static const char *decayPolicyLabel(ResourceDecayPolicy p) {
    switch (p) {
    case RESOURCE_DECAY_NONE: return "STATIC";
    case RESOURCE_DECAY_MASS_LOSS: return "MASS_LOSS";
    case RESOURCE_DECAY_DECOMPOSE: return "DECOMPOSE";
    default: return "STATIC";
    }
}

static const EvolutionResourceSnapshot *findSnapResource(const EvolutionRenderSnapshot *snap, AIObjectId id) {
    if (!snap || !id) return NULL;
    for (unsigned i = 0; i < snap->resourceCount; ++i) {
        if (snap->resources[i].id == id) return &snap->resources[i];
    }
    return NULL;
}

static void drawNeuralInspectorV2(const EvolutionRenderSnapshot *snap,
                                  const EvolutionSelectedSnapshot *sel) {
    const float px = 330.0f, py = 12.0f, pw = 616.0f;
    const NeuralGenome *g = &sel->genome;
    const NeuralBrainV2Runtime *rt = &sel->runtime.v2;
    const NeuralV2InspectorState *v2 = &sel->neuralState.v2;

    char header[160];
    snprintf(header, sizeof(header), "NEURAL V2 CANDIDATE POLICY  #%u  FP:%08llX  STEPS:%llu",
             sel->entityId, (unsigned long long)(g->metadata.fingerprint & 0xFFFFFFFFu),
             (unsigned long long)sel->runtime.stepCount);
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 8.0f, 1.30f, Color_Create(78, 226, 240, 255), header);

    /* 1) Memoria recurrente de contexto (16 neuronas) */
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 36.0f, 1.18f, Color_Create(142, 214, 228, 255),
                              "RECURRENT CONTEXT (16 HIDDEN)");
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        unsigned col = h % 8u, row = h / 8u;
        float cx = px + 26.0f + (float)col * 36.0f;
        float cy = py + 62.0f + (float)row * 30.0f;
        float v = rt->context[h];
        Color border = v >= 0.0f ? Color_Create(78, 224, 240, 235) : Color_Create(238, 102, 92, 235);
        OpenGLRenderer_DrawCircle2D(cx, cy, 10.0f, activationFill(v), border);
        char lbl[12];
        snprintf(lbl, sizeof(lbl), "C%u", h);
        OpenGLRenderer_DrawText2D(cx - 8.0f, cy - 3.5f, 0.95f, Color_Create(240, 250, 255, 255), lbl);
    }

    /* 2) Acciones globales + decisión seleccionada */
    float gx = px + 324.0f;
    OpenGLRenderer_DrawText2D(gx, py + 36.0f, 1.18f, Color_Create(142, 214, 228, 255),
                              "GLOBAL ACTIONS & SELECTED DECISION");
    static const char *gNames[NEURAL_V2_GLOBAL_ACTIONS] = {"IDLE", "EXPLORE", "REST"};
    for (unsigned i = 0; i < NEURAL_V2_GLOBAL_ACTIONS; ++i) {
        float sc = v2->globalScores[i];
        bool win = (v2->selectedAction == (NeuralV2ActionType)i);
        float norm = 0.5f + 0.5f * tanhf(sc * 0.5f);
        float ry = py + 54.0f + (float)i * 20.0f;
        OpenGLRenderer_DrawRect2D(gx, ry, 280.0f, 16.0f,
                                  win ? Color_Create(24, 72, 86, 235) : Color_Create(13, 22, 30, 220),
                                  win ? Color_Create(84, 236, 250, 245) : Color_Create(40, 74, 88, 165));
        OpenGLRenderer_DrawRect2D(gx + 1.0f, ry + 1.0f, 278.0f * norm, 14.0f,
                                  win ? Color_Create(44, 174, 196, 175) : Color_Create(36, 98, 118, 125),
                                  Color_Create(0, 0, 0, 0));
        char buf[64];
        snprintf(buf, sizeof(buf), "%-8s %+6.2f %s", gNames[i], sc, win ? "<-- WIN" : "");
        OpenGLRenderer_DrawText2D(gx + 6.0f, ry + 3.0f, 1.10f,
                                  win ? Color_Create(242, 255, 255, 255) : Color_Create(194, 220, 230, 240), buf);
    }

    /* Banner de decisión ganadora */
    OpenGLRenderer_DrawRect2D(px + 10.0f, py + 120.0f, pw - 20.0f, 26.0f,
                              Color_Create(18, 44, 56, 235), Color_Create(76, 212, 232, 225));
    char decBuf[160];
    snprintf(decBuf, sizeof(decBuf), "SELECTED: %-14s  TARGET:#%-4u  SCORE:%+6.2f  OUTCOME:%s",
             NeuralV2Action_Name(v2->selectedAction), v2->selectedTargetId, v2->selectedScore,
             NeuralInteractionOutcome_Name(sel->neuralState.lastOutcome));
    OpenGLRenderer_DrawText2D(px + 18.0f, py + 128.0f, 1.15f, Color_Create(232, 252, 255, 255), decBuf);

    /* 3) Candidatos de recursos percibidos (hasta 8) */
    float midY = py + 156.0f;
    OpenGLRenderer_DrawRect2D(px + 10.0f, midY, 294.0f, 246.0f,
                              Color_Create(10, 18, 26, 228), Color_Create(42, 118, 138, 190));
    char rTitle[64];
    snprintf(rTitle, sizeof(rTitle), "RESOURCE CANDIDATES (%u/8) CONSUME", v2->resourceCount);
    OpenGLRenderer_DrawText2D(px + 16.0f, midY + 6.0f, 1.10f, Color_Create(116, 226, 178, 255), rTitle);
    for (unsigned i = 0; i < v2->resourceCount && i < NEURAL_V2_MAX_RESOURCE_CANDIDATES; ++i) {
        AIObjectId rid = v2->resourceIds[i];
        float sc = v2->resourceScores[i];
        bool win = (v2->selectedAction == NEURAL_V2_ACTION_CONSUME_TARGET && v2->selectedTargetId == rid);
        const EvolutionResourceSnapshot *rs = findSnapResource(snap, rid);
        const char *sname = (rs && rs->substanceName[0]) ? rs->substanceName : "resource";
        float ry = midY + 26.0f + (float)i * 26.0f;
        float norm = 0.5f + 0.5f * tanhf(sc * 0.5f);
        OpenGLRenderer_DrawRect2D(px + 16.0f, ry, 282.0f, 22.0f,
                                  win ? Color_Create(24, 78, 66, 235) : Color_Create(14, 24, 32, 220),
                                  win ? Color_Create(104, 246, 188, 245) : Color_Create(38, 72, 86, 160));
        OpenGLRenderer_DrawRect2D(px + 17.0f, ry + 1.0f, 280.0f * norm, 20.0f,
                                  win ? Color_Create(54, 176, 132, 165) : Color_Create(36, 106, 98, 115),
                                  Color_Create(0, 0, 0, 0));
        if (rs && rs->molecular) {
            OpenGLRenderer_DrawRect2D(px + 20.0f, ry + 5.0f, 10.0f, 12.0f,
                                      colorFromRgbFloat(rs->color), Color_Create(220, 236, 244, 160));
        }
        char row[80];
        snprintf(row, sizeof(row), "#%-4u %-14.14s %+6.2f", rid, sname, sc);
        OpenGLRenderer_DrawText2D(px + 34.0f, ry + 6.0f, 1.05f,
                                  win ? Color_Create(242, 255, 248, 255) : Color_Create(198, 224, 234, 240), row);
    }
    if (v2->resourceCount == 0u) {
        OpenGLRenderer_DrawText2D(px + 20.0f, midY + 38.0f, 1.10f, Color_Create(136, 166, 178, 220),
                                  "(NO RESOURCES IN MEMORY)");
    }

    /* 4) Candidatos de criaturas percibidas (hasta 6) */
    float cx0 = px + 312.0f;
    OpenGLRenderer_DrawRect2D(cx0, midY, 294.0f, 246.0f,
                              Color_Create(10, 18, 26, 228), Color_Create(42, 118, 138, 190));
    char cTitle[64];
    snprintf(cTitle, sizeof(cTitle), "CREATURE CANDIDATES (%u/6)", v2->creatureCount);
    OpenGLRenderer_DrawText2D(cx0 + 8.0f, midY + 6.0f, 1.10f, Color_Create(246, 164, 128, 255), cTitle);
    OpenGLRenderer_DrawText2D(cx0 + 8.0f, midY + 22.0f, 0.98f, Color_Create(146, 182, 196, 230),
                              "ID   SP    ATT    FLE    FOL    MAT");
    for (unsigned i = 0; i < v2->creatureCount && i < NEURAL_V2_MAX_CREATURE_CANDIDATES; ++i) {
        AIObjectId cid = v2->creatureIds[i];
        const EvolutionEntitySnapshot *ce = findSnapEntity(snap, cid);
        const char *spTag = ce ? (ce->speciesIndex == 0u ? "BRUM" : "VELK") : "CRE ";
        bool winRow = (v2->selectedTargetId == cid &&
                       v2->selectedAction >= NEURAL_V2_ACTION_ATTACK_TARGET &&
                       v2->selectedAction <= NEURAL_V2_ACTION_MATE_TARGET);
        float ry = midY + 38.0f + (float)i * 32.0f;
        OpenGLRenderer_DrawRect2D(cx0 + 6.0f, ry, 282.0f, 28.0f,
                                  winRow ? Color_Create(76, 36, 34, 235) : Color_Create(14, 24, 32, 220),
                                  winRow ? Color_Create(250, 146, 112, 245) : Color_Create(38, 72, 86, 160));
        char row[96];
        snprintf(row, sizeof(row), "#%-3u %s %+5.1f %+5.1f %+5.1f %+5.1f",
                 cid, spTag,
                 v2->creatureScores[i][0], v2->creatureScores[i][1],
                 v2->creatureScores[i][2], v2->creatureScores[i][3]);
        OpenGLRenderer_DrawText2D(cx0 + 10.0f, ry + 8.0f, 1.02f,
                                  winRow ? Color_Create(255, 238, 228, 255) : Color_Create(198, 224, 234, 240), row);
    }
    if (v2->creatureCount == 0u) {
        OpenGLRenderer_DrawText2D(cx0 + 14.0f, midY + 48.0f, 1.10f, Color_Create(136, 166, 178, 220),
                                  "(NO CREATURES IN MEMORY)");
    }

    /* 5) Retroalimentación metabólica desagregada y conteo de decisiones v2 */
    float botY = py + 412.0f;
    OpenGLRenderer_DrawRect2D(px + 10.0f, botY, pw - 20.0f, 144.0f,
                              Color_Create(11, 20, 28, 235), Color_Create(44, 128, 148, 195));
    OpenGLRenderer_DrawText2D(px + 18.0f, botY + 8.0f, 1.15f, Color_Create(88, 226, 240, 255),
                              "DISAGGREGATED METABOLIC & INTERACTION FEEDBACK (LAST STEP)");
    const NeuralMetabolicFeedback *fb = &v2->lastFeedback;
    char fbTxt[256];
    snprintf(fbTxt, sizeof(fbTxt),
             "INGESTED_MASS: %+.3f   E_ABSORBED: %+.3f   W_ABSORBED: %+.3f\n"
             "NUTRIENTS_ABS: %+.3f   TOX_DELTA : %+.3f   DAMAGE    : %+.3f\n"
             "INTERACT_FAIL: %+.3f   NO PRIVILEGED DIET / COMPATIBILITY SENSORS",
             fb->ingestedMass, fb->energyAbsorbed, fb->waterAbsorbed,
             fb->nutrientsAbsorbed, fb->toxicityDelta, fb->damageReceived,
             fb->interactionFailure);
    OpenGLRenderer_DrawText2D(px + 18.0f, botY + 28.0f, 1.10f, Color_Create(210, 234, 242, 255), fbTxt);

    char cntTxt[220];
    snprintf(cntTxt, sizeof(cntTxt),
             "COUNTS  IDL:%llu  EXP:%llu  RST:%llu  CON:%llu\n"
             "        ATT:%llu  FLE:%llu  FOL:%llu  MAT:%llu",
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_IDLE],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_EXPLORE],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_REST],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_CONSUME_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_ATTACK_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_FLEE_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_FOLLOW_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_MATE_TARGET]);
    OpenGLRenderer_DrawText2D(px + 18.0f, botY + 92.0f, 1.08f, Color_Create(156, 206, 222, 245), cntTxt);
}

static void drawDigestiveInspector(int winW, int winH, bool showMinimap,
                                   const EvolutionSelectedSnapshot *ds) {
    const float pw = 312.0f, ph = 248.0f;
    const float px = showMinimap ? ((float)winW - 324.0f - 200.0f) : ((float)winW - 324.0f);
    const float py = (float)winH - ph - 44.0f;
    OpenGLRenderer_DrawRect2D(px, py, pw, ph, Color_Create(9, 15, 22, 234), Color_Create(88, 198, 158, 220));
    OpenGLRenderer_DrawRect2D(px, py, pw, 24.0f, Color_Create(15, 34, 36, 245), Color_Create(88, 198, 158, 220));
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 6.0f, 1.22f, Color_Create(118, 238, 186, 255),
                              "DIGESTIVE & METABOLIC [G]");
    if (!ds || !ds->valid || !ds->hasMetabolism) {
        OpenGLRenderer_DrawText2D(px + 10.0f, py + 36.0f, 1.15f, Color_Create(168, 196, 206, 230),
                                  "LEGACY MODE / NO METABOLIC STATE");
        return;
    }

    char metaHdr[160];
    snprintf(metaHdr, sizeof(metaHdr), "PROFILE: %.28s\nLAST   : %.28s",
             ds->metabolicProfileName[0] ? ds->metabolicProfileName : "none",
             ds->lastSubstanceName[0] ? ds->lastSubstanceName : "(none)");
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 30.0f, 1.12f, Color_Create(212, 236, 242, 255), metaHdr);

    drawPhysBar(px + 10.0f, py + 62.0f, pw - 20.0f, 14.0f, "FULL  ", ds->digestiveFullness, Color_Create(116, 206, 134, 235));
    drawPhysBar(px + 10.0f, py + 78.0f, pw - 20.0f, 14.0f, "NUTR  ", ds->nutrientReserve, Color_Create(236, 198, 86, 235));
    drawPhysBar(px + 10.0f, py + 94.0f, pw - 20.0f, 14.0f, "TOXIC ", ds->toxicity, Color_Create(232, 74, 136, 240));
    drawPhysBar(px + 10.0f, py + 110.0f, pw - 20.0f, 14.0f, "DISCM ", ds->digestiveDiscomfort, Color_Create(224, 134, 72, 235));

    char rates[160];
    snprintf(rates, sizeof(rates),
             "MASS %.2f/%.2f  WASTE %.2f  ING %.1f\n"
             "ABS E:%+.3f W:%+.3f DMG:%.3f",
             ds->digestiveMass, ds->digestiveCapacity, ds->wasteMass, ds->totalIngestedMass,
             ds->recentEnergyAbsorbed, ds->recentHydrationAbsorbed, ds->recentMetabolicDamage);
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 128.0f, 1.04f, Color_Create(188, 220, 230, 245), rates);

    OpenGLRenderer_DrawText2D(px + 10.0f, py + 158.0f, 1.05f, Color_Create(136, 214, 188, 255),
                              "STOMACH MOLECULES:");
    if (ds->digestiveEntryCount == 0u) {
        OpenGLRenderer_DrawText2D(px + 14.0f, py + 174.0f, 1.05f, Color_Create(140, 166, 178, 220),
                                  "(EMPTY DIGESTIVE TRACT)");
    } else {
        float maxM = 0.05f;
        for (unsigned i = 0; i < ds->digestiveEntryCount; ++i) {
            if (ds->digestiveMoleculeMasses[i] > maxM) maxM = ds->digestiveMoleculeMasses[i];
        }
        unsigned shown = ds->digestiveEntryCount < 5u ? ds->digestiveEntryCount : 5u;
        for (unsigned i = 0; i < shown; ++i) {
            float ry = py + 173.0f + (float)i * 14.0f;
            float frac = fminf(1.0f, ds->digestiveMoleculeMasses[i] / maxM);
            char mLbl[24];
            snprintf(mLbl, sizeof(mLbl), "%-11.11s", ds->digestiveMoleculeNames[i]);
            OpenGLRenderer_DrawText2D(px + 10.0f, ry + 1.0f, 0.98f, Color_Create(202, 226, 236, 245), mLbl);
            float bx = px + 98.0f, bw = 152.0f;
            OpenGLRenderer_DrawRect2D(bx, ry + 1.0f, bw, 11.0f, Color_Create(14, 24, 32, 230), Color_Create(40, 74, 88, 160));
            OpenGLRenderer_DrawRect2D(bx + 1.0f, ry + 2.0f, (bw - 2.0f) * frac, 9.0f,
                                      moleculeBarColor(ds->digestiveMoleculeNames[i]), Color_Create(0, 0, 0, 0));
            char mVal[20];
            snprintf(mVal, sizeof(mVal), "%.2f", ds->digestiveMoleculeMasses[i]);
            OpenGLRenderer_DrawText2D(bx + bw + 6.0f, ry + 1.0f, 0.98f, Color_Create(220, 238, 244, 255), mVal);
        }
    }
}

static void drawSubstanceInspector(int winH, bool showPerf,
                                   const EvolutionRenderSnapshot *snap,
                                   const EvolutionResourceSnapshot *res,
                                   UiButton *btnPrev, UiButton *btnNext) {
    const float pw = 308.0f, ph = 336.0f;
    const float px = showPerf ? 330.0f : 12.0f;
    const float py = (float)winH - ph - 44.0f;
    OpenGLRenderer_DrawRect2D(px, py, pw, ph, Color_Create(9, 15, 22, 234), Color_Create(214, 174, 78, 220));
    OpenGLRenderer_DrawRect2D(px, py, pw, 24.0f, Color_Create(28, 26, 18, 245), Color_Create(214, 174, 78, 220));
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 6.0f, 1.20f, Color_Create(248, 214, 108, 255),
                              "SUBSTANCE INSPECTOR [T]");

    *btnPrev = (UiButton){px + pw - 62.0f, py + 3.0f, 26.0f, 18.0f, "<"};
    *btnNext = (UiButton){px + pw - 32.0f, py + 3.0f, 26.0f, 18.0f, ">"};
    drawButton(btnPrev, false, false);
    drawButton(btnNext, false, false);

    if (!res) {
        OpenGLRenderer_DrawText2D(px + 10.0f, py + 38.0f, 1.15f, Color_Create(170, 196, 206, 230),
                                  "NO RESOURCE SELECTED");
        return;
    }

    if (res->molecular) {
        OpenGLRenderer_DrawRect2D(px + 10.0f, py + 32.0f, 16.0f, 16.0f,
                                  colorFromRgbFloat(res->color), Color_Create(230, 240, 244, 200));
    }
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "#%u  %.22s", res->id,
             res->substanceName[0] ? res->substanceName : "legacy_resource");
    OpenGLRenderer_DrawText2D(px + 32.0f, py + 34.0f, 1.22f, Color_Create(240, 248, 252, 255), hdr);

    char info[256];
    snprintf(info, sizeof(info),
             "ID    : %.24s\n"
             "PHASE : %-10s HARD: %.2f\n"
             "MASS  : %5.1f / %-5.1f (R:%.1f)\n"
             "REGEN : %-9s DECAY: %s",
             res->substanceId[0] ? res->substanceId : "legacy",
             phaseLabel(res->phase), res->hardness,
             res->mass, res->maxMass, res->radius,
             regenPolicyLabel(res->regenPolicy), decayPolicyLabel(res->decayPolicy));
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 54.0f, 1.06f, Color_Create(206, 228, 238, 245), info);

    float yCur = py + 114.0f;
    if (res->sourceCreatureId != 0u) {
        char carc[96];
        snprintf(carc, sizeof(carc), "CARCASS SRC:#%u  KILLER:#%u  TTL:%.0fS",
                 res->sourceCreatureId, res->killerCreatureId, res->remainingLifetime);
        OpenGLRenderer_DrawText2D(px + 10.0f, yCur, 1.02f, Color_Create(246, 152, 128, 255), carc);
        yCur += 16.0f;
    }

    OpenGLRenderer_DrawText2D(px + 10.0f, yCur, 1.06f, Color_Create(242, 210, 114, 255),
                              "MOLECULAR COMPOSITION:");
    yCur += 15.0f;
    unsigned shown = res->componentCount < 7u ? res->componentCount : 7u;
    for (unsigned i = 0; i < shown; ++i) {
        char cLbl[24];
        snprintf(cLbl, sizeof(cLbl), "%-11.11s", res->compNames[i]);
        OpenGLRenderer_DrawText2D(px + 10.0f, yCur + 1.0f, 0.98f, Color_Create(204, 228, 238, 245), cLbl);
        float bx = px + 98.0f, bw = 148.0f;
        float frac = fmaxf(0.0f, fminf(1.0f, res->compFractions[i]));
        OpenGLRenderer_DrawRect2D(bx, yCur + 1.0f, bw, 11.0f, Color_Create(14, 24, 32, 230), Color_Create(44, 78, 92, 165));
        OpenGLRenderer_DrawRect2D(bx + 1.0f, yCur + 2.0f, (bw - 2.0f) * frac, 9.0f,
                                  moleculeBarColor(res->compNames[i]), Color_Create(0, 0, 0, 0));
        char pBuf[16];
        snprintf(pBuf, sizeof(pBuf), "%4.1f%%", frac * 100.0f);
        OpenGLRenderer_DrawText2D(bx + bw + 5.0f, yCur + 1.0f, 0.98f, Color_Create(228, 240, 246, 255), pBuf);
        yCur += 15.0f;
    }

    float sigY = py + ph - 48.0f;
    OpenGLRenderer_DrawRect2D(px + 8.0f, sigY, pw - 16.0f, 40.0f,
                              Color_Create(12, 20, 28, 230), Color_Create(56, 98, 116, 175));
    char sigTxt[160];
    snprintf(sigTxt, sizeof(sigTxt),
             "SENSORY  MOIST:%.2f PLANT:%.2f ORG:%.2f\n"
             "         LIPID:%.2f SWEET:%.2f DEC:%.2f",
             res->sensorySignature.channels[SENSORY_MOISTURE],
             res->sensorySignature.channels[SENSORY_PLANT_AROMA],
             res->sensorySignature.channels[SENSORY_ORGANIC_AROMA],
             res->sensorySignature.channels[SENSORY_LIPID_AROMA],
             res->sensorySignature.channels[SENSORY_SWEETNESS],
             res->sensorySignature.channels[SENSORY_DECAY_AROMA]);
    OpenGLRenderer_DrawText2D(px + 14.0f, sigY + 7.0f, 0.96f, Color_Create(174, 214, 228, 245), sigTxt);
    (void)snap;
}

static void drawNeuralInspector(const EvolutionRenderSnapshot *snap,
                                const EvolutionSelectedSnapshot *sel, float edgeThreshold,
                                int inspectedLayer, int inspectedIndex,
                                UiButton *btnThMinus, UiButton *btnThPlus, UiButton *btnClearSel,
                                float inPos[NEURAL_RNN_INPUTS][2],
                                float hidPos[NEURAL_RNN_HIDDEN][2],
                                float outPos[NEURAL_RNN_OUTPUTS][2]) {
    const float px = 330.0f, py = 12.0f, pw = 616.0f, ph = 568.0f;
    OpenGLRenderer_DrawRect2D(px, py, pw, ph, Color_Create(8, 14, 20, 234), Color_Create(46, 152, 174, 220));
    OpenGLRenderer_DrawRect2D(px, py, pw, 28.0f, Color_Create(13, 28, 39, 245), Color_Create(46, 152, 174, 220));

    if (!sel || !sel->valid || sel->controllerType != CREATURE_CONTROLLER_NEURAL || !sel->hasNeuralBrain || !sel->genome.valid) {
        OpenGLRenderer_DrawText2D(px + 12.0f, py + 8.0f, 1.4f, Color_Create(78, 216, 232, 255),
                                  "NEURAL INSPECTOR  (UTILITY CONTROLLER / NO NEURAL BRAIN)");
        return;
    }

    if (sel->genome.architecture == NEURAL_ARCH_CANDIDATE_POLICY) {
        drawNeuralInspectorV2(snap, sel);
        return;
    }

    const NeuralGenome *g = &sel->genome;
    const NeuralBrainRuntime *rt = &sel->runtime;
    const NeuralControllerState *st = &sel->neuralState;

    char header[160];
    snprintf(header, sizeof(header), "NEURAL RNN 32-12-10  #%u  FP:%08llX  STEPS:%llu",
             sel->entityId, (unsigned long long)(g->metadata.fingerprint & 0xFFFFFFFFu),
             (unsigned long long)rt->stepCount);
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 8.0f, 1.35f, Color_Create(78, 222, 236, 255), header);

    *btnThMinus = (UiButton){px + pw - 196.0f, py + 4.0f, 34.0f, 20.0f, "W-"};
    *btnThPlus  = (UiButton){px + pw - 158.0f, py + 4.0f, 34.0f, 20.0f, "W+"};
    *btnClearSel = (UiButton){px + pw - 58.0f, py + 4.0f, 50.0f, 20.0f, "ALL"};
    drawButton(btnThMinus, false, false);
    drawButton(btnThPlus, false, false);
    char thBuf[24];
    snprintf(thBuf, sizeof(thBuf), "T:%.2f", edgeThreshold);
    OpenGLRenderer_DrawText2D(px + pw - 120.0f, py + 8.0f, 1.25f, Color_Create(190, 224, 232, 255), thBuf);
    drawButton(btnClearSel, inspectedLayer >= 0, false);

    float xIn = px + 96.0f, xHid = px + 310.0f, xOut = px + 458.0f;
    float yTop = py + 44.0f, yBot = py + ph - 52.0f;
    for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) {
        inPos[i][0] = xIn;
        inPos[i][1] = yTop + (yBot - yTop) * (float)i / (float)(NEURAL_RNN_INPUTS - 1u);
    }
    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        hidPos[h][0] = xHid;
        hidPos[h][1] = yTop + 18.0f + (yBot - yTop - 36.0f) * (float)h / (float)(NEURAL_RNN_HIDDEN - 1u);
    }
    for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
        outPos[o][0] = xOut;
        outPos[o][1] = yTop + 26.0f + (yBot - yTop - 52.0f) * (float)o / (float)(NEURAL_RNN_OUTPUTS - 1u);
    }

    bool filterNode = (inspectedLayer >= 0 && inspectedIndex >= 0);

    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) {
            float w = g->w_xh[h * NEURAL_RNN_INPUTS + i];
            bool match = !filterNode ||
                         (inspectedLayer == 0 && (int)i == inspectedIndex) ||
                         (inspectedLayer == 1 && (int)h == inspectedIndex);
            if (!match) continue;
            float thresh = filterNode ? fminf(edgeThreshold, 0.10f) : edgeThreshold;
            if (fabsf(w) < thresh) continue;
            float lw = 1.0f + fminf(2.5f, fabsf(w) * 0.65f);
            OpenGLRenderer_DrawLine2D(inPos[i][0] + 5.0f, inPos[i][1],
                                      hidPos[h][0] - 9.0f, hidPos[h][1],
                                      lw, weightColor(w, filterNode ? 1.0f : 0.62f, false));
        }
    }

    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        for (unsigned k = 0; k < NEURAL_RNN_HIDDEN; ++k) {
            float w = g->w_hh[h * NEURAL_RNN_HIDDEN + k];
            bool match = !filterNode ||
                         (inspectedLayer == 1 && ((int)h == inspectedIndex || (int)k == inspectedIndex));
            if (!match) continue;
            float thresh = filterNode ? fminf(edgeThreshold, 0.10f) : edgeThreshold;
            if (fabsf(w) < thresh) continue;
            Color col = weightColor(w, filterNode ? 1.0f : 0.85f, true);
            float lw = 1.2f + fminf(2.4f, fabsf(w) * 0.7f);
            if (h == k) {
                OpenGLRenderer_DrawCircle2D(hidPos[h][0] + 13.0f, hidPos[h][1], 7.0f,
                                            Color_Create(0, 0, 0, 0), col);
            } else {
                float arcX = xHid + 26.0f + fabsf((float)h - (float)k) * 3.8f;
                float midY = 0.5f * (hidPos[h][1] + hidPos[k][1]);
                OpenGLRenderer_DrawLine2D(hidPos[k][0] + 8.0f, hidPos[k][1], arcX, midY, lw, col);
                OpenGLRenderer_DrawLine2D(arcX, midY, hidPos[h][0] + 8.0f, hidPos[h][1], lw, col);
            }
        }
    }

    for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
        for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
            float w = g->w_hy[o * NEURAL_RNN_HIDDEN + h];
            bool match = !filterNode ||
                         (inspectedLayer == 1 && (int)h == inspectedIndex) ||
                         (inspectedLayer == 2 && (int)o == inspectedIndex);
            if (!match) continue;
            float thresh = filterNode ? fminf(edgeThreshold, 0.10f) : edgeThreshold;
            if (fabsf(w) < thresh) continue;
            float lw = 1.0f + fminf(2.8f, fabsf(w) * 0.75f);
            OpenGLRenderer_DrawLine2D(hidPos[h][0] + 9.0f, hidPos[h][1],
                                      outPos[o][0] - 9.0f, outPos[o][1],
                                      lw, weightColor(w, filterNode ? 1.0f : 0.78f, false));
        }
    }

    for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) {
        float v = rt->inputs[i];
        bool isSel = (inspectedLayer == 0 && inspectedIndex == (int)i);
        Color border = isSel ? Color_Create(255, 240, 120, 255)
                             : (v >= 0.0f ? Color_Create(78, 212, 226, 220) : Color_Create(232, 102, 92, 220));
        OpenGLRenderer_DrawCircle2D(inPos[i][0], inPos[i][1], isSel ? 6.0f : 4.5f, activationFill(v), border);
        char lbl[32];
        snprintf(lbl, sizeof(lbl), "%-9s", NeuralSensor_Name(i));
        OpenGLRenderer_DrawText2D(px + 8.0f, inPos[i][1] - 3.5f, 1.05f,
                                  isSel ? Color_Create(255, 245, 160, 255) : Color_Create(168, 198, 210, 235), lbl);
    }

    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        float v = rt->hidden[h];
        bool isSel = (inspectedLayer == 1 && inspectedIndex == (int)h);
        Color border = isSel ? Color_Create(255, 240, 120, 255)
                             : (v >= 0.0f ? Color_Create(80, 226, 240, 240) : Color_Create(238, 102, 94, 240));
        OpenGLRenderer_DrawCircle2D(hidPos[h][0], hidPos[h][1], isSel ? 10.5f : 8.5f, activationFill(v), border);
        char hbuf[16];
        snprintf(hbuf, sizeof(hbuf), "H%u", h);
        OpenGLRenderer_DrawText2D(hidPos[h][0] - 8.0f, hidPos[h][1] - 4.0f, 1.05f,
                                  Color_Create(240, 250, 255, 255), hbuf);
    }

    float minOut = rt->outputs[0], maxOut = rt->outputs[0];
    for (unsigned o = 1; o < NEURAL_RNN_OUTPUTS; ++o) {
        if (rt->outputs[o] < minOut) minOut = rt->outputs[o];
        if (rt->outputs[o] > maxOut) maxOut = rt->outputs[o];
    }
    float span = fmaxf(0.5f, maxOut - minOut);
    for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
        float v = rt->outputs[o];
        bool winner = (st->selectedIntent == (NeuralActionIntent)o);
        bool isSel = (inspectedLayer == 2 && inspectedIndex == (int)o);
        float norm = fmaxf(0.0f, fminf(1.0f, (v - minOut) / span));
        Color border = isSel ? Color_Create(255, 240, 120, 255)
                             : (winner ? Color_Create(84, 242, 255, 255) : Color_Create(62, 130, 148, 210));
        OpenGLRenderer_DrawCircle2D(outPos[o][0], outPos[o][1], winner ? 9.5f : 7.5f,
                                    activationFill(tanhf(v * 0.5f)), border);
        float barX = outPos[o][0] + 15.0f;
        float barW = 134.0f;
        OpenGLRenderer_DrawRect2D(barX, outPos[o][1] - 9.0f, barW, 18.0f,
                                  winner ? Color_Create(22, 68, 82, 235) : Color_Create(13, 22, 30, 210),
                                  winner ? Color_Create(78, 228, 244, 240) : Color_Create(38, 70, 84, 150));
        OpenGLRenderer_DrawRect2D(barX + 1.0f, outPos[o][1] - 8.0f, (barW - 2.0f) * norm, 16.0f,
                                  winner ? Color_Create(42, 166, 186, 175) : Color_Create(36, 98, 118, 120),
                                  Color_Create(0, 0, 0, 0));
        char obuf[32];
        snprintf(obuf, sizeof(obuf), "%-10s %+.1f", NeuralActionIntent_Name((NeuralActionIntent)o), v);
        OpenGLRenderer_DrawText2D(barX + 4.0f, outPos[o][1] - 4.0f, 1.12f,
                                  winner ? Color_Create(240, 255, 255, 255) : Color_Create(190, 216, 226, 240), obuf);
    }

    float fy = py + ph - 36.0f;
    OpenGLRenderer_DrawRect2D(px + 6.0f, fy - 4.0f, pw - 12.0f, 34.0f,
                              Color_Create(12, 20, 28, 235), Color_Create(38, 88, 104, 170));
    char foot[160];
    if (inspectedLayer == 0 && inspectedIndex >= 0 && inspectedIndex < (int)NEURAL_RNN_INPUTS) {
        snprintf(foot, sizeof(foot), "IN[%02d] %s = %+.3f  (CLICK NODE TO ISOLATE, ALL TO RESET)",
                 inspectedIndex, NeuralSensor_Name((unsigned)inspectedIndex), rt->inputs[inspectedIndex]);
    } else if (inspectedLayer == 1 && inspectedIndex >= 0 && inspectedIndex < (int)NEURAL_RNN_HIDDEN) {
        snprintf(foot, sizeof(foot), "HID[%02d] ACT=%+.3f PREV=%+.3f BIAS=%+.3f SELF_W=%+.3f",
                 inspectedIndex, rt->hidden[inspectedIndex], rt->prevHidden[inspectedIndex],
                 g->b_h[inspectedIndex], g->w_hh[inspectedIndex * NEURAL_RNN_HIDDEN + inspectedIndex]);
    } else if (inspectedLayer == 2 && inspectedIndex >= 0 && inspectedIndex < (int)NEURAL_RNN_OUTPUTS) {
        snprintf(foot, sizeof(foot), "OUT[%02d] %s = %+.3f BIAS=%+.3f (SELECTED: %s)",
                 inspectedIndex, NeuralActionIntent_Name((NeuralActionIntent)inspectedIndex),
                 rt->outputs[inspectedIndex], g->b_y[inspectedIndex],
                 NeuralActionIntent_Name(st->selectedIntent));
    } else {
        snprintf(foot, sizeof(foot), "CYAN:+W  RED:-W  AMBER:RECURRENT W_HH  | CLICK ANY NODE TO INSPECT");
    }
    OpenGLRenderer_DrawText2D(px + 14.0f, fy + 6.0f, 1.25f, Color_Create(198, 228, 238, 255), foot);
}

static void drawMinimap(int winW, int winH, const EnvironmentRenderer *env,
                        const EvolutionRenderSnapshot *snap,
                        AIObjectId selectedId, Vector3 camTarget, float zoom) {
    if (!snap || snap->arenaHalfSize <= 0.0f) return;
    const float sz = 186.0f;
    const float mx = (float)winW - sz - 14.0f;
    const float my = (float)winH - sz - 48.0f;
    OpenGLRenderer_DrawRect2D(mx, my, sz, sz + 20.0f, Color_Create(8, 14, 20, 224), Color_Create(46, 142, 164, 210));
    OpenGLRenderer_DrawText2D(mx + 8.0f, my + 5.0f, 1.15f, Color_Create(92, 222, 236, 255),
                              (env && EnvironmentRenderer_IsProcedural(env)) ? "TERRAIN MINIMAP [M]" : "ARENA MINIMAP [M]");
    const float mapX = mx + 6.0f, mapY = my + 20.0f, mapS = sz - 12.0f;
    if (env && EnvironmentRenderer_IsProcedural(env)) {
        EnvironmentRenderer_DrawMinimapBackground(env, mapX, mapY, mapS);
        OpenGLRenderer_DrawRect2D(mapX, mapY, mapS, mapS, Color_Create(0, 0, 0, 0), Color_Create(44, 96, 108, 190));
    } else {
        OpenGLRenderer_DrawRect2D(mapX, mapY, mapS, mapS, Color_Create(14, 22, 24, 240), Color_Create(44, 96, 108, 190));
    }

    float invSpan = 1.0f / (2.0f * snap->arenaHalfSize);
    bool isProc = (env && EnvironmentRenderer_IsProcedural(env));
    /* Estanques y recursos moleculares destacados */
    for (unsigned i = 0; i < snap->resourceCount; ++i) {
        const EvolutionResourceSnapshot *rs = &snap->resources[i];
        if (isProc && rs->waterBodyId != 0u && rs->shoreSiteIndex > 0u) continue;
        float ux = (rs->position.x + snap->arenaHalfSize) * invSpan;
        float uz = (rs->position.z + snap->arenaHalfSize) * invSpan;
        if (rs->molecular) {
            if (rs->presentationModel == SUBSTANCE_MODEL_WATER) {
                float r = fmaxf(2.0f, rs->radius * invSpan * mapS);
                Color c = colorFromRgbFloat(rs->color);
                OpenGLRenderer_DrawCircle2D(mapX + ux * mapS, mapY + uz * mapS, r,
                                            Color_Create(c.r, c.g, c.b, 195), Color_Create(180, 224, 240, 190));
            } else if (rs->presentationModel == SUBSTANCE_MODEL_CARCASS && rs->biomassFraction > 0.01f) {
                OpenGLRenderer_DrawRect2D(mapX + ux * mapS - 1.5f, mapY + uz * mapS - 1.5f, 3.0f, 3.0f,
                                          Color_Create(226, 110, 96, 230), Color_Create(0, 0, 0, 0));
            }
            continue;
        }
        if (rs->category != ECO_WATER) continue;
        float r = fmaxf(2.0f, rs->radius * invSpan * mapS);
        OpenGLRenderer_DrawCircle2D(mapX + ux * mapS, mapY + uz * mapS, r,
                                    Color_Create(42, 128, 178, 190), Color_Create(82, 194, 230, 210));
    }
    /* Obstáculos */
    for (unsigned i = 0; i < snap->obstacleCount; ++i) {
        float ux = (snap->obstacles[i].position.x + snap->arenaHalfSize) * invSpan;
        float uz = (snap->obstacles[i].position.z + snap->arenaHalfSize) * invSpan;
        float r = fmaxf(1.8f, snap->obstacles[i].radius * invSpan * mapS);
        OpenGLRenderer_DrawCircle2D(mapX + ux * mapS, mapY + uz * mapS, r,
                                    Color_Create(76, 92, 102, 210), Color_Create(0, 0, 0, 0));
    }
    /* Criaturas */
    float selX = -1.0f, selY = -1.0f;
    for (unsigned i = 0; i < snap->entityCount; ++i) {
        const EvolutionEntitySnapshot *e = &snap->entities[i];
        float ux = fmaxf(0.0f, fminf(1.0f, (e->position.x + snap->arenaHalfSize) * invSpan));
        float uz = fmaxf(0.0f, fminf(1.0f, (e->position.z + snap->arenaHalfSize) * invSpan));
        float px = mapX + ux * mapS, py = mapY + uz * mapS;
        Color c = e->speciesIndex == 0u ? Color_Create(110, 224, 152, 235) : Color_Create(242, 118, 92, 235);
        OpenGLRenderer_DrawRect2D(px - 1.2f, py - 1.2f, 2.4f, 2.4f, c, Color_Create(0, 0, 0, 0));
        if (e->id == selectedId) { selX = px; selY = py; }
    }
    if (selX >= 0.0f) {
        OpenGLRenderer_DrawCircle2D(selX, selY, 4.5f, Color_Create(0, 0, 0, 0), Color_Create(84, 242, 255, 255));
    }
    /* Rectángulo de cámara aproximado */
    float aspect = (float)winW / (float)winH;
    float vw = fminf(mapS, (zoom * aspect) * invSpan * mapS);
    float vh = fminf(mapS, zoom * invSpan * mapS);
    float cx = mapX + ((camTarget.x + snap->arenaHalfSize) * invSpan) * mapS - vw * 0.5f;
    float cz = mapY + ((camTarget.z + snap->arenaHalfSize) * invSpan) * mapS - vh * 0.5f;
    OpenGLRenderer_DrawRect2D(fmaxf(mapX, cx), fmaxf(mapY, cz), fminf(vw, mapX + mapS - fmaxf(mapX, cx)),
                              fminf(vh, mapY + mapS - fmaxf(mapY, cz)),
                              Color_Create(0, 0, 0, 0), Color_Create(220, 246, 255, 170));
}

static void printHelp(void) {
    puts("Uso: ./demos/demo_evolution_arena [OPCIONES]\n"
         "  --preset=small|medium|large|stress  --workers=N\n"
         "  --metabolism=molecular|legacy  --metabolism-dir=DIR  --mixed-substances  --no-mixed-substances\n"
         "  --seed=N  --arena-size=N  --brumacernos=N (--herbivores=N)  --velkares=N (--carnivores=N)\n"
         "  --plants=N  --ponds=N  --obstacles=N  --capacity=N  --speed=1|2|4|16|64|max\n"
         "  --quality=low|medium|high  --width=W  --height=H  --show-perf  --minimap  --realtime\n"
         "  --controller=neural|utility  --founders=random|seeded|imported\n"
         "  --brain=PATH  --brumacerno-brain=PATH  --velkar-brain=PATH\n"
         "  --mutation-rate=N  --mutation-sigma=N  --replace-rate=N  --crossover=neuron|uniform|mutation-only\n"
         "  --no-physical-mutation  --scenario=reference|A|B|C|D|E  --recipes-dir=DIR\n"
         "  --show-neural  --show-digestive  --show-substance  --inspect-resource=ID  --inspect-substance=SUB_ID\n"
         "  --import-brain=PATH  --export-brain=PATH  --swap-brains=A:B\n"
         "  --world=PATH  --world-seed=N  --allow-extra-ponds\n"
         "  --terrain-debug=natural|elevation|slope|hydrology|depth|biome|nav|collision|albedo|normal|roughness|weights|shadow  --wireframe\n"
         "  --show-shores  --show-rock-collision  --show-terrain-grid  --cam-x=X  --cam-z=Z  --angle=RAD\n"
         "  --camera-position=X,Y,Z --camera-target=X,Y,Z --perspective=DEG --water-time=SECONDS\n"
         "  --terrain-lod0 --wireframe --msaa=0|2|4|8 --check-gl --orbit-rate=RAD_PER_SEC --shadows=off|low|medium|high --no-contacts --no-decoration --sun=AZIMUTH,ELEVATION --periodic-materials --capture-shadow=MAP.pgm\n"
         "  --frames=N  --capture=IMG.ppm  --step-ticks=N  --profile=METRICS.json  --profile-warmup=N  --view=side|front|three-quarter|overview\n"
         "  --focus=ID  --capture-species=1|2  --capture-state=overview|adult|juvenile|newborn|walking|turning|drinking|feeding|hunting|attacking|fleeing|resting|mating\n"
         "  --zoom=N  --no-hud  --initial-age=S  --inspect=body|head|feet|neck  --validate-mouth=N  --validate-skinning  --cpu-skinning  --focus-lod=near|mid|far  --lod-cycle");
}

int main(int argc, char **argv) {
    EvolutionArenaConfig config = EvolutionArenaConfig_Default();
    const char *recipesDirectory = "recipes";
    unsigned frames = 0, validateMouthFrames = 0;
    const char *capture = NULL, *profile = NULL;
    const char *cliImportBrain = NULL, *cliExportBrain = NULL;
    const char *cliInspectSubstance = NULL;
    AIObjectId cliInspectResourceId = 0;
    AIObjectId cliSwapA = 0, cliSwapB = 0;
    double initialSpeed = 1.0, initialZoom = 26.0, initialAge = -1.0;
    bool initialMaxSpeed = false, zoomExplicit = false, qualityExplicit = false;
    bool showHud = true, showNeural = false, showDigestive = false, showSubstance = false;
    bool showPerf = false, showMinimap = false, minimapExplicit = false;
    bool forceRealtime = false, forceStepLock = false;
    unsigned exactStepTicks = 0, profileWarmup = 0;
    bool captured = false, validateSkin = false, forceCpu = false;
    int winW = ARENA_DEFAULT_W, winH = ARENA_DEFAULT_H;
    QualityPreset quality = QUALITY_HIGH;
    SceneShadowQuality shadows = SCENE_SHADOW_AUTO;
    bool contacts = true, decoration = true;
    bool periodicMaterials = false;
    float sunAzimuth = 137.5f, sunElevation = 47.0f;
    ViewMode view = VIEW_THREE_QUARTER;
    CaptureState captureState = CAPTURE_OVERVIEW;
    AIObjectId focusId = 0;
    unsigned captureSpecies = 0;
    bool hasFocus = false;
    const char *inspect = NULL, *captureShadow = NULL;
    VisualTier captureTier = VISUAL_TIER_NEAR;
    bool lodCycle = false;
    TerrainDebugMode terrainDebugMode = TERRAIN_DEBUG_NATURAL;
    bool terrainWireframe = false, terrainLod0 = false, checkGl = false;
    bool hasCameraPosition = false, hasCameraTarget = false;
    Vector3 fixedCameraPosition = {0}, fixedCameraTarget = {0};
    float perspectiveFov = 0.0f, fixedWaterTime = -1.0f;
    int msaa = 0;
    bool showShoresOverlay = false, showRockCollisionOverlay = false, showTerrainGridOverlay = false;
    bool hasCustomCam = false, hasCustomAngle = false;
    float customCamX = 0.0f, customCamZ = 0.0f;
    double customAngle = 0.65, orbitRate = 0.0;
    static const char *kWorldPresets[] = {
        "worlds/flat.world.json",
        "worlds/river_valley_v2.world.json",
        "worlds/mountain_lakes_v2.world.json",
        "worlds/rocky_highlands_v2.world.json",
        "worlds/wetlands_v2.world.json"
    };
    unsigned worldPresetIdx = 0u;

    /* Primero aplicar presets/escenarios base para que los flags explícitos puedan sobrescribirlos. */
    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--scenario=", 11)) {
            if (!EvolutionArenaConfig_ApplyLegacyScenario(&config, argv[i] + 11)) return 2;
        } else if (!strncmp(argv[i], "--preset=", 9)) {
            const char *pname = argv[i] + 9;
            if (!EvolutionArenaConfig_ApplyPreset(&config, pname)) return 2;
            if (!strcmp(pname, "medium")) { initialZoom = 56.0; quality = QUALITY_MEDIUM; }
            else if (!strcmp(pname, "large")) { initialZoom = 115.0; quality = QUALITY_MEDIUM; showMinimap = true; }
            else if (!strcmp(pname, "stress")) { initialZoom = 155.0; quality = QUALITY_LOW; showMinimap = true; }
        }
    }

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--help")) { printHelp(); return 0; }
        else if (!strncmp(a, "--scenario=", 11) || !strncmp(a, "--preset=", 9)) continue;
        else if (!strncmp(a, "--metabolism=", 13)) {
            if (!strcmp(a + 13, "molecular")) {
                config.metabolismMode = ECOSYSTEM_METABOLISM_MOLECULAR;
                config.includeMixedSubstances = true;
            } else if (!strcmp(a + 13, "legacy")) {
                config.metabolismMode = ECOSYSTEM_METABOLISM_LEGACY;
                config.includeMixedSubstances = false;
            } else return 2;
        }
        else if (!strncmp(a, "--metabolism-dir=", 17)) config.metabolismDir = a + 17;
        else if (!strcmp(a, "--mixed-substances")) config.includeMixedSubstances = true;
        else if (!strcmp(a, "--no-mixed-substances")) config.includeMixedSubstances = false;
        else if (!strcmp(a, "--show-digestive")) showDigestive = true;
        else if (!strcmp(a, "--show-substance")) showSubstance = true;
        else if (!strncmp(a, "--inspect-resource=", 19)) {
            cliInspectResourceId = (AIObjectId)strtoul(a + 19, NULL, 10);
            showSubstance = true;
        }
        else if (!strncmp(a, "--inspect-substance=", 20)) {
            cliInspectSubstance = a + 20;
            showSubstance = true;
        }
        else if (!strncmp(a, "--workers=", 10)) config.workers = (unsigned)strtoul(a + 10, NULL, 10);
        else if (!strncmp(a, "--seed=", 7)) config.seed = (uint32_t)strtoul(a + 7, NULL, 10);
        else if (!strncmp(a, "--world=", 8)) {
            snprintf(config.worldRecipePath, sizeof(config.worldRecipePath), "%s", a + 8);
        }
        else if (!strncmp(a, "--world-seed=", 13)) {
            config.worldSeed = (uint32_t)strtoul(a + 13, NULL, 10);
            config.hasWorldSeedOverride = true;
        }
        else if (!strcmp(a, "--allow-extra-ponds")) config.allowExtraPonds = true;
        else if (!strncmp(a, "--terrain-debug=", 16)) {
            if (!parseTerrainDebugMode(a + 16, &terrainDebugMode)) return 2;
        }
        else if (!strcmp(a, "--terrain-lod0")) terrainLod0 = true;
        else if (!strcmp(a, "--check-gl")) checkGl = true;
        else if (!strncmp(a, "--camera-position=", 18)) {
            if (!parseCameraPoint(a + 18, &fixedCameraPosition)) return 2;
            hasCameraPosition = true;
        } else if (!strncmp(a, "--camera-target=", 16)) {
            if (!parseCameraPoint(a + 16, &fixedCameraTarget)) return 2;
            hasCameraTarget = true;
        } else if (!strncmp(a, "--perspective=", 14)) {
            perspectiveFov = strtof(a + 14, NULL);
            if (!(perspectiveFov >= 15.0f && perspectiveFov <= 100.0f)) return 2;
        } else if (!strncmp(a, "--water-time=", 13)) {
            fixedWaterTime = strtof(a + 13, NULL);
            if (!isfinite(fixedWaterTime) || fixedWaterTime < 0.0f) return 2;
        } else if (!strncmp(a, "--msaa=", 7)) {
            msaa = (int)strtol(a + 7, NULL, 10);
            if (msaa != 0 && msaa != 2 && msaa != 4 && msaa != 8) return 2;
        }
        else if (!strcmp(a, "--wireframe")) terrainWireframe = true;
        else if (!strcmp(a, "--show-shores")) showShoresOverlay = true;
        else if (!strcmp(a, "--show-rock-collision")) showRockCollisionOverlay = true;
        else if (!strcmp(a, "--show-terrain-grid")) showTerrainGridOverlay = true;
        else if (!strncmp(a, "--cam-x=", 8)) { customCamX = strtof(a + 8, NULL); hasCustomCam = true; }
        else if (!strncmp(a, "--cam-z=", 8)) { customCamZ = strtof(a + 8, NULL); hasCustomCam = true; }
        else if (!strncmp(a, "--orbit-rate=", 13)) {
            orbitRate = strtod(a + 13, NULL);
            if (!isfinite(orbitRate)) return 2;
        }
        else if (!strncmp(a, "--angle=", 8)) { customAngle = strtod(a + 8, NULL); hasCustomAngle = true; }
        else if (!strncmp(a, "--arena-size=", 13)) { config.arenaSize = strtof(a + 13, NULL); config.hasExplicitArenaSize = true; }
        else if (!strncmp(a, "--brumacernos=", 14)) config.brumacernos = (unsigned)strtoul(a + 14, NULL, 10);
        else if (!strncmp(a, "--herbivores=", 13)) config.brumacernos = (unsigned)strtoul(a + 13, NULL, 10);
        else if (!strncmp(a, "--velkares=", 11)) config.velkares = (unsigned)strtoul(a + 11, NULL, 10);
        else if (!strncmp(a, "--carnivores=", 13)) config.velkares = (unsigned)strtoul(a + 13, NULL, 10);
        else if (!strncmp(a, "--plants=", 9)) config.plants = (unsigned)strtoul(a + 9, NULL, 10);
        else if (!strncmp(a, "--ponds=", 8)) config.ponds = (unsigned)strtoul(a + 8, NULL, 10);
        else if (!strncmp(a, "--obstacles=", 12)) config.obstacles = (unsigned)strtoul(a + 12, NULL, 10);
        else if (!strncmp(a, "--capacity=", 11)) config.capacity = (unsigned)strtoul(a + 11, NULL, 10);
        else if (!strncmp(a, "--width=", 8)) winW = (int)strtol(a + 8, NULL, 10);
        else if (!strncmp(a, "--height=", 9)) winH = (int)strtol(a + 9, NULL, 10);
        else if (!strncmp(a, "--quality=", 10)) {
            const char *v = a + 10;
            if (!strcmp(v, "low")) quality = QUALITY_LOW;
            else if (!strcmp(v, "medium") || !strcmp(v, "med")) quality = QUALITY_MEDIUM;
            else if (!strcmp(v, "high")) quality = QUALITY_HIGH;
            else return 2;
            qualityExplicit = true;
        }
        else if (!strcmp(a, "--show-perf")) showPerf = true;
        else if (!strcmp(a, "--minimap")) { showMinimap = true; minimapExplicit = true; }
        else if (!strcmp(a, "--no-minimap")) { showMinimap = false; minimapExplicit = true; }
        else if (!strcmp(a, "--realtime") || !strcmp(a, "--async")) forceRealtime = true;
        else if (!strcmp(a, "--no-contacts")) contacts = false;
        else if (!strcmp(a, "--no-decoration")) decoration = false;
        else if (!strcmp(a, "--periodic-materials")) periodicMaterials = true;
        else if (!strncmp(a, "--sun=", 6)) {
            char extra;
            if (sscanf(a + 6, "%f,%f%c", &sunAzimuth, &sunElevation, &extra) != 2 ||
                !isfinite(sunAzimuth) || !isfinite(sunElevation) || sunElevation < 15 || sunElevation > 85) return 2;
        }
        else if (!strncmp(a, "--shadows=", 10)) {
            if (!SceneLighting_ParseShadows(a + 10, &shadows)) return 2;
        }
        else if (!strcmp(a, "--step-lock")) forceStepLock = true;
        else if (!strncmp(a, "--step-ticks=", 13)) {
            char *end;
            unsigned long count = strtoul(a + 13, &end, 10);
            if (*end || count < 1 || count > 512) return 2;
            exactStepTicks = (unsigned)count;
            forceStepLock = true;
        }
        else if (!strncmp(a, "--controller=", 13)) {
            if (!strcmp(a + 13, "neural")) config.controller = CREATURE_CONTROLLER_NEURAL;
            else if (!strcmp(a + 13, "utility")) config.controller = CREATURE_CONTROLLER_UTILITY;
            else return 2;
        } else if (!strncmp(a, "--founders=", 11)) {
            if (!EvolutionFounderMode_FromName(a + 11, &config.founderMode)) return 2;
        } else if (!strncmp(a, "--brain=", 8)) config.founderBrainPath = a + 8;
        else if (!strncmp(a, "--brumacerno-brain=", 19)) config.brumacernoBrainPath = a + 19;
        else if (!strncmp(a, "--velkar-brain=", 15)) config.velkarBrainPath = a + 15;
        else if (!strncmp(a, "--mutation-rate=", 16)) config.mutation.mutationRate = strtof(a + 16, NULL);
        else if (!strncmp(a, "--mutation-sigma=", 17)) config.mutation.mutationSigma = strtof(a + 17, NULL);
        else if (!strncmp(a, "--replace-rate=", 15)) config.mutation.replaceRate = strtof(a + 15, NULL);
        else if (!strncmp(a, "--crossover=", 12)) {
            const char *v = a + 12;
            if (!strcmp(v, "neuron") || !strcmp(v, "neuron_wise")) config.mutation.crossoverMode = NEURAL_CROSSOVER_NEURON_WISE;
            else if (!strcmp(v, "uniform")) config.mutation.crossoverMode = NEURAL_CROSSOVER_UNIFORM;
            else if (!strcmp(v, "mutation-only") || !strcmp(v, "mutation_only")) config.mutation.crossoverMode = NEURAL_CROSSOVER_MUTATION_ONLY;
            else return 2;
        }
        else if (!strcmp(a, "--no-physical-mutation")) config.disablePhysicalMutation = true;
        else if (!strcmp(a, "--show-neural")) showNeural = true;
        else if (!strncmp(a, "--import-brain=", 15)) cliImportBrain = a + 15;
        else if (!strncmp(a, "--export-brain=", 15)) cliExportBrain = a + 15;
        else if (!strncmp(a, "--swap-brains=", 14)) {
            unsigned long sA = 0, sB = 0;
            if (sscanf(a + 14, "%lu:%lu", &sA, &sB) != 2 || !sA || !sB) return 2;
            cliSwapA = (AIObjectId)sA; cliSwapB = (AIObjectId)sB;
        }
        else if (!strncmp(a, "--frames=", 9)) frames = (unsigned)strtoul(a + 9, NULL, 10);
        else if (!strncmp(a, "--speed=", 8)) {
            if (!strcmp(a + 8, "max") || !strcmp(a + 8, "MAX")) { initialMaxSpeed = true; initialSpeed = 64.0; }
            else { initialMaxSpeed = false; initialSpeed = strtod(a + 8, NULL); }
        }
        else if (!strcmp(a, "--validate-skinning")) validateSkin = true;
        else if (!strcmp(a, "--lod-cycle")) lodCycle = true;
        else if (!strncmp(a, "--focus-lod=", 12)) {
            const char *v = a + 12;
            if (!strcmp(v, "near")) captureTier = VISUAL_TIER_NEAR;
            else if (!strcmp(v, "mid")) captureTier = VISUAL_TIER_MID;
            else if (!strcmp(v, "far")) captureTier = VISUAL_TIER_FAR;
            else return 2;
        }
        else if (!strcmp(a, "--cpu-skinning")) forceCpu = true;
        else if (!strncmp(a, "--recipes-dir=", 14)) recipesDirectory = a + 14;
        else if (!strncmp(a, "--inspect=", 10)) {
            inspect = a + 10;
            if (strcmp(inspect, "body") && strcmp(inspect, "head") && strcmp(inspect, "feet") && strcmp(inspect, "neck")) return 2;
        }
        else if (!strcmp(a, "--no-hud")) showHud = false;
        else if (!strncmp(a, "--initial-age=", 14)) initialAge = strtod(a + 14, NULL);
        else if (!strncmp(a, "--zoom=", 7)) { initialZoom = strtod(a + 7, NULL); zoomExplicit = true; }
        else if (!strncmp(a, "--capture=", 10)) capture = a + 10;
        else if (!strncmp(a, "--capture-shadow=", 17)) captureShadow = a + 17;
        else if (!strncmp(a, "--profile=", 10)) profile = a + 10;
        else if (!strncmp(a, "--profile-warmup=", 17)) {
            char *end;
            unsigned long count = strtoul(a + 17, &end, 10);
            if (*end || count > 10000) return 2;
            profileWarmup = (unsigned)count;
        }
        else if (!strncmp(a, "--view=", 7)) { if (!parseView(a + 7, &view)) return 2; }
        else if (!strncmp(a, "--focus=", 8)) { focusId = (AIObjectId)strtoul(a + 8, NULL, 10); hasFocus = true; }
        else if (!strncmp(a, "--capture-species=", 18)) captureSpecies = (unsigned)strtoul(a + 18, NULL, 10);
        else if (!strncmp(a, "--capture-state=", 16)) { if (!parseCaptureState(a + 16, &captureState)) return 2; }
        else if (!strncmp(a, "--validate-mouth=", 17)) validateMouthFrames = (unsigned)strtoul(a + 17, NULL, 10);
        else return 2;
    }

    if (config.worldRecipePath[0] != '\0') {
        WorldRecipe previewRec;
        char werr[160] = {0};
        if (WorldRecipe_LoadFile(config.worldRecipePath, &previewRec, werr, sizeof(werr))) {
            if (!config.hasExplicitArenaSize) config.arenaSize = previewRec.size;
            if (!previewRec.flat && !qualityExplicit && quality == QUALITY_HIGH) quality = QUALITY_MEDIUM;
            if (!minimapExplicit && !previewRec.flat && previewRec.size >= 120.0f) showMinimap = true;
        }
    }
    double maxZoom = fmax(110.0, (double)config.arenaSize * 1.65);
    if (!zoomExplicit && config.arenaSize > 80.0f && initialZoom < 30.0) {
        initialZoom = fmin(maxZoom, (double)config.arenaSize * 0.58);
    }
    if (!qualityExplicit && (worldPresetIdx != 0u || (config.brumacernos + config.velkares) >= 200u)) {
        quality = QUALITY_MEDIUM;
    }
    if (!minimapExplicit && !showHud) showMinimap = false;

    if (!isfinite(initialAge) || initialAge > 10000 || frames > 1000000 ||
        winW < 320 || winW > 7680 || winH < 240 || winH > 4320 ||
        !isfinite(initialZoom) || initialZoom < 0.4 || initialZoom > maxZoom ||
        (!initialMaxSpeed && initialSpeed != 1 && initialSpeed != 2 && initialSpeed != 4 && initialSpeed != 16 && initialSpeed != 64))
        return 2;

    char errBuf[256] = {0};
    if (!CreatureRecipeRegistry_LoadDirectory(recipesDirectory, errBuf, sizeof(errBuf))) {
        fprintf(stderr, "%s\n", errBuf);
        return 1;
    }

    /* En modo captura/regresión con --frames=N bloqueamos el paso por fotograma para determinismo;
     * en modo interactivo o --realtime/--profile puro, el hilo de simulación corre asíncrono. */
    bool stepLocked = forceStepLock ||
                      (!forceRealtime && frames > 0 &&
                       (capture != NULL || validateMouthFrames > 0 || validateSkin ||
                        inspect != NULL || captureState != CAPTURE_OVERVIEW));

    bool pausedForWarmup = !stepLocked && profile && profileWarmup > 0;
    EvolutionSimulationRunner *runner = EvolutionSimulationRunner_Create(&config, stepLocked || pausedForWarmup,
                                                                          errBuf, sizeof(errBuf));
    if (!runner) {
        fprintf(stderr, "Error al iniciar EvolutionSimulationRunner: %s\n", errBuf);
        return 1;
    }

    if (initialAge >= 0.0) {
        uint64_t cid = EvolutionSimulationRunner_PostSetInitialAge(runner, initialAge);
        EvolutionCommandAck ack;
        EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack);
    }
    {
        uint64_t cid = EvolutionSimulationRunner_PostSetSpeed(runner, initialSpeed, initialMaxSpeed);
        EvolutionCommandAck ack;
        EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack);
    }

    /* Determinar entidad inicial desde el snapshot 0 (sin tocar nunca EcosystemWorld). */
    char statusMsg[192] = "READY";
    bool statusError = false;
    AIObjectId selectedEntityId = hasFocus ? focusId : 0u;
    unsigned initialPopulation = 0u;
    {
        const EvolutionRenderSnapshot *initSnap = EvolutionSimulationRunner_AcquireSnapshot(runner);
        if (initSnap) {
            initialPopulation = initSnap->entityCount;
            if (!selectedEntityId) {
                for (unsigned i = 0; i < initSnap->entityCount; ++i) {
                    if (!captureSpecies || initSnap->entities[i].speciesId == captureSpecies) {
                        selectedEntityId = initSnap->entities[i].id;
                        break;
                    }
                }
            }
            EvolutionSimulationRunner_ReleaseSnapshot(runner, initSnap);
        }
    }

    /* Acciones CLI reproducibles de gestión de cerebros (a través de la cola segura de comandos). */
    if (cliImportBrain && selectedEntityId) {
        EvolutionCommandAck ack = {0};
        uint64_t cid = EvolutionSimulationRunner_PostImportBrain(runner, selectedEntityId, cliImportBrain);
        if (EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK) {
            snprintf(statusMsg, sizeof(statusMsg), "%s", ack.message);
        } else {
            snprintf(statusMsg, sizeof(statusMsg), "IMPORT ERROR: %.160s", ack.message);
            statusError = true;
        }
    }
    if (cliSwapA && cliSwapB) {
        EvolutionCommandAck ack = {0};
        uint64_t cid = EvolutionSimulationRunner_PostSwapBrains(runner, cliSwapA, cliSwapB);
        if (EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK) {
            snprintf(statusMsg, sizeof(statusMsg), "%s", ack.message);
        } else {
            snprintf(statusMsg, sizeof(statusMsg), "SWAP FAILED #%u <-> #%u", cliSwapA, cliSwapB);
            statusError = true;
        }
    }
    if (cliExportBrain && selectedEntityId) {
        ensureParentDir(cliExportBrain);
        EvolutionCommandAck ack = {0};
        uint64_t cid = EvolutionSimulationRunner_PostExportBrain(runner, selectedEntityId, cliExportBrain, true);
        if (EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK) {
            snprintf(statusMsg, sizeof(statusMsg), "%s", ack.message);
        } else {
            snprintf(statusMsg, sizeof(statusMsg), "EXPORT ERROR: %.160s", ack.message);
            statusError = true;
        }
    }
    if (selectedEntityId) {
        EvolutionCommandAck ack = {0};
        uint64_t cid = EvolutionSimulationRunner_PostInspectTarget(runner, selectedEntityId);
        EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack);
    }

    OpenGLDemoWindow *window = OpenGLDemoWindow_CreateScene("Monster Engine - Evolution Arena", winW, winH, msaa);
    if (!window) { EvolutionSimulationRunner_Destroy(runner); return 1; }

    EvolutionProfiler visualProfiler;
    EvolutionProfiler_Init(&visualProfiler, true);

    EcosystemVisual visual = {0};
    unsigned initVisualCap = config.capacity > 64u ? config.capacity : 64u;
    if (!EcosystemVisual_InitForSnapshots(&visual, initVisualCap)) {
        fprintf(stderr, "[ADVERTENCIA] No se pudo inicializar EcosystemVisual.\n");
        OpenGLDemoWindow_Free(window); EvolutionSimulationRunner_Destroy(runner); return 1;
    }
    applyQualityBudget(&visual, quality);
    EcosystemVisual_SetProfiler(&visual, &visualProfiler);

    ICamera camera = {.position = {28, 28, 32}, .target = {0, 0, 0}, .up = {0, 1, 0},
                      .fov = 50, .aspectRatio = (float)winW / (float)winH,
                      .nearPlane = 0.1f, .farPlane = fmaxf(450.0f, config.arenaSize * 4.5f)};
    Renderer3D renderer = OpenGLRenderer_Create(&camera);

    EnvironmentRenderer envRenderer = {0};
    if (!EnvironmentRenderer_Init(&envRenderer)) {
        fprintf(stderr, "[ADVERTENCIA] No se pudo inicializar EnvironmentRenderer.\n");
        EcosystemVisual_Shutdown(&visual); OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window); EvolutionSimulationRunner_Destroy(runner); return 1;
    }
    EnvironmentRenderer_SetShadows(&envRenderer, shadows);
    EnvironmentRenderer_SetDetails(&envRenderer, contacts, decoration);
    EnvironmentRenderer_SetPeriodicMaterials(&envRenderer, periodicMaterials);
    EnvironmentRenderer_SetSun(&envRenderer, sunAzimuth, sunElevation);
    EnvironmentRenderer_SetDebugMode(&envRenderer, terrainDebugMode);
    EnvironmentRenderer_ForceTerrainLod0(&envRenderer, terrainLod0);
    EnvironmentRenderer_SetProfiling(&envRenderer, profile != NULL && profileWarmup == 0);
    EnvironmentRenderer_SetQuality(&envRenderer,
                                   quality == QUALITY_LOW ? 0.65f : (quality == QUALITY_MEDIUM ? 1.0f : 1.35f),
                                   (int)quality, terrainWireframe);
    EnvironmentRenderer_SetOverlays(&envRenderer, showShoresOverlay, showRockCollisionOverlay, showTerrainGridOverlay);

    Mesh meshes[5];
    for (unsigned i = 0; i < 5; ++i) meshes[i] = Mesh_Create();
    Color decorColors[] = {
        {34, 44, 42, 255},   /* Suelo oscuro de laboratorio */
        {42, 128, 168, 255}, /* Estanque agua */
        {64, 152, 84, 255},  /* Vegetación */
        {134, 86, 68, 255},  /* Cadáver/carne */
        {66, 78, 86, 255}    /* Obstáculo rocoso */
    };
    bool ok = true;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[0], (Transform3D){.position = {0, -0.0001f, 0}, .rotation = {0, 0, 0}, .scale = {1, 0.0001f, 1}}, 16, 6, decorColors[0]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[1], (Transform3D){.position = {0, 0, 0}, .rotation = {0, 0, 0}, .scale = {1, 0.03f, 1}}, 16, 6, decorColors[1]) && ok;
    ok = PrimitiveMesh_GeneratePlantTuft(&meshes[2], decorColors[2]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[3], (Transform3D){.position = {0, 0.22f, 0}, .rotation = {0, 0, 0}, .scale = {0.30f, 0.22f, 0.48f}}, 12, 6, decorColors[3]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[4], (Transform3D){.position = {0, 0.65f, 0}, .rotation = {0, 0, 0}, .scale = {1.0f, 0.95f, 1.0f}}, 14, 8, decorColors[4]) && ok;
    if (!ok) {
        for (unsigned i = 0; i < 5; ++i) Mesh_Free(&meshes[i]);
        EcosystemVisual_Shutdown(&visual); OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window); EvolutionSimulationRunner_Destroy(runner); return 1;
    }

    double *times = frames ? calloc(frames, sizeof(double)) : NULL;
    double *simulationTimes = frames ? calloc(frames, sizeof(double)) : NULL;
    double *profileTimes = profile && frames ? calloc(2u * frames, sizeof(double)) : NULL;
    double *gpuTimes = profileTimes;
    double *renderTimes = profileTimes ? profileTimes + frames : NULL;
    double *mouthWindow = validateMouthFrames ? calloc(validateMouthFrames, sizeof(double)) : NULL;
    if ((frames && (!times || !simulationTimes || (profile && !profileTimes))) ||
        (validateMouthFrames && !mouthWindow)) {
        free(times); free(simulationTimes); free(mouthWindow); free(profileTimes);
        for (unsigned i = 0; i < 5; ++i) Mesh_Free(&meshes[i]);
        EcosystemVisual_Shutdown(&visual); OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window); EvolutionSimulationRunner_Destroy(runner); return 1;
    }

    /* Buffers dinámicos escalables (sin límite fijo de 256). */
    size_t dynCap = initVisualCap > 256u ? initVisualCap : 256u;
    EcosystemVisualDrawPacket *packets = calloc(dynCap, sizeof(*packets));
    EcosystemVisualDrawPacket *displayedPackets = calloc(dynCap, sizeof(*displayedPackets));
    Mesh *cpuBodies = calloc(dynCap, sizeof(*cpuBodies));
    AIObjectId *validatedSkin = calloc(dynCap, sizeof(*validatedSkin));
    uint64_t *validatedSkinMesh = calloc(dynCap, sizeof(*validatedSkinMesh));
    if (!packets || !displayedPackets || !cpuBodies || !validatedSkin || !validatedSkinMesh) {
        free(packets); free(displayedPackets); free(cpuBodies); free(validatedSkin); free(validatedSkinMesh);
        free(times); free(simulationTimes); free(mouthWindow); free(profileTimes);
        for (unsigned i = 0; i < 5; ++i) Mesh_Free(&meshes[i]);
        EcosystemVisual_Shutdown(&visual); OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window); EvolutionSimulationRunner_Destroy(runner); return 1;
    }
    size_t displayedCount = 0;

    /* Caché estática de cuadrícula + lotes de líneas e instancias GPU. */
    LineBatch staticGrid = {0}, dynamicLines = {0};
    float cachedGridHalfSize = -1.0f;
    InstanceBatch envInstances = {0}, bodyInstances = {0}, headInstances = {0};
    MeshGroup bodyGroups[64], headGroups[64];

    double last = OpenGLDemoWindow_Time(), angle = hasCustomAngle ? customAngle : 0.65, zoom = initialZoom;
    double gpuSum = 0.0, displayedFps = ARENA_MAX_FPS;
    unsigned gpuSamples = 0, gpuTimingGeneration = 0;
    OpenGLMeshPerformanceStats finalGpu = {0};
    float footErrorMax = 0.0f, stanceFootErrorMax = 0.0f, skinPositionError = 0.0f, skinNormalError = 0.0f;
    unsigned drawnMin = UINT32_MAX, drawnMax = 0;
    double drawnSum = 0.0, adapterSum = 0.0, renderSum = 0.0, geometryWarmSeconds = 0.0;
    double drawCallsSum = 0.0, instDrawsSum = 0.0, instCountSum = 0.0;
    double nearSum = 0.0, midSum = 0.0, farSum = 0.0, culledSum = 0.0;

    unsigned frame = 0;
    bool debug = false, follow = hasFocus, overheadToggle = (view == VIEW_OVERVIEW);
    Vector3 cameraCenter = hasCustomCam ? Vec3_Create(customCamX, 0.0f, customCamZ) : Vec3_Zero();

    float histBrum[ARENA_HISTORY_LEN] = {0}, histVelk[ARENA_HISTORY_LEN] = {0};
    unsigned historyCount = 0, historyIndex = 0;
    double nextSample = 0.0;

    /* Ring buffer de tiempos de fotograma para métricas en vivo del panel P. */
    double frameRing[ARENA_FRAME_RING] = {0};
    unsigned frameRingCount = 0, frameRingIdx = 0;
    double lastPerfRefreshMs = -1000.0;
    char perfPanelBuf[1024] = "COLLECTING TELEMETRY...";

    const double speeds[] = {1.0, 4.0, 16.0, 64.0};
    unsigned speedIndex = initialMaxSpeed ? 4u
                        : (initialSpeed >= 64.0 ? 3u : initialSpeed >= 16.0 ? 2u : initialSpeed >= 4.0 ? 1u : 0u);
    bool uiPaused = stepLocked;
    AIObjectId selectedRequestId = 0, inspectedTargetSent = selectedEntityId, captureTargetId = 0;
    bool focusWarned = false, captureWarned = false, updateWarned = false, projectionWarned = false;
    unsigned mouthWindowCount = 0, mouthWindowIndex = 0;
    bool scaleProofPrinted[5] = {false};
    float previousObservedYaw = 0.0f;
    AIObjectId previousObservedId = 0;

    /* Estado de gestión de cerebros en el laboratorio. */
    bool hasClipboard = false;
    NeuralGenome clipboardBrain = {0};
    AIObjectId clipboardSourceId = 0, swapPendingId = 0;
    PromptMode promptMode = PROMPT_NONE;
    bool promptSavedPause = false;
    bool promptOverwrite = false;
    char promptPath[256] = {0};
    uint64_t pendingExportCmd = 0, pendingImportCmd = 0;

    /* Estado del inspector de red neuronal y sustancias. */
    float edgeThreshold = 0.35f;
    int inspectedLayer = -1, inspectedIndex = -1;
    AIObjectId inspectedResourceId = cliInspectResourceId;
    UiButton btnExport = {0}, btnImport = {0}, btnCopy = {0}, btnPaste = {0}, btnSwap = {0}, btnReset = {0};
    UiButton btnThMinus = {0}, btnThPlus = {0}, btnClearSel = {0};
    UiButton btnResPrev = {0}, btnResNext = {0};
    float inPos[NEURAL_RNN_INPUTS][2] = {{0}};
    float hidPos[NEURAL_RNN_HIDDEN][2] = {{0}};
    float outPos[NEURAL_RNN_OUTPUTS][2] = {{0}};

    /* Campos finales para el reporte --profile. */
    unsigned finalPopulation = initialPopulation, profilePopulation = initialPopulation, minPopulation = UINT32_MAX;
    uint64_t finalTick = 0;
    CreatureControllerType finalController = config.controller;
    char finalFounderMode[32] = "random";
    EcosystemStatistics finalStats = {0};
    double finalAchievedSpeed = 0.0, finalRequestedSpeed = initialSpeed, finalTps = 0.0;
    double finalSimMeanMs = 0.0, finalSimP95Ms = 0.0;
    unsigned finalWorkers = config.workers;
    double presentationTime = 0.0;
    unsigned glErrorCount = 0;
    GLint actualMsaa = 0;
    glGetIntegerv(GL_SAMPLES, &actualMsaa);

    clock_t processStart = clock();

    while (!frames || frame < frames) {
        double start = OpenGLDemoWindow_Time(), frameSeconds = start - last;
        last = start;
        if (frame > 0 && frameSeconds > 0.0005 && frameSeconds < 1.0) {
            displayedFps += (1.0 / frameSeconds - displayedFps) * 0.1;
        }
        double dt = frames ? (1.0 / 60.0) : fmin(frameSeconds, 0.1);
        OpenGLDemoInput input = OpenGLDemoWindow_Poll(window);

        /* Recoger acuses de comandos completados en el hilo de simulación. */
        EvolutionCommandAck ack;
        while (EvolutionSimulationRunner_PopAck(runner, &ack)) {
            if (ack.type == EVO_CMD_EXPORT_BRAIN && ack.commandId == pendingExportCmd) {
                pendingExportCmd = 0;
                if (ack.status == EVO_CMD_STATUS_OK) {
                    snprintf(statusMsg, sizeof(statusMsg), "%s", ack.message);
                    statusError = false;
                    promptMode = PROMPT_NONE;
                    promptOverwrite = false;
                    if (!promptSavedPause) EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_RESUME);
                } else if (strstr(ack.message, "ya existe")) {
                    promptOverwrite = true;
                    snprintf(statusMsg, sizeof(statusMsg), "FILE EXISTS! PRESS ENTER AGAIN TO OVERWRITE");
                    statusError = true;
                } else {
                    snprintf(statusMsg, sizeof(statusMsg), "EXPORT ERR: %.140s", ack.message);
                    statusError = true;
                }
            } else if (ack.type == EVO_CMD_IMPORT_BRAIN && ack.commandId == pendingImportCmd) {
                pendingImportCmd = 0;
                if (ack.status == EVO_CMD_STATUS_OK) {
                    snprintf(statusMsg, sizeof(statusMsg), "%s", ack.message);
                    statusError = false;
                    promptMode = PROMPT_NONE;
                    if (!promptSavedPause) EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_RESUME);
                } else {
                    snprintf(statusMsg, sizeof(statusMsg), "IMPORT ERR: %.140s", ack.message);
                    statusError = true;
                }
            } else if (ack.type == EVO_CMD_ASSIGN_BRAIN || ack.type == EVO_CMD_CLONE_BRAIN ||
                       ack.type == EVO_CMD_SWAP_BRAINS || ack.type == EVO_CMD_RESET_BRAIN_RANDOM ||
                       ack.type == EVO_CMD_RESET) {
                snprintf(statusMsg, sizeof(statusMsg), "%s", ack.message);
                statusError = (ack.status != EVO_CMD_STATUS_OK);
            }
        }

        /* Modo de entrada de ruta para exportar/importar cerebro. */
        if (promptMode != PROMPT_NONE) {
            if (input.quit) {
                promptMode = PROMPT_NONE;
                if (!promptSavedPause) EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_RESUME);
                snprintf(statusMsg, sizeof(statusMsg), "CANCELLED");
                statusError = false;
                input.quit = false;
            } else {
                if (input.keyBackspace) {
                    size_t len = strlen(promptPath);
                    if (len > 0) promptPath[len - 1] = '\0';
                }
                if (input.textInput[0]) {
                    size_t len = strlen(promptPath);
                    for (size_t k = 0; input.textInput[k] && len + 1 < sizeof(promptPath); ++k) {
                        promptPath[len++] = input.textInput[k];
                    }
                    promptPath[len] = '\0';
                }
                if (input.keyEnter) {
                    if (!selectedEntityId) {
                        snprintf(statusMsg, sizeof(statusMsg), "NO CREATURE SELECTED");
                        statusError = true;
                    } else if (promptMode == PROMPT_EXPORT) {
                        ensureParentDir(promptPath);
                        pendingExportCmd = EvolutionSimulationRunner_PostExportBrain(runner, selectedEntityId, promptPath, promptOverwrite);
                        snprintf(statusMsg, sizeof(statusMsg), "EXPORTING #%u...", selectedEntityId);
                        statusError = false;
                    } else if (promptMode == PROMPT_IMPORT) {
                        pendingImportCmd = EvolutionSimulationRunner_PostImportBrain(runner, selectedEntityId, promptPath);
                        snprintf(statusMsg, sizeof(statusMsg), "IMPORTING INTO #%u...", selectedEntityId);
                        statusError = false;
                    }
                }
            }
            input.togglePause = input.toggleDebug = input.keyR = input.keyF = input.keyN = false;
            input.keyE = input.keyI = input.keyC = input.keyV = input.keyX = input.keyB = input.keyO = false;
            input.keyP = input.keyQ = input.keyM = input.keyG = input.keyT = false;
            input.digitKey = -1;
            input.surfaceDebugNext = false;
            input.panForward = input.panRight = 0;
        }

        if (input.quit) break;

        if (pausedForWarmup && frame == profileWarmup) {
            uint64_t command = EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_RESUME);
            EvolutionCommandAck ack;
            EvolutionSimulationRunner_WaitCommand(runner, command, 5.0, &ack);
        }

        /* En modo determinista paso-a-paso (--capture/--validate), avanzar ticks exactos antes de leer snapshot. */
        double simStepMs = 0.0;
        if (stepLocked) {
            unsigned stepTicks = exactStepTicks ? exactStepTicks : (unsigned)fmax(1.0, round(initialSpeed));
            double s0 = OpenGLDemoWindow_Time();
            uint64_t scid = EvolutionSimulationRunner_PostStepExact(runner, stepTicks);
            EvolutionCommandAck sack;
            EvolutionSimulationRunner_WaitCommand(runner, scid, 10.0, &sack);
            simStepMs = (OpenGLDemoWindow_Time() - s0) * 1000.0;
        }

        /* Adquirir el snapshot inmutable más reciente (triple buffering seguro). */
        const EvolutionRenderSnapshot *snap = EvolutionSimulationRunner_AcquireSnapshot(runner);
        if (!snap) {
            if (!frames) OpenGLDemoWindow_LimitFPS(start, ARENA_MAX_FPS);
            continue;
        }
        if (simulationTimes) simulationTimes[frame] = stepLocked ? simStepMs : snap->meanTickMs;
        uiPaused = snap->paused;
        if (frame > 0 && (!uiPaused || stepLocked))
            presentationTime += forceRealtime || !frames ? fmin(frameSeconds, 0.1) : 1.0 / 60.0;

        /* Crecer buffers dinámicos si la capacidad del mundo aumentó. */
        size_t needCap = snap->capacity > snap->entityCount ? snap->capacity : snap->entityCount;
        if (needCap < 64u) needCap = 64u;
        if (needCap > dynCap) {
            size_t newCap = dynCap;
            while (newCap < needCap) newCap *= 2u;
            EcosystemVisualDrawPacket *np = realloc(packets, newCap * sizeof(*np));
            EcosystemVisualDrawPacket *nd = realloc(displayedPackets, newCap * sizeof(*nd));
            Mesh *nc = realloc(cpuBodies, newCap * sizeof(*nc));
            AIObjectId *nv = realloc(validatedSkin, newCap * sizeof(*nv));
            uint64_t *nm = realloc(validatedSkinMesh, newCap * sizeof(*nm));
            if (np && nd && nc && nv && nm) {
                memset(nc + dynCap, 0, (newCap - dynCap) * sizeof(*nc));
                memset(nv + dynCap, 0, (newCap - dynCap) * sizeof(*nv));
                memset(nm + dynCap, 0, (newCap - dynCap) * sizeof(*nm));
                packets = np; displayedPackets = nd; cpuBodies = nc;
                validatedSkin = nv; validatedSkinMesh = nm;
                dynCap = newCap;
            }
        }

        /* Gestión de clics en interfaz 2D antes de selección 3D. */
        bool uiConsumedClick = false;
        const EvolutionEntitySnapshot *selectedNow = findSnapEntity(snap, selectedEntityId);
        if (input.mouseLeftPressed && showHud && promptMode == PROMPT_NONE) {
            if (hitRect(input.mouseX, input.mouseY, btnExport.x, btnExport.y, btnExport.w, btnExport.h)) {
                input.keyE = true; uiConsumedClick = true;
            } else if (hitRect(input.mouseX, input.mouseY, btnImport.x, btnImport.y, btnImport.w, btnImport.h)) {
                input.keyI = true; uiConsumedClick = true;
            } else if (hitRect(input.mouseX, input.mouseY, btnCopy.x, btnCopy.y, btnCopy.w, btnCopy.h)) {
                input.keyC = true; uiConsumedClick = true;
            } else if (hitRect(input.mouseX, input.mouseY, btnPaste.x, btnPaste.y, btnPaste.w, btnPaste.h)) {
                input.keyV = true; uiConsumedClick = true;
            } else if (hitRect(input.mouseX, input.mouseY, btnSwap.x, btnSwap.y, btnSwap.w, btnSwap.h)) {
                input.keyX = true; uiConsumedClick = true;
            } else if (hitRect(input.mouseX, input.mouseY, btnReset.x, btnReset.y, btnReset.w, btnReset.h)) {
                input.keyB = true; uiConsumedClick = true;
            } else if (showSubstance && hitRect(input.mouseX, input.mouseY, btnResPrev.x, btnResPrev.y, btnResPrev.w, btnResPrev.h)) {
                uiConsumedClick = true;
                if (snap->resourceCount > 0u) {
                    unsigned curR = 0u;
                    for (unsigned r = 0u; r < snap->resourceCount; ++r) {
                        if (snap->resources[r].id == inspectedResourceId) { curR = r; break; }
                    }
                    inspectedResourceId = snap->resources[(curR + snap->resourceCount - 1u) % snap->resourceCount].id;
                }
            } else if (showSubstance && hitRect(input.mouseX, input.mouseY, btnResNext.x, btnResNext.y, btnResNext.w, btnResNext.h)) {
                uiConsumedClick = true;
                if (snap->resourceCount > 0u) {
                    unsigned curR = 0u;
                    for (unsigned r = 0u; r < snap->resourceCount; ++r) {
                        if (snap->resources[r].id == inspectedResourceId) { curR = r; break; }
                    }
                    inspectedResourceId = snap->resources[(curR + 1u) % snap->resourceCount].id;
                }
            } else if (showNeural && hitRect(input.mouseX, input.mouseY, 330.0f, 12.0f, 616.0f, 568.0f)) {
                uiConsumedClick = true;
                if (hitRect(input.mouseX, input.mouseY, btnThMinus.x, btnThMinus.y, btnThMinus.w, btnThMinus.h)) {
                    edgeThreshold = fmaxf(0.05f, edgeThreshold - 0.10f);
                } else if (hitRect(input.mouseX, input.mouseY, btnThPlus.x, btnThPlus.y, btnThPlus.w, btnThPlus.h)) {
                    edgeThreshold = fminf(2.50f, edgeThreshold + 0.10f);
                } else if (hitRect(input.mouseX, input.mouseY, btnClearSel.x, btnClearSel.y, btnClearSel.w, btnClearSel.h)) {
                    inspectedLayer = -1; inspectedIndex = -1;
                } else {
                    float bestD = 18.0f;
                    int bestL = -1, bestIdx = -1;
                    for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) {
                        float d = hypotf((float)input.mouseX - inPos[i][0], (float)input.mouseY - inPos[i][1]);
                        if (d < bestD) { bestD = d; bestL = 0; bestIdx = (int)i; }
                    }
                    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
                        float d = hypotf((float)input.mouseX - hidPos[h][0], (float)input.mouseY - hidPos[h][1]);
                        if (d < bestD) { bestD = d; bestL = 1; bestIdx = (int)h; }
                    }
                    for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
                        float d = hypotf((float)input.mouseX - outPos[o][0], (float)input.mouseY - outPos[o][1]);
                        if (d < bestD) { bestD = d; bestL = 2; bestIdx = (int)o; }
                    }
                    inspectedLayer = bestL;
                    inspectedIndex = bestIdx;
                }
            }
        }

        /* Selección 3D por rayo sobre criaturas, cuerpos de agua naturales o terreno. */
        if (input.mouseLeftPressed && !uiConsumedClick) {
            Vector3 forward = Vec3_Normalize(Vec3_Sub(camera.target, camera.position));
            Vector3 right = Vec3_Normalize(Vec3_Cross(forward, camera.up));
            Vector3 up = Vec3_Normalize(Vec3_Cross(right, forward));
            float x = 2.0f * ((input.mouseX + 0.5f) / (float)winW - 0.5f) * camera.aspectRatio;
            float y = 2.0f * (0.5f - (input.mouseY + 0.5f) / (float)winH);
            bool perspective = camera.fov > 0.0f;
            float scale = perspective ? tanf(camera.fov * 0.00872664626f) : (float)zoom * 0.5f;
            Vector3 offset = Vec3_Add(Vec3_Scale(right, x * scale), Vec3_Scale(up, y * scale));
            Vector3 origin = perspective ? camera.position : Vec3_Add(camera.position, offset);
            if (perspective) forward = Vec3_Normalize(Vec3_Add(forward, offset));
            AIObjectId picked = displayedCount ? EcosystemVisual_PickRay(displayedPackets, displayedCount, origin, forward,
                                                                         camera.nearPlane, camera.farPlane) : 0u;
            if (picked && findSnapEntity(snap, picked)) {
                selectedEntityId = picked;
                hasFocus = false; focusId = 0; follow = true;
                selectedNow = findSnapEntity(snap, selectedEntityId);
                printf("mouse_focus_entity=%u\n", picked);
            } else {
                Vector3 hitPos = {0};
                uint16_t hitWaterBodyId = 0u;
                if (EnvironmentRenderer_PickGroundRay(&envRenderer, origin, forward, camera.farPlane,
                                                      &hitPos, &hitWaterBodyId)) {
                    if (hitWaterBodyId != 0u) {
                        for (unsigned ri = 0; ri < snap->resourceCount; ++ri) {
                            if (snap->resources[ri].waterBodyId == hitWaterBodyId) {
                                inspectedResourceId = snap->resources[ri].id;
                                showSubstance = true;
                                snprintf(statusMsg, sizeof(statusMsg), "WATER BODY #%u AT (%.1f, %.1f, %.1f)",
                                         (unsigned)hitWaterBodyId, hitPos.x, hitPos.y, hitPos.z);
                                statusError = false;
                                break;
                            }
                        }
                    } else if (snap->staticWorld && !snap->staticWorld->recipe.flat) {
                        float slopeRad = 0.0f;
                        Terrain_SampleSlopeAngle(&snap->staticWorld->terrain, hitPos.x, hitPos.z, &slopeRad);
                        snprintf(statusMsg, sizeof(statusMsg), "TERRAIN (%.1f, %.1f) Y=%.2fM SLOPE=%.1fDEG",
                                 hitPos.x, hitPos.z, hitPos.y, slopeRad * 57.29578f);
                        statusError = false;
                    }
                }
            }
        }

        /* Atajos de simulación, velocidad, calidad, terreno y laboratorio. */
        if (input.togglePause) EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_TOGGLE_PAUSE);
        if (input.toggleDebug) {
            if (input.shiftHeld && EnvironmentRenderer_IsProcedural(&envRenderer)) {
                terrainDebugMode = (TerrainDebugMode)(((int)terrainDebugMode + 1) % TERRAIN_DEBUG_MODE_COUNT);
                EnvironmentRenderer_SetDebugMode(&envRenderer, terrainDebugMode);
                snprintf(statusMsg, sizeof(statusMsg), "TERRAIN DEBUG: %s", TerrainDebugMode_Name(terrainDebugMode));
                statusError = false;
            } else {
                debug = !debug;
            }
        }
        if (input.keyA) {
            worldPresetIdx = (worldPresetIdx + 1u) % (unsigned)(sizeof(kWorldPresets) / sizeof(kWorldPresets[0]));
            snprintf(config.worldRecipePath, sizeof(config.worldRecipePath), "%s", kWorldPresets[worldPresetIdx]);
            config.hasExplicitArenaSize = false;
            EvolutionSimulationRunner_PostReset(runner, &config);
            nextSample = 0.0; historyCount = historyIndex = 0;
            selectedEntityId = 0; selectedRequestId = 0; captureTargetId = 0;
            displayedCount = 0; follow = false; swapPendingId = 0;
            cameraCenter = Vec3_Zero();
            snprintf(statusMsg, sizeof(statusMsg), "WORLD PRESET: %s", kWorldPresets[worldPresetIdx]);
            statusError = false;
        }
        if (input.keyN) showNeural = !showNeural;
        if (input.keyG) showDigestive = !showDigestive;
        if (input.keyT) showSubstance = !showSubstance;
        if (input.keyP) showPerf = !showPerf;
        if (input.keyM) showMinimap = !showMinimap;
        if (input.furDensityAdjust != 0 && snap->resourceCount > 0u) {
            unsigned curR = 0u;
            for (unsigned r = 0u; r < snap->resourceCount; ++r) {
                if (snap->resources[r].id == inspectedResourceId) { curR = r; break; }
            }
            unsigned nextR = input.furDensityAdjust > 0
                ? (curR + 1u) % snap->resourceCount
                : (curR + snap->resourceCount - 1u) % snap->resourceCount;
            inspectedResourceId = snap->resources[nextR].id;
            showSubstance = true;
        }
        if (input.keyQ) {
            if (input.shiftHeld) {
                terrainWireframe = !terrainWireframe;
                EnvironmentRenderer_SetQuality(&envRenderer,
                    quality == QUALITY_LOW ? 0.65f : (quality == QUALITY_MEDIUM ? 1.0f : 1.35f),
                    (int)quality, terrainWireframe);
                snprintf(statusMsg, sizeof(statusMsg), "TERRAIN WIREFRAME: %s", terrainWireframe ? "ON" : "OFF");
                statusError = false;
            } else {
                quality = (QualityPreset)(((int)quality + 1) % 3);
                applyQualityBudget(&visual, quality);
                EnvironmentRenderer_SetQuality(&envRenderer,
                    quality == QUALITY_LOW ? 0.65f : (quality == QUALITY_MEDIUM ? 1.0f : 1.35f),
                    (int)quality, terrainWireframe);
                snprintf(statusMsg, sizeof(statusMsg), "QUALITY PRESET: %s", qualityName(quality));
                statusError = false;
            }
        }
        if (input.keyO) { overheadToggle = !overheadToggle; view = overheadToggle ? VIEW_OVERVIEW : VIEW_THREE_QUARTER; }
        if (input.furLengthAdjust < 0) edgeThreshold = fmaxf(0.05f, edgeThreshold - 0.10f);
        if (input.furLengthAdjust > 0) edgeThreshold = fminf(2.50f, edgeThreshold + 0.10f);
        if (input.digitKey >= 0 && input.digitKey < 4) {
            speedIndex = (unsigned)input.digitKey;
            EvolutionSimulationRunner_PostSetSpeed(runner, speeds[speedIndex], false);
        } else if (input.digitKey == 4) {
            speedIndex = 4u;
            EvolutionSimulationRunner_PostSetSpeed(runner, 64.0, true);
            snprintf(statusMsg, sizeof(statusMsg), "SPEED: MAX (THROUGHPUT LIMITED)");
            statusError = false;
        }
        if (input.surfaceSelect) angle += input.surfaceSelect * 0.12;
        if (input.mouseRightHeld) angle += input.mouseDeltaX * 0.008;
        if (input.surfaceAdjust) zoom = fmax(2.0, fmin(maxZoom, zoom - input.surfaceAdjust * fmax(0.3, zoom * 0.12)));
        if (input.mouseWheel != 0) zoom = fmax(2.0, fmin(maxZoom, zoom - input.mouseWheel * fmax(0.5, zoom * 0.10)));
        if (input.surfaceDebugNext && snap->entityCount > 0) {
            hasFocus = false; focusId = 0;
            unsigned curIdx = 0;
            for (unsigned i = 0; i < snap->entityCount; ++i) {
                if (snap->entities[i].id == selectedEntityId) { curIdx = i; break; }
            }
            selectedEntityId = snap->entities[(curIdx + 1u) % snap->entityCount].id;
            selectedNow = findSnapEntity(snap, selectedEntityId);
        }
        if (input.keyF) { follow = !follow; hasFocus = false; focusId = 0; }

        /* Acciones seguras de gestión de cerebros (enviadas como comandos al hilo autoritativo). */
        if (input.keyE && selectedNow) {
            promptMode = PROMPT_EXPORT;
            promptSavedPause = uiPaused;
            promptOverwrite = false;
            EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_PAUSE);
            snprintf(promptPath, sizeof(promptPath), "build/brains/creature_%u.brain.json", selectedNow->id);
            snprintf(statusMsg, sizeof(statusMsg), "TYPE EXPORT PATH + ENTER (ESC CANCEL)");
            statusError = false;
        }
        if (input.keyI && selectedNow) {
            promptMode = PROMPT_IMPORT;
            promptSavedPause = uiPaused;
            EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_PAUSE);
            snprintf(promptPath, sizeof(promptPath), "brains/seeded_baseline.brain.json");
            snprintf(statusMsg, sizeof(statusMsg), "TYPE IMPORT PATH + ENTER (ESC CANCEL)");
            statusError = false;
        }
        if (input.keyC && selectedNow) {
            if (snap->selected.valid && snap->selected.entityId == selectedNow->id &&
                snap->selected.hasNeuralBrain && NeuralGenome_Clone(&clipboardBrain, &snap->selected.genome)) {
                hasClipboard = true;
                clipboardSourceId = selectedNow->id;
                snprintf(statusMsg, sizeof(statusMsg), "COPIED BRAIN #%u (%08llX)",
                         selectedNow->id, (unsigned long long)(clipboardBrain.metadata.fingerprint & 0xFFFFFFFFu));
                statusError = false;
            } else {
                snprintf(statusMsg, sizeof(statusMsg), "COPY PENDING/NO NEURAL BRAIN (TRY AGAIN)");
                statusError = true;
            }
        }
        if (input.keyV && selectedNow) {
            if (!hasClipboard) {
                snprintf(statusMsg, sizeof(statusMsg), "CLIPBOARD EMPTY (PRESS C TO COPY)");
                statusError = true;
            } else {
                EvolutionSimulationRunner_PostAssignBrain(runner, selectedNow->id, &clipboardBrain, "paste");
                snprintf(statusMsg, sizeof(statusMsg), "PASTING BRAIN #%u -> #%u...", clipboardSourceId, selectedNow->id);
                statusError = false;
            }
        }
        if (input.keyX && selectedNow) {
            if (swapPendingId == 0 || swapPendingId == selectedNow->id || !findSnapEntity(snap, swapPendingId)) {
                swapPendingId = selectedNow->id;
                snprintf(statusMsg, sizeof(statusMsg), "SWAP ARMED: #%u -> SELECT 2ND + PRESS X", swapPendingId);
                statusError = false;
            } else {
                AIObjectId a = swapPendingId, b = selectedNow->id;
                EvolutionSimulationRunner_PostSwapBrains(runner, a, b);
                snprintf(statusMsg, sizeof(statusMsg), "SWAPPING #%u <-> #%u...", a, b);
                statusError = false;
                swapPendingId = 0;
            }
        }
        if (input.keyB && selectedNow) {
            EvolutionSimulationRunner_PostResetBrainRandom(runner, selectedNow->id);
            snprintf(statusMsg, sizeof(statusMsg), "RESETTING #%u TO RANDOM BRAIN...", selectedNow->id);
            statusError = false;
        }
        if (input.keyR) {
            if (input.shiftHeld) {
                config.worldSeed = (config.hasWorldSeedOverride ? config.worldSeed : config.seed) + 1u;
                config.hasWorldSeedOverride = true;
            }
            EvolutionSimulationRunner_PostReset(runner, &config);
            nextSample = 0.0; historyCount = historyIndex = 0;
            selectedEntityId = 0; selectedRequestId = 0; captureTargetId = 0;
            displayedCount = 0; follow = false; swapPendingId = 0;
            cameraCenter = Vec3_Zero();
            snprintf(statusMsg, sizeof(statusMsg), "RESETTING WORLD (SIM %u / WORLD %u)...",
                     config.seed, config.hasWorldSeedOverride ? config.worldSeed : config.seed);
            statusError = false;
        }

        /* Seguimiento de giro para CAPTURE_TURNING. */
        AIObjectId observedId = hasFocus ? focusId : selectedEntityId;
        const EvolutionEntitySnapshot *obsEnt = findSnapEntity(snap, observedId);
        if (obsEnt) {
            if (previousObservedId == observedId && dt > 1e-6) {
                float deltaYaw = obsEnt->yaw - previousObservedYaw;
                observedTurnRate = atan2f(sinf(deltaYaw), cosf(deltaYaw)) / (float)dt;
                turningEntity = observedId;
            }
            previousObservedYaw = obsEnt->yaw;
            previousObservedId = observedId;
        } else {
            turningEntity = 0;
            observedTurnRate = 0.0f;
            previousObservedId = 0;
        }

        if (snap->seconds >= nextSample) {
            histBrum[historyIndex] = (float)snap->brumacernoCount;
            histVelk[historyIndex] = (float)snap->velkarCount;
            historyIndex = (historyIndex + 1u) % ARENA_HISTORY_LEN;
            if (historyCount < ARENA_HISTORY_LEN) ++historyCount;
            nextSample = snap->seconds + config.day / 12.0;
        }

        /* Desplazamiento de cámara. */
        float panScale = (float)dt * fmaxf(8.0f, (float)zoom * 0.55f);
        cameraCenter.x += (float)input.panRight * panScale;
        cameraCenter.z -= (float)input.panForward * panScale;
        if (input.mouseMiddleHeld) {
            cameraCenter.x -= input.mouseDeltaX * (float)zoom * 0.0014f;
            cameraCenter.z -= input.mouseDeltaY * (float)zoom * 0.0014f;
            follow = false;
        }

        const EvolutionEntitySnapshot *selected = findSnapEntity(snap, selectedEntityId);
        if (!selected && snap->entityCount > 0) {
            selectedEntityId = snap->entities[0].id;
            selected = &snap->entities[0];
        }
        const EvolutionEntitySnapshot *focus = findSnapEntity(snap, focusId);
        if (hasFocus && !focus && !focusWarned) {
            fprintf(stderr, "[ADVERTENCIA] --focus=%u no corresponde a una entidad viva.\n", focusId);
            focusWarned = true;
        }
        if (lodCycle) {
            const VisualTier cycle[] = {VISUAL_TIER_NEAR, VISUAL_TIER_MID, VISUAL_TIER_FAR, VISUAL_TIER_NEAR};
            captureTier = cycle[(frame / 180) % 4];
        }
        if (captureTargetId) {
            const EvolutionEntitySnapshot *current = findSnapEntity(snap, captureTargetId);
            if (!current || !matchesCaptureSnap(current, captureState)) captureTargetId = 0;
        }
        if (captureState != CAPTURE_OVERVIEW && !captureTargetId) {
            for (unsigned i = 0; i < snap->entityCount; ++i) {
                const EvolutionEntitySnapshot *e = &snap->entities[i];
                if ((!captureSpecies || e->speciesId == captureSpecies) &&
                    (!hasFocus || e->id == focusId) &&
                    matchesCaptureSnap(e, captureState)) {
                    captureTargetId = e->id;
                    selectedEntityId = e->id;
                    selected = e;
                    break;
                }
            }
        }

        /* Notificar al hilo de simulación qué entidad debe poblar snap->selected. */
        AIObjectId desiredInspect = selected ? selected->id : 0u;
        if (desiredInspect != inspectedTargetSent) {
            EvolutionSimulationRunner_PostInspectTarget(runner, desiredInspect);
            inspectedTargetSent = desiredInspect;
        }

        /* Sincronizar mundo estático con el renderizador de entorno (O(1) si no cambia época/huella). */
        EnvironmentRenderer_SyncWorld(&envRenderer, snap->staticWorld);
        bool proceduralWorld = EnvironmentRenderer_IsProcedural(&envRenderer);
        camera.farPlane = fmaxf(480.0f, snap->arenaHalfSize * 6.0f);

        /* Reconciliación incremental primero para garantizar ranuras existentes antes de RequestTier. */
        if (!EcosystemVisual_SyncSnapshot(&visual, snap) && !projectionWarned) {
            fprintf(stderr, "[ADVERTENCIA] No se pudo sincronizar EcosystemVisual.\n");
            projectionWarned = true;
        }

        if (selectedRequestId && (!selected || selectedRequestId != selected->id))
            EcosystemVisual_ClearTierRequest(&visual, selectedRequestId);
        selectedRequestId = selected ? selected->id : 0u;
        if (selected) EcosystemVisual_RequestTier(&visual, selected->id, captureTier);
        if (focus) EcosystemVisual_RequestTier(&visual, focus->id, captureTier);
        if (captureTargetId) EcosystemVisual_RequestTier(&visual, captureTargetId, captureTier);

        Vector3 camTarget = cameraCenter;
        if (focus) camTarget = focus->position;
        else if (captureTargetId) {
            const EvolutionEntitySnapshot *ct = findSnapEntity(snap, captureTargetId);
            if (ct) camTarget = ct->position;
        } else if (follow && selected) camTarget = selected->position;
        else camTarget.y = EnvironmentRenderer_SampleGroundHeight(&envRenderer, camTarget.x, camTarget.z);
        cameraCenter = camTarget;

        const EvolutionEntitySnapshot *cameraSubject = focus ? focus : findSnapEntity(snap, captureTargetId);
        double cameraAngle = cameraSubject ? cameraSubject->yaw + (view == VIEW_THREE_QUARTER ? 0.65 : 0.0) : angle + orbitRate * dt;
        angle = cameraAngle;
        if (inspect && cameraSubject && cameraSubject->hasAnatomicalBody) {
            Vector3 local = Vec3_Create(0, cameraSubject->standingHeight * 0.52f, -cameraSubject->trunkLength * 0.42f);
            float invScale = 1.0f / fmaxf(1e-4f, cameraSubject->anatomicalScale);
            if (!strcmp(inspect, "head")) {
                Vector3 headWorld = cameraSubject->headWorldPos;
                local = Vec3_Lerp(headWorld,
                    Vec3_Add(cameraSubject->anatomyCenter, Vec3_Scale(Vec3_Create(
                        (cameraSubject->mouth.x - cameraSubject->position.x) * cosf(cameraSubject->yaw) -
                        (cameraSubject->mouth.z - cameraSubject->position.z) * sinf(cameraSubject->yaw),
                        cameraSubject->mouth.y - cameraSubject->position.y,
                        (cameraSubject->mouth.x - cameraSubject->position.x) * sinf(cameraSubject->yaw) +
                        (cameraSubject->mouth.z - cameraSubject->position.z) * cosf(cameraSubject->yaw)),
                        invScale)), 0.5f);
            } else if (!strcmp(inspect, "feet")) local = Vec3_Create(0, 0.25f, 0);
            else if (!strcmp(inspect, "neck")) local = Vec3_Create(0, cameraSubject->standingHeight * 0.75f, 0);
            local = Vec3_Scale(Vec3_Sub(local, cameraSubject->anatomyCenter), cameraSubject->anatomicalScale);
            float sn = sinf(cameraSubject->yaw), cs = cosf(cameraSubject->yaw);
            camTarget = Vec3_Add(cameraSubject->position, Vec3_Create(local.x * cs + local.z * sn, local.y, -local.x * sn + local.z * cs));
        }

        setCameraPose(&camera, camTarget, (float)zoom, cameraAngle, view, inspect != NULL);
        if (proceduralWorld) {
            float camGroundY = EnvironmentRenderer_SampleGroundHeight(&envRenderer, camera.position.x, camera.position.z);
            if (camera.position.y < camGroundY + 1.25f) {
                camera.position.y = camGroundY + 1.25f;
            }
        }

        if (hasCameraPosition) camera.position = fixedCameraPosition;
        if (hasCameraTarget) camera.target = fixedCameraTarget;
        if ((hasCameraPosition || hasCameraTarget) &&
            fabsf(camera.position.x - camera.target.x) + fabsf(camera.position.z - camera.target.z) > 0.001f)
            camera.up = Vec3_Create(0, 1, 0);
        camera.fov = perspectiveFov > 0.0f ? perspectiveFov : -(float)zoom;
        camera.aspectRatio = (float)winW / (float)winH;

        for (unsigned i = 0; i < snap->entityCount; ++i) {
            EcosystemVisualProjection projection = {0};
            if (!projectSnapEntity(&camera, (float)zoom, winW, winH, &snap->entities[i], &projection) && !projectionWarned) {
                fprintf(stderr, "[ADVERTENCIA] Proyección visual no finita.\n");
                projectionWarned = true;
            }
            EcosystemVisual_SetProjection(&visual, snap->entities[i].id, projection);
        }

        /* Precalentamiento inicial acotado de mallas compartidas visibles. */
        if (frame == 0) {
            EcosystemVisual_PrewarmVisible(&visual);
            double warmStart = OpenGLDemoWindow_Time();
            bool loading = true;
            while (loading && OpenGLDemoWindow_Time() - warmStart < 60.0) {
                EcosystemVisual_UpdateFromSnapshot(&visual, snap, 0.016f);
                size_t count = EcosystemVisual_GetDrawPackets(&visual, packets, dynCap);
                loading = false;
                for (size_t i = 0; i < count; ++i)
                    if (!packets[i].ready || ((packets[i].entityId == focusId || packets[i].entityId == captureTargetId) && packets[i].tier != captureTier))
                        loading = true;
                if (OpenGLDemoWindow_Poll(window).quit) { ok = false; loading = false; }
                if (loading) {
                    renderer.beginFrame(&renderer);
                    OpenGLRenderer_DrawTextPanel(winW, winH, winW / 2 - 230, winH / 2 - 20, "PREPARING EVOLUTION ARENA ANATOMY");
                    renderer.endFrame(&renderer); OpenGLDemoWindow_Swap(window);
                    OpenGLDemoWindow_LimitFPS(OpenGLDemoWindow_Time(), ARENA_MAX_FPS);
                }
            }
            if (loading) {
                fprintf(stderr, "No se completó el LOD solicitado durante la carga.\n");
                ok = false;
                EvolutionSimulationRunner_ReleaseSnapshot(runner, snap);
                break;
            }
            geometryWarmSeconds = OpenGLDemoWindow_Time() - warmStart;
            printf("geometry_warmup_seconds=%.6f cache_builds=%zu\n", geometryWarmSeconds, EcosystemVisual_BuildCount(&visual));
            start = OpenGLDemoWindow_Time(); last = start;
        }

        double adapterStart = OpenGLDemoWindow_Time();
        if (!EcosystemVisual_UpdateFromSnapshot(&visual, snap, (float)dt) && !updateWarned) {
            fprintf(stderr, "[ADVERTENCIA] EcosystemVisual_UpdateFromSnapshot agotó su estado.\n");
            updateWarned = true;
        }
        adapterSum += (OpenGLDemoWindow_Time() - adapterStart) * 1000.0;

        /* ================================================================== */
        /* Renderizado 3D: Terreno, Entorno Instanciado y Criaturas LOD/GPU   */
        /* ================================================================== */
        if (profile && frame == profileWarmup && profileWarmup)
            EnvironmentRenderer_SetProfiling(&envRenderer, true);
        if (frame == profileWarmup) profilePopulation = snap->entityCount;
        if (frame >= profileWarmup && snap->entityCount < minPopulation) minPopulation = snap->entityCount;
        double renderStart = OpenGLDemoWindow_Time();
        OpenGLRenderer_ResetFrameDrawStats(&renderer);
        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, winW, winH);

        float limit = snap->arenaHalfSize;
        if (proceduralWorld) {
            if (validateSkin) {
                OpenGLRenderer_BeginSceneList(&renderer);
                OpenGLRenderer_EndSceneList(&renderer);
                ok = EnvironmentRenderer_RenderScene(&envRenderer, &renderer, &camera, false, NULL, 0) && ok;
            } else OpenGLRenderer_BeginSceneList(&renderer);
        } else {
            drawMesh(&renderer, &meshes[0], Vec3_Create(0.0f, -0.03f, 0.0f), 0.0f, limit * 1.45f);

            /* Caché estática de perímetro y cuadrícula (1 única llamada batch por fotograma). */
            if (fabsf(limit - cachedGridHalfSize) > 0.01f) {
                lineBatchClear(&staticGrid);
                Vector3 corners[4] = {{-limit, 0.04f, -limit}, {limit, 0.04f, -limit}, {limit, 0.04f, limit}, {-limit, 0.04f, limit}};
                for (unsigned i = 0; i < 4; ++i) {
                    lineBatchPush(&staticGrid, corners[i], corners[(i + 1) % 4], Color_FromRGB(62, 198, 214));
                    Vector3 topA = Vec3_Add(corners[i], Vec3_Create(0, 0.45f, 0));
                    Vector3 topB = Vec3_Add(corners[(i + 1) % 4], Vec3_Create(0, 0.45f, 0));
                    lineBatchPush(&staticGrid, corners[i], topA, Color_FromRGB(62, 198, 214));
                    lineBatchPush(&staticGrid, topA, topB, Color_Create(46, 134, 150, 170));
                }
                float gridStep = limit >= 80.0f ? 20.0f : (limit >= 35.0f ? 10.0f : 5.0f);
                for (float g = -limit + gridStep; g < limit; g += gridStep) {
                    lineBatchPush(&staticGrid, Vec3_Create(g, 0.01f, -limit), Vec3_Create(g, 0.01f, limit), Color_Create(42, 66, 74, 120));
                    lineBatchPush(&staticGrid, Vec3_Create(-limit, 0.01f, g), Vec3_Create(limit, 0.01f, g), Color_Create(42, 66, 74, 120));
                }
                cachedGridHalfSize = limit;
            }
            if (staticGrid.count > 0) {
                OpenGLRenderer_BatchDebugLines(staticGrid.verts, staticGrid.cols, staticGrid.count, true);
            }
        }

        lineBatchClear(&dynamicLines);

        /* Obstáculos físicos estáticos (1 llamada instanciada). */
        envInstances.count = 0;
        for (unsigned i = 0; i < snap->obstacleCount; ++i) {
            const EvolutionObstacleSnapshot *ob = &snap->obstacles[i];
            instanceBatchPush(&envInstances, (OpenGLMeshInstance){
                .position = ob->position, .yaw = 0.0f, .scale = ob->radius, .scaleY = ob->radius,
                .bobY = 0.0f, .leanPitch = 0.0f, .tint = decorColors[4]
            });
            if (showHud) lineBatchPushRing(&dynamicLines, ob->position, ob->radius, 0.04f, 18u, Color_FromRGB(96, 122, 134));
        }
        if (envInstances.count > 0) {
            OpenGLRenderer_RenderMeshInstanced(&renderer, &meshes[4], envInstances.items, envInstances.count, false);
        }

        /* Resolver recurso inspeccionado para el inspector de sustancias (T). */
        const EvolutionResourceSnapshot *inspectedRes = findSnapResource(snap, inspectedResourceId);
        if (cliInspectSubstance && (!inspectedRes || !strstr(inspectedRes->substanceId, cliInspectSubstance))) {
            for (unsigned i = 0; i < snap->resourceCount; ++i) {
                if (strstr(snap->resources[i].substanceId, cliInspectSubstance)) {
                    inspectedRes = &snap->resources[i];
                    inspectedResourceId = inspectedRes->id;
                    break;
                }
            }
        }
        if (!inspectedRes && snap->resourceCount > 0u) {
            inspectedRes = &snap->resources[0];
            inspectedResourceId = inspectedRes->id;
        }

        /* Estanques de agua planos o estanques explícitos adicionales (en modo procedural NO dibujar discos falsos para orillas naturales). */
        envInstances.count = 0;
        bool anyMolecularPool = false;
        for (unsigned i = 0; i < snap->resourceCount; ++i) {
            const EvolutionResourceSnapshot *res = &snap->resources[i];
            if (proceduralWorld && res->waterBodyId != 0u && res->shoreSiteIndex > 0u) continue;
            bool isPool = res->molecular ? (res->presentationModel == SUBSTANCE_MODEL_WATER)
                                         : (res->category == ECO_WATER);
            if (!isPool) continue;
            if (res->molecular) anyMolecularPool = true;
            Color tint = res->molecular ? colorFromRgbFloat(res->color) : decorColors[1];
            instanceBatchPush(&envInstances, (OpenGLMeshInstance){
                .position = Vec3_Create(res->position.x, proceduralWorld ? (res->position.y + 0.02f) : -0.02f, res->position.z),
                .yaw = 0.0f, .scale = res->radius, .scaleY = fminf(res->radius, 1.5f),
                .bobY = 0.0f, .leanPitch = 0.0f, .tint = tint
            });
            Color ringCol = res->molecular
                ? Color_Create((unsigned char)fminf(255.0f, tint.r + 36.0f),
                               (unsigned char)fminf(255.0f, tint.g + 46.0f),
                               (unsigned char)fminf(255.0f, tint.b + 46.0f), 255)
                : Color_FromRGB(74, 188, 224);
            if (showHud) lineBatchPushTerrainRing(&dynamicLines, &envRenderer, res->position, res->radius, 0.03f, 20u, ringCol);
        }
        if (envInstances.count > 0) {
            if (proceduralWorld && !validateSkin) OpenGLRenderer_SceneCastShadow(&renderer, false);
            OpenGLRenderer_RenderMeshInstanced(&renderer, &meshes[1], envInstances.items, envInstances.count, anyMolecularPool);
            if (proceduralWorld && !validateSkin) OpenGLRenderer_SceneCastShadow(&renderer, true);
        }

        /* Plantas / frutos (1 llamada instanciada). */
        envInstances.count = 0;
        bool anyMolecularPlant = false;
        for (unsigned i = 0; i < snap->resourceCount; ++i) {
            const EvolutionResourceSnapshot *res = &snap->resources[i];
            bool isPlantLike = res->molecular
                ? (res->presentationModel == SUBSTANCE_MODEL_VEGETATION ||
                   res->presentationModel == SUBSTANCE_MODEL_FRUIT)
                : (res->category == ECO_PLANT);
            if (!isPlantLike || res->biomassFraction <= 0.001f) continue;
            if (res->molecular) anyMolecularPlant = true;
            float s = 0.3f + 1.4f * sqrtf(res->biomassFraction);
            Color tint = res->molecular ? colorFromRgbFloat(res->color) : decorColors[2];
            instanceBatchPush(&envInstances, (OpenGLMeshInstance){
                .position = res->position, .yaw = (float)(res->id % 17u) * 0.37f,
                .scale = s, .scaleY = (res->molecular && res->presentationModel == SUBSTANCE_MODEL_FRUIT) ? s * 1.18f : s,
                .bobY = 0.0f, .leanPitch = 0.0f, .tint = tint
            });
        }
        if (envInstances.count > 0) {
            OpenGLRenderer_RenderMeshInstanced(&renderer, &meshes[2], envInstances.items, envInstances.count, anyMolecularPlant);
        }

        /* Cadáveres/carne/genéricos (1 llamada instanciada). */
        envInstances.count = 0;
        bool anyMolecularMeat = false;
        for (unsigned i = 0; i < snap->resourceCount; ++i) {
            const EvolutionResourceSnapshot *res = &snap->resources[i];
            bool isCarcassLike = res->molecular
                ? (res->presentationModel == SUBSTANCE_MODEL_CARCASS ||
                   res->presentationModel == SUBSTANCE_MODEL_GENERIC)
                : (res->category != ECO_WATER && res->category != ECO_PLANT);
            if (!isCarcassLike || res->biomassFraction <= 0.001f) continue;
            if (res->molecular) anyMolecularMeat = true;
            float s = 0.3f + 1.4f * sqrtf(res->biomassFraction);
            Color tint = res->molecular ? colorFromRgbFloat(res->color) : decorColors[3];
            instanceBatchPush(&envInstances, (OpenGLMeshInstance){
                .position = res->position, .yaw = (float)(res->id % 13u) * 0.48f,
                .scale = s, .scaleY = s, .bobY = 0.0f, .leanPitch = 0.0f, .tint = tint
            });
        }
        if (envInstances.count > 0) {
            OpenGLRenderer_RenderMeshInstanced(&renderer, &meshes[3], envInstances.items, envInstances.count, anyMolecularMeat);
        }
        if (showSubstance && inspectedRes && showHud) {
            lineBatchPushTerrainRing(&dynamicLines, &envRenderer, inspectedRes->position,
                                     fmaxf(0.85f, inspectedRes->radius + 0.35f), 0.09f, 24u,
                                     Color_FromRGB(252, 214, 96));
        }

        /* Obtener paquetes de dibujo de criaturas. */
        size_t packetCount = EcosystemVisual_GetDrawPackets(&visual, packets, dynCap);
        bool frameHasMouth = false, captureTargetReady = false;
        double frameMouthError = 0.0;
        memcpy(displayedPackets, packets, packetCount * sizeof(*packets));
        displayedCount = packetCount;
        drawnSum += (double)packetCount;
        if (packetCount < drawnMin) drawnMin = (unsigned)packetCount;
        if (packetCount > drawnMax) drawnMax = (unsigned)packetCount;

        /* 1) Agrupar y renderizar criaturas FAR elegibles mediante GPU instancing (glDrawElementsInstanced). */
        unsigned bodyGroupCount = 0u, headGroupCount = 0u;
        bodyInstances.count = 0;
        headInstances.count = 0;
        for (size_t i = 0; i < packetCount; ++i) {
            const EcosystemVisualDrawPacket *p = &packets[i];
            if (!p->instancedEligible || forceCpu || validateSkin) continue;
            const Mesh *bm = p->bodyMesh ? p->bodyMesh : EcosystemVisual_FallbackMesh(&visual);
            if (bm && bm->vertexCount) {
                unsigned g = 0u;
                while (g < bodyGroupCount && bodyGroups[g].mesh != bm) ++g;
                if (g == bodyGroupCount && bodyGroupCount < 64u) {
                    bodyGroups[bodyGroupCount++] = (MeshGroup){.mesh = bm, .start = 0, .count = 0};
                }
                if (g < bodyGroupCount) ++bodyGroups[g].count;
            }
            if (p->headMesh && p->headMesh->vertexCount) {
                unsigned g = 0u;
                while (g < headGroupCount && headGroups[g].mesh != p->headMesh) ++g;
                if (g == headGroupCount && headGroupCount < 64u) {
                    headGroups[headGroupCount++] = (MeshGroup){.mesh = p->headMesh, .start = 0, .count = 0};
                }
                if (g < headGroupCount) ++headGroups[g].count;
            }
        }
        size_t bodyTotal = 0, headTotal = 0;
        for (unsigned g = 0; g < bodyGroupCount; ++g) { bodyGroups[g].start = bodyTotal; bodyTotal += bodyGroups[g].count; bodyGroups[g].count = 0; }
        for (unsigned g = 0; g < headGroupCount; ++g) { headGroups[g].start = headTotal; headTotal += headGroups[g].count; headGroups[g].count = 0; }
        if (bodyTotal > bodyInstances.capacity) {
            OpenGLMeshInstance *ni = realloc(bodyInstances.items, bodyTotal * sizeof(*ni));
            if (ni) { bodyInstances.items = ni; bodyInstances.capacity = bodyTotal; }
        }
        if (headTotal > headInstances.capacity) {
            OpenGLMeshInstance *ni = realloc(headInstances.items, headTotal * sizeof(*ni));
            if (ni) { headInstances.items = ni; headInstances.capacity = headTotal; }
        }
        if (bodyTotal > 0 && bodyInstances.capacity >= bodyTotal) {
            for (size_t i = 0; i < packetCount; ++i) {
                const EcosystemVisualDrawPacket *p = &packets[i];
                if (!p->instancedEligible || forceCpu || validateSkin) continue;
                Color tint = p->tintColor; /* Variación tonal individual exacta respecto a la receta canónica. */
                OpenGLMeshInstance inst = {
                    .position = p->position, .yaw = p->yaw, .scale = p->scale, .scaleY = p->scale,
                    .bobY = p->bobY, .leanPitch = p->leanPitch,
                    .orientation = p->orientation, .useOrientation = proceduralWorld,
                    .tint = tint
                };
                const Mesh *bm = p->bodyMesh ? p->bodyMesh : EcosystemVisual_FallbackMesh(&visual);
                if (bm && bm->vertexCount) {
                    for (unsigned g = 0; g < bodyGroupCount; ++g) {
                        if (bodyGroups[g].mesh == bm) {
                            bodyInstances.items[bodyGroups[g].start + bodyGroups[g].count++] = inst;
                            break;
                        }
                    }
                }
                if (p->headMesh && p->headMesh->vertexCount && headInstances.capacity >= headTotal) {
                    for (unsigned g = 0; g < headGroupCount; ++g) {
                        if (headGroups[g].mesh == p->headMesh) {
                            headInstances.items[headGroups[g].start + headGroups[g].count++] = inst;
                            break;
                        }
                    }
                }
            }
            for (unsigned g = 0; g < bodyGroupCount; ++g) {
                if (bodyGroups[g].count > 0) {
                    OpenGLRenderer_RenderMeshInstanced(&renderer, bodyGroups[g].mesh,
                                                       &bodyInstances.items[bodyGroups[g].start],
                                                       bodyGroups[g].count, true);
                }
            }
            for (unsigned g = 0; g < headGroupCount; ++g) {
                if (headGroups[g].count > 0) {
                    OpenGLRenderer_RenderMeshInstanced(&renderer, headGroups[g].mesh,
                                                       &headInstances.items[headGroups[g].start],
                                                       headGroups[g].count, true);
                }
            }
        }

        /* 2) Renderizar criaturas detalladas (NEAR y MID) con skinning GPU, pelaje (NEAR) y ojos/mandíbula. */
        for (size_t i = 0; i < packetCount; ++i) {
            EcosystemVisualDrawPacket *packet = &packets[i];
            if (packet->entityId == captureTargetId && packet->ready && packet->tier == VISUAL_TIER_NEAR)
                captureTargetReady = true;
            const EvolutionEntitySnapshot *packetEntity = findSnapEntity(snap, packet->entityId);
            if (packet->ready && packetEntity) {
                uint32_t recipeId = packetEntity->recipeId;
                if ((recipeId == 3u || recipeId == 4u) && !scaleProofPrinted[recipeId] && packet->meshRadius > 1e-4f) {
                    float renderedWidth = packet->scale * packet->meshRadius;
                    float size = packetEntity->genome.size * packetEntity->developmentScale;
                    float expectedWidth = packetEntity->speciesRadius * size;
                    float normalizedWidth = renderedWidth / fmaxf(0.01f, size);
                    printf("scale_proof recipeId=%u meshRadius=%.6f measuredMeshRadius=%.6f meshLength=%.6f calibrationLength=%.6f totalScale=%.6f k=%.9f rendered_width=%.6f species_radius=%.6f expected_width=%.6f normalized_width=%.6f\n",
                           recipeId, packet->meshRadius, packet->measuredMeshRadius, packet->meshLength,
                           packet->calibrationLength, packet->axialTotalScale, packet->calibrationFactor,
                           renderedWidth, packetEntity->speciesRadius, expectedWidth, normalizedWidth);
                    scaleProofPrinted[recipeId] = true;
                }
            }
            if (packet->instancedEligible && !forceCpu && !validateSkin) {
                if (validateMouthFrames) {
                    bool hasSample = false;
                    double err = mouthFrameErrorSnap(snap, packet, &hasSample);
                    if (hasSample) { frameHasMouth = true; if (err > frameMouthError) frameMouthError = err; }
                }
                continue;
            }
            const Mesh *body = packet->bodyMesh ? packet->bodyMesh : EcosystemVisual_FallbackMesh(&visual);
            if (body) {
                /* Desactivar capas de pelaje costosas en MID; conservar pelaje completo en NEAR y seleccionada. */
                bool enableFur = (packet->tier == VISUAL_TIER_NEAR) || (packet->entityId == selectedEntityId);
                OpenGLRenderer_SetFurQuality(&renderer, enableFur, enableFur, 0);

                if (proceduralWorld) {
                    OpenGLRenderer_ModelTransformQuat(packet->position, packet->orientation, packet->scale, Vec3_Zero());
                } else {
                    OpenGLRenderer_ModelTransform(packet->position, packet->yaw * 57.29578f, packet->scale, Vec3_Zero());
                }
                bool skinning = !forceCpu && packet->deformer && packet->skeleton && packet->pose &&
                                OpenGLRenderer_SetMeshMorphSkinning(&renderer, body, packet->deformer, packet->skeleton,
                                                                    packet->pose, packet->targetGraph, packet->headJoint,
                                                                    packet->headScale, packetGeneration(packet));
                OpenGLRenderer_SetMeshSurfaceOverride(&renderer, &packet->surfaceRecipe);
                if (skinning || !packet->ready) {
                    renderer.renderMesh(&renderer, body);
                } else if (packet->slotIndex < dynCap && EcosystemVisual_DeformBody(packet, &cpuBodies[packet->slotIndex])) {
                    renderer.renderMesh(&renderer, &cpuBodies[packet->slotIndex]);
                    ++visualProfiler.counters.cpuDeformationFallbacks;
                }
                if (validateSkin && skinning && packet->slotIndex < dynCap &&
                    (validatedSkin[packet->slotIndex] != packet->entityId ||
                     validatedSkinMesh[packet->slotIndex] != body->identity)) {
                    float positionError = 0.0f, normalError = 0.0f;
                    bool valid = OpenGLRenderer_ValidateMeshSkinning(&renderer, body, packet->deformer,
                                                                     packet->skeleton, packet->pose,
                                                                     &positionError, &normalError);
                    ok = valid && positionError < 0.0001f && normalError < 0.0002f && ok;
                    skinPositionError = fmaxf(skinPositionError, positionError);
                    skinNormalError = fmaxf(skinNormalError, normalError);
                    validatedSkin[packet->slotIndex] = packet->entityId;
                    validatedSkinMesh[packet->slotIndex] = body->identity;
                }
                footErrorMax = fmaxf(footErrorMax, packet->footError);
                stanceFootErrorMax = fmaxf(stanceFootErrorMax, packet->stanceFootError);
                OpenGLRenderer_SetMeshSkinning(&renderer, NULL, NULL, NULL, NULL, 0);
                OpenGLRenderer_PushJointMorphTransform(packet->restPose, packet->pose, packet->headJoint, packet->headScale);
                if (packet->headMesh && packet->headMesh->vertexCount) renderer.renderMesh(&renderer, packet->headMesh);
                if (renderer.renderEye && enableFur) {
                    for (size_t eye = 0; eye < packet->eyeCount; ++eye) {
                        OpenGLRenderer_ModelTransform(packet->eyes[eye].center, 0, packet->eyeScale, packet->eyes[eye].center);
                        renderer.renderEye(&renderer, &packet->eyes[eye]);
                        OpenGLRenderer_PopModelMatrix();
                    }
                }
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_PushJointMorphTransform(packet->restPose, packet->pose, packet->jawJoint, packet->jawScale);
                for (size_t mouth = 0; mouth < packet->mouthCount; ++mouth)
                    if (packet->mouths[mouth].jaw.vertexCount) renderer.renderMesh(&renderer, &packet->mouths[mouth].jaw);
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_PushJointMorphTransform(packet->restPose, packet->pose, packet->headJoint, packet->headScale);
                for (size_t mouth = 0; mouth < packet->mouthCount; ++mouth)
                    if (packet->mouths[mouth].hinge.vertexCount) renderer.renderMesh(&renderer, &packet->mouths[mouth].hinge);
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_SetMeshSurfaceOverride(&renderer, NULL);
                OpenGLRenderer_SetMeshSkinning(&renderer, NULL, NULL, NULL, NULL, 0);
                OpenGLRenderer_PopModelMatrix();
            }
            if (validateMouthFrames) {
                bool hasSample = false;
                double err = mouthFrameErrorSnap(snap, packet, &hasSample);
                if (hasSample) { frameHasMouth = true; if (err > frameMouthError) frameMouthError = err; }
            }
        }
        OpenGLRenderer_SetFurQuality(&renderer, true, true, 0);

        /* Pasada translúcida de agua natural y overlays conformes al relieve. */
        if (proceduralWorld) {
            if (!validateSkin) {
                EnvironmentRenderer_QueueDecoration(&envRenderer, &renderer, &camera);
                OpenGLRenderer_EndSceneList(&renderer);
                ok = EnvironmentRenderer_RenderScene(&envRenderer, &renderer, &camera, true, packets, packetCount) && ok;
            }
            EnvironmentRenderer_RenderWater(&envRenderer, &camera,
                                             fixedWaterTime >= 0.0f ? fixedWaterTime : (float)fmod(presentationTime, 80.0));
            EnvironmentRenderer_EndScene(&envRenderer);
            EnvironmentRenderer_RenderOverlays(&envRenderer, &camera);
        }

        if (validateMouthFrames && frameHasMouth) {
            mouthWindow[mouthWindowIndex] = frameMouthError;
            mouthWindowIndex = (mouthWindowIndex + 1) % validateMouthFrames;
            if (mouthWindowCount < validateMouthFrames) ++mouthWindowCount;
        }

        /* Indicadores de especie/selección y overlays opcionales (D) en un único batch. */
        bool drawAllRings = showHud && !(zoom > 55.0 && snap->entityCount > 120u && !debug);
        unsigned ringSegs = snap->entityCount > 120u ? 12u : 22u;
        for (unsigned i = 0; i < snap->entityCount; ++i) {
            const EvolutionEntitySnapshot *entity = &snap->entities[i];
            if (drawAllRings) {
                Color spMark = entity->speciesIndex == 0 ? Color_Create(110, 216, 152, 160) : Color_Create(238, 118, 92, 160);
                lineBatchPushTerrainRing(&dynamicLines, &envRenderer, entity->position, entity->clearance * 0.92f, 0.04f, ringSegs, spMark);
            }
            if (debug) {
                lineBatchPush(&dynamicLines, entity->oralPivot, entity->oralElbow, Color_FromRGB(245, 200, 100));
                lineBatchPush(&dynamicLines, entity->oralElbow, entity->mouth, Color_FromRGB(245, 200, 100));
                if (entity->hasTarget) {
                    lineBatchPush(&dynamicLines, entity->eyePos, entity->targetPos, Color_FromRGB(68, 224, 240));
                }
            }
        }
        if (debug && snap->selected.valid && snap->selected.navPointCount > 1u) {
            for (unsigned p = snap->selected.navPointIndex; p + 1u < snap->selected.navPointCount; ++p) {
                lineBatchPush(&dynamicLines,
                    Vec3_Add(snap->selected.navPoints[p], Vec3_Create(0, 0.08f, 0)),
                    Vec3_Add(snap->selected.navPoints[p + 1u], Vec3_Create(0, 0.08f, 0)),
                    Color_FromRGB(120, 210, 225));
            }
        }
        if (selected && showHud) {
            lineBatchPushTerrainRing(&dynamicLines, &envRenderer, selected->position, selected->clearance + 0.28f, 0.08f, 28u, Color_FromRGB(74, 236, 250));
            if (swapPendingId && swapPendingId != selected->id) {
                const EvolutionEntitySnapshot *sw = findSnapEntity(snap, swapPendingId);
                if (sw) lineBatchPushTerrainRing(&dynamicLines, &envRenderer, sw->position, sw->clearance + 0.34f, 0.09f, 28u, Color_FromRGB(252, 206, 82));
            }
        }
        if (dynamicLines.count > 0) {
            OpenGLRenderer_BatchDebugLines(dynamicLines.verts, dynamicLines.cols, dynamicLines.count, true);
        }

        EcosystemVisualStats vStats = EcosystemVisual_GetStats(&visual);
        OpenGLMeshPerformanceStats drawStatsNow = OpenGLRenderer_GetMeshPerformanceStats(&renderer);
        drawCallsSum += (double)drawStatsNow.drawCalls;
        instDrawsSum += (double)drawStatsNow.instancedDrawCalls;
        instCountSum += (double)drawStatsNow.instancedCount;
        nearSum += (double)vStats.nearCount;
        midSum += (double)vStats.midCount;
        farSum += (double)vStats.farCount;
        culledSum += (double)vStats.culledCount;

        /* ================================================================== */
        /* Interfaz 2D de laboratorio                                         */
        /* ================================================================== */
        if (showHud) {
            OpenGLRenderer_Begin2D(winW, winH);

            /* 1) Panel Izquierdo: Estadísticas de Población y Gráfica. */
            const float lx = 12.0f, ly = 12.0f, lw = 308.0f, lh = 396.0f;
            OpenGLRenderer_DrawRect2D(lx, ly, lw, lh, Color_Create(9, 15, 22, 226), Color_Create(42, 138, 158, 205));
            OpenGLRenderer_DrawRect2D(lx, ly, lw, 26.0f, Color_Create(14, 28, 39, 240), Color_Create(42, 138, 158, 205));
            char hdr[96];
            snprintf(hdr, sizeof(hdr), "EVOLUTION ARENA [%s%s]",
                     snap->controller == CREATURE_CONTROLLER_NEURAL ? snap->founderModeName : "UTILITY",
                     snap->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR ? "/MOL" : "");
            OpenGLRenderer_DrawText2D(lx + 10.0f, ly + 7.0f, 1.32f, Color_Create(78, 222, 236, 255), hdr);

            unsigned minute = (unsigned)(snap->timeOfDay * 1440.0);
            char reqBuf[16];
            if (snap->maxSpeedMode) snprintf(reqBuf, sizeof(reqBuf), "MAX");
            else snprintf(reqBuf, sizeof(reqBuf), "%2.0fX", snap->requestedSpeed);
            char statsTxt[512];
            if (snap->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR) {
                snprintf(statsTxt, sizeof(statsTxt),
                         "SEED %-6u  DAY %3.0f %02u:%02u\n"
                         "REQ %-3s  ACH %5.1fX  %s%s\n"
                         "POP %4u/%-4u GEN MAX %-2u\n"
                         "BIRTHS %-4llu  DEATHS %-4llu\n"
                         "STV %-3llu DHY %-3llu PRD %-3llu TOX %-3llu\n"
                         "AVG HP %.2f  AVG ENG %.2f\n"
                         "INGEST %5.0f  DIV %.3f\n"
                         "LINEAGES %-3u  W:%u Q:%s",
                         config.seed, floor(snap->days), minute / 60, minute % 60,
                         reqBuf, snap->achievedSpeed, snap->paused ? "PAUSE" : "RUN",
                         snap->behind ? " LIM" : "",
                         snap->entityCount, snap->capacity, snap->statistics.maximumGeneration,
                         (unsigned long long)snap->statistics.births, (unsigned long long)snap->statistics.deaths,
                         (unsigned long long)snap->statistics.deathCauses[DEATH_STARVATION],
                         (unsigned long long)snap->statistics.deathCauses[DEATH_DEHYDRATION],
                         (unsigned long long)snap->statistics.deathCauses[DEATH_PREDATION],
                         (unsigned long long)snap->statistics.deathCauses[DEATH_TOXICITY],
                         snap->statistics.averageHealth, snap->statistics.averageEnergy,
                         snap->statistics.totalIngestedMass, snap->statistics.neuralDiversity,
                         snap->statistics.activeLineages, snap->workerCount, qualityName(quality));
            } else {
                snprintf(statsTxt, sizeof(statsTxt),
                         "SEED %-6u  DAY %3.0f %02u:%02u\n"
                         "REQ %-3s  ACH %5.1fX  %s%s\n"
                         "POP %4u/%-4u GEN MAX %-2u\n"
                         "BIRTHS %-4llu  DEATHS %-4llu\n"
                         "STARV %-3llu DEHYD %-3llu PRED %-3llu\n"
                         "AVG HP %.2f  AVG ENG %.2f\n"
                         "BIOMASS %5.0f  DIV %.3f\n"
                         "LINEAGES %-3u  W:%u Q:%s",
                         config.seed, floor(snap->days), minute / 60, minute % 60,
                         reqBuf, snap->achievedSpeed, snap->paused ? "PAUSE" : "RUN",
                         snap->behind ? " LIM" : "",
                         snap->entityCount, snap->capacity, snap->statistics.maximumGeneration,
                         (unsigned long long)snap->statistics.births, (unsigned long long)snap->statistics.deaths,
                         (unsigned long long)snap->statistics.deathCauses[DEATH_STARVATION],
                         (unsigned long long)snap->statistics.deathCauses[DEATH_DEHYDRATION],
                         (unsigned long long)snap->statistics.deathCauses[DEATH_PREDATION],
                         snap->statistics.averageHealth, snap->statistics.averageEnergy,
                         snap->statistics.plantBiomass, snap->statistics.neuralDiversity,
                         snap->statistics.activeLineages, snap->workerCount, qualityName(quality));
            }
            OpenGLRenderer_DrawText2D(lx + 10.0f, ly + 36.0f, 1.35f, Color_Create(218, 234, 240, 255), statsTxt);

            float gy = ly + 258.0f;
            char spBuf[64];
            snprintf(spBuf, sizeof(spBuf), "BRUMACERNO: %u", snap->brumacernoCount);
            OpenGLRenderer_DrawRect2D(lx + 10.0f, gy, 10.0f, 10.0f, Color_Create(110, 216, 152, 255), Color_Create(0, 0, 0, 0));
            OpenGLRenderer_DrawText2D(lx + 24.0f, gy, 1.25f, Color_Create(158, 232, 186, 255), spBuf);
            snprintf(spBuf, sizeof(spBuf), "VELKAR: %u", snap->velkarCount);
            OpenGLRenderer_DrawRect2D(lx + 170.0f, gy, 10.0f, 10.0f, Color_Create(238, 118, 92, 255), Color_Create(0, 0, 0, 0));
            OpenGLRenderer_DrawText2D(lx + 184.0f, gy, 1.25f, Color_Create(244, 158, 138, 255), spBuf);

            float gBrum[ARENA_HISTORY_LEN], gVelk[ARENA_HISTORY_LEN];
            for (unsigned i = 0; i < historyCount; ++i) {
                unsigned idx = (historyIndex + ARENA_HISTORY_LEN - historyCount + i) % ARENA_HISTORY_LEN;
                gBrum[i] = histBrum[idx];
                gVelk[i] = histVelk[idx];
            }
            OpenGLRenderer_DrawMultiHistory2D(lx + 10.0f, gy + 18.0f, lw - 20.0f, 108.0f,
                                              gBrum, Color_Create(110, 216, 152, 255),
                                              gVelk, Color_Create(238, 118, 92, 255),
                                              historyCount, 12.0f);

            /* 1b) Panel Opcional de Rendimiento y Diagnóstico (P), refrescado a ~5 Hz. */
            if (showPerf) {
                double nowMsVal = EvolutionSimulationRunner_NowMs();
                if (nowMsVal - lastPerfRefreshMs >= 200.0 || frame == 0) {
                    lastPerfRefreshMs = nowMsVal;
                    double sorted[ARENA_FRAME_RING];
                    unsigned n = frameRingCount;
                    double fMean = 0.0, fP95 = 0.0, fP99 = 0.0;
                    if (n > 0) {
                        for (unsigned k = 0; k < n; ++k) { sorted[k] = frameRing[k]; fMean += sorted[k]; }
                        qsort(sorted, n, sizeof(double), compareDouble);
                        fMean /= (double)n;
                        fP95 = sorted[(n - 1u) * 95u / 100u];
                        fP99 = sorted[(n - 1u) * 99u / 100u];
                    }
                    double snapAgeMs = fmax(0.0, nowMsVal - snap->publishedAtMs);
                    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
                    double rssMb = (double)ru.ru_maxrss / 1024.0;
                    double cacheMb = (double)vStats.cacheBytes / (1024.0 * 1024.0);
                    EnvironmentRenderStats envSt = EnvironmentRenderer_GetStats(&envRenderer);
                    if (envSt.isProcedural && snap->staticWorld) {
                        const WorldStaticData *sw = snap->staticWorld;
                        snprintf(perfPanelBuf, sizeof(perfPanelBuf),
                                 "FPS %5.1f  FRM %.1f P95 %.1f P99 %.1f\n"
                                 "SIM %.2fMS (P95 %.2f)  TPS %5.0f\n"
                                 "SPEED REQ %-3s  ACH %5.1fX  CPU %2.0f%%\n"
                                 "WORLD %.16s S:%u V%u\n"
                                 "WFP %08llX  L:%u R:%u SH:%u RK:%u\n"
                                 "CHK %u/%u L0:%u L1:%u L2:%u L3:%u\n"
                                 "ENV TRI %llu  DC %llu  VRAM %.1fMB\n"
                                 "POP %4u/%-4u VIS:%4zu N:%2zu M:%2zu F:%3zu\n"
                                 "DRAWS %3llu INST %2llu (%4llu OBJ)\n"
                                 "DBG:%s  RSS %.1fMB",
                                 displayedFps, fMean, fP95, fP99,
                                 snap->meanTickMs, snap->p95TickMs, snap->ticksPerSecond,
                                 reqBuf, snap->achievedSpeed, snap->simCpuUtilization,
                                 sw->recipe.id[0] ? sw->recipe.id : "procedural",
                                 sw->masterSeed, sw->generatorVersion,
                                 (unsigned long long)(sw->dataFingerprint & 0xFFFFFFFFu),
                                 sw->hydrology.lakeCount, sw->hydrology.riverCount,
                                 sw->hydrology.shoreSiteCount, sw->geology.rockCount,
                                 envSt.terrain.renderedChunks, envSt.terrain.totalChunks,
                                 envSt.terrain.lodCounts[0], envSt.terrain.lodCounts[1],
                                 envSt.terrain.lodCounts[2], envSt.terrain.lodCounts[3],
                                 (unsigned long long)envSt.totalTriangles,
                                 (unsigned long long)envSt.totalDrawCalls,
                                 (double)envSt.totalVramBytes / (1024.0 * 1024.0),
                                 snap->entityCount, snap->capacity,
                                 vStats.visibleCount, vStats.nearCount, vStats.midCount, vStats.farCount,
                                 (unsigned long long)drawStatsNow.drawCalls,
                                 (unsigned long long)drawStatsNow.instancedDrawCalls,
                                 (unsigned long long)drawStatsNow.instancedCount,
                                 TerrainDebugMode_Name(terrainDebugMode), rssMb);
                    } else {
                        snprintf(perfPanelBuf, sizeof(perfPanelBuf),
                                 "FPS %5.1f  FRM %.1f P95 %.1f P99 %.1f\n"
                                 "SIM %.2fMS (P95 %.2f)  TPS %5.0f\n"
                                 "SPEED REQ %-3s  ACH %5.1fX  CPU %2.0f%%\n"
                                 "BACKLOG %.3fS  SNAP AGE %4.1fMS\n"
                                 "POP %4u/%-4u  WORKERS %u  QLT %s\n"
                                 "VIS %4zu  N:%2zu M:%2zu F:%3zu CUL:%3zu\n"
                                 "ANIM %3zu  MONSTERS %2zu  BUILD %zu\n"
                                 "DRAWS %3llu  INST %2llu (%4llu OBJ)\n"
                                 "REACH %llu  NAV %llu  CAND %llu\n"
                                 "CACHE %zu (%.1fMB)  RSS %.1fMB",
                                 displayedFps, fMean, fP95, fP99,
                                 snap->meanTickMs, snap->p95TickMs, snap->ticksPerSecond,
                                 reqBuf, snap->achievedSpeed, snap->simCpuUtilization,
                                 snap->backlogSeconds, snapAgeMs,
                                 snap->entityCount, snap->capacity, snap->workerCount, qualityName(quality),
                                 vStats.visibleCount, vStats.nearCount, vStats.midCount, vStats.farCount, vStats.culledCount,
                                 vStats.animatedCount, vStats.monsterInstances, vStats.buildCount,
                                 (unsigned long long)drawStatsNow.drawCalls,
                                 (unsigned long long)drawStatsNow.instancedDrawCalls,
                                 (unsigned long long)drawStatsNow.instancedCount,
                                 (unsigned long long)snap->simCounters.anatomicalSolverInvocations,
                                 (unsigned long long)snap->simCounters.navigationRequests,
                                 (unsigned long long)snap->simCounters.spatialQueryCandidates,
                                 vStats.cacheEntries, cacheMb, rssMb);
                    }
                }
                const float px = 12.0f, py = ly + lh + 10.0f, pw = 308.0f, ph = 248.0f;
                OpenGLRenderer_DrawRect2D(px, py, pw, ph, Color_Create(8, 14, 21, 232), Color_Create(58, 172, 192, 220));
                OpenGLRenderer_DrawRect2D(px, py, pw, 22.0f, Color_Create(15, 32, 44, 245), Color_Create(58, 172, 192, 220));
                OpenGLRenderer_DrawText2D(px + 10.0f, py + 5.0f, 1.25f, Color_Create(92, 232, 244, 255), "PERFORMANCE TELEMETRY [P]");
                OpenGLRenderer_DrawText2D(px + 8.0f, py + 28.0f, 1.18f, Color_Create(208, 234, 242, 255), perfPanelBuf);
            }

            /* 2) Panel Derecho: Inspector del Individuo Seleccionado + Controles de Cerebro. */
            const float rx = (float)winW - 324.0f, ry = 12.0f, rw = 312.0f, rh = 482.0f;
            OpenGLRenderer_DrawRect2D(rx, ry, rw, rh, Color_Create(9, 15, 22, 226), Color_Create(42, 138, 158, 205));
            OpenGLRenderer_DrawRect2D(rx, ry, rw, 26.0f, Color_Create(14, 28, 39, 240), Color_Create(42, 138, 158, 205));
            if (selected) {
                const EvolutionSelectedSnapshot *ds = (snap->selected.valid && snap->selected.entityId == selected->id)
                                                      ? &snap->selected : NULL;
                const char *spName = selected->speciesIndex < snap->speciesCount ? snap->speciesNames[selected->speciesIndex] : "SPECIES";
                const NeuralGenome *ng = (ds && ds->hasNeuralBrain) ? &ds->genome : NULL;
                char rHdr[80];
                snprintf(rHdr, sizeof(rHdr), "CREATURE #%u  %s", selected->id, spName);
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 7.0f, 1.35f,
                                          selected->speciesIndex == 0 ? Color_Create(126, 232, 168, 255)
                                                                      : Color_Create(246, 146, 118, 255), rHdr);
                const char *stageStr = selected->lifeStage == LIFE_JUVENILE ? "JUV"
                                       : selected->lifeStage == LIFE_ADULT ? "ADULT" : "OLD";
                char idTxt[256];
                snprintf(idTxt, sizeof(idTxt),
                         "AGE %5.1fS (%s)  GEN %-2u\n"
                         "PARENTS %u,%u  OFFSP %-2u\n"
                         "LINEAGE #%llu",
                         ds ? ds->age : (double)selected->ageSeconds, stageStr, selected->generation,
                         ds ? ds->parents[0] : 0u, ds ? ds->parents[1] : 0u,
                         selected->offspringCount,
                         (unsigned long long)(ng ? ng->metadata.lineageId : 0u));
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 34.0f, 1.3f, Color_Create(208, 228, 236, 255), idTxt);

                drawPhysBar(rx + 10.0f, ry + 84.0f, rw - 20.0f, 16.0f, "HEALTH", selected->health, Color_Create(96, 214, 140, 235));
                drawPhysBar(rx + 10.0f, ry + 104.0f, rw - 20.0f, 16.0f, "ENERGY", selected->energy, Color_Create(64, 210, 224, 235));
                drawPhysBar(rx + 10.0f, ry + 124.0f, rw - 20.0f, 16.0f, "HYDRAT", selected->hydration, Color_Create(78, 162, 238, 235));
                drawPhysBar(rx + 10.0f, ry + 144.0f, rw - 20.0f, 16.0f, "FATIG ", selected->fatigue, Color_Create(236, 186, 78, 235));

                const char *reproStr = selected->gestating ? "GESTATING"
                                       : selected->mateReady ? "READY" : "WAIT";
                const char *intentStr = "UTILITY";
                const char *archStr = "LEGACY_UTILITY";
                if (ds && ds->controllerType == CREATURE_CONTROLLER_NEURAL) {
                    if (ng && ng->architecture == NEURAL_ARCH_CANDIDATE_POLICY) {
                        intentStr = NeuralV2Action_Name(ds->neuralState.v2.selectedAction);
                        archStr = "CAND_POLICY_V2 1416P";
                    } else {
                        intentStr = NeuralActionIntent_Name(ds->neuralState.selectedIntent);
                        archStr = "FIXED_RNN 32-12-10";
                    }
                }
                char mindTxt[384];
                if (ds && ds->hasMetabolism) {
                    snprintf(mindTxt, sizeof(mindTxt),
                             "INTENT  %s\n"
                             "ACTION  %s\n"
                             "TARGET  #%u  REPRO %s\n"
                             "OUTCOME %s\n"
                             "FULL %.0f%% TOX %.0f%% NUT %.0f%%\n"
                             "ARCH    %s\n"
                             "FP      %016llX",
                             intentStr, CreatureAction_Name(selected->action),
                             selected->targetId, reproStr,
                             NeuralInteractionOutcome_Name(ds->neuralState.lastOutcome),
                             ds->digestiveFullness * 100.0f, ds->toxicity * 100.0f, ds->nutrientReserve * 100.0f,
                             archStr,
                             (unsigned long long)(ng ? ng->metadata.fingerprint : 0u));
                } else {
                    snprintf(mindTxt, sizeof(mindTxt),
                             "INTENT  %s\n"
                             "ACTION  %s\n"
                             "TARGET  #%u  REPRO %s\n"
                             "OUTCOME %s\n"
                             "FB F:%.1f I:%.1f D:%.1f\n"
                             "ARCH    %s\n"
                             "FP      %016llX",
                             intentStr, CreatureAction_Name(selected->action),
                             selected->targetId, reproStr,
                             ds ? NeuralInteractionOutcome_Name(ds->neuralState.lastOutcome) : "NONE",
                             ds ? ds->neuralState.lastFeedback[0] : 0.0f,
                             ds ? ds->neuralState.lastFeedback[1] : 0.0f,
                             ds ? ds->neuralState.lastFeedback[2] : 0.0f,
                             archStr,
                             (unsigned long long)(ng ? ng->metadata.fingerprint : 0u));
                }
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 172.0f, 1.3f, Color_Create(212, 232, 240, 255), mindTxt);

                float by1 = ry + 322.0f, by2 = ry + 350.0f, bw = 92.0f, bh = 24.0f;
                btnExport = (UiButton){rx + 10.0f, by1, bw, bh, "[E]EXPORT"};
                btnImport = (UiButton){rx + 110.0f, by1, bw, bh, "[I]IMPORT"};
                btnCopy   = (UiButton){rx + 210.0f, by1, bw, bh, "[C]COPY"};
                btnPaste  = (UiButton){rx + 10.0f, by2, bw, bh, "[V]PASTE"};
                btnSwap   = (UiButton){rx + 110.0f, by2, bw, bh, "[X]SWAP"};
                btnReset  = (UiButton){rx + 210.0f, by2, bw, bh, "[B]RANDOM"};
                drawButton(&btnExport, promptMode == PROMPT_EXPORT, true);
                drawButton(&btnImport, promptMode == PROMPT_IMPORT, true);
                drawButton(&btnCopy, hasClipboard && clipboardSourceId == selected->id, false);
                drawButton(&btnPaste, hasClipboard, false);
                drawButton(&btnSwap, swapPendingId != 0, false);
                drawButton(&btnReset, false, false);

                char clipTxt[128];
                snprintf(clipTxt, sizeof(clipTxt), "CLIP: %s  SWAP: %s",
                         hasClipboard ? "LOADED" : "EMPTY",
                         swapPendingId ? "ARMED" : "NONE");
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 384.0f, 1.2f, Color_Create(146, 186, 200, 255), clipTxt);
            } else {
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 7.0f, 1.35f, Color_Create(180, 198, 206, 255), "NO LIVING CREATURES");
            }

            OpenGLRenderer_DrawRect2D(rx + 8.0f, ry + rh - 70.0f, rw - 16.0f, 62.0f,
                                      statusError ? Color_Create(56, 18, 22, 235) : Color_Create(13, 28, 36, 235),
                                      statusError ? Color_Create(236, 92, 86, 230) : Color_Create(46, 148, 166, 200));
            char wrapped[220];
            size_t wi = 0, col = 0;
            for (size_t si = 0; statusMsg[si] && wi + 2 < sizeof(wrapped); ++si) {
                if (statusMsg[si] == '\n') { wrapped[wi++] = '\n'; col = 0; continue; }
                if (col >= 38u) { wrapped[wi++] = '\n'; col = 0; }
                wrapped[wi++] = statusMsg[si];
                ++col;
            }
            wrapped[wi] = '\0';
            OpenGLRenderer_DrawText2D(rx + 14.0f, ry + rh - 62.0f, 1.15f,
                                      statusError ? Color_Create(255, 168, 160, 255) : Color_Create(120, 232, 240, 255),
                                      wrapped);

            /* 3) Inspector de Red Neuronal en vivo (N). */
            if (showNeural) {
                drawNeuralInspector(snap, &snap->selected, edgeThreshold, inspectedLayer, inspectedIndex,
                                    &btnThMinus, &btnThPlus, &btnClearSel, inPos, hidPos, outPos);
            }

            /* 3b) Inspector Digestivo y Metabólico en vivo (G). */
            if (showDigestive) {
                drawDigestiveInspector(winW, winH, showMinimap, &snap->selected);
            }

            /* 3c) Inspector de Sustancias y Composición Molecular (T). */
            if (showSubstance) {
                drawSubstanceInspector(winH, showPerf, snap, inspectedRes, &btnResPrev, &btnResNext);
            }

            /* 4) Minimapa cenital topográfico / densidad poblacional (M). */
            if (showMinimap) {
                drawMinimap(winW, winH, &envRenderer, snap, selectedEntityId, camTarget, (float)zoom);
            }

            /* 5) Modal de entrada de ruta de archivo (Export / Import). */
            if (promptMode != PROMPT_NONE) {
                float mx = (float)winW * 0.5f - 280.0f, my = (float)winH - 200.0f, mw = 560.0f, mh = 96.0f;
                OpenGLRenderer_DrawRect2D(mx, my, mw, mh, Color_Create(8, 16, 24, 248), Color_Create(78, 226, 244, 255));
                OpenGLRenderer_DrawText2D(mx + 14.0f, my + 12.0f, 1.4f, Color_Create(86, 232, 246, 255),
                                          promptMode == PROMPT_EXPORT ? "EXPORT BRAIN (*.brain.json) - ENTER TO SAVE, ESC CANCEL"
                                                                      : "IMPORT BRAIN (*.brain.json) - ENTER TO LOAD, ESC CANCEL");
                OpenGLRenderer_DrawRect2D(mx + 14.0f, my + 40.0f, mw - 28.0f, 34.0f,
                                          Color_Create(14, 26, 36, 255), Color_Create(62, 178, 198, 240));
                char pbuf[272];
                snprintf(pbuf, sizeof(pbuf), "%s_", promptPath);
                OpenGLRenderer_DrawText2D(mx + 22.0f, my + 50.0f, 1.45f, Color_Create(238, 250, 255, 255), pbuf);
            }

            /* 6) Barra inferior de controles y rendimiento. */
            OpenGLRenderer_DrawRect2D(12.0f, (float)winH - 36.0f, (float)winW - 24.0f, 26.0f,
                                      Color_Create(9, 15, 22, 220), Color_Create(38, 108, 126, 175));
            OpenGLRenderer_DrawText2D(20.0f, (float)winH - 28.0f, 1.08f, Color_Create(168, 206, 218, 255),
                                      "SPC:PAUSE 1-5:SPD P:PERF Q:QLT M:MAP N:RNN G:DIG T:SUB A:WORLD SH+D:TDBG SH+Q:WIRE R/SH+R:RST");
            double renderCpu = (OpenGLDemoWindow_Time() - start) * 1000.0;
            char fpsBuf[64];
            snprintf(fpsBuf, sizeof(fpsBuf), "FPS %2.0f  %.1fMS", displayedFps, renderCpu);
            OpenGLRenderer_DrawText2D((float)winW - 160.0f, (float)winH - 28.0f,
                                      1.25f, Color_Create(86, 226, 238, 255), fpsBuf);

            OpenGLRenderer_End2D();
        }

        double renderMs = (OpenGLDemoWindow_Time() - renderStart) * 1000.0;
        renderSum += renderMs;
        if (renderTimes) renderTimes[frame] = renderMs;

        bool captureNow = capture && frames && (captureState == CAPTURE_OVERVIEW
                                                    ? frame + 1 == frames
                                                    : frame >= 60 && captureTargetId && captureTargetReady &&
                                                          (!hasFocus || captureTargetId == focusId));
        if (captureNow) {
            for (size_t i = 0; i < packetCount; ++i) {
                if (packets[i].entityId == (captureTargetId ? captureTargetId : focusId)) {
                    printf("capture_mesh_ready=%u tier=%u body_vertices=%zu head_vertices=%zu\n",
                           packets[i].ready, packets[i].tier,
                           packets[i].bodyMesh ? packets[i].bodyMesh->vertexCount : 0,
                           packets[i].headMesh ? packets[i].headMesh->vertexCount : 0);
                    size_t components = 0, largest = 0, second = 0;
                    if (packets[i].bodyMesh && Mesh_ComponentStatistics(packets[i].bodyMesh, &components, &largest, &second))
                        printf("capture_body_components=%zu largest_triangles=%zu second_triangles=%zu\n", components, largest, second);
                    reportMeshIslands(packets[i].bodyMesh);
                    Mesh posed = Mesh_Create();
                    if (EcosystemVisual_DeformBody(&packets[i], &posed)) {
                        AABB3D bounds = AABB_Empty();
                        float sn = sinf(packets[i].yaw), cs = cosf(packets[i].yaw);
                        float surfaceY[RIG_MAX_LIMBS] = {0};
                        unsigned surfaceVertices[RIG_MAX_LIMBS] = {0};
                        for (size_t v = 0; v < posed.vertexCount; ++v) {
                            Vector3 p = Vec3_Scale(posed.vertices[v].position, packets[i].scale);
                            Vector3 worldPoint = proceduralWorld ? Quat_RotateVector(packets[i].orientation, p) :
                                Vec3_Create(p.x * cs + p.z * sn, p.y, -p.x * sn + p.z * cs);
                            worldPoint = Vec3_Add(packets[i].position, worldPoint);
                            AABB_ExpandPoint(&bounds, worldPoint);
                            for (unsigned foot = 0; proceduralWorld && foot < packets[i].supportCount; ++foot) {
                                Vector3 sole = packets[i].supportPoints[foot];
                                if (hypotf(worldPoint.x - sole.x, worldPoint.z - sole.z) >
                                    fmaxf(.055f, packets[i].scale * .42f)) continue;
                                float h;
                                if (!Terrain_SampleHeight(&snap->staticWorld->terrain, worldPoint.x, worldPoint.z, &h)) continue;
                                float gap = worldPoint.y - h;
                                surfaceY[foot] = surfaceVertices[foot]++ ? fminf(gap, surfaceY[foot]) : gap;
                            }
                        }
                        printf("capture_world_body_bounds min=%g,%g,%g max=%g,%g,%g\n",
                               bounds.start.x, bounds.start.y, bounds.start.z, bounds.end.x, bounds.end.y, bounds.end.z);
                        for (unsigned foot = 0; proceduralWorld && foot < packets[i].supportCount; ++foot) {
                            Vector3 sole = packets[i].supportPoints[foot];
                            float h;
                            if (!Terrain_SampleHeight(&snap->staticWorld->terrain, sole.x, sole.z, &h)) continue;
                            printf("capture_support tick=%llu foot=%u phase=%s sole=%.4f,%.4f,%.4f "
                                   "terrain_y=%.4f signed_sole_gap=%.4f signed_mesh_min=%.4f vertex_count=%u\n",
                                   (unsigned long long)snap->tick, foot,
                                   packets[i].stanceMask & (1u << foot) ? "stance" : "swing",
                                   sole.x, sole.y, sole.z, h, sole.y - h, surfaceY[foot], surfaceVertices[foot]);
                        }
                    }
                    Mesh_Free(&posed);
                    if (packets[i].jawJoint >= 0 && packets[i].restPose && packets[i].pose) {
                        printf("capture_jaw_scale=%g,%g,%g rest_rot=%g,%g,%g,%g pose_rot=%g,%g,%g,%g\n",
                               packets[i].jawScale.x, packets[i].jawScale.y, packets[i].jawScale.z,
                               packets[i].restPose->joints[packets[i].jawJoint].localRotation.x,
                               packets[i].restPose->joints[packets[i].jawJoint].localRotation.y,
                               packets[i].restPose->joints[packets[i].jawJoint].localRotation.z,
                               packets[i].restPose->joints[packets[i].jawJoint].localRotation.w,
                               packets[i].pose->joints[packets[i].jawJoint].localRotation.x,
                               packets[i].pose->joints[packets[i].jawJoint].localRotation.y,
                               packets[i].pose->joints[packets[i].jawJoint].localRotation.z,
                               packets[i].pose->joints[packets[i].jawJoint].localRotation.w);
                    }
                    if (!packets[i].ready || packets[i].tier != captureTier) ok = false;
                }
            }
            ensureParentDir(capture);
            captured = ok && OpenGLRenderer_SavePPM(capture, winW, winH);
            ok = captured && ok;
            if (captureShadow) {
                ensureParentDir(captureShadow);
                ok = EnvironmentRenderer_SaveShadowDepth(&envRenderer, captureShadow) && ok;
            }
            const EvolutionEntitySnapshot *subject = findSnapEntity(snap, captureTargetId ? captureTargetId : focusId);
            if (subject) {
                printf("capture_entity=%u species=%u recipe=%u action=%s stage=%u\n",
                       subject->id, subject->speciesId, subject->recipeId,
                       CreatureAction_Name(subject->action), subject->lifeStage);
            }
            if (subject && snap->staticWorld && subject->action == ACTION_DRINK) {
                const WorldStaticData *world = snap->staticWorld;
                WaterSample mouthWater = {0}, targetWater = {0};
                WorldHydrology_SampleWater(&world->hydrology, &world->terrain,
                                           subject->mouth.x, subject->mouth.z, &mouthWater);
                if (subject->hasTarget)
                    WorldHydrology_SampleWater(&world->hydrology, &world->terrain,
                                               subject->targetPos.x, subject->targetPos.z, &targetWater);
                printf("capture_tick=%llu mouth=%.4f,%.4f,%.4f mouth_body=%u mouth_surface=%.4f "
                       "target_body=%u target_surface=%.4f stance_error_max=%.5f\n",
                       (unsigned long long)snap->tick, subject->mouth.x, subject->mouth.y, subject->mouth.z,
                       (unsigned)mouthWater.waterBodyId, mouthWater.surfaceY,
                       (unsigned)targetWater.waterBodyId, targetWater.surfaceY, stanceFootErrorMax);
                WaterSample contactWater = {0};
                Vector3 point = subject->oralContactTarget;
                WorldHydrology_SampleWater(&world->hydrology, &world->terrain, point.x, point.z, &contactWater);
                printf("capture_ingestion mouth_to_contact=%.5f contact_tolerance=%.5f "
                       "contact_body=%u stable_contact_s=%.5f last_accepted_mass=%.6f "
                       "last_ingestion_s=%.6f world_water_consumed=%.7f\n",
                       Vec3_Distance(subject->mouth, point), subject->oralContactTolerance,
                       contactWater.waterBodyId, subject->oralContactTime, subject->lastIngestedMass,
                       subject->lastIngestionTime, snap->statistics.waterConsumed);
            }
            if (subject && subject->id == turningEntity) printf("capture_yaw_rate_rad_s=%g\n", observedTurnRate);
        }

        /* Guardar última telemetría autoritativa antes de liberar propiedad de lectura del snapshot. */
        finalPopulation = snap->entityCount;
        finalTick = snap->tick;
        finalController = snap->controller;
        snprintf(finalFounderMode, sizeof(finalFounderMode), "%s", snap->founderModeName);
        finalStats = snap->statistics;
        finalAchievedSpeed = snap->achievedSpeed;
        finalRequestedSpeed = snap->requestedSpeed;
        finalTps = snap->ticksPerSecond;
        finalSimMeanMs = snap->meanTickMs;
        finalSimP95Ms = snap->p95TickMs;
        finalWorkers = snap->workerCount;

        EvolutionSimulationRunner_ReleaseSnapshot(runner, snap);

        renderer.endFrame(&renderer);
        if (checkGl) {
            GLenum error;
            while ((error = glGetError()) != GL_NO_ERROR) {
                if (glErrorCount < 16) fprintf(stderr, "[OpenGL] frame=%u error=0x%x\n", frame, error);
                ++glErrorCount;
            }
        }
        finalGpu = OpenGLRenderer_GetMeshPerformanceStats(&renderer);
        if (finalGpu.gpuTimingGeneration != gpuTimingGeneration) {
            gpuTimingGeneration = (unsigned)finalGpu.gpuTimingGeneration;
            if (gpuTimingGeneration > profileWarmup + 1u) {
                gpuSum += finalGpu.gpuRenderMs;
                if (gpuTimes) gpuTimes[gpuSamples] = finalGpu.gpuRenderMs;
                ++gpuSamples;
            }
        }
        OpenGLDemoWindow_Swap(window);
        double frameTotalMs = (OpenGLDemoWindow_Time() - start) * 1000.0;
        frameRing[frameRingIdx] = frameTotalMs;
        frameRingIdx = (frameRingIdx + 1u) % ARENA_FRAME_RING;
        if (frameRingCount < ARENA_FRAME_RING) ++frameRingCount;
        if (times) times[frame] = frameTotalMs;
        if (!frames) OpenGLDemoWindow_LimitFPS(start, ARENA_MAX_FPS);
        ++frame;
        if (captured) break;
    }

    if (capture && frames && !captured && !captureWarned) {
        fprintf(stderr, "No se observó el estado solicitado para captura en %u frames.\n", frames);
        captureWarned = true;
        ok = false;
    }

    if (checkGl && glErrorCount) ok = false;
    if (profileTimes && frame > profileWarmup) {
        unsigned measured = frame - profileWarmup;
        const double *frameSample = times + profileWarmup, *renderSample = renderTimes + profileWarmup;
        double sum = 0.0, simSum = 0.0;
        for (unsigned i = profileWarmup; i < frame; ++i) { sum += times[i]; simSum += simulationTimes[i]; }
        qsort(times + profileWarmup, measured, sizeof(double), compareDouble);
        qsort(renderTimes + profileWarmup, measured, sizeof(double), compareDouble);
        if (gpuSamples) qsort(gpuTimes, gpuSamples, sizeof(double), compareDouble);
        unsigned gpuLast = gpuSamples ? gpuSamples - 1u : 0u;
        double mouthMax = 0.0;
        for (unsigned i = 0; i < mouthWindowCount; ++i) if (mouthWindow[i] > mouthMax) mouthMax = mouthWindow[i];
        struct rusage usage; getrusage(RUSAGE_SELF, &usage);
        ensureParentDir(profile);
        FILE *f = fopen(profile, "w");
        if (f) {
            EnvironmentRenderStats env = EnvironmentRenderer_GetProfile(&envRenderer);
            const WorldStaticData *world = EnvironmentRenderer_GetWorld(&envRenderer);
            fprintf(f,
                "{\"frames\":%u,\"width\":%d,\"height\":%d,\"quality\":\"%s\",\"workers\":%u,"
                "\"warmup_frames\":%u,\"initial_population\":%u,\"min_population\":%u,\"final_population\":%u,\"controller\":\"%s\",\"founder_mode\":\"%s\","
                "\"mean_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f,\"max_ms\":%.6f,"
                "\"sim_mean_ms\":%.6f,\"sim_tick_mean_ms\":%.6f,\"sim_tick_p95_ms\":%.6f,"
                "\"requested_speed\":%.3f,\"achieved_speed\":%.3f,\"ticks_per_second\":%.3f,"
                "\"adapter_mean_ms\":%.6f,\"render_mean_ms\":%.6f,"
                "\"geometry_warmup_seconds\":%.6f,\"gpu_mean_ms\":%.6f,\"process_cpu_ms\":%.6f,"
                "\"draw_calls_mean\":%.2f,\"instanced_draws_mean\":%.2f,\"instanced_count_mean\":%.2f,"
                "\"near_mean\":%.2f,\"mid_mean\":%.2f,\"far_mean\":%.2f,\"culled_mean\":%.2f,"
                "\"cache_entries\":%zu,\"cache_builds\":%zu,\"cache_hits\":%zu,\"drawn_mean\":%.3f,"
                "\"drawn_min\":%u,\"drawn_max\":%u,\"mouth_window_max_error\":%.6f,"
                "\"foot_error_max\":%.6f,\"stance_foot_error_max\":%.6f,"
                "\"skin_position_error\":%.6f,\"skin_normal_error\":%.6f,"
                "\"peak_rss_kib\":%ld,\"births\":%llu,\"deaths\":%llu,\"max_generation\":%u,\"neural_diversity\":%.6f,"
                "\"msaa\":%d,\"projection\":\"%s\",\"gl_errors\":%u,"
                "\"world_seed\":%u,\"world_generator\":%u,\"world_fingerprint\":\"%016llx\",\"world_generation_ms\":%.4f,"
                "\"env_upload_ms\":%.4f,\"env_vram_bytes\":%zu,\"env_material_bytes\":%zu,"
                "\"env_triangles\":%llu,\"env_draws\":%llu,\"terrain_lods\":[%u,%u,%u,%u],"
                "\"terrain_cpu_ms\":%.5f,\"water_cpu_ms\":%.5f,\"rocks_cpu_ms\":%.5f,"
                "\"terrain_gpu_ms\":%.5f,\"water_gpu_ms\":%.5f,\"rocks_gpu_ms\":%.5f,"
                "\"shadow_gpu_ms\":%.5f,\"shadow_cpu_ms\":%.5f,\"shadow_draws\":%zu,\"shadow_triangles\":%zu,\"shadow_vram_bytes\":%zu,\"shadow_cache_misses\":%zu,\"contact_gpu_ms\":%.5f,\"contact_cpu_ms\":%.5f,"
                "\"object_gpu_ms\":%.5f,\"object_gpu_p95_ms\":%.5f,\"decoration_gpu_ms\":%.5f,\"decoration_gpu_p95_ms\":%.5f,"
                "\"object_draws\":%zu,\"object_triangles\":%zu,\"decoration_draws\":%zu,\"decoration_triangles\":%zu,\"composition_vram_bytes\":%zu,"
                "\"terrain_gpu_p95_ms\":%.5f,\"rock_gpu_p95_ms\":%.5f,\"water_gpu_p95_ms\":%.5f,\"shadow_gpu_p95_ms\":%.5f,"
                "\"env_gpu_samples\":%zu,\"tick\":%llu,\"event_hash\":\"%016llx\","
                "\"render_p50_ms\":%.5f,\"render_p95_ms\":%.5f,\"render_p99_ms\":%.5f,"
                "\"gpu_p50_ms\":%.5f,\"gpu_p95_ms\":%.5f,\"gpu_p99_ms\":%.5f}\n",
                frame, winW, winH, qualityName(quality), finalWorkers,
                profileWarmup, profilePopulation, minPopulation, finalPopulation,
                finalController == CREATURE_CONTROLLER_NEURAL ? "neural" : "utility",
                finalFounderMode,
                sum / measured, frameSample[measured / 2], frameSample[(measured - 1) * 95 / 100],
                frameSample[(measured - 1) * 99 / 100], frameSample[measured - 1],
                simSum / measured, finalSimMeanMs, finalSimP95Ms,
                finalRequestedSpeed, finalAchievedSpeed, finalTps,
                adapterSum / frame, renderSum / frame,
                geometryWarmSeconds, gpuSamples ? gpuSum / gpuSamples : 0.0,
                1000.0 * (double)(clock() - processStart) / CLOCKS_PER_SEC / frame,
                drawCallsSum / frame, instDrawsSum / frame, instCountSum / frame,
                nearSum / frame, midSum / frame, farSum / frame, culledSum / frame,
                EcosystemVisual_CacheCount(&visual), EcosystemVisual_BuildCount(&visual),
                EcosystemVisual_CacheHitCount(&visual), drawnSum / frame,
                drawnMin == UINT32_MAX ? 0u : drawnMin, drawnMax,
                mouthMax, footErrorMax, stanceFootErrorMax, skinPositionError, skinNormalError,
                usage.ru_maxrss, (unsigned long long)finalStats.births,
                (unsigned long long)finalStats.deaths, finalStats.maximumGeneration,
                finalStats.neuralDiversity,
                actualMsaa, perspectiveFov > 0.0f ? "perspective" : "orthographic", glErrorCount,
                world ? world->masterSeed : 0u, world ? world->generatorVersion : 0u,
                (unsigned long long)(world ? world->dataFingerprint : 0u), world ? world->stats.generationMs : 0.0,
                env.lastUploadMs, env.totalVramBytes, env.materialVramBytes,
                (unsigned long long)env.totalTriangles, (unsigned long long)env.totalDrawCalls,
                env.terrain.lodCounts[0], env.terrain.lodCounts[1], env.terrain.lodCounts[2], env.terrain.lodCounts[3],
                env.terrainPass.cpuMeanMs, env.waterPass.cpuMeanMs, env.rockPass.cpuMeanMs,
                env.terrainPass.gpuMeanMs, env.waterPass.gpuMeanMs, env.rockPass.gpuMeanMs,
                env.shadowPass.gpuMeanMs, env.shadowPass.cpuMeanMs, env.shadowDraws, env.shadowTriangles, env.shadowVramBytes,
                env.shadowCacheMisses, env.contactPass.gpuMeanMs, env.contactPass.cpuMeanMs,
                env.objectPass.gpuMeanMs, env.objectPass.gpuP95Ms, env.decorationPass.gpuMeanMs, env.decorationPass.gpuP95Ms,
                env.objectDraws, env.objectTriangles, env.decorationDraws, env.decorationTriangles, env.compositionVramBytes,
                env.terrainPass.gpuP95Ms, env.rockPass.gpuP95Ms, env.waterPass.gpuP95Ms, env.shadowPass.gpuP95Ms,
                env.terrainPass.gpuSamples, (unsigned long long)finalTick,
                (unsigned long long)finalStats.eventHash,
                renderSample[measured / 2], renderSample[(measured - 1) * 95 / 100],
                renderSample[(measured - 1) * 99 / 100], gpuTimes[gpuLast / 2],
                gpuTimes[gpuLast * 95 / 100], gpuTimes[gpuLast * 99 / 100]);
            fclose(f);
        } else {
            ok = false;
        }
    }

    if (hasClipboard) NeuralGenome_Destroy(&clipboardBrain);
    lineBatchFree(&staticGrid);
    lineBatchFree(&dynamicLines);
    instanceBatchFree(&envInstances);
    instanceBatchFree(&bodyInstances);
    instanceBatchFree(&headInstances);
    for (size_t i = 0; i < dynCap; ++i) Mesh_Free(&cpuBodies[i]);
    free(packets); free(displayedPackets); free(cpuBodies); free(validatedSkin); free(validatedSkinMesh);
    free(times); free(simulationTimes); free(mouthWindow); free(profileTimes);
    for (unsigned i = 0; i < 5; ++i) Mesh_Free(&meshes[i]);
    EnvironmentRenderer_Destroy(&envRenderer);
    EcosystemVisual_Shutdown(&visual);
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    EvolutionSimulationRunner_Destroy(runner);
    return ok ? 0 : 1;
}
