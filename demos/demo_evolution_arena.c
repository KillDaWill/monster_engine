/**
 * @file demo_evolution_arena.c
 * @brief Evolution Arena visual: laboratorio interactivo sobre la misma simulación autoritativa
 *        que demo_evolution_arena_headless.
 */
#include "EvolutionArena.h"
#include "EcosystemVisual.h"
#include "CreatureRecipeRegistry.h"
#include "EcosystemAnatomy.h"
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

#define ARENA_WINDOW_W 1280
#define ARENA_WINDOW_H 800
#define ARENA_MAX_FPS 60.0
#define ARENA_HISTORY_LEN 180u

typedef enum ViewMode { VIEW_SIDE, VIEW_FRONT, VIEW_THREE_QUARTER, VIEW_OVERVIEW } ViewMode;
typedef enum CaptureState {
    CAPTURE_OVERVIEW, CAPTURE_ADULT, CAPTURE_JUVENILE, CAPTURE_NEWBORN,
    CAPTURE_WALKING, CAPTURE_DRINKING, CAPTURE_HUNTING, CAPTURE_FLEEING,
    CAPTURE_FEEDING, CAPTURE_RESTING, CAPTURE_ATTACKING, CAPTURE_TURNING,
    CAPTURE_MATING
} CaptureState;

typedef enum PromptMode { PROMPT_NONE = 0, PROMPT_EXPORT = 1, PROMPT_IMPORT = 2 } PromptMode;

typedef struct UiButton {
    float x, y, w, h;
    const char *label;
} UiButton;

static AIObjectId turningEntity = 0;
static float observedTurnRate = 0.0f;

static int compareDouble(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
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

static void drawRing3D(Vector3 p, float radius, float yOffset, Color c) {
    for (unsigned i = 0; i < 28; ++i) {
        float a = i * 6.2831853f / 28.0f, b = (i + 1) * 6.2831853f / 28.0f;
        OpenGLRenderer_DebugLine(
            Vec3_Add(p, Vec3_Create(sinf(a) * radius, yOffset, cosf(a) * radius)),
            Vec3_Add(p, Vec3_Create(sinf(b) * radius, yOffset, cosf(b) * radius)), c);
    }
}

static uint64_t packetGeneration(const EcosystemVisualDrawPacket *packet) {
    return packet->bodyMesh ? packet->bodyMesh->geometryGeneration : 1;
}

static bool projectEntity(const ICamera *camera, float orthoHeight, int width, int height,
                          const EcosystemEntity *entity, EcosystemVisualProjection *projection) {
    Vector3 forward = Vec3_Normalize(Vec3_Sub(camera->target, camera->position));
    Vector3 right = Vec3_Normalize(Vec3_Cross(forward, camera->up));
    Vector3 up = Vec3_Normalize(Vec3_Cross(right, forward));
    float radius = fmaxf(0.18f, 0.75f * entity->developmentScale * fmaxf(0.1f, entity->genome.size));
    Vector3 relative = Vec3_Sub(entity->body.position, camera->target);
    float horizontal = fabsf(Vec3_Dot(relative, right));
    float vertical = fabsf(Vec3_Dot(relative, up));
    float depth = Vec3_Dot(Vec3_Sub(entity->body.position, camera->position), forward);
    float halfHeight = orthoHeight * 0.5f, halfWidth = halfHeight * (float)width / (float)height;
    projection->visible = depth + radius >= camera->nearPlane && depth - radius <= camera->farPlane &&
                          horizontal <= halfWidth + radius && vertical <= halfHeight + radius;
    projection->projectedPixels = 2.0f * radius * (float)height / orthoHeight;
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

static bool matchesCapture(const EcosystemWorld *world, const EcosystemEntity *entity, CaptureState state) {
    if (state == CAPTURE_OVERVIEW || entity->physiology.stage == LIFE_DEAD) return false;
    switch (state) {
    case CAPTURE_ADULT:     return entity->physiology.stage != LIFE_JUVENILE;
    case CAPTURE_JUVENILE:  return entity->physiology.stage == LIFE_JUVENILE;
    case CAPTURE_NEWBORN: {
        const EcosystemSpecies *species = &world->species[entity->speciesIndex];
        return entity->physiology.stage == LIFE_JUVENILE &&
               entity->physiology.age <= species->physiology.maturityAge * 0.25;
    }
    case CAPTURE_WALKING:   return entity->body.speed > 0.05f;
    case CAPTURE_TURNING:   return entity->id == turningEntity && entity->body.speed > 0.05f && fabsf(observedTurnRate) > 0.1f;
    case CAPTURE_DRINKING:  return entity->mind.action == ACTION_DRINK && entity->body.reach.status == REACH_REACHED;
    case CAPTURE_FEEDING:   return entity->mind.action == ACTION_EAT && entity->body.reach.status == REACH_REACHED;
    case CAPTURE_ATTACKING: return entity->mind.action == ACTION_ATTACK && entity->body.reach.status == REACH_REACHED;
    case CAPTURE_RESTING:   return entity->mind.action == ACTION_REST;
    case CAPTURE_HUNTING:   return entity->mind.action == ACTION_CHASE || entity->mind.action == ACTION_ATTACK || entity->mind.goal == GOAL_HUNT;
    case CAPTURE_FLEEING:   return entity->mind.action == ACTION_ESCAPE || entity->mind.goal == GOAL_FLEE;
    case CAPTURE_MATING:    return entity->mind.action == ACTION_APPROACH_MATE || entity->reproduction.gestating;
    default:                return false;
    }
}

static const EcosystemEntity *findEntity(const EcosystemWorld *world, AIObjectId id) {
    return EcosystemWorld_GetConst(world, id);
}

static double mouthFrameError(const EcosystemWorld *world, const EcosystemVisualDrawPacket *packet, bool *hasSample) {
    const EcosystemEntity *entity = findEntity(world, packet->entityId);
    if (!entity || !packet->ready || !packet->pose || packet->headJoint < 0) return 0.0;
    *hasSample = true;
    return Vec3_Distance(packet->renderedMouth, entity->body.mouth);
}

/* ========================================================================== */
/* Laboratorio 2D: Barras, Paneles e Inspector de Red Neuronal en vivo        */
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

static void drawNeuralInspector(const EcosystemEntity *e, float edgeThreshold,
                                int inspectedLayer, int inspectedIndex,
                                UiButton *btnThMinus, UiButton *btnThPlus, UiButton *btnClearSel,
                                float inPos[NEURAL_RNN_INPUTS][2],
                                float hidPos[NEURAL_RNN_HIDDEN][2],
                                float outPos[NEURAL_RNN_OUTPUTS][2]) {
    const float px = 330.0f, py = 12.0f, pw = 616.0f, ph = 568.0f;
    OpenGLRenderer_DrawRect2D(px, py, pw, ph, Color_Create(8, 14, 20, 234), Color_Create(46, 152, 174, 220));
    OpenGLRenderer_DrawRect2D(px, py, pw, 28.0f, Color_Create(13, 28, 39, 245), Color_Create(46, 152, 174, 220));

    if (!e || e->mind.controllerType != CREATURE_CONTROLLER_NEURAL || !e->mind.neuralBrain.genome.valid) {
        OpenGLRenderer_DrawText2D(px + 12.0f, py + 8.0f, 1.4f, Color_Create(78, 216, 232, 255),
                                  "NEURAL INSPECTOR  (UTILITY CONTROLLER / NO NEURAL BRAIN)");
        return;
    }

    const NeuralGenome *g = &e->mind.neuralBrain.genome;
    const NeuralBrainRuntime *rt = &e->mind.neuralBrain.runtime;
    const NeuralControllerState *st = &e->mind.neuralState;

    char header[160];
    snprintf(header, sizeof(header), "NEURAL RNN 32-12-10  #%u  FP:%08llX  STEPS:%llu",
             e->id, (unsigned long long)(g->metadata.fingerprint & 0xFFFFFFFFu),
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

    /* Posiciones deterministas de las 3 capas. */
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

    /* Conexiones W_xh (entrada -> oculta). */
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

    /* Conexiones recurrentes W_hh (oculta_{t-1} -> oculta_t) en tono ámbar identificable. */
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
                /* Bucle autorrecurrente a la derecha del nodo oculto. */
                OpenGLRenderer_DrawCircle2D(hidPos[h][0] + 13.0f, hidPos[h][1], 7.0f,
                                            Color_Create(0, 0, 0, 0), col);
            } else {
                /* Arco lateral hacia la derecha entre neuronas ocultas. */
                float arcX = xHid + 26.0f + fabsf((float)h - (float)k) * 3.8f;
                float midY = 0.5f * (hidPos[h][1] + hidPos[k][1]);
                OpenGLRenderer_DrawLine2D(hidPos[k][0] + 8.0f, hidPos[k][1], arcX, midY, lw, col);
                OpenGLRenderer_DrawLine2D(arcX, midY, hidPos[h][0] + 8.0f, hidPos[h][1], lw, col);
            }
        }
    }

    /* Conexiones W_hy (oculta -> salida). */
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

    /* Nodos de entrada (32). */
    for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) {
        float v = rt->inputs[i];
        bool sel = (inspectedLayer == 0 && inspectedIndex == (int)i);
        Color border = sel ? Color_Create(255, 240, 120, 255)
                           : (v >= 0.0f ? Color_Create(78, 212, 226, 220) : Color_Create(232, 102, 92, 220));
        OpenGLRenderer_DrawCircle2D(inPos[i][0], inPos[i][1], sel ? 6.0f : 4.5f, activationFill(v), border);
        char lbl[32];
        snprintf(lbl, sizeof(lbl), "%-9s", NeuralSensor_Name(i));
        OpenGLRenderer_DrawText2D(px + 8.0f, inPos[i][1] - 3.5f, 1.05f,
                                  sel ? Color_Create(255, 245, 160, 255) : Color_Create(168, 198, 210, 235), lbl);
    }

    /* Nodos ocultos recurrentes (12). */
    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        float v = rt->hidden[h];
        bool sel = (inspectedLayer == 1 && inspectedIndex == (int)h);
        Color border = sel ? Color_Create(255, 240, 120, 255)
                           : (v >= 0.0f ? Color_Create(80, 226, 240, 240) : Color_Create(238, 102, 94, 240));
        OpenGLRenderer_DrawCircle2D(hidPos[h][0], hidPos[h][1], sel ? 10.5f : 8.5f, activationFill(v), border);
        char hbuf[16];
        snprintf(hbuf, sizeof(hbuf), "H%u", h);
        OpenGLRenderer_DrawText2D(hidPos[h][0] - 8.0f, hidPos[h][1] - 4.0f, 1.05f,
                                  Color_Create(240, 250, 255, 255), hbuf);
    }

    /* Nodos de salida (10) con barras de preferencia y ganador destacado. */
    float minOut = rt->outputs[0], maxOut = rt->outputs[0];
    for (unsigned o = 1; o < NEURAL_RNN_OUTPUTS; ++o) {
        if (rt->outputs[o] < minOut) minOut = rt->outputs[o];
        if (rt->outputs[o] > maxOut) maxOut = rt->outputs[o];
    }
    float span = fmaxf(0.5f, maxOut - minOut);
    for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
        float v = rt->outputs[o];
        bool winner = (st->selectedIntent == (NeuralActionIntent)o);
        bool sel = (inspectedLayer == 2 && inspectedIndex == (int)o);
        float norm = fmaxf(0.0f, fminf(1.0f, (v - minOut) / span));
        Color border = sel ? Color_Create(255, 240, 120, 255)
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

    /* Leyenda inferior e inspección detallada de neurona seleccionada. */
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

