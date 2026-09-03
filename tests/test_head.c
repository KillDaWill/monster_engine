#include "test_utils.h"
#include "Head.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "SDFPrimitives.h"
#include "Mesh.h"
#include "MonsterAger.h"
#include <math.h>

static Monster HeadFixture(HeadArchetype archetype) {
    Monster monster=Monster_Create(); Monster_Init(&monster);
    monster.bodyParts[0].width=monster.bodyParts[0].widthRender=1.0f;
    monster.bodyParts[0].height=monster.bodyParts[0].heightRender=.9f;
    monster.bodyParts[0].length=monster.bodyParts[0].lengthRender=1.0f;
    Head head=Head_Create(archetype,0,Vec3_Create(.5f,.45f,.5f));
    TEST_ASSERT(Monster_SetHead(&monster,head),"No se pudo instalar la cabeza anatómica");
    return monster;
}

static void test_head_presets_and_ownership(void) {
    Head lizard=Head_Create(HEAD_ARCHETYPE_LIZARD,0,Vec3_Create(.5f,.45f,.5f));
    Head canid=Head_Create(HEAD_ARCHETYPE_CANID,0,Vec3_Create(.5f,.45f,.5f));
    Head avian=Head_Create(HEAD_ARCHETYPE_AVIAN,0,Vec3_Create(.5f,.45f,.5f));
    TEST_ASSERT(HeadAnatomy_Validate(&lizard.anatomy)==HEAD_VALID,"Preset lagarto inválido");
    TEST_ASSERT(HeadAnatomy_Validate(&canid.anatomy)==HEAD_VALID,"Preset cánido inválido");
    TEST_ASSERT(HeadAnatomy_Validate(&avian.anatomy)==HEAD_VALID,"Preset ave inválido");
    TEST_ASSERT(lizard.anatomy.surface.craniumRadii.y<canid.anatomy.surface.craniumRadii.y,"El lagarto no conserva cráneo aplanado");
    TEST_ASSERT(canid.anatomy.surface.hasEars && canid.anatomy.surface.hasNasalPad,"El cánido perdió orejas o almohadilla nasal");
    TEST_ASSERT(avian.anatomy.surface.isBeak && avian.anatomy.oralSystem.shape==MOUTH_SHAPE_LOWER_BEAK && avian.anatomy.surface.faceTipRadii.x<avian.anatomy.surface.faceRootRadii.x,"El pico no usa su receta superior/inferior ahusada");
    Monster monster=HeadFixture(HEAD_ARCHETYPE_CANID);
    TEST_ASSERT(monster.hasHead && monster.mouthCount==1 && monster.eyeCount==2,"El puente visual de Head no se sincronizó");
    TEST_ASSERT(monster.head.anatomy.oralSystem.bodyPartIndex==monster.mouths[0].bodyPartIndex,"OralSystem perdió su anfitrión");
    monster.eyes[0].scleraColor=Color_FromRGB(210,180,60); monster.eyes[0].pupilScale=.34f;
    monster.head.phenotype.muzzleLength=.80f;
    TEST_ASSERT(Monster_ResolveHead(&monster),"No se pudo volver a resolver la cabeza");
    TEST_ASSERT(monster.eyes[0].scleraColor.r==210&&fabsf(monster.eyes[0].pupilScale-.34f)<.001f,"Resolver perdió la apariencia poseable del ojo");
    Monster_Free(&monster);
    printf("[PASS] test_head_presets_and_ownership\n");
}

static void test_head_randomized_anatomy_sweep(void) {
    Vector3 scales[]={Vec3_Create(.25f,.20f,.28f),Vec3_Create(.5f,.45f,.5f),Vec3_Create(1.0f,.8f,1.2f)};
    for(int archetype=HEAD_ARCHETYPE_LIZARD;archetype<=HEAD_ARCHETYPE_AVIAN;++archetype) {
        for(uint32_t seed=1;seed<=180;++seed) {
            HeadPhenotype p=HeadPhenotype_RandomValid((HeadArchetype)archetype,seed);
            HeadAnatomy a;
            TEST_ASSERT(HeadAnatomy_Resolve(&p,0,scales[seed%3],&a),"Resolver rechazó un fenotipo legal");
            TEST_ASSERT(HeadAnatomy_Validate(&a)==HEAD_VALID,"Sweep produjo landmarks inválidos");
            TEST_ASSERT(a.landmarks.leftJawHinge.z<a.landmarks.leftMouthCorner.z,"Bisagra delante de comisura");
            TEST_ASSERT(a.landmarks.leftNostril.z>=a.landmarks.muzzleRoot.z && a.landmarks.leftNostril.z<=a.landmarks.muzzleTip.z,"Narina fuera del rostro");
        }
    }
    printf("[PASS] test_head_randomized_anatomy_sweep\n");
}

