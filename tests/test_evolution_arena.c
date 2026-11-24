#include "EvolutionArena.h"
#include "CreatureRecipeRegistry.h"
#include "test_utils.h"
#define NEAR_EPS(a, b, eps) (fabsf((a) - (b)) < (eps))
#include <math.h>
#include <stdio.h>
#include <string.h>

static void stepTicks(EcosystemWorld *w, unsigned n) {
    for (unsigned i = 0; i < n; ++i) EcosystemWorld_Step(w);
}

static void test_brain_copy_swap_and_cross_species_independence(void) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.seed = 101u;
    cfg.brumacernos = 2u;
    cfg.velkares = 2u;
    cfg.plants = 4u;
    cfg.obstacles = 2u;
    cfg.founderMode = EVOLUTION_FOUNDERS_RANDOM;

    EcosystemWorld w;
    char err[256] = {0};
    TEST_ASSERT(EvolutionArena_Create(&w, &cfg, err, sizeof(err)), "crear arena para intercambio");

    AIObjectId brumId = 0, velkId = 0;
    for (unsigned i = 0; i < w.capacity; ++i) {
        if (!w.entities[i].id) continue;
        if (w.entities[i].speciesIndex == 0 && !brumId) brumId = w.entities[i].id;
        if (w.entities[i].speciesIndex == 1 && !velkId) velkId = w.entities[i].id;
    }
    TEST_ASSERT(brumId && velkId, "Brumacerno y Velkar presentes");

    EcosystemEntity *brum = EcosystemWorld_Get(&w, brumId);
    EcosystemEntity *velk = EcosystemWorld_Get(&w, velkId);
    uint64_t brumFp0 = CreatureMind_GetNeuralGenome(&brum->mind)->metadata.fingerprint;
    uint64_t velkFp0 = CreatureMind_GetNeuralGenome(&velk->mind)->metadata.fingerprint;
    TEST_ASSERT(brumFp0 != velkFp0, "cerebros iniciales distintos");

    /* Guardar invariantes físicos antes del intercambio. */
    unsigned brumSpecies = brum->speciesIndex, velkSpecies = velk->speciesIndex;
    float brumEnergy = brum->physiology.energy, velkEnergy = velk->physiology.energy;
    double brumAge = brum->physiology.age, velkAge = velk->physiology.age;
    unsigned brumRole = brum->reproduction.role, velkRole = velk->reproduction.role;
    bool brumAttack = brum->body.canAttack, velkAttack = velk->body.canAttack;
    unsigned brumDiet = w.species[brum->speciesIndex].physiology.dietMask;
    unsigned velkDiet = w.species[velk->speciesIndex].physiology.dietMask;
    TEST_ASSERT(!brumAttack && velkAttack, "capacidades físicas distintas por especie");
    TEST_ASSERT(brumDiet == ECO_PLANT && velkDiet == ECO_MEAT, "dietas físicas distintas");

    /* Avanzar unos ticks para llenar activaciones recurrentes. */
    stepTicks(&w, 30);
    brum = EcosystemWorld_Get(&w, brumId);
    velk = EcosystemWorld_Get(&w, velkId);
    brumEnergy = brum->physiology.energy; velkEnergy = velk->physiology.energy;
    brumAge = brum->physiology.age; velkAge = velk->physiology.age;

    /* Intercambio entre Brumacerno y Velkar. */
    TEST_ASSERT(EcosystemWorld_SwapBrains(&w, brumId, velkId), "intercambio atómico entre especies");
    TEST_ASSERT(CreatureMind_GetNeuralGenome(&brum->mind)->metadata.fingerprint == velkFp0, "Brumacerno recibió cerebro de Velkar");
    TEST_ASSERT(CreatureMind_GetNeuralGenome(&velk->mind)->metadata.fingerprint == brumFp0, "Velkar recibió cerebro de Brumacerno");
    TEST_ASSERT(brum->mind.neuralBrain.runtime.stepCount == 0u && velk->mind.neuralBrain.runtime.stepCount == 0u,
                "estado recurrente reiniciado al intercambiar");
    TEST_ASSERT(brum->speciesIndex == brumSpecies && velk->speciesIndex == velkSpecies, "especies intactas");
    TEST_ASSERT(NEAR_EPS(brum->physiology.energy, brumEnergy, 1e-6f) &&
                NEAR_EPS(velk->physiology.energy, velkEnergy, 1e-6f), "energía intacta");
    TEST_ASSERT(fabs(brum->physiology.age - brumAge) < 1e-6 && fabs(velk->physiology.age - velkAge) < 1e-6, "edad intacta");
    TEST_ASSERT(brum->reproduction.role == brumRole && velk->reproduction.role == velkRole, "rol reproductivo intacto");
    TEST_ASSERT(brum->body.canAttack == brumAttack && velk->body.canAttack == velkAttack, "ataque físico intacto");
    TEST_ASSERT(w.interventionCount == 1u, "intervención registrada en bitácora");

    /* Copia independiente: avanzar uno no modifica runtime del otro. */
    TEST_ASSERT(EcosystemWorld_CloneBrain(&w, brumId, velkId), "clonación de cerebro");
    TEST_ASSERT(CreatureMind_GetNeuralGenome(&velk->mind)->metadata.fingerprint == velkFp0, "copia tiene misma huella");
    brum->mind.neuralBrain.runtime.hidden[0] = 0.77f;
    TEST_ASSERT(NEAR_EPS(velk->mind.neuralBrain.runtime.hidden[0], 0.0f, 1e-6f), "sin estado runtime compartido");

    /* Interacción física imposible: Brumacerno intenta atacar a Velkar -> rechazado, sin daño. */
    brum->mind.action = ACTION_ATTACK;
    brum->mind.target = velkId;
    brum->body.position = Vec3_Create(-6.0f, 0, -6.0f);
    velk->body.position = Vec3_Create(-6.0f, 0, -5.2f);
    brum->body.yaw = 0.0f;
    float velkHpBefore = velk->physiology.health;
    TEST_ASSERT(!EcosystemWorld_Attack(&w, brumId, velkId), "Brumacerno no puede atacar físicamente");
    TEST_ASSERT(NEAR_EPS(velk->physiology.health, velkHpBefore, 1e-6f), "ataque fallido no causa daño");
    TEST_ASSERT(brum->mind.neuralState.lastOutcome == NEURAL_OUTCOME_FAILED_ATTACK, "resultado de fallo registrado");

    /* Interacción dietética incompatible: Brumacerno intenta comer carne -> 0 beneficio. */
    AIObjectId meatId = EcosystemWorld_AddResource(&w, (EcosystemResource){
        .category = ECO_MEAT, .biomass = 2.0f, .maximumBiomass = 2.0f,
        .nutritionalValue = 0.4f, .remainingLifetime = 100.0, .accessible = true
    }, Vec3_Create(-6.0f, 0, -5.5f), 0.6f);
    TEST_ASSERT(meatId != 0, "añadir carne de prueba");
    brum->mind.action = ACTION_EAT;
    brum->mind.target = meatId;
    brum->mind.intakeNeed = NEED_HUNGER;
    brum->mind.intakeRequested = 0.1f;
    brum->mind.contactTime = 0.3f;
    brum->mind.interactionPhase = INTERACTION_CONSUME;
    brum->body.speed = 0.0f;
    brum->body.mouth = Vec3_Create(-6.0f, 0.04f, -5.5f);
    brum->body.mouthDirection = Vec3_Create(0, -1, 0);
    brum->mind.interaction.approach = Vec3_Create(0, -1, 0);
    brum->mind.interaction.orientationTolerance = 1.4f;
    brum->body.reach = (ReachResult){.objectId = meatId, .status = REACH_REACHED};
    float brumEngBefore = brum->physiology.energy;
    TEST_ASSERT(EcosystemWorld_Consume(&w, brumId, meatId, 0.1f) == 0.0f, "dieta incompatible rechazada");
    TEST_ASSERT(NEAR_EPS(brum->physiology.energy, brumEngBefore, 1e-6f), "sin beneficio nutricional artificial");
    TEST_ASSERT(brum->mind.neuralState.lastOutcome == NEURAL_OUTCOME_FAILED_DIET, "fallo dietético reportado");

    EcosystemWorld_Free(&w);
}