static void printHelp(void) {
    puts("Uso: ./demos/demo_evolution_arena [OPCIONES]\n"
         "  --seed=N  --arena-size=N  --brumacernos=N (--herbivores=N)  --velkares=N (--carnivores=N)\n"
         "  --plants=N  --ponds=N  --obstacles=N  --capacity=N  --speed=1|2|4|16|64\n"
         "  --controller=neural|utility  --founders=random|seeded|imported\n"
         "  --brain=PATH  --brumacerno-brain=PATH  --velkar-brain=PATH\n"
         "  --mutation-rate=N  --mutation-sigma=N  --replace-rate=N  --crossover=neuron|uniform|mutation-only\n"
         "  --no-physical-mutation  --scenario=reference|A|B|C|D|E  --recipes-dir=DIR\n"
         "  --show-neural  --import-brain=PATH  --export-brain=PATH  --swap-brains=A:B\n"
         "  --frames=N  --capture=IMG.ppm  --profile=METRICS.json  --view=side|front|three-quarter|overview\n"
         "  --focus=ID  --capture-species=1|2  --capture-state=overview|adult|juvenile|newborn|walking|turning|drinking|feeding|hunting|attacking|fleeing|resting|mating\n"
         "  --zoom=N  --no-hud  --initial-age=S  --inspect=body|head|feet|neck  --validate-mouth=N  --validate-skinning  --cpu-skinning  --focus-lod=near|mid|far  --lod-cycle");
}

