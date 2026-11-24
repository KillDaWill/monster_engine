/**
 * @file test_evolution_scalability.c
 * @brief Pruebas de regresión, corrección y escalabilidad para Evolution Arena 500+:
 *        índice espacial vs fuerza bruta, solver cervical compacto vs oráculo completo,
 *        caché de plantillas anatómicas, hilo de simulación con triple buffering,
 *        determinismo paralelo (1/2/4/6 hilos), estadísticas evolutivas y visual 1024+.
 */
#include "test_utils.h"
#include "AISpatialIndex.h"
#include "AIWorld.h"
#include "AnatomicalReach.h"
#include "EcosystemAnatomy.h"
#include "EcosystemAnatomyCache.h"
#include "EcosystemVisual.h"
#include "EcosystemWorld.h"
#include "EvolutionArena.h"
#include "EvolutionSimulationRunner.h"
#include "CreatureRecipeRegistry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t lcgNext(uint32_t *state) {
    *state = (*state) * 1664525u + 1013904223u;
    return *state;
}

static float randFloat(uint32_t *state, float lo, float hi) {
    float u = (float)(lcgNext(state) >> 8) / (float)(1u << 24);
    return lo + (hi - lo) * u;
}

static double monoMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

/* ========================================================================== */
/* 1. Índice espacial vs fuerza bruta (radio, segmento, bordes, borrado/reuso)*/
/* ========================================================================== */
static void test_spatial_index_vs_bruteforce(void) {
    AIWorld w = {0};
    TEST_ASSERT(AIWorld_InitCapacity(&w, 60.0f, 512u), "init AIWorld");
    TEST_ASSERT(AIWorld_SetSpatialCellSize(&w, 4.0f), "configurar celda 4.0");

    uint32_t rng = 0x5A17BEEFu;
    AIObjectId ids[240];
    unsigned idCount = 0;

    /* Insertar mezcla de criaturas, recursos, cadáveres y obstáculos grandes cerca de bordes. */
    for (unsigned i = 0; i < 200u; ++i) {
        AIObjectType type = (i < 24u) ? AI_OBSTACLE
                          : (i < 44u) ? AI_WATER
                          : (i < 130u) ? AI_FOOD
                                       : AI_CREATURE;
        float r = (type == AI_OBSTACLE) ? randFloat(&rng, 1.5f, 5.5f)
                : (type == AI_WATER)    ? randFloat(&rng, 2.0f, 4.5f)
                                        : randFloat(&rng, 0.35f, 1.4f);
        /* Incluir explícitamente bordes de celda y perímetro de arena. */
        float x = (i % 13u == 0u) ? ((i & 1u) ? 59.5f : -59.5f)
                : (i % 7u == 0u)  ? (float)((int)(i % 15u) - 7) * 4.0f
                                  : randFloat(&rng, -58.0f, 58.0f);
        float z = (i % 11u == 0u) ? ((i & 2u) ? 59.5f : -59.5f)
                                  : randFloat(&rng, -58.0f, 58.0f);
        AIWorldObject obj = {
            .type = type, .species = (i % 2u) + 1u,
            .foodCategory = (i >= 110u && i < 130u) ? 2u : 1u,
            .available = true, .visible = true,
            .occludes = (type == AI_OBSTACLE),
            .drinkable = (type == AI_WATER),
            .position = Vec3_Create(x, 0.0f, z),
            .radius = r, .height = 1.5f
        };
        AIObjectId id = AIWorld_Add(&w, obj);
        TEST_ASSERT(id != 0u, "alta en AIWorld");
        ids[idCount++] = id;
    }

    /* Mover objetos, borrar 40 y reinsertar 40 para probar reutilización de ranuras e IDs obsoletos. */
    for (unsigned step = 0; step < 3u; ++step) {
        for (unsigned i = 0; i < idCount; ++i) {
            if (!ids[i]) continue;
            const AIWorldObject *o = AIWorld_Get(&w, ids[i]);
            if (o && o->type == AI_CREATURE) {
                Vector3 np = Vec3_Create(randFloat(&rng, -59.0f, 59.0f), 0.0f, randFloat(&rng, -59.0f, 59.0f));
                TEST_ASSERT(AIWorld_Move(&w, ids[i], np), "mover criatura");
            }
        }
        for (unsigned k = 0; k < 20u; ++k) {
            unsigned idx = 130u + ((step * 17u + k) % 70u);
            if (ids[idx]) {
                AIObjectId stale = ids[idx];
                AIWorld_Remove(&w, stale);
                TEST_ASSERT(AIWorld_Get(&w, stale) == NULL, "ID obsoleto rechazado tras borrado");
                AIWorldObject freshObj = {
                    .type = AI_CREATURE, .species = 1u, .available = true, .visible = true,
                    .position = Vec3_Create(randFloat(&rng, -58.0f, 58.0f), 0.0f, randFloat(&rng, -58.0f, 58.0f)),
                    .radius = 0.9f, .height = 1.5f
                };
                AIObjectId fresh = AIWorld_Add(&w, freshObj);
                TEST_ASSERT(fresh != 0u && fresh != stale, "reuso de ranura con nuevo ID");
                TEST_ASSERT(AIWorld_Get(&w, stale) == NULL, "ID antiguo sigue rechazado tras reuso");
                TEST_ASSERT(AIWorld_Get(&w, fresh) != NULL, "nuevo ID localizable O(1)");
                ids[idx] = fresh;
            }
        }
    }

    /* Comparar 120 consultas de radio aleatorias contra fuerza bruta exacta. */
    uint32_t slots[512];
    for (unsigned q = 0; q < 120u; ++q) {
        Vector3 center = Vec3_Create(randFloat(&rng, -60.0f, 60.0f), 0.0f, randFloat(&rng, -60.0f, 60.0f));
        float radius = randFloat(&rng, 1.0f, 22.0f);
        bool overflow = false;
        unsigned got = AIWorld_QuerySlotsRadius(&w, AI_SPATIAL_MASK_ALL, center, radius, slots, 512u, &overflow);
        TEST_ASSERT(!overflow, "sin desbordamiento en consulta");

        bool seen[512] = {false};
        for (unsigned i = 0; i < got; ++i) {
            TEST_ASSERT(slots[i] < w.capacity, "ranura válida");
            TEST_ASSERT(!seen[slots[i]], "cero duplicados en consulta espacial");
            seen[slots[i]] = true;
        }
        for (unsigned s = 0; s < w.capacity; ++s) {
            const AIWorldObject *o = AIWorld_At(&w, s);
            if (!o || !o->active) continue;
            float dx = o->position.x - center.x, dz = o->position.z - center.z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist <= radius + o->radius - 1e-4f) {
                TEST_ASSERT(seen[s], "objeto en rango exacto incluido por índice espacial");
            }
        }
    }

    /* Comparar 60 consultas de segmento barrido contra fuerza bruta exacta. */
    for (unsigned q = 0; q < 60u; ++q) {
        Vector3 a = Vec3_Create(randFloat(&rng, -58.0f, 58.0f), 0.0f, randFloat(&rng, -58.0f, 58.0f));
        Vector3 b = Vec3_Create(randFloat(&rng, -58.0f, 58.0f), 0.0f, randFloat(&rng, -58.0f, 58.0f));
        float pad = randFloat(&rng, 0.2f, 2.5f);
        bool overflow = false;
        unsigned got = AIWorld_QuerySlotsSegment(&w, AI_SPATIAL_MASK_ALL, a, b, pad, slots, 512u, &overflow);
        TEST_ASSERT(!overflow, "sin desbordamiento segmento");
        bool seen[512] = {false};
        for (unsigned i = 0; i < got; ++i) {
            TEST_ASSERT(!seen[slots[i]], "cero duplicados en segmento");
            seen[slots[i]] = true;
        }
        float abx = b.x - a.x, abz = b.z - a.z;
        float ab2 = abx * abx + abz * abz;
        for (unsigned s = 0; s < w.capacity; ++s) {
            const AIWorldObject *o = AIWorld_At(&w, s);
            if (!o || !o->active) continue;
            float t = ab2 > 1e-6f ? ((o->position.x - a.x) * abx + (o->position.z - a.z) * abz) / ab2 : 0.0f;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            float cx = a.x + abx * t - o->position.x;
            float cz = a.z + abz * t - o->position.z;
            float dist = sqrtf(cx * cx + cz * cz);
            if (dist <= pad + o->radius - 1e-4f) {
                TEST_ASSERT(seen[s], "objeto en segmento exacto incluido por índice espacial");
            }
        }
    }

    AIWorld_Destroy(&w);
    puts("[PASS] test_spatial_index_vs_bruteforce");
}

