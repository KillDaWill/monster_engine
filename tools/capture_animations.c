/** @file capture_animations.c
 * @brief Captura directa y reproducible de poses y animaciones sin depender de demo_ecosystem.
 */
#include "CreatureRecipeIO.h"
#include "CreatureRecipes.h"
#include "CreatureRecipeRegistry.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "MonsterVisual.h"
#include "AnimatedVisual.h"
#include "CreatureRig.h"
#include "MonsterAnimation.h"
#include "GaitPresets.h"
#include "OpenGLRenderer.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AnimPoseStats {
    float withersPitch, cervPitch, cervYaw, neckPitch, neckYaw, neckRoll, headPitch, headYaw, headRoll;
    float reachErr, reachOri, reachSat;
    float minEdgeRatio, maxEdgeRatio, minAreaRatio, maxAreaRatio;
    int checkedTris, crushedTris, extremeTris, status;
} AnimPoseStats;

typedef struct SurfaceStrain {
    size_t checked, extreme, crushed;
    double area, extremeArea;
} SurfaceStrain;

typedef enum AnimKind {
    ANIM_IDLE = 0,
    ANIM_WALK,
    ANIM_RUN,
    ANIM_TURN_LEFT,
    ANIM_TURN_RIGHT,
    ANIM_LOOK_UP,
    ANIM_LOOK_DOWN,
    ANIM_LOOK_LEFT,
    ANIM_LOOK_RIGHT,
    ANIM_LOOK_DOWN_LEFT,
    ANIM_LOOK_UP_LEFT,
    ANIM_EAT,
    ANIM_DRINK,
    ANIM_ATTACK,
    ANIM_REACH_SIDE_LEFT,
    ANIM_REACH_UP,
    ANIM_CURL,
    ANIM_CURL_PARTIAL,
    ANIM_HOP_LAUNCH,
    ANIM_HOP_FLIGHT,
    ANIM_HOP_LANDING,
    ANIM_WING_EXTENDED,
    ANIM_WING_PARTIAL,
    ANIM_WING_FOLDED,
    ANIM_WING_FLAP,
    ANIM_COUNT
} AnimKind;

typedef struct AnimSpec {
    AnimKind kind;
    const char* name;
} AnimSpec;

static const AnimSpec kAnimSpecs[ANIM_COUNT] = {
    {ANIM_IDLE, "idle"},
    {ANIM_WALK, "walk"},
    {ANIM_RUN, "run"},
    {ANIM_TURN_LEFT, "turn_left"},
    {ANIM_TURN_RIGHT, "turn_right"},
    {ANIM_LOOK_UP, "look_up"},
    {ANIM_LOOK_DOWN, "look_down"},
    {ANIM_LOOK_LEFT, "look_left"},
    {ANIM_LOOK_RIGHT, "look_right"},
    {ANIM_LOOK_DOWN_LEFT, "look_down_left"},
    {ANIM_LOOK_UP_LEFT, "look_up_left"},
    {ANIM_EAT, "eat"},
    {ANIM_DRINK, "drink"},
    {ANIM_ATTACK, "attack"},
    {ANIM_REACH_SIDE_LEFT, "reach_side_left"},
    {ANIM_REACH_UP, "reach_up"},
    {ANIM_CURL, "curl"},
    {ANIM_CURL_PARTIAL, "curl_partial"},
    {ANIM_HOP_LAUNCH, "hop_launch"},
    {ANIM_HOP_FLIGHT, "hop_flight"},
    {ANIM_HOP_LANDING, "hop_landing"},
    {ANIM_WING_EXTENDED, "wing_extended"},
    {ANIM_WING_PARTIAL, "wing_partial"},
    {ANIM_WING_FOLDED, "wing_folded"},
    {ANIM_WING_FLAP, "wing_flap"}
};

typedef struct ViewSpec {
    const char* name;
    Vector3 dir;
} ViewSpec;

static const ViewSpec kViews[] = {
    {"side", {1.0f, 0.0f, 0.0f}},
    {"three-quarter", {0.78f, 0.22f, 0.62f}},
    {"front", {0.0f, 0.0f, 1.0f}},
    {"top", {0.0f, 1.0f, 0.0f}},
    {"rear", {0.0f, 0.0f, -1.0f}}
};

