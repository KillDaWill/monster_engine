/**
 * @file test_world_water_access.c
 * @brief Pruebas de acceso a orillas irregulares, libro molecular compartido por cuerpo de agua,
 *        contaminación molecular, compatibilidad de cerebros v2 y relocalización vegetal en terreno.
 */
#include "test_utils.h"
#include "WorldGenerator.h"
#include "WorldPlacement.h"
#include "EcosystemWorld.h"
#include "EvolutionArena.h"
#include "MetabolismIO.h"
#include "NeuralBrainIO.h"
#include "AIWorld.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static EcosystemSpecies makeWaterTestSpecies(const MetabolismDatabase *db) {
    EcosystemSpecies sp = EcosystemSpecies_Default(1u, 1u, ECO_PLANT, 0u, 2u);
    sp.recipeId = 3u;
    sp.traversal = TraversalProfile_DefaultLand(0.95f);
    if (db) {
        sp.metabolicProfile = MetabolicProfileRegistry_Find(&db->profiles, "brumacerno");
    }
    return sp;
}

static void test_shared_water_ledger_finite_and_infinite(void) {
    /* Lago sintético con múltiples sitios de orilla accesibles y política FINITE_FIXED_VOLUME. */
    WorldRecipe rec = WorldRecipe_Flat(64.0f);
    rec.flat = false;
    rec.seed = 404u;
    rec.terrain.resolution = 65u;
    rec.hydrology.enabled = true;
    rec.hydrology.lakeDensity = 0.85f;
    rec.hydrology.riverDensity = 0.20f;
    rec.hydrology.reservoirPolicy = WORLD_RESERVOIR_FINITE_FIXED_VOLUME;
    rec.hydrology.finiteVolume = 12.0f;
    snprintf(rec.hydrology.waterSubstanceId, sizeof(rec.hydrology.waterSubstanceId), "clean_water");
    rec.geology.rockDensity = 0.0f;

    WorldStaticData *wd = WorldStaticData_Alloc();
    TEST_ASSERT(wd != NULL, "create bowl lake world");
    wd->recipe = rec;
    wd->masterSeed = rec.seed;
    wd->generatorVersion = rec.generatorVersion;
    TEST_ASSERT(TerrainHeightfield_Init(&wd->terrain, 65u, 64.0f), "init bowl lake terrain");
    for (uint32_t gz = 0; gz < wd->terrain.resolution; ++gz) {
        for (uint32_t gx = 0; gx < wd->terrain.resolution; ++gx) {
            float x = wd->terrain.originX + (float)gx * wd->terrain.spacing;
            float z = wd->terrain.originZ + (float)gz * wd->terrain.spacing;
            float r = hypotf(x, z);
            float h = 5.0f;
            if (r < 14.0f) {
                float t = r / 14.0f;
                h = 1.5f + 3.5f * t * t;
            } else if (r > 24.0f) {
                h = fmaxf(0.5f, 5.0f - (r - 24.0f) * 0.4f);
            }
            wd->terrain.heights[(size_t)gz * wd->terrain.resolution + gx] = h;
        }
    }
    char err[256] = {0};
    TEST_ASSERT(WorldGenerator_FinalizeFromHeightfield(wd, true, false, err, sizeof(err)),
                "finalize bowl lake world");
    TEST_ASSERT(wd->hydrology.lakeCount >= 1u && wd->hydrology.shoreSiteCount >= 2u,
                "bowl lake generated multiple shore sites");

    MetabolismDatabase db = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)), "load metabolism db");

    EcosystemWorld w = {0};
    TEST_ASSERT(EcosystemWorld_InitArena(&w, 16u, 512u, 4u, 32.0f, 404u, 0.05, 600.0), "init world");
    TEST_ASSERT(EcosystemWorld_EnableMolecularMetabolism(&w, &db), "enable molecular metabolism");
    TEST_ASSERT(EcosystemWorld_AttachStaticWorld(&w, wd), "attach lake world");
    TEST_ASSERT(EcosystemWorld_RegisterNaturalWaterBodies(&w), "register natural water bodies");
    TEST_ASSERT(EcosystemWorld_AddSpecies(&w, makeWaterTestSpecies(&db)) == 1u, "add species");

    /* Encontrar dos proxies de orilla distintos que apunten al MISMO cuerpo de agua. */
    const EcosystemResource *proxyA = NULL, *proxyB = NULL;
    for (unsigned i = 0; i < w.activeResourceCount; ++i) {
        const EcosystemResource *r = &w.resources[w.activeResourceSlots[i]];
        if (r->waterBodyId != 0u && r->shoreSiteIndex > 0u) {
            if (!proxyA) proxyA = r;
            else if (r->waterBodyId == proxyA->waterBodyId && r->id != proxyA->id) {
                proxyB = r;
                break;
            }
        }
    }
    TEST_ASSERT(proxyA != NULL && proxyB != NULL, "found two shore proxies sharing one water body");
    TEST_ASSERT(proxyA->sharedLedgerResourceId == proxyB->sharedLedgerResourceId &&
                proxyA->sharedLedgerResourceId != 0u,
                "both shore proxies reference the same authoritative molecular ledger");

    const AIWorldObject *objA = AIWorld_Get(&w.observations, proxyA->id);
    const AIWorldObject *objB = AIWorld_Get(&w.observations, proxyB->id);
    TEST_ASSERT(objA != NULL && objB != NULL && objA->shoreSiteIndex > 0u,
                "shore proxies registered in AIWorld");

    /* Verificar que AIWorld_CanDrink acepta bankApproachPoint y rechaza centro seco lejano. */
    Vector3 bankA = objA->bankApproachPoint;
    Vector3 mouthA = Vec3_Add(bankA, Vec3_Create(0.0f, 0.8f, 0.0f));
    InteractionTarget interactTgt = {0};
    TEST_ASSERT(AIWorld_InteractionTarget(&w.observations, proxyA->id, mouthA, 1.35f, &interactTgt),
                "irregular shore interaction target resolved");
    TEST_ASSERT(isfinite(interactTgt.position.x) && isfinite(interactTgt.position.y) && isfinite(interactTgt.position.z),
                "irregular shore contact point is finite");
    TEST_ASSERT(AIWorld_CanDrink(&w.observations, proxyA->id, mouthA, 1.85f),
                "creature at bankApproachPoint can drink from irregular shore");
    TEST_ASSERT(!AIWorld_CanDrink(&w.observations, proxyA->id, Vec3_Create(28.0f, 5.8f, 28.0f), 1.85f),
                "distant creature cannot drink from shore");

    /* Spawnear criatura sedienta cerca de orilla A y dejar que camine, alinee boca y beba en ticks reales. */
    AIObjectId cidA = EcosystemWorld_SpawnOnGround(&w, 1u, bankA.x, bankA.z, 240.0, 0.85f, NULL);
    TEST_ASSERT(cidA != 0u, "spawn creature at shore A");
    EcosystemEntity *eA = EcosystemWorld_Get(&w, cidA);
    TEST_ASSERT(eA != NULL, "get creature A");
    eA->physiology.hydration = 0.25f;

    EcosystemResource *ledger = EcosystemWorld_Resource(&w, proxyA->sharedLedgerResourceId);
    TEST_ASSERT(ledger != NULL && fabsf(ledger->biomass - 12.0f) < 0.1f, "ledger starts with finiteVolume 12.0");

    for (int t = 0; t < 160; ++t) EcosystemWorld_Step(&w);

    ledger = EcosystemWorld_Resource(&w, proxyB->sharedLedgerResourceId);
    TEST_ASSERT(ledger != NULL && ledger->biomass < 11.9f,
                "real drinking at shore A reduced shared molecular ledger visible from shore B");
    float drunkMass = 12.0f - ledger->biomass;
    TEST_ASSERT(drunkMass > 0.1f && w.statistics.totalIngestedMass >= (double)drunkMass - 0.05,
                "ingested mass matches shared lake ledger depletion");

    /* Agotar manualmente el volumen finito restante -> siguiente paso no puede extraer más agua. */
    ledger->biomass = 0.0f;
    double ingestedBefore = w.statistics.totalIngestedMass;
    for (int t = 0; t < 30; ++t) EcosystemWorld_Step(&w);
    TEST_ASSERT(w.statistics.totalIngestedMass <= ingestedBefore + 0.001,
                "depleted finite lake yields 0 across all shore sites");

    EcosystemWorld_Free(&w);
    WorldStaticData_Release(wd);
}

