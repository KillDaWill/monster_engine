/**
 * @file EvolutionSimulationRunner.c
 * @brief Hilo dedicado de simulación, cola de comandos con acuses y snapshots triple-buffer.
 */
#include "EvolutionSimulationRunner.h"
#include "EcosystemAnatomy.h"
#include "VisualVariation.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct RunnerCommand {
    uint64_t id;
    EvolutionCommandType type;
    AIObjectId idA;
    AIObjectId idB;
    double valueDouble;
    unsigned valueUint;
    bool valueBool;
    char path[256];
    char reason[32];
    EvolutionArenaConfig resetConfig;
    bool hasGenome;
    NeuralGenome genome;
} RunnerCommand;

struct EvolutionSimulationRunner {
    EcosystemWorld world;
    EvolutionProfiler profiler;
    EvolutionArenaConfig currentConfig;

    pthread_t thread;
    bool threadStarted;

    /* Sincronización de comandos y control del hilo. */
    pthread_mutex_t cmdMutex;
    pthread_cond_t cmdCond;
    pthread_cond_t ackCond;
    RunnerCommand queue[EVO_RUNNER_CMD_QUEUE_CAPACITY];
    unsigned qHead, qTail, qCount;
    uint64_t nextCmdId;
    bool stopRequested;

    /* Historial de acuses + cola FIFO de notificación para la UI. */
    EvolutionCommandAck acks[EVO_RUNNER_ACK_HISTORY];
    EvolutionCommandAck uiAckQueue[EVO_RUNNER_ACK_HISTORY];
    unsigned uiAckHead, uiAckTail, uiAckCount;

    /* Triple buffering de snapshots con propiedad explícita (escritor vs lector). */
    pthread_mutex_t snapMutex;
    EvolutionRenderSnapshot buffers[3];
    int writeIdx;
    int latestIdx;
    int readerIdx;
    uint64_t nextSequence;

    /* Estado de ritmo/aceleración y selección de inspección (propiedad del hilo de sim). */
    AIObjectId inspectTargetId;
    double requestedSpeed;
    bool maxSpeedMode;
    double scheduledDebt;
    double achievedSpeed;
    double ticksPerSecond;
    double simCpuUtil;
    double lastPublishMs;
    uint64_t ticksInWindow;
    double simWorkMsInWindow;
    double windowStartMs;
};

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static void sleepUs(long us) {
    if (us <= 0) return;
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

static bool ensureSnapshotBuffers(EvolutionRenderSnapshot *s, unsigned entCap, unsigned resCap,
                                  unsigned obsCap) {
    if (entCap > s->entityCapacity) {
        unsigned cap = entCap < 64u ? 64u : entCap;
        EvolutionEntitySnapshot *p = realloc(s->entities, cap * sizeof(*p));
        if (!p) return false;
        s->entities = p;
        s->entityCapacity = cap;
    }
    if (resCap > s->resourceCapacity) {
        unsigned cap = resCap < 64u ? 64u : resCap;
        EvolutionResourceSnapshot *p = realloc(s->resources, cap * sizeof(*p));
        if (!p) return false;
        s->resources = p;
        s->resourceCapacity = cap;
    }
    if (obsCap > s->obstacleCapacity) {
        unsigned cap = obsCap < 32u ? 32u : obsCap;
        EvolutionObstacleSnapshot *p = realloc(s->obstacles, cap * sizeof(*p));
        if (!p) return false;
        s->obstacles = p;
        s->obstacleCapacity = cap;
    }
    return true;
}

static void freeSnapshot(EvolutionRenderSnapshot *s) {
    if (!s) return;
    WorldStaticData_Release(s->staticWorld);
    free(s->entities);
    free(s->resources);
    free(s->obstacles);
    memset(s, 0, sizeof(*s));
}

static void fillSelectedSnapshot(const EcosystemWorld *w, AIObjectId targetId,
                                 EvolutionSelectedSnapshot *out) {
    memset(out, 0, sizeof(*out));
    const EcosystemEntity *e = targetId ? EcosystemWorld_GetConst(w, targetId) : NULL;
    if (!e && w->count > 0u) {
        e = &w->entities[w->activeEntitySlots[0]];
    }
    if (!e || !e->id) return;

    out->valid = true;
    out->entityId = e->id;
    out->speciesIndex = e->speciesIndex;
    out->speciesId = w->species[e->speciesIndex].id;
    out->generation = e->generation;
    out->offspringCount = e->offspringCount;
    out->parents[0] = e->reproduction.parents[0];
    out->parents[1] = e->reproduction.parents[1];
    out->age = e->physiology.age;
    out->observedSurvival = fmax(0.0, w->clock.seconds - e->bornAt);
    out->lifeStage = e->physiology.stage;
    out->action = e->mind.action;
    out->targetId = e->mind.target;
    out->gestating = e->reproduction.gestating;
    out->mateReady = e->body.mateReady;
    out->health = e->physiology.health;
    out->energy = e->physiology.energy;
    out->hydration = e->physiology.hydration;
    out->fatigue = e->physiology.fatigue;
    out->controllerType = e->mind.controllerType;

    const NeuralGenome *g = CreatureMind_GetNeuralGenome(&e->mind);
    if (g) {
        out->hasNeuralBrain = true;
        out->genome = *g;
        out->runtime = e->mind.neuralBrain.runtime;
        out->neuralState = e->mind.neuralState;
    }

    unsigned navN = e->mind.navigation.count;
    if (navN > 32u) navN = 32u;
    out->navPointCount = navN;
    out->navPointIndex = e->mind.navigation.index;
    for (unsigned i = 0; i < navN; ++i) {
        out->navPoints[i] = e->mind.navigation.points[i];
    }

    if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR && e->metabolism.initialized && w->metabolismDb) {
        out->hasMetabolism = true;
        const EcosystemSpecies *sp = &w->species[e->speciesIndex];
        const MetabolicProfile *mp = MetabolicProfileRegistry_Get(&w->metabolismDb->profiles, sp->metabolicProfile);
        snprintf(out->metabolicProfileName, sizeof(out->metabolicProfileName), "%.47s",
                 mp ? mp->name : "unknown");
        const SubstanceDef *lastSub = SubstanceRegistry_Get(&w->metabolismDb->substances,
                                                            e->metabolism.lastIngestedSubstance);
        snprintf(out->lastSubstanceName, sizeof(out->lastSubstanceName), "%.39s",
                 lastSub ? lastSub->name : "none");
        out->digestiveMass = e->metabolism.digestiveMass;
        out->digestiveCapacity = e->metabolism.digestiveCapacity;
        out->digestiveFullness = MetabolicState_Fullness(&e->metabolism);
        out->wasteMass = e->metabolism.wasteMass;
        out->nutrientReserve = e->metabolism.nutrientReserve;
        out->toxicity = e->metabolism.toxicity;
        out->digestiveDiscomfort = e->metabolism.digestiveDiscomfort;
        out->recentEnergyAbsorbed = e->metabolism.recentEnergyAbsorbed;
        out->recentHydrationAbsorbed = e->metabolism.recentHydrationAbsorbed;
        out->recentMetabolicDamage = e->metabolism.recentMetabolicDamage;
        out->totalIngestedMass = e->metabolism.totalIngestedMass;
        out->totalEnergyGenerated = e->metabolism.totalEnergyGenerated;
        out->totalHydrationGenerated = e->metabolism.totalHydrationGenerated;
        out->totalToxinExposure = e->metabolism.totalToxinExposure;
        unsigned ec = e->metabolism.activeCount;
        if (ec > METABOLIC_STATE_MAX_ACTIVE) ec = METABOLIC_STATE_MAX_ACTIVE;
        out->digestiveEntryCount = ec;
        for (unsigned i = 0; i < ec; ++i) {
            const MoleculeDef *md = MoleculeRegistry_Get(&w->metabolismDb->molecules,
                                                         e->metabolism.entries[i].molecule);
            snprintf(out->digestiveMoleculeNames[i], sizeof(out->digestiveMoleculeNames[i]), "%.31s",
                     md ? md->id : "?");
            out->digestiveMoleculeMasses[i] = e->metabolism.entries[i].mass;
        }
    }
}