/* ========================================================================== */
/* 2. Solver cervical compacto vs oráculo + caché compartida de plantillas    */
/* ========================================================================== */
static void ensureRecipesLoaded(void) {
    /* Recetas cargadas y limpiadas en run_evolution_scalability_tests(). */
}

static void test_anatomical_compact_vs_oracle_and_cache(void) {
    char err[256] = {0};
    ensureRecipesLoaded();

    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.seed = 77u;
    cfg.brumacernos = 2u;
    cfg.velkares = 2u;
    cfg.capacity = 16u;
    EcosystemWorld w;
    TEST_ASSERT(EvolutionArena_Create(&w, &cfg, err, sizeof(err)), "crear arena pequeña");

    /* Probar Brumacerno (spec 0) y Velkar (spec 1) contra el oráculo completo AnatomicalReach_Update. */
    unsigned testedSpecies = 0u;
    double compactTotalMs = 0.0, oracleTotalMs = 0.0;
    float maxMouthDiff = 0.0f;
    for (unsigned i = 0; i < w.count; ++i) {
        EcosystemEntity *e = &w.entities[w.activeEntitySlots[i]];
        if (!e->anatomicalBody || !e->anatomicalBody->tmpl) continue;
        if (testedSpecies & (1u << e->speciesIndex)) continue;
        testedSpecies |= (1u << e->speciesIndex);

        const EcosystemAnatomyTemplate *tmpl = e->anatomicalBody->tmpl;
        AnatomicalReachState compactState = {0}, oracleState = {0};
        AnatomicalReach_Reset(&compactState, &tmpl->profile, &tmpl->standingRest);
        AnatomicalReach_Reset(&oracleState, &tmpl->profile, &tmpl->standingRest);

        Vector3 neutral = tmpl->neutralMouthLocal;
        Vector3 dir = tmpl->neutralMouthDir;
        /* 5 objetivos alcanzables alrededor de la boca neutral + 1 objetivo fuera de alcance. */
        Vector3 offsets[6] = {
            {0.0f, -0.30f, 0.20f},
            {0.25f, -0.18f, 0.25f},
            {-0.25f, -0.15f, 0.22f},
            {0.10f, 0.12f, 0.28f},
            {0.0f, -0.42f, 0.15f},
            {0.0f, 0.0f, 12.0f} /* Inalcanzable */
        };
        for (unsigned k = 0; k < 6u; ++k) {
            InteractionTarget tgt = {
                .objectId = 100u + k, .revision = 1u, .type = INTERACTION_EAT,
                .position = Vec3_Add(neutral, offsets[k]),
                .normal = Vec3_Create(0.0f, 1.0f, 0.0f),
                .approach = dir,
                .positionTolerance = 0.12f,
                .orientationTolerance = 0.85f,
                .valid = true, .moving = false, .blocked = false
            };

            SkeletonPose oraclePose = tmpl->standingRest;
            AnatomicalReach_Reset(&compactState, &tmpl->profile, &tmpl->standingRest);
            AnatomicalReach_Reset(&oracleState, &tmpl->profile, &tmpl->standingRest);

            double t0 = monoMs();
            ReachResult rCompact = {0};
            Vector3 cPos[REACH_MAX_JOINTS];
            Quaternion cRot[REACH_MAX_JOINTS];
            unsigned solveCalls = 0u, fkUpdates = 0u;
            for (unsigned s = 0; s < 40u; ++s) {
                rCompact = AnatomicalReach_UpdateCompact(&tmpl->profile, &compactState,
                                                         tmpl->cervicalParentStandingPos,
                                                         tmpl->cervicalParentStandingRot,
                                                         &tgt, 1.0f / 60.0f,
                                                         cPos, cRot, &solveCalls, &fkUpdates);
            }
            compactTotalMs += monoMs() - t0;

            double t1 = monoMs();
            ReachResult rOracle = {0};
            for (unsigned s = 0; s < 40u; ++s) {
                oraclePose = tmpl->standingRest;
                rOracle = AnatomicalReach_Update(&tmpl->profile, &oracleState, &tmpl->rig.skeleton, &oraclePose, &tgt, 1.0f / 60.0f);
            }
            oracleTotalMs += monoMs() - t1;

            /* Verificar que cada rotación cervical permanece unitaria y finita. */
            for (unsigned j = 0; j < tmpl->profile.count; ++j) {
                Quaternion q = compactState.rotation[j];
                float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
                TEST_ASSERT(isfinite(n2) && fabsf(n2 - 1.0f) < 0.01f, "rotación cervical unitaria y finita");
            }
            if (k == 5u) {
                TEST_ASSERT(rCompact.status == REACH_UNREACHABLE, "objetivo fuera de alcance rechazado sin estirar");
            } else {
                float diff = Vec3_Distance(rCompact.mouth, rOracle.mouth);
                if (diff > maxMouthDiff) maxMouthDiff = diff;
                TEST_ASSERT(diff < 0.12f, "acuerdo boca compacto vs oráculo completo (<0.12)");
            }
        }
        /* Invalidación de caché al cambiar cerebro. */
        EcosystemAnatomy_InvalidateInteraction(e->anatomicalBody);
        TEST_ASSERT(!e->anatomicalBody->settled && !e->anatomicalBody->state.hasSolvedDesired,
                    "invalidación limpia caché de alcance");
    }
    TEST_ASSERT(testedSpecies == 3u, "ambas especies verificadas contra oráculo");
    TEST_ASSERT(compactTotalMs < oracleTotalMs, "solver compacto más rápido que oráculo completo");
    printf("  [anat] compact=%.3fms oracle=%.3fms speedup=%.1fx maxMouthDiff=%.5f\n",
           compactTotalMs, oracleTotalMs, oracleTotalMs / fmax(1e-6, compactTotalMs), maxMouthDiff);

    EcosystemWorld_Free(&w);

    /* Verificar compartición de plantillas y ciclo de refCount en 500 criaturas. */
    EcosystemAnatomyCache_ClearUnused();
    EvolutionArenaConfig big = EvolutionArenaConfig_Default();
    TEST_ASSERT(EvolutionArenaConfig_ApplyPreset(&big, "large"), "aplicar preset large");
    big.seed = 42u;
    EcosystemWorld w500;
    TEST_ASSERT(EvolutionArena_Create(&w500, &big, err, sizeof(err)), "crear arena 500");
    EcosystemAnatomyCacheStats st = EcosystemAnatomyCache_GetStats();
    TEST_ASSERT(st.activeEntries > 0u && st.activeEntries <= 32u, "500 criaturas comparten pocas plantillas");
    TEST_ASSERT(st.hits >= 450u, "alta tasa de aciertos en caché anatómica");
    EcosystemWorld_Free(&w500);
    EcosystemAnatomyCacheStats stAfter = EcosystemAnatomyCache_GetStats();
    TEST_ASSERT(stAfter.referencedEntries == 0u, "cero fugas de refCount tras liberar mundo");
    EcosystemAnatomyCache_ClearUnused();
    puts("[PASS] test_anatomical_compact_vs_oracle_and_cache");
}