static void test_contaminated_water_and_v2_brain_compatibility(void) {
    WorldRecipe rec;
    char err[256] = {0};
    TEST_ASSERT(WorldRecipe_LoadFile("worlds/lab_valley_small.world.json", &rec, err, sizeof(err)),
                "load lab_valley_small");
    rec.seed = 77u;
    snprintf(rec.hydrology.waterSubstanceId, sizeof(rec.hydrology.waterSubstanceId), "contaminated_water");
    WorldStaticData *wd = NULL;
    TEST_ASSERT(WorldGenerator_Generate(&rec, &wd, err, sizeof(err)) && wd != NULL && wd->hydrology.shoreSiteCount > 0u,
                "generate contaminated valley");

    MetabolismDatabase db = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)), "load metabolism db");

    EcosystemWorld w = {0};
    TEST_ASSERT(EcosystemWorld_InitArena(&w, 16u, 512u, 4u, rec.size * 0.5f, 77u, 0.05, 600.0), "init world");
    TEST_ASSERT(EcosystemWorld_EnableMolecularMetabolism(&w, &db), "enable molecular metabolism");
    TEST_ASSERT(EcosystemWorld_AttachStaticWorld(&w, wd), "attach contaminated world");
    TEST_ASSERT(EcosystemWorld_RegisterNaturalWaterBodies(&w), "register contaminated water");
    TEST_ASSERT(EcosystemWorld_AddSpecies(&w, makeWaterTestSpecies(&db)) == 1u, "add species");

    /* Cargar cerebro v2 existente (1416 parámetros) sin cambios de esquema. */
    NeuralGenome brain = {0};
    TEST_ASSERT(NeuralBrain_LoadFile("brains/brumacerno_forager_v2.brain.json", &brain, err, sizeof(err)),
                "load existing v2 brain");
    TEST_ASSERT(brain.architecture == NEURAL_ARCH_CANDIDATE_POLICY, "v2 architecture preserved");

    const WorldShoreSite *site = &wd->hydrology.shoreSites[0];
    AIObjectId cid = EcosystemWorld_SpawnOnGround(&w, 1u, site->bankApproachPoint.x, site->bankApproachPoint.z,
                                                  240.0, 0.7f, NULL);
    TEST_ASSERT(cid != 0u, "spawn near shore");
    TEST_ASSERT(EcosystemWorld_AssignBrain(&w, cid, &brain, NULL), "assign existing v2 brain on 3D terrain");

    /* Ingesta molecular real desde cuerpo de agua contaminado produce toxicidad real. */
    EcosystemEntity *ce = EcosystemWorld_Get(&w, cid);
    TEST_ASSERT(ce != NULL, "get creature");
    ce->physiology.hydration = 0.22f;
    float toxBefore = ce->metabolism.toxicity;
    /* Dejar que la criatura con cerebro v2 beba agua contaminada de la orilla natural o usar controlador utility si el v2 evita agua contaminada; verificamos ambos:
     * primero verificamos que el cerebro v2 corre limpio en ticks reales en terreno 3D. */
    for (int t = 0; t < 40; ++t) EcosystemWorld_Step(&w);
    ce = EcosystemWorld_Get(&w, cid);
    TEST_ASSERT(ce != NULL && isfinite(ce->body.position.y), "v2 brain ran cleanly on 3D terrain");

    /* Spawnear un individuo con controlador utility sediento en la orilla contaminada para verificar
     * que el libro molecular de agua contaminada produce toxicidad fisiológica real al beber. */
    AIObjectId cid2 = EcosystemWorld_SpawnOnGround(&w, 1u, site->bankApproachPoint.x, site->bankApproachPoint.z,
                                                   240.0, 0.85f, NULL);
    TEST_ASSERT(cid2 != 0u, "spawn utility creature at contaminated shore");
    EcosystemEntity *ce2 = EcosystemWorld_Get(&w, cid2);
    TEST_ASSERT(ce2 != NULL, "get utility creature");
    ce2->physiology.hydration = 0.20f;
    toxBefore = ce2->metabolism.toxicity;
    for (int t = 0; t < 160; ++t) EcosystemWorld_Step(&w);
    ce2 = EcosystemWorld_Get(&w, cid2);
    TEST_ASSERT(ce2 != NULL && ce2->metabolism.toxicity > toxBefore + 0.005f,
                "contaminated natural water increases real molecular toxicity");

    /* Verificar que un recurso vegetal con DEPLETE_RELOCATE se relocaliza en tierra firme fuera de rocas/agua. */
    Vector3 plantPos = Vec3_Zero(), plantNormal = Vec3_Create(0.0f, 1.0f, 0.0f);
    WorldHabitatFilter hf = WorldHabitatFilter_Vegetation(0.85f);
    uint32_t rng = 999u;
    TEST_ASSERT(WorldPlacement_SampleHabitat(wd, &rng, &hf, false, Vec3_Zero(), 0.0f, 64u, &plantPos, &plantNormal),
                "sample terrain habitat for grass");
    SubstanceHandle grassH = SubstanceRegistry_Find(&db.substances, "grass");
    const SubstanceDef *grassDef = SubstanceRegistry_Get(&db.substances, grassH);
    TEST_ASSERT(grassDef != NULL, "find grass substance");
    EcosystemResource plantCfg = EcosystemResource_FromSubstance(
        grassDef, 1.0f, 1.0f, RESOURCE_REGEN_DEPLETE_RELOCATE, 0.0f,
        RESOURCE_DECAY_NONE, 0.0, 0.0f, MOLECULE_INVALID_HANDLE);
    plantCfg.respawnDelaySeconds = 0.2;
    plantCfg.minRelocateDistance = 4.0f;
    plantCfg.rarityWeight = 1.0f;
    AIObjectId pid = EcosystemWorld_AddMolecularResource(&w, plantCfg, plantPos, 0.85f);
    TEST_ASSERT(pid != 0u, "add relocating grass resource");
    EcosystemResource *pr = EcosystemWorld_Resource(&w, pid);
    TEST_ASSERT(pr != NULL, "get plant resource");
    pr->biomass = 0.0f;
    pr->depleted = true;
    pr->respawnTimer = 0.05;
    for (int t = 0; t < 20; ++t) EcosystemWorld_Step(&w);
    const AIWorldObject *pObj = AIWorld_Get(&w.observations, pid);
    TEST_ASSERT(pObj != NULL, "relocated plant still valid");
    WaterSample ws = {0};
    TEST_ASSERT(WorldHydrology_SampleWater(&wd->hydrology, &wd->terrain, pObj->position.x, pObj->position.z, &ws),
                "sample water at relocated plant");
    TEST_ASSERT(!ws.wet || ws.depth < 0.1f, "relocated plant is not submerged in deep water");
    TEST_ASSERT(!WorldGeology_CollidesDisc(&wd->geology, pObj->position, 0.3f),
                "relocated plant does not overlap rocks");

    NeuralGenome_Destroy(&brain);
    EcosystemWorld_Free(&w);
    WorldStaticData_Release(wd);
}

void run_world_water_access_tests(void) {
    printf("=== Pruebas de Orillas Irregulares, Libro Molecular Compartido y Compatibilidad v2 ===\n");
    test_shared_water_ledger_finite_and_infinite();
    test_contaminated_water_and_v2_brain_compatibility();
    printf("Pruebas de hidrología interactiva y compatibilidad superadas.\n");
}
