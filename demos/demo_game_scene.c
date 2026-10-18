/**
 * @file demo_game_scene.c
 * @brief Demo visual del juego con vista ortogonal/isométrica, plano de terreno 3D
 *        y criaturas (perro y lagarto) navegando con animación procedural continua.
 * @author Monster Engine Team
 * @date 2026
 */

#include "AnimatedVisual.h"
#include "Creature.h"
#include "CreatureActions.h"
#include "CreatureBoundary.h"
#include "CreatureMind.h"
#include "CreatureRecipes.h"
#include "CreatureRig.h"
#include "CreatureVariation.h"
#include "GaitPresets.h"
#include "LizardVariations.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "MonsterAnimation.h"
#include "NavigationProgress.h"
#include "OpenGLRenderer.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static AIWorld aiWorld;
static AIObjectId pondId;
static float stageHalf = 28.0f;
#define STAGE_HALF stageHalf
#define STAGE_HEIGHT 0.8f
#define TURN_MARGIN 2.6f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @struct StageActor
 * @brief Estado de simulación y navegación de una criatura en el plano.
 */
typedef struct StageActor {
    const char *name;
    CreatureMind mind;
    CreatureBodyState aiBody;
    CreatureMotorCommand aiMotor;
    bool aiEnabled, aiViolation;
    double aiMs;
    unsigned aiRigGeneration;
    int aiNodeJoints[ANATOMY_MAX_NODES];
    int aiConnectionFrom[ANATOMY_MAX_CONNECTIONS], aiConnectionTo[ANATOMY_MAX_CONNECTIONS];
    float aiRequestedSpeed;
    Monster monster;
    MonsterVisual visual;
    AnimatedVisual binding;
    Vector3 position;          /**< Posición en el plano mundial (X, Y=0, Z) */
    float yaw;                 /**< Orientación actual en radianes */
    float targetYaw;           /**< Orientación deseada en radianes */
    float speed;               /**< Velocidad de traslación hacia adelante */
    float turnRate;            /**< Velocidad angular máxima (rad/s) */
    float currentTurnRate;     /**< Velocidad angular actual de viraje (rad/s) */
    float yawDelta;            /**< Giro real que transporta los contactos de este paso. */
    float wanderTimer;         /**< Temporizador para cambios aleatorios de rumbo */
    float lookOffset;          /**< Anticipación cefálica de giro */
    float scaleFactor;         /**< Escala geométrica para morphing uniforme */
    CreatureBoundary boundary; /**< Contacto de todo el volumen articulado. */
    float effectiveSpeed;
    float collisionPadding;
    double solveMs, animationMs, deformMs;
    bool gpuSkinning;
    Vector3 escapeWaypoint;
    bool avoiding;
    NavigationProgress goalProgress;
    unsigned goalArrivals, goalRecoveries;
    float maxGoalStall;
    float avoidHold, turnSign, blockedTime, maxBlockedTime, backupTimer;
    float pondBlockedTime;
    float travelled, projectionDistance, maxProjection, stepProjection, nativeProgress;
    Color debugColor;
} StageActor;

/**
 * @struct MutationState
 * @brief Estado de la metamorfosis procedural activa del lagarto con MonsterAger.
 */
typedef struct MutationState {
    bool active;

    int currentVariantIndex;
    int targetVariantIndex;
    uint32_t seed;

    float progress; /**< Avance normalizado de la transición [0.0 - 1.0] */
    float duration; /**< Duración total en segundos */

    Monster sourceMonster;
    Monster targetMonster;
    MonsterAger ager;

    float sourceSpeed;
    float targetSpeed;
    float sourceTurnRate;
    float targetTurnRate;

    GaitProfile sourceGait;
    GaitProfile targetGait;

    Monster materialReference;
    MonsterSDF sdf;
    GpuSDFSurfaceCache *cache;

    char statusText[128];
} MutationState;

/**
 * @brief Interpola linealmente dos perfiles de marcha procedural.
 */
static GaitProfile GaitProfile_Lerp(GaitProfile a, GaitProfile b, float t) {
    GaitProfile r;
    r.cycleDuration = a.cycleDuration + t * (b.cycleDuration - a.cycleDuration);
    r.strideLength = a.strideLength + t * (b.strideLength - a.strideLength);
    r.stepHeight = a.stepHeight + t * (b.stepHeight - a.stepHeight);
    r.stanceRatio = a.stanceRatio + t * (b.stanceRatio - a.stanceRatio);
    for (int i = 0; i < RIG_MAX_LIMBS; ++i) {
        r.limbPhase[i] = a.limbPhase[i] + t * (b.limbPhase[i] - a.limbPhase[i]);
    }
    r.bodyBob = a.bodyBob + t * (b.bodyBob - a.bodyBob);
    r.bodySway = a.bodySway + t * (b.bodySway - a.bodySway);
    r.spineWave = a.spineWave + t * (b.spineWave - a.spineWave);
    r.tailCounterSwing = a.tailCounterSwing + t * (b.tailCounterSwing - a.tailCounterSwing);
    r.spineTurn = a.spineTurn + t * (b.spineTurn - a.spineTurn);
    r.tailTurn = a.tailTurn + t * (b.tailTurn - a.tailTurn);
    r.bodyBank = a.bodyBank + t * (b.bodyBank - a.bodyBank);
    r.supportCompression = a.supportCompression + t * (b.supportCompression - a.supportCompression);
    return r;
}

/**
 * @brief Añade un cuadrilátero a la malla del escenario mediante dos triángulos.
 */
static void AddStageQuad(Mesh *m, Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3, Vector3 n,
                         Color c) {
    MeshIndex i0, i1, i2, i3;
    MeshVertex vert0 = {.position = v0, .normal = n, .color = c};
    MeshVertex vert1 = {.position = v1, .normal = n, .color = c};
    MeshVertex vert2 = {.position = v2, .normal = n, .color = c};
    MeshVertex vert3 = {.position = v3, .normal = n, .color = c};
    Mesh_AddVertex(m, vert0, &i0);
    Mesh_AddVertex(m, vert1, &i1);
    Mesh_AddVertex(m, vert2, &i2);
    Mesh_AddVertex(m, vert3, &i3);
    Mesh_AddTriangle(m, i0, i1, i2);
    Mesh_AddTriangle(m, i0, i2, i3);
}

/**
 * @brief Construye una malla 3D para el escenario (podio con baldosas y faldón lateral).
 */
static Mesh BuildStageMesh(float halfSize, float height, int cells) {
    Mesh m = Mesh_Create();
    float cellSize = (halfSize * 2.0f) / (float)cells;
    size_t tileCount = (size_t)cells * (size_t)cells;
    size_t sideQuads = (size_t)cells * 4;
    size_t totalQuads = tileCount + sideQuads;

    Mesh_ReserveVertices(&m, totalQuads * 4);
    Mesh_ReserveIndices(&m, totalQuads * 6);

    /* Baldosas superiores (Y = 0) */
    for (int gz = 0; gz < cells; ++gz) {
        for (int gx = 0; gx < cells; ++gx) {
            float x0 = -halfSize + (float)gx * cellSize;
            float x1 = x0 + cellSize;
            float z0 = -halfSize + (float)gz * cellSize;
            float z1 = z0 + cellSize;

            /* Determinar si es borde perimetral exterior */
            bool isBorder = (gx == 0 || gx == cells - 1 || gz == 0 || gz == cells - 1);
            bool isCenterSub = ((gx + gz) % 2 == 0);

            Color color;
            if (isBorder) {
                color = Color_FromRGB(50, 54, 64);
            } else if (isCenterSub) {
                color = Color_FromRGB(82, 88, 102);
            } else {
                color = Color_FromRGB(74, 80, 93);
            }

            /* Resaltar cuadrícula principal cada 4 baldosas */
            if (!isBorder && (gx % 4 == 0 || gz % 4 == 0)) {
                color = Color_FromRGB(color.r - 8, color.g - 8, color.b - 8);
            }

            Vector3 n = Vec3_Create(0, 1, 0);
            Vector3 v0 = Vec3_Create(x0, 0, z0);
            Vector3 v1 = Vec3_Create(x1, 0, z0);
            Vector3 v2 = Vec3_Create(x1, 0, z1);
            Vector3 v3 = Vec3_Create(x0, 0, z1);

            AddStageQuad(&m, v0, v1, v2, v3, n, color);
        }
    }

    /* Faldones laterales (4 paredes del podio, de Y=0 a Y=-height) */
    Color sideColor = Color_FromRGB(38, 41, 50);
    Color sideBevelColor = Color_FromRGB(30, 33, 40);

    /* Pared Norte (Z = -halfSize) */
    for (int gx = 0; gx < cells; ++gx) {
        float x0 = -halfSize + (float)gx * cellSize;
        float x1 = x0 + cellSize;
        Vector3 n = Vec3_Create(0, 0, -1);
        Vector3 v0 = Vec3_Create(x0, 0, -halfSize);
        Vector3 v1 = Vec3_Create(x1, 0, -halfSize);
        Vector3 v2 = Vec3_Create(x1, -height, -halfSize);
        Vector3 v3 = Vec3_Create(x0, -height, -halfSize);
        AddStageQuad(&m, v0, v1, v2, v3, n, (gx % 2 == 0) ? sideColor : sideBevelColor);
    }

    /* Pared Sur (Z = +halfSize) */
    for (int gx = 0; gx < cells; ++gx) {
        float x0 = -halfSize + (float)gx * cellSize;
        float x1 = x0 + cellSize;
        Vector3 n = Vec3_Create(0, 0, 1);
        Vector3 v0 = Vec3_Create(x1, 0, halfSize);
        Vector3 v1 = Vec3_Create(x0, 0, halfSize);
        Vector3 v2 = Vec3_Create(x0, -height, halfSize);
        Vector3 v3 = Vec3_Create(x1, -height, halfSize);
        AddStageQuad(&m, v0, v1, v2, v3, n, (gx % 2 == 0) ? sideColor : sideBevelColor);
    }

    /* Pared Oeste (X = -halfSize) */
    for (int gz = 0; gz < cells; ++gz) {
        float z0 = -halfSize + (float)gz * cellSize;
        float z1 = z0 + cellSize;
        Vector3 n = Vec3_Create(-1, 0, 0);
        Vector3 v0 = Vec3_Create(-halfSize, 0, z1);
        Vector3 v1 = Vec3_Create(-halfSize, 0, z0);
        Vector3 v2 = Vec3_Create(-halfSize, -height, z0);
        Vector3 v3 = Vec3_Create(-halfSize, -height, z1);
        AddStageQuad(&m, v0, v1, v2, v3, n, (gz % 2 == 0) ? sideColor : sideBevelColor);
    }

    /* Pared Este (X = +halfSize) */
    for (int gz = 0; gz < cells; ++gz) {
        float z0 = -halfSize + (float)gz * cellSize;
        float z1 = z0 + cellSize;
        Vector3 n = Vec3_Create(1, 0, 0);
        Vector3 v0 = Vec3_Create(halfSize, 0, z0);
        Vector3 v1 = Vec3_Create(halfSize, 0, z1);
        Vector3 v2 = Vec3_Create(halfSize, -height, z1);
        Vector3 v3 = Vec3_Create(halfSize, -height, z0);
        AddStageQuad(&m, v0, v1, v2, v3, n, (gz % 2 == 0) ? sideColor : sideBevelColor);
    }

    return m;
}

/**
 * @brief Normaliza un ángulo al intervalo [-pi, pi].
 */
static float WrapAngle(float a) {
    while (a > (float)M_PI)
        a -= (float)(2.0 * M_PI);
    while (a < (float)-M_PI)
        a += (float)(2.0 * M_PI);
    return a;
}

/** @brief Capacidad de avance y giro según la zancada y el radio de los apoyos. */
static void ActorMotionLimits(StageActor *actor, float *maximumSpeed, float *maximumTurn,
                              float *radius) {
    MonsterAnimation *animation = actor->monster.animation;
    const GaitProfile *gait = &animation->animator.gait;
    float maxSpeed =
        actor->scaleFactor * gait->strideLength / (gait->cycleDuration * gait->stanceRatio);
    SkeletonPose rest;
    SkeletonPose_Init(&rest, &animation->rig.skeleton);
    float footRadius = 0;
    for (size_t i = 0; i < animation->rig.limbCount; ++i)
        if (animation->rig.limbs[i].walking) {
            Vector3 p =
                Vec3_Sub(rest.joints[animation->rig.limbs[i].endEffectorJoint].worldPosition,
                         animation->animator.locomotion.supportCenter);
            footRadius = fmaxf(footRadius, sqrtf(p.x * p.x + p.z * p.z) * actor->scaleFactor);
        }
    *maximumSpeed = maxSpeed;
    *maximumTurn = fminf(actor->turnRate, .65f * maxSpeed / fmaxf(footRadius, .1f));
    *radius = footRadius;
}

/**
 * @brief Actualiza la IA y cinemática de navegación de una criatura para que no salga del plano.
 */
