#include "test_utils.h"
#include "Color.h"
#include "ColorPalette.h"

void test_color_creation_and_conversion(void) {
    Color red = Color_FromRGB(255, 0, 0);
    TEST_ASSERT(red.r == 255 && red.g == 0 && red.b == 0 && red.a == 255, "Color_FromRGB failed");

    Color hexColor = Color_FromHex(0xFF0000FF);
    TEST_ASSERT(Color_Equals(red, hexColor), "Color_FromHex failed");

    ColorHSV hsv = Color_ToHSV(red);
    TEST_ASSERT(FLOAT_NEAR(hsv.h, 0.0f) && FLOAT_NEAR(hsv.s, 1.0f) && FLOAT_NEAR(hsv.v, 1.0f), "Color_ToHSV failed");

    Color backToRgb = Color_FromHSV(hsv);
    TEST_ASSERT(Color_Equals(red, backToRgb), "Color_FromHSV failed");

    Color lerpColor = Color_Lerp(COLOR_BLACK, COLOR_WHITE, 0.5f);
    TEST_ASSERT(lerpColor.r == 127 && lerpColor.g == 127 && lerpColor.b == 127, "Color_Lerp failed");

    printf("[PASS] test_color_creation_and_conversion\n");
}

void test_color_palette(void) {
    ColorPalette palette = ColorPalette_CreateGradient(COLOR_BLACK, COLOR_WHITE, 3);
    TEST_ASSERT(ColorPalette_GetCount(&palette) == 3, "ColorPalette_GetCount failed");

    Color mid = ColorPalette_Sample(&palette, 0.5f);
    TEST_ASSERT(mid.r == 127 && mid.g == 127 && mid.b == 127, "ColorPalette_Sample failed");

    printf("[PASS] test_color_palette\n");
}

void run_color_tests(void) {
    test_color_creation_and_conversion();
    test_color_palette();
}
