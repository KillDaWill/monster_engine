#include "ai_test_helpers.h"
void run_creature_perception_tests(void) {
    AIWorld w = AI_TestWorld();
    AIObjectId water = AI_TestWater(&w, Vec3_Create(0, 0, 0));
    CreatureBodyState b = AI_TestBody();
    PerceptionProfile cfg = {20, 1.57f, 0, .1f, 2};
    CreaturePerception p = {0};
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 0, 0);
    TEST_ASSERT(p.count == 1 && p.entries[0].visible, "agua frontal");
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 3.14159f, .2);
    TEST_ASSERT(p.count == 1 && !p.entries[0].visible, "agua recordada detrás");
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 3.14159f, 3);
    TEST_ASSERT(p.count == 0, "memoria expirada");
    b.eye = Vec3_Create(0, 1, -40);
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 0, 4);
    TEST_ASSERT(p.count == 0, "fuera de alcance");
    b.eye = Vec3_Create(0, 1, -10);
    AIObjectId obstacle = AIWorld_Add(&w, (AIWorldObject){.type = AI_OBSTACLE,
                                                          .position = {0, 0, -5},
                                                          .radius = 2,
                                                          .height = 3,
                                                          .occludes = true});
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 0, 5);
    TEST_ASSERT(p.count == 0, "oclusión cilíndrica");
    AIWorld_Remove(&w, obstacle);
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 0, 6);
    TEST_ASSERT(p.count == 1, "cambio de pose/visión");
    AIWorld_Remove(&w, water);
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 0, 6.01);
    TEST_ASSERT(p.count == 0, "invalidación inmediata entre refrescos");
    AIObjectId newer = AI_TestWater(&w, Vec3_Create(0, 0, 0));
    TEST_ASSERT(newer != water && !AIWorld_Get(&w, water), "IDs no reciclados");
    cfg.verticalFOV = .1f;
    b.eye.y = 10;
    CreaturePerception_Update(&p, &cfg, &w, 0, b.eye, 0, 7);
    TEST_ASSERT(p.count == 0, "FOV vertical");
    unsigned revision = w.revision;
    TEST_ASSERT(AIWorld_SetBounds(&w, newer, 4, 0) && w.revision > revision,
                "cambio de recurso invalida rutas");
    AIObjectId entity = AIWorld_Add(&w, (AIWorldObject){.type = AI_CREATURE, .walkable = true});
    revision = w.revision;
    TEST_ASSERT(AIWorld_SetBounds(&w, entity, 2, 3) && w.revision == revision,
                "crecimiento de entidad conserva rutas estáticas e ID");
    TEST_ASSERT(!AIWorld_SetBounds(&w, entity, -1, 3), "cotas inválidas rechazadas");
}
