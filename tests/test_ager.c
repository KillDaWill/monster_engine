#include "test_utils.h"
#include "MonsterAger.h"

void test_monster_ager(void) {
    /* 1. Crear Monstruo Joven (Fase 1: Pequeño y Verde) */
    Monster young = Monster_Create();
    Monster_Init(&young);
    young.colorPalette = ColorPalette_CreateGradient(COLOR_GREEN, COLOR_GREEN, 2);
    BodyPart* headYoung = Monster_GetHead(&young);
    if (headYoung) {
        headYoung->width = 1.0f;
        headYoung->height = 1.0f;
        headYoung->length = 1.0f;
    }

    /* 2. Crear Monstruo Adulto (Fase 2: Grande y Rojo con más partes) */
    Monster adult = Monster_Create();
    Monster_Init(&adult);
    adult.colorPalette = ColorPalette_CreateGradient(COLOR_RED, COLOR_RED, 2);
    BodyPart* headAdult = Monster_GetHead(&adult);
    if (headAdult) {
        headAdult->width = 3.0f;
        headAdult->height = 3.0f;
        headAdult->length = 3.0f;
    }
    BodyPart tail = BodyPart_Create(0.0f, 0.0f, -3.0f, 1.5f, 2.0f, 1.5f, 0.0f);
    Monster_AddBodyPart(&adult, tail);

    /* 3. Crear MonsterAger al 50% de envejecimiento */
    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.5f);
    const Monster* result = MonsterAger_GetResultConst(&ager);

    TEST_ASSERT(result != NULL, "MonsterAger result should not be NULL");
    TEST_ASSERT(result->bodyPartCount == 2, "MonsterAger should equalize body parts to 2");

    /* Verificar dimensiones interpoladas (Mitad entre 1.0f y 3.0f = 2.0f) */
    TEST_ASSERT(FLOAT_NEAR(result->bodyParts[0].width, 2.0f), "Width interpolation at 50% failed");

    /* Verificar interpolación de color (Verde + Rojo al 50% = Amarillo R:127, G:127) */
    Color colorMid = Monster_GetColorFromIndex(result, 0);
    TEST_ASSERT(colorMid.r == 127 && colorMid.g == 127, "Color interpolation at 50% failed");

    /* 4. Cambiar porcentaje al 100% (Adulto) */
    MonsterAger_SetPerc(&ager, 1.0f);
    result = MonsterAger_GetResultConst(&ager);
    TEST_ASSERT(FLOAT_NEAR(result->bodyParts[0].width, 3.0f), "Width interpolation at 100% failed");

    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);

    printf("[PASS] test_monster_ager\n");
}

void run_ager_tests(void) {
    test_monster_ager();
}