static void publishSnapshot(EvolutionSimulationRunner *r) {
    double tPub0 = EvolutionProfiler_Begin(&r->profiler);
    EvolutionRenderSnapshot *dst = &r->buffers[r->writeIdx];
    const EcosystemWorld *w = &r->world;

    if (!ensureSnapshotBuffers(dst, w->count > 0 ? w->count : 1u,
                               w->activeResourceCount > 0 ? w->activeResourceCount : 1u,
                               w->obstacleCount > 0 ? w->obstacleCount : 1u)) {
        return;
    }

    dst->sequence = ++r->nextSequence;
    dst->tick = w->clock.tick;
    dst->seconds = w->clock.seconds;
    dst->days = SimulationClock_Days(&w->clock);
    dst->timeOfDay = SimulationClock_TimeOfDay(&w->clock);
    dst->publishedAtMs = nowMs();
    dst->arenaHalfSize = w->observations.halfSize;
    /* Retener de forma segura la referencia inmutable al mundo estático sin copiar datos. */
    const WorldStaticData *prevSw = dst->staticWorld;
    dst->staticWorld = WorldStaticData_Retain(w->staticWorld);
    WorldStaticData_Release(prevSw);
    if (dst->staticWorld) {
        dst->worldEpoch = dst->staticWorld->epochId;
        dst->worldFingerprint = dst->staticWorld->dataFingerprint;
        dst->worldRecipeHash = dst->staticWorld->recipeHash;
        dst->worldSeed = dst->staticWorld->masterSeed;
        dst->worldGeneratorVersion = dst->staticWorld->generatorVersion;
    } else {
        dst->worldEpoch = 0u;
        dst->worldFingerprint = 0u;
        dst->worldRecipeHash = 0u;
        dst->worldSeed = w->initialSeed;
        dst->worldGeneratorVersion = 0u;
    }
    dst->capacity = w->capacity;
    dst->workerCount = EcosystemWorld_WorkerCount(w);
    dst->paused = w->clock.paused;
    dst->maxSpeedMode = r->maxSpeedMode;
    dst->requestedSpeed = r->requestedSpeed;
    dst->achievedSpeed = w->clock.paused ? 0.0 : r->achievedSpeed;
    dst->ticksPerSecond = w->clock.paused ? 0.0 : r->ticksPerSecond;
    dst->backlogSeconds = r->scheduledDebt;
    dst->behind = (!w->clock.paused) && (r->scheduledDebt > w->clock.fixedStep * 4.0);
    dst->simCpuUtilization = r->simCpuUtil;

    EvolutionProfileSummary tickSum = EvolutionProfiler_Summarize(&r->profiler, EVO_PROF_SIM_TICK);
    dst->meanTickMs = tickSum.meanMs;
    dst->p95TickMs = tickSum.p95Ms;
    dst->p99TickMs = tickSum.p99Ms;
    EvolutionProfiler_SampleMemory(&r->profiler);
    dst->simCounters = r->profiler.counters;

    dst->statistics = w->statistics;
    dst->interventionCount = w->interventionCount;
    dst->controller = w->defaultController;
    dst->metabolismMode = w->metabolismMode;
    snprintf(dst->founderModeName, sizeof(dst->founderModeName), "%s", w->founderModeName);
    dst->speciesCount = w->speciesCount < EVO_RUNNER_MAX_SPECIES ? w->speciesCount : EVO_RUNNER_MAX_SPECIES;
    for (unsigned s = 0; s < dst->speciesCount; ++s) {
        snprintf(dst->speciesNames[s], sizeof(dst->speciesNames[s]), "%s", w->species[s].name);
        dst->speciesRecipeIds[s] = w->species[s].recipeId;
    }

    unsigned brums = 0u, velks = 0u, outEnt = 0u;
    for (unsigned k = 0; k < w->count; ++k) {
        unsigned slot = w->activeEntitySlots[k];
        const EcosystemEntity *e = &w->entities[slot];
        if (!e->id) continue;
        if (e->speciesIndex == 0u) ++brums; else ++velks;

        EvolutionEntitySnapshot *es = &dst->entities[outEnt++];
        es->id = e->id;
        es->slotIndex = slot;
        es->recipeId = w->species[e->speciesIndex].recipeId;
        es->entitySeed = e->seed;
        es->speciesIndex = e->speciesIndex;
        es->speciesId = w->species[e->speciesIndex].id;
        es->generation = e->generation;
        es->offspringCount = e->offspringCount;
        es->lifeStage = e->physiology.stage;
        es->action = e->mind.action;
        es->goal = e->mind.goal;
        es->reachStatus = e->body.reach.status;
        es->oralContactTarget = e->mind.intent.interaction.position;
        es->oralContactTolerance = e->mind.intent.interact ? e->mind.intent.interaction.positionTolerance : 0.0f;
        es->oralContactTime = e->mind.contactTime;
        es->lastIngestionTime = e->metabolism.lastIngestionTime;
        es->lastIngestedMass = e->metabolism.lastIngestedMass;
        es->position = e->body.position;
        es->velocity = e->velocity;
        es->eyePos = e->body.eye;
        es->mouth = e->body.mouth;
        es->oralPivot = e->oralPivot;
        es->oralElbow = e->oralElbow;
        es->groundNormal = e->body.groundNormal;
        es->orientation = e->body.orientation;
        es->yaw = e->body.yaw;
        es->speed = e->body.speed;
        es->developmentScale = e->developmentScale;
        es->speciesRadius = w->species[e->speciesIndex].radius;
        es->clearance = e->body.capabilities.clearance;
        es->ageSeconds = (float)e->physiology.age;
        es->maturityAgeSeconds = (float)w->species[e->speciesIndex].physiology.maturityAge;
        es->anatomicalScale = e->anatomicalBody ? EcosystemAnatomy_Scale(e->anatomicalBody, e)
                                                : e->developmentScale * e->genome.size;
        es->genome = e->genome;
        es->health = e->physiology.health;
        es->energy = e->physiology.energy;
        es->hydration = e->physiology.hydration;
        es->fatigue = e->physiology.fatigue;
        es->oralActive = e->mind.intent.interact || e->mind.action == ACTION_ATTACK;
        es->gestating = e->reproduction.gestating;
        es->mateReady = e->body.mateReady;
        es->targetId = e->mind.target;
        es->hasTarget = false;
        if (e->mind.target) {
            const AIWorldObject *obj = AIWorld_Get(&w->observations, e->mind.target);
            if (obj) { es->hasTarget = true; es->targetPos = obj->position; }
        }
        es->maturity = e->physiology.stage == LIFE_ADULT ? 1.0f : 0.65f;
        es->morphologyBucket = VisualVariation_Profile(&e->genome, e->seed).bucket;

        if (e->anatomicalBody && e->anatomicalBody->tmpl) {
            const EcosystemAnatomy *a = e->anatomicalBody;
            const EcosystemAnatomyTemplate *tmpl = a->tmpl;
            es->hasAnatomicalBody = true;
            es->growthStep = a->growthStep;
            es->anatomyGeneration = a->generation;
            es->crouch = a->crouch;
            es->maturity = tmpl->phenotype.development.colorMaturity;
            es->standingHeight = a->standingHeight;
            es->trunkLength = tmpl->phenotype.axial.trunkLength;
            es->anatomyCenter = a->center;
            es->modelLength = a->modelLength;
            unsigned jc = tmpl->profile.count;
            if (jc > REACH_MAX_JOINTS) jc = REACH_MAX_JOINTS;
            es->cervicalJointCount = (uint8_t)jc;
            for (unsigned j = 0; j < jc; ++j) {
                es->cervicalJointIndices[j] = (int16_t)tmpl->profile.joints[j].index;
                es->cervicalRotations[j] = a->state.rotation[j];
            }
            es->headWorldPos = jc > 0 ? a->cervicalWorldPos[jc - 1u]
                                      : tmpl->standingRest.joints[tmpl->rig.headJoint].worldPosition;
        } else {
            es->hasAnatomicalBody = false;
            es->growthStep = ECOSYSTEM_ANATOMY_GROWTH_STEPS;
            es->anatomyGeneration = 0u;
            es->crouch = 0.0f;
            es->cervicalJointCount = 0u;
            es->standingHeight = 0.0f;
            es->trunkLength = 0.0f;
            es->anatomyCenter = Vec3_Zero();
            es->modelLength = 1.0f;
            es->headWorldPos = Vec3_Zero();
        }
    }
    dst->entityCount = outEnt;
    dst->brumacernoCount = brums;
    dst->velkarCount = velks;

    unsigned outRes = 0u;
    for (unsigned k = 0; k < w->activeResourceCount; ++k) {
        const EcosystemResource *res = &w->resources[w->activeResourceSlots[k]];
        if (!res->id || res->isWaterLedger) continue;
        const AIWorldObject *obj = AIWorld_Get(&w->observations, res->id);
        if (!obj) continue;
        const EcosystemResource *ledger = (res->sharedLedgerResourceId != 0u)
                                              ? EcosystemWorld_Resource((EcosystemWorld *)w, res->sharedLedgerResourceId)
                                              : res;
        if (!ledger) ledger = res;
        EvolutionResourceSnapshot *rs = &dst->resources[outRes++];
        memset(rs, 0, sizeof(*rs));
        rs->id = res->id;
        rs->category = ledger->category;
        rs->position = obj->position;
        rs->radius = obj->radius;
        rs->biomassFraction = EcosystemResource_AvailabilityFraction(ledger);
        rs->molecular = ledger->molecular;
        rs->mass = ledger->biomass;
        rs->maxMass = ledger->maximumBiomass;
        rs->waterBodyId = res->waterBodyId;
        rs->shoreSiteIndex = res->shoreSiteIndex;
        if (ledger->molecular) {
            rs->phase = ledger->phase;
            rs->presentationModel = ledger->presentationModel;
            rs->color[0] = ledger->color[0]; rs->color[1] = ledger->color[1]; rs->color[2] = ledger->color[2];
            rs->hardness = ledger->hardness;
            rs->regenPolicy = ledger->regenPolicy;
            rs->decayPolicy = ledger->decayPolicy;
            rs->remainingLifetime = ledger->remainingLifetime;
            rs->sensorySignature = ledger->sensorySignature;
            rs->sourceCreatureId = ledger->sourceCreatureId;
            rs->killerCreatureId = ledger->killerCreatureId;
            if (w->metabolismDb) {
                const SubstanceDef *sd = SubstanceRegistry_Get(&w->metabolismDb->substances, ledger->substanceHandle);
                snprintf(rs->substanceId, sizeof(rs->substanceId), "%.31s", sd ? sd->id : "custom");
                snprintf(rs->substanceName, sizeof(rs->substanceName), "%.39s", sd ? sd->name : "Custom");
                unsigned cc = ledger->componentCount < 8u ? ledger->componentCount : 8u;
                rs->componentCount = cc;
                for (unsigned ci = 0; ci < cc; ++ci) {
                    const MoleculeDef *md = MoleculeRegistry_Get(&w->metabolismDb->molecules,
                                                                 ledger->composition[ci].molecule);
                    snprintf(rs->compNames[ci], sizeof(rs->compNames[ci]), "%.27s", md ? md->id : "?");
                    rs->compFractions[ci] = ledger->composition[ci].fraction;
                }
            }
        }
    }
    dst->resourceCount = outRes;

    for (unsigned i = 0; i < w->obstacleCount; ++i) {
        dst->obstacles[i] = (EvolutionObstacleSnapshot){
            .id = w->obstacles[i].id,
            .position = w->obstacles[i].position,
            .radius = w->obstacles[i].radius,
            .height = w->obstacles[i].height
        };
    }
    dst->obstacleCount = w->obstacleCount;

    fillSelectedSnapshot(w, r->inspectTargetId, &dst->selected);
    EvolutionProfiler_End(&r->profiler, EVO_PROF_SNAPSHOT_PUB, tPub0);

    /* Publicar atómicamente rotando hacia un slot libre distinto al que ocupa el lector. */
    pthread_mutex_lock(&r->snapMutex);
    r->latestIdx = r->writeIdx;
    int nextWrite = -1;
    for (int i = 0; i < 3; ++i) {
        if (i != r->latestIdx && i != r->readerIdx) {
            nextWrite = i;
            break;
        }
    }
    if (nextWrite >= 0) r->writeIdx = nextWrite;
    pthread_mutex_unlock(&r->snapMutex);
    r->lastPublishMs = dst->publishedAtMs;
}