static void UpdateActorNavigation(StageActor *actor, float dt) {
    float maxSpeed, turnLimit, footRadius;
    ActorMotionLimits(actor, &maxSpeed, &turnLimit, &footRadius);
    float cruiseSpeed = fminf(actor->speed, maxSpeed);
    if (actor->aiEnabled)
        actor->targetYaw = actor->aiMotor.heading;
    bool aiSteering = actor->aiEnabled && (!actor->mind.intent.moving || actor->mind.intent.reverse);
    if(aiSteering) {
        actor->avoiding=false;
        actor->backupTimer=0;
    }
    /* Vector frontal según rumbo actual */
    float fwdX = sinf(actor->yaw);
    float fwdZ = cosf(actor->yaw);

    AABB3D body =
        CreatureBoundary_Bounds(&actor->boundary, actor->monster.animation, actor->position,
                                actor->yaw, actor->scaleFactor, actor->collisionPadding);
    Vector3 predictedPosition = Vec3_Add(
        actor->position, Vec3_Create(fwdX * (actor->aiEnabled?actor->aiMotor.speed:actor->speed) * .8f,
                                    0, fwdZ * (actor->aiEnabled?actor->aiMotor.speed:actor->speed) * .8f));
    AABB3D predicted = CreatureBoundary_Bounds(
        &actor->boundary, actor->monster.animation, predictedPosition,
        actor->yaw + actor->currentTurnRate * .6f, actor->scaleFactor, actor->collisionPadding);
    float distances[] = {STAGE_HALF - fmaxf(body.end.x, predicted.end.x),
                         fminf(body.start.x, predicted.start.x) + STAGE_HALF,
                         STAGE_HALF - fmaxf(body.end.z, predicted.end.z),
                         fminf(body.start.z, predicted.start.z) + STAGE_HALF};
    float nearest = fminf(fminf(distances[0], distances[1]), fminf(distances[2], distances[3]));
    float span = fmaxf(AABB_Size(body).x, AABB_Size(body).z);
    float margin =
        fmaxf(TURN_MARGIN, fminf(10.f, span * .35f + cruiseSpeed / fmaxf(turnLimit, .01f)));
    float reachX =
        fmaxf(fabsf(body.start.x - actor->position.x), fabsf(body.end.x - actor->position.x));
    float reachZ =
        fmaxf(fabsf(body.start.z - actor->position.z), fabsf(body.end.z - actor->position.z));
    float turnRadius = sqrtf(reachX * reachX + reachZ * reachZ);
    if (actor->aiEnabled) {
        double start = OpenGLDemoWindow_Time();
        turnRadius = CreatureBoundary_TurningRadius(&actor->boundary, actor->monster.animation,
                                                    actor->scaleFactor, actor->collisionPadding);
        actor->aiMs += (OpenGLDemoWindow_Time() - start) * 1000;
    }

    float turningClearance =
        STAGE_HALF - fmaxf(fabsf(actor->position.x), fabsf(actor->position.z)) - turnRadius;
    actor->avoidHold = fmaxf(0, actor->avoidHold - dt);
    actor->backupTimer = fmaxf(0, actor->backupTimer - dt);
    bool wallThreat=nearest<margin||turningClearance<1.5f;
    if(actor->aiEnabled) {
        float currentClearance=fminf(STAGE_HALF-fmaxf(body.end.x,body.end.z),
                                     fminf(body.start.x,body.start.z)+STAGE_HALF);
        float futureClearance=fminf(STAGE_HALF-fmaxf(predicted.end.x,predicted.end.z),
                                    fminf(predicted.start.x,predicted.start.z)+STAGE_HALF);
        /* Un disco de giro completo penalizaba colas largas incluso viajando
         * hacia espacio libre. Anticipar sólo un contacto que se aproxima. */
        wallThreat=futureClearance<1.5f&&futureClearance<currentClearance;
    }
    if (!aiSteering && !actor->avoiding && wallThreat) {
        float repelX = fmaxf(0, margin - distances[1]) - fmaxf(0, margin - distances[0]);
        float repelZ = fmaxf(0, margin - distances[3]) - fmaxf(0, margin - distances[2]);
        Vector3 inward = Vec3_Normalize(Vec3_Create(repelX, 0, repelZ));
        if (Vec3_LengthSq(inward) < .01f)
            inward = Vec3_Normalize(Vec3_Scale(actor->position, -1));
        Vector3 tangent = Vec3_Create(-inward.z, 0, inward.x);
        if (tangent.x * fwdX + tangent.z * fwdZ < 0)
            tangent = Vec3_Scale(tangent, -1);
        Vector3 direction =
            Vec3_Normalize(Vec3_Add(Vec3_Scale(inward, .85f), Vec3_Scale(tangent, .65f)));
        actor->escapeWaypoint =
            Vec3_Add(actor->position, Vec3_Scale(direction, fmaxf(6.f, margin * 2)));
        float turnSpace = fmaxf(1.f, STAGE_HALF - turnRadius - 2.f);
        actor->escapeWaypoint.x = fmaxf(-turnSpace, fminf(turnSpace, actor->escapeWaypoint.x));
        actor->escapeWaypoint.z = fmaxf(-turnSpace, fminf(turnSpace, actor->escapeWaypoint.z));
        actor->avoiding = true;
        actor->avoidHold = 1.5f;
        actor->goalProgress = (NavigationProgress){0};
        float desired = atan2f(actor->escapeWaypoint.x - actor->position.x,
                               actor->escapeWaypoint.z - actor->position.z);
        actor->turnSign = WrapAngle(desired - actor->yaw) >= 0 ? 1.f : -1.f;
    }
    if (actor->avoiding) {
        Vector3 direction = Vec3_Sub(actor->escapeWaypoint, actor->position);
        float arrivalRadius =
            fmaxf(.5f, actor->monster.animation->animator.gait.strideLength * actor->scaleFactor);
        NavigationGoalStatus goal =
            NavigationProgress_Update(&actor->goalProgress, actor->position, actor->escapeWaypoint,
                                      actor->yaw, cruiseSpeed, turnLimit, arrivalRadius, dt);
        actor->maxGoalStall = fmaxf(actor->maxGoalStall, actor->goalProgress.withoutProgress);
        if (goal == NAVIGATION_ARRIVED) {
            /* Mantener el rumbo al alcanzar el objetivo, antes de cruzarlo:
             * atan2 de un vector casi nulo hacía perseguirlo hacia atrás. */
            actor->avoiding = false;
            actor->targetYaw = actor->yaw;
            ++actor->goalArrivals;
        } else if (goal == NAVIGATION_STALLED) {
            Vector3 inward = Vec3_Normalize(Vec3_Scale(actor->position, -1));
            if (Vec3_LengthSq(inward) < .01f)
                inward = Vec3_Create(fwdX, 0, fwdZ);
            actor->escapeWaypoint =
                Vec3_Add(actor->position, Vec3_Scale(inward, fmaxf(6.f, margin * 2)));
            float turnSpace = fmaxf(1.f, STAGE_HALF - turnRadius - 2.f);
            actor->escapeWaypoint.x = fmaxf(-turnSpace, fminf(turnSpace, actor->escapeWaypoint.x));
            actor->escapeWaypoint.z = fmaxf(-turnSpace, fminf(turnSpace, actor->escapeWaypoint.z));
            actor->goalProgress = (NavigationProgress){0};
            ++actor->goalRecoveries;
            direction = Vec3_Sub(actor->escapeWaypoint, actor->position);
        }
        if (actor->avoiding)
            actor->targetYaw = atan2f(direction.x, direction.z);
        /* El objetivo permanece fijo mientras se libera la pared: no perseguir
         * una normal que cambia de signo al girar una cola larga. */
        if (actor->avoidHold == 0 && nearest > margin * .65f && turningClearance > 1.5f &&
            cosf(WrapAngle(actor->targetYaw - actor->yaw)) > .8f)
            actor->avoiding = false;
        if (actor->blockedTime > .35f && actor->backupTimer == 0) {
            actor->backupTimer = 1.2f;
            actor->escapeWaypoint = Vec3_Zero();
            actor->avoidHold = 2.f;
            actor->goalProgress = (NavigationProgress){0};
            actor->targetYaw = atan2f(-actor->position.x, -actor->position.z);
        }
    }

    /* La velocidad angular gobierna el rumbo, incluida su aceleración y
     * frenada. La capacidad de los apoyos limita los pivotes cerrados. */
    float diff = WrapAngle(actor->targetYaw - actor->yaw);
    if (actor->avoiding && fabsf(diff) > 2.7f)
        diff = copysignf(fabsf(diff), actor->turnSign);
    float targetTurnRate = fmaxf(-turnLimit, fminf(turnLimit, 2.5f * diff));
    actor->currentTurnRate += (targetTurnRate - actor->currentTurnRate) * (1 - expf(-6 * dt));
    actor->yawDelta = actor->currentTurnRate * dt;
    actor->yaw = WrapAngle(actor->yaw + actor->yawDelta);

    /* Reservar recorrido para el arco de la pata exterior, y acortar el paso
     * cuando el destino queda a un lado o detrás. La pose integra la traslación. */
    float available = fmaxf(0, maxSpeed - fabsf(actor->currentTurnRate) * footRadius);
    float requested = actor->aiEnabled ? actor->aiMotor.speed : actor->speed;
    float effectiveSpeed = copysignf(fminf(fabsf(requested), available), requested);
    effectiveSpeed *= actor->aiEnabled ? fmaxf(0,cosf(diff))
                                      : .18f + .82f * fmaxf(0, cosf(diff));
    if (actor->backupTimer > 0)
        effectiveSpeed = -fminf(actor->speed * .5f, available);
    if (actor->aiEnabled && !actor->mind.intent.moving)
        effectiveSpeed = 0;
    actor->effectiveSpeed = effectiveSpeed;

    /* Anticipación de la mirada (lookDirection) hacia la curva */
    actor->lookOffset = diff / (actor->turnRate * 0.05f + 1e-4f);
    if (actor->lookOffset > 0.5f)
        actor->lookOffset = 0.5f;
    if (actor->lookOffset < -0.5f)
        actor->lookOffset = -0.5f;
}

/**
 * @brief Actualiza la cinemática de huesos y deformador de malla de la criatura.
 */
/** @brief Volúmenes anatómicos conservadores contra cilindros de obstáculos. */
static bool ActorInteractionPoseSafe(const Rig *rig,const SkeletonPose *pose,void *context) {
    StageActor *actor=context;
    Vector3 origin=actor->monster.animation->animator.locomotion.bodyPosition;origin.y=0;
    Quaternion frame=Quat_FromAxisAngle(Vec3_Create(0,1,0),actor->yaw);
    const AnatomyGraph *graph=&actor->monster.anatomyGraph;
    Vector3 centers[ANATOMY_MAX_NODES];float radii[ANATOMY_MAX_NODES];
    for(size_t i=0;i<graph->nodeCount;++i) {
        const AnatomyNode *node=&graph->nodes[i];
        int joint=Skeleton_FindJoint(&rig->skeleton,node->id);if(joint<0)return false;
        centers[i]=Vec3_Add(actor->position,Quat_RotateVector(frame,
            Vec3_Scale(Vec3_Sub(pose->joints[joint].worldPosition,origin),actor->scaleFactor)));
        radii[i]=fmaxf(node->widthRadius,node->heightRadius)*actor->scaleFactor+actor->collisionPadding;
        if(!AIWorld_ClearSegment(&aiWorld,centers[i],centers[i],radii[i],0))return false;
    }
    for(size_t k=0;k<graph->connectionCount;++k) {
        int from=-1,to=-1;
        for(size_t j=0;j<graph->nodeCount;++j) {
            if(graph->nodes[j].id==graph->connections[k].fromId)from=(int)j;
            if(graph->nodes[j].id==graph->connections[k].toId)to=(int)j;
        }
        if(from>=0&&to>=0&&!AIWorld_ClearSegment(&aiWorld,centers[from],centers[to],fmaxf(radii[from],radii[to]),0))return false;
    }
    return true;
}
static bool UpdateActorAnimation(StageActor *actor, float dt) {
    if (!actor->monster.animation)
        return false;

    float effectiveSpeed = actor->effectiveSpeed;

    /* Velocidad local hacia adelante y velocidad angular de giro */
    actor->monster.animation->animator.desiredVelocity =
        Vec3_Create(0, 0, effectiveSpeed / actor->scaleFactor);
    actor->monster.animation->animator.angularVelocity = actor->currentTurnRate;
    Locomotion *locomotion = &actor->monster.animation->animator.locomotion;
    Vector3 previous = locomotion->bodyPosition;
    Locomotion_TurnFrame(locomotion, dt > 0 ? actor->yawDelta : 0);
    if (dt > 0) {
        Vector3 pivot = Vec3_Scale(locomotion->supportCenter, actor->scaleFactor);
        Quaternion old = Quat_FromAxisAngle(Vec3_Create(0, 1, 0), actor->yaw - actor->yawDelta);
        Quaternion current = Quat_FromAxisAngle(Vec3_Create(0, 1, 0), actor->yaw);
        actor->position = Vec3_Add(actor->position, Vec3_Sub(Quat_RotateVector(old, pivot),
                                                             Quat_RotateVector(current, pivot)));
    }

    /* Mirada adelantada guiando la trayectoria en curva */
    float lookLead = actor->currentTurnRate / (actor->turnRate > 0 ? actor->turnRate : 1.0f);
    if (lookLead > 0.8f)
        lookLead = 0.8f;
    if (lookLead < -0.8f)
        lookLead = -0.8f;
    Vector3 desiredLook = Vec3_Create(lookLead * 0.5f, 0.02f, 1.0f);

    if (actor->aiEnabled && actor->mind.intent.lookAt) {
        Vector3 d = Vec3_Sub(actor->mind.intent.lookTarget, actor->aiBody.eye);
        d = Quat_RotateVector(Quat_FromAxisAngle(Vec3_Create(0, 1, 0), -actor->yaw), d);
        d = Vec3_Normalize(d);
        desiredLook = Vec3_Normalize(d);
    }
    actor->monster.animation->animator.lookDirection =
        actor->aiEnabled && dt > 0
            ? Vec3_Normalize(Vec3_Lerp(actor->monster.animation->animator.lookDirection,
                                       desiredLook, 1 - expf(-5 * dt)))
            : desiredLook;
    ProceduralAnimator *animator = &actor->monster.animation->animator;
    animator->validateInteractionPose=ActorInteractionPoseSafe;animator->interactionContext=actor;
    animator->oralReachActive=actor->aiEnabled&&actor->mind.intent.interact&&fabsf(effectiveSpeed)<.08f;
    animator->interactionTarget=actor->mind.intent.interaction;
    if(animator->oralReachActive) {
        Quaternion frame=Quat_FromAxisAngle(Vec3_Create(0,1,0),-actor->yaw);
        InteractionTarget *t=&animator->interactionTarget;
        Vector3 origin=animator->locomotion.bodyPosition;origin.y=0;
        t->position=Vec3_Add(Quat_RotateVector(frame,Vec3_Scale(Vec3_Sub(t->position,actor->position),1/actor->scaleFactor)),origin);
        t->approach=Quat_RotateVector(frame,t->approach);t->normal=Quat_RotateVector(frame,t->normal);
        t->velocity=Quat_RotateVector(frame,Vec3_Scale(t->velocity,1/actor->scaleFactor));
        t->positionTolerance/=actor->scaleFactor;
        animator->oralReachTarget=t->position;
    }
    if (!MonsterAnimation_Update(&actor->monster, dt))
        return false;
    Vector3 displacement =
        Vec3_Scale(Vec3_Sub(locomotion->bodyPosition, previous), actor->scaleFactor);
    displacement.y = 0;
    actor->position = Vec3_Add(
        actor->position,
        Quat_RotateVector(Quat_FromAxisAngle(Vec3_Create(0, 1, 0), actor->yaw), displacement));

    return true;
}

/** @brief Incluye el alcance máximo de cintas y volumen de pelaje procedural. */
static float ActorCollisionPadding(const StageActor *actor) {
    const Monster *m = &actor->monster;
    if (!m->hasSurface)
        return .1f;
    SurfaceRecipe recipe = SurfaceRecipe_CompileScaled(&m->surface, m->surfaceMapping.unitScale);
    if (recipe.data[11][3] <= 0)
        return .1f;
    float maximum = 0;
    for (int i = 0; i < SURFACE_REGION_COUNT; ++i) {
        const float *profile = recipe.data[12 + 2 * i];
        const float *extra = recipe.data[13 + 2 * i];
        float length = recipe.data[8][0] * profile[0] * recipe.data[11][2];
        float lay = recipe.data[8][3] * profile[3], stiffness = recipe.data[9][0];
        float clump = recipe.data[9][1] * extra[2], variation = recipe.data[9][2] * extra[3];
        float reach = length * (1 + .3f * variation) *
                      (1 + fabsf(lay * (1 - .8f * stiffness)) + .175f * clump + .06f * variation);
        reach += .0015f * recipe.data[8][2] * profile[2] * recipe.data[11][2];
        maximum = fmaxf(maximum, reach);
    }
    return .1f + actor->scaleFactor * maximum;
}

