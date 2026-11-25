/** @file EcosystemAnatomy.c
 * @brief Alcance cervical autoritativo sobre plantillas compartidas y cadena compacta. */
#include "EcosystemAnatomy.h"
#include "VisualVariation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Vector3 YawFlat(Vector3 v, float a) {
    float c = cosf(a), s = sinf(a);
    return Vec3_Create(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

static bool isFlatNormal(const EcosystemEntity *e) {
    float nLenSq = Vec3_LengthSq(e->body.groundNormal);
    return (nLenSq < 0.25f || e->body.groundNormal.y >= 0.9999f);
}

static Quaternion bodyFrame(const EcosystemEntity *e) {
    if (isFlatNormal(e)) {
        return Quat_FromAxisAngle(Vec3_Create(0.0f, 1.0f, 0.0f), e->body.yaw);
    }
    Quaternion q = e->body.orientation;
    if (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w > 0.5f) {
        return q;
    }
    return World_BuildBodyFrame(e->body.yaw, e->body.groundNormal, 0.65f);
}

float EcosystemAnatomy_ModelLength(const CreaturePhenotype *p) {
    return (p->axial.trunkLength + p->axial.neckLength +
            2.2f * p->headEnvelope.scale * p->headEnvelope.lengthScale) *
           p->axial.totalScale;
}

float EcosystemAnatomy_Scale(const EcosystemAnatomy *a, const EcosystemEntity *e) {
    return 2.05f / a->modelLength * e->genome.size * e->developmentScale;
}

void EcosystemAnatomy_Destroy(EcosystemAnatomy *a) {
    if (!a) return;
    EcosystemAnatomyCache_Release(a->tmpl);
    free(a);
}

void EcosystemAnatomy_InvalidateInteraction(EcosystemAnatomy *a) {
    if (!a) return;
    AnatomicalReach_InvalidateCache(&a->state);
    a->settled = false;
}

bool EcosystemAnatomy_ConfigureProfiled(EcosystemEntity *e, const EcosystemSpecies *s,
                                        EvolutionProfiler *profiler) {
    if (!s->anatomicalOral) return true;
    unsigned stage = e->physiology.stage == LIFE_JUVENILE ? CREATURE_STAGE_JUVENILE : CREATURE_STAGE_ADULT;
    unsigned bucket = VisualVariation_Profile(&e->genome, e->seed).bucket;
    double maturity = s->physiology.maturityAge * e->genome.maturation;
    double normalizedAge = (maturity > 1e-6) ? fmin(1.0, fmax(0.0, e->physiology.age / maturity)) : 1.0;
    uint32_t growthStep = EcosystemAnatomyCache_GrowthStep(normalizedAge);

    EcosystemAnatomy *a = e->anatomicalBody;
    if (a && a->tmpl && a->tmpl->key.recipeId == s->recipeId &&
        a->stage == stage && a->bucket == bucket && a->growthStep == growthStep) {
        return true;
    }

    const EcosystemAnatomyTemplate *tmpl =
        EcosystemAnatomyCache_Acquire(s->recipeId, bucket, growthStep, profiler);
    if (!tmpl) return false;

    if (!a) {
        a = calloc(1, sizeof(*a));
        if (!a) {
            EcosystemAnatomyCache_Release(tmpl);
            return false;
        }
        if (profiler) EvolutionProfiler_AddAtomic(&profiler->counters.allocations, 1u);
        a->tmpl = tmpl;
        a->center = tmpl->center;
        a->modelLength = tmpl->modelLength;
        a->standingHeight = tmpl->standingHeight;
        a->stage = stage;
        a->bucket = bucket;
        a->growthStep = growthStep;
        a->growthSample = normalizedAge;
        a->generation = 1u;
        a->crouch = 0.0f;
        AnatomicalReach_Reset(&a->state, &tmpl->profile, NULL);
        for (unsigned k = 0; k < tmpl->profile.count; ++k) {
            int j = tmpl->profile.joints[k].index;
            a->cervicalWorldPos[k] = tmpl->standingRest.joints[j].worldPosition;
            a->cervicalWorldRot[k] = tmpl->standingRest.joints[j].worldRotation;
        }
        a->settled = true;
        e->anatomicalBody = a;
        return true;
    }

    /* Reutilizar el almacenamiento por entidad cambiando la plantilla compartida sin malloc/free. */
    const EcosystemAnatomyTemplate *prev = a->tmpl;
    bool compatibleTopology = (prev && prev->topologyHash == tmpl->topologyHash &&
                               prev->profile.count == tmpl->profile.count);
    Quaternion preserved[REACH_MAX_JOINTS];
    if (compatibleTopology) {
        for (unsigned k = 0; k < tmpl->profile.count; ++k) {
            preserved[k] = a->state.rotation[k];
        }
    }

    a->tmpl = tmpl;
    a->center = tmpl->center;
    a->modelLength = tmpl->modelLength;
    a->standingHeight = tmpl->standingHeight;
    a->stage = stage;
    a->bucket = bucket;
    a->growthStep = growthStep;
    a->growthSample = normalizedAge;
    ++a->generation;

    AnatomicalReach_Reset(&a->state, &tmpl->profile, NULL);
    if (compatibleTopology) {
        for (unsigned k = 0; k < tmpl->profile.count; ++k) {
            a->state.rotation[k] = Skeleton_ConstrainRotation(&tmpl->profile.joints[k].bounded, preserved[k]);
            a->state.desired[k] = a->state.rotation[k];
        }
    }
    for (unsigned k = 0; k < tmpl->profile.count; ++k) {
        int j = tmpl->profile.joints[k].index;
        a->cervicalWorldPos[k] = tmpl->standingRest.joints[j].worldPosition;
        a->cervicalWorldRot[k] = tmpl->standingRest.joints[j].worldRotation;
    }
    a->settled = false;
    EcosystemAnatomyCache_Release(prev);
    return true;
}

bool EcosystemAnatomy_Configure(EcosystemEntity *e, const EcosystemSpecies *s) {
    return EcosystemAnatomy_ConfigureProfiled(e, s, NULL);
}

void EcosystemAnatomy_UpdateProfiled(EcosystemEntity *e, float dt, EvolutionProfiler *profiler) {
    EcosystemAnatomy *a = e->anatomicalBody;
    if (!a || !a->tmpl) return;
    const EcosystemAnatomyTemplate *tmpl = a->tmpl;
    const AnatomicalReachProfile *profile = &tmpl->profile;
    float scale = EcosystemAnatomy_Scale(a, e);

    InteractionTarget target = e->mind.intent.interaction;
    bool active = e->mind.intent.interact && target.valid;

    float desiredCrouch = e->mind.action == ACTION_REST ? profile->maximumCrouch * 0.60f :
        e->mind.action == ACTION_ATTACK ? profile->maximumCrouch * 0.46f :
        (e->mind.action == ACTION_EAT || e->mind.action == ACTION_DRINK) ? profile->maximumCrouch * 0.45f : 0.0f;

    /* Camino rápido sin interacción oral cuando la postura cervical ya está asentada en reposo. */
    bool flat = isFlatNormal(e);
    Quaternion orient = flat ? Quat_Identity() : bodyFrame(e);
    if (!active && a->settled && fabsf(desiredCrouch - a->crouch) < 1e-4f) {
        a->crouch = desiredCrouch;
        Vector3 crouchShift = Vec3_Create(0.0f, -a->crouch, 0.0f);
        Vector3 mouthModel = Vec3_Add(tmpl->neutralMouthLocal, crouchShift);
        Vector3 pivotModel = Vec3_Add(tmpl->neutralPivotLocal, crouchShift);
        Vector3 elbowModel = Vec3_Add(tmpl->neutralElbowLocal, crouchShift);
        Vector3 mouth, dir, pivot, elbow;
        if (flat) {
            float c = cosf(e->body.yaw), s = sinf(e->body.yaw);
            Vector3 mRel = Vec3_Scale(Vec3_Sub(mouthModel, a->center), scale);
            mouth = Vec3_Add(e->body.position, Vec3_Create(c * mRel.x + s * mRel.z, mRel.y, -s * mRel.x + c * mRel.z));
            Vector3 d = tmpl->neutralMouthDir;
            dir = Vec3_Create(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
            Vector3 pRel = Vec3_Scale(Vec3_Sub(pivotModel, a->center), scale);
            pivot = Vec3_Add(e->body.position, Vec3_Create(c * pRel.x + s * pRel.z, pRel.y, -s * pRel.x + c * pRel.z));
            Vector3 eRel = Vec3_Scale(Vec3_Sub(elbowModel, a->center), scale);
            elbow = Vec3_Add(e->body.position, Vec3_Create(c * eRel.x + s * eRel.z, eRel.y, -s * eRel.x + c * eRel.z));
        } else {
            mouth = World_LocalToWorld(mouthModel, a->center, scale, orient, e->body.position);
            dir = World_LocalDirToWorld(tmpl->neutralMouthDir, orient);
            pivot = World_LocalToWorld(pivotModel, a->center, scale, orient, e->body.position);
            elbow = World_LocalToWorld(elbowModel, a->center, scale, orient, e->body.position);
        }
        e->oralMotion = dt > 0.0f ? Vec3_Distance(mouth, e->body.mouth) / dt : 0.0f;
        e->body.mouth = mouth;
        e->body.mouthDirection = dir;
        e->oralPivot = pivot;
        e->oralElbow = elbow;
        e->body.reach = (ReachResult){
            .status = REACH_INVALID,
            .mouth = mouth,
            .direction = e->body.mouthDirection
        };
        return;
    }

    if (active) {
        if (flat) {
            target.position = Vec3_Add(a->center,
                                       Vec3_Scale(YawFlat(Vec3_Sub(target.position, e->body.position), -e->body.yaw),
                                                  1.0f / scale));
            target.approach = YawFlat(target.approach, -e->body.yaw);
            target.normal = YawFlat(target.normal, -e->body.yaw);
        } else {
            target.position = World_WorldToLocal(target.position, a->center, scale, orient, e->body.position);
            target.approach = World_WorldDirToLocal(target.approach, orient);
            target.normal = World_WorldDirToLocal(target.normal, orient);
        }
        target.positionTolerance /= scale;

        /* Posición base cervical antes del agachamiento actual. */
        int baseJ = profile->joints[0].index;
        Vector3 baseStanding = tmpl->standingRest.joints[baseJ].worldPosition;
        Vector3 delta = Vec3_Sub(baseStanding, target.position);
        float reach = profile->maximumReach * 0.68f;
        float planar = delta.x * delta.x + delta.z * delta.z;
        if (delta.y > 0.75f)
            desiredCrouch = fmaxf(desiredCrouch, profile->maximumCrouch * 0.78f);
        if (planar < reach * reach && delta.y > 0.0f)
            desiredCrouch = fmaxf(desiredCrouch, delta.y - sqrtf(reach * reach - planar) + 0.04f);
    }

    desiredCrouch = fminf(profile->maximumCrouch, desiredCrouch);
    a->crouch += (desiredCrouch - a->crouch) * (1.0f - expf(-9.0f * dt));

    Vector3 baseParentPos = tmpl->cervicalParentStandingPos;
    baseParentPos.y -= a->crouch;
    Quaternion baseParentRot = tmpl->cervicalParentStandingRot;

    unsigned solveCalls = 0u, fkUpdates = 0u;
    ReachResult reach = AnatomicalReach_UpdateCompact(
        profile, &a->state, baseParentPos, baseParentRot,
        active ? &target : NULL, dt,
        a->cervicalWorldPos, a->cervicalWorldRot,
        &solveCalls, &fkUpdates);

    if (profiler) {
        if (solveCalls) EvolutionProfiler_AddAtomic(&profiler->counters.anatomicalSolverInvocations, solveCalls);
        if (reach.iterations) EvolutionProfiler_AddAtomic(&profiler->counters.anatomicalSolverIterations, reach.iterations);
        if (fkUpdates) EvolutionProfiler_AddAtomic(&profiler->counters.skeletonTransformUpdates, fkUpdates);
    }

    /* Detectar asentamiento en reposo para activar el camino rápido en ticks sucesivos. */
    if (!active && fabsf(desiredCrouch - a->crouch) < 1e-4f && reach.stability < 1e-4f) {
        bool atNeutral = true;
        for (unsigned k = 0; k < profile->count; ++k) {
            float dot = fabsf(a->state.rotation[k].x * profile->joints[k].neutral.x +
                              a->state.rotation[k].y * profile->joints[k].neutral.y +
                              a->state.rotation[k].z * profile->joints[k].neutral.z +
                              a->state.rotation[k].w * profile->joints[k].neutral.w);
            if (dot < 0.999999f) { atNeutral = false; break; }
        }
        a->settled = atNeutral;
    } else {
        a->settled = false;
    }

    Vector3 mouth = flat
        ? Vec3_Add(e->body.position, YawFlat(Vec3_Scale(Vec3_Sub(reach.mouth, a->center), scale), e->body.yaw))
        : World_LocalToWorld(reach.mouth, a->center, scale, orient, e->body.position);
    e->oralMotion = dt > 0.0f ? Vec3_Distance(mouth, e->body.mouth) / dt : 0.0f;
    e->body.mouth = mouth;
    e->body.mouthDirection = flat ? YawFlat(reach.direction, e->body.yaw)
                                  : World_LocalDirToWorld(reach.direction, orient);

    Vector3 pivotModel = (tmpl->pivotChainIndex >= 0)
                             ? a->cervicalWorldPos[tmpl->pivotChainIndex]
                             : Vec3_Add(tmpl->neutralPivotLocal, Vec3_Create(0.0f, -a->crouch, 0.0f));
    Vector3 elbowModel = (tmpl->elbowChainIndex >= 0)
                             ? a->cervicalWorldPos[tmpl->elbowChainIndex]
                             : Vec3_Add(tmpl->neutralElbowLocal, Vec3_Create(0.0f, -a->crouch, 0.0f));
    e->oralPivot = flat
        ? Vec3_Add(e->body.position, YawFlat(Vec3_Scale(Vec3_Sub(pivotModel, a->center), scale), e->body.yaw))
        : World_LocalToWorld(pivotModel, a->center, scale, orient, e->body.position);
    e->oralElbow = flat
        ? Vec3_Add(e->body.position, YawFlat(Vec3_Scale(Vec3_Sub(elbowModel, a->center), scale), e->body.yaw))
        : World_LocalToWorld(elbowModel, a->center, scale, orient, e->body.position);

    reach.mouth = mouth;
    reach.direction = e->body.mouthDirection;
    reach.error *= scale;
    e->body.reach = reach;
    if (active) e->body.reach.objectId = target.objectId;
}

void EcosystemAnatomy_Update(EcosystemEntity *e, float dt) {
    EcosystemAnatomy_UpdateProfiled(e, dt, NULL);
}

void EcosystemAnatomy_ApplyCervicalPose(const EcosystemAnatomy *body, const Rig *rig, SkeletonPose *pose) {
    if (!body || !body->tmpl || !pose) return;
    const AnatomicalReachProfile *profile = &body->tmpl->profile;
    for (unsigned k = 0; k < profile->count; ++k) {
        int j = profile->joints[k].index;
        if (j >= 0 && (size_t)j < pose->jointCount) {
            pose->joints[j].localRotation = body->state.rotation[k];
        }
    }
    if (rig) {
        for (size_t k = 0; k < rig->spineCount; ++k) {
            int j = rig->spine[k];
            if (j >= 0 && (size_t)j < pose->jointCount && (size_t)j < body->tmpl->rest.jointCount) {
                /* Si el eslabón de columna forma parte de la cadena cervical ya quedó aplicado arriba. */
                bool inChain = false;
                for (unsigned c = 0; c < profile->count; ++c) {
                    if (profile->joints[c].index == j) { inChain = true; break; }
                }
                if (!inChain) {
                    pose->joints[j].localRotation = body->tmpl->rest.joints[j].localRotation;
                }
            }
        }
    }
}