static void recordAckLocked(EvolutionSimulationRunner *r, uint64_t cmdId, EvolutionCommandType type,
                            EvolutionCommandStatus status, const char *msg) {
    EvolutionCommandAck ack = {.commandId = cmdId, .type = type, .status = status};
    if (msg) snprintf(ack.message, sizeof(ack.message), "%s", msg);
    r->acks[cmdId % EVO_RUNNER_ACK_HISTORY] = ack;
    if (status == EVO_CMD_STATUS_OK || status == EVO_CMD_STATUS_FAILED) {
        if (r->uiAckCount < EVO_RUNNER_ACK_HISTORY) {
            r->uiAckQueue[r->uiAckTail] = ack;
            r->uiAckTail = (r->uiAckTail + 1u) % EVO_RUNNER_ACK_HISTORY;
            ++r->uiAckCount;
        }
        pthread_cond_broadcast(&r->ackCond);
    }
}

static bool executeCommand(EvolutionSimulationRunner *r, const RunnerCommand *cmd,
                           char *msgOut, size_t msgCap) {
    EcosystemWorld *w = &r->world;
    char err[192] = {0};
    switch (cmd->type) {
    case EVO_CMD_PAUSE:
        w->clock.paused = true;
        r->scheduledDebt = 0.0;
        snprintf(msgOut, msgCap, "PAUSED");
        return true;
    case EVO_CMD_RESUME:
        w->clock.paused = false;
        snprintf(msgOut, msgCap, "RUNNING");
        return true;
    case EVO_CMD_TOGGLE_PAUSE:
        w->clock.paused = !w->clock.paused;
        if (w->clock.paused) r->scheduledDebt = 0.0;
        snprintf(msgOut, msgCap, "%s", w->clock.paused ? "PAUSED" : "RUNNING");
        return true;
    case EVO_CMD_SET_SPEED:
        if (!isfinite(cmd->valueDouble) || cmd->valueDouble <= 0.0) {
            snprintf(msgOut, msgCap, "INVALID SPEED");
            return false;
        }
        r->requestedSpeed = cmd->valueDouble;
        r->maxSpeedMode = cmd->valueBool;
        w->clock.speed = cmd->valueDouble;
        r->scheduledDebt = 0.0;
        snprintf(msgOut, msgCap, r->maxSpeedMode ? "SPEED MAX" : "SPEED %.0fX", r->requestedSpeed);
        return true;
    case EVO_CMD_SET_MAX_SPEED:
        r->maxSpeedMode = cmd->valueBool;
        r->scheduledDebt = 0.0;
        snprintf(msgOut, msgCap, r->maxSpeedMode ? "SPEED MAX" : "SPEED %.0fX", r->requestedSpeed);
        return true;
    case EVO_CMD_SET_WORKERS:
        if (!EcosystemWorld_SetWorkerCount(w, cmd->valueUint)) {
            snprintf(msgOut, msgCap, "WORKER CONFIG FAILED");
            return false;
        }
        snprintf(msgOut, msgCap, "WORKERS %u", EcosystemWorld_WorkerCount(w));
        return true;
    case EVO_CMD_SET_INSPECT_TARGET:
        r->inspectTargetId = cmd->idA;
        snprintf(msgOut, msgCap, "INSPECT #%u", cmd->idA);
        return true;
    case EVO_CMD_SET_INITIAL_AGE:
        for (unsigned k = 0; k < w->count; ++k) {
            EcosystemEntity *e = &w->entities[w->activeEntitySlots[k]];
            if (e->id) e->physiology.age = cmd->valueDouble;
        }
        snprintf(msgOut, msgCap, "INITIAL AGE %.1f", cmd->valueDouble);
        return true;
    case EVO_CMD_RESET: {
        bool wasPaused = w->clock.paused;
        unsigned prevWorkers = EcosystemWorld_WorkerCount(w);
        EcosystemWorld_Free(w);
        EvolutionArenaConfig nextCfg = cmd->resetConfig;
        if (nextCfg.workers == 0u) nextCfg.workers = prevWorkers;
        if (!EvolutionArena_Create(w, &nextCfg, err, sizeof(err))) {
            snprintf(msgOut, msgCap, "RESET FAILED: %.140s", err);
            return false;
        }
        EcosystemWorld_SetProfiler(w, &r->profiler);
        EvolutionProfiler_Reset(&r->profiler);
        w->clock.paused = wasPaused;
        w->clock.speed = r->requestedSpeed;
        r->currentConfig = nextCfg;
        r->scheduledDebt = 0.0;
        r->inspectTargetId = 0u;
        snprintf(msgOut, msgCap, "EXPERIMENT RESET (SEED %u)", nextCfg.seed);
        return true;
    }
    case EVO_CMD_ASSIGN_BRAIN:
        if (!cmd->hasGenome || !EcosystemWorld_AssignBrain(w, cmd->idA, &cmd->genome,
                                                           cmd->reason[0] ? cmd->reason : "assign")) {
            snprintf(msgOut, msgCap, "ASSIGN BRAIN FAILED #%u", cmd->idA);
            return false;
        }
        snprintf(msgOut, msgCap, "ASSIGNED BRAIN INTO #%u", cmd->idA);
        return true;
    case EVO_CMD_CLONE_BRAIN:
        if (!EcosystemWorld_CloneBrain(w, cmd->idA, cmd->idB)) {
            snprintf(msgOut, msgCap, "CLONE FAILED #%u -> #%u", cmd->idA, cmd->idB);
            return false;
        }
        snprintf(msgOut, msgCap, "CLONED BRAIN #%u -> #%u", cmd->idA, cmd->idB);
        return true;
    case EVO_CMD_SWAP_BRAINS:
        if (!EcosystemWorld_SwapBrains(w, cmd->idA, cmd->idB)) {
            snprintf(msgOut, msgCap, "SWAP FAILED #%u <-> #%u", cmd->idA, cmd->idB);
            return false;
        }
        snprintf(msgOut, msgCap, "SWAPPED BRAINS #%u <-> #%u", cmd->idA, cmd->idB);
        return true;
    case EVO_CMD_RESET_BRAIN_RANDOM:
        if (!EcosystemWorld_ResetBrainRandom(w, cmd->idA)) {
            snprintf(msgOut, msgCap, "RANDOM RESET FAILED #%u", cmd->idA);
            return false;
        }
        snprintf(msgOut, msgCap, "RESET #%u TO NEW RANDOM BRAIN", cmd->idA);
        return true;
    case EVO_CMD_IMPORT_BRAIN:
        if (!EcosystemWorld_ImportBrain(w, cmd->idA, cmd->path, err, sizeof(err))) {
            snprintf(msgOut, msgCap, "IMPORT ERR: %.140s", err);
            return false;
        }
        snprintf(msgOut, msgCap, "IMPORTED BRAIN INTO #%u", cmd->idA);
        return true;
    case EVO_CMD_EXPORT_BRAIN:
        if (!EcosystemWorld_ExportBrain(w, cmd->idA, cmd->path, cmd->valueBool, err, sizeof(err))) {
            snprintf(msgOut, msgCap, "EXPORT ERR: %.140s", err);
            return false;
        }
        snprintf(msgOut, msgCap, "EXPORTED #%u -> %.120s", cmd->idA, cmd->path);
        return true;
    case EVO_CMD_STEP_EXACT: {
        bool savedPause = w->clock.paused;
        w->clock.paused = false;
        for (unsigned i = 0; i < cmd->valueUint; ++i) {
            EcosystemWorld_Step(w);
        }
        w->clock.paused = savedPause;
        snprintf(msgOut, msgCap, "STEPPED %u TICKS", cmd->valueUint);
        return true;
    }
    case EVO_CMD_SHUTDOWN:
        r->stopRequested = true;
        snprintf(msgOut, msgCap, "SHUTDOWN");
        return true;
    default:
        snprintf(msgOut, msgCap, "UNKNOWN COMMAND");
        return false;
    }
}

