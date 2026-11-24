/**
 * @file test_neural_v2_and_molecular_world.c
 * @brief Pruebas exhaustivas de NeuralControllerV2, serialización v1/v2 e integración molecular.
 */
#include "test_utils.h"
#include "EcosystemWorld.h"
#include "MetabolismIO.h"
#include "NeuralBrain.h"
#include "NeuralBrainIO.h"
#include "NeuralControllerV2.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_v2_genome_and_serialization(void) {
    uint32_t rng = 12345u;
    NeuralGenome gA, gB, child, cloned, loaded;
    TEST_ASSERT(NeuralGenome_InitRandomV2(&gA, &rng, 0.7f, 5.0f, 10u), "InitRandomV2 A");
    TEST_ASSERT(NeuralGenome_InitRandomV2(&gB, &rng, 0.7f, 5.0f, 11u), "InitRandomV2 B");
    TEST_ASSERT(NeuralGenome_ParamCount(&gA) == NEURAL_V2_PARAM_COUNT, "1416 parámetros en v2");
    TEST_ASSERT(gA.metadata.fingerprint != 0u && gA.metadata.fingerprint != gB.metadata.fingerprint,
                "huellas distintas para genomas aleatorios v2");
    TEST_ASSERT(NeuralGenome_Distance(&gA, &gB) > 0.01f, "distancia positiva entre v2 distintos");
    TEST_ASSERT(NeuralGenome_Clone(&cloned, &gA), "clonación v2");
    TEST_ASSERT(FLOAT_NEAR(NeuralGenome_Distance(&gA, &cloned), 0.0f), "clon idéntico");

    /* Recombinación v2 (por neurona y solo mutación). */
    NeuralMutationConfig cfg = NeuralMutationConfig_Default();
    cfg.crossoverMode = NEURAL_CROSSOVER_NEURON_WISE;
    TEST_ASSERT(NeuralGenome_Inherit(&child, &gA, &gB, cfg, &rng), "herencia neuron_wise v2");
    TEST_ASSERT(child.architecture == NEURAL_ARCH_CANDIDATE_POLICY, "hijo conserva arquitectura v2");
    TEST_ASSERT(child.metadata.fingerprint != gA.metadata.fingerprint, "mutación altera huella");

    cfg.crossoverMode = NEURAL_CROSSOVER_MUTATION_ONLY;
    cfg.mutationRate = 0.0f;
    cfg.replaceRate = 0.0f;
    NeuralGenome childNoMut;
    TEST_ASSERT(NeuralGenome_Inherit(&childNoMut, &gA, &gB, cfg, &rng), "herencia mutation_only sin mutación");
    TEST_ASSERT(childNoMut.metadata.fingerprint == gA.metadata.fingerprint,
                "mutation_only con tasa 0 preserva exactamente los parámetros de parentA");

    /* Rechazar cruce entre arquitecturas incompatibles (v1 x v2). */
    NeuralGenome v1;
    TEST_ASSERT(NeuralGenome_InitRandom(&v1, &rng, 0.7f, 5.0f, 99u), "InitRandom v1");
    TEST_ASSERT(!NeuralGenome_Compatible(&v1, &gA), "v1 y v2 incompatibles");
    TEST_ASSERT(!NeuralGenome_Inherit(&child, &v1, &gA, NeuralMutationConfig_Default(), &rng),
                "rechaza cruce v1 x v2");

    /* Round-trip JSON v2 exacto + metadatos opcionales de entrenamiento. */
    snprintf(gA.metadata.trainedSpecies, sizeof(gA.metadata.trainedSpecies), "brumacerno");
    snprintf(gA.metadata.trainedEnvironment, sizeof(gA.metadata.trainedEnvironment), "brumacerno_mixed");
    gA.metadata.trainerGeneration = 42u;
    gA.metadata.validationFitness = 0.875f;
    gA.metadata.validationSurvival = 0.95f;

    char err[256] = {0};
    TEST_ASSERT(NeuralBrain_SaveFile(&gA, "/tmp/test_monster_v2.brain.json", true, err, sizeof(err)),
                "guardar v2 *.brain.json");
    TEST_ASSERT(NeuralBrain_LoadFile("/tmp/test_monster_v2.brain.json", &loaded, err, sizeof(err)),
                "cargar v2 *.brain.json");
    TEST_ASSERT(loaded.metadata.fingerprint == gA.metadata.fingerprint, "huella v2 exacta tras round-trip");
    TEST_ASSERT(FLOAT_NEAR(NeuralGenome_Distance(&gA, &loaded), 0.0f), "parámetros float32 exactos");
    TEST_ASSERT(strcmp(loaded.metadata.trainedSpecies, "brumacerno") == 0, "metadato trained_species");
    TEST_ASSERT(loaded.metadata.trainerGeneration == 42u, "metadato trainer_generation");

    /* Compatibilidad intacta con archivos legacy v1 existentes. */
    NeuralGenome legacyLoaded;
    TEST_ASSERT(NeuralBrain_LoadFile("brains/seeded_baseline.brain.json", &legacyLoaded, err, sizeof(err)),
                "cargar archivo legacy v1 existente");
    TEST_ASSERT(legacyLoaded.architecture == NEURAL_ARCH_FIXED_RNN, "legacy v1 conserva arquitectura");

    /* Rechazar inconsistencia de esquema (v2 con sensor_schema v1). */
    const char *badSchema = "{\"version\":2,\"architecture\":\"recurrent_candidate_policy\","
                            "\"sensor_schema\":\"monster_sensor_v1\",\"action_schema\":\"monster_action_v2\","
                            "\"topology\":{},\"weights\":{},\"biases\":{},\"metadata\":{}}";
    NeuralGenome badOut;
    TEST_ASSERT(!NeuralBrain_DeserializeJSON(badSchema, strlen(badSchema), &badOut, err, sizeof(err)),
                "rechaza mezcla de esquema v1 con arquitectura v2");
}

