/**
 * @file test_transform.c
 * @brief Pruebas de verificación de equivalencia para precálculo de bases de rotación en Transform3D.
 */

#include "test_utils.h"
#include "Transform3D.h"
#include <stdio.h>
#include <math.h>

static void test_rotation_basis_equivalence(Vector3 rotation, Vector3 v) {
    Vector3 expected = Transform3D_InverseRotateVector(rotation, v);
    RotationBasis3D basis = Transform3D_BuildInverseRotationBasis(rotation);
    Vector3 actual = Transform3D_ApplyRotationBasis(basis, v);

    TEST_ASSERT(FLOAT_NEAR(expected.x, actual.x), "RotationBasis3D X diff");
    TEST_ASSERT(FLOAT_NEAR(expected.y, actual.y), "RotationBasis3D Y diff");
    TEST_ASSERT(FLOAT_NEAR(expected.z, actual.z), "RotationBasis3D Z diff");
}

void run_transform_tests(void) {
    printf("Ejecutando pruebas de Transform3D (RotationBasis3D)...\n");

    Vector3 testVectors[] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.5f, -2.3f, 4.1f},
        {-10.0f, 5.5f, -0.7f}
    };
    size_t numVecs = sizeof(testVectors) / sizeof(testVectors[0]);

    Vector3 testRotations[] = {
        {0.0f, 0.0f, 0.0f},
        {45.0f, 0.0f, 0.0f},
        {0.0f, 90.0f, 0.0f},
        {0.0f, 0.0f, -30.0f},
        {25.0f, 40.0f, 15.0f},
        {-15.0f, -45.0f, 60.0f}
    };
    size_t numRots = sizeof(testRotations) / sizeof(testRotations[0]);

    for (size_t r = 0; r < numRots; ++r) {
        for (size_t v = 0; v < numVecs; ++v) {
            test_rotation_basis_equivalence(testRotations[r], testVectors[v]);
        }
    }

    printf("  [OK] Transform3D RotationBasis3D equivale a InverseRotateVector exactamente.\n");
}
