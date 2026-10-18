#include "Creature.h"
#include "CreatureRecipes.h"
#include "CreatureRig.h"
#include "CreatureVariation.h"
#include "GaitPresets.h"
#include "LizardVariations.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "MonsterAnimation.h"
#include "ai_test_helpers.h"
void run_brain_profile_tests(void) {
    const BrainProfile *p = BrainProfile_Find("balanced");
    CreatureMind a, b;
    Monster dog = Monster_Create(), lizard = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &CreatureRecipes_Dog()->adult),
                "cuerpo perro");
    TEST_ASSERT(
        Creature_BuildMonster(&lizard, CreatureRecipes_Lizard(), &CreatureRecipes_Lizard()->adult),
        "cuerpo lagarto");
    CreatureMind_Init(&a, p, 1, 42);
    CreatureMind_Init(&b, p, 2, 43);
    a.needs.urgency[0] = .8f;
    a.perception.count = 1;
    TEST_ASSERT(a.profile == b.profile && b.needs.urgency[0] == 0 && b.perception.count == 0,
                "estado individual");
    Monster snapshot = Monster_Clone(&lizard);
    TEST_ASSERT(Monster_CopyInto(&lizard, &snapshot), "snapshot");
    TEST_ASSERT(a.needs.urgency[0] == .8f && a.perception.count == 1, "morfología ajena a mente");
    a.action = ACTION_DRINK;
    a.goal = GOAL_HYDRATE;
    a.target = 123;
    a.perception.entries[0].id = 123;
    CreatureMind saved = a;
    for (size_t i = 0; i < LIZARD_VARIATIONS_COUNT; ++i) {
        CreatureVariation variation = LizardVariations_Get(i, 42);
        CreatureVariant variant;
        TEST_ASSERT(CreatureVariation_Apply(CreatureRecipes_Lizard(),
                                            &CreatureRecipes_Lizard()->adult, &variation, &variant),
                    "variante completa");
        Monster target = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&target, &variant.recipe, &variant.phenotype),
                    "construcción variante");
        MonsterAger ager = MonsterAger_Create(&snapshot, &target, 0);
        MonsterAger_SetGeometrySnapshotEnabled(&ager, false);
        for (int age = 0; age <= 4; ++age) {
            MonsterAger_SetPerc(&ager, age * .25f);
            TEST_ASSERT(Monster_CopyInto(&lizard, MonsterAger_GetResultConst(&ager)), "copia Ager");
            Rig rig;
            TEST_ASSERT(CreatureRig_Build(&lizard, &rig), "rig variante");
            TEST_ASSERT(MonsterAnimation_Configure(
                            &lizard, &rig,
                            GaitPreset_SprawlingQuadruped(variant.phenotype.axial.totalScale)),
                        "animación variante");
            TEST_ASSERT(!memcmp(&a, &saved, sizeof(a)), "mente persiste en edades y variantes");
        }
        MonsterAger_Free(&ager);
        Monster_Free(&target);
    }
    BrainProfile invalid = BrainProfile_Default();
    invalid.perception.horizontalFOV = NAN;
    TEST_ASSERT(!BrainProfile_Validate(&invalid), "perfil inválido rechazado");
    TEST_ASSERT(BrainProfile_Validate(p) &&
                    BrainProfile_Find("waterless")->weights[ACTION_DRINK] == 0,
                "presets válidos y bebida excluida");
    CreatureMind_AssignProfile(&a, BrainProfile_Find("waterless"));
    TEST_ASSERT(a.needs.urgency[0] == 0 && a.perception.count == 0 && a.target == 0,
                "sustitución sin targets");
    for (int i = 0; i < 50; ++i) {
        CreatureMind_Reset(&a);
        CreatureMind_Init(&a, p, 1, 42);
        TEST_ASSERT(a.perception.count == 0, "reset acotado");
    }
    Rig dogRig;
    TEST_ASSERT(CreatureRig_Build(&dog, &dogRig), "rig para postura");
    TEST_ASSERT(MonsterAnimation_Configure(&dog, &dogRig, GaitPreset_CanidWalk(&dogRig)),
                "marcha para postura");
    float maximumContactError = 0;
    float minimumMouthHeight=INFINITY;
    for (int frame = 0; frame < 360; ++frame) {
        float t = fminf(1, frame / 120.f);
        dog.animation->animator.lookDirection = Vec3_Normalize(Vec3_Create(0, -t, 1));
        dog.animation->animator.oralReachActive = true;
        dog.animation->animator.oralReachTarget = Vec3_Create(0,.025f,1.5f);
        TEST_ASSERT(MonsterAnimation_Update(&dog, 1.f / 60), "postura gradual con IK");
        JointPose *head=&dog.animation->pose.joints[dogRig.headJoint];
        Vector3 mouth=Vec3_Add(head->worldPosition,Quat_RotateVector(head->worldRotation,dogRig.oralPoint));
        minimumMouthHeight=fminf(minimumMouthHeight,mouth.y);
        for (size_t j = 0; j < dogRig.skeleton.jointCount; ++j) {
            int parent = dogRig.skeleton.joints[j].parentIndex;
            if (parent >= 0)
                TEST_ASSERT(fabsf(Vec3_Distance(dog.animation->pose.joints[j].worldPosition,
                                                dog.animation->pose.joints[parent].worldPosition) -
                                  Vec3_Length(dogRig.skeleton.joints[j].restPosition)) < 1e-4f,
                            "postura conserva huesos");
            Quaternion q = dog.animation->pose.joints[j].localRotation,
                       limited = Skeleton_ConstrainRotation(&dogRig.skeleton.joints[j], q);
            TEST_ASSERT(Vec3_Distance(Quat_RotateVector(q, Vec3_Create(0, 0, 1)),
                                      Quat_RotateVector(limited, Vec3_Create(0, 0, 1))) < 1e-4f,
                        "postura respeta restricciones");
        }
        for (size_t j = 0; j < dogRig.limbCount; ++j)
            if (dogRig.limbs[j].walking &&
                dog.animation->animator.locomotion.feet[j].phase == FOOT_STANCE)
                maximumContactError =
                    fmaxf(maximumContactError, dog.animation->animator.limbResults[j].error);
    }
    printf("[AI POSTURA] contacto_max=%.6f boca_min=%.6f\n", maximumContactError,minimumMouthHeight);
    TEST_ASSERT(minimumMouthHeight<.2f,"boca real alcanza superficie con cuello");
    TEST_ASSERT(maximumContactError < .004f, "postura mantiene contacto plantar");
    Monster_Free(&snapshot);
    Monster_Free(&dog);
    Monster_Free(&lizard);
    TEST_ASSERT(!BrainProfile_Find("inexistente"), "preset inválido");
}