static void test_v2_permutation_equivariance_and_recurrence(void) {
    uint32_t rng = 777u;
    NeuralGenome g;
    TEST_ASSERT(NeuralGenome_InitRandomV2(&g, &rng, 0.8f, 5.0f, 1u), "genoma v2");

    float ctx[NEURAL_V2_CONTEXT_INPUTS] = {0};
    ctx[0] = 0.5f; ctx[1] = 0.7f; ctx[2] = 0.9f; ctx[5] = 0.2f;

    float resA[3][NEURAL_V2_RES_FEAT] = {{0}};
    resA[0][0] = 1.0f; resA[0][1] = 0.2f; resA[0][8] = 0.65f; resA[0][9] = 0.8f;
    resA[1][0] = 1.0f; resA[1][1] = 0.5f; resA[1][8] = 0.55f; resA[1][10] = 0.9f; resA[1][11] = 0.7f;
    resA[2][0] = 0.8f; resA[2][1] = 0.3f; resA[2][8] = 0.99f; resA[2][6] = 1.0f;

    /* Permutación: [2, 0, 1] */
    float resB[3][NEURAL_V2_RES_FEAT];
    memcpy(resB[0], resA[2], sizeof(resA[0]));
    memcpy(resB[1], resA[0], sizeof(resA[0]));
    memcpy(resB[2], resA[1], sizeof(resA[0]));

    NeuralBrainRuntime rtA, rtB;
    NeuralBrain_ResetRuntime(&rtA);
    NeuralBrain_ResetRuntime(&rtB);

    float glbA[NEURAL_V2_GLOBAL_ACTIONS], glbB[NEURAL_V2_GLOBAL_ACTIONS];
    float scA[NEURAL_V2_MAX_RESOURCE_CANDIDATES], scB[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    float crA[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS];
    float crB[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS];

    TEST_ASSERT(NeuralBrain_EvaluateV2(&g, &rtA, ctx, (const float (*)[NEURAL_V2_RES_FEAT])resA, 3u, NULL, 0u, glbA, scA, crA), "eval A");
    TEST_ASSERT(NeuralBrain_EvaluateV2(&g, &rtB, ctx, (const float (*)[NEURAL_V2_RES_FEAT])resB, 3u, NULL, 0u, glbB, scB, crB), "eval B");

    /* Equivariancia exacta: scB[0]==scA[2], scB[1]==scA[0], scB[2]==scA[1]. */
    TEST_ASSERT(fabsf(scB[0] - scA[2]) < 1e-6f, "equivariancia permutación candidato 2->0");
    TEST_ASSERT(fabsf(scB[1] - scA[0]) < 1e-6f, "equivariancia permutación candidato 0->1");
    TEST_ASSERT(fabsf(scB[2] - scA[1]) < 1e-6f, "equivariancia permutación candidato 1->2");

    /* Memoria recurrente: un segundo paso con misma entrada cambia contexto respecto al primero. */
    float prevCtx0 = rtA.v2.context[0];
    TEST_ASSERT(NeuralBrain_EvaluateV2(&g, &rtA, ctx, (const float (*)[NEURAL_V2_RES_FEAT])resA, 3u, NULL, 0u, glbA, scA, crA), "eval A paso 2");
    bool changed = false;
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        if (fabsf(rtA.v2.context[h] - rtA.v2.prevContext[h]) > 1e-5f) changed = true;
    }
    (void)prevCtx0;
    TEST_ASSERT(changed, "estado recurrente evoluciona entre pasos");
}

