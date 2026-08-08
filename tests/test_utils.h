#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("[FAIL] %s: %s (Line %d)\n", __func__, message, __LINE__); \
            exit(1); \
        } \
    } while(0)

#define FLOAT_NEAR(a, b) (fabsf((a) - (b)) < 0.001f)

#endif // TEST_UTILS_H
