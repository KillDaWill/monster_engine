#include "test_utils.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include <stdlib.h>
#include "CreatureRecipes.h"
#include "MonsterAger.h"
#include "MonsterVisual.h"
#include "Eye.h"
#include "EyeTexture.h"
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

    /* El reloj limita extrapolación en ambos sentidos, sin bloquear el hilo visual. */
    MonsterAger_SetPerc(&ager,.5f);
    TEST_ASSERT(MonsterAger_AdvanceWithGeometry(&ager,1,.5f,.05f)<=.550001f,"Límite de avance con geometría atrasada");
    float stopped=ager.perc;
    TEST_ASSERT(FLOAT_NEAR(MonsterAger_AdvanceWithGeometry(&ager,.1f,.5f,.05f),stopped),"No acumular desfase");
    TEST_ASSERT(MonsterAger_AdvanceWithGeometry(&ager,-1,.5f,.05f)>=.449999f,"Límite de retroceso");
    stopped=ager.perc;
    TEST_ASSERT(FLOAT_NEAR(MonsterAger_AdvanceWithGeometry(&ager,NAN,.5f,.05f),stopped),"Rechazar tiempo no finito");

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
    printf("[TRACE] eye globe and appearance sweep: age -> globe vertices / material fingerprint\n");
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
            (void)partPos;
            const Mesh* globe = MonsterVisual_GetEyeSclera(&visual, ei);
            TEST_ASSERT(globe && globe->vertexCount>0&&Mesh_Validate(globe).valid,"Globo tridimensional válido durante el crecimiento");
            TEST_ASSERT(MonsterVisual_GetEyeIris(&visual,ei)==NULL&&MonsterVisual_GetEyePupil(&visual,ei)==NULL,"Iris y pupila siguen siendo material, no mallas");
            TEST_ASSERT(EyeTexture_Fingerprint(eye)!=0,"Apariencia ocular con huella válida");
            printf("[TRACE] age=%.2f eye=%zu globeVertices=%zu material=%llu\n",age,ei,globe->vertexCount,(unsigned long long)EyeTexture_Fingerprint(eye));
        }
        MonsterVisual_Free(&visual);
    }
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);
    printf("[PASS] test_bug_eye_pupil_buried_behind_sclera_during_growth\n");
}

