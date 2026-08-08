#include "ColorPalette.h"
#include <math.h>

ColorPalette ColorPalette_Create(void) {
    ColorPalette palette;
    palette.count = 0;
    return palette;
}

bool ColorPalette_AddColor(ColorPalette* palette, Color color) {
    if (!palette || palette->count >= MAX_PALETTE_COLORS) {
        return false;
    }
    palette->colors[palette->count++] = color;
    return true;
}

bool ColorPalette_SetColor(ColorPalette* palette, size_t index, Color color) {
    if (!palette || index >= palette->count) {
        return false;
    }
    palette->colors[index] = color;
    return true;
}

Color ColorPalette_GetColor(const ColorPalette* palette, size_t index) {
    if (!palette || index >= palette->count) {
        return COLOR_BLACK;
    }
    return palette->colors[index];
}

size_t ColorPalette_GetCount(const ColorPalette* palette) {
    return palette ? palette->count : 0;
}

Color ColorPalette_Sample(const ColorPalette* palette, float t) {
    if (!palette || palette->count == 0) {
        return COLOR_BLACK;
    }
    if (palette->count == 1) {
        return palette->colors[0];
    }

    if (t <= 0.0f) return palette->colors[0];
    if (t >= 1.0f) return palette->colors[palette->count - 1];

    float scaled = t * (float)(palette->count - 1);
    size_t index = (size_t)scaled;
    float localT = scaled - (float)index;

    if (index >= palette->count - 1) {
        return palette->colors[palette->count - 1];
    }

    return Color_Lerp(palette->colors[index], palette->colors[index + 1], localT);
}

ColorPalette ColorPalette_CreateGradient(Color start, Color end, size_t steps) {
    ColorPalette palette = ColorPalette_Create();
    if (steps == 0) return palette;
    if (steps == 1) {
        ColorPalette_AddColor(&palette, start);
        return palette;
    }

    for (size_t i = 0; i < steps; ++i) {
        float t = (float)i / (float)(steps - 1);
        ColorPalette_AddColor(&palette, Color_Lerp(start, end, t));
    }
    return palette;
}

ColorPalette ColorPalette_CreateHarmonic(Color baseColor, float angleStep, size_t count) {
    ColorPalette palette = ColorPalette_Create();
    ColorHSV hsv = Color_ToHSV(baseColor);

    for (size_t i = 0; i < count; ++i) {
        ColorHSV current = hsv;
        current.h = fmodf(hsv.h + i * angleStep, 360.0f);
        if (current.h < 0.0f) current.h += 360.0f;
        ColorPalette_AddColor(&palette, Color_FromHSV(current));
    }

    return palette;
}