/** @brief Transporta un landmark semántico mediante la articulación cefálica vigente. */
static Vector3 ActorHeadPoint(const StageActor *actor, Vector3 landmark) {
    const MonsterAnimation *a = actor->monster.animation;
    int head = a->rig.headJoint;
    if (actor->monster.hasHead &&
        actor->monster.head.anatomy.attachmentBodyPartIndex < actor->monster.bodyPartCount)
        landmark = Vec3_Add(
            landmark, actor->monster.bodyParts[actor->monster.head.anatomy.attachmentBodyPartIndex]
                          .positionRender);
    Vector3 local = landmark;
    if (head >= 0) {
        Quaternion q =
            Quat_Multiply(a->pose.joints[head].worldRotation, actor->boundary.inverseRest[head]);
        local =
            Vec3_Add(a->pose.joints[head].worldPosition,
                     Quat_RotateVector(
                         q, Vec3_Sub(landmark, actor->boundary.rest.joints[head].worldPosition)));
    }
    Vector3 center = a->animator.locomotion.bodyPosition;
    center.y = 0;
    return Vec3_Add(actor->position,
                    Quat_RotateVector(Quat_FromAxisAngle(Vec3_Create(0, 1, 0), actor->yaw),
                                      Vec3_Scale(Vec3_Sub(local, center), actor->scaleFactor)));
}
/** @brief Adaptador corporal; consulta rig y landmarks existentes sin reconstruir geometría. */
static bool ActorCervicalJoint(const StageActor *actor,int joint) {
    const Rig *rig=&actor->monster.animation->rig;
    for(int j=joint;j>=0;j=rig->skeleton.joints[j].parentIndex)
        if(j==rig->neckJoint||j==rig->headJoint)return true;
    return false;
}
static CreatureBodyState ActorAIState(StageActor *actor) {
    float speed, turn, feet;
    ActorMotionLimits(actor, &speed, &turn, &feet);
    CreatureBodyState b = {.position = actor->position,
                           .yaw = actor->yaw,
                           .visionYaw = actor->yaw,
                           .speed =
                               Vec3_Length(actor->monster.animation->animator.locomotion.velocity) *
                               actor->scaleFactor,
                           .group = actor->aiBody.group};
    b.eye = Vec3_Add(actor->position, Vec3_Create(0, 1, 0));
    b.mouth = b.eye;
    if (actor->monster.hasHead) {
        const HeadLandmarks *h = &actor->monster.head.anatomy.landmarks;
        b.eye =
            ActorHeadPoint(actor, Vec3_Scale(Vec3_Add(h->leftEyeCenter, h->rightEyeCenter), .5f));
        b.mouth = ActorHeadPoint(actor, h->mandibularSymphysis);
        Vector3 ahead = ActorHeadPoint(
            actor, Vec3_Add(Vec3_Scale(Vec3_Add(h->leftEyeCenter, h->rightEyeCenter), .5f),
                            Vec3_Create(0, 0, 1)));
        Vector3 direction = Vec3_Sub(ahead, b.eye);
        b.visionYaw = atan2f(direction.x, direction.z);
    }
    float torsoRadius = .4f;
    for (size_t j = 0; j < actor->monster.anatomyGraph.nodeCount; ++j) {
        const AnatomyNode *n = &actor->monster.anatomyGraph.nodes[j];
        if (n->region == ANATOMY_REGION_TRUNK || n->region == ANATOMY_REGION_PELVIS)
            torsoRadius = fmaxf(torsoRadius, n->widthRadius * actor->scaleFactor);
    }
    bool walking = false;
    for (size_t i = 0; i < actor->monster.animation->rig.limbCount; ++i)
        walking |= actor->monster.animation->rig.limbs[i].walking;
    b.capabilities = (CreatureCapabilities){.ground = walking,
                                            .waterInteraction = actor->monster.hasHead,
                                            .hasInteractionPoint = true,
                                            .clearance = torsoRadius + actor->collisionPadding,
                                            .drinkReach = .3f,
                                            .contactTolerance = fmaxf(.008f,.025f*Vec3_Length(actor->monster.animation->rig.oralPoint)) * actor->scaleFactor,
                                            .maximumOralReach = 0,
                                            .maximumSpeed = fminf(speed, actor->speed),
                                            .maximumTurn = turn};
    const AnatomicalReachProfile *reachProfile=&actor->monster.animation->animator.reachProfile;
    for(unsigned k=reachProfile->torsoCount;k<reachProfile->count;++k)b.capabilities.maximumOralReach+=reachProfile->joints[k].length*actor->scaleFactor;
    Vector3 mouthOffset = Vec3_Sub(b.mouth, b.position);
    b.capabilities.mouthForward = mouthOffset.x * sinf(b.yaw) + mouthOffset.z * cosf(b.yaw);
    /* La satisfacción usa el landmark oral posado, nunca su proyección al suelo. */
    b.interactionPoint = b.mouth;
    b.reach=actor->monster.animation->animator.reachResult;
    const MonsterAnimation *animation=actor->monster.animation;
    Vector3 direction=animation->rig.headJoint>=0?Quat_RotateVector(
        animation->pose.joints[animation->rig.headJoint].worldRotation,animation->animator.reachProfile.oralDirection):Vec3_Zero();
    b.mouthDirection=Quat_RotateVector(Quat_FromAxisAngle(Vec3_Create(0,1,0),actor->yaw),direction);
    b.reach.mouth=b.mouth;b.reach.direction=b.mouthDirection;b.reach.footError=0;
    for(size_t k=0;k<animation->rig.limbCount;++k)if(animation->rig.limbs[k].walking&&
        animation->animator.locomotion.feet[k].phase==FOOT_STANCE) {
        float error=Vec3_Distance(animation->pose.joints[animation->rig.limbs[k].endEffectorJoint].worldPosition,
            animation->animator.locomotion.feet[k].targetPosition);
        b.reach.footError=fmaxf(b.reach.footError,error);
        if(error>.004f) {b.reach.status=REACH_BLOCKED;b.reach.blockedReason=4;}
    }
    b.capabilities.bankClearance = b.capabilities.clearance;
    const PerceptionEntry *water =
        CreaturePerception_Find(&actor->mind.perception, actor->mind.target);
    if (water && water->type == AI_WATER) {
        const AnatomyGraph *g = &actor->monster.anatomyGraph;
        Vector3 toward=Vec3_Sub(water->position,b.position);
        float angle=atan2f(toward.x,toward.z)-actor->yaw;
        float cAngle=cosf(angle),sAngle=sinf(angle);
        float required = water->radius + .2f;
        for (size_t j = 0; j < g->nodeCount; ++j) {
            const AnatomyNode *n = &g->nodes[j];
            if (n->region == ANATOMY_REGION_HEAD || n->region == ANATOMY_REGION_NECK)
                continue;
            int joint = Skeleton_FindJoint(&actor->monster.animation->rig.skeleton, n->id);
            if(ActorCervicalJoint(actor,joint))continue;
            Vector3 local =
                joint >= 0 ? Vec3_Sub(actor->monster.animation->pose.joints[joint].worldPosition,
                                      actor->monster.animation->animator.locomotion.bodyPosition)
                           : n->center;
            float x = (local.x*cAngle-local.z*sAngle)*actor->scaleFactor;
            float z = (local.x*sAngle+local.z*cAngle)*actor->scaleFactor;
            float radius = (fmaxf(n->widthRadius, n->envelopeRadii.x) * actor->scaleFactor +
                            actor->collisionPadding + water->radius);
            if (fabsf(x) < radius)
                required = fmaxf(required, z + sqrtf(radius * radius - x * x));
        }
        /* La misma envolvente de segmentos que valida el contacto: no basta
         * con sus nodos cuando la conexión tiene ensanchamiento intermedio. */
        for(size_t j=0;j<g->connectionCount;++j) {
            const BodyConnection *c=&g->connections[j];
            int from=AnatomyGraph_FindNode(g,c->fromId)-g->nodes;
            int to=AnatomyGraph_FindNode(g,c->toId)-g->nodes;
            const AnatomyNode *a=&g->nodes[from],*n=&g->nodes[to];
            int ja=Skeleton_FindJoint(&actor->monster.animation->rig.skeleton,a->id);
            int jb=Skeleton_FindJoint(&actor->monster.animation->rig.skeleton,n->id);
            if(ActorCervicalJoint(actor,ja)||ActorCervicalJoint(actor,jb))continue;
            float radius=(fmaxf(fmaxf(a->widthRadius,a->envelopeRadii.x),
                               fmaxf(n->widthRadius,n->envelopeRadii.x))*actor->scaleFactor+
                          actor->collisionPadding)*(1+fmaxf(0,c->widthBulge))+water->radius;
            int endpoints[]={ja,jb};
            for(int k=0;k<2;++k)if(endpoints[k]>=0) {
                Vector3 local=Vec3_Sub(actor->monster.animation->pose.joints[endpoints[k]].worldPosition,
                    actor->monster.animation->animator.locomotion.bodyPosition);
                float x=(local.x*cAngle-local.z*sAngle)*actor->scaleFactor;
                float z=(local.x*sAngle+local.z*cAngle)*actor->scaleFactor;
                if(fabsf(x)<radius)required=fmaxf(required,z+sqrtf(radius*radius-x*x));
            }
        }
        b.capabilities.bankClearance = fmaxf(.2f, required - water->radius);
    }
    /* El cuello resuelve el alcance en la orilla: su proyección instantánea
     * no desplaza continuamente el destino mientras se inclina la cabeza. */
    b.capabilities.mouthForward = fminf(b.capabilities.mouthForward,b.capabilities.bankClearance);
    return b;
}
/** @brief Publica posiciones antes de observar; ambas mentes ven el mismo estado mundial. */
static void UpdateSceneAI(StageActor *dog, StageActor *lizard, float dt, bool diagnostics,
                          int frame) {
    StageActor *actors[] = {dog, lizard};
    for (int i = 0; i < 2; ++i) {
        StageActor *a = actors[i];
        double start = OpenGLDemoWindow_Time();
        a->aiBody = ActorAIState(a);
        AIWorld_Move(&aiWorld, a->mind.self, a->position);
        AIWorld_SetBounds(&aiWorld, a->mind.self, a->aiBody.capabilities.clearance,
                          fmaxf(0, a->aiBody.eye.y - a->position.y));
        if (a->aiRigGeneration != a->monster.animation->rigGeneration) {
            const AnatomyGraph *g = &a->monster.anatomyGraph;
            for (size_t j = 0; j < g->nodeCount; ++j)
                a->aiNodeJoints[j] =
                    Skeleton_FindJoint(&a->monster.animation->rig.skeleton, g->nodes[j].id);
            for (size_t j = 0; j < g->connectionCount; ++j) {
                a->aiConnectionFrom[j] = a->aiConnectionTo[j] = -1;
                for (size_t k = 0; k < g->nodeCount; ++k) {
                    if (g->nodes[k].id == g->connections[j].fromId)
                        a->aiConnectionFrom[j] = (int)k;
                    if (g->nodes[k].id == g->connections[j].toId)
                        a->aiConnectionTo[j] = (int)k;
                }
            }
            a->aiRigGeneration = a->monster.animation->rigGeneration;
        }
        a->aiMs = (OpenGLDemoWindow_Time() - start) * 1000;
    }
    for (int i = 0; i < 2; ++i) {
        StageActor *a = actors[i];
        if (!a->aiEnabled)
            continue;
        double start = OpenGLDemoWindow_Time();
        float previousThirst = a->mind.needs.urgency[NEED_HYDRATION];
        AIObjectId previousTarget = a->mind.target;
        unsigned previousDrinks = a->mind.drinks, previousCompleted = a->mind.completedDrinks;
        CreatureMind_Update(&a->mind, &aiWorld, &a->aiBody, dt);
        if (diagnostics) {
            if (!a->mind.profile->needs[NEED_HYDRATION].enabled &&
                (a->mind.needs.urgency[NEED_HYDRATION] != 0 || a->mind.goal == GOAL_HYDRATE ||
                 a->mind.drinks))
                a->aiViolation = true;
            if (a->mind.profile->needs[NEED_HYDRATION].enabled &&
                a->mind.needs.urgency[NEED_HYDRATION] < previousThirst - 1e-6f &&
                (a->aiBody.reach.status!=REACH_REACHED||a->aiBody.reach.objectId!=previousTarget||
                 Vec3_Distance(a->aiBody.mouth,a->mind.interaction.position)>a->mind.interaction.positionTolerance||
                 a->aiBody.speed>.04f||a->mind.intent.moving||a->mind.contactTime<.25f))
                a->aiViolation = true;
        }
        if (diagnostics &&
            (a->mind.drinks != previousDrinks || a->mind.completedDrinks != previousCompleted))
            printf("[AI EVENT] id=%u time=%.3f drinks=%u completed=%u thirst=%.6f\n", a->mind.self,
                   a->mind.time, a->mind.drinks, a->mind.completedDrinks, a->mind.needs.urgency[0]);
        a->aiMotor = CreatureMotor_Resolve(&a->mind.intent, &a->aiBody);
        a->aiMs += (OpenGLDemoWindow_Time() - start) * 1000;
        if (diagnostics && (frame % 120 == 0||(a->mind.intent.interact&&frame%30==0))) {
            printf("[REACH SAFETY] id=%u reason=%u foot=%.5f\n",a->mind.self,a->aiBody.reach.blockedReason,a->aiBody.reach.footError);
            const AnatomicalReachProfile *rp=&a->monster.animation->animator.reachProfile;
            printf("[REACH BOUNDS] id=%u reach=%.3f root_y=%.3f distance=%.3f\n",a->mind.self,rp->maximumReach,
                a->monster.animation->pose.joints[rp->joints[0].index].worldPosition.y,
                Vec3_Distance(a->monster.animation->pose.joints[rp->joints[0].index].worldPosition,
                    a->monster.animation->animator.interactionTarget.position));
            if(frame%120==0)for(unsigned k=0;k<rp->count;++k) {
                Quaternion q=a->monster.animation->animator.reachState.rotation[k];
                float angle=2*acosf(fminf(1,fabsf(q.w)));
                printf("[REACH JOINT] id=%u joint=%u angle=%.4f comfort=%.4f\n",a->mind.self,rp->joints[k].id,angle,rp->joints[k].comfort);
            }
            const ReachResult *reach=&a->monster.animation->animator.reachResult;
            printf("[REACH SCENE] frame=%d id=%u phase=%d status=%d error=%.5f orientation=%.5f speed=%.5f stable=%.3f saturation=%.3f target=%.3f,%.3f,%.3f mouth=%.3f,%.3f,%.3f\n",
                frame,a->mind.self,a->mind.interactionPhase,reach->status,reach->error,reach->orientationError,
                reach->stability,reach->stableTime,reach->saturation,a->mind.interaction.position.x,a->mind.interaction.position.y,
                a->mind.interaction.position.z,a->aiBody.mouth.x,a->aiBody.mouth.y,a->aiBody.mouth.z);
            unsigned visible = 0, remembered = 0;
            for (unsigned j = 0; j < a->mind.perception.count; ++j)
                if (a->mind.perception.entries[j].type == AI_WATER) {
                    ++remembered;
                    visible += a->mind.perception.entries[j].visible;
                }
            printf("[AI] frame=%d id=%u brain=%s goal=%d action=%s thirst=%.4f visible=%u "
                   "remembered=%u target=%u distance=%.3f drinks=%u completed=%u failures=%u "
                   "mouth_y=%.3f clearance=%.3f mouth_bank=%.3f forward=%.3f speed=%.3f yaw=%.3f "
                   "random=%u mode=%s\n",
                   frame, a->mind.self, a->mind.profile->name, a->mind.goal,
                   CreatureAction_Name(a->mind.action), a->mind.needs.urgency[0], visible,
                   remembered, a->mind.target, Vec3_Distance(a->position, a->mind.destination),
                   a->mind.drinks, a->mind.completedDrinks, a->mind.actionFailures,
                   a->aiBody.mouth.y, a->aiBody.capabilities.clearance,
                   Vec3_Distance(a->aiBody.mouth, AIWorld_Get(&aiWorld, pondId)->position) -
                       AIWorld_Get(&aiWorld, pondId)->radius,
                   a->aiBody.capabilities.mouthForward, a->effectiveSpeed, a->yaw, a->mind.random,
                   a->mind.intent.reverse  ? "reverse"
                   : a->mind.intent.moving ? "move"
                                           : "idle");
            if (a->mind.action == ACTION_APPROACH_WATER) {
                AABB3D bounds =
                    CreatureBoundary_Bounds(&a->boundary, a->monster.animation, a->position, a->yaw,
                                            a->scaleFactor, a->collisionPadding);
                printf("[AI NAV] id=%u root=%.3f,%.3f goal=%.3f,%.3f waypoint=%.3f,%.3f "
                       "desired=%.3f turn=%.4f avoid=%d bank=%.3f bounds=%.2f,%.2f,%.2f,%.2f\n",
                       a->mind.self, a->position.x, a->position.z, a->mind.destination.x,
                       a->mind.destination.z, a->mind.intent.destination.x,
                       a->mind.intent.destination.z, a->aiMotor.heading,
                       a->aiBody.capabilities.maximumTurn, a->avoiding,
                       a->aiBody.capabilities.bankClearance, bounds.start.x, bounds.end.x,
                       bounds.start.z, bounds.end.z);
            }
        }
    }
}
/** @brief Agua y orilla comparten exactamente el radio lógico; anillos con relieve. */
static Mesh BuildPondMesh(const AIWorldObject *pond) {
    Mesh m = Mesh_Create();
    Vector3 n = Vec3_Create(0, 1, 0);
    for (int i = 0; i < 96; ++i) {
        float a = i * 2 * (float)M_PI / 96, b = (i + 1) * 2 * (float)M_PI / 96;
        Vector3 innerA = Vec3_Add(pond->position,
                                  Vec3_Create(sinf(a) * pond->radius, 0, cosf(a) * pond->radius));
        Vector3 innerB = Vec3_Add(pond->position,
                                  Vec3_Create(sinf(b) * pond->radius, 0, cosf(b) * pond->radius));
        Vector3 outerA = Vec3_Add(pond->position, Vec3_Create(sinf(a) * (pond->radius + .5f), .12f,
                                                              cosf(a) * (pond->radius + .5f)));
        Vector3 outerB = Vec3_Add(pond->position, Vec3_Create(sinf(b) * (pond->radius + .5f), .12f,
                                                              cosf(b) * (pond->radius + .5f)));
        Vector3 c = Vec3_Add(pond->position, Vec3_Create(0, 0, 0));
        AddStageQuad(&m, c, innerA, innerB, c, n, pond->type==AI_FOOD?Color_FromRGB(225,138,68):Color_FromRGB(28, 108, 148));
        AddStageQuad(&m, innerA, outerA, outerB, innerB, n,
                     Color_FromRGB(115 + (i % 3) * 9, 105, 69));
    }
    return m;
}

