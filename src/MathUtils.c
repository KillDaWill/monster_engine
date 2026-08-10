#include "MathUtils.h"
#include <stdint.h>

float Math_Clamp(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

float Math_Clamp01(float value) {
    return Math_Clamp(value, 0.0f, 1.0f);
}

float Math_Lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float Math_DegToRad(float degrees) {
    return degrees * ((float)M_PI / 180.0f);
}

float Math_RadToDeg(float radians) {
    return radians * (180.0f / (float)M_PI);
}

float Math_Min(float a, float b) {
    return (a < b) ? a : b;
}

float Math_Max(float a, float b) {
    return (a > b) ? a : b;
}

bool Math_GrowCapacity(size_t currentCapacity, size_t minimumRequired, size_t elementSize, size_t* outNewCapacity) {
    if (!outNewCapacity || elementSize == 0) return false;

    size_t maxCapacity = SIZE_MAX / elementSize;
    if (minimumRequired > maxCapacity || currentCapacity > maxCapacity) return false;

    size_t newCapacity = 0;
    if (currentCapacity == 0) {
        newCapacity = 4;
    } else if (currentCapacity > maxCapacity / 2) {
        newCapacity = maxCapacity;
    } else {
        newCapacity = currentCapacity * 2;
    }

    if (newCapacity < minimumRequired) {
        newCapacity = minimumRequired;
    }

    if (newCapacity > maxCapacity) return false;

    *outNewCapacity = newCapacity;
    return true;
}

bool Math_MulSize(size_t a, size_t b, size_t* out) {
    if (!out) return false;
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if (a > SIZE_MAX / b) {
        return false;
    }
    *out = a * b;
    return true;
}