static void drainCommands(EvolutionSimulationRunner *r, bool *didChange) {
    for (;;) {
        RunnerCommand cmd;
        pthread_mutex_lock(&r->cmdMutex);
        if (r->qCount == 0u) {
            pthread_mutex_unlock(&r->cmdMutex);
            break;
        }
        cmd = r->queue[r->qHead];
        r->qHead = (r->qHead + 1u) % EVO_RUNNER_CMD_QUEUE_CAPACITY;
        --r->qCount;
        pthread_mutex_unlock(&r->cmdMutex);

        char msg[192] = {0};
        bool ok = executeCommand(r, &cmd, msg, sizeof(msg));
        if (cmd.hasGenome) NeuralGenome_Destroy(&cmd.genome);
        if (didChange) *didChange = true;
        if (!r->stopRequested) publishSnapshot(r);

        pthread_mutex_lock(&r->cmdMutex);
        recordAckLocked(r, cmd.id, cmd.type, ok ? EVO_CMD_STATUS_OK : EVO_CMD_STATUS_FAILED, msg);
        pthread_mutex_unlock(&r->cmdMutex);

        if (r->stopRequested) break;
    }
}

static void *simulationThreadMain(void *opaque) {
    EvolutionSimulationRunner *r = (EvolutionSimulationRunner *)opaque;
    double lastWallMs = nowMs();
    r->windowStartMs = lastWallMs;
    publishSnapshot(r);

    while (!r->stopRequested) {
        bool cmdChanged = false;
        drainCommands(r, &cmdChanged);
        if (r->stopRequested) break;

        double now = nowMs();
        double realSec = (now - lastWallMs) * 1e-3;
        if (realSec < 0.0) realSec = 0.0;
        if (realSec > 0.1) realSec = 0.1;
        lastWallMs = now;

        unsigned ticksRun = 0u;
        double workStartMs = nowMs();
        if (!r->world.clock.paused) {
            double fixed = r->world.clock.fixedStep;
            if (r->maxSpeedMode) {
                /* Modo máxima velocidad sostenible: ejecutar lotes acotados de hasta 6 ms
                 * de CPU para mantener snapshots frescos y respuesta inmediata a comandos. */
                while (ticksRun < 64u) {
                    EcosystemWorld_Step(&r->world);
                    ++ticksRun;
                    if (nowMs() - workStartMs >= 6.0) break;
                }
                r->scheduledDebt = 0.0;
            } else {
                r->scheduledDebt += realSec * r->requestedSpeed;
                /* Acotar la deuda de reloj para evitar horas de puesta al día tras sobrecarga. */
                double maxDebt = fmin(fmax(0.25, 0.25 * r->requestedSpeed), 2.0);
                if (r->scheduledDebt > maxDebt) r->scheduledDebt = maxDebt;
                while (r->scheduledDebt + 1e-12 >= fixed && ticksRun < 64u) {
                    EcosystemWorld_Step(&r->world);
                    r->scheduledDebt = fmax(0.0, r->scheduledDebt - fixed);
                    ++ticksRun;
                    if (nowMs() - workStartMs >= 8.0) break;
                }
            }
        }
        double workMs = nowMs() - workStartMs;
        r->ticksInWindow += ticksRun;
        r->simWorkMsInWindow += workMs;

        double winElapsedMs = nowMs() - r->windowStartMs;
        if (winElapsedMs >= 250.0) {
            double winSec = winElapsedMs * 1e-3;
            r->ticksPerSecond = (double)r->ticksInWindow / winSec;
            r->achievedSpeed = r->ticksPerSecond * r->world.clock.fixedStep;
            r->simCpuUtil = fmin(100.0, (r->simWorkMsInWindow / winElapsedMs) * 100.0);
            r->ticksInWindow = 0u;
            r->simWorkMsInWindow = 0.0;
            r->windowStartMs = nowMs();
        }

        /* Publicar snapshot tras cambios de comando o al menos a ~60-120 Hz. */
        double sincePub = nowMs() - r->lastPublishMs;
        if (cmdChanged || (ticksRun > 0u && sincePub >= 8.0) || sincePub >= 33.0) {
            publishSnapshot(r);
        }

        if (r->world.clock.paused) {
            /* Dormir eficientemente en pausa hasta que llegue un comando. */
            pthread_mutex_lock(&r->cmdMutex);
            if (r->qCount == 0u && !r->stopRequested) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 16000000L;
                if (ts.tv_nsec >= 1000000000L) {
                    ts.tv_sec += 1;
                    ts.tv_nsec -= 1000000000L;
                }
                pthread_cond_timedwait(&r->cmdCond, &r->cmdMutex, &ts);
            }
            pthread_mutex_unlock(&r->cmdMutex);
        } else if (!r->maxSpeedMode && r->scheduledDebt < r->world.clock.fixedStep) {
            double waitSec = (r->world.clock.fixedStep - r->scheduledDebt) / fmax(r->requestedSpeed, 0.1);
            long us = (long)(waitSec * 1e6);
            if (us > 4000L) us = 4000L;
            if (us > 100L) sleepUs(us);
        }
    }
    return NULL;
}