static bool ContainsToken(const char* csv, const char* token) {
    if (!csv || !strcmp(csv, "all")) return true;
    size_t len = strlen(token);
    const char* p = csv;
    while (*p) {
        while (*p == ',' || isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        const char* end = p;
        while (*end && *end != ',') ++end;
        if ((size_t)(end - p) == len && !strncmp(p, token, len)) return true;
        p = *end ? end + 1 : end;
    }
    return false;
}

static void NeutralMesh(Mesh* mesh) {
    mesh->hasSurface = false;
    for (size_t i = 0; i < mesh->vertexCount; ++i)
        mesh->vertices[i].color = Color_FromRGB(172, 172, 172);
    Mesh_MarkGeometryChanged(mesh);
}

static void NeutralVisual(MonsterVisual* v) {
    NeutralMesh(&v->mesh);
    NeutralMesh(&v->headMesh);
    NeutralMesh(&v->membraneMesh);
    NeutralMesh(&v->armorMesh);
    for (size_t i = 0; i < v->mouthCount; ++i) {
        NeutralMesh(&v->mouths[i].jawBase);
        NeutralMesh(&v->mouths[i].hingeBase);
        NeutralMesh(&v->mouths[i].jaw);
        NeutralMesh(&v->mouths[i].hinge);
    }
}

static float TriangleArea2(Vector3 a, Vector3 b, Vector3 c) {
    return Vec3_Length(Vec3_Cross(Vec3_Sub(b, a), Vec3_Sub(c, a)));
}

static SurfaceStrain MeasureSurface(Mesh* mesh, const AnatomyDeformer* def, float scale, bool paint) {
    SurfaceStrain result = {0};
    if (!def || mesh->vertexCount != def->vertexCount) return result;
    if (paint) NeutralMesh(mesh);
    float minimumArea = scale * scale * 1e-6f;
    for (size_t i = 0; i + 2 < mesh->indexCount; i += 3) {
        MeshIndex a = mesh->indices[i], b = mesh->indices[i + 1], c = mesh->indices[i + 2];
        float rest = TriangleArea2(def->basePositions[a], def->basePositions[b], def->basePositions[c]);
        if (rest <= minimumArea) continue;
        float ratio = TriangleArea2(mesh->vertices[a].position, mesh->vertices[b].position,
                                    mesh->vertices[c].position) / rest;
        ++result.checked;
        result.area += rest;
        if (ratio < .25f) ++result.crushed;
        if (ratio >= .35f && ratio <= 2.0f) continue;
        ++result.extreme;
        result.extremeArea += rest;
        if (paint) {
            Color color = ratio < .35f ? Color_FromRGB(30, 112, 230) : Color_FromRGB(240, 55, 28);
            mesh->vertices[a].color = color;
            mesh->vertices[b].color = color;
            mesh->vertices[c].color = color;
        }
    }
    return result;
}

static void WriteSurfaceStrain(FILE* file, const char* name, SurfaceStrain strain) {
    fprintf(file, ", \"%s\": {\"triangles\": %zu, \"extreme\": %zu, \"crushed\": %zu, "
                  "\"extremeAreaFraction\": %.7f}", name, strain.checked, strain.extreme,
                  strain.crushed, strain.area > 0.0 ? strain.extremeArea / strain.area : 0.0);
}

static void QuatToAngles(Quaternion q, float* pitchDown, float* yawLeft, float* rollRight) {
    q = Quat_Normalize(q);
    if (q.w < 0.0f) q = (Quaternion){-q.x, -q.y, -q.z, -q.w};
    float proj = q.z;
    Quaternion twist = Quat_Normalize((Quaternion){0.0f, 0.0f, proj, q.w});
    *rollRight = 2.0f * atan2f(twist.z, twist.w);
    Quaternion swing = Quat_Normalize(Quat_Multiply(q, Quat_Inverse(twist)));
    if (swing.w < 0.0f) swing = (Quaternion){-swing.x, -swing.y, -swing.z, -swing.w};
    float theta = 2.0f * acosf(fmaxf(-1.0f, fminf(1.0f, swing.w)));
    float sinHalf = sqrtf(fmaxf(0.0f, swing.x * swing.x + swing.y * swing.y + swing.z * swing.z));
    float scale = (theta > 1e-6f && sinHalf > 1e-6f) ? (theta / sinHalf) : 2.0f;
    *pitchDown = scale * swing.x;
    *yawLeft = scale * swing.y;
}

static void ComputeMetrics(const Mesh* restMesh, const Mesh* posedMesh, const Monster* m,
                           const AnatomyDeformer* def, AnimPoseStats* out) {
    memset(out, 0, sizeof(*out));
    const Rig* r = &m->animation->rig;
    const SkeletonPose* p = &m->animation->pose;
    const SkeletonPose* rp = NULL;
    SkeletonPose restPose = {0};
    SkeletonPose_Init(&restPose, &r->skeleton);
    rp = &restPose;

    if (r->spineCount > 0) {
        int jw = r->spine[r->spineCount - 1];
        float yaw = 0, roll = 0;
        QuatToAngles(Quat_Multiply(Quat_Inverse(r->skeleton.joints[jw].restRotation), p->joints[jw].localRotation),
                     &out->withersPitch, &yaw, &roll);
    }
    if (r->cervicalBaseJoint >= 0) {
        float roll = 0;
        QuatToAngles(Quat_Multiply(Quat_Inverse(r->skeleton.joints[r->cervicalBaseJoint].restRotation),
                                   p->joints[r->cervicalBaseJoint].localRotation),
                     &out->cervPitch, &out->cervYaw, &roll);
    }
    if (r->neckJoint >= 0) {
        QuatToAngles(Quat_Multiply(Quat_Inverse(r->skeleton.joints[r->neckJoint].restRotation),
                                   p->joints[r->neckJoint].localRotation),
                     &out->neckPitch, &out->neckYaw, &out->neckRoll);
    }
    if (r->headJoint >= 0) {
        QuatToAngles(Quat_Multiply(Quat_Inverse(r->skeleton.joints[r->headJoint].restRotation),
                                   p->joints[r->headJoint].localRotation),
                     &out->headPitch, &out->headYaw, &out->headRoll);
    }
    out->reachErr = m->animation->animator.reachResult.error;
    out->reachOri = m->animation->animator.reachResult.orientationError;
    out->reachSat = m->animation->animator.reachResult.saturation;
    out->status = (int)m->animation->animator.reachResult.status;

    out->minEdgeRatio = 1.0f;
    out->maxEdgeRatio = 1.0f;
    out->minAreaRatio = 1.0f;
    out->maxAreaRatio = 1.0f;
    if (!def || !def->skinBindings || posedMesh->vertexCount != restMesh->vertexCount) return;

    int jW = r->spineCount > 0 ? r->spine[r->spineCount - 1] : -1;
    int jCB = r->cervicalBaseJoint, jN = r->neckJoint, jH = r->headJoint;
    float minE = 100.0f, maxE = 0.0f, minA = 100.0f, maxA = 0.0f;
    for (uint32_t t = 0; t + 2 < restMesh->indexCount; t += 3) {
        uint32_t i0 = restMesh->indices[t], i1 = restMesh->indices[t + 1], i2 = restMesh->indices[t + 2];
        bool relevant = false;
        for (int k = 0; k < 3 && !relevant; ++k) {
            uint32_t vi = restMesh->indices[t + k];
            for (int w = 0; w < 2 * ANATOMY_MORPH_INFLUENCES; ++w) {
                if (def->skinBindings[vi].weights[w] > 0.1f) {
                    int ji = def->skinBindings[vi].joints[w];
                    if (ji == jW || ji == jCB || ji == jN || ji == jH) { relevant = true; break; }
                }
            }
        }
        if (!relevant) continue;
        Vector3 c = Vec3_Scale(Vec3_Add(restMesh->vertices[i0].position,
                               Vec3_Add(restMesh->vertices[i1].position, restMesh->vertices[i2].position)), 1.0f / 3.0f);
        if (jH >= 0 && c.z > rp->joints[jH].worldPosition.z + 0.15f) continue;

        Vector3 rpos[3] = {restMesh->vertices[i0].position, restMesh->vertices[i1].position, restMesh->vertices[i2].position};
        Vector3 ppos[3] = {posedMesh->vertices[i0].position, posedMesh->vertices[i1].position, posedMesh->vertices[i2].position};
        for (int e = 0; e < 3; ++e) {
            float rl = Vec3_Distance(rpos[e], rpos[(e + 1) % 3]);
            float pl = Vec3_Distance(ppos[e], ppos[(e + 1) % 3]);
            if (rl > 1e-4f) {
                float ratio = pl / rl;
                if (ratio < minE) minE = ratio;
                if (ratio > maxE) maxE = ratio;
            }
        }
        float ra = Vec3_Length(Vec3_Cross(Vec3_Sub(rpos[1], rpos[0]), Vec3_Sub(rpos[2], rpos[0])));
        float pa = Vec3_Length(Vec3_Cross(Vec3_Sub(ppos[1], ppos[0]), Vec3_Sub(ppos[2], ppos[0])));
        if (ra > 1e-6f) {
            ++out->checkedTris;
            float ar = pa / ra;
            if (ar < minA) minA = ar;
            if (ar > maxA) maxA = ar;
            if (ar < 0.25f) ++out->crushedTris;
            if (ar > 2.00f || ar < 0.35f) ++out->extremeTris;
        }
    }
    if (maxE > 0.0f) { out->minEdgeRatio = minE; out->maxEdgeRatio = maxE; }
    if (maxA > 0.0f) { out->minAreaRatio = minA; out->maxAreaRatio = maxA; }
}

static bool LoadRecipeArg(const char* arg, CreatureRecipe* outRecipe, CreatureRecipeDocument* doc) {
    char error[256] = {0};
    if (strstr(arg, ".creature.json")) {
        if (!CreatureRecipeIO_LoadFile(doc, arg, error, sizeof(error))) {
            fprintf(stderr, "Error leyendo receta '%s': %s\n", arg, error);
            return false;
        }
        *outRecipe = doc->recipe;
        return true;
    }
    CreatureRecipeRegistry_LoadDirectory("recipes", error, sizeof(error));
    char* end = NULL;
    unsigned long id = strtoul(arg, &end, 10);
    if (end && *end == 0 && id > 0) {
        const CreatureRecipe* r = CreatureRecipeRegistry_Find((CreatureRecipeId)id);
        if (!r) r = CreatureRecipes_Find((CreatureRecipeId)id);
        if (r) { *outRecipe = *r; return true; }
    }
    if (!strcasecmp(arg, "lizard")) { *outRecipe = *CreatureRecipes_Lizard(); return true; }
    if (!strcasecmp(arg, "dog")) { *outRecipe = *CreatureRecipes_Dog(); return true; }
    if (!strcasecmp(arg, "brumacerno")) { *outRecipe = *CreatureRecipes_Brumacerno(); return true; }
    if (!strcasecmp(arg, "velkar")) { *outRecipe = *CreatureRecipes_Velkar(); return true; }
    static const struct { const char* name; CreatureRecipeId id; } kExt[] = {
        {"caballo", 1005}, {"asno", 1006}, {"cerdo", 1007}, {"elefante", 1008},
        {"rinoceronte", 1009}, {"rata", 1010}, {"jirafa", 1011}, {"giraffe", 1011},
        {"ornitorrinco", 1012}, {"platypus", 1012}, {"pangolin", 1013},
        {"canguro", 1014}, {"kangaroo", 1014}, {"murcielago", 1015}, {"bat", 1015},
        {"explorador", 1000}
    };
    for (size_t i = 0; i < sizeof(kExt) / sizeof(kExt[0]); ++i) {
        if (!strcasecmp(arg, kExt[i].name)) {
            const CreatureRecipe* r = CreatureRecipeRegistry_Find(kExt[i].id);
            if (r) { *outRecipe = *r; return true; }
        }
    }
    fprintf(stderr, "Receta no encontrada: '%s'\n", arg);
    return false;
}

static void ConfigureAnimState(const Monster* m, const Rig* rig, ProceduralAnimator* a,
                               AnimKind kind, int step, int totalSteps) {
    float s = m->phenotype.axial.totalScale;
    float baseSpeed = a->gait.strideLength / fmaxf(0.1f, a->gait.cycleDuration);
    a->desiredVelocity = Vec3_Zero();
    a->angularVelocity = 0.0f;
    a->lookDirection = Vec3_Create(0, 0, 1);
    a->mouthOpen = 0.0f;
    a->defensiveCurl = 0.0f;
    a->wingFold = 0.0f;
    a->wingFlap = 0.0f;
    a->oralReachActive = false;
    a->interactionTarget.valid = false;

    const AnatomicalReachProfile* rp = &a->reachProfile;
    Vector3 neckBaseRest = (rp->count > 0) ? m->animation->pose.joints[rp->joints[0].index].worldPosition : Vec3_Zero();
    Vector3 headPosRest = (rig->headJoint >= 0) ? m->animation->pose.joints[rig->headJoint].worldPosition : neckBaseRest;
    (void)headPosRest;
    float R = fmaxf(0.5f * s, rp->maximumReach);
    bool agile = m->recipe.gait == CREATURE_GAIT_FELINE_STALK;

    switch (kind) {
        case ANIM_IDLE:
            break;
        case ANIM_WALK:
            if (m->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP) {
                a->gait = GaitPreset_BipedalHop(rig);
            } else {
                a->gait = GaitPreset_QuadrupedMotion(rig, agile, baseSpeed * 0.9f, 0.0f);
            }
            a->desiredVelocity = Vec3_Create(0, 0, baseSpeed * 0.9f);
            break;
        case ANIM_HOP_LAUNCH:
        case ANIM_HOP_FLIGHT:
        case ANIM_HOP_LANDING:
        case ANIM_RUN:
            if (m->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP) {
                a->gait = GaitPreset_BipedalHop(rig);
                a->gait.cycleDuration *= 0.85f;
                a->gait.strideLength *= 1.15f;
            } else {
                a->gait = GaitPreset_QuadrupedMotion(rig, agile, baseSpeed * 1.9f, 1.0f);
            }
            a->desiredVelocity = Vec3_Create(0, 0, baseSpeed * 1.9f);
            break;
        case ANIM_TURN_LEFT:
            a->desiredVelocity = Vec3_Create(0, 0, baseSpeed * 0.8f);
            a->angularVelocity = 1.25f;
            break;
        case ANIM_TURN_RIGHT:
            a->desiredVelocity = Vec3_Create(0, 0, baseSpeed * 0.8f);
            a->angularVelocity = -1.25f;
            break;
        case ANIM_LOOK_UP:
            a->lookDirection = Vec3_Normalize(Vec3_Create(0, 0.85f, 0.53f));
            break;
        case ANIM_LOOK_DOWN:
            a->lookDirection = Vec3_Normalize(Vec3_Create(0, -0.85f, 0.53f));
            break;
        case ANIM_LOOK_LEFT:
            a->lookDirection = Vec3_Normalize(Vec3_Create(0.90f, 0.0f, 0.44f));
            break;
        case ANIM_LOOK_RIGHT:
            a->lookDirection = Vec3_Normalize(Vec3_Create(-0.90f, 0.0f, 0.44f));
            break;
        case ANIM_LOOK_DOWN_LEFT:
            a->lookDirection = Vec3_Normalize(Vec3_Create(0.55f, -0.58f, 0.60f));
            break;
        case ANIM_LOOK_UP_LEFT:
            a->lookDirection = Vec3_Normalize(Vec3_Create(0.55f, 0.58f, 0.60f));
            break;
        case ANIM_EAT:
        case ANIM_DRINK: {
            float downFrac = (kind == ANIM_DRINK) ? -0.92f : -0.78f;
            float fwdFrac = (kind == ANIM_DRINK) ? 0.39f : 0.62f;
            Vector3 dir = Vec3_Normalize(Vec3_Create(0, downFrac, fwdFrac));
            Vector3 tpos = Vec3_Add(neckBaseRest, Vec3_Scale(dir, R * 0.78f));
            if (tpos.y < 0.08f * s) tpos.y = 0.08f * s;
            a->oralReachActive = true;
            a->oralReachTarget = tpos;
            a->interactionTarget = (InteractionTarget){
                .valid = true, .objectId = 101u + (uint32_t)kind,
                .type = (kind == ANIM_DRINK) ? INTERACTION_DRINK : INTERACTION_EAT,
                .position = tpos, .normal = {0, 1, 0},
                .approach = Vec3_Normalize(Vec3_Create(0, (kind == ANIM_DRINK) ? -0.88f : -0.70f, 0.50f)),
                .positionTolerance = 0.09f * s, .orientationTolerance = 0.7f
            };
            if (kind == ANIM_EAT && step > totalSteps / 2) {
                a->mouthOpen = 0.35f + 0.25f * sinf((float)step * 0.45f);
            }
            break;
        }
        case ANIM_ATTACK: {
            if (m->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP) {
                a->gait = GaitPreset_BipedalHop(rig);
            } else {
                a->gait = GaitPreset_QuadrupedMotion(rig, agile, baseSpeed * 1.4f, 0.7f);
            }
            a->desiredVelocity = Vec3_Create(0, 0, baseSpeed * 0.4f);
            Vector3 tpos = Vec3_Add(neckBaseRest, Vec3_Create(0, -0.25f * R, 0.92f * R));
            a->oralReachActive = true;
            a->oralReachTarget = tpos;
            a->interactionTarget = (InteractionTarget){
                .valid = true, .objectId = 201u, .type = INTERACTION_EAT,
                .position = tpos, .normal = {0, 0, -1},
                .approach = Vec3_Normalize(Vec3_Create(0, -0.18f, 0.98f)),
                .positionTolerance = 0.12f * s, .orientationTolerance = 0.6f
            };
            a->mouthOpen = 0.92f;
            break;
        }
        case ANIM_REACH_SIDE_LEFT: {
            Vector3 tpos = Vec3_Add(neckBaseRest, Vec3_Scale(Vec3_Normalize(Vec3_Create(0.76f, -0.10f, 0.64f)), R * 0.85f));
            a->oralReachActive = true;
            a->oralReachTarget = tpos;
            a->interactionTarget = (InteractionTarget){
                .valid = true, .objectId = 301u, .type = INTERACTION_EAT,
                .position = tpos, .normal = {0, 1, 0},
                .approach = Vec3_Normalize(Vec3_Create(0.75f, 0.0f, 0.66f)),
                .positionTolerance = 0.10f * s, .orientationTolerance = 0.75f
            };
            break;
        }
        case ANIM_REACH_UP: {
            Vector3 tpos = Vec3_Add(neckBaseRest, Vec3_Scale(Vec3_Normalize(Vec3_Create(0.0f, 0.68f, 0.73f)), R * 0.85f));
            a->oralReachActive = true;
            a->oralReachTarget = tpos;
            a->interactionTarget = (InteractionTarget){
                .valid = true, .objectId = 401u, .type = INTERACTION_EAT,
                .position = tpos, .normal = {0, -1, 0},
                .approach = Vec3_Normalize(Vec3_Create(0, 0.60f, 0.80f)),
                .positionTolerance = 0.10f * s, .orientationTolerance = 0.75f
            };
            break;
        }
        case ANIM_CURL:
            a->defensiveCurl = 1.0f;
            break;
        case ANIM_CURL_PARTIAL:
            a->defensiveCurl = .50f;
            break;
        case ANIM_WING_EXTENDED:
            a->wingFold = 0.0f;
            a->wingFlap = 0.08f;
            break;
        case ANIM_WING_PARTIAL:
            a->wingFold = 0.48f;
            a->wingFlap = -0.15f;
            break;
        case ANIM_WING_FOLDED:
            a->wingFold = 0.92f;
            a->wingFlap = -0.25f;
            break;
        case ANIM_WING_FLAP:
            a->wingFold = 0.08f;
            a->wingFlap = sinf((float)step * 0.25f) * 0.85f;
            break;
        default: break;
    }
}

static AABB3D ComputePosedBounds(const MonsterVisual* v, const Monster* m) {
    AABB3D b = AABB_Empty();
    for (size_t i = 0; i < v->mesh.vertexCount; ++i) AABB_ExpandPoint(&b, v->mesh.vertices[i].position);
    for (size_t i = 0; i < v->headMesh.vertexCount; ++i) AABB_ExpandPoint(&b, v->headMesh.vertices[i].position);
    for (size_t i = 0; i < v->membraneMesh.vertexCount; ++i) AABB_ExpandPoint(&b, v->membraneMesh.vertices[i].position);
    for (size_t i = 0; i < v->armorMesh.vertexCount; ++i) AABB_ExpandPoint(&b, v->armorMesh.vertices[i].position);
    if (m->animation) {
        for (size_t j = 0; j < m->animation->pose.jointCount; ++j)
            AABB_ExpandPoint(&b, m->animation->pose.joints[j].worldPosition);
    }
    return b;
}

static void ComputeFraming(const MonsterVisual* v, const Monster* m, const char* focus,
                           Vector3* outTarget, float* outHeight) {
    float s = m->phenotype.axial.totalScale;
    const Rig* r = &m->animation->rig;
    const SkeletonPose* p = &m->animation->pose;
    if (!strcmp(focus, "neck") && r->neckJoint >= 0) {
        Vector3 n = p->joints[r->neckJoint].worldPosition;
        Vector3 b = r->cervicalBaseJoint >= 0 ? p->joints[r->cervicalBaseJoint].worldPosition : n;
        Vector3 h = r->headJoint >= 0 ? p->joints[r->headJoint].worldPosition : n;
        *outTarget = Vec3_Scale(Vec3_Add(Vec3_Add(b, n), h), 1.0f / 3.0f);
        float span = Vec3_Distance(b, h) + 2.2f * s;
        *outHeight = fmaxf(3.0f * s, span);
        return;
    }
    if (!strcmp(focus, "head") && r->headJoint >= 0) {
        *outTarget = Vec3_Add(p->joints[r->headJoint].worldPosition,
                              Quat_RotateVector(p->joints[r->headJoint].worldRotation, Vec3_Scale(r->oralPoint, 0.35f)));
        *outHeight = 2.8f * s;
        return;
    }
    AABB3D b = ComputePosedBounds(v, m);
    *outTarget = Vec3_Scale(Vec3_Add(b.start, b.end), 0.5f);
    Vector3 ext = AABB_Size(b);
    *outHeight = fmaxf(3.5f * s, fmaxf(ext.y * 1.25f, fmaxf(ext.x, ext.z) * 0.95f));
}

static bool MatchPoseFraming(const char* name, Vector3* target, float* height) {
    const char* path = getenv("MONSTER_CAPTURE_CAMERA_REFERENCE");
    if (path) {
        FILE* input = fopen(path, "r");
        if (!input) return false;
        char line[256], pose[80];
        bool found = false;
        while (fgets(line, sizeof(line), input)) {
            Vector3 center;
            float fixedHeight;
            if (sscanf(line, "camera %79s %f %f %f %f", pose, &center.x, &center.y, &center.z,
                       &fixedHeight) == 5 && !strcmp(name, pose)) {
                *target = center;
                *height = fixedHeight;
                found = true;
                break;
            }
        }
        fclose(input);
        if (!found) return false;
    }
    printf("camera %s %.9g %.9g %.9g %.9g\n", name, target->x, target->y, target->z, *height);
    return true;
}

static bool CapturePose(Renderer3D* renderer, ICamera* camera, const MonsterVisual* visual,
                        Vector3 target, float height, const ViewSpec* view,
                        const char* directory, const char* name, int width, int pixelsHigh) {
    Vector3 dir = Vec3_Normalize(view->dir);
    camera->target = target;
    camera->position = Vec3_Add(target, Vec3_Scale(dir, 22.0f));
    camera->up = fabsf(dir.y) > 0.9f ? Vec3_Create(0, 0, 1) : Vec3_Create(0, 1, 0);
    renderer->beginFrame(renderer);
    OpenGLRenderer_SetupCameraOrtho(camera, height, width, pixelsHigh);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s_%s.ppm", directory, name, view->name);
    bool ok = MonsterVisual_Render(visual, renderer) && OpenGLRenderer_SavePPM(path, width, pixelsHigh);
    renderer->endFrame(renderer);
    return ok;
}

static void CenterTravel(MonsterAnimation* animation) {
    Vector3 shift = animation->animator.locomotion.bodyPosition;
    shift.y = 0;
    for (size_t j = 0; j < animation->pose.jointCount; ++j)
        animation->pose.joints[j].worldPosition = Vec3_Sub(animation->pose.joints[j].worldPosition, shift);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <receta|id|nombre> <out_dir> [--stage=0..3] [--anims=all|...] [--views=side,three-quarter,...] [--focus=body|neck|head] [--flat] [--strain] [--hop-cycle] [--width=900] [--height=900]\n", argv[0]);
        return 2;
    }
    const char* recipeArg = argv[1];
    const char* outDir = argv[2];
    int stage = 3, width = 900, height = 900;
    const char* animFilter = "all";
    const char* viewFilter = "side,three-quarter";
    const char* focus = "body";
    bool flat = false, strainOverlay = false, hopCycle = false;

    for (int i = 3; i < argc; ++i) {
        if (!strncmp(argv[i], "--stage=", 8)) stage = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--anims=", 8)) animFilter = argv[i] + 8;
        else if (!strncmp(argv[i], "--views=", 8)) viewFilter = argv[i] + 8;
        else if (!strncmp(argv[i], "--focus=", 8)) focus = argv[i] + 8;
        else if (!strcmp(argv[i], "--flat")) flat = true;
        else if (!strcmp(argv[i], "--strain")) strainOverlay = true;
        else if (!strcmp(argv[i], "--hop-cycle")) hopCycle = true;
        else if (!strncmp(argv[i], "--width=", 8)) width = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--height=", 9)) height = atoi(argv[i] + 9);
    }

    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    CreatureRecipe recipe = {0};
    if (!doc || !LoadRecipeArg(recipeArg, &recipe, doc)) { free(doc); return 1; }
    const CreaturePhenotype* phen = CreatureRecipe_GetStage(&recipe, (CreatureStage)stage);
    Monster* m = malloc(sizeof(*m));
    MonsterVisual* v = malloc(sizeof(*v));
    if (!m || !v) { free(doc); free(m); free(v); return 1; }
    *m = Monster_Create();
    if (!Creature_BuildMonster(m, &recipe, phen)) { free(doc); free(m); free(v); return 1; }
    if (!m->animation) {
        Rig fallbackRig = {0};
        if (!CreatureRig_Build(m, &fallbackRig) ||
            !MonsterAnimation_Configure(m, &fallbackRig, GaitPreset_CanidWalk(&fallbackRig))) {
            Monster_Free(m); free(v); free(m); free(doc); return 1;
        }
    }

    float scale = m->phenotype.axial.totalScale;
    SDFMesherConfig mcfg = SDFMesher_DefaultConfig();
    mcfg.voxelSize = 0.055f * scale;
    mcfg.maxCells = 2200000;
    mcfg.maxResolution = 384;
    *v = MonsterVisual_Create(mcfg);
    if (!MonsterVisual_RebuildNow(v, m, MonsterSDF_DefaultConfig())) {
        MonsterVisual_Free(v); Monster_Free(m); free(v); free(m); free(doc); return 1;
    }
    Mesh restCopy = Mesh_Create();
    Mesh_ReserveVertices(&restCopy, v->mesh.vertexCount);
    Mesh_ReserveIndices(&restCopy, v->mesh.indexCount);
    restCopy.vertexCount = v->mesh.vertexCount;
    restCopy.indexCount = v->mesh.indexCount;
    memcpy(restCopy.vertices, v->mesh.vertices, v->mesh.vertexCount * sizeof(MeshVertex));
    memcpy(restCopy.indices, v->mesh.indices, v->mesh.indexCount * sizeof(MeshIndex));
    if (flat) NeutralVisual(v);

    AnimatedVisual binding = {0};
    if (!AnimatedVisual_Bind(&binding, v, m)) {
        fprintf(stderr, "AnimatedVisual_Bind fallo\n");
        Mesh_Free(&restCopy); MonsterVisual_Free(v); Monster_Free(m); free(v); free(m); free(doc);
        return 1;
    }

    OpenGLDemoWindow* window = OpenGLDemoWindow_Create("Animaciones Monster Engine", width, height);
    if (!window) {
        AnimatedVisual_Free(&binding); Mesh_Free(&restCopy); MonsterVisual_Free(v); Monster_Free(m); free(v); free(m); free(doc);
        return 1;
    }
    ICamera camera = {.fov = 40, .aspectRatio = (float)width / (float)height, .nearPlane = 0.01f, .farPlane = 120.0f};
    Renderer3D renderer = OpenGLRenderer_Create(&camera);
    if (!OpenGLRenderer_SurfaceReady(&renderer)) {
        OpenGLRenderer_Destroy(&renderer); OpenGLDemoWindow_Free(window);
        AnimatedVisual_Free(&binding); Mesh_Free(&restCopy); MonsterVisual_Free(v); Monster_Free(m); free(v); free(m); free(doc);
        return 1;
    }

    char jsonPath[1024];
    snprintf(jsonPath, sizeof(jsonPath), "%s/metrics.json", outDir);
    FILE* jf = fopen(jsonPath, "w");
    if (jf) fprintf(jf, "{\n  \"species\": \"%s\",\n  \"recipeId\": %u,\n  \"stage\": %d,\n  \"animations\": {\n",
                    recipe.name, recipe.id, stage);
    bool firstJson = true, ok = true;

    for (int ai = 0; ai < ANIM_COUNT; ++ai) {
        if (!ContainsToken(animFilter, kAnimSpecs[ai].name)) continue;
        bool hopPose = ai >= ANIM_HOP_LAUNCH && ai <= ANIM_HOP_LANDING;
        if (hopPose && recipe.gait != CREATURE_GAIT_BIPEDAL_HOP) continue;
        /* Reiniciar la pose limpia antes de cada animación para reproductibilidad exacta */
        Monster_SetHeadOpenFactor(m, 0);
        GaitProfile baseGait = recipe.gait == CREATURE_GAIT_FELINE_STALK ? GaitPreset_FelineStalk(&m->animation->rig) :
                               recipe.gait == CREATURE_GAIT_BOVINE_WALK ? GaitPreset_BovineWalk(&m->animation->rig) :
                               recipe.gait == CREATURE_GAIT_SPRAWLING_QUADRUPED ? GaitPreset_SprawlingQuadruped(scale) :
                               recipe.gait == CREATURE_GAIT_BIPEDAL_HOP ? GaitPreset_BipedalHop(&m->animation->rig) :
                               GaitPreset_CanidWalk(&m->animation->rig);
        MonsterAnimation_Configure(m, &m->animation->rig, baseGait);
        m->animation->rigGeneration = binding.rigGeneration;
        int totalSteps = (ai == ANIM_IDLE) ? 12 : 90;
        for (int step = 0; step < totalSteps; ++step) {
            ConfigureAnimState(m, &m->animation->rig, &m->animation->animator, kAnimSpecs[ai].kind, step, totalSteps);
            MonsterAnimation_Update(m, 1.0f / 30.0f);
        }
        if ((ai == ANIM_RUN || hopPose) && recipe.gait == CREATURE_GAIT_BIPEDAL_HOP) {
            float targetPhase = ai == ANIM_HOP_LAUNCH ? .40f :
                ai == ANIM_HOP_LANDING ? .97f : .73f;
            for (int extra = 0; extra < 40; ++extra) {
                float ph = fmodf(m->animation->animator.locomotion.phase, 1.0f);
                if (fabsf(ph - targetPhase) <= .065f) break;
                ConfigureAnimState(m, &m->animation->rig, &m->animation->animator, ANIM_RUN, totalSteps, totalSteps);
                MonsterAnimation_Update(m, 1.0f / 30.0f);
            }
        }
        /* Anclar cuerpo en origen XZ para que la cámara no derive tras caminar/girar */
        CenterTravel(m->animation);

        ok = AnimatedVisual_Deform(&binding, v, m) && ok;
        if (flat) NeutralVisual(v);

        AnimPoseStats st;
        ComputeMetrics(&restCopy, &v->mesh, m, binding.body, &st);
        SurfaceStrain bodyStrain = MeasureSurface(&v->mesh, binding.body, scale, strainOverlay);
        SurfaceStrain membraneStrain = MeasureSurface(&v->membraneMesh, binding.membrane, scale, strainOverlay);
        SurfaceStrain armorStrain = MeasureSurface(&v->armorMesh, binding.armor, scale, strainOverlay);
        printf("%-16s | cbPitch=%+.2f cbYaw=%+.2f nPitch=%+.2f nYaw=%+.2f hPitch=%+.2f hYaw=%+.2f | edge=[%.2f..%.2f] area=[%.2f..%.2f] ext=%d crush=%d err=%.3f sat=%.2f\n",
               kAnimSpecs[ai].name, st.cervPitch, st.cervYaw, st.neckPitch, st.neckYaw, st.headPitch, st.headYaw,
               st.minEdgeRatio, st.maxEdgeRatio, st.minAreaRatio, st.maxAreaRatio,
               st.extremeTris, st.crushedTris, st.reachErr, st.reachSat);

        if (jf) {
            fprintf(jf, "%s    \"%s\": {\"cbPitch\": %.4f, \"cbYaw\": %.4f, \"neckPitch\": %.4f, \"neckYaw\": %.4f, \"headPitch\": %.4f, \"headYaw\": %.4f, "
                        "\"minEdge\": %.4f, \"maxEdge\": %.4f, \"minArea\": %.4f, \"maxArea\": %.4f, \"extremeTris\": %d, \"crushedTris\": %d, "
                        "\"checkedTris\": %d, \"reachErr\": %.4f, \"reachSat\": %.4f, \"gaitPhase\": %.4f",
                    firstJson ? "" : ",\n", kAnimSpecs[ai].name,
                    st.cervPitch, st.cervYaw, st.neckPitch, st.neckYaw, st.headPitch, st.headYaw,
                    st.minEdgeRatio, st.maxEdgeRatio, st.minAreaRatio, st.maxAreaRatio,
                    st.extremeTris, st.crushedTris, st.checkedTris, st.reachErr, st.reachSat,
                    m->animation->animator.locomotion.phase);
            WriteSurfaceStrain(jf, "body", bodyStrain);
            WriteSurfaceStrain(jf, "membranes", membraneStrain);
            WriteSurfaceStrain(jf, "armor", armorStrain);
            fputc('}', jf);
            firstJson = false;
        }

        Vector3 target;
        float viewH;
        ComputeFraming(v, m, focus, &target, &viewH);
        ok = MatchPoseFraming(kAnimSpecs[ai].name, &target, &viewH) && ok;
        for (size_t vi = 0; vi < sizeof(kViews) / sizeof(kViews[0]); ++vi) {
            if (!ContainsToken(viewFilter, kViews[vi].name)) continue;
            ok = CapturePose(&renderer, &camera, v, target, viewH, &kViews[vi], outDir,
                             kAnimSpecs[ai].name, width, height) && ok;
        }
    }
    if (jf) { fprintf(jf, "\n  }\n}\n"); fclose(jf); }

    if (hopCycle && recipe.gait == CREATURE_GAIT_BIPEDAL_HOP) {
        const int samples = 12, steps = 480;
        int captured = 0;
        float previous = 0;
        bool started = false;
        Vector3 target;
        float viewH;
        ComputeFraming(v, m, "body", &target, &viewH);
        ok = MatchPoseFraming("hop_cycle", &target, &viewH) && ok;
        MonsterAnimation_Configure(m, &m->animation->rig, GaitPreset_BipedalHop(&m->animation->rig));
        m->animation->rigGeneration = binding.rigGeneration;
        for (int step = 0; step < steps && captured < samples; ++step) {
            ConfigureAnimState(m, &m->animation->rig, &m->animation->animator, ANIM_RUN, step, steps);
            ok = MonsterAnimation_Update(m, 1.0f / 120.0f) && ok;
            float phase = m->animation->animator.locomotion.phase;
            if (step > 60 && phase < previous) started = true;
            previous = phase;
            if (!started || phase < (float)captured / samples) continue;
            CenterTravel(m->animation);
            ok = AnimatedVisual_Deform(&binding, v, m) && ok;
            if (flat) NeutralVisual(v);
            char name[32];
            snprintf(name, sizeof(name), "hop_cycle_%02d", captured++);
            for (size_t i = 0; i < sizeof(kViews) / sizeof(kViews[0]); ++i) {
                if (ContainsToken(viewFilter, kViews[i].name))
                    ok = CapturePose(&renderer, &camera, v, target, viewH, &kViews[i], outDir,
                                     name, width, height) && ok;
            }
        }
        ok = captured == samples && ok;
    }

    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    AnimatedVisual_Free(&binding);
    Mesh_Free(&restCopy);
    MonsterVisual_Free(v);
    Monster_Free(m);
    free(v);
    free(m);
    free(doc);
    return ok ? 0 : 1;
}