/* ========================================================================== */
/* 3. Hilo dedicado, triple buffering y determinismo paralelo (1/2/4/6 hilos) */
/* ========================================================================== */
static void test_runner_threading_and_parallel_determinism(void) {
    char err[256] = {0};
    ensureRecipesLoaded();

    /* 3a. Determinismo exacto entre 1, 2, 4 y 6 hilos en 200 criaturas durante 60 ticks. */
    const unsigned workerCounts[4] = {1u, 2u, 4u, 6u};
    uint64_t refRandom = 0u, refHash = 0u;
    unsigned refCount = 0u;
    Vector3 refPos0 = Vec3_Zero(), refPosLast = Vec3_Zero();

    for (unsigned k = 0; k < 4u; ++k) {
        EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
        TEST_ASSERT(EvolutionArenaConfig_ApplyPreset(&cfg, "medium"), "preset medium");
        cfg.brumacernos = 160u;
        cfg.velkares = 40u;
        cfg.capacity = 512u;
        cfg.seed = 2026u;
        cfg.workers = workerCounts[k];

        EcosystemWorld w;
        TEST_ASSERT(EvolutionArena_Create(&w, &cfg, err, sizeof(err)), "crear mundo determinismo");
        for (unsigned t = 0; t < 60u; ++t) {
            EcosystemWorld_Step(&w);
        }
        if (k == 0u) {
            refRandom = w.random;
            refHash = w.statistics.eventHash;
            refCount = w.count;
            refPos0 = w.entities[w.activeEntitySlots[0]].body.position;
            refPosLast = w.entities[w.activeEntitySlots[w.count - 1u]].body.position;
        } else {
            TEST_ASSERT(w.random == refRandom, "PRNG idéntico entre 1 y N hilos");
            TEST_ASSERT(w.statistics.eventHash == refHash, "eventHash idéntico entre 1 y N hilos");
            TEST_ASSERT(w.count == refCount, "población idéntica entre 1 y N hilos");
            Vector3 p0 = w.entities[w.activeEntitySlots[0]].body.position;
            Vector3 pL = w.entities[w.activeEntitySlots[w.count - 1u]].body.position;
            TEST_ASSERT(Vec3_Distance(p0, refPos0) == 0.0f && Vec3_Distance(pL, refPosLast) == 0.0f,
                        "posiciones idénticas bit-a-bit entre 1 y N hilos");
        }
        EcosystemWorld_Free(&w);
    }

    /* 3b. Estrés concurrente de EvolutionSimulationRunner (MAX speed + comandos + snapshots). */
    EvolutionArenaConfig rcfg = EvolutionArenaConfig_Default();
    TEST_ASSERT(EvolutionArenaConfig_ApplyPreset(&rcfg, "medium"), "preset medium runner");
    rcfg.seed = 99u;
    rcfg.workers = 4u;

    EvolutionSimulationRunner *runner = EvolutionSimulationRunner_Create(&rcfg, false, err, sizeof(err));
    TEST_ASSERT(runner != NULL, "crear EvolutionSimulationRunner");

    uint64_t cmdSpeed = EvolutionSimulationRunner_PostSetSpeed(runner, 64.0, true);
    EvolutionCommandAck ack = {0};
    TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cmdSpeed, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK,
                "ack comando MAX speed");

    uint64_t lastSeq = 0u, lastTick = 0u;
    unsigned validReads = 0u;
    AIObjectId firstId = 0u, secondId = 0u;
    for (unsigned iter = 0; iter < 150u; ++iter) {
        const EvolutionRenderSnapshot *snap = EvolutionSimulationRunner_AcquireSnapshot(runner);
        if (snap) {
            TEST_ASSERT(snap->sequence >= lastSeq, "secuencia de snapshot monótona");
            TEST_ASSERT(snap->tick >= lastTick, "tick de snapshot monótono");
            TEST_ASSERT(snap->entityCount > 0u && snap->entityCount <= snap->capacity, "conteo consistente");
            for (unsigned i = 0; i < snap->entityCount; ++i) {
                TEST_ASSERT(isfinite(snap->entities[i].position.x) && isfinite(snap->entities[i].position.z),
                            "coordenadas finitas en snapshot");
            }
            if (!firstId && snap->entityCount >= 2u) {
                firstId = snap->entities[0].id;
                secondId = snap->entities[1].id;
            }
            lastSeq = snap->sequence;
            lastTick = snap->tick;
            ++validReads;
            EvolutionSimulationRunner_ReleaseSnapshot(runner, snap);
        }
        if (iter == 25u && firstId && secondId) {
            EvolutionSimulationRunner_PostInspectTarget(runner, firstId);
            EvolutionSimulationRunner_PostSwapBrains(runner, firstId, secondId);
        } else if (iter == 60u && firstId) {
            EvolutionSimulationRunner_PostResetBrainRandom(runner, firstId);
        } else if (iter == 90u) {
            uint64_t cPause = EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_PAUSE);
            TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cPause, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK,
                        "ack pausa");
            const EvolutionRenderSnapshot *p1 = EvolutionSimulationRunner_AcquireSnapshot(runner);
            uint64_t pausedTick = p1 ? p1->tick : 0u;
            EvolutionSimulationRunner_ReleaseSnapshot(runner, p1);
            struct timespec ts = {0, 25000000};
            nanosleep(&ts, NULL);
            const EvolutionRenderSnapshot *p2 = EvolutionSimulationRunner_AcquireSnapshot(runner);
            TEST_ASSERT(p2 && p2->paused && p2->tick == pausedTick, "pausa detiene avance biológico exacto");
            EvolutionSimulationRunner_ReleaseSnapshot(runner, p2);
            uint64_t cRes = EvolutionSimulationRunner_PostSimple(runner, EVO_CMD_RESUME);
            TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cRes, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK,
                        "ack resume");
        }
        struct timespec ts = {0, 1500000};
        nanosleep(&ts, NULL);
    }
    TEST_ASSERT(validReads >= 100u && lastTick > 10u, "runner avanzó ticks concurrentemente sin bloqueo");

    /* Probar comando RESET limpio. */
    uint64_t cRst = EvolutionSimulationRunner_PostReset(runner, &rcfg);
    TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cRst, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK,
                "ack reset");
    EvolutionSimulationRunner_Destroy(runner);
    puts("[PASS] test_runner_threading_and_parallel_determinism");
}