static uint64_t enqueueCommand(EvolutionSimulationRunner *r, RunnerCommand cmd) {
    if (!r) return 0u;
    pthread_mutex_lock(&r->cmdMutex);
    if (r->qCount >= EVO_RUNNER_CMD_QUEUE_CAPACITY) {
        pthread_mutex_unlock(&r->cmdMutex);
        if (cmd.hasGenome) NeuralGenome_Destroy(&cmd.genome);
        return 0u;
    }
    uint64_t id = ++r->nextCmdId;
    cmd.id = id;
    r->queue[r->qTail] = cmd;
    r->qTail = (r->qTail + 1u) % EVO_RUNNER_CMD_QUEUE_CAPACITY;
    ++r->qCount;
    recordAckLocked(r, id, cmd.type, EVO_CMD_STATUS_PENDING, "PENDING");
    pthread_cond_signal(&r->cmdCond);
    pthread_mutex_unlock(&r->cmdMutex);
    return id;
}

EvolutionSimulationRunner *EvolutionSimulationRunner_Create(const EvolutionArenaConfig *config,
                                                            bool startPaused,
                                                            char *error, size_t errorSize) {
    if (!config) return NULL;
    EvolutionSimulationRunner *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->currentConfig = *config;
    r->requestedSpeed = 1.0;
    r->writeIdx = 0;
    r->latestIdx = -1;
    r->readerIdx = -1;

    if (!EvolutionArena_Create(&r->world, config, error, errorSize)) {
        free(r);
        return NULL;
    }
    EvolutionProfiler_Init(&r->profiler, true);
    EcosystemWorld_SetProfiler(&r->world, &r->profiler);
    r->world.clock.paused = startPaused;

    pthread_mutex_init(&r->cmdMutex, NULL);
    pthread_cond_init(&r->cmdCond, NULL);
    pthread_cond_init(&r->ackCond, NULL);
    pthread_mutex_init(&r->snapMutex, NULL);

    /* Publicar snapshot inicial síncronamente antes de arrancar el hilo para disponibilidad inmediata. */
    publishSnapshot(r);

    if (pthread_create(&r->thread, NULL, simulationThreadMain, r) != 0) {
        for (int i = 0; i < 3; ++i) freeSnapshot(&r->buffers[i]);
        pthread_mutex_destroy(&r->snapMutex);
        pthread_cond_destroy(&r->ackCond);
        pthread_cond_destroy(&r->cmdCond);
        pthread_mutex_destroy(&r->cmdMutex);
        EcosystemWorld_Free(&r->world);
        free(r);
        return NULL;
    }
    r->threadStarted = true;
    return r;
}

