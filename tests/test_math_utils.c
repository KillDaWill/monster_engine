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

void run_math_utils_tests(void) {
    test_math_grow_capacity();
}
