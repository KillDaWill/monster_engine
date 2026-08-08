#include "test_utils.h"
#include "Vector.h"

void test_vectors_fixed(void) {
    Vector3 v1 = Vec3_Create(1.0f, 2.0f, 3.0f);
    Vector3 v2 = Vec3_Create(4.0f, 5.0f, 6.0f);

    Vector3 add = Vec3_Add(v1, v2);
    TEST_ASSERT(FLOAT_NEAR(add.x, 5.0f) && FLOAT_NEAR(add.y, 7.0f) && FLOAT_NEAR(add.z, 9.0f), "Vec3_Add failed");

    float dot = Vec3_Dot(v1, v2);
    TEST_ASSERT(FLOAT_NEAR(dot, 32.0f), "Vec3_Dot failed");

    Vector3 norm = Vec3_Normalize(Vec3_Create(3.0f, 0.0f, 0.0f));
    TEST_ASSERT(FLOAT_NEAR(norm.x, 1.0f) && FLOAT_NEAR(norm.y, 0.0f), "Vec3_Normalize failed");

    printf("[PASS] test_vectors_fixed\n");
}

void test_vector_nd(void) {
    float dataA[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    float dataB[] = {5.0f, 4.0f, 3.0f, 2.0f, 1.0f};

    VectorND vA = VecND_FromData(5, dataA);
    VectorND vB = VecND_FromData(5, dataB);
    VectorND vOut = VecND_Create(5);

    TEST_ASSERT(VecND_Add(&vA, &vB, &vOut), "VecND_Add execution failed");
    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT(FLOAT_NEAR(VecND_Get(&vOut, i), 6.0f), "VecND_Add output mismatch");
    }

    float dot = VecND_Dot(&vA, &vB);
    TEST_ASSERT(FLOAT_NEAR(dot, 35.0f), "VecND_Dot failed");

    VecND_Free(&vA);
    VecND_Free(&vB);
    VecND_Free(&vOut);

    printf("[PASS] test_vector_nd\n");
}

void run_vector_tests(void) {
    test_vectors_fixed();
    test_vector_nd();
}
