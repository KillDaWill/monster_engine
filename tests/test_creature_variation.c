/**
 * @file test_creature_variation.c
 * @brief Suite de pruebas unitarias para el sistema de variación morfológica de criaturas.
 * @author Monster Engine Team
 * @date 2026
 */

#include "test_utils.h"
#include "CreatureVariation.h"
#include "LizardVariations.h"
#include "CreatureRecipes.h"
#include "Creature.h"
#include "MonsterSDF.h"
#include "AttachmentPath.h"
#include "PigmentPattern.h"
#include "Tail.h"
#include "PrimitiveMesh.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "EyeTexture.h"
#include "AxialBody.h"
#include "Limb.h"
#include <string.h>
#include <math.h>

static void TestVariationDeterminism(void) {
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    const CreaturePhenotype* basePheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
    TEST_ASSERT(baseRecipe && basePheno, "Receta y fenotipo base de lagarto disponibles");

    for (size_t i = 0; i < LizardVariations_GetCount(); ++i) {
        uint32_t seed = 48201u + (uint32_t)(i * 73u);
        CreatureVariation v1 = LizardVariations_Get(i, seed);
        CreatureVariation v2 = LizardVariations_Get(i, seed);

        CreatureVariant var1, var2;
        TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &v1, &var1), "Aplicar variación 1");
        TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &v2, &var2), "Aplicar variación 2");

        uint64_t fp1 = CreaturePhenotype_Fingerprint(&var1.phenotype);
        uint64_t fp2 = CreaturePhenotype_Fingerprint(&var2.phenotype);
        TEST_ASSERT(fp1 == fp2, "Misma variación y semilla producen huella fenotípica idéntica");

        /* Diferentes semillas deben producir sutiles diferencias pero permanecer válidas */
        CreatureVariation vDiff = LizardVariations_Get(i, seed + 999u);
        CreatureVariant varDiff;
        TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &vDiff, &varDiff), "Aplicar variación con semilla distinta");
        uint64_t fpDiff = CreaturePhenotype_Fingerprint(&varDiff.phenotype);
        TEST_ASSERT(fp1 != fpDiff, "Distintas semillas producen individuos diferenciados");
    }
}

static void TestBaseImmutability(void) {
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    const CreaturePhenotype* basePheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
    uint64_t initialFp = CreaturePhenotype_Fingerprint(basePheno);

    for (size_t i = 0; i < LizardVariations_GetCount(); ++i) {
        CreatureVariation v = LizardVariations_Get(i, 12345u + (uint32_t)i);
        CreatureVariant var;
        TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &v, &var), "Aplicar variación");
    }

    const CreaturePhenotype* currentPheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
    uint64_t currentFp = CreaturePhenotype_Fingerprint(currentPheno);
    TEST_ASSERT(initialFp == currentFp, "La receta y fenotipo base permanecen inmutables tras aplicar variaciones");
}

static void TestTraitReuse(void) {
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    CreaturePhenotype cloneA = *CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
    CreaturePhenotype cloneB = *CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);

    CreatureVariation varArboreal = CreatureVariation_Create("Reutilizacion Arborea", 777u);
    CreatureTrait arborealTrait = {
        .type = CREATURE_TRAIT_LIMBS_ARBOREAL,
        .strength = 0.90f
    };
    CreatureVariation_AddTrait(&varArboreal, arborealTrait);

    CreatureVariant outA, outB;
    TEST_ASSERT(CreatureVariation_Apply(baseRecipe, &cloneA, &varArboreal, &outA), "Aplicar rasgo a clon A");
    TEST_ASSERT(CreatureVariation_Apply(baseRecipe, &cloneB, &varArboreal, &outB), "Aplicar rasgo a clon B");

    TEST_ASSERT(outA.phenotype.limbs[0].footScale > cloneA.limbs[0].footScale * 1.15f,
                "El rasgo arbóreo escala pies de forma genérica");
    TEST_ASSERT(outA.phenotype.limbs[0].digitSpread > cloneA.limbs[0].digitSpread * 1.20f,
                "El rasgo arbóreo expande dígitos de forma genérica");
    TEST_ASSERT(FLOAT_NEAR(outA.phenotype.limbs[0].footScale, outB.phenotype.limbs[0].footScale),
                "Aplicación consistente e independiente de presets");
}

static void TestExtremeParameters(void) {
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    const CreaturePhenotype* basePheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);

    size_t extremeIndices[] = {
        LIZARD_VARIANT_SAND_BURROWER,
        LIZARD_VARIANT_JEWEL_CHAMELEON,
        LIZARD_VARIANT_STONE_ARMORED
    };

    for (size_t k = 0; k < sizeof(extremeIndices) / sizeof(extremeIndices[0]); ++k) {
        size_t idx = extremeIndices[k];
        CreatureVariation v = LizardVariations_Get(idx, 888u);
        CreatureVariant var;
        TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &v, &var), "Aplicar variante extrema");

        Monster m = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m, &var.recipe, &var.phenotype), "Construir monstruo con morfología extrema");
        TEST_ASSERT(AnatomyGraph_Validate(&m.anatomyGraph), "Grafo anatómico válido bajo parámetros extremos");

        for (size_t n = 0; n < m.anatomyGraph.nodeCount; ++n) {
            const AnatomyNode* node = &m.anatomyGraph.nodes[n];
            TEST_ASSERT(isfinite(node->center.x) && isfinite(node->center.y) && isfinite(node->center.z),
                        "Coordenadas de estaciones anatómicas finitas");
            TEST_ASSERT(node->widthRadius > 0.0001f && node->heightRadius > 0.0001f,
                        "Radios de estaciones estrictamente positivos");
        }

        MonsterSDF sdf = MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf, &m, MonsterSDF_DefaultConfig()), "Construir SDF con morfología extrema");
        TEST_ASSERT(isfinite(sdf.bounds.start.x) && isfinite(sdf.bounds.end.x), "AABB de SDF finita");
        TEST_ASSERT(sdf.bounds.end.x > sdf.bounds.start.x &&
                    sdf.bounds.end.y > sdf.bounds.start.y &&
                    sdf.bounds.end.z > sdf.bounds.start.z, "AABB de SDF con volumen positivo");

        MonsterSDF_Free(&sdf);
        Monster_Free(&m);
    }
}