static void test_gestation_conception_snapshot_parent_death_and_swap(void) {
    EcosystemWorld w;
    TEST_ASSERT(EcosystemWorld_InitArena(&w, 16, 32, 1, 20.0f, 55u, 1.0 / 60.0, 120.0), "init arena gestación");
    w.defaultController = CREATURE_CONTROLLER_NEURAL;
    w.neuralMutation = NeuralMutationConfig_Default();
    w.neuralMutation.mutationRate = 0.0f;
    w.neuralMutation.replaceRate = 0.0f;
    w.neuralMutation.crossoverMode = NEURAL_CROSSOVER_MUTATION_ONLY;

    EcosystemSpecies sp = EcosystemSpecies_Default(1, 1, ECO_PLANT, 0, 0);
    sp.reproduction.gestation = 1.0; /* gestación corta para prueba unitaria */
    sp.reproduction.cooldown = 10.0;
    TEST_ASSERT(EcosystemWorld_AddSpecies(&w, sp) == 1u, "añadir especie");

    AIObjectId idA = EcosystemWorld_Spawn(&w, 1, Vec3_Create(-0.32f, 0, 0), 300.0, 0.95f, NULL);
    AIObjectId idB = EcosystemWorld_Spawn(&w, 1, Vec3_Create(0.32f, 0, 0), 300.0, 0.95f, NULL);
    TEST_ASSERT(idA && idB, "padres creados");

    EcosystemEntity *eA = EcosystemWorld_Get(&w, idA);
    EcosystemEntity *eB = EcosystemWorld_Get(&w, idB);
    eA->reproduction.role = 0; /* madre */
    eB->reproduction.role = 1; /* padre */

    /* Asignar cerebros conocidos a madre y padre. */
    uint32_t sM = 301u, sF = 302u, sSwap = 303u;
    NeuralGenome gMother, gFather, gSwapped;
    TEST_ASSERT(NeuralGenome_InitRandom(&gMother, &sM, 0.8f, 5.0f, 10u), "genoma madre");
    TEST_ASSERT(NeuralGenome_InitRandom(&gFather, &sF, 0.8f, 5.0f, 11u), "genoma padre");
    TEST_ASSERT(NeuralGenome_InitRandom(&gSwapped, &sSwap, 0.8f, 5.0f, 12u), "genoma reemplazo");
    uint64_t fpMotherAtConception = gMother.metadata.fingerprint;
    uint64_t fpSwapped = gSwapped.metadata.fingerprint;

    TEST_ASSERT(EcosystemWorld_AssignBrain(&w, idA, &gMother, NULL), "asignar madre");
    TEST_ASSERT(EcosystemWorld_AssignBrain(&w, idB, &gFather, NULL), "asignar padre");

    /* Orientar ambos progenitores frente a frente para que se perciban mutuamente y conciban. */
    eA->body.yaw = 1.5707963f; eA->mind.perception.nextRefresh = 0.0;
    eB->body.yaw = -1.5707963f; eB->mind.perception.nextRefresh = 0.0;
    eA->mind.nextDecision = 1e9; eB->mind.nextDecision = 1e9;
    stepTicks(&w, 1);
    eA = EcosystemWorld_Get(&w, idA); eB = EcosystemWorld_Get(&w, idB);
    eA->mind.action = ACTION_APPROACH_MATE; eA->mind.target = idB; eA->mind.started = w.clock.seconds;
    eB->mind.action = ACTION_APPROACH_MATE; eB->mind.target = idA; eB->mind.started = w.clock.seconds;
    stepTicks(&w, 1);
    eA = EcosystemWorld_Get(&w, idA);
    TEST_ASSERT(eA && eA->reproduction.gestating && eA->reproduction.hasGestationNeuralGenomes,
                "concepción captura genomas neuronales");

    /* 1) El padre muere durante la gestación. */
    TEST_ASSERT(EcosystemWorld_RequestDeath(&w, idB, DEATH_REMOVED), "muerte del padre");
    stepTicks(&w, 1);
    TEST_ASSERT(EcosystemWorld_Get(&w, idB) == NULL, "padre retirado");

    /* 2) Se cambia el cerebro de la madre durante la gestación. */
    TEST_ASSERT(EcosystemWorld_AssignBrain(&w, idA, &gSwapped, "swap_during_gestation"),
                "reemplazo de cerebro materno durante gestación");
    TEST_ASSERT(CreatureMind_GetNeuralGenome(&eA->mind)->metadata.fingerprint == fpSwapped,
                "madre ahora tiene cerebro nuevo");

    /* Avanzar hasta el nacimiento (> 1.0 s). */
    stepTicks(&w, 75);
    TEST_ASSERT(w.statistics.births == 1u, "nacimiento completado tras muerte paterna y swap materno");

    /* Verificar que la cría heredó el genoma materno DE LA CONCEPCIÓN (no el reemplazado),
     * tiene estado recurrente limpio y memoria perceptiva vacía. */
    EcosystemEntity *child = NULL;
    for (unsigned i = 0; i < w.capacity; ++i) {
        if (w.entities[i].id && w.entities[i].id != idA) { child = &w.entities[i]; break; }
    }
    TEST_ASSERT(child != NULL, "cría localizada");
    TEST_ASSERT(child->generation == 1u, "generación de la cría");
    TEST_ASSERT(child->reproduction.parents[0] == idA && child->reproduction.parents[1] == idB, "IDs de progenitores");
    const NeuralGenome *childGenome = CreatureMind_GetNeuralGenome(&child->mind);
    TEST_ASSERT(childGenome != NULL && childGenome->metadata.fingerprint == fpMotherAtConception,
                "cría heredó el genoma de concepción, no el cerebro sustituido durante gestación");
    TEST_ASSERT(child->mind.perception.count == 0u || child->mind.neuralBrain.runtime.stepCount <= 2u,
                "cría sin memoria heredada de los padres");

    /* Reutilización de ranura no restaura el cerebro del individuo anterior. */
    AIObjectId deadId = child->id;
    TEST_ASSERT(EcosystemWorld_RequestDeath(&w, deadId, DEATH_REMOVED), "retirar cría");
    stepTicks(&w, 1);
    TEST_ASSERT(EcosystemWorld_Get(&w, deadId) == NULL, "cría destruida");
    AIObjectId newcomerId = EcosystemWorld_Spawn(&w, 1, Vec3_Create(5, 0, 5), 200.0, 0.8f, NULL);
    TEST_ASSERT(newcomerId != 0 && newcomerId != deadId, "nuevo individuo con ID nuevo");
    EcosystemEntity *newcomer = EcosystemWorld_Get(&w, newcomerId);
    TEST_ASSERT(newcomer->mind.controllerType == CREATURE_CONTROLLER_UTILITY &&
                CreatureMind_GetNeuralGenome(&newcomer->mind) == NULL,
                "ranura reutilizada no hereda cerebro antiguo");

    NeuralGenome_Destroy(&gMother);
    NeuralGenome_Destroy(&gFather);
    NeuralGenome_Destroy(&gSwapped);
    EcosystemWorld_Free(&w);
}