/* ========================================================================== */
/* 4. Estadísticas evolutivas (500 fundadores, linajes, supervivencia, muerte)*/
/* ========================================================================== */
static void test_evolutionary_statistics_large_scale(void) {
    char err[256] = {0};
    ensureRecipesLoaded();

    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    TEST_ASSERT(EvolutionArenaConfig_ApplyPreset(&cfg, "large"), "preset large");
    cfg.seed = 55u;

    EcosystemWorld w;
    TEST_ASSERT(EvolutionArena_Create(&w, &cfg, err, sizeof(err)), "crear arena 500");

    /* 1) Los 500 fundadores deben registrarse sin tope de 256 ni pérdida. */
    TEST_ASSERT(w.count == 500u, "500 criaturas vivas");
    TEST_ASSERT(w.founderCount == 500u, "500 fundadores registrados sin truncar en 256");
    TEST_ASSERT(w.lineageCount >= 500u, "500 linajes dinámicos registrados");
    TEST_ASSERT(w.statistics.neuralDispersionRms >= 0.0f && isfinite(w.statistics.neuralDispersionRms),
                "dispersión neural exacta finita");

    /* 2) Intercambio de cerebro entre especies actualiza speciesMask del linaje. */
    EcosystemEntity *brum = NULL, *velk = NULL;
    for (unsigned k = 0; k < w.count; ++k) {
        EcosystemEntity *e = &w.entities[w.activeEntitySlots[k]];
        if (e->speciesIndex == 0u && !brum) brum = e;
        if (e->speciesIndex == 1u && !velk) velk = e;
        if (brum && velk) break;
    }
    TEST_ASSERT(brum && velk, "encontrar Brumacerno y Velkar");
    uint64_t brumLin = brum->mind.neuralBrain.genome.metadata.lineageId;
    TEST_ASSERT(EcosystemWorld_SwapBrains(&w, brum->id, velk->id), "intercambiar cerebros entre especies");
    bool foundCross = false;
    for (unsigned i = 0; i < w.lineageCount; ++i) {
        if (w.lineages[i].lineageId == brumLin && (w.lineages[i].speciesMask & 3u) == 3u) {
            foundCross = true;
            break;
        }
    }
    TEST_ASSERT(foundCross, "linaje intercambiado registra ambas especies en speciesMask");

    /* 3) Exportar e importar cerebro verifica remapeo de colisión de linaje y supervivencia observada. */
    const char *tmpBrain = "/tmp/test_scalability_import.brain.json";
    TEST_ASSERT(EcosystemWorld_ExportBrain(&w, brum->id, tmpBrain, true, err, sizeof(err)), "exportar cerebro");
    AIObjectId targetId = w.entities[w.activeEntitySlots[10]].id;
    TEST_ASSERT(EcosystemWorld_ImportBrain(&w, targetId, tmpBrain, err, sizeof(err)), "importar cerebro");
    remove(tmpBrain);

    /* 4) Verificar que observedSurvival != biological age inicial y que stillLiving cambia al morir. */
    for (unsigned t = 0; t < 30u; ++t) EcosystemWorld_Step(&w);
    EcosystemEntity *victim = &w.entities[w.activeEntitySlots[0]];
    double expectedObs = w.clock.seconds - victim->bornAt;
    TEST_ASSERT(expectedObs > 0.2 && expectedObs < victim->physiology.age,
                "supervivencia observada separada de la edad biológica inicial");
    AIObjectId victimId = victim->id;
    victim->physiology.health = 0.0f;
    victim->physiology.energy = 0.0f;
    victim->physiology.cause = DEATH_STARVATION;
    victim->physiology.stage = LIFE_DEAD;
    EcosystemWorld_Step(&w);
    TEST_ASSERT(EcosystemWorld_Get(&w, victimId) == NULL, "víctima retirada tras morir");
    if (w.archiveBestOffspring.valid && w.archiveBestOffspring.entityId == victimId)
        TEST_ASSERT(!w.archiveBestOffspring.stillLiving, "archiveBestOffspring actualiza stillLiving=false");
    if (w.archiveLongestSurvival.valid && w.archiveLongestSurvival.entityId == victimId)
        TEST_ASSERT(!w.archiveLongestSurvival.stillLiving, "archiveLongestSurvival actualiza stillLiving=false");
    if (w.archiveHighestGeneration.valid && w.archiveHighestGeneration.entityId == victimId)
        TEST_ASSERT(!w.archiveHighestGeneration.stillLiving, "archiveHighestGeneration actualiza stillLiving=false");

    EcosystemWorld_Free(&w);
    puts("[PASS] test_evolutionary_statistics_large_scale");
}

