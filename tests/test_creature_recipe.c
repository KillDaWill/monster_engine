/**
 * @file test_creature_recipe.c
 * @brief Pruebas de recetas de criaturas, desacoplamiento arquitectónico y extensibilidad modular.
 * @author Monster Engine Team
 * @date 2026
 */

#include "Creature.h"
#include "CreatureRig.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "Limb.h"
#include "Ornament.h"
#include "AxialBody.h"
#include "AxialBodySprawlingTetrapod.h"
#include "HeadModule.h"
#include "test_utils.h"
#include <math.h>

void run_creature_recipe_tests(void) {
    const CreatureRecipe* recipe = CreatureRecipes_Lizard();
    TEST_ASSERT(recipe && recipe->id == 1 && CreatureRecipes_Find(recipe->id) == recipe, "Identidad de receta inestable");

    /* 1. Conteo y composición canónica del lagarto */
    unsigned counts[5] = {0};
    for (size_t i = 0; i < recipe->bodyPlan.moduleCount; ++i) ++counts[recipe->bodyPlan.modules[i].kind];
    TEST_ASSERT(counts[CREATURE_MODULE_AXIAL] == 1 && counts[CREATURE_MODULE_HEAD] == 1 &&
                counts[CREATURE_MODULE_LIMB] == 4 && counts[CREATURE_MODULE_TAIL] == 1, "Composición inesperada");
    TEST_ASSERT(!Anatomy_MakeId(0, 1) && !Anatomy_MakeId(65535, 1) && !Anatomy_MakeId(1, 0), "IDs fuera de rango aceptados");

    /* 2. Determinismo y metadatos semánticos */
    AnatomyGraph a, b;
    TEST_ASSERT(Creature_ResolveAnatomy(recipe, &recipe->adult, &a) &&
                Creature_ResolveAnatomy(recipe, &recipe->adult, &b), "Resolución fallida");
    TEST_ASSERT(AnatomyGraph_Fingerprint(&a) == AnatomyGraph_Fingerprint(&b), "Resolución no determinista");
    for (size_t i = 0; i < a.nodeCount; ++i) {
        const AnatomyNode* n = &a.nodes[i];
        TEST_ASSERT(n->id == Anatomy_MakeId(n->moduleInstanceId, n->localNodeId) &&
                    n->region != ANATOMY_REGION_UNKNOWN, "Metadatos ausentes");
    }

    /* 3. Compatibilidad topológica entre etapas ontogenéticas */
    for (unsigned stage = 0; stage < 4; ++stage) {
        TEST_ASSERT(Creature_ResolveAnatomy(recipe, CreatureRecipe_GetStage(recipe, (CreatureStage)stage), &b) &&
                    AnatomyGraph_Validate(&b) &&
                    AnatomyGraph_TopologyCompatible(&a, &b), "Etapa incompatible");
    }
    b = a; b.connections[0].toId = a.nodes[a.nodeCount - 1].id;
    TEST_ASSERT(!AnatomyGraph_TopologyCompatible(&a, &b), "La comparación ignora cambios de arista");

    /* 4. Ager e interpolación continua con Ager */
    Monster first = Monster_Create(), last = Monster_Create(), result = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&first, recipe, &recipe->larva) &&
                Creature_BuildMonster(&last, recipe, &recipe->adult), "Extremos inválidos");
    const float ages[] = {0, 0.10f, 0.25f, 0.50f, 0.75f, 1.0f};
    for (size_t i = 0; i < 6; ++i) {
        MonsterAger_Interpolate(&first, &last, ages[i], &result);
        TEST_ASSERT(result.hasCreaturePhenotype && result.recipeId == recipe->id &&
                    AnatomyGraph_Validate(&result.anatomyGraph) &&
                    AnatomyGraph_TopologyCompatible(&a, &result.anatomyGraph) &&
                    HeadAnatomy_Validate(&result.head.anatomy) == HEAD_VALID, "Ager inválido");
        TEST_ASSERT(result.surfaceMapping.tagCount == result.anatomyGraph.nodeCount, "Mapa incompleto");
    }

    /* 5. Monster_CopyInto preserva identidad y morfología */
    Monster clone = Monster_Clone(&result), copy = Monster_Create();
    TEST_ASSERT(Monster_CopyInto(&copy, &clone) && copy.recipeId == recipe->id && copy.hasGrowthAge && copy.growthAge == 1 &&
                AnatomyGraph_Fingerprint(&copy.anatomyGraph) == AnatomyGraph_Fingerprint(&result.anatomyGraph) &&
                copy.phenotype.axial.totalScale == result.phenotype.axial.totalScale, "Copia pierde estado");

    /* 6. Rig canónico de lagarto: 4 miembros, 1 cola */
    Rig rig;
    TEST_ASSERT(CreatureRig_Build(&last, &rig) && rig.limbCount == 4 && Skeleton_Validate(&rig.skeleton), "Rig inválido");
    TEST_ASSERT(rig.tailCount == 1 && rig.tails[0].jointCount == 4, "Rig de cola canónica inválido");
    for (size_t i = 0; i < rig.limbCount; ++i) {
        TEST_ASSERT(rig.limbs[i].walking && rig.limbs[i].role == (i < 2 ? LIMB_FORE : LIMB_HIND) &&
                    rig.limbs[i].side == (i % 2 ? LIMB_RIGHT : LIMB_LEFT), "Semántica locomotora incorrecta");
    }
    Monster_Free(&clone); Monster_Free(&copy); Monster_Free(&first); Monster_Free(&last); Monster_Free(&result);

    /* 7. Test: Selección explícita de AxialArchetype y rechazo de no implementados */
    TEST_ASSERT(recipe->adult.axial.archetype == AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD, "Arquetipo axial no seleccionado explícitamente");
    CreatureRecipe unsupportedAxial = *recipe;
    unsupportedAxial.adult.axial.archetype = AXIAL_ARCHETYPE_SERPENTINE;
    TEST_ASSERT(!CreatureRecipe_Validate(&unsupportedAxial, &unsupportedAxial.adult), "Arquetipo axial no implementado fue aceptado");
    AttachmentSlotSet dummySlots = {0};
    AnatomyGraph dummyGraph; AnatomyGraph_Init(&dummyGraph);
    TEST_ASSERT(!AxialBody_Resolve(&unsupportedAxial.adult.axial, 1, &dummyGraph, &dummySlots), "AxialBody_Resolve aceptó arquetipo no soportado");

    /* 8. Test: Garantía de interpolación en extremos (t=0 -> A exacto, t=1 -> B exacto) y rasgos categóricos */
    CreaturePhenotype phenoA = recipe->adult;
    CreaturePhenotype phenoB = recipe->adult;
    phenoA.eyes.pupilShape = PUPIL_VERTICAL;
    phenoA.eyes.size = 0.50f;
    phenoA.dorsalColor = Color_FromRGB(10, 20, 30);
    phenoB.eyes.pupilShape = PUPIL_HORIZONTAL;
    phenoB.eyes.size = 1.50f;
    phenoB.dorsalColor = Color_FromRGB(200, 100, 50);

    CreaturePhenotype atZero = CreaturePhenotype_Interpolate(&phenoA, &phenoB, 0.0f);
    TEST_ASSERT(atZero.eyes.pupilShape == PUPIL_VERTICAL && FLOAT_NEAR(atZero.eyes.size, 0.50f) &&
                atZero.dorsalColor.r == 10 && atZero.dorsalColor.g == 20 && atZero.dorsalColor.b == 30,
                "Interpolate(A, B, 0) no es idéntico a A");

    CreaturePhenotype atOne = CreaturePhenotype_Interpolate(&phenoA, &phenoB, 1.0f);
    TEST_ASSERT(atOne.eyes.pupilShape == PUPIL_HORIZONTAL && FLOAT_NEAR(atOne.eyes.size, 1.50f) &&
                atOne.dorsalColor.r == 200 && atOne.dorsalColor.g == 100 && atOne.dorsalColor.b == 50,
                "Interpolate(A, B, 1) no es idéntico a B");

    CreaturePhenotype atQuarter = CreaturePhenotype_Interpolate(&phenoA, &phenoB, 0.25f);
    TEST_ASSERT(atQuarter.eyes.pupilShape == PUPIL_VERTICAL, "Transición discreta incorrecta en t < 0.5");
    CreaturePhenotype atThreeQuarters = CreaturePhenotype_Interpolate(&phenoA, &phenoB, 0.75f);
    TEST_ASSERT(atThreeQuarters.eyes.pupilShape == PUPIL_HORIZONTAL, "Transición discreta incorrecta en t >= 0.5");

    /* 9. Test: Distinción entre TopologyCompatible y MorphCompatible */
    CreaturePhenotype phenoDiffArchetype = recipe->adult;
    phenoDiffArchetype.head.archetype = HEAD_ARCHETYPE_AVIAN;
    TEST_ASSERT(CreaturePhenotype_TopologyCompatible(&recipe->adult, &phenoDiffArchetype),
                "TopologyCompatible falló para fenotipos con igual blueprint");
    TEST_ASSERT(!CreaturePhenotype_MorphCompatible(&recipe->adult, &phenoDiffArchetype),
                "MorphCompatible aceptó arquetipos cefálicos incompatibles");

    /* 10. Test: Preservación de EyePhenotype.size a través de las etapas */
    TEST_ASSERT(recipe->seed.eyes.size == 0.0f, "EyePhenotype.size de semilla sobreescrito");
    TEST_ASSERT(recipe->larva.eyes.size == 0.0f, "EyePhenotype.size de larva sobreescrito");
    TEST_ASSERT(FLOAT_NEAR(recipe->juvenile.eyes.size, 0.67f), "EyePhenotype.size juvenil alterado");
    TEST_ASSERT(FLOAT_NEAR(recipe->adult.eyes.size, 0.50f), "EyePhenotype.size adulto alterado");

    /* 11. Test: CREATURE_GAIT_NONE es válido y permite construir criaturas sin locomoción */
    CreatureRecipe noGaitRecipe = *recipe;
    noGaitRecipe.id = 99;
    noGaitRecipe.gait = CREATURE_GAIT_NONE;
    Monster noGaitMonster = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&noGaitMonster, &noGaitRecipe, &noGaitRecipe.adult),
                "Creature_BuildMonster falló con CREATURE_GAIT_NONE");
    TEST_ASSERT(noGaitMonster.animation == NULL, "Criatura con CREATURE_GAIT_NONE no debe poseer animador");
    Monster_Free(&noGaitMonster);

    /* 12. Test: Raíz del BodyPlan configurable y desacoplada de pelvis */
    CreatureRecipe customRootRecipe = *recipe;
    customRootRecipe.id = 88;
    customRootRecipe.bodyPlan.root = (AnatomyRef){.moduleInstanceId = 1, .localNodeId = AXIAL_NODE_PECTORAL};
    Monster customRootMonster = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&customRootMonster, &customRootRecipe, &customRootRecipe.adult),
                "No se pudo construir criatura con raíz no pélvica");
    Rig customRig;
    TEST_ASSERT(CreatureRig_Build(&customRootMonster, &customRig) &&
                customRig.skeleton.joints[0].id == Anatomy_MakeId(1, AXIAL_NODE_PECTORAL),
                "El rig no adoptó la raíz definida por el BodyPlan");
    Monster_Free(&customRootMonster);

    /* 13. Test: Descriptores de miembros con longitud de cadena arbitraria */
    LimbRigDescriptor testLimbDesc = {
        .moduleInstanceId = 50,
        .jointCount = 3,
        .joints = {Anatomy_MakeId(50, 1), Anatomy_MakeId(50, 2), Anatomy_MakeId(50, 3)},
        .endEffector = Anatomy_MakeId(50, 3),
        .role = LIMB_ARM,
        .side = LIMB_LEFT,
        .archetype = LIMB_ARCHETYPE_REPTILE,
        .locomotionLimb = false
    };
    Rig arbRig = {0};
    AnatomyGraph arbGraph; AnatomyGraph_Init(&arbGraph);
    AnatomyGraph_AddNode(&arbGraph, (AnatomyNode){.id = Anatomy_MakeId(50, 1), .widthRadius = 0.2f, .heightRadius = 0.2f});
    AnatomyGraph_AddNode(&arbGraph, (AnatomyNode){.id = Anatomy_MakeId(50, 2), .widthRadius = 0.2f, .heightRadius = 0.2f});
    AnatomyGraph_AddNode(&arbGraph, (AnatomyNode){.id = Anatomy_MakeId(50, 3), .widthRadius = 0.2f, .heightRadius = 0.2f});
    AnatomyGraph_Connect(&arbGraph, (BodyConnection){.id = 1, .fromId = Anatomy_MakeId(50, 1), .toId = Anatomy_MakeId(50, 2), .kind = BODY_CONNECTION_LIMB_SEGMENT});
    AnatomyGraph_Connect(&arbGraph, (BodyConnection){.id = 2, .fromId = Anatomy_MakeId(50, 2), .toId = Anatomy_MakeId(50, 3), .kind = BODY_CONNECTION_LIMB_SEGMENT});
    TEST_ASSERT(RigBuilder_FromAnatomy(&arbGraph, Anatomy_MakeId(50, 1), &arbRig), "No se pudo construir esqueleto para miembro de 3 articulaciones");
    TEST_ASSERT(RigBuilder_AddLimb(&arbRig, testLimbDesc.joints, testLimbDesc.jointCount, testLimbDesc.role, testLimbDesc.side, testLimbDesc.locomotionLimb),
                "RigBuilder_AddLimb rechazó cadena de longitud 3");
    TEST_ASSERT(arbRig.limbs[0].chain.jointCount == 3, "Conteo de articulaciones en LimbRig incorrecto");

    /* 14. Test: Identidad de ranuras de anclaje (AttachmentSlotId independiente del rol) */
    AttachmentSlotSet multiSlots = {0};
    multiSlots.slots[multiSlots.count++] = (AttachmentSlot){
        .id = 1, .role = ATTACHMENT_PECTORAL_LEFT, .hostModuleInstanceId = 100, .hostNode = 1001,
        .position = Vec3_Create(-1.0f, 0, 0), .scale = 1.0f
    };
    multiSlots.slots[multiSlots.count++] = (AttachmentSlot){
        .id = 2, .role = ATTACHMENT_PECTORAL_LEFT, .hostModuleInstanceId = 100, .hostNode = 1001,
        .position = Vec3_Create(-3.0f, 0, 0), .scale = 1.0f
    };
    const AttachmentSlot* slotA = AttachmentSlotSet_Find(&multiSlots, 100, 1);
    const AttachmentSlot* slotB = AttachmentSlotSet_Find(&multiSlots, 100, 2);
    TEST_ASSERT(slotA && slotB && slotA != slotB && slotA->position.x == -1.0f && slotB->position.x == -3.0f,
                "Búsqueda por AttachmentSlotId no distinguió ranuras del mismo rol");

    /* 15. Test: Módulo de ornamentos (HORN) y mapeo semántico superficial */
    CreatureRecipe hornRecipe = *recipe;
    hornRecipe.id = 77;
    hornRecipe.adult.ornamentCount = 1;
    hornRecipe.adult.ornaments[0] = (OrnamentPhenotype){
        .archetype = ORNAMENT_ARCHETYPE_HORN,
        .length = 1.2f, .baseRadius = 0.25f, .tipRadius = 0.05f, .curvature = 0.4f, .development = 1.0f
    };
    hornRecipe.seed = hornRecipe.larva = hornRecipe.juvenile = hornRecipe.adult;
    hornRecipe.bodyPlan.modules[hornRecipe.bodyPlan.moduleCount++] = (CreatureModuleInstance){
        .instanceId = 400,
        .kind = CREATURE_MODULE_ORNAMENT,
        .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SPRAWLING_SLOT_DORSAL},
        .phenotypeIndex = 0
    };
    Monster hornMonster = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&hornMonster, &hornRecipe, &hornRecipe.adult),
                "No se pudo construir criatura con módulo de cuerno");
    const AnatomyNode* hornNode = AnatomyGraph_FindModuleNode(&hornMonster.anatomyGraph, 400, ORNAMENT_NODE_BASE);
    TEST_ASSERT(hornNode && hornNode->region == ANATOMY_REGION_ORNAMENT,
                "Nodo de ornamento no posee la región semántica ANATOMY_REGION_ORNAMENT");
    Monster_Free(&hornMonster);

    /* 16. Test: Composición quimérica estrés: 1 axial, 1 cabeza, 6 miembros, 2 colas independientes, 1 cuerno */
    CreatureRecipe custom = *recipe;
    custom.id = 71;
    custom.name = "Quimera modular";
    custom.adult.limbCount = 6;
    custom.adult.tailCount = 2;
    custom.adult.ornamentCount = 1;
    custom.adult.limbs[0].digitCount = 0;
    custom.adult.limbs[1].digitCount = 3;
    custom.adult.limbs[4] = custom.adult.limbs[2];
    custom.adult.limbs[5] = custom.adult.limbs[3];
    custom.adult.tails[1] = custom.adult.tails[0];
    custom.adult.tails[1].segmentCount = 7;
    custom.adult.tails[1].curvature = 1.0f;
    custom.adult.ornaments[0] = (OrnamentPhenotype){
        .archetype = ORNAMENT_ARCHETYPE_HORN,
        .length = 0.8f, .baseRadius = 0.2f, .tipRadius = 0.04f, .curvature = 0.3f, .development = 1.0f
    };
    custom.bodyPlan.modules[custom.bodyPlan.moduleCount++] = (CreatureModuleInstance){
        .instanceId = 107,
        .kind = CREATURE_MODULE_LIMB,
        .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SPRAWLING_SLOT_DORSAL},
        .phenotypeIndex = 4
    };
    custom.bodyPlan.modules[custom.bodyPlan.moduleCount++] = (CreatureModuleInstance){
        .instanceId = 3001,
        .kind = CREATURE_MODULE_LIMB,
        .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SPRAWLING_SLOT_VENTRAL},
        .phenotypeIndex = 5
    };
    custom.bodyPlan.modules[custom.bodyPlan.moduleCount++] = (CreatureModuleInstance){
        .instanceId = 702,
        .kind = CREATURE_MODULE_TAIL,
        .attachment = {.hostModuleInstanceId = 1, .slotId = AXIAL_SPRAWLING_SLOT_CAUDAL},
        .phenotypeIndex = 1
    };
    custom.bodyPlan.modules[custom.bodyPlan.moduleCount++] = (CreatureModuleInstance){
        .instanceId = 888,
        .kind = CREATURE_MODULE_ORNAMENT,
        .attachment = {.hostModuleInstanceId = 2, .slotId = HEAD_SLOT_CRANIAL_DORSAL},
        .phenotypeIndex = 0
    };
    custom.seed = custom.larva = custom.juvenile = custom.adult;

    Monster m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&m, &custom, &custom.adult), "Composición quimérica no construible");
    TEST_ASSERT(CreatureRig_Build(&m, &rig) && rig.limbCount == 6, "Rig presupone cuatro miembros");
    TEST_ASSERT(rig.tailCount == 2 && rig.tails[0].jointCount == 4 && rig.tails[1].jointCount == 7,
                "Rig de colas múltiples no contiene las dos cadenas independientes");
    TEST_ASSERT(rig.tails[0].moduleInstanceId == 20 && rig.tails[1].moduleInstanceId == 702,
                "Identificadores de instancia de cola cruzados en el rig");
    TEST_ASSERT(AnatomyGraph_FindModuleNode(&m.anatomyGraph, 107, LIMB_NODE_ROOT) &&
                AnatomyGraph_FindModuleNode(&m.anatomyGraph, 3001, LIMB_NODE_ROOT), "Instancias de miembros colisionan");
    TEST_ASSERT(AnatomyGraph_FindModuleNode(&m.anatomyGraph, 888, ORNAMENT_NODE_BASE),
                "Cuerno no acoplado a la ranura craneal de la cabeza");

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &m, MonsterSDF_DefaultConfig()), "SDF acoplado a receta");
    TEST_ASSERT(sdf.axialStationCount == 0 && isfinite(sdf.bounds.start.x) && isfinite(sdf.bounds.end.z),
                "Sweep mezcla ramas caudales");
    MonsterSDF_Free(&sdf);
    Monster_Free(&m);

    /* 17. Validación: rechazo de duplicados */
    custom.bodyPlan.modules[1].instanceId = custom.bodyPlan.modules[0].instanceId;
    TEST_ASSERT(!CreatureRecipe_Validate(&custom, &custom.adult), "IDs de módulos duplicados aceptados");

    printf("[PASS] test_creature_recipe: etapas, IDs, copia, rig, arquetipos, múltiples colas y quimeras\n");
}