static void test_molecular_world_ingestion_attack_carcass_and_determinism(void) {
    MetabolismDatabase db;
    char err[256] = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)), "cargar db");

    EcosystemWorld w;
    TEST_ASSERT(EcosystemWorld_InitArena(&w, 32u, 64u, 4u, 20.0f, 42u, 1.0 / 60.0, 120.0), "init arena");
    TEST_ASSERT(EcosystemWorld_EnableMolecularMetabolism(&w, &db), "activar modo molecular");
    w.defaultController = CREATURE_CONTROLLER_NEURAL;

    EcosystemSpecies brum = EcosystemSpecies_Default(1u, 1u, 0u, 0u, 0u);
    snprintf(brum.name, sizeof(brum.name), "Brumacerno");
    brum.metabolicProfile = MetabolicProfileRegistry_Find(&db.profiles, "brumacerno");
    brum.tissueSubstance = SubstanceRegistry_Find(&db.substances, "brumacerno_tissue");
    brum.tissueMass = 5.0f;
    brum.physiology.canAttackPhysically = false;
    brum.anatomicalOral = false;

    EcosystemSpecies velk = EcosystemSpecies_Default(2u, 2u, 0u, 0u, 0u);
    snprintf(velk.name, sizeof(velk.name), "Velkar");
    velk.metabolicProfile = MetabolicProfileRegistry_Find(&db.profiles, "velkar");
    velk.tissueSubstance = SubstanceRegistry_Find(&db.substances, "velkar_tissue");
    velk.tissueMass = 3.0f;
    velk.physiology.canAttackPhysically = true;
    velk.physiology.dietMask = 0u; /* Intencionalmente 0: el ataque NO depende de dietMask! */
    velk.physiology.preyMask = 0u; /* Intencionalmente 0: el ataque NO depende de preyMask! */
    velk.damage = 0.6f;
    velk.oralReach = 2.0f;
    velk.attackRange = 1.8f;
    velk.anatomicalOral = false;

    TEST_ASSERT(EcosystemWorld_AddSpecies(&w, brum) == 1u, "añadir brum");
    TEST_ASSERT(EcosystemWorld_AddSpecies(&w, velk) == 2u, "añadir velk");

    AIObjectId idB = EcosystemWorld_Spawn(&w, 1u, Vec3_Create(-2.0f, 0.0f, 0.0f), 200.0, 0.7f, NULL);
    AIObjectId idV = EcosystemWorld_Spawn(&w, 2u, Vec3_Create(-2.0f, 0.0f, -1.0f), 200.0, 0.7f, NULL);
    TEST_ASSERT(idB != 0u && idV != 0u, "spawn brum y velk");

    /* Rechazar cerebro v1 en mundo molecular (Sección 19). */
    uint32_t rng = 99u;
    NeuralGenome gV1, gV2a, gV2b;
    TEST_ASSERT(NeuralGenome_InitRandom(&gV1, &rng, 0.7f, 5.0f, 1u), "init v1");
    TEST_ASSERT(!EcosystemWorld_AssignBrain(&w, idB, &gV1, "reject_v1"),
                "rechaza cerebro v1 en mundo molecular");
    TEST_ASSERT(NeuralGenome_InitRandomV2(&gV2a, &rng, 0.7f, 5.0f, 2u), "init v2 a");
    TEST_ASSERT(NeuralGenome_InitRandomV2(&gV2b, &rng, 0.7f, 5.0f, 3u), "init v2 b");
    TEST_ASSERT(EcosystemWorld_AssignBrain(&w, idB, &gV2a, "assign_v2"), "asigna v2 a brum");
    TEST_ASSERT(EcosystemWorld_AssignBrain(&w, idV, &gV2b, "assign_v2"), "asigna v2 a velk");

    /* Intercambio de cerebros entre especies conserva perfil, estado digestivo y fisiología. */
    EcosystemEntity *eB = EcosystemWorld_Get(&w, idB);
    EcosystemEntity *eV = EcosystemWorld_Get(&w, idV);
    eB->metabolism.toxicity = 0.12f;
    eV->metabolism.toxicity = 0.03f;
    TEST_ASSERT(EcosystemWorld_SwapBrains(&w, idB, idV), "intercambio v2 entre especies");
    TEST_ASSERT(FLOAT_NEAR(eB->metabolism.toxicity, 0.12f) && FLOAT_NEAR(eV->metabolism.toxicity, 0.03f),
                "swap conserva estado digestivo y toxicidad del cuerpo");
    TEST_ASSERT(eB->mind.neuralBrain.genome.metadata.fingerprint == gV2b.metadata.fingerprint,
                "swap intercambió los genomas correctamente");

    /* Experimento 5: ataque desacoplado de dieta -> muerte -> cadáver molecular -> sin energía gratuita. */
    eV->body.yaw = 0.0f; /* Mirando hacia +Z (hacia eB en z=0 desde z=-1) */
    eV->body.position = Vec3_Create(-2.0f, 0.0f, -1.0f);
    eV->body.mouth = Vec3_Create(-2.0f, 0.45f, -0.5f);
    eV->mind.action = ACTION_ATTACK;
    eV->mind.target = idB;
    float velkEnergyBefore = eV->physiology.energy;
    TEST_ASSERT(EcosystemWorld_Attack(&w, idV, idB), "primer golpe físico sin dietMask");
    TEST_ASSERT(eV->physiology.energy < velkEnergyBefore, "ataque cuesta energía y NO da recompensa energética");
    w.clock.seconds += 2.0;
    TEST_ASSERT(EcosystemWorld_Attack(&w, idV, idB), "segundo golpe letal");
    TEST_ASSERT(eB->physiology.cause == DEATH_PREDATION, "presa muere por depredación");
    EcosystemWorld_Step(&w); /* commit() retira el cadáver y crea el recurso molecular */

    /* Buscar el recurso cadáver molecular generado y verificar su composición y descomposición. */
    EcosystemResource *carcass = NULL;
    for (unsigned k = 0; k < w.activeResourceCount; ++k) {
        EcosystemResource *r = &w.resources[w.activeResourceSlots[k]];
        if (r->molecular && r->sourceCreatureId == idB) { carcass = r; break; }
    }
    TEST_ASSERT(carcass != NULL, "muerte generó cadáver molecular");
    TEST_ASSERT(carcass->killerCreatureId == idV, "cadáver registra killerCreatureId diagnóstico");
    TEST_ASSERT(carcass->decayPolicy == RESOURCE_DECAY_DECOMPOSE, "cadáver usa política de descomposición");
    float decayBefore = 0.0f;
    MoleculeHandle hDecay = MoleculeRegistry_Find(&db.molecules, "decay_products");
    for (uint16_t i = 0; i < carcass->componentCount; ++i)
        if (carcass->composition[i].molecule == hDecay) decayBefore = carcass->composition[i].fraction;
    for (int t = 0; t < 60 * 15; ++t) EcosystemResource_UpdateMolecular(carcass, &db.molecules, 1.0f / 60.0f);
    float decayAfter = 0.0f;
    double fracSum = 0.0;
    for (uint16_t i = 0; i < carcass->componentCount; ++i) {
        fracSum += carcass->composition[i].fraction;
        if (carcass->composition[i].molecule == hDecay) decayAfter = carcass->composition[i].fraction;
    }
    TEST_ASSERT(decayAfter > decayBefore + 0.01f, "descomposición genera decay_products progresivamente");
    TEST_ASSERT(fabs(fracSum - 1.0) < 1e-3, "descomposición conserva suma de fracciones == 1.0");

    /* Determinismo multi-hilo (1 vs 4 trabajadores en modo molecular con cerebros v2). */
    EcosystemWorld w1, w4;
    EcosystemWorld_InitArena(&w1, 64u, 128u, 2u, 24.0f, 321u, 1.0 / 60.0, 120.0);
    EcosystemWorld_InitArena(&w4, 64u, 128u, 2u, 24.0f, 321u, 1.0 / 60.0, 120.0);
    EcosystemWorld_EnableMolecularMetabolism(&w1, &db);
    EcosystemWorld_EnableMolecularMetabolism(&w4, &db);
    EcosystemWorld_SetWorkerCount(&w1, 1u);
    EcosystemWorld_SetWorkerCount(&w4, 4u);
    w1.defaultController = w4.defaultController = CREATURE_CONTROLLER_NEURAL;
    EcosystemWorld_AddSpecies(&w1, brum); EcosystemWorld_AddSpecies(&w1, velk);
    EcosystemWorld_AddSpecies(&w4, brum); EcosystemWorld_AddSpecies(&w4, velk);

    const SubstanceDef *grassSub = SubstanceRegistry_Get(&db.substances, SubstanceRegistry_Find(&db.substances, "grass"));
    const SubstanceDef *waterSub = SubstanceRegistry_Get(&db.substances, SubstanceRegistry_Find(&db.substances, "clean_water"));
    for (int i = 0; i < 6; ++i) {
        EcosystemResource gRes = EcosystemResource_FromSubstance(grassSub, 4.0f, 4.0f, RESOURCE_REGEN_LINEAR, 0.01f, RESOURCE_DECAY_NONE, 0.0, 0.0f, MOLECULE_INVALID_HANDLE);
        Vector3 p = Vec3_Create(-8.0f + i * 3.0f, 0.0f, 4.0f);
        EcosystemWorld_AddMolecularResource(&w1, gRes, p, 0.8f);
        EcosystemWorld_AddMolecularResource(&w4, gRes, p, 0.8f);
    }
    EcosystemResource wtRes = EcosystemResource_FromSubstance(waterSub, 0.0f, 0.0f, RESOURCE_REGEN_INFINITE, 0.0f, RESOURCE_DECAY_NONE, 0.0, 0.0f, MOLECULE_INVALID_HANDLE);
    EcosystemWorld_AddMolecularResource(&w1, wtRes, Vec3_Create(0.0f, 0.0f, -6.0f), 1.5f);
    EcosystemWorld_AddMolecularResource(&w4, wtRes, Vec3_Create(0.0f, 0.0f, -6.0f), 1.5f);

    uint32_t r1 = 555u, r4 = 555u;
    for (int i = 0; i < 20; ++i) {
        Vector3 p = Vec3_Create(-12.0f + (i % 5) * 5.0f, 0.0f, -2.0f + (i / 5) * 3.5f);
        unsigned sp = (i % 3 == 0) ? 2u : 1u;
        AIObjectId a1 = EcosystemWorld_Spawn(&w1, sp, p, 200.0, 0.7f, NULL);
        AIObjectId a4 = EcosystemWorld_Spawn(&w4, sp, p, 200.0, 0.7f, NULL);
        NeuralGenome ga, gb;
        NeuralGenome_InitRandomV2(&ga, &r1, 0.75f, 5.0f, (uint64_t)(i + 1));
        NeuralGenome_InitRandomV2(&gb, &r4, 0.75f, 5.0f, (uint64_t)(i + 1));
        EcosystemWorld_AssignBrain(&w1, a1, &ga, NULL);
        EcosystemWorld_AssignBrain(&w4, a4, &gb, NULL);
    }
    for (int t = 0; t < 180; ++t) {
        EcosystemWorld_Step(&w1);
        EcosystemWorld_Step(&w4);
    }
    TEST_ASSERT(w1.statistics.eventHash == w4.statistics.eventHash,
                "determinismo bit-a-bit 1 hilo vs 4 hilos en modo molecular v2");
    TEST_ASSERT(fabs(w1.statistics.totalIngestedMass - w4.statistics.totalIngestedMass) < 1e-9,
                "masa ingerida idéntica entre 1 y 4 hilos");

    EcosystemWorld_Free(&w1);
    EcosystemWorld_Free(&w4);
    EcosystemWorld_Free(&w);
}

void run_neural_v2_and_molecular_world_tests(void) {
    test_v2_genome_and_serialization();
    test_v2_permutation_equivariance_and_recurrence();
    test_molecular_world_ingestion_attack_carcass_and_determinism();
    printf("[PASS] neural_v2_and_molecular_world: v2 genoma/IO, equivariancia, ataque/cadáver molecular y determinismo paralelo\n");
}
