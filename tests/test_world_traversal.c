/**
 * @file test_world_traversal.c
 * @brief Pruebas de locomoción, navegación A*, oclusión LOS y adaptación de animación en terreno 3D.
 */
#include "test_utils.h"
#include "WorldGenerator.h"
#include "WorldCollision.h"
#include "EcosystemWorld.h"
#include "CreatureNavigation.h"
#include "TerrainAnimationAdapter.h"
#include "EvolutionSpeciesRegistry.h"
#include "AIWorld.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EcosystemSpecies makeTestHerbivoreSpecies(void) {
    EcosystemSpecies sp = EcosystemSpecies_Default(1u, 1u, ECO_PLANT, 0u, 2u);
    sp.recipeId = 3u;
    sp.traversal = TraversalProfile_DefaultLand(0.95f);
    return sp;
}

static void test_flat_compatibility_and_procedural_spawn(void) {
    /* 1) Compatibilidad plana estricta: rechaza Y != 0. */
    EcosystemWorld flat = {0};
    TEST_ASSERT(EcosystemWorld_InitArena(&flat, 16u, 32u, 4u, 32.0f, 42u, 0.05, 600.0), "init flat world");
    TEST_ASSERT(EcosystemWorld_AddSpecies(&flat, makeTestHerbivoreSpecies()) == 1u, "add species flat");
    TEST_ASSERT(EcosystemWorld_Spawn(&flat, 1u, Vec3_Create(0.0f, 1.5f, 0.0f), 200.0, 0.8f, NULL) == 0u,
                "flat world must reject nonzero Y spawn");
    AIObjectId flatId = EcosystemWorld_Spawn(&flat, 1u, Vec3_Create(2.0f, 0.0f, -2.0f), 200.0, 0.8f, NULL);
    TEST_ASSERT(flatId != 0u, "flat world accepts Y=0 spawn");
    EcosystemWorld_Free(&flat);

    /* 2) Mundo procedural: spawnea a la altura exacta del terreno con orientación válida. */
    WorldRecipe rec;
    char err[256] = {0};
    TEST_ASSERT(WorldRecipe_LoadFile("worlds/lab_valley_small.world.json", &rec, err, sizeof(err)),
                "load lab_valley_small.world.json");
    rec.seed = 42u;
    WorldStaticData *wd = NULL;
    TEST_ASSERT(WorldGenerator_Generate(&rec, &wd, err, sizeof(err)) && wd != NULL,
                "generate lab_valley_small");

    EcosystemWorld proc = {0};
    TEST_ASSERT(EcosystemWorld_InitArena(&proc, 16u, 64u, 4u, rec.size * 0.5f, 42u, 0.05, 600.0), "init proc world");
    TEST_ASSERT(EcosystemWorld_AttachStaticWorld(&proc, wd), "attach static world");
    TEST_ASSERT(EcosystemWorld_AddSpecies(&proc, makeTestHerbivoreSpecies()) == 1u, "add species proc");

    /* Buscar un punto seco y transitable. */
    AIObjectId pid = 0u;
    for (int gx = -20; gx <= 20 && !pid; gx += 2) {
        for (int gz = -20; gz <= 20 && !pid; gz += 2) {
            pid = EcosystemWorld_SpawnOnGround(&proc, 1u, (float)gx, (float)gz, 220.0, 0.8f, NULL);
        }
    }
    TEST_ASSERT(pid != 0u, "spawn on procedural ground succeeded");
    const EcosystemEntity *ent = EcosystemWorld_GetConst(&proc, pid);
    TEST_ASSERT(ent != NULL, "get spawned entity");
    float expectedY = 0.0f;
    TEST_ASSERT(Terrain_SampleHeight(&wd->terrain, ent->body.position.x, ent->body.position.z, &expectedY),
                "sample height at entity");
    TEST_ASSERT(fabsf(ent->body.position.y - expectedY) < 0.02f, "entity Y matches terrain height");
    float qLen = sqrtf(ent->body.orientation.x * ent->body.orientation.x +
                       ent->body.orientation.y * ent->body.orientation.y +
                       ent->body.orientation.z * ent->body.orientation.z +
                       ent->body.orientation.w * ent->body.orientation.w);
    TEST_ASSERT(fabsf(qLen - 1.0f) < 0.01f, "entity orientation quaternion normalized");
    TEST_ASSERT(ent->body.groundNormal.y > 0.5f, "entity groundNormal points upward");

    EcosystemWorld_Free(&proc);
    WorldStaticData_Release(wd);
}

