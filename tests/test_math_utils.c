#include "test_utils.h"
#include "MathUtils.h"
#include <stdint.h>
#include <stddef.h>

void test_math_grow_capacity(void) {
    size_t newCap = 0;

    /* 1. NULL out parameter o elementSize 0 */
    TEST_ASSERT(!Math_GrowCapacity(0, 1, sizeof(int), NULL), "outNewCapacity NULL debe retornar false");
    TEST_ASSERT(!Math_GrowCapacity(0, 1, 0, &newCap), "elementSize 0 debe retornar false");

    /* 2. Crecimiento normal desde 0 */
    TEST_ASSERT(Math_GrowCapacity(0, 1, sizeof(int), &newCap), "Crecimiento desde 0 falló");
    TEST_ASSERT(newCap >= 4, "Capacidad inicial debe ser al menos 4");

    /* 3. Crecimiento de 4 necesitando 5 */
    TEST_ASSERT(Math_GrowCapacity(4, 5, sizeof(int), &newCap), "Crecimiento de 4 a 5 falló");
    TEST_ASSERT(newCap >= 5, "Nueva capacidad debe ser al menos 5");

    /* 4. Solicitud que excede SIZE_MAX / elementSize */
    size_t elemSize = 100;
    size_t maxCap = SIZE_MAX / elemSize;
    TEST_ASSERT(!Math_GrowCapacity(0, maxCap + 1, elemSize, &newCap), "minimumRequired excesivo debe retornar false");
    TEST_ASSERT(!Math_GrowCapacity(maxCap + 1, 1, elemSize, &newCap), "currentCapacity excesivo debe retornar false");

    /* 5. Capacidad cercana a maxCapacity que al duplicar desbordaría */
    size_t nearMax = maxCap / 2 + 10;
    TEST_ASSERT(Math_GrowCapacity(nearMax, nearMax + 1, elemSize, &newCap), "Crecimiento cerca a maxCap falló");
    TEST_ASSERT(newCap == maxCap, "Capacidad desbordante debe sujetarse a maxCapacity");

    printf("[PASS] test_math_grow_capacity\n");
}

void test_math_mul_size(void) {
    size_t res = 0;

    TEST_ASSERT(!Math_MulSize(10, 10, NULL), "out NULL debe retornar false");
    TEST_ASSERT(Math_MulSize(0, 500, &res) && res == 0, "Multiplicación por 0 debe ser 0");
    TEST_ASSERT(Math_MulSize(500, 0, &res) && res == 0, "Multiplicación por 0 debe ser 0");
    TEST_ASSERT(Math_MulSize(123, 456, &res) && res == 123 * 456, "Multiplicación normal falló");
    TEST_ASSERT(!Math_MulSize(SIZE_MAX, 2, &res), "Desbordamiento debe retornar false");
    TEST_ASSERT(!Math_MulSize(SIZE_MAX / 2 + 1, 2, &res), "Desbordamiento limite debe retornar false");
    TEST_ASSERT(Math_MulSize(SIZE_MAX / 2, 2, &res) && res == (SIZE_MAX / 2) * 2, "Multiplicación limite aceptable falló");

    printf("[PASS] test_math_mul_size\n");
}

void run_math_utils_tests(void) {
    test_math_grow_capacity();
    test_math_mul_size();
}