void test_ager_cross_species_morph_dog_to_lizard(void) {
    Monster dog = Monster_Create();
    const CreatureRecipe* dogRecipe = CreatureRecipes_Dog();
    CreaturePhenotype dogPhenotype = dogRecipe->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog, dogRecipe, &dogPhenotype), "Construcción de perro adulto falló");

    Monster lizard = Monster_Create();
    const CreatureRecipe* lizardRecipe = CreatureRecipes_Lizard();
    CreaturePhenotype lizardPhenotype = lizardRecipe->adult;
    TEST_ASSERT(Creature_BuildMonster(&lizard, lizardRecipe, &lizardPhenotype), "Construcción de lagarto adulto falló");

    MonsterAger ager = MonsterAger_Create(&dog, &lizard, 0.0f);
    const float testAges[] = {0.0f, 0.20f, 0.50f, 0.80f, 1.0f};
    const size_t testCount = sizeof(testAges)/sizeof(testAges[0]);

    for (size_t i = 0; i < testCount; ++i) {
        float age = testAges[i];
        MonsterAger_SetPerc(&ager, age);
        const Monster* m = MonsterAger_GetResultConst(&ager);
        TEST_ASSERT(m != NULL, "Resultado del ager no debe ser NULL en metamorfosis inter-especies");
        TEST_ASSERT(m->hasCreaturePhenotype, "El resultado debe conservar fenotipo biológico");
        TEST_ASSERT(AnatomyGraph_Validate(&m->anatomyGraph), "Grafo anatómico debe ser válido en cualquier fase de la metamorfosis");

        /* Verificación de la transición de cobertura: Pelo -> Piel desnuda -> Escamas */
        if (age < 0.45f) {
            TEST_ASSERT(m->surface.integument.type == INTEGUMENT_FUR, "Fase temprana debe ser pelaje");
            TEST_ASSERT(m->surface.integument.coverage <= 1.0f && m->surface.integument.coverage >= 0.0f, "Cobertura de pelaje acotada");
        } else if (age >= 0.45f && age < 0.55f) {
            TEST_ASSERT(m->surface.integument.type == INTEGUMENT_SMOOTH_SKIN, "Fase intermedia debe ser piel desnuda");
            TEST_ASSERT(m->surface.integument.coverage == 0.0f, "Piel desnuda intermedia no debe tener cobertura");
        } else {
            TEST_ASSERT(m->surface.integument.type == INTEGUMENT_SCALES, "Fase tardía debe ser escamas");
            TEST_ASSERT(m->surface.integument.coverage <= 1.0f && m->surface.integument.coverage >= 0.0f, "Cobertura de escamas acotada");
        }

        /* Verificación de continuidad de escala, cola y miembros */
        TEST_ASSERT(m->anatomyGraph.nodeCount >= 100, "Conteo suficiente de nodos anatómicos");
    }

    /* Verificación de continuidad métrica alrededor de t=0.50 (sin popping ni saltos bruscos) */
    Vector3 prevNeck = Vec3_Zero(), prevElbow = Vec3_Zero(), prevPelvis = Vec3_Zero();
    for (float t = 0.40f; t <= 0.60f; t += 0.02f) {
        MonsterAger_SetPerc(&ager, t);
        const Monster* m = MonsterAger_GetResultConst(&ager);
        const AnatomyNode* neck = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_NECK);
        const AnatomyNode* elbow = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 10, 2);
        const AnatomyNode* pelvis = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_PELVIS);
        TEST_ASSERT(neck && elbow && pelvis, "Nodos semánticos principales deben existir durante la metamorfosis");

        if (t > 0.405f) {
            float dNeck = Vec3_Distance(neck->center, prevNeck);
            float dElbow = Vec3_Distance(elbow->center, prevElbow);
            float dPelvis = Vec3_Distance(pelvis->center, prevPelvis);
            /* Para un paso dt=0.02, el desplazamiento debe ser continuo y acotado (< 0.25 unidades) */
            TEST_ASSERT(dNeck < 0.25f, "El cuello debe desplazarse suavemente sin saltos discontinuos");
            TEST_ASSERT(dElbow < 0.25f, "El codo debe rotar suavemente sin saltos discontinuos");
            TEST_ASSERT(dPelvis < 0.25f, "La pelvis debe descender suavemente sin saltos discontinuos");
        }
        prevNeck = neck->center;
        prevElbow = elbow->center;
        prevPelvis = pelvis->center;
    }

    MonsterAger_Free(&ager);
    Monster_Free(&dog);
    Monster_Free(&lizard);
    printf("[PASS] test_ager_cross_species_morph_dog_to_lizard\n");
}