void EvolutionSimulationRunner_Destroy(EvolutionSimulationRunner *r) {
    if (!r) return;
    if (r->threadStarted) {
        EvolutionSimulationRunner_PostSimple(r, EVO_CMD_SHUTDOWN);
        pthread_join(r->thread, NULL);
        r->threadStarted = false;
    }
    /* Liberar genomas pendientes en cola si los hubiera. */
    while (r->qCount > 0u) {
        RunnerCommand *c = &r->queue[r->qHead];
        if (c->hasGenome) NeuralGenome_Destroy(&c->genome);
        r->qHead = (r->qHead + 1u) % EVO_RUNNER_CMD_QUEUE_CAPACITY;
        --r->qCount;
    }
    for (int i = 0; i < 3; ++i) freeSnapshot(&r->buffers[i]);
    pthread_mutex_destroy(&r->snapMutex);
    pthread_cond_destroy(&r->ackCond);
    pthread_cond_destroy(&r->cmdCond);
    pthread_mutex_destroy(&r->cmdMutex);
    EcosystemWorld_Free(&r->world);
    free(r);
}

uint64_t EvolutionSimulationRunner_PostSimple(EvolutionSimulationRunner *r, EvolutionCommandType type) {
    RunnerCommand cmd = {.type = type};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostSetSpeed(EvolutionSimulationRunner *r,
                                                double speed, bool maxSpeedMode) {
    RunnerCommand cmd = {.type = EVO_CMD_SET_SPEED, .valueDouble = speed, .valueBool = maxSpeedMode};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostSetWorkers(EvolutionSimulationRunner *r, unsigned workers) {
    RunnerCommand cmd = {.type = EVO_CMD_SET_WORKERS, .valueUint = workers};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostInspectTarget(EvolutionSimulationRunner *r, AIObjectId entityId) {
    RunnerCommand cmd = {.type = EVO_CMD_SET_INSPECT_TARGET, .idA = entityId};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostReset(EvolutionSimulationRunner *r,
                                             const EvolutionArenaConfig *config) {
    if (!config) return 0u;
    RunnerCommand cmd = {.type = EVO_CMD_RESET, .resetConfig = *config};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostAssignBrain(EvolutionSimulationRunner *r,
                                                   AIObjectId entityId,
                                                   const NeuralGenome *genome,
                                                   const char *reason) {
    if (!genome) return 0u;
    RunnerCommand cmd = {.type = EVO_CMD_ASSIGN_BRAIN, .idA = entityId};
    if (!NeuralGenome_Clone(&cmd.genome, genome)) return 0u;
    cmd.hasGenome = true;
    if (reason) snprintf(cmd.reason, sizeof(cmd.reason), "%s", reason);
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostCloneBrain(EvolutionSimulationRunner *r,
                                                  AIObjectId sourceId, AIObjectId targetId) {
    RunnerCommand cmd = {.type = EVO_CMD_CLONE_BRAIN, .idA = sourceId, .idB = targetId};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostSwapBrains(EvolutionSimulationRunner *r,
                                                  AIObjectId idA, AIObjectId idB) {
    RunnerCommand cmd = {.type = EVO_CMD_SWAP_BRAINS, .idA = idA, .idB = idB};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostResetBrainRandom(EvolutionSimulationRunner *r,
                                                        AIObjectId entityId) {
    RunnerCommand cmd = {.type = EVO_CMD_RESET_BRAIN_RANDOM, .idA = entityId};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostImportBrain(EvolutionSimulationRunner *r,
                                                   AIObjectId entityId, const char *path) {
    if (!path) return 0u;
    RunnerCommand cmd = {.type = EVO_CMD_IMPORT_BRAIN, .idA = entityId};
    snprintf(cmd.path, sizeof(cmd.path), "%s", path);
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostExportBrain(EvolutionSimulationRunner *r,
                                                   AIObjectId entityId, const char *path,
                                                   bool overwrite) {
    if (!path) return 0u;
    RunnerCommand cmd = {.type = EVO_CMD_EXPORT_BRAIN, .idA = entityId, .valueBool = overwrite};
    snprintf(cmd.path, sizeof(cmd.path), "%s", path);
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostStepExact(EvolutionSimulationRunner *r, unsigned ticks) {
    RunnerCommand cmd = {.type = EVO_CMD_STEP_EXACT, .valueUint = ticks};
    return enqueueCommand(r, cmd);
}

uint64_t EvolutionSimulationRunner_PostSetInitialAge(EvolutionSimulationRunner *r, double age) {
    RunnerCommand cmd = {.type = EVO_CMD_SET_INITIAL_AGE, .valueDouble = age};
    return enqueueCommand(r, cmd);
}

bool EvolutionSimulationRunner_GetAck(EvolutionSimulationRunner *r, uint64_t commandId,
                                      EvolutionCommandAck *outAck) {
    if (!r || !commandId || !outAck) return false;
    pthread_mutex_lock(&r->cmdMutex);
    EvolutionCommandAck a = r->acks[commandId % EVO_RUNNER_ACK_HISTORY];
    pthread_mutex_unlock(&r->cmdMutex);
    if (a.commandId != commandId) return false;
    *outAck = a;
    return true;
}

bool EvolutionSimulationRunner_WaitCommand(EvolutionSimulationRunner *r, uint64_t commandId,
                                           double timeoutSeconds, EvolutionCommandAck *outAck) {
    if (!r || !commandId) return false;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    long addSec = (long)floor(timeoutSeconds);
    long addNs = (long)((timeoutSeconds - (double)addSec) * 1e9);
    deadline.tv_sec += addSec;
    deadline.tv_nsec += addNs;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&r->cmdMutex);
    for (;;) {
        EvolutionCommandAck a = r->acks[commandId % EVO_RUNNER_ACK_HISTORY];
        if (a.commandId == commandId &&
            (a.status == EVO_CMD_STATUS_OK || a.status == EVO_CMD_STATUS_FAILED)) {
            if (outAck) *outAck = a;
            pthread_mutex_unlock(&r->cmdMutex);
            return a.status == EVO_CMD_STATUS_OK;
        }
        int rc = pthread_cond_timedwait(&r->ackCond, &r->cmdMutex, &deadline);
        if (rc != 0) {
            pthread_mutex_unlock(&r->cmdMutex);
            return false;
        }
    }
}

bool EvolutionSimulationRunner_PopAck(EvolutionSimulationRunner *r, EvolutionCommandAck *outAck) {
    if (!r || !outAck) return false;
    pthread_mutex_lock(&r->cmdMutex);
    if (r->uiAckCount == 0u) {
        pthread_mutex_unlock(&r->cmdMutex);
        return false;
    }
    *outAck = r->uiAckQueue[r->uiAckHead];
    r->uiAckHead = (r->uiAckHead + 1u) % EVO_RUNNER_ACK_HISTORY;
    --r->uiAckCount;
    pthread_mutex_unlock(&r->cmdMutex);
    return true;
}

const EvolutionRenderSnapshot *EvolutionSimulationRunner_AcquireSnapshot(EvolutionSimulationRunner *r) {
    if (!r) return NULL;
    pthread_mutex_lock(&r->snapMutex);
    if (r->latestIdx < 0) {
        pthread_mutex_unlock(&r->snapMutex);
        return NULL;
    }
    r->readerIdx = r->latestIdx;
    const EvolutionRenderSnapshot *snap = &r->buffers[r->readerIdx];
    pthread_mutex_unlock(&r->snapMutex);
    return snap;
}

void EvolutionSimulationRunner_ReleaseSnapshot(EvolutionSimulationRunner *r,
                                             const EvolutionRenderSnapshot *snapshot) {
    if (!r || !snapshot) return;
    pthread_mutex_lock(&r->snapMutex);
    if (r->readerIdx >= 0 && &r->buffers[r->readerIdx] == snapshot) {
        r->readerIdx = -1;
    }
    pthread_mutex_unlock(&r->snapMutex);
}

double EvolutionSimulationRunner_NowMs(void) {
    return nowMs();
}
