#include "Color.h"
#include <math.h>

const Color COLOR_WHITE       = {255, 255, 255, 255};
const Color COLOR_BLACK       = {0, 0, 0, 255};
const Color COLOR_RED         = {255, 0, 0, 255};
const Color COLOR_GREEN       = {0, 255, 0, 255};
const Color COLOR_BLUE        = {0, 0, 255, 255};
const Color COLOR_YELLOW      = {255, 255, 0, 255};
const Color COLOR_MAGENTA     = {255, 0, 255, 255};
const Color COLOR_CYAN        = {0, 255, 255, 255};
const Color COLOR_TRANSPARENT = {0, 0, 0, 0};

Color Color_Create(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color c = {r, g, b, a};
    return c;
}

Color Color_FromRGB(uint8_t r, uint8_t g, uint8_t b) {
    return Color_Create(r, g, b, 255);
}

Color Color_FromHex(uint32_t hex) {
    Color c;
    // Si la opacidad está en los 8 bits menos significativos (0xRRGGBBAA)
    c.r = (hex >> 24) & 0xFF;
    c.g = (hex >> 16) & 0xFF;
    c.b = (hex >> 8) & 0xFF;
    c.a = hex & 0xFF;
    return c;
}

Color Color_FromNormalized(float r, float g, float b, float a) {
    Color c;
    c.r = (uint8_t)(fmaxf(0.0f, fminf(1.0f, r)) * 255.0f);
    c.g = (uint8_t)(fmaxf(0.0f, fminf(1.0f, g)) * 255.0f);
    c.b = (uint8_t)(fmaxf(0.0f, fminf(1.0f, b)) * 255.0f);
    c.a = (uint8_t)(fmaxf(0.0f, fminf(1.0f, a)) * 255.0f);
    return c;
}

ColorHSV Color_ToHSV(Color color) {
    ColorHSV hsv;
    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;

    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float delta = max - min;

    hsv.v = max;
    hsv.a = color.a / 255.0f;

    if (delta < 0.00001f) {
        hsv.s = 0.0f;
        hsv.h = 0.0f;
        return hsv;
    }

    hsv.s = (max > 0.0f) ? (delta / max) : 0.0f;

    if (r >= max) {
        hsv.h = (g - b) / delta;
    } else if (g >= max) {
        hsv.h = 2.0f + (b - r) / delta;
    } else {
        hsv.h = 4.0f + (r - g) / delta;
    }

    hsv.h *= 60.0f;
    if (hsv.h < 0.0f) {
        hsv.h += 360.0f;
    }

    return hsv;
}

Color Color_FromHSV(ColorHSV hsv) {
    float r = 0, g = 0, b = 0;
    float h = hsv.h;
    float s = hsv.s;
    float v = hsv.v;

    if (s <= 0.0f) {
        r = v; g = v; b = v;
    } else {
        if (h >= 360.0f) h = 0.0f;
        h /= 60.0f;
        int i = (int)h;
        float ff = h - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - (s * ff));
        float t = v * (1.0f - (s * (1.0f - ff)));

        switch (i) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            case 5: default: r = v; g = p; b = q; break;
        }
    }

    return Color_FromNormalized(r, g, b, hsv.a);
}

Color Color_Lerp(Color start, Color end, float t) {
    if (t <= 0.0f) return start;
    if (t >= 1.0f) return end;

    Color result;
    result.r = (uint8_t)(start.r + t * (end.r - start.r));
    result.g = (uint8_t)(start.g + t * (end.g - start.g));
    result.b = (uint8_t)(start.b + t * (end.b - start.b));
    result.a = (uint8_t)(start.a + t * (end.a - start.a));
    return result;
}

Color Color_Blend(Color src, Color dest) {
    float srcA = src.a / 255.0f;
    float destA = dest.a / 255.0f;
    float outA = srcA + destA * (1.0f - srcA);

    if (outA <= 0.0f) return COLOR_TRANSPARENT;

    Color result;
    result.r = (uint8_t)((src.r * srcA + dest.r * destA * (1.0f - srcA)) / outA);
    result.g = (uint8_t)((src.g * srcA + dest.g * destA * (1.0f - srcA)) / outA);
    result.b = (uint8_t)((src.b * srcA + dest.b * destA * (1.0f - srcA)) / outA);
    result.a = (uint8_t)(outA * 255.0f);

    return result;
}

Color Color_Multiply(Color color, float factor) {
    Color c;
    c.r = (uint8_t)fmaxf(0.0f, fminf(255.0f, color.r * factor));
    c.g = (uint8_t)fmaxf(0.0f, fminf(255.0f, color.g * factor));
    c.b = (uint8_t)fmaxf(0.0f, fminf(255.0f, color.b * factor));
    c.a = color.a;
    return c;
}

bool Color_Equals(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

uint32_t Color_ToRGBA(Color color) {
    return ((uint32_t)color.r << 24) | ((uint32_t)color.g << 16) | ((uint32_t)color.b << 8) | (uint32_t)color.a;
}

uint32_t Color_ToARGB(Color color) {
    return ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
}

uint32_t Color_ToBGRA(Color color) {
    return ((uint32_t)color.b << 24) | ((uint32_t)color.g << 16) | ((uint32_t)color.r << 8) | (uint32_t)color.a;
}