static void test_slope_cliff_and_wading_mechanics(void) {
    WorldRecipe rec = WorldRecipe_Flat(64.0f);
    rec.flat = false;
    rec.seed = 101u;
    rec.terrain.resolution = 65u;
    rec.hydrology.enabled = false;
    rec.geology.rockDensity = 0.0f;

    WorldStaticData *wd = WorldStaticData_Alloc();
    TEST_ASSERT(wd != NULL, "create synthetic slope/cliff world");
    wd->recipe = rec;
    wd->masterSeed = rec.seed;
    wd->generatorVersion = rec.generatorVersion;
    TEST_ASSERT(TerrainHeightfield_Init(&wd->terrain, 65u, 64.0f), "init synthetic terrain");
    for (uint32_t gz = 0; gz < wd->terrain.resolution; ++gz) {
        for (uint32_t gx = 0; gx < wd->terrain.resolution; ++gx) {
            float x = wd->terrain.originX + (float)gx * wd->terrain.spacing;
            float h = 2.0f;
            if (x > -6.0f && x <= 6.0f) {
                h = 2.0f + (x + 6.0f) * (3.5f / 12.0f); /* pendiente ~16.2 grados */
            } else if (x > 6.0f && x <= 12.0f) {
                h = 5.5f;
            } else if (x > 12.0f && x <= 15.0f) {
                h = 5.5f + (x - 12.0f) * 3.5f; /* pendiente ~74 grados: acantilado */
            } else if (x > 15.0f) {
                h = 16.0f;
            }
            wd->terrain.heights[(size_t)gz * wd->terrain.resolution + gx] = h;
        }
    }
    char err[256] = {0};
    TEST_ASSERT(WorldGenerator_FinalizeFromHeightfield(wd, false, false, err, sizeof(err)),
                "finalize synthetic slope/cliff world");

    TraversalProfile prof = TraversalProfile_DefaultLand(0.95f);

    /* Subir rampa vs plano: menor velocidad y mayor coste energético. */
    TraversalResult rFlat = {0}, rUp = {0}, rDown = {0}, rCliff = {0};
    World_CheckTraversalSegment(wd, Vec3_Create(-16.0f, 2.0f, 0.0f), Vec3_Create(-12.0f, 2.0f, 0.0f), &prof, &rFlat);
    World_CheckTraversalSegment(wd, Vec3_Create(-4.0f, 2.58f, 0.0f), Vec3_Create(2.0f, 4.33f, 0.0f), &prof, &rUp);
    World_CheckTraversalSegment(wd, Vec3_Create(2.0f, 4.33f, 0.0f), Vec3_Create(-4.0f, 2.58f, 0.0f), &prof, &rDown);
    TEST_ASSERT(rFlat.traversable && rUp.traversable && rDown.traversable, "flat and gentle ramp traversable");
    TEST_ASSERT(rUp.speedMultiplier < rFlat.speedMultiplier, "uphill slower than flat");
    TEST_ASSERT(rUp.energyMultiplier > rFlat.energyMultiplier, "uphill costs more energy than flat");
    TEST_ASSERT(rDown.speedMultiplier > 0.4f && rDown.speedMultiplier <= 1.25f, "downhill speed bounded");

    /* Acantilado debe bloquearse explícitamente. */
    World_CheckTraversalSegment(wd, Vec3_Create(9.0f, 5.5f, 0.0f), Vec3_Create(18.0f, 16.0f, 0.0f), &prof, &rCliff);
    TEST_ASSERT(!rCliff.traversable, "steep cliff blocks segment traversal");

    /* Oclusión LOS a través de cresta/acantilado. */
    Vector3 lowA = Vec3_Create(10.0f, 6.5f, 0.0f);
    Vector3 lowB = Vec3_Create(18.0f, 16.2f, 0.0f);
    TEST_ASSERT(!World_ClearSightSegment(wd, lowA, lowB, 0.05f, 0.05f), "ridge/cliff blocks low sightline");
    TEST_ASSERT(World_ClearSightSegment(wd, Vec3_Create(-16.0f, 3.2f, 0.0f),
                                        Vec3_Create(-8.0f, 3.2f, 0.0f), 0.05f, 0.05f),
                "flat plateau has clear sightline");

    WorldStaticData_Release(wd);
}

