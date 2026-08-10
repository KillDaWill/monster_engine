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
    if (minimumRequired > SIZE_MAX / elementSize) return false;

    size_t newCap = (currentCapacity == 0) ? 4 : currentCapacity * 2;
    if (newCap < minimumRequired) newCap = minimumRequired;
    if (newCap > SIZE_MAX / elementSize) return false;

    *outNewCapacity = newCap;
    return true;
}
