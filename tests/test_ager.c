#include "test_utils.h"
#include "MonsterAger.h"
#include "MonsterVisual.h"
#include "Eye.h"
#include "MathUtils.h"
#include "Vector.h"
#include "Transform3D.h"
#include "SDFMesher.h"
#include <math.h>
#include <stdio.h>

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

static Monster BuildDemoYoung(void) {
    Monster young = Monster_Create();
    Monster_Init(&young);
    young.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(40, 200, 80), Color_FromRGB(100, 230, 120), 4);
    BodyPart* headY = Monster_GetHead(&young);
    if (headY) { headY->width = 1.0f; headY->height = 0.8f; headY->length = 1.2f; }
    BodyPart chestY = BodyPart_Create(0.0f, 0.0f, -1.3f, 1.2f, 1.4f, 0.9f, 0.0f);
    BodyPart tailY = BodyPart_Create(0.0f, 0.0f, -2.8f, 0.6f, 1.5f, 0.5f, 0.0f);
    Monster_AddBodyPart(&young, chestY);
    Monster_AddBodyPart(&young, tailY);
    BodyPart* hy = Monster_GetHead(&young);
    Mouth youngMouth = Mouth_Create(0, Vec3_Create(0.0f, -hy->height * 0.18f, hy->length * 0.47f), Vec3_Create(hy->width * 0.6f, hy->height * 0.38f, hy->length * 0.42f), Color_FromRGB(100, 10, 10), Color_FromRGB(150, 40, 40));
    youngMouth.openFactor = 0.3f;
    Monster_AddMouth(&young, youngMouth);
    Eye youngLeft = Eye_Create(0, Vec3_Create(-0.28f, 0.12f, 0.45f), Vec3_Create(0.16f, 0.15f, 0.10f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    youngLeft.pupilScale = 0.4f;
    Monster_AddEye(&young, youngLeft);
    Eye youngRight = Eye_Create(0, Vec3_Create(0.28f, 0.12f, 0.45f), Vec3_Create(0.16f, 0.15f, 0.10f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    youngRight.pupilScale = 0.4f;
    Monster_AddEye(&young, youngRight);
    return young;
}

static Monster BuildDemoAdult(void) {
    Monster adult = Monster_Create();
    Monster_Init(&adult);
    adult.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(220, 40, 40), Color_FromRGB(240, 180, 30), 4);
    BodyPart* headA = Monster_GetHead(&adult);
    if (headA) { headA->width = 2.5f; headA->height = 1.8f; headA->length = 2.8f; }
    BodyPart chestA = BodyPart_Create(0.0f, 0.0f, -3.0f, 3.2f, 3.5f, 2.2f, 0.0f);
    BodyPart abdomenA = BodyPart_Create(0.0f, 0.0f, -6.6f, 2.8f, 3.2f, 1.9f, 0.0f);
    BodyPart tail1A = BodyPart_Create(0.0f, 0.0f, -10.0f, 1.8f, 3.0f, 1.4f, 0.0f);
    BodyPart tail2A = BodyPart_Create(0.0f, 0.0f, -13.1f, 0.9f, 2.5f, 0.8f, 0.0f);
    Monster_AddBodyPart(&adult, chestA);
    Monster_AddBodyPart(&adult, abdomenA);
    Monster_AddBodyPart(&adult, tail1A);
    Monster_AddBodyPart(&adult, tail2A);
    BodyPart* ha = Monster_GetHead(&adult);
    Mouth adultMouth = Mouth_Create(0, Vec3_Create(0.0f, -ha->height * 0.18f, ha->length * 0.47f), Vec3_Create(ha->width * 0.64f, ha->height * 0.5f, ha->length * 0.43f), Color_FromRGB(60, 0, 0), Color_FromRGB(200, 30, 30));
    adultMouth.openFactor = 0.95f;
    Monster_AddMouth(&adult, adultMouth);
    Eye adultLeft = Eye_Create(0, Vec3_Create(-0.75f, 0.35f, 1.1f), Vec3_Create(0.42f, 0.4f, 0.25f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    adultLeft.pupilScale = 0.45f;
    Monster_AddEye(&adult, adultLeft);
    Eye adultRight = Eye_Create(0, Vec3_Create(0.75f, 0.35f, 1.1f), Vec3_Create(0.42f, 0.4f, 0.25f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    adultRight.pupilScale = 0.45f;
    Monster_AddEye(&adult, adultRight);
    return adult;
}

void test_bug_eye_pupil_buried_behind_sclera_during_growth(void) {
    Monster young = BuildDemoYoung();
    Monster adult = BuildDemoAdult();
    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.0f);
    const float ages[] = {0.00f, 0.10f, 0.25f, 0.50f, 0.75f, 0.90f, 1.00f};
    const size_t ageCount = sizeof(ages)/sizeof(ages[0]);
    printf("[TRACE] eye pupil straddle sweep (malla observable): age -> scleraFront / pupilFront / pupilRear\n");
    for (size_t ai = 0; ai < ageCount; ++ai) {
        float age = ages[ai];
        MonsterAger_SetPerc(&ager, age);
        const Monster* cur = MonsterAger_GetResultConst(&ager);
        TEST_ASSERT(cur != NULL, "MonsterAger result no debe ser NULL durante barrido pupilar");
        TEST_ASSERT(cur->eyeCount >= 2, "Fixture demo debe tener al menos 2 ojos en cada edad");

        /* Geometría observable: mallas generadas por el pipeline productivo */
        SDFMesherConfig cfg = SDFMesher_DefaultConfig();
        cfg.resolutionX = 8; cfg.resolutionY = 8; cfg.resolutionZ = 8; cfg.voxelSize = 0.0f;
        MonsterVisual visual = MonsterVisual_Create(cfg);
        TEST_ASSERT(MonsterVisual_RebuildNow(&visual, cur, MonsterSDF_DefaultConfig()), "Rebuild visual falló durante barrido pupilar");
        TEST_ASSERT(MonsterVisual_GetEyeCount(&visual) == cur->eyeCount, "Conteo de ojos visual debe coincidir con monstruo");

        for (size_t ei = 0; ei < cur->eyeCount; ++ei) {
            const Eye* eye = &cur->eyes[ei];
            Vector3 partPos = Vec3_Zero();
            if (eye->bodyPartIndex < cur->bodyPartCount) partPos = cur->bodyParts[eye->bodyPartIndex].positionRender;
            Vector3 eyePos = Vec3_Add(partPos, eye->offset);
            Vector3 forward = Transform3D_RotateVector(eye->rotation, Vec3_Create(0.0f, 0.0f, 1.0f));

            const Mesh* sclera = MonsterVisual_GetEyeSclera(&visual, ei);
            const Mesh* pupil = MonsterVisual_GetEyePupil(&visual, ei);
            TEST_ASSERT(sclera && sclera->vertexCount > 0, "Malla de esclerótica vacía");
            TEST_ASSERT(pupil && pupil->vertexCount > 0, "Malla de pupila vacía");

            /* Extremos proyectados sobre el eje forward respecto a eyePos */
            float scleraFront = -1e30f;
            for (size_t vi = 0; vi < sclera->vertexCount; ++vi) {
                float d = Vec3_Dot(Vec3_Sub(sclera->vertices[vi].position, eyePos), forward);
                if (d > scleraFront) scleraFront = d;
            }
            float pupilFront = -1e30f;
            float pupilRear = 1e30f;
            for (size_t vi = 0; vi < pupil->vertexCount; ++vi) {
                float d = Vec3_Dot(Vec3_Sub(pupil->vertices[vi].position, eyePos), forward);
                if (d > pupilFront) pupilFront = d;
                if (d < pupilRear) pupilRear = d;
            }
            /* Pupila halfDepth derivada de la transform real (escala Z de la pupila) */
            Vector3 eyeRadii = Vec3_Scale(eye->scale, 0.5f);
            float pr = Math_Min(eyeRadii.x, eyeRadii.y) * Math_Clamp01(eye->pupilScale) * 0.5f;
            pr = Math_Max(pr, 0.01f);
            float halfDepth = pr * 0.2f;
            float tol = halfDepth * 0.25f;
            if (tol < 0.0005f) tol = 0.0005f;
            if (tol > scleraFront * 0.02f) tol = scleraFront * 0.02f;
            if (tol < 0.0005f) tol = 0.0005f;

            printf("[TRACE] age=%.2f eye=%zu scleraFront=%.4f pupilFront=%.4f pupilRear=%.4f halfDepth=%.4f tol=%.4f\n",
                   age, ei, scleraFront, pupilFront, pupilRear, halfDepth, tol);
            TEST_ASSERT(pupilRear + tol < scleraFront, "REGRESIÓN BUG: pupila debe permanecer ligeramente dentro de esclerótica (rear < front) durante crecimiento");
            TEST_ASSERT(pupilFront > scleraFront + tol, "REGRESIÓN BUG: pupila enterrada/ocluida detrás de esclerótica durante crecimiento — pupil does not straddle sclera front (malla observable)");
        }
        MonsterVisual_Free(&visual);
    }
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);
    printf("[PASS] test_bug_eye_pupil_buried_behind_sclera_during_growth\n");
}

void run_ager_tests(void) {
    test_monster_ager();
    test_bug_eye_pupil_buried_behind_sclera_during_growth();
}
