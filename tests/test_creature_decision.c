#include "ai_test_helpers.h"
void run_creature_decision_tests(void) {
    AIWorld w = AI_TestWorld();
    AIObjectId water = AI_TestWater(&w, Vec3_Create(0, 0, 0));
    CreatureBodyState b = AI_TestBody();
    CreatureMind m;
    CreatureMind_Init(&m, BrainProfile_Find("balanced"), 90, 42);
    m.time = 2;
    CreatureDecision_Update(&m, &w, &b);
    TEST_ASSERT(m.action == ACTION_WANDER, "sed baja");
    m.needs.urgency[0] = 1;
    m.nextDecision = 0;
    CreatureDecision_Update(&m, &w, &b);
    TEST_ASSERT(m.action == ACTION_SEARCH_WATER && m.target == 0, "búsqueda sin omnisciencia");
    AI_TestObserve(&m, &w, b);
    m.nextDecision = 0;
    CreatureDecision_Update(&m, &w, &b);
    TEST_ASSERT(m.action == ACTION_APPROACH_WATER && m.target == water, "agua conocida priorizada");
    CreatureMind waterless;
    CreatureMind_Init(&waterless, BrainProfile_Find("waterless"), 91, 42);
    waterless.time = 2;
    waterless.needs.urgency[0] = 1;
    AI_TestObserve(&waterless, &w, b);
    CreatureDecision_Update(&waterless, &w, &b);
    TEST_ASSERT(waterless.action == ACTION_WANDER, "sin beber aun con urgencia corrupta");
    AIWorld_Add(&w, (AIWorldObject){.type = AI_CREATURE,
                                    .position = {0, 0, -8},
                                    .group = 7,
                                    .visible = true,
                                    .walkable = true});
    CreatureMind solitary, social;
    CreatureMind_Init(&solitary, BrainProfile_Find("solitary"), 92, 1);
    CreatureMind_Init(&social, BrainProfile_Find("social"), 93, 1);
    solitary.time = social.time = 2;
    social.needs.urgency[NEED_SOCIAL] = 1;
    AI_TestObserve(&solitary, &w, b);
    AI_TestObserve(&social, &w, b);
    CreatureDecision_Update(&solitary, &w, &b);
    CreatureDecision_Update(&social, &w, &b);
    TEST_ASSERT(solitary.action == ACTION_AVOID_COMPANY && social.action == ACTION_SEEK_COMPANY,
                "políticas sociales diferentes");
    BrainProfile follower = BrainProfile_Default();
    follower.name = "seguidor";
    follower.social = SOCIAL_AFFILIATIVE;
    follower.weights[ACTION_FOLLOW_TARGET] = .9f;
    CreatureMind following;
    CreatureMind_Init(&following, &follower, 94, 1);
    following.time = 2;
    following.needs.urgency[NEED_SOCIAL] = 1;
    AI_TestObserve(&following, &w, b);
    CreatureDecision_Update(&following, &w, &b);
    TEST_ASSERT(following.action == ACTION_FOLLOW_TARGET, "seguimiento declarativo elegible");
    BrainProfile hungry = BrainProfile_Default();
    hungry.needs[NEED_HUNGER].enabled = true;
    CreatureMind hunger;
    CreatureMind_Init(&hunger, &hungry, 95, 1);
    hunger.time = 2;
    hunger.needs.urgency[NEED_HUNGER] = 1;
    CreatureDecision_Update(&hunger, &w, &b);
    TEST_ASSERT(hunger.action == ACTION_WANDER,
                "hambre sin alimentación no crea tareas imposibles");
    m.failedTarget = water;
    m.cooldownUntil = 10;
    m.nextDecision = 0;
    m.target = 0;
    m.score = 0;
    CreatureDecision_Update(&m, &w, &b);
    TEST_ASSERT(m.action == ACTION_SEARCH_WATER, "cooldown de fuente fallida");
    BrainProfile still = BrainProfile_Default();
    for (int i = 0; i < ACTION_COUNT; ++i)
        still.weights[i] = 0;
    CreatureMind quiet;
    CreatureMind_Init(&quiet, &still, 96, 1);
    CreatureMind_Update(&quiet, &w, &b, .1f);
    TEST_ASSERT(quiet.action == ACTION_IDLE && !quiet.intent.moving,
                "ninguna acción permitida produce reposo inerte");
    BrainProfile noDrink = BrainProfile_Default();
    noDrink.weights[ACTION_DRINK] = 0;
    CreatureMind prohibited;
    CreatureMind_Init(&prohibited, &noDrink, 97, 1);
    prohibited.needs.urgency[0] = 1;
    prohibited.time = 2;
    AI_TestObserve(&prohibited, &w, b);
    CreatureDecision_Update(&prohibited, &w, &b);
    TEST_ASSERT(prohibited.goal != GOAL_HYDRATE, "bebida excluida no genera adquisición imposible");
}
