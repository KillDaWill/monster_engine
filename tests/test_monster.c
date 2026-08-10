#include "test_utils.h"
#include "Monster.h"
#include <string.h>

void test_monster_lifecycle(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);

    TEST_ASSERT(monster.bodyPartCount == 1, "Monster_Init should create 1 default head part");

    BodyPart body = BodyPart_Create(0.0f, 0.0f, 5.0f, 1.5f, 2.0f, 1.5f, 0.0f);
    TEST_ASSERT(Monster_AddBodyPart(&monster, body), "Monster_AddBodyPart failed");
    TEST_ASSERT(monster.bodyPartCount == 2, "Monster should have 2 body parts now");

    Vector3 center = Monster_GetCenter(&monster);
    TEST_ASSERT(FLOAT_NEAR(center.z, 2.5f), "Monster_GetCenter calculation failed");

    float totalLength = Monster_GetTotalLength(&monster);
    TEST_ASSERT(FLOAT_NEAR(totalLength, 3.0f), "Monster_GetTotalLength failed");

    /* Muestreo de posición por porcentaje */
    Vector3 headPos = Monster_GetPosition(&monster, 0.0);
    TEST_ASSERT(FLOAT_NEAR(headPos.z, 0.0f), "Monster_GetPosition at 0% failed");

    Monster_Free(&monster);
    printf("[PASS] test_monster_lifecycle\n");
}

typedef struct CustomBehavior {
    MonsterBehavior base;
    int* count;
} CustomBehavior;

static void BehaviorUpdate_Callback(struct MonsterBehavior* self, struct Monster* m, double diff) {
    (void)m; (void)diff;
    CustomBehavior* cb = (CustomBehavior*)self;
    if (cb && cb->count) (*cb->count)++;
}

static void test_behavior_update(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);

    int updateCallCount = 0;
    CustomBehavior behavior;
    memset(&behavior, 0, sizeof(behavior));
    behavior.base.update = BehaviorUpdate_Callback;
    behavior.count = &updateCallCount;

    monster.behavior = &behavior.base;
    Monster_Update(&monster, 0.016);

    TEST_ASSERT(updateCallCount == 1, "MonsterBehavior.update no se invocó exactamente una vez");

    Monster_Free(&monster);
    printf("[PASS] test_behavior_update\n");
}

typedef struct TestCounter {
    int callCount;
    int receivedIndices[10];
} TestCounter;

typedef struct CustomMonsterTrait {
    Trait base;
    TestCounter* counter;
} CustomMonsterTrait;

static void TestMonsterTrait_Update(Trait* self, Monster* monster, int index, double diff) {
    (void)monster; (void)diff;
    CustomMonsterTrait* ct = (CustomMonsterTrait*)self;
    if (ct && ct->counter && ct->counter->callCount < 10) {
        ct->counter->receivedIndices[ct->counter->callCount] = index;
        ct->counter->callCount++;
    }
}

static void test_monster_trait_update(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster); /* 1 parte (cabeza) */

    BodyPart chest = BodyPart_Create(0.0f, 0.0f, -2.0f, 1.0f, 1.0f, 1.0f, 0.0f);
    BodyPart tail = BodyPart_Create(0.0f, 0.0f, -4.0f, 1.0f, 1.0f, 1.0f, 0.0f);
    Monster_AddBodyPart(&monster, chest);
    Monster_AddBodyPart(&monster, tail); /* Total 3 partes */

    TestCounter counter = {0};
    CustomMonsterTrait trait;
    memset(&trait, 0, sizeof(trait));
    trait.base.type = (TraitType)999;
    trait.base.update = TestMonsterTrait_Update;
    trait.counter = &counter;

    Monster_AddTrait(&monster, &trait.base);
    Monster_Update(&monster, 0.016);

    TEST_ASSERT(counter.callCount == 3, "Monster Trait.update debe ejecutarse N veces para N partes");
    TEST_ASSERT(counter.receivedIndices[0] == 0, "Índice 0 incorrecto");
    TEST_ASSERT(counter.receivedIndices[1] == 1, "Índice 1 incorrecto");
    TEST_ASSERT(counter.receivedIndices[2] == 2, "Índice 2 incorrecto");

    Monster_Free(&monster);
    printf("[PASS] test_monster_trait_update\n");
}

typedef struct CustomBodyPartTrait {
    Trait base;
    int* count;
} CustomBodyPartTrait;

static void TestBodyPartTrait_Update(Trait* self, Monster* monster, int index, double diff) {
    (void)monster; (void)index; (void)diff;
    CustomBodyPartTrait* ct = (CustomBodyPartTrait*)self;
    if (ct && ct->count) (*ct->count)++;
}

static void test_bodypart_trait_update(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);

    int callCount = 0;
    CustomBodyPartTrait trait;
    memset(&trait, 0, sizeof(trait));
    trait.base.type = (TraitType)888;
    trait.base.update = TestBodyPartTrait_Update;
    trait.count = &callCount;

    BodyPart* head = Monster_GetHead(&monster);
    BodyPart_AddTrait(head, &trait.base);

    Monster_Update(&monster, 0.016);
    TEST_ASSERT(callCount == 1, "BodyPart Trait.update debe ejecutarse exactamente una vez");

    Monster_Free(&monster);
    printf("[PASS] test_bodypart_trait_update\n");
}

typedef struct PositionBehaviorContext {
    MonsterBehavior base;
    int tick;
} PositionBehaviorContext;

static void PositionBehavior_Update(struct MonsterBehavior* self, Monster* monster, double diff) {
    (void)diff;
    PositionBehaviorContext* ctx = (PositionBehaviorContext*)self;
    BodyPart* head = Monster_GetHead(monster);
    if (!head || !ctx) return;

    if (ctx->tick == 0) {
        head->position.x = 10.0f;
    } else if (ctx->tick == 1) {
        head->position.x = 20.0f;
    }
    ctx->tick++;
}

static void test_position_lifecycle(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);

    BodyPart* head = Monster_GetHead(&monster);
    head->position.x = 0.0f;

    PositionBehaviorContext behavior;
    memset(&behavior, 0, sizeof(behavior));
    behavior.base.update = PositionBehavior_Update;
    behavior.tick = 0;

    monster.behavior = &behavior.base;

    /* Primer Tick */
    Monster_Update(&monster, 0.016);
    TEST_ASSERT(FLOAT_NEAR(head->oldPosition.x, 0.0f), "oldPosition en tick 1 debe ser 0");
    TEST_ASSERT(FLOAT_NEAR(head->position.x, 10.0f), "position en tick 1 debe ser 10");

    Monster_RenderUpdate(&monster, 0.016, 0.5);
    TEST_ASSERT(FLOAT_NEAR(head->positionRender.x, 5.0f), "positionRender al 50% lerp debe ser 5");

    /* Segundo Tick */
    Monster_Update(&monster, 0.016);
    TEST_ASSERT(FLOAT_NEAR(head->oldPosition.x, 10.0f), "oldPosition en tick 2 debe ser 10");
    TEST_ASSERT(FLOAT_NEAR(head->position.x, 20.0f), "position en tick 2 debe ser 20");

    Monster_RenderUpdate(&monster, 0.016, 0.5);
    TEST_ASSERT(FLOAT_NEAR(head->positionRender.x, 15.0f), "positionRender al 50% lerp en tick 2 debe ser 15");

    Monster_Free(&monster);
    printf("[PASS] test_position_lifecycle\n");
}

void run_monster_tests(void) {
    test_monster_lifecycle();
    test_behavior_update();
    test_monster_trait_update();
    test_bodypart_trait_update();
    test_position_lifecycle();
}