static void TestAllTenVariants(void) {
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    const CreaturePhenotype* basePheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);

    for (size_t i = 0; i < LizardVariations_GetCount(); ++i) {
        CreatureVariation v = LizardVariations_Get(i, 42000u + (uint32_t)i);
        CreatureVariant var;
        TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &v, &var),
                    "CreatureVariation_Apply exitoso");

        Monster m = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m, &var.recipe, &var.phenotype),
                    "Creature_BuildMonster exitoso para variante");
        TEST_ASSERT(AnatomyGraph_Validate(&m.anatomyGraph),
                    "AnatomyGraph_Validate exitoso");

        MonsterSDF sdf = MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf, &m, MonsterSDF_DefaultConfig()),
                    "MonsterSDF_Build exitoso para variante");
        TEST_ASSERT(sdf.connectorCount > 0, "SDF contiene conectores válidos");

        MonsterSDF_Free(&sdf);
        Monster_Free(&m);
    }
}

static void TestTailArchetypes(void) {
    TailArchetype archetypes[] = {
        TAIL_ARCHETYPE_TAPERED,
        TAIL_ARCHETYPE_WHIP,
        TAIL_ARCHETYPE_HEAVY,
        TAIL_ARCHETYPE_PREHENSILE,
        TAIL_ARCHETYPE_FINNED
    };

    AttachmentSlot dummySlot = {
        .id = 1,
        .role = ATTACHMENT_CAUDAL,
        .hostModuleInstanceId = 1,
        .hostNode = Anatomy_MakeId(1, 1),
        .position = {0, 0, 0},
        .forward = {0, 0, -1},
        .up = {0, 1, 0},
        .side = {1, 0, 0},
        .hostRadii = {0.6f, 0.4f, 0.6f},
        .scale = 1.0f
    };

    for (size_t k = 0; k < sizeof(archetypes) / sizeof(archetypes[0]); ++k) {
        TailPhenotype tp;
        memset(&tp, 0, sizeof(tp));
        tp.archetype = archetypes[k];
        tp.length = 6.0f;
        tp.baseWidth = 0.65f;
        tp.baseHeight = 0.45f;
        tp.tipWidth = 0.05f;
        tp.tipHeight = 0.04f;
        tp.taperCurve = 1.35f;
        tp.curvature = 0.8f;
        tp.development = 1.0f;
        tp.segmentCount = 12;

        AnatomyGraph g;
        AnatomyGraph_Init(&g);
        AnatomyNode host = {
            .id = Anatomy_MakeId(1, 1), .center = {0, 0, 0}, .widthRadius = 0.6f, .heightRadius = 0.4f,
            .colorIndex = 1, .role = ANATOMY_ROLE_AXIAL, .region = ANATOMY_REGION_PELVIS,
            .side = ANATOMY_SIDE_CENTER, .moduleInstanceId = 1, .localNodeId = 1, .development = 1.0f
        };
        TEST_ASSERT(AnatomyGraph_AddNode(&g, host), "Añadir nodo anfitrión");

        TEST_ASSERT(Tail_Resolve(&tp, 20, &dummySlot, &g), "Tail_Resolve para cada arquetipo");
        TEST_ASSERT(g.nodeCount == 13, "Estaciones resueltas corresponden a segmentCount + anfitrión");
        TEST_ASSERT(AnatomyGraph_Validate(&g), "Grafo anatómico de cola válido");

        if (tp.archetype == TAIL_ARCHETYPE_FINNED) {
            /* En aleta caudal distal, heightRadius debe superar widthRadius significativamente */
            const AnatomyNode* midNode = AnatomyGraph_FindModuleNode(&g, 20, 7);
            TEST_ASSERT(midNode && midNode->heightRadius > midNode->widthRadius * 1.5f,
                        "Aleta caudal comprimida lateralmente con expansión dorsoventral");
        } else if (tp.archetype == TAIL_ARCHETYPE_HEAVY) {
            /* En cola pesada, la sección media conserva gran anchura */
            const AnatomyNode* midNode = AnatomyGraph_FindModuleNode(&g, 20, 5);
            TEST_ASSERT(midNode && midNode->widthRadius > tp.baseWidth * 0.70f,
                        "Cola pesada conserva anchura a través del tercio medio");
        }
    }
}

static void TestEyeSizesAndPupilShapes(void) {
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    float testSizes[] = {0.40f, 1.0f, 1.80f};

    for (size_t s = 0; s < 3; ++s) {
        CreaturePhenotype pheno = *CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
        pheno.eyes.size = testSizes[s];

        Monster m = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m, baseRecipe, &pheno), "Construir monstruo para tamaño ocular");
        TEST_ASSERT(m.eyeCount > 0, "Monstruo posee ojos");
        float eyeRad = m.eyes[0].scale.x;
        TEST_ASSERT(eyeRad > 0.001f, "Escala de ojo no nula");

        Monster mBase = Monster_Create();
        CreaturePhenotype phenoBase = *CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
        phenoBase.eyes.size = 1.0f;
        Creature_BuildMonster(&mBase, baseRecipe, &phenoBase);

        float limit=fminf(m.head.anatomy.surface.craniumRadii.y*.55f,m.head.anatomy.surface.craniumRadii.z*.40f);
        TEST_ASSERT(m.eyes[0].scale.y<=limit+1e-5f,"El globo excede el espacio orbital anatómico");
        TEST_ASSERT(Creature_ValidateGeometry(&m)==0,"El globo salió de su órbita");
        if(testSizes[s]<1)TEST_ASSERT(eyeRad<=mBase.eyes[0].scale.x,"El tamaño ocular no es monótono");
        else TEST_ASSERT(eyeRad>=mBase.eyes[0].scale.x,"El tamaño ocular no es monótono");

        Monster_Free(&mBase);
        Monster_Free(&m);
    }

    /* Verificar mallas para cada forma de pupila */
    PupilShape shapes[] = {PUPIL_ROUND, PUPIL_VERTICAL, PUPIL_HORIZONTAL, PUPIL_DIAMOND};
    for (size_t p = 0; p < 4; ++p) {
        CreaturePhenotype pheno = *CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
        pheno.eyes.pupilShape = shapes[p];
        Monster m = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m, baseRecipe, &pheno), "Construir monstruo con forma de pupila");
        TEST_ASSERT(m.eyes[0].pupilShape == shapes[p], "pupilShape propagada a Eye");

        MonsterVisualEye eyeMeshes[2]={0};
        for (size_t e = 0; e < m.eyeCount; ++e) {
            eyeMeshes[e].globe = Mesh_Create();
        }
        TEST_ASSERT(MonsterVisual_UpdateEyes(eyeMeshes, m.eyeCount, &m), "Actualizar mallas oculares");
        TEST_ASSERT(eyeMeshes[0].globe.vertexCount>0,"Un ojo contiene un solo globo tridimensional");
        unsigned char tex[64*64*4];
        TEST_ASSERT(EyeTexture_Generate(&m.eyes[0],64,tex,sizeof(tex)),"Textura ocular válida para cada forma pupilar");

        for (size_t e = 0; e < m.eyeCount; ++e) {
            Mesh_Free(&eyeMeshes[e].globe);
        }
        Monster_Free(&m);
    }
}