static void test_determinism_pause_acceleration_and_archive_export(void) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.seed = 777u;
    cfg.brumacernos = 6u;
    cfg.velkares = 2u;
    cfg.plants = 12u;
    cfg.obstacles = 3u;
    cfg.capacity = 32u;
    cfg.founderMode = EVOLUTION_FOUNDERS_SEEDED;

    EcosystemWorld a, b;
    char err[256] = {0};
    TEST_ASSERT(EvolutionArena_Create(&a, &cfg, err, sizeof(err)), "crear arena A");
    TEST_ASSERT(EvolutionArena_Create(&b, &cfg, err, sizeof(err)), "crear arena B");

    /* 1x vs 16x deben producir exactamente la misma simulación tras igual número de ticks. */
    a.clock.speed = 1.0;
    b.clock.speed = 16.0;
    for (unsigned i = 0; i < 320; ++i) EcosystemWorld_Advance(&a, 1.0 / 60.0);
    for (unsigned i = 0; i < 20; ++i) EcosystemWorld_Advance(&b, 1.0 / 60.0);
    TEST_ASSERT(a.clock.tick == 320u && b.clock.tick == 320u, "mismo número de ticks fijos");
    TEST_ASSERT(a.statistics.eventHash == b.statistics.eventHash, "determinismo exacto 1x vs 16x");
    TEST_ASSERT(a.count == b.count && a.statistics.births == b.statistics.births &&
                a.statistics.deaths == b.statistics.deaths, "mismo estado poblacional");

    /* Pausa congela completamente tiempo, fisiología e inferencia. */
    a.clock.paused = true;
    uint64_t tickBefore = a.clock.tick;
    double secBefore = a.clock.seconds;
    EcosystemEntity *first = NULL;
    for (unsigned i = 0; i < a.capacity; ++i) if (a.entities[i].id) { first = &a.entities[i]; break; }
    TEST_ASSERT(first != NULL, "individuo vivo");
    float engBefore = first->physiology.energy;
    uint64_t stepsBefore = first->mind.neuralBrain.runtime.stepCount;
    EcosystemWorld_Advance(&a, 5.0);
    TEST_ASSERT(a.clock.tick == tickBefore && a.clock.seconds == secBefore, "pausa congela reloj");
    TEST_ASSERT(NEAR_EPS(first->physiology.energy, engBefore, 1e-7f) &&
                first->mind.neuralBrain.runtime.stepCount == stepsBefore, "pausa congela fisiología e inferencia");

    /* Exportación de archivo observacional. */
    const char *archDir = "/tmp/monster_test_archive";
    TEST_ASSERT(EcosystemWorld_ExportArchiveBrains(&a, archDir, err, sizeof(err)), "exportar archivo observacional");
    NeuralGenome checkLoad;
    TEST_ASSERT(NeuralBrain_LoadFile("/tmp/monster_test_archive/longest_survival.brain.json",
                                     &checkLoad, err, sizeof(err)), "archivo exportado es *.brain.json válido");
    NeuralGenome_Destroy(&checkLoad);

    EcosystemWorld_Free(&a);
    EcosystemWorld_Free(&b);
}

void run_evolution_arena_tests(void) {
    char err[256] = {0};
    CreatureRecipeRegistry_Clear();
    CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err));
    test_brain_copy_swap_and_cross_species_independence();
    test_gestation_conception_snapshot_parent_death_and_swap();
    test_determinism_pause_acceleration_and_archive_export();
    CreatureRecipeRegistry_Clear();
    printf("[PASS] evolution_arena: intercambio entre especies, gestación, determinismo, pausa y archivo\n");
}