int main(int argc, char **argv) {
    EvolutionArenaConfig config = EvolutionArenaConfig_Default();
    const char *recipesDirectory = "recipes";
    unsigned frames = 0, validateMouthFrames = 0;
    const char *capture = NULL, *profile = NULL;
    const char *cliImportBrain = NULL, *cliExportBrain = NULL;
    AIObjectId cliSwapA = 0, cliSwapB = 0;
    double initialSpeed = 1.0, initialZoom = 26.0, initialAge = -1.0;
    bool showHud = true, showNeural = false, captured = false, validateSkin = false, forceCpu = false;
    ViewMode view = VIEW_THREE_QUARTER;
    CaptureState captureState = CAPTURE_OVERVIEW;
    AIObjectId focusId = 0;
    unsigned captureSpecies = 0;
    bool hasFocus = false;
    const char *inspect = NULL;
    VisualTier captureTier = VISUAL_TIER_NEAR;
    bool lodCycle = false;

    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--scenario=", 11)) {
            if (!EvolutionArenaConfig_ApplyLegacyScenario(&config, argv[i] + 11)) return 2;
        }
    }

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--help")) { printHelp(); return 0; }
        else if (!strncmp(a, "--scenario=", 11)) continue;
        else if (!strncmp(a, "--seed=", 7)) config.seed = (uint32_t)strtoul(a + 7, NULL, 10);
        else if (!strncmp(a, "--arena-size=", 13)) config.arenaSize = strtof(a + 13, NULL);
        else if (!strncmp(a, "--brumacernos=", 14)) config.brumacernos = (unsigned)strtoul(a + 14, NULL, 10);
        else if (!strncmp(a, "--herbivores=", 13)) config.brumacernos = (unsigned)strtoul(a + 13, NULL, 10);
        else if (!strncmp(a, "--velkares=", 11)) config.velkares = (unsigned)strtoul(a + 11, NULL, 10);
        else if (!strncmp(a, "--carnivores=", 13)) config.velkares = (unsigned)strtoul(a + 13, NULL, 10);
        else if (!strncmp(a, "--plants=", 9)) config.plants = (unsigned)strtoul(a + 9, NULL, 10);
        else if (!strncmp(a, "--ponds=", 8)) config.ponds = (unsigned)strtoul(a + 8, NULL, 10);
        else if (!strncmp(a, "--obstacles=", 12)) config.obstacles = (unsigned)strtoul(a + 12, NULL, 10);
        else if (!strncmp(a, "--capacity=", 11)) config.capacity = (unsigned)strtoul(a + 11, NULL, 10);
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
        else if (!strncmp(a, "--speed=", 8)) initialSpeed = strtod(a + 8, NULL);
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
        else if (!strncmp(a, "--zoom=", 7)) initialZoom = strtod(a + 7, NULL);
        else if (!strncmp(a, "--capture=", 10)) capture = a + 10;
        else if (!strncmp(a, "--profile=", 10)) profile = a + 10;
        else if (!strncmp(a, "--view=", 7)) { if (!parseView(a + 7, &view)) return 2; }
        else if (!strncmp(a, "--focus=", 8)) { focusId = (AIObjectId)strtoul(a + 8, NULL, 10); hasFocus = true; }
        else if (!strncmp(a, "--capture-species=", 18)) captureSpecies = (unsigned)strtoul(a + 18, NULL, 10);
        else if (!strncmp(a, "--capture-state=", 16)) { if (!parseCaptureState(a + 16, &captureState)) return 2; }
        else if (!strncmp(a, "--validate-mouth=", 17)) validateMouthFrames = (unsigned)strtoul(a + 17, NULL, 10);
        else return 2;
    }

    if (!isfinite(initialAge) || initialAge > 10000 || frames > 1000000 ||
        !isfinite(initialZoom) || initialZoom < 0.4 || initialZoom > 110.0 ||
        (initialSpeed != 1 && initialSpeed != 2 && initialSpeed != 4 && initialSpeed != 16 && initialSpeed != 64))
        return 2;

    char errBuf[256] = {0};
    if (!CreatureRecipeRegistry_LoadDirectory(recipesDirectory, errBuf, sizeof(errBuf))) {
        fprintf(stderr, "%s\n", errBuf);
        return 1;
    }

    EcosystemWorld w;
    if (!EvolutionArena_Create(&w, &config, errBuf, sizeof(errBuf))) {
        fprintf(stderr, "Error al iniciar Evolution Arena: %s\n", errBuf);
        return 1;
    }
    if (initialAge >= 0.0) {
        for (unsigned i = 0; i < w.capacity; ++i)
            if (w.entities[i].id) w.entities[i].physiology.age = initialAge;
    }
    w.clock.speed = initialSpeed;

    /* Acciones CLI reproducibles de gestión de cerebros (para validación visual/automatizada). */
    char statusMsg[160] = "READY";
    bool statusError = false;
    AIObjectId initialTargetId = hasFocus ? focusId : 0u;
    if (!initialTargetId) {
        for (unsigned i = 0; i < w.capacity; ++i) {
            if (w.entities[i].id && (!captureSpecies || w.entities[i].body.species == captureSpecies)) {
                initialTargetId = w.entities[i].id;
                break;
            }
        }
    }
    if (cliImportBrain && initialTargetId) {
        if (EcosystemWorld_ImportBrain(&w, initialTargetId, cliImportBrain, errBuf, sizeof(errBuf))) {
            snprintf(statusMsg, sizeof(statusMsg), "IMPORTED BRAIN INTO #%u", initialTargetId);
        } else {
            snprintf(statusMsg, sizeof(statusMsg), "IMPORT ERROR: %s", errBuf);
            statusError = true;
        }
    }
    if (cliSwapA && cliSwapB) {
        if (EcosystemWorld_SwapBrains(&w, cliSwapA, cliSwapB)) {
            snprintf(statusMsg, sizeof(statusMsg), "SWAPPED BRAINS #%u <-> #%u", cliSwapA, cliSwapB);
        } else {
            snprintf(statusMsg, sizeof(statusMsg), "SWAP FAILED #%u <-> #%u", cliSwapA, cliSwapB);
            statusError = true;
        }
    }
    if (cliExportBrain && initialTargetId) {
        ensureParentDir(cliExportBrain);
        if (EcosystemWorld_ExportBrain(&w, initialTargetId, cliExportBrain, true, errBuf, sizeof(errBuf))) {
            snprintf(statusMsg, sizeof(statusMsg), "EXPORTED #%u -> %s", initialTargetId, cliExportBrain);
        } else {
            snprintf(statusMsg, sizeof(statusMsg), "EXPORT ERROR: %s", errBuf);
            statusError = true;
        }
    }

    OpenGLDemoWindow *window = OpenGLDemoWindow_Create("Monster Engine - Evolution Arena", ARENA_WINDOW_W, ARENA_WINDOW_H);
    if (!window) { EcosystemWorld_Free(&w); return 1; }

    EcosystemVisual visual = {0};
    if (!EcosystemVisual_Init(&visual, &w)) {
        fprintf(stderr, "[ADVERTENCIA] No se pudo inicializar EcosystemVisual.\n");
        OpenGLDemoWindow_Free(window); EcosystemWorld_Free(&w); return 1;
    }

    ICamera camera = {.position = {28, 28, 32}, .target = {0, 0, 0}, .up = {0, 1, 0},
                      .fov = 50, .aspectRatio = (float)ARENA_WINDOW_W / (float)ARENA_WINDOW_H,
                      .nearPlane = 0.1f, .farPlane = 240.0f};
    Renderer3D renderer = OpenGLRenderer_Create(&camera);

    /* Mallas de entorno: suelo, estanque, planta, cadáver y obstáculo monolito. */
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
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[2], (Transform3D){.position = {0, 0.25f, 0}, .rotation = {0, 0, 0}, .scale = {0.28f, 0.24f, 0.48f}}, 12, 6, decorColors[2]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[3], (Transform3D){.position = {0, 0.22f, 0}, .rotation = {0, 0, 0}, .scale = {0.30f, 0.22f, 0.48f}}, 12, 6, decorColors[3]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[4], (Transform3D){.position = {0, 0.65f, 0}, .rotation = {0, 0, 0}, .scale = {1.0f, 0.95f, 1.0f}}, 14, 8, decorColors[4]) && ok;
    if (!ok) {
        for (unsigned i = 0; i < 5; ++i) Mesh_Free(&meshes[i]);
        EcosystemVisual_Shutdown(&visual); OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window); EcosystemWorld_Free(&w); return 1;
    }

    double *times = frames ? calloc(frames, sizeof(double)) : NULL;
    double *simulationTimes = frames ? calloc(frames, sizeof(double)) : NULL;
    double *mouthWindow = validateMouthFrames ? calloc(validateMouthFrames, sizeof(double)) : NULL;
    if ((frames && (!times || !simulationTimes)) || (validateMouthFrames && !mouthWindow)) {
        free(times); free(simulationTimes); free(mouthWindow);
        for (unsigned i = 0; i < 5; ++i) Mesh_Free(&meshes[i]);
        EcosystemVisual_Shutdown(&visual); OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window); EcosystemWorld_Free(&w); return 1;
    }

    double last = OpenGLDemoWindow_Time(), angle = 0.65, zoom = initialZoom;
    double gpuSum = 0.0, displayedFps = ARENA_MAX_FPS, effectiveSpeed = initialSpeed;
    unsigned gpuSamples = 0, gpuTimingGeneration = 0;
    OpenGLMeshPerformanceStats finalGpu = {0};
    float footErrorMax = 0.0f, stanceFootErrorMax = 0.0f, skinPositionError = 0.0f, skinNormalError = 0.0f;
    unsigned initialPopulation = w.count, drawnMin = ECOSYSTEM_VISUAL_MAX_SLOTS, drawnMax = 0;
    double drawnSum = 0.0, adapterSum = 0.0, renderSum = 0.0, geometryWarmSeconds = 0.0;
    AIObjectId validatedSkin[ECOSYSTEM_VISUAL_MAX_SLOTS] = {0};
    uint64_t validatedSkinMesh[ECOSYSTEM_VISUAL_MAX_SLOTS] = {0};
    Mesh cpuBodies[ECOSYSTEM_VISUAL_MAX_SLOTS] = {0};

    unsigned frame = 0, selectedSlot = 0;
    bool debug = false, follow = hasFocus, overheadToggle = (view == VIEW_OVERVIEW);
    Vector3 cameraCenter = Vec3_Zero();

    float histBrum[ARENA_HISTORY_LEN] = {0}, histVelk[ARENA_HISTORY_LEN] = {0};
    unsigned historyCount = 0, historyIndex = 0;
    double nextSample = 0.0;

    if (hasFocus) {
        for (unsigned i = 0; i < w.capacity; ++i) if (w.entities[i].id == focusId) { selectedSlot = i; break; }
    } else if (captureSpecies) {
        for (unsigned i = 0; i < w.capacity; ++i)
            if (w.entities[i].id && w.entities[i].body.species == captureSpecies) { selectedSlot = i; break; }
    }

    const double speeds[] = {1.0, 4.0, 16.0, 64.0};
    unsigned speedIndex = initialSpeed >= 64.0 ? 3u : initialSpeed >= 16.0 ? 2u : initialSpeed >= 4.0 ? 1u : 0u;
    AIObjectId selectedRequestId = 0, captureTargetId = 0;
    bool focusWarned = false, captureWarned = false, updateWarned = false, projectionWarned = false;
    unsigned mouthWindowCount = 0, mouthWindowIndex = 0;
    bool scaleProofPrinted[5] = {false};

    /* Estado de gestión de cerebros en el laboratorio. */
    bool hasClipboard = false;
    NeuralGenome clipboardBrain = {0};
    AIObjectId clipboardSourceId = 0, swapPendingId = 0;
    PromptMode promptMode = PROMPT_NONE;
    bool promptSavedPause = false;
    bool promptOverwrite = false;
    char promptPath[256] = {0};

    /* Estado del inspector de red neuronal. */
    float edgeThreshold = 0.35f;
    int inspectedLayer = -1, inspectedIndex = -1;
    UiButton btnExport = {0}, btnImport = {0}, btnCopy = {0}, btnPaste = {0}, btnSwap = {0}, btnReset = {0};
    UiButton btnThMinus = {0}, btnThPlus = {0}, btnClearSel = {0};
    float inPos[NEURAL_RNN_INPUTS][2] = {{0}};
    float hidPos[NEURAL_RNN_HIDDEN][2] = {{0}};
    float outPos[NEURAL_RNN_OUTPUTS][2] = {{0}};

    clock_t processStart = clock();
    EcosystemVisualDrawPacket displayedPackets[ECOSYSTEM_VISUAL_MAX_SLOTS];
    size_t displayedCount = 0;

    while (!frames || frame < frames) {
        double start = OpenGLDemoWindow_Time(), frameSeconds = start - last;
        last = start;
        if (frame > 0 && frameSeconds > 0.0005 && frameSeconds < 1.0) {
            displayedFps += (1.0 / frameSeconds - displayedFps) * 0.1;
        }
        double dt = frames ? (1.0 / 60.0) : fmin(frameSeconds, 0.1);
        OpenGLDemoInput input = OpenGLDemoWindow_Poll(window);

        /* Modo de entrada de ruta para exportar/importar cerebro. */
        if (promptMode != PROMPT_NONE) {
            if (input.quit) {
                promptMode = PROMPT_NONE;
                w.clock.paused = promptSavedPause;
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
                    EcosystemEntity *sel = w.entities[selectedSlot].id ? &w.entities[selectedSlot] : NULL;
                    if (!sel) {
                        snprintf(statusMsg, sizeof(statusMsg), "NO CREATURE SELECTED");
                        statusError = true;
                    } else if (promptMode == PROMPT_EXPORT) {
                        ensureParentDir(promptPath);
                        if (EcosystemWorld_ExportBrain(&w, sel->id, promptPath, promptOverwrite, errBuf, sizeof(errBuf))) {
                            snprintf(statusMsg, sizeof(statusMsg), "EXPORTED #%u -> %s", sel->id, promptPath);
                            statusError = false;
                            promptMode = PROMPT_NONE;
                            w.clock.paused = promptSavedPause;
                            promptOverwrite = false;
                        } else {
                            /* Permitir segunda confirmación explícita si el archivo ya existe. */
                            if (strstr(errBuf, "ya existe")) {
                                promptOverwrite = true;
                                snprintf(statusMsg, sizeof(statusMsg), "FILE EXISTS! PRESS ENTER AGAIN TO OVERWRITE");
                            } else {
                                snprintf(statusMsg, sizeof(statusMsg), "EXPORT ERR: %.120s", errBuf);
                            }
                            statusError = true;
                        }
                    } else if (promptMode == PROMPT_IMPORT) {
                        if (EcosystemWorld_ImportBrain(&w, sel->id, promptPath, errBuf, sizeof(errBuf))) {
                            snprintf(statusMsg, sizeof(statusMsg), "IMPORTED BRAIN INTO #%u", sel->id);
                            statusError = false;
                            promptMode = PROMPT_NONE;
                            w.clock.paused = promptSavedPause;
                        } else {
                            snprintf(statusMsg, sizeof(statusMsg), "IMPORT ERR: %.120s", errBuf);
                            statusError = true;
                        }
                    }
                }
            }
            /* Desactivar atajos mientras se escribe una ruta. */
            input.togglePause = input.toggleDebug = input.keyR = input.keyF = input.keyN = false;
            input.keyE = input.keyI = input.keyC = input.keyV = input.keyX = input.keyB = input.keyO = false;
            input.digitKey = -1;
            input.surfaceDebugNext = false;
            input.panForward = input.panRight = 0;
        }

        if (input.quit) break;

        /* Gestión de clics en interfaz 2D antes de selección 3D. */
        bool uiConsumedClick = false;
        EcosystemEntity *selectedNow = w.entities[selectedSlot].id ? &w.entities[selectedSlot] : NULL;
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
            } else if (showNeural && hitRect(input.mouseX, input.mouseY, 330.0f, 12.0f, 616.0f, 568.0f)) {
                uiConsumedClick = true;
                if (hitRect(input.mouseX, input.mouseY, btnThMinus.x, btnThMinus.y, btnThMinus.w, btnThMinus.h)) {
                    edgeThreshold = fmaxf(0.05f, edgeThreshold - 0.10f);
                } else if (hitRect(input.mouseX, input.mouseY, btnThPlus.x, btnThPlus.y, btnThPlus.w, btnThPlus.h)) {
                    edgeThreshold = fminf(2.50f, edgeThreshold + 0.10f);
                } else if (hitRect(input.mouseX, input.mouseY, btnClearSel.x, btnClearSel.y, btnClearSel.w, btnClearSel.h)) {
                    inspectedLayer = -1; inspectedIndex = -1;
                } else {
                    /* Seleccionar neurona más cercana al clic dentro del inspector. */
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

        /* Selección 3D por rayo sobre las criaturas dibujadas. */
        if (input.mouseLeftPressed && !uiConsumedClick && displayedCount) {
            Vector3 forward = Vec3_Normalize(Vec3_Sub(camera.target, camera.position));
            Vector3 right = Vec3_Normalize(Vec3_Cross(forward, camera.up));
            Vector3 up = Vec3_Normalize(Vec3_Cross(right, forward));
            float x = ((input.mouseX + 0.5f) / (float)ARENA_WINDOW_W - 0.5f) * (float)zoom * camera.aspectRatio;
            float y = (0.5f - (input.mouseY + 0.5f) / (float)ARENA_WINDOW_H) * (float)zoom;
            Vector3 origin = Vec3_Add(camera.position, Vec3_Add(Vec3_Scale(right, x), Vec3_Scale(up, y)));
            AIObjectId picked = EcosystemVisual_PickRay(displayedPackets, displayedCount, origin, forward,
                                                        camera.nearPlane, camera.farPlane);
            if (picked) {
                for (unsigned i = 0; i < w.capacity; ++i) {
                    if (w.entities[i].id == picked) {
                        selectedSlot = i; hasFocus = false; focusId = 0; follow = true;
                        selectedNow = &w.entities[i];
                        printf("mouse_focus_entity=%u\n", picked);
                        break;
                    }
                }
            }
        }

        /* Atajos de simulación y laboratorio. */
        if (input.togglePause) w.clock.paused = !w.clock.paused;
        if (input.toggleDebug) debug = !debug;
        if (input.keyN) showNeural = !showNeural;
        if (input.keyO) { overheadToggle = !overheadToggle; view = overheadToggle ? VIEW_OVERVIEW : VIEW_THREE_QUARTER; }
        if (input.furLengthAdjust < 0) edgeThreshold = fmaxf(0.05f, edgeThreshold - 0.10f);
        if (input.furLengthAdjust > 0) edgeThreshold = fminf(2.50f, edgeThreshold + 0.10f);
        if (input.digitKey >= 0 && input.digitKey < 4) {
            speedIndex = (unsigned)input.digitKey;
            w.clock.speed = speeds[speedIndex];
        }
        if (input.surfaceSelect) angle += input.surfaceSelect * 0.12;
        if (input.mouseRightHeld) angle += input.mouseDeltaX * 0.008;
        if (input.surfaceAdjust) zoom = fmax(2.0, fmin(100.0, zoom - input.surfaceAdjust * fmax(0.3, zoom * 0.12)));
        if (input.mouseWheel != 0) zoom = fmax(2.0, fmin(100.0, zoom - input.mouseWheel * fmax(0.5, zoom * 0.10)));
        if (input.surfaceDebugNext) {
            hasFocus = false; focusId = 0;
            for (unsigned i = 0; i < w.capacity; ++i) {
                selectedSlot = (selectedSlot + 1) % w.capacity;
                if (w.entities[selectedSlot].id) break;
            }
            selectedNow = w.entities[selectedSlot].id ? &w.entities[selectedSlot] : NULL;
        }
        if (input.keyF) { follow = !follow; hasFocus = false; focusId = 0; }

        /* Acciones seguras de gestión de cerebros (entre ticks o en pausa). */
        if (input.keyE && selectedNow) {
            promptMode = PROMPT_EXPORT;
            promptSavedPause = w.clock.paused;
            promptOverwrite = false;
            w.clock.paused = true;
            snprintf(promptPath, sizeof(promptPath), "build/brains/creature_%u.brain.json", selectedNow->id);
            snprintf(statusMsg, sizeof(statusMsg), "TYPE EXPORT PATH + ENTER (ESC CANCEL)");
            statusError = false;
        }
        if (input.keyI && selectedNow) {
            promptMode = PROMPT_IMPORT;
            promptSavedPause = w.clock.paused;
            w.clock.paused = true;
            snprintf(promptPath, sizeof(promptPath), "brains/seeded_baseline.brain.json");
            snprintf(statusMsg, sizeof(statusMsg), "TYPE IMPORT PATH + ENTER (ESC CANCEL)");
            statusError = false;
        }
        if (input.keyC && selectedNow) {
            const NeuralGenome *g = CreatureMind_GetNeuralGenome(&selectedNow->mind);
            if (g && NeuralGenome_Clone(&clipboardBrain, g)) {
                hasClipboard = true;
                clipboardSourceId = selectedNow->id;
                snprintf(statusMsg, sizeof(statusMsg), "COPIED BRAIN #%u (%08llX)",
                         selectedNow->id, (unsigned long long)(g->metadata.fingerprint & 0xFFFFFFFFu));
                statusError = false;
            } else {
                snprintf(statusMsg, sizeof(statusMsg), "COPY FAILED: NO NEURAL BRAIN");
                statusError = true;
            }
        }
        if (input.keyV && selectedNow) {
            if (!hasClipboard) {
                snprintf(statusMsg, sizeof(statusMsg), "CLIPBOARD EMPTY (PRESS C TO COPY)");
                statusError = true;
            } else if (EcosystemWorld_AssignBrain(&w, selectedNow->id, &clipboardBrain, "paste")) {
                snprintf(statusMsg, sizeof(statusMsg), "PASTED BRAIN FROM #%u INTO #%u",
                         clipboardSourceId, selectedNow->id);
                statusError = false;
            } else {
                snprintf(statusMsg, sizeof(statusMsg), "PASTE FAILED");
                statusError = true;
            }
        }
        if (input.keyX && selectedNow) {
            if (swapPendingId == 0 || swapPendingId == selectedNow->id || !EcosystemWorld_Get(&w, swapPendingId)) {
                swapPendingId = selectedNow->id;
                snprintf(statusMsg, sizeof(statusMsg), "SWAP ARMED: #%u -> SELECT 2ND + PRESS X", swapPendingId);
                statusError = false;
            } else {
                AIObjectId a = swapPendingId, b = selectedNow->id;
                if (EcosystemWorld_SwapBrains(&w, a, b)) {
                    snprintf(statusMsg, sizeof(statusMsg), "SWAPPED BRAINS #%u <-> #%u", a, b);
                    statusError = false;
                } else {
                    snprintf(statusMsg, sizeof(statusMsg), "SWAP FAILED #%u <-> #%u", a, b);
                    statusError = true;
                }
                swapPendingId = 0;
            }
        }
        if (input.keyB && selectedNow) {
            if (EcosystemWorld_ResetBrainRandom(&w, selectedNow->id)) {
                snprintf(statusMsg, sizeof(statusMsg), "RESET #%u TO NEW RANDOM BRAIN", selectedNow->id);
                statusError = false;
            } else {
                snprintf(statusMsg, sizeof(statusMsg), "RANDOM RESET FAILED");
                statusError = true;
            }
        }
        if (input.keyR) {
            EcosystemWorld_Free(&w);
            if (!EvolutionArena_Create(&w, &config, errBuf, sizeof(errBuf))) { ok = false; break; }
            w.clock.speed = speeds[speedIndex];
            nextSample = 0.0; historyCount = historyIndex = 0;
            selectedSlot = 0; selectedRequestId = 0; captureTargetId = 0;
            displayedCount = 0; follow = false; swapPendingId = 0;
            cameraCenter = Vec3_Zero();
            snprintf(statusMsg, sizeof(statusMsg), "EXPERIMENT RESET (SEED %u)", config.seed);
            statusError = false;
            if (!EcosystemVisual_BindWorld(&visual, &w))
                fprintf(stderr, "[ADVERTENCIA] Fallo al volver a enlazar EcosystemVisual.\n");
        }

        /* Avance autoritativo de la simulación con pasos fijos. */
        AIObjectId observedId = hasFocus ? focusId : w.entities[selectedSlot].id;
        const EcosystemEntity *beforeTurn = findEntity(&w, observedId);
        float previousYaw = beforeTurn ? beforeTurn->body.yaw : 0.0f;
        double simStart = OpenGLDemoWindow_Time();
        unsigned stepped = EcosystemWorld_Advance(&w, dt);
        double simMs = (OpenGLDemoWindow_Time() - simStart) * 1000.0;
        if (simulationTimes) simulationTimes[frame] = simMs;
        if (!w.clock.paused && dt > 1e-6) {
            double instSpeed = ((double)stepped * w.clock.fixedStep) / dt;
            effectiveSpeed += (instSpeed - effectiveSpeed) * 0.15;
        } else if (w.clock.paused) {
            effectiveSpeed = 0.0;
        }

        const EcosystemEntity *afterTurn = findEntity(&w, observedId);
        turningEntity = afterTurn ? observedId : 0;
        float deltaYaw = afterTurn ? afterTurn->body.yaw - previousYaw : 0.0f;
        observedTurnRate = dt > 1e-6 ? atan2f(sinf(deltaYaw), cosf(deltaYaw)) / (float)dt : 0.0f;

        if (w.clock.seconds >= nextSample) {
            unsigned bCount = 0, vCount = 0;
            for (unsigned i = 0; i < w.capacity; ++i) {
                if (!w.entities[i].id) continue;
                if (w.entities[i].speciesIndex == 0) ++bCount;
                else ++vCount;
            }
            histBrum[historyIndex] = (float)bCount;
            histVelk[historyIndex] = (float)vCount;
            historyIndex = (historyIndex + 1u) % ARENA_HISTORY_LEN;
            if (historyCount < ARENA_HISTORY_LEN) ++historyCount;
            nextSample = w.clock.seconds + config.day / 12.0;
        }

        /* Desplazamiento de cámara (pan por teclado W/S/J/L o botón central). */
        float panScale = (float)dt * fmaxf(8.0f, (float)zoom * 0.55f);
        cameraCenter.x += (float)input.panRight * panScale;
        cameraCenter.z -= (float)input.panForward * panScale;
        if (input.mouseMiddleHeld) {
            cameraCenter.x -= input.mouseDeltaX * (float)zoom * 0.0014f;
            cameraCenter.z -= input.mouseDeltaY * (float)zoom * 0.0014f;
            follow = false;
        }

        if (!w.entities[selectedSlot].id) {
            for (unsigned i = 0; i < w.capacity; ++i) if (w.entities[i].id) { selectedSlot = i; break; }
        }
        EcosystemEntity *selected = w.entities[selectedSlot].id ? &w.entities[selectedSlot] : NULL;
        const EcosystemEntity *focus = findEntity(&w, focusId);
        if (hasFocus && !focus && !focusWarned) {
            fprintf(stderr, "[ADVERTENCIA] --focus=%u no corresponde a una entidad viva.\n", focusId);
            focusWarned = true;
        }
        if (lodCycle) {
            const VisualTier cycle[] = {VISUAL_TIER_NEAR, VISUAL_TIER_MID, VISUAL_TIER_FAR, VISUAL_TIER_NEAR};
            captureTier = cycle[(frame / 180) % 4];
        }
        if (captureTargetId) {
            const EcosystemEntity *current = findEntity(&w, captureTargetId);
            if (!current || !matchesCapture(&w, current, captureState)) captureTargetId = 0;
        }
        if (captureState != CAPTURE_OVERVIEW && !captureTargetId) {
            for (unsigned i = 0; i < w.capacity; ++i) {
                if (w.entities[i].id &&
                    (!captureSpecies || w.entities[i].body.species == captureSpecies) &&
                    (!hasFocus || w.entities[i].id == focusId) &&
                    matchesCapture(&w, &w.entities[i], captureState)) {
                    captureTargetId = w.entities[i].id;
                    selectedSlot = i;
                    selected = &w.entities[i];
                    break;
                }
            }
        }

        if (selectedRequestId && (!selected || selectedRequestId != selected->id))
            EcosystemVisual_ClearTierRequest(&visual, selectedRequestId);
        selectedRequestId = selected ? selected->id : 0;
        if (selected) EcosystemVisual_RequestTier(&visual, selected->id, captureTier);
        if (focus) EcosystemVisual_RequestTier(&visual, focus->id, captureTier);
        if (captureTargetId) EcosystemVisual_RequestTier(&visual, captureTargetId, captureTier);

        Vector3 camTarget = cameraCenter;
        if (focus) camTarget = focus->body.position;
        else if (captureTargetId) {
            const EcosystemEntity *ct = findEntity(&w, captureTargetId);
            if (ct) camTarget = ct->body.position;
        } else if (follow && selected) camTarget = selected->body.position;
        cameraCenter = camTarget;

        const EcosystemEntity *cameraSubject = focus ? focus : findEntity(&w, captureTargetId);
        double cameraAngle = cameraSubject ? cameraSubject->body.yaw + (view == VIEW_THREE_QUARTER ? 0.65 : 0.0) : angle;
        angle = cameraAngle;
        if (inspect && cameraSubject && cameraSubject->anatomicalBody) {
            const EcosystemAnatomy *a = cameraSubject->anatomicalBody;
            Vector3 local = Vec3_Create(0, a->standingHeight * 0.52f, -a->phenotype.axial.trunkLength * 0.42f);
            if (!strcmp(inspect, "head")) {
                local = Vec3_Lerp(a->pose.joints[a->rig.headJoint].worldPosition,
                    Vec3_Add(a->center, Vec3_Scale(Vec3_Create(
                        (cameraSubject->body.mouth.x - cameraSubject->body.position.x) * cosf(cameraSubject->body.yaw) -
                        (cameraSubject->body.mouth.z - cameraSubject->body.position.z) * sinf(cameraSubject->body.yaw),
                        cameraSubject->body.mouth.y - cameraSubject->body.position.y,
                        (cameraSubject->body.mouth.x - cameraSubject->body.position.x) * sinf(cameraSubject->body.yaw) +
                        (cameraSubject->body.mouth.z - cameraSubject->body.position.z) * cosf(cameraSubject->body.yaw)),
                        1.0f / EcosystemAnatomy_Scale(a, cameraSubject))), 0.5f);
            } else if (!strcmp(inspect, "feet")) local = Vec3_Create(0, 0.25f, 0);
            else if (!strcmp(inspect, "neck")) local = Vec3_Create(0, a->standingHeight * 0.75f, 0);
            local = Vec3_Scale(Vec3_Sub(local, a->center), EcosystemAnatomy_Scale(a, cameraSubject));
            float sn = sinf(cameraSubject->body.yaw), cs = cosf(cameraSubject->body.yaw);
            camTarget = Vec3_Add(cameraSubject->body.position, Vec3_Create(local.x * cs + local.z * sn, local.y, -local.x * sn + local.z * cs));
        }

        setCameraPose(&camera, camTarget, (float)zoom, cameraAngle, view, inspect != NULL);
        if (!EcosystemVisual_Sync(&visual) && !projectionWarned) {
            fprintf(stderr, "[ADVERTENCIA] No se pudo sincronizar EcosystemVisual.\n");
            projectionWarned = true;
        }
        for (unsigned i = 0; i < w.capacity; ++i) if (w.entities[i].id) {
            EcosystemVisualProjection projection = {0};
            if (!projectEntity(&camera, (float)zoom, ARENA_WINDOW_W, ARENA_WINDOW_H, &w.entities[i], &projection) && !projectionWarned) {
                fprintf(stderr, "[ADVERTENCIA] Proyección visual no finita.\n");
                projectionWarned = true;
            }
            EcosystemVisual_SetProjection(&visual, w.entities[i].id, projection);
        }

        if (frame == 0) {
            EcosystemVisual_PrewarmVisible(&visual);
            double warmStart = OpenGLDemoWindow_Time();
            bool loading = true;
            while (loading && OpenGLDemoWindow_Time() - warmStart < 60.0) {
                EcosystemVisual_Update(&visual, 0.016f);
                EcosystemVisualDrawPacket warm[ECOSYSTEM_VISUAL_MAX_SLOTS];
                size_t count = EcosystemVisual_GetDrawPackets(&visual, warm, ECOSYSTEM_VISUAL_MAX_SLOTS);
                loading = false;
                for (size_t i = 0; i < count; ++i)
                    if (!warm[i].ready || ((warm[i].entityId == focusId || warm[i].entityId == captureTargetId) && warm[i].tier != captureTier))
                        loading = true;
                if (OpenGLDemoWindow_Poll(window).quit) { ok = false; loading = false; }
                if (loading) {
                    renderer.beginFrame(&renderer);
                    OpenGLRenderer_DrawTextPanel(ARENA_WINDOW_W, ARENA_WINDOW_H, 410, 380, "PREPARING EVOLUTION ARENA ANATOMY");
                    renderer.endFrame(&renderer); OpenGLDemoWindow_Swap(window);
                    OpenGLDemoWindow_LimitFPS(OpenGLDemoWindow_Time(), ARENA_MAX_FPS);
                }
            }
            if (loading) { fprintf(stderr, "No se completó el LOD solicitado durante la carga.\n"); ok = false; break; }
            geometryWarmSeconds = OpenGLDemoWindow_Time() - warmStart;
            printf("geometry_warmup_seconds=%.6f cache_builds=%zu\n", geometryWarmSeconds, EcosystemVisual_BuildCount(&visual));
            start = OpenGLDemoWindow_Time(); last = start;
        }

        double adapterStart = OpenGLDemoWindow_Time();
        if (!EcosystemVisual_Update(&visual, (float)dt) && !updateWarned) {
            fprintf(stderr, "[ADVERTENCIA] EcosystemVisual_Update agotó su estado.\n");
            updateWarned = true;
        }
        adapterSum += (OpenGLDemoWindow_Time() - adapterStart) * 1000.0;

        /* Renderizado 3D del terreno, límites físicos, obstáculos, recursos y criaturas. */
        double renderStart = OpenGLDemoWindow_Time();
        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCameraOrtho(&camera, (float)zoom, ARENA_WINDOW_W, ARENA_WINDOW_H);

        float limit = w.observations.halfSize;
        drawMesh(&renderer, &meshes[0], Vec3_Create(0.0f, -0.03f, 0.0f), 0.0f, limit * 1.45f);

        /* Límites físicos visibles de la arena + cuadrícula de referencia sutil. */
        Vector3 corners[4] = {{-limit, 0.04f, -limit}, {limit, 0.04f, -limit}, {limit, 0.04f, limit}, {-limit, 0.04f, limit}};
        for (unsigned i = 0; i < 4; ++i) {
            OpenGLRenderer_DebugLine(corners[i], corners[(i + 1) % 4], Color_FromRGB(62, 198, 214));
            Vector3 topA = Vec3_Add(corners[i], Vec3_Create(0, 0.45f, 0));
            Vector3 topB = Vec3_Add(corners[(i + 1) % 4], Vec3_Create(0, 0.45f, 0));
            OpenGLRenderer_DebugLine(corners[i], topA, Color_FromRGB(62, 198, 214));
            OpenGLRenderer_DebugLine(topA, topB, Color_Create(46, 134, 150, 170));
        }
        for (float g = -limit + 5.0f; g < limit; g += 5.0f) {
            OpenGLRenderer_DebugLine(Vec3_Create(g, 0.01f, -limit), Vec3_Create(g, 0.01f, limit), Color_Create(42, 66, 74, 120));
            OpenGLRenderer_DebugLine(Vec3_Create(-limit, 0.01f, g), Vec3_Create(limit, 0.01f, g), Color_Create(42, 66, 74, 120));
        }

        /* Obstáculos físicos estáticos. */
        for (unsigned i = 0; i < w.obstacleCount; ++i) {
            const EcosystemObstacle *ob = &w.obstacles[i];
            drawMesh(&renderer, &meshes[4], ob->position, 0.0f, ob->radius);
            drawRing3D(ob->position, ob->radius, 0.04f, Color_FromRGB(96, 122, 134));
        }

        /* Recursos (estanques, plantas, cadáveres). */
        for (unsigned i = 0; i < w.resourceCapacity; ++i) {
            const EcosystemResource *resource = &w.resources[i];
            if (!resource->id) continue;
            const AIWorldObject *object = AIWorld_Get(&w.observations, resource->id);
            if (!object) continue;
            if (resource->category == ECO_WATER) {
                drawMesh(&renderer, &meshes[1], Vec3_Create(object->position.x, -0.02f, object->position.z), 0.0f, object->radius);
                drawRing3D(object->position, object->radius, 0.03f, Color_FromRGB(74, 188, 224));
            } else {
                float biomass = resource->maximumBiomass > 0.0f ? resource->biomass / resource->maximumBiomass : 0.0f;
                if (biomass > 0.001f) {
                    drawMesh(&renderer, &meshes[resource->category == ECO_PLANT ? 2 : 3],
                             object->position, 0.0f, 0.3f + 1.4f * sqrtf(biomass));
                }
            }
        }

        /* Criaturas procedurales (reutilizando caché compartida de mallas, LOD y skinning). */
        EcosystemVisualDrawPacket packets[ECOSYSTEM_VISUAL_MAX_SLOTS];
        size_t packetCount = EcosystemVisual_GetDrawPackets(&visual, packets, ECOSYSTEM_VISUAL_MAX_SLOTS);
        bool frameHasMouth = false, captureTargetReady = false;
        double frameMouthError = 0.0;
        memcpy(displayedPackets, packets, packetCount * sizeof(*packets));
        displayedCount = packetCount;
        drawnSum += (double)packetCount;
        if (packetCount < drawnMin) drawnMin = (unsigned)packetCount;
        if (packetCount > drawnMax) drawnMax = (unsigned)packetCount;

        for (size_t i = 0; i < packetCount; ++i) {
            EcosystemVisualDrawPacket *packet = &packets[i];
            if (packet->entityId == captureTargetId && packet->ready && packet->tier == VISUAL_TIER_NEAR)
                captureTargetReady = true;
            const Mesh *body = packet->bodyMesh ? packet->bodyMesh : EcosystemVisual_FallbackMesh(&visual);
            const EcosystemEntity *packetEntity = findEntity(&w, packet->entityId);
            if (packet->ready && packetEntity && packetEntity->speciesIndex < w.speciesCount) {
                const EcosystemSpecies *packetSpecies = &w.species[packetEntity->speciesIndex];
                uint32_t recipeId = packetSpecies->recipeId;
                if ((recipeId == 3u || recipeId == 4u) && !scaleProofPrinted[recipeId] && packet->meshRadius > 1e-4f) {
                    float renderedWidth = packet->scale * packet->meshRadius;
                    float size = packetEntity->genome.size * packetEntity->developmentScale;
                    float expectedWidth = packetSpecies->radius * size;
                    float normalizedWidth = renderedWidth / fmaxf(0.01f, size);
                    printf("scale_proof recipeId=%u meshRadius=%.6f measuredMeshRadius=%.6f meshLength=%.6f calibrationLength=%.6f totalScale=%.6f k=%.9f rendered_width=%.6f species_radius=%.6f expected_width=%.6f normalized_width=%.6f\n",
                           recipeId, packet->meshRadius, packet->measuredMeshRadius, packet->meshLength,
                           packet->calibrationLength, packet->axialTotalScale, packet->calibrationFactor,
                           renderedWidth, packetSpecies->radius, expectedWidth, normalizedWidth);
                    scaleProofPrinted[recipeId] = true;
                }
            }
            if (body) {
                OpenGLRenderer_ModelTransform(packet->position, packet->yaw * 57.29578f, packet->scale, Vec3_Zero());
                bool skinning = !forceCpu && packet->deformer && packet->skeleton && packet->pose &&
                                OpenGLRenderer_SetMeshMorphSkinning(&renderer, body, packet->deformer, packet->skeleton,
                                                                    packet->pose, packet->targetGraph, packet->headJoint,
                                                                    packet->headScale, packetGeneration(packet));
                OpenGLRenderer_SetMeshSurfaceOverride(&renderer, &packet->surfaceRecipe);
                if (skinning || !packet->ready) renderer.renderMesh(&renderer, body);
                else if (EcosystemVisual_DeformBody(packet, &cpuBodies[packet->slotIndex]))
                    renderer.renderMesh(&renderer, &cpuBodies[packet->slotIndex]);
                if (validateSkin && skinning &&
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
                if (renderer.renderEye) {
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
                double err = mouthFrameError(&w, packet, &hasSample);
                if (hasSample) { frameHasMouth = true; if (err > frameMouthError) frameMouthError = err; }
            }
        }
        if (validateMouthFrames && frameHasMouth) {
            mouthWindow[mouthWindowIndex] = frameMouthError;
            mouthWindowIndex = (mouthWindowIndex + 1) % validateMouthFrames;
            if (mouthWindowCount < validateMouthFrames) ++mouthWindowCount;
        }

        /* Indicadores de especie/selección y overlays opcionales (D). */
        for (unsigned i = 0; i < w.capacity; ++i) {
            if (!w.entities[i].id) continue;
            EcosystemEntity *entity = &w.entities[i];
            if (showHud) {
                Color spMark = entity->speciesIndex == 0 ? Color_Create(110, 216, 152, 160) : Color_Create(238, 118, 92, 160);
                drawRing3D(entity->body.position, entity->body.capabilities.clearance * 0.92f, 0.02f, spMark);
            }
            if (debug) {
                OpenGLRenderer_DebugLine(entity->oralPivot, entity->oralElbow, Color_FromRGB(245, 200, 100));
                OpenGLRenderer_DebugLine(entity->oralElbow, entity->body.mouth, Color_FromRGB(245, 200, 100));
                if (entity->mind.target) {
                    const AIWorldObject *obj = AIWorld_Get(&w.observations, entity->mind.target);
                    if (obj) OpenGLRenderer_DebugLine(entity->body.eye, obj->position, Color_FromRGB(68, 224, 240));
                }
                for (unsigned p = entity->mind.navigation.index; p + 1 < entity->mind.navigation.count; ++p) {
                    OpenGLRenderer_DebugLine(
                        Vec3_Add(entity->mind.navigation.points[p], Vec3_Create(0, 0.08f, 0)),
                        Vec3_Add(entity->mind.navigation.points[p + 1], Vec3_Create(0, 0.08f, 0)),
                        Color_FromRGB(120, 210, 225));
                }
            }
        }
        if (selected && showHud) {
            drawRing3D(selected->body.position, selected->body.capabilities.clearance + 0.25f, 0.05f, Color_FromRGB(74, 236, 250));
            if (swapPendingId && swapPendingId != selected->id) {
                const EcosystemEntity *sw = findEntity(&w, swapPendingId);
                if (sw) drawRing3D(sw->body.position, sw->body.capabilities.clearance + 0.30f, 0.06f, Color_FromRGB(252, 206, 82));
            }
        }

        /* ================================================================== */
        /* Interfaz 2D de laboratorio                                         */
        /* ================================================================== */
        if (showHud) {
            OpenGLRenderer_Begin2D(ARENA_WINDOW_W, ARENA_WINDOW_H);

            /* 1) Panel Izquierdo: Estadísticas de Población y Gráfica. */
            unsigned brums = 0, velks = 0;
            for (unsigned i = 0; i < w.capacity; ++i) {
                if (!w.entities[i].id) continue;
                if (w.entities[i].speciesIndex == 0) ++brums; else ++velks;
            }
            const float lx = 12.0f, ly = 12.0f, lw = 308.0f, lh = 396.0f;
            OpenGLRenderer_DrawRect2D(lx, ly, lw, lh, Color_Create(9, 15, 22, 226), Color_Create(42, 138, 158, 205));
            OpenGLRenderer_DrawRect2D(lx, ly, lw, 26.0f, Color_Create(14, 28, 39, 240), Color_Create(42, 138, 158, 205));
            char hdr[96];
            snprintf(hdr, sizeof(hdr), "EVOLUTION ARENA  [%s]",
                     w.defaultController == CREATURE_CONTROLLER_NEURAL ? w.founderModeName : "UTILITY");
            OpenGLRenderer_DrawText2D(lx + 10.0f, ly + 7.0f, 1.35f, Color_Create(78, 222, 236, 255), hdr);

            unsigned minute = (unsigned)(SimulationClock_TimeOfDay(&w.clock) * 1440.0);
            char statsTxt[512];
            snprintf(statsTxt, sizeof(statsTxt),
                     "SEED %-6u  DAY %3.0f %02u:%02u\n"
                     "REQ %2.0fX  EFF %4.1fX  %s%s\n"
                     "POP %3u/%-3u  GEN MAX %-2u\n"
                     "BIRTHS %-4llu  DEATHS %-4llu\n"
                     "STARV %-3llu DEHYD %-3llu PRED %-3llu\n"
                     "AVG HP %.2f  AVG ENG %.2f\n"
                     "BIOMASS %5.1f  DIV %.3f\n"
                     "LINEAGES %-3u  INTV %-3llu",
                     config.seed, floor(SimulationClock_Days(&w.clock)), minute / 60, minute % 60,
                     w.clock.speed, effectiveSpeed, w.clock.paused ? "PAUSE" : "RUN",
                     w.clock.behind ? " BACKLOG" : "",
                     w.count, w.capacity, w.statistics.maximumGeneration,
                     (unsigned long long)w.statistics.births, (unsigned long long)w.statistics.deaths,
                     (unsigned long long)w.statistics.deathCauses[DEATH_STARVATION],
                     (unsigned long long)w.statistics.deathCauses[DEATH_DEHYDRATION],
                     (unsigned long long)w.statistics.deathCauses[DEATH_PREDATION],
                     w.statistics.averageHealth, w.statistics.averageEnergy,
                     w.statistics.plantBiomass, w.statistics.neuralDiversity,
                     w.statistics.activeLineages, (unsigned long long)w.interventionCount);
            OpenGLRenderer_DrawText2D(lx + 10.0f, ly + 36.0f, 1.35f, Color_Create(218, 234, 240, 255), statsTxt);

            /* Leyenda por especie. */
            float gy = ly + 258.0f;
            char spBuf[64];
            snprintf(spBuf, sizeof(spBuf), "BRUMACERNO: %u", brums);
            OpenGLRenderer_DrawRect2D(lx + 10.0f, gy, 10.0f, 10.0f, Color_Create(110, 216, 152, 255), Color_Create(0, 0, 0, 0));
            OpenGLRenderer_DrawText2D(lx + 24.0f, gy, 1.25f, Color_Create(158, 232, 186, 255), spBuf);
            snprintf(spBuf, sizeof(spBuf), "VELKAR: %u", velks);
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

            /* 2) Panel Derecho: Inspector del Individuo Seleccionado + Controles de Cerebro. */
            const float rx = (float)ARENA_WINDOW_W - 324.0f, ry = 12.0f, rw = 312.0f, rh = 482.0f;
            OpenGLRenderer_DrawRect2D(rx, ry, rw, rh, Color_Create(9, 15, 22, 226), Color_Create(42, 138, 158, 205));
            OpenGLRenderer_DrawRect2D(rx, ry, rw, 26.0f, Color_Create(14, 28, 39, 240), Color_Create(42, 138, 158, 205));
            if (selected) {
                const EcosystemSpecies *sp = &w.species[selected->speciesIndex];
                const NeuralGenome *ng = CreatureMind_GetNeuralGenome(&selected->mind);
                char rHdr[80];
                snprintf(rHdr, sizeof(rHdr), "CREATURE #%u  %s", selected->id, sp->name);
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 7.0f, 1.35f,
                                          selected->speciesIndex == 0 ? Color_Create(126, 232, 168, 255)
                                                                      : Color_Create(246, 146, 118, 255), rHdr);
                const char *stageStr = selected->physiology.stage == LIFE_JUVENILE ? "JUV"
                                       : selected->physiology.stage == LIFE_ADULT ? "ADULT" : "OLD";
                char idTxt[256];
                snprintf(idTxt, sizeof(idTxt),
                         "AGE %5.1fS (%s)  GEN %-2u\n"
                         "PARENTS %u,%u  OFFSP %-2u\n"
                         "LINEAGE #%llu",
                         selected->physiology.age, stageStr, selected->generation,
                         selected->reproduction.parents[0], selected->reproduction.parents[1],
                         selected->offspringCount,
                         (unsigned long long)(ng ? ng->metadata.lineageId : 0u));
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 34.0f, 1.3f, Color_Create(208, 228, 236, 255), idTxt);

                drawPhysBar(rx + 10.0f, ry + 84.0f, rw - 20.0f, 16.0f, "HEALTH", selected->physiology.health, Color_Create(96, 214, 140, 235));
                drawPhysBar(rx + 10.0f, ry + 104.0f, rw - 20.0f, 16.0f, "ENERGY", selected->physiology.energy, Color_Create(64, 210, 224, 235));
                drawPhysBar(rx + 10.0f, ry + 124.0f, rw - 20.0f, 16.0f, "HYDRAT", selected->physiology.hydration, Color_Create(78, 162, 238, 235));
                drawPhysBar(rx + 10.0f, ry + 144.0f, rw - 20.0f, 16.0f, "FATIG ", selected->physiology.fatigue, Color_Create(236, 186, 78, 235));

                const char *reproStr = selected->reproduction.gestating ? "GESTATING"
                                       : selected->body.mateReady ? "READY" : "WAIT";
                char mindTxt[384];
                snprintf(mindTxt, sizeof(mindTxt),
                         "INTENT  %s\n"
                         "ACTION  %s\n"
                         "TARGET  #%u  REPRO %s\n"
                         "OUTCOME %s\n"
                         "FB F:%.1f I:%.1f D:%.1f\n"
                         "ARCH    %s\n"
                         "FP      %016llX",
                         selected->mind.controllerType == CREATURE_CONTROLLER_NEURAL
                             ? NeuralActionIntent_Name(selected->mind.neuralState.selectedIntent) : "UTILITY",
                         CreatureAction_Name(selected->mind.action),
                         selected->mind.target, reproStr,
                         NeuralInteractionOutcome_Name(selected->mind.neuralState.lastOutcome),
                         selected->mind.neuralState.lastFeedback[0],
                         selected->mind.neuralState.lastFeedback[1],
                         selected->mind.neuralState.lastFeedback[2],
                         ng ? "FIXED_RNN 32-12-10" : "LEGACY_UTILITY",
                         (unsigned long long)(ng ? ng->metadata.fingerprint : 0u));
                OpenGLRenderer_DrawText2D(rx + 10.0f, ry + 172.0f, 1.3f, Color_Create(212, 232, 240, 255), mindTxt);

                /* Botones de gestión de cerebros. */
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

            /* Banner de estado / errores de validación con ajuste de línea limpio. */
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
                drawNeuralInspector(selected, edgeThreshold, inspectedLayer, inspectedIndex,
                                    &btnThMinus, &btnThPlus, &btnClearSel, inPos, hidPos, outPos);
            }

            /* 4) Modal de entrada de ruta de archivo (Export / Import). */
            if (promptMode != PROMPT_NONE) {
                float mx = 360.0f, my = 600.0f, mw = 560.0f, mh = 96.0f;
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

            /* 5) Barra inferior de controles y rendimiento. */
            OpenGLRenderer_DrawRect2D(12.0f, (float)ARENA_WINDOW_H - 36.0f, (float)ARENA_WINDOW_W - 24.0f, 26.0f,
                                      Color_Create(9, 15, 22, 220), Color_Create(38, 108, 126, 175));
            OpenGLRenderer_DrawText2D(20.0f, (float)ARENA_WINDOW_H - 28.0f, 1.2f, Color_Create(168, 206, 218, 255),
                                      "SPACE:PAUSE  1-4:1X/4X/16X/64X  TAB:NEXT  F:FOLLOW  O:TOP  N:NEURAL  D:DEBUG  E:EXP I:IMP C:CPY V:PST X:SWP B:RND R:RST");
            double renderCpu = (OpenGLDemoWindow_Time() - start) * 1000.0;
            char fpsBuf[64];
            snprintf(fpsBuf, sizeof(fpsBuf), "FPS %2.0f  %.1fMS", displayedFps, renderCpu);
            OpenGLRenderer_DrawText2D((float)ARENA_WINDOW_W - 160.0f, (float)ARENA_WINDOW_H - 28.0f,
                                      1.25f, Color_Create(86, 226, 238, 255), fpsBuf);

            OpenGLRenderer_End2D();
        }

        renderSum += (OpenGLDemoWindow_Time() - renderStart) * 1000.0;

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
                        for (size_t v = 0; v < posed.vertexCount; ++v) {
                            Vector3 p = Vec3_Scale(posed.vertices[v].position, packets[i].scale);
                            AABB_ExpandPoint(&bounds, Vec3_Add(packets[i].position, Vec3_Create(p.x * cs + p.z * sn, p.y, -p.x * sn + p.z * cs)));
                        }
                        printf("capture_world_body_bounds min=%g,%g,%g max=%g,%g,%g\n",
                               bounds.start.x, bounds.start.y, bounds.start.z, bounds.end.x, bounds.end.y, bounds.end.z);
                    }
                    Mesh_Free(&posed);
                    if (packets[i].jawJoint >= 0) {
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
            captured = ok && OpenGLRenderer_SavePPM(capture, ARENA_WINDOW_W, ARENA_WINDOW_H);
            ok = captured && ok;
            const EcosystemEntity *subject = findEntity(&w, captureTargetId ? captureTargetId : focusId);
            if (subject) {
                printf("capture_entity=%u species=%u recipe=%u action=%s stage=%u\n",
                       subject->id, subject->body.species, w.species[subject->speciesIndex].recipeId,
                       CreatureAction_Name(subject->mind.action), subject->physiology.stage);
            }
            if (subject && subject->id == turningEntity) printf("capture_yaw_rate_rad_s=%g\n", observedTurnRate);
        }

        renderer.endFrame(&renderer);
        finalGpu = OpenGLRenderer_GetMeshPerformanceStats(&renderer);
        if (finalGpu.gpuTimingGeneration != gpuTimingGeneration) {
            gpuTimingGeneration = (unsigned)finalGpu.gpuTimingGeneration;
            gpuSum += finalGpu.gpuRenderMs;
            ++gpuSamples;
        }
        OpenGLDemoWindow_Swap(window);
        if (times) times[frame] = (OpenGLDemoWindow_Time() - start) * 1000.0;
        if (!frames) OpenGLDemoWindow_LimitFPS(start, ARENA_MAX_FPS);
        ++frame;
        if (captured) break;
    }

    if (capture && frames && !captured && !captureWarned) {
        fprintf(stderr, "No se observó el estado solicitado para captura en %u frames.\n", frames);
        captureWarned = true;
        ok = false;
    }

    if (profile && frame) {
        double sum = 0.0, simSum = 0.0;
        for (unsigned i = 0; i < frame; ++i) { sum += times[i]; simSum += simulationTimes[i]; }
        qsort(times, frame, sizeof(double), compareDouble);
        double mouthMax = 0.0;
        for (unsigned i = 0; i < mouthWindowCount; ++i) if (mouthWindow[i] > mouthMax) mouthMax = mouthWindow[i];
        struct rusage usage; getrusage(RUSAGE_SELF, &usage);
        ensureParentDir(profile);
        FILE *f = fopen(profile, "w");
        if (f) {
            fprintf(f,
                "{\"frames\":%u,\"initial_population\":%u,\"final_population\":%u,\"controller\":\"%s\",\"founder_mode\":\"%s\","
                "\"mean_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f,\"max_ms\":%.6f,"
                "\"sim_mean_ms\":%.6f,\"adapter_mean_ms\":%.6f,\"render_mean_ms\":%.6f,"
                "\"geometry_warmup_seconds\":%.6f,\"gpu_mean_ms\":%.6f,\"process_cpu_ms\":%.6f,"
                "\"cache_entries\":%zu,\"cache_builds\":%zu,\"cache_hits\":%zu,\"drawn_mean\":%.3f,"
                "\"drawn_min\":%u,\"drawn_max\":%u,\"mouth_window_max_error\":%.6f,"
                "\"foot_error_max\":%.6f,\"stance_foot_error_max\":%.6f,"
                "\"skin_position_error\":%.6f,\"skin_normal_error\":%.6f,"
                "\"peak_rss_kib\":%ld,\"births\":%llu,\"deaths\":%llu,\"max_generation\":%u,\"neural_diversity\":%.6f}\n",
                frame, initialPopulation, w.count,
                w.defaultController == CREATURE_CONTROLLER_NEURAL ? "neural" : "utility",
                w.founderModeName,
                sum / frame, times[frame / 2], times[(frame - 1) * 95 / 100],
                times[(frame - 1) * 99 / 100], times[frame - 1],
                simSum / frame, adapterSum / frame, renderSum / frame,
                geometryWarmSeconds, gpuSamples ? gpuSum / gpuSamples : 0.0,
                1000.0 * (double)(clock() - processStart) / CLOCKS_PER_SEC / frame,
                EcosystemVisual_CacheCount(&visual), EcosystemVisual_BuildCount(&visual),
                EcosystemVisual_CacheHitCount(&visual), drawnSum / frame,
                drawnMin == ECOSYSTEM_VISUAL_MAX_SLOTS ? 0 : drawnMin, drawnMax,
                mouthMax, footErrorMax, stanceFootErrorMax, skinPositionError, skinNormalError,
                usage.ru_maxrss, (unsigned long long)w.statistics.births,
                (unsigned long long)w.statistics.deaths, w.statistics.maximumGeneration,
                w.statistics.neuralDiversity);
            fclose(f);
        } else {
            ok = false;
        }
    }

    if (hasClipboard) NeuralGenome_Destroy(&clipboardBrain);
    free(times); free(simulationTimes); free(mouthWindow);
    for (unsigned i = 0; i < ECOSYSTEM_VISUAL_MAX_SLOTS; ++i) Mesh_Free(&cpuBodies[i]);
    for (unsigned i = 0; i < 5; ++i) Mesh_Free(&meshes[i]);
    EcosystemVisual_Shutdown(&visual);
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    EcosystemWorld_Free(&w);
    return ok ? 0 : 1;
}
