#include "test_utils.h"
#include "Monster.h"

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

void run_monster_tests(void) {
    test_monster_lifecycle();
}