static void test_astar_cliff_detour_profile_ford_and_unreachable(void) {
    WorldRecipe rec = WorldRecipe_Flat(64.0f);
    rec.flat = false;
    rec.seed = 202u;
    rec.terrain.resolution = 65u;
    rec.hydrology.enabled = false;
    rec.geology.rockDensity = 0.0f;

    WorldStaticData *wd = WorldStaticData_Alloc();
    TEST_ASSERT(wd != NULL, "create nav synthetic world");
    wd->recipe = rec;
    wd->masterSeed = rec.seed;
    wd->generatorVersion = rec.generatorVersion;
    TEST_ASSERT(TerrainHeightfield_Init(&wd->terrain, 65u, 64.0f), "init nav synthetic terrain");
    for (uint32_t gz = 0; gz < wd->terrain.resolution; ++gz) {
        for (uint32_t gx = 0; gx < wd->terrain.resolution; ++gx) {
            float x = wd->terrain.originX + (float)gx * wd->terrain.spacing;
            float z = wd->terrain.originZ + (float)gz * wd->terrain.spacing;
            float h = 2.0f;
            /* Muro/acantilado central bloqueando paso directo entre x=-10 y x=+10 en z in [-16, 12]. */
            if (fabsf(x) <= 2.2f && z >= -16.0f && z <= 12.0f) {
                h = 14.0f;
            }
            /* Anillo cerrado intransitable alrededor de (-22, -22) radio [3, 6.5]. */
            float dr = hypotf(x + 22.0f, z + 22.0f);
            if (dr >= 3.0f && dr <= 6.5f) {
                h = 16.0f;
            }
            wd->terrain.heights[(size_t)gz * wd->terrain.resolution + gx] = h;
        }
    }
    char err[256] = {0};
    TEST_ASSERT(WorldGenerator_FinalizeFromHeightfield(wd, false, false, err, sizeof(err)),
                "finalize nav synthetic world");

    /* Inyectar banda de vado somero en x in [-2.5, 2.5], z in [12, 22] (profundidad 0.35m)
     * para comprobar rutas dependientes del perfil (vadeador vs no vadeador). */
    for (uint32_t cz = 0; cz < wd->navBase.dim; ++cz) {
        float wz = -wd->navBase.halfSize + ((float)cz + 0.5f) * wd->navBase.cellSize;
        for (uint32_t cx = 0; cx < wd->navBase.dim; ++cx) {
            float wx = -wd->navBase.halfSize + ((float)cx + 0.5f) * wd->navBase.cellSize;
            if (fabsf(wx) <= 2.5f && wz >= 12.0f && wz <= 22.0f) {
                wd->navBase.cellMaxWaterDepth[(size_t)cz * wd->navBase.dim + cx] = 0.35f;
            }
        }
    }

    AIWorld aw = {0};
    AIWorld_Init(&aw, 32.0f);
    AIWorld_SetStaticWorld(&aw, wd);

    TraversalProfile wader = TraversalProfile_DefaultLand(0.95f);
    wader.canWade = true;
    wader.maxWadingDepth = 0.70f;
    wader.wadingSpeedFactor = 0.75f;

    TraversalProfile dryOnly = TraversalProfile_DefaultLand(0.95f);
    dryOnly.canWade = false;
    dryOnly.maxWadingDepth = 0.02f;

    CreatureNavigation pathWader = {0};
    bool okWader = CreatureNavigation_PlanWithProfile(
        &pathWader, &aw, Vec3_Create(-10.0f, 2.0f, 0.0f), Vec3_Create(10.0f, 2.0f, 0.0f),
        &wader);
    TEST_ASSERT(okWader && pathWader.count >= 2u, "A* plans route around cliff for wader");
    bool detoured = false;
    for (unsigned i = 0; i < pathWader.count; ++i) {
        TEST_ASSERT(pathWader.points[i].y > 1.0f && pathWader.points[i].y < 5.0f, "waypoints carry true terrain Y");
        if (pathWader.points[i].z > 11.0f || pathWader.points[i].z < -15.0f) detoured = true;
    }
    TEST_ASSERT(detoured, "A* actually detours around cliff wall");

    CreatureNavigation pathDry = {0};
    bool okDry = CreatureNavigation_PlanWithProfile(
        &pathDry, &aw, Vec3_Create(-10.0f, 2.0f, 0.0f), Vec3_Create(10.0f, 2.0f, 0.0f),
        &dryOnly);
    TEST_ASSERT(okDry && pathDry.count >= 2u, "A* plans dry detour avoiding shallow ford");
    for (unsigned i = 0; i < pathDry.count; ++i) {
        if (fabsf(pathDry.points[i].x) < 1.5f) {
            TEST_ASSERT(!(pathDry.points[i].z >= 13.5f && pathDry.points[i].z <= 20.5f),
                        "non-wading profile avoids shallow ford");
        }
    }

    /* Destino dentro de anillo cerrado intransitable debe fallar explícitamente. */
    CreatureNavigation pathFail = {0};
    bool okUnreach = CreatureNavigation_PlanWithProfile(
        &pathFail, &aw, Vec3_Create(-10.0f, 2.0f, 0.0f), Vec3_Create(-22.0f, 2.0f, -22.0f),
        &wader);
    TEST_ASSERT(!okUnreach, "A* explicitly fails on unreachable cliff-ring target");

    AIWorld_Destroy(&aw);
    WorldStaticData_Release(wd);
}