static void TestOrnamentArray(void) {
    CreatureVariation v = LizardVariations_VolcanicSpiny(999u);
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    const CreaturePhenotype* basePheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);

    CreatureVariant var;
    TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &v, &var), "Aplicar Volcanic Spiny");
    TEST_ASSERT(var.phenotype.ornamentCount >= 8, "Arreglo de espinas crea al menos 8 ornamentos");

    /* Verificar unicidad de moduleInstanceId */
    for (size_t i = 0; i < var.recipe.bodyPlan.moduleCount; ++i) {
        for (size_t j = i + 1; j < var.recipe.bodyPlan.moduleCount; ++j) {
            TEST_ASSERT(var.recipe.bodyPlan.modules[i].instanceId != var.recipe.bodyPlan.modules[j].instanceId,
                        "Identificadores de instancia modular estrictamente únicos");
        }
    }

    Monster m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&m, &var.recipe, &var.phenotype), "Construir monstruo con arreglo de espinas");
    TEST_ASSERT(AnatomyGraph_Validate(&m.anatomyGraph), "Grafo anatómico con espinas válido");

    size_t spineNodes = 0;
    for (size_t n = 0; n < m.anatomyGraph.nodeCount; ++n) {
        if (m.anatomyGraph.nodes[n].region == ANATOMY_REGION_ORNAMENT) {
            spineNodes++;
        }
    }
    TEST_ASSERT(spineNodes >= 24, "Cada espina en el arreglo genera 3 estaciones anatómicas");
    Monster_Free(&m);
}

static void TestPigmentPatternDeterminism(void) {
    PigmentPattern patterns[] = {
        PIGMENT_PATTERN_SOLID,
        PIGMENT_PATTERN_NOISE,
        PIGMENT_PATTERN_SPOTS,
        PIGMENT_PATTERN_BANDS,
        PIGMENT_PATTERN_STRIPES,
        PIGMENT_PATTERN_BLOTCHES,
        PIGMENT_PATTERN_OCELLI,
        PIGMENT_PATTERN_GRADIENT
    };

    Vector3 testPoint = Vec3_Create(1.25f, -0.34f, 2.78f);
    Vector3 testDir = Vec3_Create(0.0f, 1.0f, 0.0f);
    uint32_t seed = 716253u;

    for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        float s1 = PigmentPattern_Sample(patterns[p], testPoint, 1.5f, 0.7f, testDir, seed);
        float s2 = PigmentPattern_Sample(patterns[p], testPoint, 1.5f, 0.7f, testDir, seed);
        TEST_ASSERT(FLOAT_NEAR(s1, s2), "Muestreo de patrón determinista con mismos parámetros");

        float totalDiff = 0.0f;
        for (int step = 0; step < 6; ++step) {
            Vector3 pt = Vec3_Add(testPoint, Vec3_Create((float)step * 0.55f, (float)step * -0.35f, (float)step * 0.45f));
            float v1 = PigmentPattern_Sample(patterns[p], pt, 1.5f, 0.7f, testDir, seed);
            float v2 = PigmentPattern_Sample(patterns[p], pt, 1.5f, 0.7f, testDir, seed + 100u);
            totalDiff += fabsf(v1 - v2);
        }
        if (patterns[p] != PIGMENT_PATTERN_SOLID && patterns[p] != PIGMENT_PATTERN_GRADIENT) {
            TEST_ASSERT(totalDiff > 0.005f, "Diferentes semillas alteran la muestra procedimental");
        }

        PigmentLayer layer = {
            .pattern = patterns[p],
            .color = Color_FromRGB(200, 50, 80),
            .strength = 0.8f,
            .scale = 1.2f,
            .sharpness = 0.6f,
            .direction = testDir,
            .seed = seed
        };
        Color base = Color_FromRGB(40, 40, 40);
        Color r1 = PigmentPattern_EvaluateLayer(&layer, testPoint, base);
        Color r2 = PigmentPattern_EvaluateLayer(&layer, testPoint, base);
        TEST_ASSERT(r1.r == r2.r && r1.g == r2.g && r1.b == r2.b, "Evaluación de capa cromática determinista");
    }
}

