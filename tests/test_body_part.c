#include "test_utils.h"
#include "BodyPart.h"

void test_body_part(void) {
    BodyPart part = BodyPart_Create(0.0f, 0.5f, 0.0f, 2.0f, 1.0f, 1.0f, 0.5f);

    TEST_ASSERT(FLOAT_NEAR(part.position.x, 0.0f) && FLOAT_NEAR(part.position.y, 0.5f), "BodyPart initialization failed");

    part.position = Vec3_Create(10.0f, 0.5f, 0.0f);
    BodyPart_RenderUpdate(&part, NULL, 0, 0.016, 0.5);

    TEST_ASSERT(FLOAT_NEAR(part.positionRender.x, 5.0f), "BodyPart_RenderUpdate Lerp failed");

    BodyPart_Free(&part);
    printf("[PASS] test_body_part\n");
}

void run_body_part_tests(void) {
    test_body_part();
}