static void test_terrain_animation_adapter_on_slope(void) {
    WorldRecipe rec = WorldRecipe_Flat(64.0f);
    rec.flat = false;
    rec.seed = 303u;
    rec.terrain.resolution = 65u;
    rec.hydrology.enabled = false;
    rec.geology.rockDensity = 0.0f;

    WorldStaticData *wd = WorldStaticData_Alloc();
    TEST_ASSERT(wd != NULL, "create slope adapter world");
    wd->recipe = rec;
    wd->masterSeed = rec.seed;
    wd->generatorVersion = rec.generatorVersion;
    TEST_ASSERT(TerrainHeightfield_Init(&wd->terrain, 65u, 64.0f), "init slope adapter terrain");
    const float slope = 0.32f; /* ~17.7 deg */
    for (uint32_t gz = 0; gz < wd->terrain.resolution; ++gz) {
        for (uint32_t gx = 0; gx < wd->terrain.resolution; ++gx) {
            float z = wd->terrain.originZ + (float)gz * wd->terrain.spacing;
            wd->terrain.heights[(size_t)gz * wd->terrain.resolution + gx] = 3.0f + slope * z;
        }
    }
    char err[256] = {0};
    TEST_ASSERT(WorldGenerator_FinalizeFromHeightfield(wd, false, false, err, sizeof(err)),
                "finalize slope adapter world");

    SurfaceHit g = {0};
    TEST_ASSERT(Terrain_SampleGround(&wd->terrain, 0.0f, 4.0f, &g), "sample slope ground");
    Quaternion orient = World_BuildBodyFrame(0.0f, g.normal, 0.7f);

    TerrainAnimationAdapter adapter = {0};
    TerrainAnimationAdapter_Init(&adapter, wd, g.position, orient, Vec3_Zero(), 1.0f);
    World *w = TerrainAnimationAdapter_AsWorld(&adapter);

    SurfaceHit localHit = {0};
    TEST_ASSERT(World_SampleGround(w, Vec3_Create(0.0f, 0.0f, 1.5f), &localHit),
                "adapter samples forward foot on slope");
    TEST_ASSERT(fabsf(localHit.position.y) < 0.08f, "slope-aligned local ground height near 0");
    TEST_ASSERT(localHit.normal.y > 0.95f, "slope-aligned local normal near +Y");

    Rig rig = {.limbCount = 1};
    rig.limbs[0].walking = true;
    rig.limbs[0].soleHeight = .12f;
    Vector3 point = Vec3_Add(localHit.position, Vec3_Scale(localHit.normal, rig.limbs[0].soleHeight));
    Locomotion locomotion = {.initialized = true};
    locomotion.feet[0] = (FootState){.phase = FOOT_STANCE, .normal = localHit.normal,
        .plantedPosition = point, .targetPosition = point, .swingStart = point, .swingTarget = point};
    TerrainAnimationAdapter moved;
    Quaternion rotated = Quat_Multiply(orient, Quat_FromAxisAngle(Vec3_Create(0, 0, 1), .22f));
    TerrainAnimationAdapter_Init(&moved, wd, Vec3_Add(g.position, Vec3_Create(.34f,.09f,.29f)),
                                 rotated, Vec3_Create(.03f,0,.08f), 1.3f);
    TerrainAnimationAdapter_RebaseFeet(&locomotion, &rig, &adapter, &moved);
    FootState foot = locomotion.feet[0];
    Vector3 sole = Vec3_Sub(foot.plantedPosition, Vec3_Scale(foot.normal, rig.limbs[0].soleHeight));
    Vector3 worldSole = World_LocalToWorld(sole, moved.modelCenter, moved.scale, moved.bodyOrientation, moved.worldOrigin);
    Vector3 beforeSole = World_LocalToWorld(localHit.position, adapter.modelCenter, adapter.scale,
                                           adapter.bodyOrientation, adapter.worldOrigin);
    TEST_ASSERT(Vec3_Distance(worldSole, beforeSole) < 1e-5f, "planted sole stays fixed under roll, growth and translation");
    TEST_ASSERT(Vec3_Distance(foot.targetPosition, foot.plantedPosition) < 1e-5f, "stance target stays consistent");


    WorldStaticData_Release(wd);
}

void run_world_traversal_tests(void) {
    printf("=== Pruebas de Traversabilidad, A* Topográfico, LOS y Adaptador de Animación ===\n");
    test_flat_compatibility_and_procedural_spawn();
    test_slope_cliff_and_wading_mechanics();
    test_astar_cliff_detour_profile_ford_and_unreachable();
    test_terrain_animation_adapter_on_slope();
    printf("Pruebas de traversabilidad y navegación superadas.\n");
}