static void test_resolved_morph_continuity(void) {
    Monster a=Monster_Create(),b=Monster_Create(),previous=Monster_Create(),current=Monster_Create(),reverse=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&a,CreatureRecipes_Dog(),&CreatureRecipes_Dog()->adult),"Perro de referencia");
    TEST_ASSERT(Creature_BuildMonster(&b,CreatureRecipes_Lizard(),&CreatureRecipes_Lizard()->adult),"Lagarto de referencia");
    TEST_ASSERT(CreatureMorph_Interpolate(&a,&b,0,&previous),"Inicio resuelto");
    float maxStep=0,maxHeadStep=0;
    for(unsigned i=1;i<=100;++i) {
        float t=(float)i/100;
        TEST_ASSERT(CreatureMorph_Interpolate(&a,&b,t,&current),"Interpolación resuelta");
        TEST_ASSERT(CreatureMorph_Interpolate(&b,&a,1-t,&reverse),"Interpolación reversible");
        TEST_ASSERT(AnatomyGraph_Validate(&current.anatomyGraph),"Grafo y perfil finitos");
        TEST_ASSERT(AnatomyGraph_TopologyCompatible(&previous.anatomyGraph,&current.anatomyGraph),"Blueprint estable incluso en extremos y al 50 por ciento");
        for(size_t j=0;j<current.anatomyGraph.nodeCount;++j) {
            const AnatomyNode* n=&current.anatomyGraph.nodes[j];
            const AnatomyNode* p=AnatomyGraph_FindNode(&previous.anatomyGraph,n->id);
            const AnatomyNode* r=AnatomyGraph_FindNode(&reverse.anatomyGraph,n->id);
            float step=Vec3_Distance(n->center,p->center);maxStep=fmaxf(maxStep,step);
            TEST_ASSERT(step<.25f,"Ninguna rama aparece mediante un salto");
            TEST_ASSERT(r && Vec3_Distance(n->center,r->center)<.001f,"Correspondencia independiente del sentido de transición");
        }
        const HeadSurfaceRecipe* ear=&current.head.anatomy.surface;
        Vector3 earRoot=Vec3_Sub(ear->leftEarCenter,Vec3_Scale(ear->earDirection,ear->earShape.y*.5f));
        TEST_ASSERT(Vec3_Distance(earRoot,current.head.anatomy.landmarks.leftEarBase)<.0001f,"La contracción auricular conserva la inserción, no el centro elevado");
        float step=Vec3_Distance(current.head.anatomy.surface.faceTip,previous.head.anatomy.surface.faceTip);
        maxHeadStep=fmaxf(maxHeadStep,step);
        TEST_ASSERT(step<.10f,"Hocico resuelto continuo");
        TEST_ASSERT(Monster_CopyInto(&previous,&current),"Snapshot anterior");
    }
    /* Cruce fino del antiguo umbral: comprobar el campo visible, no solo nodos. */
    MonsterSDF before=MonsterSDF_Create(),after=MonsterSDF_Create();
    TEST_ASSERT(CreatureMorph_Interpolate(&a,&b,.4999f,&previous),"Antes del umbral");
    TEST_ASSERT(CreatureMorph_Interpolate(&a,&b,.5001f,&current),"Después del umbral");
    TEST_ASSERT(MonsterSDF_Build(&before,&previous,MonsterSDF_DefaultConfig()),"Campo anterior");
    TEST_ASSERT(MonsterSDF_Build(&after,&current,MonsterSDF_DefaultConfig()),"Campo posterior");
    float maxField[2]={0,0},maxSurface=0;
    for(int pass=0;pass<2;++pass) {
        float epsilon=pass?.00001f:.0001f;
        TEST_ASSERT(CreatureMorph_Interpolate(&a,&b,.5f-epsilon,&previous),"Campo fino anterior");
        TEST_ASSERT(CreatureMorph_Interpolate(&a,&b,.5f+epsilon,&current),"Campo fino posterior");
        TEST_ASSERT(MonsterSDF_Build(&before,&previous,MonsterSDF_DefaultConfig()),"Campo anterior fino");
        TEST_ASSERT(MonsterSDF_Build(&after,&current,MonsterSDF_DefaultConfig()),"Campo posterior fino");
        for(int x=-5;x<=5;++x)for(int y=-1;y<=7;++y)for(int z=-25;z<=7;++z) {
            Vector3 p={(float)x*.4f,(float)y*.4f,(float)z*.4f};
            float d0=MonsterSDF_EvaluateDistance(&before,p),d1=MonsterSDF_EvaluateDistance(&after,p);
            float delta=fabsf(d0-d1);maxField[pass]=fmaxf(maxField[pass],delta);
            if(!pass && fminf(fabsf(d0),fabsf(d1))<.15f)maxSurface=fmaxf(maxSurface,delta);
        }
    }
    TEST_ASSERT(maxField[1]<maxField[0]*.2f+.00001f,"El salto del campo converge a cero al reducir el paso temporal");
    TEST_ASSERT(maxSurface<.005f,"La superficie visible no cambia de receta al 50 por ciento");
    printf("[MORPH] 101 edades: paso nodal %.6f cefálico %.6f campo %.6f -> %.6f superficie %.6f\n",maxStep,maxHeadStep,maxField[0],maxField[1],maxSurface);
    MonsterSDF_Free(&before);MonsterSDF_Free(&after);
    Monster_Free(&a);Monster_Free(&b);Monster_Free(&previous);Monster_Free(&current);Monster_Free(&reverse);
}

void run_ager_tests(void) {
    test_resolved_morph_continuity();
    test_monster_ager();
    test_bug_eye_pupil_buried_behind_sclera_during_growth();
    test_ager_cross_species_morph_dog_to_lizard();
}