static void TestMorphologyInfrastructure(void) {
    const CreatureRecipe* recipe=CreatureRecipes_Lizard();
    CreaturePhenotype p=recipe->adult;
    Monster base=Monster_Create(),wide=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&base,recipe,&p),"Construir referencia morfológica");
    p.headEnvelope.widthScale=1.5f;
    TEST_ASSERT(Creature_BuildMonster(&wide,recipe,&p),"Construir envolvente ancha");
    TEST_ASSERT(fabsf(wide.head.anatomy.surface.craniumRadii.x/base.head.anatomy.surface.craniumRadii.x-1.5f)<.001f,
        "La escala física no controla la anchura real del cráneo");
    TEST_ASSERT(memcmp(&p.head,&recipe->adult.head,sizeof(p.head))==0,"La envolvente alteró la forma normalizada");
    TEST_ASSERT(Creature_ValidateGeometry(&wide)==0,"La envolvente desplazó las uniones");
    Monster_Free(&base);Monster_Free(&wide);
    CreatureVariation v=CreatureVariation_Create("Selección funcional",42);
    CreatureTrait t={.type=CREATURE_TRAIT_LIMBS_LONG,.strength=1,.limbTarget=CREATURE_LIMBS_HIND};
    CreatureVariation_AddTrait(&v,t);
    CreatureVariant out;
    TEST_ASSERT(CreatureVariation_Apply(recipe,&recipe->adult,&v,&out),"Aplicar rasgo posterior");
    TEST_ASSERT(out.phenotype.limbs[0].length==recipe->adult.limbs[0].length &&
        out.phenotype.limbs[2].length>recipe->adult.limbs[2].length*1.2f,"El selector de miembros no se respeta");
    v=CreatureVariation_Create("Composición múltiple",13);
    CreatureVariation_AddTrait(&v,(CreatureTrait){.type=CREATURE_TRAIT_HORNS,.strength=.5f});
    CreatureVariation_AddTrait(&v,(CreatureTrait){.type=CREATURE_TRAIT_HORNS,.strength=.5f});
    CreatureVariation_AddTrait(&v,(CreatureTrait){.type=CREATURE_TRAIT_DORSAL_SPINES,.strength=.8f,.paramA=9});
    TEST_ASSERT(CreatureVariation_Apply(recipe,&recipe->adult,&v,&out),"Los IDs colisionan al combinar ornamentos");
    TEST_ASSERT(CreatureRecipe_Validate(&out.recipe,&out.phenotype),"Receta combinada inválida");
    CreatureVariant before=out;
    v=CreatureVariation_Create("Capacidad imposible",13);
    CreatureVariation_AddTrait(&v,(CreatureTrait){.type=CREATURE_TRAIT_DORSAL_SAIL,.strength=1,.paramA=100});
    TEST_ASSERT(!CreatureVariation_Apply(recipe,&recipe->adult,&v,&out),"Solicitud imposible silenciada");
    TEST_ASSERT(memcmp(&out,&before,sizeof(out))==0,"El fallo dejó una variante parcialmente modificada");

    AnatomyGraph graph;TEST_ASSERT(Creature_ResolveAnatomy(recipe,&recipe->adult,&graph),"Resolver ruta");
    AttachmentPath path=AttachmentPath_FromAxialDorsal(&graph,recipe->bodyPlan.root.moduleInstanceId);
    CreatureRecipe a=*recipe,b=*recipe;CreaturePhenotype pa=recipe->adult,pb=pa;
    OrnamentArray array={.ornament={.archetype=ORNAMENT_ARCHETYPE_SPINE,.length=.5f,.baseRadius=.1f,.tipRadius=.02f,.development=1},
        .count=3,.pathStart=.1f,.pathEnd=.3f,.sizeStart=1,.sizePeak=1,.sizeEnd=1};
    TEST_ASSERT(OrnamentArray_Instantiate(&array,&path,0,1,recipe->bodyPlan.root.moduleInstanceId,&a,&pa),"Primera ruta");
    array.pathStart=.6f;array.pathEnd=.9f;array.lateralOffset=.4f;
    TEST_ASSERT(OrnamentArray_Instantiate(&array,&path,0,1,recipe->bodyPlan.root.moduleInstanceId,&b,&pb),"Segunda ruta");
    AnatomyGraph ga,gb;
    TEST_ASSERT(Creature_ResolveAnatomy(&a,&pa,&ga)&&Creature_ResolveAnatomy(&b,&pb,&gb),"Resolver rutas parametrizadas");
    uint32_t module=a.bodyPlan.modules[recipe->bodyPlan.moduleCount].instanceId;
    const AnatomyNode* na=AnatomyGraph_FindModuleNode(&ga,module,1),*nb=AnatomyGraph_FindModuleNode(&gb,module,1);
    TEST_ASSERT(na&&nb&&Vec3_Distance(na->center,nb->center)>1 && fabsf(nb->center.x)>.1f,
        "Los parámetros de ruta no cambian la posición real");
}

static void TestDiversityGeometry(void) {
    const CreatureRecipe* recipe=CreatureRecipes_Lizard();
    Monster base=Monster_Create();TEST_ASSERT(Creature_BuildMonster(&base,recipe,&recipe->adult),"Referencia de firma");
    MorphologicalSignature reference=Creature_MorphologicalSignature(&base);Monster_Free(&base);
    for(size_t i=0;i<10;++i) {
        CreatureVariation variation=LizardVariations_Get(i,54321u+(uint32_t)i*1337u);CreatureVariant variant;
        TEST_ASSERT(CreatureVariation_Apply(recipe,&recipe->adult,&variation,&variant),"Aplicación de diversidad");
        Monster m=Monster_Create();TEST_ASSERT(Creature_BuildMonster(&m,&variant.recipe,&variant.phenotype),"Construcción de diversidad");
        TEST_ASSERT(Creature_ValidateGeometry(&m)==0,"Uniones o asientos incoherentes");
        MorphologicalSignature signature=Creature_MorphologicalSignature(&m);
        const float values[]={signature.bodyLengthWidthRatio,signature.bodyHeightWidthRatio,signature.headBodyScale,
            signature.headLengthWidthRatio,signature.hindForeRatio,signature.footBodyRatio,signature.tailBodyLengthRatio,
            signature.tailBaseBodyRatio,signature.eyeHeadRatio,signature.forelimbThicknessBodyRatio};
        const float baseline[]={reference.bodyLengthWidthRatio,reference.bodyHeightWidthRatio,reference.headBodyScale,
            reference.headLengthWidthRatio,reference.hindForeRatio,reference.footBodyRatio,reference.tailBodyLengthRatio,
            reference.tailBaseBodyRatio,reference.eyeHeadRatio,reference.forelimbThicknessBodyRatio};
        unsigned changed=signature.maxOrnamentHeightBodyRatio>.25f?1:0;
        for(size_t j=0;j<sizeof(values)/sizeof(values[0]);++j)if(fabsf(values[j]/baseline[j]-1)>.20f)++changed;
        TEST_ASSERT(changed>=3,"La variante regresó a una recoloración");
        MonsterSDF sdf=MonsterSDF_Create();TEST_ASSERT(MonsterSDF_Build(&sdf,&m,MonsterSDF_DefaultConfig()),"SDF diversidad");
        MonsterSDFHeadField context;SDFField head=MonsterSDF_GetHeadField(&sdf,0,&context);
        Vector3 origin=m.bodyParts[0].positionRender;
        Vector3 hinge=Vec3_Add(origin,m.head.anatomy.landmarks.leftJawHinge);
        float seam=head.evaluateDistance(head.context,hinge);
        TEST_ASSERT(isfinite(seam)&&seam<m.head.anatomy.surface.craniumRadii.y*.5f,"Bisagra separada del tejido craneal");
        MonsterSDF_Free(&sdf);Monster_Free(&m);
    }
}