/** @brief Dos anillos discretos hacen visible la interacción funcional con el agua. */
static Mesh BuildDrinkingRipple(void) {
    Mesh mesh = Mesh_Create();
    for (int ring = 0; ring < 2; ++ring)
        for (int i = 0; i < 32; ++i) {
            float a = i * 2 * (float)M_PI / 32, b = (i + 1) * 2 * (float)M_PI / 32,
                  r = .13f + ring * .13f;
            AddStageQuad(&mesh, Vec3_Create(sinf(a) * r, 0, cosf(a) * r),
                         Vec3_Create(sinf(a) * (r + .012f), 0, cosf(a) * (r + .012f)),
                         Vec3_Create(sinf(b) * (r + .012f), 0, cosf(b) * (r + .012f)),
                         Vec3_Create(sinf(b) * r, 0, cosf(b) * r), Vec3_Create(0, 1, 0),
                         Color_FromRGB(135, 200, 218));
        }
    return mesh;
}

/** @brief Conserva fuera del agua el volumen aproximado, salvo interacción cefálica.
 * Las cápsulas anatómicas incluyen apéndices y margen de pelaje; no se consulta el SDF. */
static bool ActorPondClear(const StageActor *actor, Vector3 position, float yaw) {
    const AIWorldObject *pond = AIWorld_Get(&aiWorld, pondId);
    if (!pond)
        return true;
    const MonsterAnimation *a = actor->monster.animation;
    const AnatomyGraph *graph = &actor->monster.anatomyGraph;
    Vector3 points[ANATOMY_MAX_NODES];
    float radii[ANATOMY_MAX_NODES];
    Vector3 center = a->animator.locomotion.bodyPosition;
    center.y = 0;
    Quaternion rotation = Quat_FromAxisAngle(Vec3_Create(0, 1, 0), yaw);
    for (size_t j = 0; j < graph->nodeCount; ++j) {
        const AnatomyNode *node = &graph->nodes[j];
        int joint = actor->aiNodeJoints[j];
        Vector3 local = joint >= 0 ? a->pose.joints[joint].worldPosition
                                   : Vec3_Add(node->center, a->animator.locomotion.bodyPosition);
        points[j] = Vec3_Add(
            position,
            Quat_RotateVector(rotation, Vec3_Scale(Vec3_Sub(local, center), actor->scaleFactor)));
        radii[j] = fmaxf(node->widthRadius, node->envelopeRadii.x) * actor->scaleFactor +
                   actor->collisionPadding;
        if (node->region == ANATOMY_REGION_HEAD || node->region == ANATOMY_REGION_NECK ||
            ActorCervicalJoint(actor,joint))
            radii[j] = -1;
        float x = points[j].x - pond->position.x, z = points[j].z - pond->position.z;
        if (radii[j] >= 0 && x * x + z * z < (pond->radius + radii[j]) * (pond->radius + radii[j])) {
            return false;
        }
    }
    for (size_t j = 0; j < graph->connectionCount; ++j) {
        const BodyConnection *c = &graph->connections[j];
        int from = actor->aiConnectionFrom[j], to = actor->aiConnectionTo[j];
        if (from < 0 || to < 0 || radii[from] < 0 || radii[to] < 0)
            continue;
        Vector3 d = Vec3_Sub(points[to], points[from]);
        d.y = 0;
        Vector3 delta = Vec3_Sub(pond->position, points[from]);
        delta.y = 0;
        float t = Vec3_LengthSq(d) > 1e-8f
                      ? fmaxf(0, fminf(1, Vec3_Dot(delta, d) / Vec3_LengthSq(d)))
                      : 0;
        Vector3 closest = Vec3_Add(points[from], Vec3_Scale(d, t));
        float x = closest.x - pond->position.x, z = closest.z - pond->position.z;
        float radius = pond->radius + fmaxf(radii[from], radii[to]) * (1 + fmaxf(0, c->widthBulge));
        if (x * x + z * z < radius * radius) {
            return false;
        }
    }
    return true;
}

/** @brief Integra navegación y contacto en subpasos; deforma una vez al publicar. */
static bool UpdateActorSimulation(StageActor *actor, float dt, bool stationary) {
    actor->solveMs = actor->animationMs = actor->deformMs = 0;
    actor->collisionPadding = ActorCollisionPadding(actor);
    Vector3 previous = actor->position;
    float previousYaw = actor->yaw;
    ProceduralAnimator previousAnimator = actor->monster.animation->animator;
    SkeletonPose previousPose = actor->monster.animation->pose;
    if (!stationary && dt > 0)
        UpdateActorNavigation(actor, dt);
    else {
        actor->effectiveSpeed = 0;
        actor->yawDelta = 0;
    }
    double mark = OpenGLDemoWindow_Time();
    /* Los subpasos de apoyos viven en Locomotion. La pose y el contacto final
     * se resuelven una vez: el coste no se multiplica cuando baja el FPS. */
    if (!UpdateActorAnimation(actor, stationary ? 0 : dt))
        return false;
    actor->animationMs = (OpenGLDemoWindow_Time() - mark) * 1000;
    mark = OpenGLDemoWindow_Time();
    double aiContactStart = OpenGLDemoWindow_Time();
    bool pondContact = actor->aiEnabled && !ActorPondClear(actor, actor->position, actor->yaw);
    if(pondContact) {
        const AIWorldObject *pond=AIWorld_Get(&aiWorld,pondId);
        Vector3 outward=Vec3_Sub(actor->position,pond->position);outward.y=0;
        outward=Vec3_Normalize(outward);
        for(int i=0;i<8&&!ActorPondClear(actor,actor->position,actor->yaw);++i)
            actor->position=Vec3_Add(actor->position,Vec3_Scale(outward,.01f));
    }
    if (pondContact && !ActorPondClear(actor,actor->position,actor->yaw)) {
        actor->pondBlockedTime += dt;
        actor->position = previous;
        actor->yaw = previousYaw;
        /* La pose, el marco de apoyos y la raíz se publican juntos. Rechazar
         * únicamente el rumbo acumulaba giros de contactos sin giro corporal. */
        actor->monster.animation->animator = previousAnimator;
        actor->monster.animation->pose = previousPose;
        actor->yawDelta = 0;
        actor->currentTurnRate = 0;
        actor->effectiveSpeed = 0;
        if (!ActorPondClear(actor, actor->position, actor->yaw)) {
            const AIWorldObject *pond = AIWorld_Get(&aiWorld, pondId);
            Vector3 outward = Vec3_Sub(actor->position, pond->position);
            outward.y = 0;
            outward = Vec3_Normalize(outward);
            for (int i = 0; i < 8 && !ActorPondClear(actor, actor->position, actor->yaw); ++i)
                actor->position = Vec3_Add(actor->position, Vec3_Scale(outward, .01f));
        }
        if (actor->pondBlockedTime > .25f && !actor->mind.retreating) {
            CreatureActions_Fail(&actor->mind);
            actor->mind.retreating = true;
            actor->mind.retreatStart=previous;
            actor->mind.retreatGoal=Vec3_Sub(previous,Vec3_Scale(Vec3_Create(sinf(previousYaw),0,cosf(previousYaw)),
                actor->aiBody.capabilities.clearance+2));
            actor->mind.navigation.progress=(NavigationProgress){0};
            actor->pondBlockedTime=0;
        }
    } else {
        actor->pondBlockedTime = 0;
    }
    if (actor->aiEnabled)
        actor->aiMs += (OpenGLDemoWindow_Time() - aiContactStart) * 1000;
    Vector3 proposed = actor->position;
    if (!CreatureBoundary_Solve(&actor->boundary, actor->monster.animation, &actor->position,
                                &actor->yaw, actor->scaleFactor, STAGE_HALF,
                                actor->collisionPadding, stationary ? 0 : dt))
        return false;
    actor->solveMs = (OpenGLDemoWindow_Time() - mark) * 1000;
    float correction = Vec3_Distance(proposed, actor->position);
    actor->stepProjection = correction;
    actor->projectionDistance += correction;
    actor->maxProjection = fmaxf(actor->maxProjection, correction);
    float moved = Vec3_Distance(previous, actor->position);
    actor->travelled += moved;
    if (!stationary && dt > 0) {
        Vector3 intended = Vec3_Sub(proposed, previous),
                actual = Vec3_Sub(actor->position, previous);
        float expected = Vec3_Length(intended);
        float progress = expected > 1e-6f ? Vec3_Dot(actual, intended) / expected : 0;
        actor->nativeProgress += fmaxf(0, fminf(expected, progress));
        if (progress < expected * .3f && correction > .0001f)
            actor->blockedTime += dt;
        else
            actor->blockedTime = fmaxf(0, actor->blockedTime - dt * 2);
        actor->maxBlockedTime = fmaxf(actor->maxBlockedTime, actor->blockedTime);
    }
    mark = OpenGLDemoWindow_Time();
    bool ok = !actor->binding.bound ||
              (actor->gpuSkinning
                   ? AnimatedVisual_DeformRigid(&actor->binding, &actor->visual, &actor->monster)
                   : AnimatedVisual_Deform(&actor->binding, &actor->visual, &actor->monster));
    actor->deformMs = (OpenGLDemoWindow_Time() - mark) * 1000;
    return ok;
}

/** @brief Actualiza ojos de la edad actual y los transporta con la articulación cefálica. */
static bool RenderActorEyes(StageActor *actor, Renderer3D *renderer, bool restOnly) {
    MonsterVisual *visual = &actor->visual;
    if (!MonsterVisual_UpdateEyes(visual->eyes, visual->eyeCount, &actor->monster))
        return false;
    MonsterAnimation *a = actor->monster.animation;
    int head = a->rig.headJoint;
    if (head < 0)
        return false;
    SkeletonPose rest;
    if (!SkeletonPose_Init(&rest, &a->rig.skeleton))
        return false;
    Quaternion q = restOnly ? Quat_Identity()
                            : Quat_Multiply(a->pose.joints[head].worldRotation,
                                            Quat_Inverse(rest.joints[head].worldRotation));
    Vector3 shift = restOnly ? Vec3_Zero()
                             : Vec3_Sub(a->pose.joints[head].worldPosition,
                                        Quat_RotateVector(q, rest.joints[head].worldPosition));
    for (size_t i = 0; i < visual->eyeCount; ++i) {
        MonsterVisualEye *eye = &visual->eyes[i];
        for (size_t v = 0; v < eye->globe.vertexCount; ++v) {
            MeshVertex *vertex = &eye->globe.vertices[v];
            vertex->position = Vec3_Add(Quat_RotateVector(q, vertex->position), shift);
            vertex->normal = Quat_RotateVector(q, vertex->normal);
        }
        eye->center = Vec3_Add(Quat_RotateVector(q, eye->center), shift);
        eye->forward = Quat_RotateVector(q, eye->forward);
        eye->right = Quat_RotateVector(q, eye->right);
        eye->up = Quat_RotateVector(q, eye->up);
        renderer->renderEye(renderer, eye);
    }
    return true;
}

/** @brief Extiende el reposo material al nacer módulos; no mueve coordenadas existentes. */
static bool EnsureMaterialReference(Monster *reference, const Monster *current) {
    bool changed = false;
    for (size_t i = 0; i < current->anatomyGraph.nodeCount; ++i) {
        const AnatomyNode *node = &current->anatomyGraph.nodes[i];
        if (AnatomyGraph_FindNode(&reference->anatomyGraph, node->id))
            continue;
        if (!AnatomyGraph_AddNode(&reference->anatomyGraph, *node))
            return false;
        changed = true;
    }
    if (changed) {
        Vector3 origin = reference->surfaceMapping.origin;
        float scale = reference->surfaceMapping.unitScale;
        reference->surfaceMapping = SurfaceMapping_FromAnatomy(&reference->anatomyGraph, scale);
        reference->surfaceMapping.origin = origin;
    }
    return true;
}

/**
 * @brief Dibuja guías y contornos de límites en el suelo para depuración visual.
 */
static void DrawDebugBounds(void) {
    Color boundCol = Color_FromRGB(230, 80, 50);
    float y = 0.02f;
    /* Paredes de contacto, coincidentes con el borde de la plataforma */
    OpenGLRenderer_DebugLine(Vec3_Create(-STAGE_HALF, y, -STAGE_HALF),
                             Vec3_Create(STAGE_HALF, y, -STAGE_HALF), boundCol);
    OpenGLRenderer_DebugLine(Vec3_Create(STAGE_HALF, y, -STAGE_HALF),
                             Vec3_Create(STAGE_HALF, y, STAGE_HALF), boundCol);
    OpenGLRenderer_DebugLine(Vec3_Create(STAGE_HALF, y, STAGE_HALF),
                             Vec3_Create(-STAGE_HALF, y, STAGE_HALF), boundCol);
    OpenGLRenderer_DebugLine(Vec3_Create(-STAGE_HALF, y, STAGE_HALF),
                             Vec3_Create(-STAGE_HALF, y, -STAGE_HALF), boundCol);
}