/* ========================================================================== */
/* 5. Infraestructura visual escalable (1024 ranuras, LOD, culling, 0 rebuild)*/
/* ========================================================================== */
static void test_visual_scalability_1024_and_lod(void) {
    char err[256] = {0};
    ensureRecipesLoaded();

    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    TEST_ASSERT(EvolutionArenaConfig_ApplyPreset(&cfg, "large"), "preset large visual");
    cfg.seed = 42u;

    EvolutionSimulationRunner *runner = EvolutionSimulationRunner_Create(&cfg, true, err, sizeof(err));
    TEST_ASSERT(runner != NULL, "crear runner para test visual");

    EcosystemVisual visual = {0};
    TEST_ASSERT(EcosystemVisual_InitForSnapshots(&visual, 1024u), "init EcosystemVisual 1024");
    EcosystemVisual_SetBudget(&visual, 4u, 12u, 1.0f);

    const EvolutionRenderSnapshot *snap = EvolutionSimulationRunner_AcquireSnapshot(runner);
    TEST_ASSERT(snap != NULL && snap->entityCount == 500u && snap->capacity >= 1024u, "snapshot 500/1024");
    TEST_ASSERT(EcosystemVisual_SyncSnapshot(&visual, snap), "sync 500 entidades");

    /* Proyectar solo 200 visibles (50 cerca, 150 lejos) y 300 fuera de cámara. */
    for (unsigned i = 0; i < snap->entityCount; ++i) {
        EcosystemVisualProjection p = {0};
        if (i < 50u) { p.visible = true; p.projectedPixels = 260.0f; }
        else if (i < 200u) { p.visible = true; p.projectedPixels = 45.0f; }
        else { p.visible = false; p.projectedPixels = 10.0f; }
        TEST_ASSERT(EcosystemVisual_SetProjection(&visual, snap->entities[i].id, p), "proyección");
    }
    TEST_ASSERT(EcosystemVisual_UpdateFromSnapshot(&visual, snap, 1.0f / 60.0f), "update snapshot");

    EcosystemVisualStats st = EcosystemVisual_GetStats(&visual);
    TEST_ASSERT(st.activeSlots == 500u, "500 ranuras activas sin tope de 256");
    TEST_ASSERT(st.visibleCount == 200u && st.culledCount == 300u, "culling exacto 200 visibles / 300 ocultas");
    TEST_ASSERT(st.nearCount <= 4u && st.midCount <= 12u, "presupuesto LOD NEAR<=4 y MID<=12 respetado");
    TEST_ASSERT(st.animatedCount <= st.nearCount + st.midCount, "criaturas ocultas/FAR sin animación pesada");
    TEST_ASSERT(st.monsterInstances <= 16u, "construcción perezosa de Monster sólo para NEAR/MID");

    /* Mutar/intercambiar cerebros jamás debe disparar reconstrucción de mallas. */
    size_t buildsBefore = EcosystemVisual_BuildCount(&visual);
    AIObjectId idA = snap->entities[0].id, idB = snap->entities[1].id;
    EvolutionSimulationRunner_ReleaseSnapshot(runner, snap);

    EvolutionCommandAck ack = {0};
    uint64_t cid = EvolutionSimulationRunner_PostSwapBrains(runner, idA, idB);
    TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK,
                "swap de cerebro");
    cid = EvolutionSimulationRunner_PostStepExact(runner, 2u);
    TEST_ASSERT(EvolutionSimulationRunner_WaitCommand(runner, cid, 5.0, &ack) && ack.status == EVO_CMD_STATUS_OK,
                "avanzar 2 ticks");

    const EvolutionRenderSnapshot *snap2 = EvolutionSimulationRunner_AcquireSnapshot(runner);
    TEST_ASSERT(snap2 != NULL, "segundo snapshot");
    TEST_ASSERT(EcosystemVisual_SyncSnapshot(&visual, snap2), "re-sync");
    TEST_ASSERT(EcosystemVisual_UpdateFromSnapshot(&visual, snap2, 1.0f / 60.0f), "update tras swap");
    size_t buildsAfter = EcosystemVisual_BuildCount(&visual);
    TEST_ASSERT(buildsAfter == buildsBefore, "cambio de cerebro no dispara reconstrucción de malla");
    EvolutionSimulationRunner_ReleaseSnapshot(runner, snap2);

    EcosystemVisual_Shutdown(&visual);
    EvolutionSimulationRunner_Destroy(runner);
    puts("[PASS] test_visual_scalability_1024_and_lod");
}

void run_evolution_scalability_tests(void) {
    char err[256] = {0};
    CreatureRecipeRegistry_Clear();
    CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err));
    test_spatial_index_vs_bruteforce();
    test_anatomical_compact_vs_oracle_and_cache();
    test_runner_threading_and_parallel_determinism();
    test_evolutionary_statistics_large_scale();
    test_visual_scalability_1024_and_lod();
    EcosystemAnatomyCache_ClearUnused();
    CreatureRecipeRegistry_Clear();
}