static void TestDistinctMuzzleProfiles(void) {
    HeadPhenotype p=HeadPhenotype_LizardPreset();
    HeadAnatomy blunt,longFace;
    p.muzzleForm=HEAD_MUZZLE_BLUNT;
    TEST_ASSERT(HeadAnatomy_Resolve(&p,0,Vec3_Create(1,1,1),&blunt),"Resolver rostro romo");
    p.muzzleForm=HEAD_MUZZLE_LONG;
    TEST_ASSERT(HeadAnatomy_Resolve(&p,0,Vec3_Create(1,1,1),&longFace),"Resolver rostro largo");
    TEST_ASSERT(blunt.surface.faceTipRadii.x/blunt.surface.faceRootRadii.x > .65f &&
                longFace.surface.faceTipRadii.x/longFace.surface.faceRootRadii.x < .4f,
                "Los rostros romo y largo no comparten el mismo perfil escalado");
    TEST_ASSERT(longFace.surface.faceTip.z-longFace.surface.faceRoot.z >
                1.5f*(blunt.surface.faceTip.z-blunt.surface.faceRoot.z),"Longitud facial diferenciada");
}

static void TestSandBurrowerReferenceShape(void) {
    const CreatureRecipe* recipe = CreatureRecipes_Lizard();
    const uint32_t seeds[] = {61006u, 888u, 17u};
    for (size_t i=0; i<sizeof(seeds)/sizeof(seeds[0]); ++i) {
        CreatureVariation variation = LizardVariations_SandBurrower(seeds[i]);
        CreatureVariant variant;
        TEST_ASSERT(CreatureVariation_Apply(recipe, &recipe->adult, &variation, &variant), "Resolver Sand Burrower alargado");
        Monster m = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m, &variant.recipe, &variant.phenotype), "Construir Sand Burrower");
        const AxialPhenotype* a = &variant.phenotype.axial;
        TEST_ASSERT(a->trunkLength/a->thoraxWidth > 3.6f && a->trunkLength/a->thoraxWidth < 4.5f,
                    "Tronco alargado sin disco abdominal");
        const AnatomyNode* thorax = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_THORAX_ANTERIOR);
        TEST_ASSERT(thorax->heightRadius/thorax->widthRadius > .65f && thorax->heightRadius/thorax->widthRadius < .95f,
                    "Sección ovalada con profundidad real");
        TEST_ASSERT(variant.phenotype.tails[0].length/a->trunkLength > 1.05f &&
                    variant.phenotype.tails[0].length/a->trunkLength < 1.22f,
                    "Cola larga y afinada proporcional al tronco");
        const TailPhenotype* tail = &variant.phenotype.tails[0];
        float arc=0;
        const AnatomyNode* previous=AnatomyGraph_FindModuleNode(&m.anatomyGraph,20,1);
        Vector3 tailRoot=previous->center;
        for (unsigned n=2; n<=tail->segmentCount; ++n) {
            const AnatomyNode* next=AnatomyGraph_FindModuleNode(&m.anatomyGraph,20,n);
            TEST_ASSERT(next != NULL,"Estación caudal resuelta");
            arc += Vec3_Distance(previous->center,next->center);
            previous=next;
        }
        TEST_ASSERT(arc > tail->length*.98f && arc < tail->length*1.01f,
                    "La curvatura conserva longitud de arco");
        TEST_ASSERT(fabsf(previous->center.x-tailRoot.x) > tail->length*.25f &&
                    Vec3_Distance(previous->center,tailRoot) < arc*.85f,
                    "La cola curva distalmente sin simular una cola más larga");
        TEST_ASSERT(m.eyes[0].scale.y/m.eyes[0].scale.x < .75f,
                    "Ojo visible ovalado y no circular");
        const HeadSurfaceRecipe* h = &m.head.anatomy.surface;
        TEST_ASSERT(h->orbitRadii.y/h->orbitRadii.z < .78f,
                    "La abertura orbital acompaña el globo ovalado");
        TEST_ASSERT(h->faceTip.z-h->faceRoot.z < h->craniumRadii.z*.75f,
                    "Rostro compacto de la referencia");
        const AnatomyNode* neck = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_NECK);
        const AnatomyNode* shoulder = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_PECTORAL);
        TEST_ASSERT(neck->widthRadius/shoulder->widthRadius > .93f &&
                    neck->widthRadius/h->craniumRadii.x > .93f &&
                    neck->heightRadius/shoulder->heightRadius > .90f,
                    "Cuello continuo con cabeza y hombros sin estrechamiento cervical");
        TEST_ASSERT(h->craniumRadii.y/h->craniumRadii.x > .55f && h->craniumRadii.x < thorax->widthRadius,
                    "Cabeza con volumen más estrecha que el tronco");
        for (size_t l=0; l<variant.phenotype.limbCount; ++l) {
            const LimbPhenotype* limb = &variant.phenotype.limbs[l];
            TEST_ASSERT(limb->digitCount == 5 && limb->footWidthScale < .85f && limb->digitThicknessScale < 1,
                        "Cinco dedos finos y autopodio estrecho");
            TEST_ASSERT(limb->thickness/limb->length < .14f, "Miembros gráciles sin hipertrofia excavadora");
        }
        for (uint32_t module=12; module<=13; ++module) {
            const AnatomyNode* hip = AnatomyGraph_FindModuleNode(&m.anatomyGraph,module,LIMB_NODE_ROOT);
            const AnatomyNode* knee = AnatomyGraph_FindModuleNode(&m.anatomyGraph,module,LIMB_NODE_MIDDLE);
            const AnatomyNode* ankle = AnatomyGraph_FindModuleNode(&m.anatomyGraph,module,LIMB_NODE_AUTOPOD);
            const LimbPhenotype* hind = &variant.phenotype.limbs[module-10];
            const AnatomyNode* toe = AnatomyGraph_FindModuleNode(&m.anatomyGraph,module,
                Limb_DigitLocalId(2,hind->digitPhalanges[2]+1));
            TEST_ASSERT(knee->center.z < hip->center.z && ankle->center.z < knee->center.z,
                        "Rodilla y tobillo posteriores dirigidos hacia la cola");
            TEST_ASSERT(toe->center.z < ankle->center.z && fabsf(toe->center.x) > fabsf(ankle->center.x),
                        "Dedos posteriores dirigidos hacia atrás y fuera");
        }
        TEST_ASSERT(variant.phenotype.limbs[0].footYaw == 0 && variant.phenotype.limbs[1].proximalSweep == 0,
                    "La postura posterior no modifica los miembros anteriores");
        CreaturePhenotype neutral=variant.phenotype;
        neutral.head.eyeCompression=0;
        for (size_t l=0; l<neutral.limbCount; ++l) {
            neutral.limbs[l].proximalSweep=0; neutral.limbs[l].distalSweep=0; neutral.limbs[l].footYaw=0;
        }
        CreaturePhenotype blend=CreaturePhenotype_Interpolate(&neutral,&variant.phenotype,.5f);
        TEST_ASSERT(FLOAT_NEAR(blend.head.eyeCompression,variant.phenotype.head.eyeCompression*.5f),
                    "Apertura orbital interpolable");
        TEST_ASSERT(FLOAT_NEAR(blend.limbs[2].footYaw,variant.phenotype.limbs[2].footYaw*.5f),
                    "El giro del autopodio se interpola de forma continua");
        SDFMesherConfig cfg = SDFMesher_DefaultConfig();
        cfg.voxelSize=.11f; cfg.maxCells=350000; cfg.maxResolution=256;
        MonsterVisual visual = MonsterVisual_Create(cfg);
        TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "Generar malla de producción");
        size_t components=0, largest=0, second=0;
        TEST_ASSERT(Mesh_ComponentStatistics(&visual.mesh, &components, &largest, &second) && components==1,
                    "Cuerpo, miembros, dedos, cola y cabeza forman una componente sin islotes maxilares");
        MeshValidationResult validation=Mesh_Validate(&visual.mesh);
        TEST_ASSERT(validation.valid && validation.watertight && validation.manifold,
                    "Malla cerrada y manifold del nuevo Sand Burrower");
        MonsterVisual_Free(&visual);
        Monster_Free(&m);
    }
}