int main(int argc, char **argv) {
    bool headless = false;
    bool validateAI = false;
    const char *interactionCase=NULL;
    AIObjectId interactionIds[2]={0};
    Vector3 interactionOrigins[2];
    BrainProfile interactionBrains[2];
    Mesh interactionMarker=Mesh_Create();
    Mesh resourceMarkers[2]={Mesh_Create(),Mesh_Create()};
    bool aiNatural = false;
    const char *dogBrain = "balanced";
    const char *lizardBrain = "solitary";
    double aiTotalMs = 0, aiMaxMs = 0;
    int aiMutation = 0;
    bool aiMutationTriggered = false;
    unsigned aiCaptured[2] = {0};
    bool aiMutationCaptured = false;
    bool cpuSkinning = false, validateSkinning = false;
    bool inspectHead = false, inspectRest = false;
    int initialVariant = 0;
    float inspectAzimuth = 1.2f, inspectElevation = .12f;
    int inspectCreature = 0;
    bool animationStress = false;
    bool benchmarkUncapped = false;
    int maxFrames = 0;
    const char *capturePath = NULL;
    int msaa = 4;
    bool debugBounds = false;
    bool furEnabled = true;
    int targetOverride = -1;
    int cycles = 1;
    unsigned randomSeed = (unsigned)time(NULL);
    bool seedExplicit = false;
    const char *profilePath = NULL;
    const char *capturePrefix = NULL;
    bool stationary = false;
    bool validateGPU = false;
    bool boundaryStress = false;
    bool orbitStress = false;
    bool validateNavigation = false;
    float simulationSeconds = 0;
    float fixedDt = 0;
    float maximumEscape = 0, maximumBend = 0;

    for (int i = 1; i < argc; ++i) {
        if(!strncmp(argv[i],"--interaction-case=",19))interactionCase=argv[i]+19;
        else if (!strcmp(argv[i], "--headless"))
            headless = true;
        else if (!strcmp(argv[i], "--validate-ai"))
            validateAI = true;
        else if (!strcmp(argv[i], "--ai-natural"))
            aiNatural = true;
        else if (!strcmp(argv[i], "--ai-mutate=approach"))
            aiMutation = 1;
        else if (!strcmp(argv[i], "--ai-mutate=drink"))
            aiMutation = 2;
        else if (!strncmp(argv[i], "--dog-brain=", 12))
            dogBrain = argv[i] + 12;
        else if (!strncmp(argv[i], "--lizard-brain=", 15))
            lizardBrain = argv[i] + 15;
        else if (!strcmp(argv[i], "--cpu-skinning"))
            cpuSkinning = true;
        else if (!strcmp(argv[i], "--validate-skinning"))
            validateSkinning = true;
        else if (!strncmp(argv[i], "--frames=", 9))
            maxFrames = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--capture=", 10))
            capturePath = argv[i] + 10;
        else if (!strncmp(argv[i], "--msaa=", 7))
            msaa = atoi(argv[i] + 7);
        else if (!strcmp(argv[i], "--no-fur"))
            furEnabled = false;
        else if (!strncmp(argv[i], "--initial-variant=", 18))
            initialVariant = atoi(argv[i] + 18);
        else if (!strcmp(argv[i], "--inspect-rest"))
            inspectRest = true;
        else if (!strcmp(argv[i], "--inspect-head"))
            inspectHead = true;
        else if (!strcmp(argv[i], "--inspect-dog"))
            inspectCreature = 1;
        else if (!strcmp(argv[i], "--inspect-lizard"))
            inspectCreature = 2;
        else if (!strcmp(argv[i], "--animation-stress"))
            animationStress = true;
        else if (!strncmp(argv[i], "--inspect-azimuth=", 18))
            inspectAzimuth = strtof(argv[i] + 18, NULL);
        else if (!strncmp(argv[i], "--inspect-elevation=", 20))
            inspectElevation = strtof(argv[i] + 20, NULL);
        else if (!strcmp(argv[i], "--debug"))
            debugBounds = true;
        else if (!strncmp(argv[i], "--target=", 9))
            targetOverride = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--cycles=", 9))
            cycles = atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--seed=", 7)) {
            randomSeed = (unsigned)strtoul(argv[i] + 7, NULL, 10);
            seedExplicit = true;
        } else if (!strncmp(argv[i], "--profile=", 10))
            profilePath = argv[i] + 10;
        else if (!strncmp(argv[i], "--capture-prefix=", 17))
            capturePrefix = argv[i] + 17;
        else if (!strcmp(argv[i], "--benchmark-uncapped"))
            benchmarkUncapped = true;
        else if (!strcmp(argv[i], "--stationary"))
            stationary = true;
        else if (!strcmp(argv[i], "--validate-gpu"))
            validateGPU = true;
        else if (!strcmp(argv[i], "--navigation-orbit-stress")) {
            orbitStress = true;
            validateNavigation = true;
        } else if (!strcmp(argv[i], "--boundary-stress"))
            boundaryStress = true;
        else if (!strcmp(argv[i], "--validate-navigation")) {
            boundaryStress = true;
            validateNavigation = true;
        } else if (!strncmp(argv[i], "--stage-half=", 13))
            stageHalf = strtof(argv[i] + 13, NULL);
        else if (!strncmp(argv[i], "--fixed-dt=", 11))
            fixedDt = strtof(argv[i] + 11, NULL);
    }

    if (headless && maxFrames <= 0)
        maxFrames = validateAI ? 1800 : 240;
    if (validateAI && !seedExplicit)
        randomSeed = 42;
    if (validateAI && fixedDt == 0)
        fixedDt = 1.f / 60;
    if (!isfinite(stageHalf) || stageHalf < 13 || !isfinite(fixedDt) || fixedDt < 0 ||
        fixedDt > .25f)
        return 2;
    if (targetOverride >= LIZARD_VARIATIONS_COUNT || targetOverride < -1)
        return 2;
    if (initialVariant < 0 || initialVariant >= LIZARD_VARIATIONS_COUNT ||
        !isfinite(inspectAzimuth) || !isfinite(inspectElevation))
        return 2;
    if (!BrainProfile_Find(dogBrain) || !BrainProfile_Find(lizardBrain))
        return 2;
    srand(randomSeed);

    printf("=================================================================\n");
    printf(" Monster Engine | Demo Visual del Juego: Escena Ortogonal\n");
    printf("=================================================================\n");
    printf(" Controles de cámara y escena:\n");
    printf("   1..4: Cuatro ángulos isométricos axiales (NE, NW, SW, SE)\n");
    printf("   Q / E: Rotación isométrica suave de la vista\n");
    printf("   W / S / A / D: Desplazar objetivo de cámara por el plano\n");
    printf("   + / - (Z / X): Zoom ortogonal (acercar / alejar)\n");
    printf("   F: Alternar pelaje procedural del perro\n");
    printf("   T: Mutar aleatoriamente el lagarto (MonsterAger)\n");
    printf("   D: Mostrar/ocultar límites del plano y vectores\n");
    printf("   Espacio: Pausar / reanudar simulación\n");
    printf("   Escape: Salir\n");
    printf("=================================================================\n\n");

    /* 1. Inicialización de ventana y renderizador OpenGL */
    int winWidth = 1200;
    int winHeight = 850;
    OpenGLDemoWindow *window = NULL;
    Renderer3D renderer = {0};

    /* Cámara ortogonal */
    float orthoHeight = STAGE_HALF * 1.7f;
    float isoAzimuth = (float)(M_PI * 0.25); /* 45 grados */
    float isoElevation = 0.61548f;           /* atan(1/sqrt(2)) ~= 35.264 grados */
    float camDist = 45.0f;
    Vector3 camTarget = Vec3_Create(0, 0, 0);

    ICamera camera = {.target = camTarget,
                      .up = Vec3_Create(0, 1, 0),
                      .fov = -orthoHeight, /* FOV <= 0 indica proyección ortogonal */
                      .nearPlane = 0.5f,
                      .farPlane = 160.0f};

    window = OpenGLDemoWindow_CreateMSAA("Monster Engine | Escena del Juego (Vista Ortogonal)",
                                         winWidth, winHeight, msaa);
    if (!window) {
        fprintf(stderr, "Error al crear la ventana OpenGL\n");
        return 1;
    }

    renderer = OpenGLRenderer_Create(&camera);
    if (!OpenGLRenderer_SurfaceReady(&renderer)) {
        fprintf(stderr, "Error al inicializar programas GLSL de superficie\n");
        OpenGLDemoWindow_Free(window);
        return 1;
    }
    OpenGLRenderer_SetFurQuality(&renderer, furEnabled, furEnabled, 0);

    /* 2. Construcción de la malla del escenario */
    AIWorld_Init(&aiWorld, STAGE_HALF);
    pondId =
        AIWorld_Add(&aiWorld, (AIWorldObject){.type = AI_WATER,
                                              .position = {0, .025f, fminf(10, STAGE_HALF - 5.5f)},
                                              .radius = 3.5f,
                                              .visible = true,
                                              .drinkable = true});
    if(!interactionCase) {
        AIWorldObject resources[2]={{.type=AI_WATER,.position={8,.8f,8},.radius=.45f,.visible=true,.drinkable=true,.walkable=true},
            {.type=AI_FOOD,.position={-8,1,4},.radius=.3f,.height=.1f,.visible=true,.walkable=true}};
        for(int i=0;i<2;++i) {
            AIWorld_Add(&aiWorld,resources[i]);
            resources[i].position.y+=resources[i].height;
            resourceMarkers[i]=BuildPondMesh(&resources[i]);
        }
    }
    Mesh drinkingRipple = BuildDrinkingRipple();
    Mesh pondMesh = BuildPondMesh(AIWorld_Get(&aiWorld, pondId));
    Mesh stageMesh = BuildStageMesh(STAGE_HALF, STAGE_HEIGHT, (int)(STAGE_HALF * 2));
    printf("[Escenario] Plano 3D generado con %zu vértices y %zu triángulos.\n",
           stageMesh.vertexCount, stageMesh.indexCount / 3);

    /* 3. Inicialización del Perro (Mamífero cuadrúpedo) */
    StageActor dog = {.name = "Perro (Cánido)",
                      .position = Vec3_Create(-4.5f, 0.0f, -3.0f),
                      .yaw = 0.8f,
                      .targetYaw = 0.8f,
                      .speed = 1.35f,
                      .turnRate = 1.6f,
                      .wanderTimer = 3.0f,
                      .scaleFactor = 1.0f,
                      .debugColor = Color_FromRGB(255, 170, 40)};
    dog.gpuSkinning = !cpuSkinning && OpenGLRenderer_SupportsComputeSurface(&renderer);
    dog.monster = Monster_Create();
    const CreatureRecipe *dogRecipe = CreatureRecipes_Dog();
    CreaturePhenotype dogPheno = dogRecipe->adult;
    if (!Creature_BuildMonster(&dog.monster, dogRecipe, &dogPheno)) {
        fprintf(stderr, "Error al construir anatomía del perro\n");
        return 1;
    }
    Rig dogRig;
    if (!CreatureRig_Build(&dog.monster, &dogRig)) {
        fprintf(stderr, "Error al construir rig del perro\n");
        return 1;
    }
    GaitProfile dogGait = GaitPreset_CanidWalk(&dogRig);
    if (!MonsterAnimation_Configure(&dog.monster, &dogRig, dogGait)) {
        fprintf(stderr, "Error al configurar animación del perro\n");
        return 1;
    }
    SDFMesherConfig mesherCfg = SDFMesher_DefaultConfig();
    mesherCfg.voxelSize = 0.065f;
    dog.visual = MonsterVisual_Create(mesherCfg);
    if (!MonsterVisual_RebuildNow(&dog.visual, &dog.monster, MonsterSDF_DefaultConfig()) ||
        !AnimatedVisual_Bind(&dog.binding, &dog.visual, &dog.monster)) {
        fprintf(stderr, "Error al mallar y enlazar visual del perro\n");
        return 1;
    }
    if (!CreatureBoundary_BindMesh(&dog.boundary, dog.binding.body, &dogRig.skeleton) ||
        !CreatureBoundary_AddRigid(&dog.boundary, &dog.binding.headBase, dogRig.headJoint))
        return 1;
    for (size_t i = 0; i < dog.binding.eyeCount; ++i)
        if (!CreatureBoundary_AddRigid(&dog.boundary, &dog.binding.eyeBases[i].globe,
                                       dogRig.headJoint))
            return 1;
    for (size_t i = 0; i < dog.visual.mouthCount; ++i) {
        if (!CreatureBoundary_AddRigid(&dog.boundary, &dog.visual.mouths[i].jaw,
                                       dogRig.headJoint) ||
            !CreatureBoundary_AddRigid(&dog.boundary, &dog.visual.mouths[i].hinge,
                                       dogRig.headJoint))
            return 1;
    }
    printf("[Criatura] Perro preparado (%zu vértices).\n", dog.visual.mesh.vertexCount);

    /* 4. Inicialización del Lagarto (Reptil con Variante 0: Emerald Climber) */
    StageActor lizard = {.name = "Lagarto (Reptil)",
                         .position = Vec3_Create(9.0f, 0.0f, -5.0f),
                         .yaw = 3.6f,
                         .targetYaw = 3.6f,
                         .speed = 0.75f,
                         .turnRate = 1.2f,
                         .wanderTimer = 2.0f,
                         .scaleFactor = 1.0f,
                         .debugColor = Color_FromRGB(50, 220, 110)};
    lizard.monster = Monster_Create();
    const CreatureRecipe *lizRecipe = CreatureRecipes_Lizard();
    CreatureVariation initVar = LizardVariations_Get(initialVariant, 42);
    CreatureVariant initVariant;
    if (!CreatureVariation_Apply(lizRecipe, &lizRecipe->adult, &initVar, &initVariant) ||
        !Creature_BuildMonster(&lizard.monster, &initVariant.recipe, &initVariant.phenotype)) {
        fprintf(stderr, "Error al construir anatomía del lagarto inicial (variante 0)\n");
        return 1;
    }
    mesherCfg.voxelSize = 0.075f;
    lizard.visual = MonsterVisual_Create(mesherCfg);
    /* El lagarto mantiene una única representación GPU, incluso al terminar Ager. */
    if (!OpenGLRenderer_SupportsComputeSurface(&renderer)) {
        fprintf(stderr, "La escena requiere extracción SDF GPU (OpenGL 4.3).\n");
        return 1;
    }
    lizard.visual.eyeCount = lizard.monster.eyeCount;
    lizard.visual.eyeCapacity = lizard.monster.eyeCount;
    lizard.visual.eyes = calloc(lizard.monster.eyeCount, sizeof(*lizard.visual.eyes));
    if (!lizard.visual.eyes)
        return 1;
    MutationState lizMutation = {
        .currentVariantIndex = initialVariant, .targetVariantIndex = -1, .seed = randomSeed};
    lizMutation.materialReference = Monster_Clone(&lizard.monster);
    if (!MonsterSDF_Build(&lizMutation.sdf, &lizard.monster, MonsterSDF_DefaultConfig()))
        return 1;
    if (!CreatureBoundary_BindSDF(&lizard.boundary, &lizard.monster, &lizMutation.sdf,
                                  mesherCfg.voxelSize))
        return 1;
    /* Envolvente de todas las variantes: la retícula no se mueve entre mutaciones. */
    AABB3D envelope = lizMutation.sdf.bounds;
    for (size_t i = 0; i < LIZARD_VARIATIONS_COUNT; ++i) {
        CreatureVariation variation = LizardVariations_Get(i, randomSeed + 137);
        CreatureVariant variant;
        Monster endpoint = Monster_Create();
        MonsterSDF field = {0};
        if (!CreatureVariation_Apply(lizRecipe, &lizRecipe->adult, &variation, &variant) ||
            !Creature_BuildMonster(&endpoint, &variant.recipe, &variant.phenotype) ||
            !MonsterSDF_Build(&field, &endpoint, MonsterSDF_DefaultConfig()))
            return 1;
        AABB_ExpandPoint(&envelope, field.bounds.start);
        AABB_ExpandPoint(&envelope, field.bounds.end);
        MonsterSDF_Free(&field);
        Monster_Free(&endpoint);
    }
    Vector3 margin = Vec3_Create(2, 2, 2);
    envelope.start = Vec3_Sub(envelope.start, margin);
    envelope.end = Vec3_Add(envelope.end, margin);
    GpuSDFSurfaceGrid grid;
    if (!GpuSDFSurfaceGrid_Configure(&grid, envelope, mesherCfg.voxelSize, 2) ||
        !(lizMutation.cache = GpuSDFSurfaceCache_Create(&grid, 8)))
        return 1;
    printf("[Criatura] Lagarto GPU preparado: %s (voxel %.3f).\n",
           LizardVariations_GetName(initialVariant), grid.voxelSize);
    snprintf(lizMutation.statusText, sizeof(lizMutation.statusText), "Variante: %s [T: Mutar]",
             LizardVariations_GetName(initialVariant));

    if (boundaryStress) {
        dog.position = Vec3_Create(STAGE_HALF - 2.5f, 0, STAGE_HALF - 2.5f);
        dog.yaw = dog.targetYaw = .7f;
        lizard.position = Vec3_Create(-STAGE_HALF + 2.5f, 0, -STAGE_HALF + 2.5f);
        lizard.yaw = lizard.targetYaw = 3.85f;
    }
    if (validateAI && !aiNatural) {
        lizard.position = Vec3_Create(8, 0, 2);
        StageActor *actors[] = {&dog, &lizard};
        for (int i = 0; i < 2; ++i) {
            Vector3 d = Vec3_Sub(AIWorld_Get(&aiWorld, pondId)->position, actors[i]->position);
            actors[i]->yaw = actors[i]->targetYaw = atan2f(d.x, d.z);
        }
    }
    /* La colocación inicial se valida antes del primer fotograma visible. */
    StageActor *initialActors[] = {&dog, &lizard};
    for (size_t i = 0; i < 2; ++i) {
        StageActor *actor = initialActors[i];
        actor->collisionPadding = ActorCollisionPadding(actor);
        if (!UpdateActorAnimation(actor, 0) ||
            !CreatureBoundary_Solve(&actor->boundary, actor->monster.animation, &actor->position,
                                    &actor->yaw, actor->scaleFactor, STAGE_HALF,
                                    actor->collisionPadding, 0))
            return 1;
    }
    for (int i = 0; i < 2; ++i) {
        StageActor *a = initialActors[i];
        AIObjectId id = AIWorld_Add(&aiWorld, (AIWorldObject){.type = AI_CREATURE,
                                                              .position = a->position,
                                                              .radius = 1,
                                                              .visible = true,
                                                              .walkable = true,
                                                              .group = (unsigned)i + 1});
        CreatureMind_Init(&a->mind, BrainProfile_Find(i ? lizardBrain : dogBrain), id,
                          randomSeed + (unsigned)i * 101);
        a->aiBody.group = (unsigned)i + 1;
        a->aiEnabled = !animationStress && !orbitStress && !boundaryStress;
        a->mind.needs.urgency[NEED_HYDRATION] =
            a->mind.profile->needs[NEED_HYDRATION].enabled ? (validateAI && !aiNatural ? .85f : .35f) : 0;
    }
    if(interactionCase&&strcmp(interactionCase,"pond")) {
        bool food=strcmp(interactionCase,"bowl")!=0;
        interactionMarker=BuildPondMesh(&(AIWorldObject){.type=food?AI_FOOD:AI_WATER,.radius=.22f});
        for(int i=0;i<2;++i) {
            StageActor *a=initialActors[i];
            a->aiBody=ActorAIState(a);
            interactionOrigins[i]=a->aiBody.mouth;
            if(!strcmp(interactionCase,"ground"))interactionOrigins[i].y=.025f;
            else if(!strcmp(interactionCase,"high"))interactionOrigins[i].y+=10;
            else if(!strcmp(interactionCase,"bowl"))interactionOrigins[i].y=.8f;
            else interactionOrigins[i].y-=.3f;
            interactionIds[i]=AIWorld_Add(&aiWorld,(AIWorldObject){.type=food?AI_FOOD:AI_WATER,
                .position=interactionOrigins[i],.approachDirection=food?Vec3_Create(sinf(a->yaw),-.2f,cosf(a->yaw)):Vec3_Zero(),.radius=.22f,.visible=true,.drinkable=!food,.walkable=true});
            interactionBrains[i]=BrainProfile_Default();
            interactionBrains[i].perception.horizontalFOV=6.2831853f;
            interactionBrains[i].needs[NEED_HYDRATION].enabled=!food;
            interactionBrains[i].needs[NEED_HUNGER]=(NeedDefinition){food,food?.025f:0,2,.9f,food?.22f:0,.15f};
            interactionBrains[i].weights[ACTION_SEARCH_FOOD]=food?1.4f:0;
            interactionBrains[i].weights[ACTION_APPROACH_FOOD]=food?1.5f:0;
            interactionBrains[i].weights[ACTION_EAT]=food?1.6f:0;
            CreatureMind_AssignProfile(&a->mind,&interactionBrains[i]);
            a->mind.nextDecision=0;a->mind.perception.nextRefresh=0;
            a->mind.needs.urgency[food?NEED_HUNGER:NEED_HYDRATION]=.85f;
            if(!strcmp(interactionCase,"blocked"))AIWorld_Add(&aiWorld,(AIWorldObject){.type=AI_OBSTACLE,
                .position={interactionOrigins[i].x,0,interactionOrigins[i].z},.radius=.4f,
                .height=interactionOrigins[i].y+1,.visible=true,.occludes=true});
        }
    }
    if (orbitStress) {
        lizard.avoiding = true;
        lizard.escapeWaypoint = lizard.position;
        lizard.avoidHold = 2.f;
    }
    /* 5. Bucle principal de simulación y renderizado */
    bool running = true;
    bool paused = false;
    int frame = 0;
    double lastTime = OpenGLDemoWindow_Time();
    double previousPresentation = lastTime;
    int status = 0;
    int mutationCount = 0, nextMutationFrame = cycles > 0 && !aiMutation ? 30 : INT_MAX;
    FILE *profile = profilePath ? fopen(profilePath, "w") : NULL;
    if (profilePath && !profile)
        return 1;
    if (profile)
        fprintf(profile,
                "frame,active,age,wall_ms,update_ms,draw_cpu_ms,gpu_ms,sampling_ms,emission_ms,"
                "dirty_bricks,samples,indices,classify_ms,render_ms,upload_ms,timing_generation,"
                "certify_ms,empty_bricks,sample_requests,gpu_bytes,boundary_bind_ms,dog_contact_ms,"
                "lizard_contact_ms,animation_ms,deform_ms,dog_projection,lizard_projection,dog_x,"
                "dog_z,lizard_x,lizard_z,blocked_s,simulation_dt,ager_ms,rig_ms,sdf_ms,work_ms,"
                "sleep_ms,presentation_ms,ai_ms\n");
    float currentFps = 60.0f;
    float frameMs = 16.6f;
    float fpsAccumTime = 0.0f;
    int fpsAccumFrames = 0;

    while (running && (!maxFrames || frame < maxFrames)) {
        double frameStart = OpenGLDemoWindow_Time();
        if (window) {
            OpenGLDemoInput in = OpenGLDemoWindow_Poll(window);
            if (in.quit)
                break;
            if (in.togglePause)
                paused = !paused;
            if (in.toggleDebug)
                debugBounds = !debugBounds;
            if (in.keyF) {
                furEnabled = !furEnabled;
                OpenGLRenderer_SetFurQuality(&renderer, furEnabled, furEnabled, 0);
                printf("[Pelaje] %s\n", furEnabled ? "ACTIVADO" : "DESACTIVADO");
            }

            /* Ángulos axiales 1..4 */
            if (in.view == 0)
                isoAzimuth = (float)(M_PI * 0.25);
            else if (in.view == 1)
                isoAzimuth = (float)(M_PI * 0.75);
            else if (in.view == 2)
                isoAzimuth = (float)(M_PI * 1.25);
            else if (in.view == 3)
                isoAzimuth = (float)(M_PI * 1.75);

            /* Rotación suave continua con flechas izquierda/derecha */
            if (in.surfaceSelect != 0) {
                isoAzimuth += (float)in.surfaceSelect * 0.10f;
            }

            /* Ajuste de zoom ortogonal con flechas arriba/abajo o +/- */
            if (in.furLengthAdjust > 0 || in.surfaceAdjust > 0) {
                orthoHeight = fmaxf(10.0f, orthoHeight - 1.5f);
            } else if (in.furLengthAdjust < 0 || in.surfaceAdjust < 0) {
                orthoHeight = fminf(STAGE_HALF * 3.2f, orthoHeight + 1.5f);
            }

            /* Reinicio de posiciones con tecla R */
            if (in.keyR) {
                dog.position = Vec3_Create(-4.5f, 0.0f, -3.0f);
                dog.yaw = 0.8f;
                dog.targetYaw = 0.8f;
                lizard.position = Vec3_Create(9.0f, 0.0f, -5.0f);
                lizard.yaw = 3.6f;
                lizard.targetYaw = 3.6f;
                dog.mind.destinationValid = lizard.mind.destinationValid = false;
                dog.mind.retreating = lizard.mind.retreating = false;
                dog.avoiding = lizard.avoiding = false;
                dog.goalProgress = lizard.goalProgress = (NavigationProgress){0};
                dog.blockedTime = lizard.blockedTime = 0;
                dog.backupTimer = lizard.backupTimer = 0;
                dog.currentTurnRate = lizard.currentTurnRate = 0;
                printf("[Escena] Posiciones de criaturas reiniciadas.\n");
            }

            /* Tecla T: Mutación aleatoria a una de las 10 variantes con MonsterAger */
            bool aiTrigger =
                aiMutation && !aiMutationTriggered && lizard.mind.time > 1 &&
                lizard.mind.action == (aiMutation == 1 ? ACTION_APPROACH_WATER : ACTION_DRINK);
            if (in.keyT || aiTrigger || (headless && frame == nextMutationFrame)) {
                if (lizMutation.active) {
                    printf("[MUTACIÓN] Mutación en curso hacia '%s' (%.0f%%), espere a que "
                           "finalice.\n",
                           LizardVariations_GetName((size_t)lizMutation.targetVariantIndex),
                           lizMutation.progress * 100.0f);
                } else {
                    int nextIdx =
                        targetOverride >= 0 ? targetOverride : lizMutation.currentVariantIndex;
                    while (nextIdx == lizMutation.currentVariantIndex && targetOverride < 0) {
                        nextIdx = rand() % LIZARD_VARIATIONS_COUNT;
                    }
                    lizMutation.targetVariantIndex = nextIdx;
                    lizMutation.seed += 137;

                    CreatureVariation targetVar =
                        LizardVariations_Get((size_t)nextIdx, lizMutation.seed);
                    CreatureVariant targetVariant;
                    if (!CreatureVariation_Apply(lizRecipe, &lizRecipe->adult, &targetVar,
                                                 &targetVariant)) {
                        fprintf(stderr, "[ERROR] Falló CreatureVariation_Apply para variante %d\n",
                                nextIdx);
                    } else {
                        /* Un mismo individuo conserva células y ruido; los rasgos sí son
                         * aleatorios. */
                        targetVariant.phenotype.surface.pigment.seed =
                            lizard.monster.surface.pigment.seed;
                        targetVariant.phenotype.surface.integument.scales.seed =
                            lizard.monster.surface.integument.scales.seed;
                        lizMutation.targetMonster = Monster_Create();
                        if (!Creature_BuildMonster(&lizMutation.targetMonster,
                                                   &targetVariant.recipe,
                                                   &targetVariant.phenotype)) {
                            fprintf(stderr,
                                    "[ERROR] Falló Creature_BuildMonster para variante %d\n",
                                    nextIdx);
                            Monster_Free(&lizMutation.targetMonster);
                        } else {
                            float targetScale = targetVariant.phenotype.axial.totalScale;
                            lizMutation.sourceMonster = Monster_Clone(&lizard.monster);
                            lizMutation.ager = MonsterAger_Create(&lizMutation.sourceMonster,
                                                                  &lizMutation.targetMonster, 0.0f);
                            /* La superficie GPU consume cada edad continua; no usa el snapshot del
                             * mallador CPU. */
                            MonsterAger_SetGeometrySnapshotEnabled(&lizMutation.ager, false);
                            /* El dominio de un accesorio nuevo procede de su
                             * reposo adulto, nunca del brote de radio casi cero.
                             * Ager resuelve también las identidades remapeadas. */
                            MonsterAger_SetPerc(&lizMutation.ager, 1);
                            if (!EnsureMaterialReference(
                                    &lizMutation.materialReference,
                                    MonsterAger_GetResultConst(&lizMutation.ager))) {
                                status = 1;
                                break;
                            }
                            MonsterAger_SetPerc(&lizMutation.ager, 0);

                            lizMutation.sourceSpeed = lizard.speed;
                            lizMutation.targetSpeed = 0.75f * targetScale;
                            lizMutation.sourceTurnRate = lizard.turnRate;
                            lizMutation.targetTurnRate = 1.2f / fmaxf(0.75f, targetScale);
                            if (lizard.monster.animation) {
                                lizMutation.sourceGait = lizard.monster.animation->animator.gait;
                            }
                            if (lizMutation.targetMonster.animation) {
                                lizMutation.targetGait =
                                    lizMutation.targetMonster.animation->animator.gait;
                            } else {
                                lizMutation.targetGait = lizMutation.sourceGait;
                            }

                            lizMutation.progress = 0.0f;
                            lizMutation.duration = 3.0f;
                            lizMutation.active = true;
                            ++mutationCount;
                            nextMutationFrame = INT_MAX;
                            if (aiTrigger) {
                                aiMutationTriggered = true;
                                printf("[AI MUTATION] action=%s thirst=%.6f target=%u memory=%u\n",
                                       CreatureAction_Name(lizard.mind.action),
                                       lizard.mind.needs.urgency[0], lizard.mind.target,
                                       lizard.mind.perception.count);
                            }

                            snprintf(lizMutation.statusText, sizeof(lizMutation.statusText),
                                     "Mutando a: %s (0%%)",
                                     LizardVariations_GetName((size_t)nextIdx));
                            printf(
                                "[MUTACIÓN] Metamorfosis iniciada: '%s' -> '%s' (3.0s con "
                                "MonsterAger)\n",
                                LizardVariations_GetName((size_t)lizMutation.currentVariantIndex),
                                LizardVariations_GetName((size_t)nextIdx));
                        }
                    }
                }
            }
        }

        double now = OpenGLDemoWindow_Time();
        float wallDt = (float)(now - lastTime);
        float dt = fixedDt > 0 ? fixedDt : (headless ? 1.0f / 60.0f : wallDt);
        if (dt > 0.25f)
            dt = 0.25f;
        if (dt <= 0.0f)
            dt = 1.0f / 60.0f;
        lastTime = now;

        /* Cálculo de FPS y actualización del título de ventana */
        fpsAccumTime += wallDt;
        fpsAccumFrames++;
        if (fpsAccumTime >= 0.20f) {
            currentFps = (float)fpsAccumFrames / fpsAccumTime;
            frameMs = (fpsAccumTime / (float)fpsAccumFrames) * 1000.0f;
            fpsAccumTime = 0.0f;
            fpsAccumFrames = 0;
            char title[512];
            snprintf(title, sizeof(title),
                     "Monster Engine | FPS: %.0f (%.1f ms) | D: %s %.0f%% | L: %s %.0f%% | %s",
                     currentFps, frameMs, CreatureAction_Name(dog.mind.action),
                     dog.mind.needs.urgency[0] * 100, CreatureAction_Name(lizard.mind.action),
                     lizard.mind.needs.urgency[0] * 100, lizMutation.statusText);
            if (window)
                OpenGLDemoWindow_SetTitle(window, title);
        }

        double boundaryBindMs = 0, agerMs = 0, rigMs = 0, sdfMs = 0;
        /* Actualización de simulación física y cinemática */
        if (!paused) {

            /* Actualización de mutación suave en curso con MonsterAger */
            if (lizMutation.active) {
                lizMutation.progress += (headless ? dt : wallDt) / lizMutation.duration;
                if (lizMutation.progress >= 1.0f)
                    lizMutation.progress = 1.0f;

                float p = lizMutation.progress;
                float smoothT = p * p * (3.0f - 2.0f * p);

                /* 1. Avanzar MonsterAger e interpolar fenotipo ontogenético */
                double agerStart = OpenGLDemoWindow_Time();
                MonsterAger_SetPerc(&lizMutation.ager, smoothT);
                agerMs = (OpenGLDemoWindow_Time() - agerStart) * 1000;
                double rigStart = OpenGLDemoWindow_Time();
                const Monster *blended = MonsterAger_GetResultConst(&lizMutation.ager);

                /* Anatomía, rig y apariencia proceden de la misma edad continua. */
                MonsterAnimation *animation = lizard.monster.animation;
                lizard.monster.animation = NULL;
                bool copied = Monster_CopyInto(&lizard.monster, blended);
                lizard.monster.animation = animation;
                Rig rig;
                GaitProfile gait =
                    GaitProfile_Lerp(lizMutation.sourceGait, lizMutation.targetGait, smoothT);
                if (!copied || !CreatureRig_Build(&lizard.monster, &rig) ||
                    !MonsterAnimation_Configure(&lizard.monster, &rig, gait)) {
                    fprintf(stderr, "[ERROR] Rig de mutación en frame %d\n", frame);
                    status = 1;
                    break;
                }
                rigMs = (OpenGLDemoWindow_Time() - rigStart) * 1000;
                lizard.speed =
                    (1 - smoothT) * lizMutation.sourceSpeed + smoothT * lizMutation.targetSpeed;
                lizard.turnRate = (1 - smoothT) * lizMutation.sourceTurnRate +
                                  smoothT * lizMutation.targetTurnRate;

                snprintf(lizMutation.statusText, sizeof(lizMutation.statusText),
                         "Mutando a: %s (%.0f%%)",
                         LizardVariations_GetName((size_t)lizMutation.targetVariantIndex),
                         p * 100.0f);

                /* No hay sustitución de malla ni espera al alcanzar el destino. */
                if (lizMutation.progress >= 1.0f) {
                    lizard.speed = lizMutation.targetSpeed;
                    lizard.turnRate = lizMutation.targetTurnRate;
                    Monster_Free(&lizMutation.targetMonster);
                    lizMutation.currentVariantIndex = lizMutation.targetVariantIndex;

                    MonsterAger_Free(&lizMutation.ager);
                    Monster_Free(&lizMutation.sourceMonster);

                    lizMutation.active = false;
                    if (headless && mutationCount < cycles)
                        nextMutationFrame = frame + 30;
                    snprintf(lizMutation.statusText, sizeof(lizMutation.statusText),
                             "Variante: %s [T: Mutar]",
                             LizardVariations_GetName((size_t)lizMutation.currentVariantIndex));
                    printf("[MUTACIÓN] Metamorfosis completada con éxito a '%s'!\n",
                           LizardVariations_GetName((size_t)lizMutation.currentVariantIndex));
                }
            }

            double sdfStart = OpenGLDemoWindow_Time();
            if (!MonsterSDF_Build(&lizMutation.sdf, &lizard.monster, MonsterSDF_DefaultConfig())) {
                status = 1;
                break;
            }
            sdfMs = (OpenGLDemoWindow_Time() - sdfStart) * 1000;
            double bindStart = OpenGLDemoWindow_Time();
            bool bound = CreatureBoundary_BindSDF(&lizard.boundary, &lizard.monster,
                                                  &lizMutation.sdf, mesherCfg.voxelSize);
            boundaryBindMs = (OpenGLDemoWindow_Time() - bindStart) * 1000;
            if (!bound || !MonsterVisual_UpdateEyes(lizard.visual.eyes, lizard.visual.eyeCount,
                                                    &lizard.monster)) {
                status = 1;
                break;
            }
            for (size_t i = 0; i < lizard.visual.eyeCount; ++i)
                if (!CreatureBoundary_AddRigid(&lizard.boundary, &lizard.visual.eyes[i].globe,
                                               lizard.monster.animation->rig.headJoint)) {
                    status = 1;
                    running = false;
                }
            if (!running)
                break;
            if (animationStress) {
                dog.wanderTimer = lizard.wanderTimer = 1000;
                dog.targetYaw = .8f + 1.4f * sinf(simulationSeconds * .6f);
                lizard.targetYaw = 3.6f + 1.4f * sinf(simulationSeconds * .45f);
            }
            if(interactionCase&&!strcmp(interactionCase,"moving"))for(int i=0;i<2;++i) {
                Vector3 p=interactionOrigins[i];p.y+=.15f*sinf(simulationSeconds*.7f);
                AIWorld_Move(&aiWorld,interactionIds[i],p);
            }
            if (!stationary)
                UpdateSceneAI(&dog, &lizard, dt, validateAI || debugBounds || interactionCase, frame);
            if (!UpdateActorSimulation(&dog, dt, stationary)) {
                fprintf(stderr, "Fallo en animación de perro en frame %d\n", frame);
                status = 1;
                break;
            }
            if (!UpdateActorSimulation(&lizard, dt, stationary)) {
                fprintf(stderr, "Fallo en animación de lagarto en frame %d\n", frame);
                status = 1;
                break;
            }
        }
        if (!paused && !stationary) {
            aiTotalMs += dog.aiMs + lizard.aiMs;
            aiMaxMs = fmax(aiMaxMs, dog.aiMs + lizard.aiMs);
        }
        if (validateAI && !paused && !stationary) {
            for (int i = 0; i < 2; ++i) {
                StageActor *a = initialActors[i];
                if (a->aiViolation) {
                    fprintf(stderr, "[ERROR] Precondición de hidratación incumplida id=%u\n",
                            a->mind.self);
                    status = 1;
                    running = false;
                }
                if (!ActorPondClear(a, a->position, a->yaw)) {
                    fprintf(stderr, "[ERROR] AI volumen invade estanque id=%u frame=%d\n",
                            a->mind.self, frame);
                    status = 1;
                    running = false;
                }
                AABB3D bounds =
                    CreatureBoundary_Bounds(&a->boundary, a->monster.animation, a->position, a->yaw,
                                            a->scaleFactor, a->collisionPadding);
                if (bounds.end.x > STAGE_HALF + .001f || bounds.start.x < -STAGE_HALF - .001f ||
                    bounds.end.z > STAGE_HALF + .001f || bounds.start.z < -STAGE_HALF - .001f) {
                    fprintf(stderr, "[ERROR] AI fuera de plataforma\n");
                    status = 1;
                    running = false;
                }
            }
        }
        if (boundaryStress) {
            StageActor *actors[] = {&dog, &lizard};
            for (int i = 0; i < 2; ++i) {
                StageActor *actor = actors[i];
                AABB3D box = CreatureBoundary_Bounds(&actor->boundary, actor->monster.animation,
                                                     actor->position, actor->yaw,
                                                     actor->scaleFactor, actor->collisionPadding);
                float escape = fmaxf(fmaxf(box.end.x - STAGE_HALF, -STAGE_HALF - box.start.x),
                                     fmaxf(box.end.z - STAGE_HALF, -STAGE_HALF - box.start.z));
                maximumEscape = fmaxf(maximumEscape, escape);
                for (size_t j = 0; j < actor->boundary.jointCount; ++j)
                    maximumBend = fmaxf(maximumBend, fabsf(actor->boundary.bend[j]));
                if (escape > 1e-4f) {
                    fprintf(stderr, "[ERROR] Escape %.6f de %s en frame %d\n", escape, actor->name,
                            frame);
                    status = 1;
                    running = false;
                }
            }
        }
        if (!paused && !stationary)
            simulationSeconds += dt;
        double updateMs = (OpenGLDemoWindow_Time() - frameStart) * 1000;
        double drawStart = OpenGLDemoWindow_Time();

        /* Configuración de cámara ortogonal isométrica */
        camera.target = camTarget;
        camera.position =
            Vec3_Add(camTarget, Vec3_Create(camDist * cosf(isoElevation) * sinf(isoAzimuth),
                                            camDist * sinf(isoElevation),
                                            camDist * cosf(isoElevation) * cosf(isoAzimuth)));
        camera.fov = -orthoHeight;
        if (inspectCreature) {
            StageActor *actor = inspectCreature == 1 ? &dog : &lizard;
            MonsterAnimation *a = actor->monster.animation;
            Vector3 local = a->animator.locomotion.supportCenter;
            local.y = actor->monster.phenotype.axial.withersHeight * .5f;
            Quaternion rotation = Quat_FromAxisAngle(Vec3_Create(0, 1, 0), actor->yaw);
            camera.target =
                Vec3_Add(actor->position,
                         Quat_RotateVector(rotation, Vec3_Scale(local, actor->scaleFactor)));
            camera.position = Vec3_Add(
                camera.target,
                Quat_RotateVector(rotation,
                                  Vec3_Create(18 * cosf(inspectElevation) * sinf(inspectAzimuth),
                                              18 * sinf(inspectElevation),
                                              18 * cosf(inspectElevation) * cosf(inspectAzimuth))));
            orthoHeight = inspectCreature == 1 ? 9.f : 16.f;
            camera.fov = -orthoHeight;
        }
        if (inspectHead) {
            /* Cámara diagnóstica centrada en la cabeza articulada; la escena
             * normal conserva su encuadre y plataforma. */
            MonsterAnimation *a = lizard.monster.animation;
            Vector3 center = a->animator.locomotion.bodyPosition;
            center.y = 0;
            Vector3 local = Vec3_Sub(a->pose.joints[a->rig.headJoint].worldPosition, center);
            Quaternion rotation = Quat_FromAxisAngle(Vec3_Create(0, 1, 0), lizard.yaw);
            if (inspectRest) {
                const AnatomyNode *h =
                    AnatomyGraph_FindFirstRegion(&lizard.monster.anatomyGraph, ANATOMY_REGION_HEAD);
                if (h)
                    local = Vec3_Sub(h->center, center);
            }
            camera.target = Vec3_Add(lizard.position, Quat_RotateVector(rotation, local));
            camera.position = Vec3_Add(
                camera.target,
                Quat_RotateVector(rotation,
                                  Vec3_Create(6 * cosf(inspectElevation) * sinf(inspectAzimuth),
                                              6 * sinf(inspectElevation),
                                              6 * cosf(inspectElevation) * cosf(inspectAzimuth))));
            const AnatomyNode *head =
                AnatomyGraph_FindFirstRegion(&lizard.monster.anatomyGraph, ANATOMY_REGION_HEAD);
            orthoHeight = head ? fmaxf(1.5f, Vec3_Length(head->envelopeRadii) * 2.5f) : 3.f;
            camera.fov = inspectRest ? 30.f : -orthoHeight;
        }

        /* Renderizado del fotograma */
        renderer.beginFrame(&renderer);
        if (inspectRest)
            OpenGLRenderer_SetupCamera(&camera, winWidth, winHeight);
        else
            OpenGLRenderer_SetupCameraOrtho(&camera, orthoHeight, winWidth, winHeight);

        /* A) Dibujo del escenario / plano 3D */
        if (!inspectHead) {
            renderer.renderMesh(&renderer, &stageMesh);
            renderer.renderMesh(&renderer, &pondMesh);
            for(int i=0;i<2;++i)if(resourceMarkers[i].vertexCount)renderer.renderMesh(&renderer,&resourceMarkers[i]);
            for(int i=0;i<2;++i)if(interactionIds[i]) {
                const AIWorldObject *o=AIWorld_Get(&aiWorld,interactionIds[i]);
                OpenGLRenderer_ModelTransform(Vec3_Add(o->position,Vec3_Create(0,o->height,0)),0,1,Vec3_Zero());
                renderer.renderMesh(&renderer,&interactionMarker);OpenGLRenderer_PopModelMatrix();
            }
            for (int i = 0; i < 2; ++i) {
                StageActor *actor = initialActors[i];
                if (actor->mind.action != ACTION_DRINK||
                    (actor->mind.interactionPhase!=INTERACTION_STABLE_CONTACT&&actor->mind.interactionPhase!=INTERACTION_CONSUME))continue;
                const AIWorldObject *water=AIWorld_Get(&aiWorld,actor->mind.target);
                if(!water)continue;
                Vector3 position=actor->mind.interaction.position;position.y+=.01f;
                OpenGLRenderer_ModelTransform(
                    position, 0, .8f + .2f * sinf((float)actor->mind.time * 5), Vec3_Zero());
                renderer.renderMesh(&renderer, &drinkingRipple);
                OpenGLRenderer_PopModelMatrix();
            }
        }

        /* B) Dibujo del Perro con su orientación y recentrado en plano XZ */
        float dogYawDeg = (float)(dog.yaw * (180.0 / M_PI));
        Vector3 dogCenter =
            Vec3_Create(dog.monster.animation->animator.locomotion.bodyPosition.x, 0.0f,
                        dog.monster.animation->animator.locomotion.bodyPosition.z);
        OpenGLRenderer_ModelTransform(dog.position, dogYawDeg, dog.scaleFactor, dogCenter);
        if (dog.gpuSkinning &&
            !OpenGLRenderer_SetMeshSkinning(&renderer, &dog.visual.mesh, dog.binding.body,
                                            &dog.monster.animation->rig.skeleton,
                                            &dog.monster.animation->pose, dog.binding.generation)) {
            fprintf(stderr, "[ERROR] Pose de malla GPU en frame %d\n", frame);
            status = 1;
            break;
        }
        MonsterVisual_Render(&dog.visual, &renderer);
        if (dog.gpuSkinning && (validateSkinning || validateGPU) &&
            (frame == 29 || frame == 75 || frame == 120 || frame == 165 || frame == 210 ||
             frame == maxFrames - 1)) {
            float positionError, normalError;
            if (!OpenGLRenderer_ValidateMeshSkinning(&renderer, &dog.visual.mesh, dog.binding.body,
                                                     &dog.monster.animation->rig.skeleton,
                                                     &dog.monster.animation->pose, &positionError,
                                                     &normalError) ||
                positionError > 1e-5f || normalError > 1e-5f) {
                fprintf(stderr, "[ERROR] Paridad de skinning en frame %d\n", frame);
                status = 1;
                break;
            }
            printf("[SKINNING] frame=%d vertices=%zu posicion=%.9f normal=%.9f\n", frame,
                   dog.visual.mesh.vertexCount, positionError, normalError);
        }
        if (!OpenGLRenderer_SetMeshSkinning(&renderer, NULL, NULL, NULL, NULL, 0)) {
            status = 1;
            break;
        }
        OpenGLRenderer_PopModelMatrix();

        /* C) Dibujo del Lagarto con su orientación y recentrado en plano XZ (suelas sobre la
         * plataforma) */
        float lizYawDeg = (float)(lizard.yaw * (180.0 / M_PI));
        Vector3 lizCenter =
            Vec3_Create(lizard.monster.animation->animator.locomotion.bodyPosition.x, 0.0f,
                        lizard.monster.animation->animator.locomotion.bodyPosition.z);
        OpenGLRenderer_ModelTransform(lizard.position, lizYawDeg, lizard.scaleFactor, lizCenter);
        ICamera localCamera = camera;
        Vector3 yawRotation = Vec3_Create(0, lizYawDeg, 0);
        localCamera.position =
            Vec3_Add(Transform3D_InverseRotateVector(yawRotation,
                                                     Vec3_Sub(camera.position, lizard.position)),
                     lizCenter);
        localCamera.target = Vec3_Add(
            Transform3D_InverseRotateVector(yawRotation, Vec3_Sub(camera.target, lizard.position)),
            lizCenter);
        localCamera.up = Transform3D_InverseRotateVector(yawRotation, camera.up);
        if (!EnsureMaterialReference(&lizMutation.materialReference, &lizard.monster) ||
            !OpenGLRenderer_SetMorphSurfaceDomain(&renderer, &lizard.monster,
                                                  &lizMutation.materialReference)) {
            fprintf(stderr, "[ERROR] Dominio material en frame %d\n", frame);
            status = 1;
            break;
        }
        bool verifySamples =
            validateGPU && (frame == 75 || frame == 120 || frame == 165 || frame == 209);
        if (verifySamples)
            setenv("MONSTER_SPARSE_VERIFY_SAMPLES", "1", 1);
        bool posed =
            OpenGLRenderer_SetGpuSurfacePose(&renderer, inspectRest ? NULL : &lizard.monster);
        bool rendered = posed && OpenGLRenderer_RenderGpuSparseSDFSurface(
                                     &renderer, &lizMutation.sdf, lizMutation.cache, &localCamera);
        if (!rendered)
            fprintf(stderr, "[ERROR] Superficie GPU en frame %d (pose=%d)\n", frame, posed);
        if(rendered&&validateSkinning&&!inspectRest&&(frame==29||frame==120||frame==210||frame==maxFrames-1)) {
            MonsterAnimation* animation=lizard.monster.animation;
            SkeletonPose saved=animation->pose;
            bool valid=OpenGLRenderer_ValidateCranialPose(&renderer,&lizard.monster);
            int jaw=animation->rig.jawJoint;
            animation->pose.joints[jaw].localRotation=Quat_FromAxisAngle(
                animation->rig.skeleton.joints[jaw].constraint.hingeAxis,animation->rig.maxJawAngle*.75f);
            valid=SkeletonPose_UpdateSubtree(&animation->pose,&animation->rig.skeleton,jaw)&&valid;
            valid=OpenGLRenderer_SetGpuSurfacePose(&renderer,&lizard.monster)&&valid;
            valid=OpenGLRenderer_ValidateCranialPose(&renderer,&lizard.monster)&&valid;
            animation->pose=saved;
            valid=OpenGLRenderer_SetGpuSurfacePose(&renderer,&lizard.monster)&&valid;
            if(!valid) {fprintf(stderr,"[ERROR] Articulación craneal GPU en frame %d\n",frame);status=1;break;}
        }
        if (verifySamples)
            unsetenv("MONSTER_SPARSE_VERIFY_SAMPLES");
        if (!rendered || !RenderActorEyes(&lizard, &renderer, inspectRest)) {
            status = 1;
            break;
        }
        if (validateGPU &&
            (frame == 29 || frame == 75 || frame == 120 || frame == 165 || frame == 210)) {
            Vector3 probes[512];
            Vector3 size = AABB_Size(lizMutation.sdf.bounds);
            for (unsigned i = 0; i < 512; ++i)
                probes[i] = Vec3_Add(lizMutation.sdf.bounds.start,
                                     Vec3_Create(size.x * ((i * 97) % 509) / 508.f,
                                                 size.y * ((i * 193) % 503) / 502.f,
                                                 size.z * ((i * 251) % 499) / 498.f));
            float error;
            if (!OpenGLRenderer_ValidateComputeSDF(&renderer, &lizMutation.sdf, probes, 512,
                                                   &error) ||
                error > 1e-4f) {
                fprintf(stderr, "Error de paridad frame %d: %.9f\n", frame, error);
                status = 1;
                break;
            }
            printf("[PARIDAD] frame=%d error=%.9f\n", frame, error);
        }
        OpenGLRenderer_SetGpuSurfacePose(&renderer, NULL);
        OpenGLRenderer_SetMorphSurfaceDomain(&renderer, NULL, NULL);
        OpenGLRenderer_PopModelMatrix();

        /* D) Overlay de depuración si está activado */
        if (debugBounds) {
            DrawDebugBounds();
            for (int i = 0; i < 2; ++i) {
                StageActor *a = initialActors[i];
                if (!a->aiEnabled)
                    continue;
                for (int ray = -1; ray <= 1; ++ray) {
                    float angle =
                        a->aiBody.visionYaw + ray * a->mind.profile->perception.horizontalFOV * .5f;
                    OpenGLRenderer_DebugLine(
                        a->aiBody.eye,
                        Vec3_Add(a->aiBody.eye, Vec3_Create(sinf(angle) * 5, 0, cosf(angle) * 5)),
                        a->debugColor);
                }
                OpenGLRenderer_DebugLine(a->aiBody.mouth, a->mind.intent.lookTarget,
                    a->mind.interactionPhase==INTERACTION_CONSUME?Color_FromRGB(40,255,60):a->debugColor);
                const AnatomicalReachProfile *rp=&a->monster.animation->animator.reachProfile;
                Quaternion frameRotation=Quat_FromAxisAngle(Vec3_Create(0,1,0),a->yaw);
                Vector3 origin=a->monster.animation->animator.locomotion.bodyPosition;origin.y=0;
                for(unsigned k=1;k<rp->count;++k) {
                    Vector3 from=a->monster.animation->pose.joints[rp->joints[k-1].index].worldPosition;
                    Vector3 to=a->monster.animation->pose.joints[rp->joints[k].index].worldPosition;
                    from=Vec3_Add(a->position,Quat_RotateVector(frameRotation,Vec3_Scale(Vec3_Sub(from,origin),a->scaleFactor)));
                    to=Vec3_Add(a->position,Quat_RotateVector(frameRotation,Vec3_Scale(Vec3_Sub(to,origin),a->scaleFactor)));
                    OpenGLRenderer_DebugLine(from,to,a->debugColor);
                }
                if(a->mind.interaction.valid)OpenGLRenderer_DebugLine(a->mind.interaction.position,
                    Vec3_Add(a->mind.interaction.position,Vec3_Scale(a->mind.interaction.normal,.4f)),Color_FromRGB(100,180,255));
                Vector3 previous = a->position;
                for (unsigned j = a->mind.navigation.index; j < a->mind.navigation.count; ++j) {
                    Vector3 point = a->mind.navigation.points[j];
                    point.y = .15f;
                    OpenGLRenderer_DebugLine(previous, point, a->debugColor);
                    previous = point;
                }
            }
            /* Líneas de orientación de las criaturas */
            Vector3 dogHead = Vec3_Add(
                dog.position, Vec3_Create(sinf(dog.yaw) * 1.8f, 0.1f, cosf(dog.yaw) * 1.8f));
            OpenGLRenderer_DebugLine(Vec3_Add(dog.position, Vec3_Create(0, 0.1f, 0)), dogHead,
                                     dog.debugColor);

            Vector3 lizHead = Vec3_Add(lizard.position, Vec3_Create(sinf(lizard.yaw) * 1.6f, 0.1f,
                                                                    cosf(lizard.yaw) * 1.6f));
            OpenGLRenderer_DebugLine(Vec3_Add(lizard.position, Vec3_Create(0, 0.1f, 0)), lizHead,
                                     lizard.debugColor);
        }

        /* E) Indicador visual de rendimiento (FPS) en la ventana */
        OpenGLRenderer_DrawHUD(winWidth, winHeight, currentFps, frameMs);

        renderer.endFrame(&renderer);

        if (!OpenGLRenderer_CheckErrors()) {
            fprintf(stderr, "Error de OpenGL detectado en frame %d\n", frame);
            status = 1;
            break;
        }

        if (validateGPU && frame == maxFrames - 1 && capturePrefix) {
            char path[1024];
            snprintf(path, sizeof(path), "%s-diagnostics.json", capturePrefix);
            if (!OpenGLRenderer_SaveSparseSDFDiagnostics(&renderer, lizMutation.cache, path)) {
                status = 1;
                break;
            }
            snprintf(path, sizeof(path), "%s-signature.json", capturePrefix);
            if (!OpenGLRenderer_ExportGpuSurfaceSignature(&renderer, path)) {
                status = 1;
                break;
            }
        }
        if (capturePath && (frame == maxFrames - 1 || maxFrames <= 1)) {
            if (OpenGLRenderer_SavePPM(capturePath, winWidth, winHeight)) {
                printf("[Captura] Guardada con éxito en '%s'\n", capturePath);
            }
        }

        double drawMs = (OpenGLDemoWindow_Time() - drawStart) * 1000;
        if (capturePrefix && (frame == 29 || frame == 75 || frame == 120 || frame == 165 ||
                              frame == 208 || frame == 209 || frame == 210 || frame == 211)) {
            char path[1024];
            snprintf(path, sizeof(path), "%s-%03d.ppm", capturePrefix, frame);
            if (!OpenGLRenderer_SavePPM(path, winWidth, winHeight)) {
                status = 1;
                break;
            }
        }
        if ((validateAI||interactionCase) && capturePrefix) {
            for (int i = 0; i < 2; ++i) {
                StageActor *a = initialActors[i];
                unsigned state = 0;
                const char *label = NULL;
                if (a->mind.action == ACTION_APPROACH_WATER) {
                    state = 1;
                    label = "approach";
                }
                if (a->mind.action == ACTION_DRINK) {
                    state = 2;
                    label = "drink";
                }
                if(a->mind.interactionPhase==INTERACTION_CONSUME) {state=8;label="stable";}
                if (a->mind.completedDrinks && a->mind.action != ACTION_DRINK) {
                    state = 4;
                    label = "resume";
                }
                if (state && !(aiCaptured[i] & state)) {
                    char path[1024];
                    snprintf(path, sizeof(path), "%s-ai-%u-%s.ppm", capturePrefix, a->mind.self,
                             label);
                    if (!OpenGLRenderer_SavePPM(path, winWidth, winHeight)) {
                        status = 1;
                        running = false;
                    }
                    aiCaptured[i] |= state;
                }
            }
            if (aiMutation && lizMutation.active && lizMutation.progress > .5f &&
                !aiMutationCaptured) {
                char path[1024];
                snprintf(path, sizeof(path), "%s-ai-mutation.ppm", capturePrefix);
                if (!OpenGLRenderer_SavePPM(path, winWidth, winHeight)) {
                    status = 1;
                    running = false;
                }
                aiMutationCaptured = true;
            }
        }
        double workMs = (OpenGLDemoWindow_Time() - frameStart) * 1000;
        double sleepMs =
            benchmarkUncapped ? 0 : OpenGLDemoWindow_LimitFPS(previousPresentation, 60) * 1000;
        if (window)
            OpenGLDemoWindow_Swap(window);
        double presentation = OpenGLDemoWindow_Time();
        double presentationMs = (presentation - previousPresentation) * 1000;
        previousPresentation = presentation;
        if (profile) {
            GpuSDFSurfaceStats stats = OpenGLRenderer_GetGpuSDFSurfaceStats(&renderer);
            fprintf(
                profile,
                "%d,%d,%.7f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu,%.3f,%.3f,%.3f,%llu,%.3f,%zu,"
                "%zu,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%.3f,%."
                "3f,%.3f,%.3f,%.3f,%.3f,%.6f\n",
                frame, lizMutation.active, lizMutation.progress,
                (OpenGLDemoWindow_Time() - frameStart) * 1000, updateMs, drawMs, stats.totalGPUms,
                stats.samplingGPUms, stats.emissionGPUms, stats.dirtyBricks, stats.samples,
                stats.publishedIndices, stats.classificationGPUms, stats.renderGPUms,
                stats.uploadCPUms, (unsigned long long)stats.timingGeneration,
                stats.certificationCPUms, stats.certifiedEmptyBricks, stats.sampleRequests,
                stats.gpuBytes, boundaryBindMs, dog.solveMs, lizard.solveMs,
                dog.animationMs + lizard.animationMs, dog.deformMs + lizard.deformMs,
                dog.stepProjection, lizard.stepProjection, dog.position.x, dog.position.z,
                lizard.position.x, lizard.position.z, fmaxf(dog.blockedTime, lizard.blockedTime),
                dt, agerMs, rigMs, sdfMs, workMs, sleepMs, presentationMs, dog.aiMs + lizard.aiMs);
        }
        ++frame;
    }

    printf("[AI PERFORMANCE] mean_ms=%.6f max_ms=%.6f frames=%d\n", frame ? aiTotalMs / frame : 0,
           aiMaxMs, frame);
    if(interactionCase)for(int i=0;i<2;++i) {
        StageActor *a=initialActors[i];
        printf("[INTERACTION RESULT] case=%s id=%u phase=%d status=%d meals=%u drinks=%u failures=%u contact=%.3f error=%.5f\n",
            interactionCase,a->mind.self,a->mind.interactionPhase,a->aiBody.reach.status,a->mind.completedMeals,
            a->mind.completedDrinks,a->mind.actionFailures,a->mind.contactTime,a->aiBody.reach.error);
    }
    if (validateAI && aiMutation && !aiMutationTriggered) {
        fprintf(stderr, "[ERROR] Mutación en acción solicitada no ejecutada\n");
        status = 1;
    }
    if (validateAI) {
        for (int i = 0; i < 2; ++i) {
            StageActor *a = initialActors[i];
            printf("[AI RESULT] id=%u brain=%s thirst=%.5f drinks=%u completed=%u failures=%u\n",
                   a->mind.self, a->mind.profile->name, a->mind.needs.urgency[0], a->mind.drinks,
                   a->mind.completedDrinks, a->mind.actionFailures);
            if (a->mind.profile->needs[0].enabled) {
                if (!a->mind.completedDrinks) {
                    fprintf(stderr, "[ERROR] AI no completó bebida id=%u\n", a->mind.self);
                    status = 1;
                }
            } else if (a->mind.needs.urgency[0] != 0 || a->mind.drinks) {
                fprintf(stderr, "[ERROR] Hidratación desactivada incumplida\n");
                status = 1;
            }
        }
    }
    printf("[OBJETIVOS] llegadas=%u recuperaciones=%u sin_avance_max=%.4f\n",
           dog.goalArrivals + lizard.goalArrivals, dog.goalRecoveries + lizard.goalRecoveries,
           fmaxf(dog.maxGoalStall, lizard.maxGoalStall));
    if (orbitStress && lizard.goalArrivals == 0) {
        fprintf(stderr, "[ERROR] No abandona objetivo alcanzado.\n");
        status = 1;
    }
    if (boundaryStress)
        printf("[NAVEGACION] perro_recorrido=%.4f lagarto_recorrido=%.4f bloqueo_max=%.4f "
               "correccion_total=%.4f avance_perro=%.4f avance_lagarto=%.4f segundos=%.4f\n",
               dog.travelled, lizard.travelled, fmaxf(dog.maxBlockedTime, lizard.maxBlockedTime),
               dog.projectionDistance + lizard.projectionDistance, dog.nativeProgress,
               lizard.nativeProgress, simulationSeconds);
    if (validateNavigation && simulationSeconds > 5 &&
        (dog.maxBlockedTime > 2 || lizard.maxBlockedTime > 2 ||
         dog.nativeProgress < simulationSeconds * .06f ||
         lizard.nativeProgress < simulationSeconds * .06f)) {
        fprintf(stderr, "[ERROR] La navegación no mantiene progreso o permanece bloqueada.\n");
        status = 1;
    }
    if (boundaryStress)
        printf("[CONTACTO] escape_max=%.8f correccion_objetivo_max=%.5f rad\n", maximumEscape,
               maximumBend);
    printf("Simulación finalizada tras %d frames.\n", frame);

    /* Liberación de recursos */
    AIWorld_Remove(&aiWorld, dog.mind.self);
    AIWorld_Remove(&aiWorld, lizard.mind.self);
    AIWorld_Remove(&aiWorld, pondId);
    if (profile)
        fclose(profile);
    MonsterSDF_Free(&lizMutation.sdf);
    GpuSDFSurfaceCache_Destroy(lizMutation.cache);
    Monster_Free(&lizMutation.materialReference);
    if (lizMutation.active) {
        MonsterAger_Free(&lizMutation.ager);
        Monster_Free(&lizMutation.sourceMonster);
        Monster_Free(&lizMutation.targetMonster);
    }

    AnimatedVisual_Free(&dog.binding);
    MonsterVisual_Free(&dog.visual);
    Monster_Free(&dog.monster);

    AnimatedVisual_Free(&lizard.binding);
    MonsterVisual_Free(&lizard.visual);
    Monster_Free(&lizard.monster);

    CreatureMind_Reset(&dog.mind);
    CreatureMind_Reset(&lizard.mind);
    Mesh_Free(&drinkingRipple);
    for(int i=0;i<2;++i)Mesh_Free(&resourceMarkers[i]);
    Mesh_Free(&interactionMarker);
    Mesh_Free(&pondMesh);
    Mesh_Free(&stageMesh);
    OpenGLRenderer_Destroy(&renderer);
    if (window)
        OpenGLDemoWindow_Free(window);

    return status;
}