static void test_head_socket_and_nostril_cutters(void) {
    for(int archetype=HEAD_ARCHETYPE_LIZARD;archetype<=HEAD_ARCHETYPE_AVIAN;++archetype) {
        Monster monster=HeadFixture((HeadArchetype)archetype); MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"No se compiló la cabeza SDF");
        Vector3 host=monster.bodyParts[0].positionRender;
        Vector3 orbit=Vec3_Add(host,monster.head.anatomy.landmarks.leftOrbit);
        Vector3 nostril=Vec3_Add(host,monster.head.anatomy.landmarks.leftNostril);
        TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,orbit)>0.0f,"La órbita no es una cavidad sustractiva");
        TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,nostril)>0.0f,"La narina no es una cavidad sustractiva");
        TEST_ASSERT(sdf.mouths[0].anatomicalHead,"MonsterSDF ignoró la receta Head");
        MonsterSDF_Free(&sdf); Monster_Free(&monster);
    }
    printf("[PASS] test_head_socket_and_nostril_cutters\n");
}

static void test_head_meshes_and_primitive(void) {
    float root=SDF_TaperedEllipticalCapsuleApprox(Vec3_Zero(),Vec3_Zero(),Vec3_Create(0,0,1),Vec3_Create(.5f,.3f,.3f),Vec3_Create(.1f,.1f,.1f));
    float far=SDF_TaperedEllipticalCapsuleApprox(Vec3_Create(2,0,0),Vec3_Zero(),Vec3_Create(0,0,1),Vec3_Create(.5f,.3f,.3f),Vec3_Create(.1f,.1f,.1f));
    TEST_ASSERT(isfinite(root)&&root<0.0f&&far>0.0f,"Cápsula elíptica ahusada inválida");
    for(int archetype=HEAD_ARCHETYPE_LIZARD;archetype<=HEAD_ARCHETYPE_AVIAN;++archetype) {
        Monster monster=HeadFixture((HeadArchetype)archetype); MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"Build SDF de preset falló");
        SDFField field=MonsterSDF_GetField(&sdf); SDFMesherConfig cfg=SDFMesher_DefaultConfig(); cfg.voxelSize=.13f; cfg.maxCells=220000;
        SDFMesher mesher=SDFMesher_Create(cfg); Mesh mesh=Mesh_Create();
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&mesh),"Marching Cubes falló para un preset de cabeza");
        TEST_ASSERT(mesh.vertexCount>0 && Mesh_Validate(&mesh).valid,"Preset produjo una malla inválida");
        Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster);
    }
    printf("[PASS] test_head_meshes_and_primitive\n");
}

static void test_head_semantic_aging_and_pose(void) {
    Monster juvenile=HeadFixture(HEAD_ARCHETYPE_CANID), adult=HeadFixture(HEAD_ARCHETYPE_CANID);
    juvenile.head.phenotype.muzzleLength=.30f; adult.head.phenotype.muzzleLength=.90f;
    TEST_ASSERT(Monster_ResolveHead(&juvenile)&&Monster_ResolveHead(&adult),"No se resolvieron extremos de edad");
    Monster_SetHeadOpenFactor(&juvenile,.15f); Monster_SetHeadOpenFactor(&adult,.75f);
    MonsterAger ager=MonsterAger_Create(&juvenile,&adult,.5f); Monster* middle=MonsterAger_GetResult(&ager);
    TEST_ASSERT(middle->hasHead&&fabsf(middle->head.phenotype.muzzleLength-.60f)<.001f,"MonsterAger no interpola rasgos semánticos");
    TEST_ASSERT(fabsf(middle->mouths[0].openFactor-.45f)<.001f&&fabsf(middle->head.anatomy.oralSystem.openFactor-.45f)<.001f,"Pose oral se desincronizó al envejecer");
    Monster_SetHeadOpenFactor(middle,.9f);
    TEST_ASSERT(fabsf(middle->mouths[0].openFactor-.9f)<.001f,"La articulación de Head no alcanzó el renderer heredado");
    MonsterAger_Free(&ager); Monster_Free(&juvenile); Monster_Free(&adult);
    printf("[PASS] test_head_semantic_aging_and_pose\n");
}

void run_head_tests(void) {
    printf("\n--- Módulo Cabeza Anatómica ---\n");
    test_head_presets_and_ownership();
    test_head_randomized_anatomy_sweep();
    test_head_socket_and_nostril_cutters();
    test_head_meshes_and_primitive();
    test_head_semantic_aging_and_pose();
}