static void TestBroadTriangularHeadVolume(void) {
    const CreatureRecipe* base = CreatureRecipes_Lizard();
    const uint32_t seeds[] = {55658u, 17u, 777u, 48201u};
    for (size_t i=0; i<sizeof(seeds)/sizeof(seeds[0]); ++i) {
        CreatureVariation variation = LizardVariations_DesertHorned(seeds[i]);
        CreatureVariant variant;
        TEST_ASSERT(CreatureVariation_Apply(base, &base->adult, &variation, &variant), "Resolver cabeza triangular");
        Monster monster = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&monster, &variant.recipe, &variant.phenotype), "Construir cabeza triangular");
        const HeadSurfaceRecipe* h = &monster.head.anatomy.surface;
        float aspect = h->craniumRadii.y / h->craniumRadii.x;
        TEST_ASSERT(aspect > .42f && aspect < .65f, "El cráneo ancho conserva volumen vertical");
        TEST_ASSERT(h->faceRootRadii.x < h->craniumRadii.x*.68f &&
                    h->faceMidRadii.x < h->faceRootRadii.x*.70f &&
                    h->faceTipRadii.x < h->faceMidRadii.x*.70f,
                    "La cara se estrecha desde el cráneo hasta la nariz");
        TEST_ASSERT(monster.head.anatomy.landmarks.leftOrbit.x < h->craniumRadii.x*.73f,
                    "Órbitas integradas hacia el interior del cráneo");
        TEST_ASSERT(monster.eyes[0].offset.x + monster.eyes[0].scale.y < h->craniumRadii.x*.91f,
                    "El globo ocular no alcanza el borde lateral craneal");
        TEST_ASSERT(h->leftCheekCenter.x + h->cheekRadii.x < h->craniumRadii.x*.65f,
                    "Las mejillas sostienen el cráneo sin lóbulos laterales");
        MonsterSDF sdf = MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf, &monster, MonsterSDF_DefaultConfig()), "Construir barrido craneal");
        const MonsterSDFMouth* mouth = &sdf.mouths[0];
        TEST_ASSERT(mouth->sweptSkull, "La silueta conserva el barrido craneal");
        TEST_ASSERT(mouth->headStations[3].center.z < mouth->craniumCenterLocal.z,
                    "El máximo transversal está en el cráneo posterior");
        for (int j=0; j<3; ++j)
            TEST_ASSERT(mouth->headStations[j].width < mouth->headStations[j+1].width,
                        "El barrido facial se ensancha continuamente hacia atrás");
        size_t cranialSupports = 0;
        for (size_t j=0; j<sdf.connectorCount; ++j) {
            const MonsterSDFConnector* c = &sdf.connectors[j];
            const AnatomyNode* a = AnatomyGraph_FindNode(&monster.anatomyGraph, c->fromId);
            const AnatomyNode* b = AnatomyGraph_FindNode(&monster.anatomyGraph, c->toId);
            if (c->kind != BODY_CONNECTION_SUPPORT ||
                (a->region != ANATOMY_REGION_HEAD && b->region != ANATOMY_REGION_HEAD)) continue;
            const AnatomyNode* root = a->region == ANATOMY_REGION_ORNAMENT ? a : b;
            TEST_ASSERT(c->widthA <= root->widthRadius*1.01f && c->widthB <= root->widthRadius*1.01f,
                        "El soporte craneal usa la raíz del cuerno y no duplica el cráneo");
            TEST_ASSERT(MonsterSDF_EvaluateDebug(&sdf, root->center, MONSTER_HEAD_DEBUG_UPPER_HEAD).distance < root->widthRadius,
                        "La raíz de cada cuerno solapa el cráneo resuelto");
            cranialSupports++;
        }
        TEST_ASSERT(cranialSupports == 8, "La corona conserva sus ocho anclajes craneales");
        MonsterSDF_Free(&sdf);
        Monster_Free(&monster);
    }
}

static void TestDesertHornedMorphology(void) {
    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    const CreaturePhenotype* basePheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
    TEST_ASSERT(baseRecipe && basePheno, "Receta y fenotipo base disponibles");

    /* Verificar con la semilla canónica 55658 */
    CreatureVariation v = LizardVariations_DesertHorned(55658u);
    CreatureVariant var;
    TEST_ASSERT(CreatureVariation_Apply(baseRecipe, basePheno, &v, &var), "Aplicar Desert Horned 55658");

    Monster m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&m, &var.recipe, &var.phenotype), "Construir monstruo Desert Horned");
    TEST_ASSERT(AnatomyGraph_Validate(&m.anatomyGraph), "Grafo anatómico válido");

    /* 1. Proporciones dorsoventrales: estaciones del torso en [0.25, 0.40], cinturas en [0.35, 0.55] */
    const AnatomyNode* pectoral = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_PECTORAL);
    const AnatomyNode* thoraxA = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* thoraxP = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* abdomen = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* pelvis = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 1, AXIAL_NODE_PELVIS);

    TEST_ASSERT(pectoral && thoraxA && thoraxP && abdomen && pelvis, "Estaciones axiales del tronco encontradas");

    const AnatomyNode* torsoStations[] = { thoraxA, thoraxP, abdomen };
    for (size_t s = 0; s < 3; ++s) {
        float aspect = torsoStations[s]->heightRadius / torsoStations[s]->widthRadius;
        TEST_ASSERT(aspect >= 0.25f && aspect <= 0.40f,
                    "Relación de aspecto H/W de estación torso dentro de [0.25, 0.40]");
        TEST_ASSERT(torsoStations[s]->heightRadius >= 0.35f,
                    "Radio de altura de estación torso volumétrico (>= 0.35)");
    }

    const AnatomyNode* girdleStations[] = { pectoral, pelvis };
    for (size_t s = 0; s < 2; ++s) {
        float aspect = girdleStations[s]->heightRadius / girdleStations[s]->widthRadius;
        TEST_ASSERT(aspect >= 0.35f && aspect <= 0.55f,
                    "Relación de aspecto H/W de cintura escapular/pélvica dentro de [0.35, 0.55]");
        TEST_ASSERT(girdleStations[s]->heightRadius >= 0.35f,
                    "Radio de altura de cintura volumétrico (>= 0.35)");
    }

    /* 2. Continuidad de raíz de cola con la pelvis anfitriona (rootMatchStrength = 0.95) */
    const AnatomyNode* tailRoot = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 20, 1);
    TEST_ASSERT(tailRoot != NULL, "Estación raíz de cola encontrada");
    TEST_ASSERT(fabsf(tailRoot->widthRadius - pelvis->widthRadius) < 0.12f,
                "Anchura raíz de cola coincide con pelvis anfitriona dentro de tolerancia");
    TEST_ASSERT(fabsf(tailRoot->heightRadius - pelvis->heightRadius) < 0.05f,
                "Altura raíz de cola coincide con pelvis anfitriona dentro de tolerancia");

    /* 3. Conexiones axiales a miembros son BODY_CONNECTION_SUPPORT */
    size_t limbAttachments = 0;
    for (size_t c = 0; c < m.anatomyGraph.connectionCount; ++c) {
        const BodyConnection* conn = &m.anatomyGraph.connections[c];
        const AnatomyNode* from = AnatomyGraph_FindNode(&m.anatomyGraph, conn->fromId);
        const AnatomyNode* to = AnatomyGraph_FindNode(&m.anatomyGraph, conn->toId);
        if (from && to && from->role == ANATOMY_ROLE_AXIAL && to->role == ANATOMY_ROLE_JOINT && to->localNodeId == LIMB_NODE_ROOT) {
            TEST_ASSERT(conn->kind == BODY_CONNECTION_SUPPORT,
                        "Conexión axial a raíz de miembro utiliza BODY_CONNECTION_SUPPORT");
            limbAttachments++;
        }
    }
    TEST_ASSERT(limbAttachments == 4, "Las 4 extremidades conectan al eje axial con soporte sin volumen redundante");

    /* 4. Construcción de SDF y validación de conectores */
    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &m, MonsterSDF_DefaultConfig()), "Construir SDF Desert Horned");
    TEST_ASSERT(sdf.connectorCount > 0, "SDF Desert Horned contiene conectores");
    MonsterSDF_Free(&sdf);

    Monster_Free(&m);
}

static void TestOrnamentFields(void) {
    const CreatureRecipe* base = CreatureRecipes_Lizard();
    for (unsigned target = 0; target < 2; ++target) {
        CreatureVariation variation = CreatureVariation_Create("Campo reutilizable", 731);
        CreatureTrait trait = {0};
        trait.type = CREATURE_TRAIT_ORNAMENT_FIELD;
        trait.strength = 1;
        trait.hostKind = target ? CREATURE_MODULE_TAIL : CREATURE_MODULE_AXIAL;
        trait.field = (OrnamentField){
            .row = {.ornament = {.archetype=ORNAMENT_ARCHETYPE_SPINE, .length=.3f,
                .baseRadius=.12f, .tipRadius=.015f, .development=1},
                .count=4, .pathStart=.2f, .pathEnd=.8f,
                .sizeStart=.7f, .sizePeak=1, .sizeEnd=.4f},
            .rows=3, .angularSpread=2.2f, .stagger=.5f, .sizeJitter=.2f};
        TEST_ASSERT(CreatureVariation_AddTrait(&variation, trait), "Añadir campo genérico");
        CreatureVariant a, b;
        TEST_ASSERT(CreatureVariation_Apply(base, &base->adult, &variation, &a), "Campo axial y caudal reutilizable");
        TEST_ASSERT(CreatureVariation_Apply(base, &base->adult, &variation, &b), "Repetir campo");
        TEST_ASSERT(memcmp(&a, &b, sizeof(a)) == 0, "Campo determinista completo");
        TEST_ASSERT(a.phenotype.ornamentCount == 12, "Campo de tres hileras por cuatro estaciones");
        AnatomyGraph graph;
        TEST_ASSERT(Creature_ResolveAnatomy(&a.recipe, &a.phenotype, &graph), "Resolver campo");
        for (size_t j=base->bodyPlan.moduleCount; j<a.recipe.bodyPlan.moduleCount; ++j) {
            const CreatureModuleInstance* module = &a.recipe.bodyPlan.modules[j];
            const AnatomyNode* root = AnatomyGraph_FindModuleNode(&graph, module->instanceId, 1);
            const AnatomyNode* tip = AnatomyGraph_FindModuleNode(&graph, module->instanceId, 3);
            TEST_ASSERT(root && tip && Vec3_Distance(root->center,tip->center) > .05f, "Volumen de ornamento resuelto");
            const AnatomyNode* host = AnatomyGraph_FindNode(&graph,module->pathNodeA);
            TEST_ASSERT(host && (target ? host->region == ANATOMY_REGION_TAIL : host->moduleInstanceId == base->bodyPlan.root.moduleInstanceId), "Anfitrión semántico");
            bool support = false;
            for (size_t k=0; k<graph.connectionCount; ++k)
                if (graph.connections[k].toId == root->id)
                    support = graph.connections[k].kind == BODY_CONNECTION_SUPPORT;
            TEST_ASSERT(support, "Anclaje de ornamento no añade volumen desde el centro del anfitrión");
        }
        variation.seed++;
        TEST_ASSERT(CreatureVariation_Apply(base, &base->adult, &variation, &b), "Otra semilla de campo");
        TEST_ASSERT(CreaturePhenotype_Fingerprint(&a.phenotype) != CreaturePhenotype_Fingerprint(&b.phenotype), "Semilla modifica tamaño del campo");
        b = a;
        variation.traits[0].field.rows = CREATURE_MAX_ORNAMENTS;
        TEST_ASSERT(!CreatureVariation_Apply(base, &base->adult, &variation, &b), "Rechazo de capacidad del campo");
        TEST_ASSERT(memcmp(&a,&b,sizeof(a)) == 0, "Fallo no publica resultado parcial");
        variation.traits[0] = trait;
        variation.traits[0].field.angularSpread = NAN;
        TEST_ASSERT(!CreatureVariation_Apply(base, &base->adult, &variation, &b), "Rechazo de apertura no finita");
    }
}

static void TestHeadCompositionProfiles(void) {
    const CreatureRecipe* recipe = CreatureRecipes_Lizard();
    CreatureVariation shield = CreatureVariation_Create("Cabeza escudo", 17);
    CreatureVariation narrow = CreatureVariation_Create("Cabeza estrecha", 17);
    CreatureVariation_AddTrait(&shield, (CreatureTrait){.type=CREATURE_TRAIT_HEAD_FORM,
        .strength=1, .paramA=HEAD_CRANIAL_SHIELD, .paramB=HEAD_MUZZLE_WEDGE});
    CreatureVariation_AddTrait(&shield, (CreatureTrait){.type=CREATURE_TRAIT_EYE_LAYOUT,
        .strength=1, .paramA=HEAD_EYES_LATERAL});
    CreatureVariation_AddTrait(&narrow, (CreatureTrait){.type=CREATURE_TRAIT_HEAD_FORM,
        .strength=1, .paramA=HEAD_CRANIAL_NARROW, .paramB=HEAD_MUZZLE_LONG});
    CreatureVariation_AddTrait(&narrow, (CreatureTrait){.type=CREATURE_TRAIT_EYE_LAYOUT,
        .strength=1, .paramA=HEAD_EYES_FORWARD});

    CreatureVariant shieldVariant, narrowVariant;
    TEST_ASSERT(CreatureVariation_Apply(recipe, &recipe->adult, &shield, &shieldVariant),
                "Aplicar composición cefálica de escudo");
    TEST_ASSERT(CreatureVariation_Apply(recipe, &recipe->adult, &narrow, &narrowVariant),
                "Aplicar composición cefálica estrecha");
    Monster shieldMonster = Monster_Create(), narrowMonster = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&shieldMonster, &shieldVariant.recipe, &shieldVariant.phenotype),
                "Construir cabeza escudo");
    TEST_ASSERT(Creature_BuildMonster(&narrowMonster, &narrowVariant.recipe, &narrowVariant.phenotype),
                "Construir cabeza estrecha");
    const HeadAnatomy* a = &shieldMonster.head.anatomy;
    const HeadAnatomy* b = &narrowMonster.head.anatomy;
    TEST_ASSERT(a->surface.craniumRadii.x > b->surface.craniumRadii.x * 1.20f,
                "El perfil craneal cambia la anchura posterior");
    TEST_ASSERT((b->landmarks.muzzleTip.z - b->landmarks.muzzleRoot.z) >
                (a->landmarks.muzzleTip.z - a->landmarks.muzzleRoot.z) * 1.15f,
                "El perfil de hocico cambia la longitud real");
    TEST_ASSERT(fabsf(a->landmarks.leftOrbit.x - b->landmarks.leftOrbit.x) > .01f ||
                fabsf(a->landmarks.leftOrbit.z - b->landmarks.leftOrbit.z) > .01f,
                "La disposición ocular cambia la posición orbital");
    Monster_Free(&shieldMonster);
    Monster_Free(&narrowMonster);
}

void run_creature_variation_tests(void) {
    TestHeadCompositionProfiles();
    TestOrnamentFields();
    TestMorphologyInfrastructure();
    TestDiversityGeometry();
    TestVariationDeterminism();
    TestBaseImmutability();
    TestTraitReuse();
    TestTailArchetypes();
    TestEyeSizesAndPupilShapes();
    TestOrnamentArray();
    TestPigmentPatternDeterminism();
    TestExtremeParameters();
    TestAllTenVariants();
    TestDistinctMuzzleProfiles();
    TestSandBurrowerReferenceShape();
    TestBroadTriangularHeadVolume();
    TestDesertHornedMorphology();
    printf("[PASS] variaciones de criatura: determinismo, inmutabilidad, arquetipos de cola, ojos, ornamentos, pigmentos y 10 variantes\n");
}
