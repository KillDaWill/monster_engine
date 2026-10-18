#include "ai_test_helpers.h"
void run_creature_actions_tests(void) {
    AIWorld w = AI_TestWorld();
    AIObjectId water = AI_TestWater(&w, Vec3_Create(0, 0, 0));
    CreatureBodyState b = AI_TestBody();
    CreatureMind m;
    CreatureMind_Init(&m, BrainProfile_Find("balanced"), 90, 42);
    m.time = 2;
    m.needs.urgency[0] = .9f;
    AI_TestObserve(&m, &w, b);
    m.action = ACTION_DRINK;
    m.target = water;
    CreatureActions_Update(&m, &w, &b, .1f);
    TEST_ASSERT(m.needs.urgency[0] == .9f && m.action == ACTION_APPROACH_WATER,
                "no beber a distancia");
    b.position = Vec3_Create(0, 0, -4.35f);
    b.mouth = Vec3_Create(0, .2f, -3.35f);
    AIObjectId barrier = AIWorld_Add(&w, (AIWorldObject){.type = AI_OBSTACLE,
                                                         .position = {0, 0, -3.2f},
                                                         .radius = .1f,
                                                         .height = 2,
                                                         .occludes = true});
    TEST_ASSERT(!AIWorld_CanDrink(&w, water, b.mouth, 1), "no beber a través de obstáculo");
    AIWorld_Remove(&w, barrier);
    TEST_ASSERT(!AIWorld_CanDrink(&w, water, Vec3_Create(0, .2f, 0), 1),
                "no beber desde centro del estanque");
    m.action = ACTION_DRINK;
    b.mouth=Vec3_Create(0,0,-3);b.reach=(ReachResult){.status=REACH_REACHED,.objectId=water};
    CreatureActions_Update(&m, &w, &b, 1);
    TEST_ASSERT(m.needs.urgency[0] < .9f && m.needs.urgency[0] > .15f && !m.intent.moving,
                "beber progresivo parado");
    for (int i = 0; i < 50 && m.action == ACTION_DRINK; ++i)
        CreatureActions_Update(&m, &w, &b, .1f);
    TEST_ASSERT(m.action == ACTION_WANDER && m.completedDrinks == 1, "final hidratación");
    m.target = water;
    m.action = ACTION_DRINK;
    AIWorld_Remove(&w, water);
    CreatureMind_Update(&m, &w, &b, .1f);
    TEST_ASSERT(m.target == 0 && m.actionFailures > 0, "retirada segura");
    CreatureMind stuck;
    CreatureMind_Init(&stuck,BrainProfile_Find("balanced"),90,42);
    stuck.retreating=true;
    stuck.retreatStart=b.position;
    stuck.retreatGoal=Vec3_Create(10,0,0);
    b.capabilities.maximumTurn=1;
    for(int i=0;i<100&&stuck.retreating;++i) {
        stuck.time+=.1;
        CreatureActions_Update(&stuck,&w,&b,.1f);
    }
    TEST_ASSERT(!stuck.retreating&&stuck.actionFailures==1,"retroceso atascado tiene recuperación finita");
    CreatureIntent zero={.moving=true,.destination=b.position,.speed=2};
    b.yaw=.7f;
    CreatureMotorCommand stopped=CreatureMotor_Resolve(&zero,&b);
    TEST_ASSERT(FLOAT_NEAR(stopped.heading,b.yaw)&&stopped.speed==0,"destino nulo no inventa un giro");
    zero.reverse=true;zero.destination=Vec3_Add(b.position,Vec3_Create(1,0,0));
    stopped=CreatureMotor_Resolve(&zero,&b);
    TEST_ASSERT(fabsf(stopped.speed)<=b.capabilities.maximumSpeed,"retroceso respeta capacidad");
    CreatureMind a, c;
    CreatureMind_Init(&a, BrainProfile_Find("waterless"), 90, 42);
    CreatureMind_Init(&c, BrainProfile_Find("waterless"), 90, 42);
    for (int j = 0; j < 2000; ++j) {
        CreatureMind_Update(&a, &w, &b, .016f);
        CreatureMind_Update(&c, &w, &b, .016f);
        TEST_ASSERT(a.action == c.action && a.random == c.random && a.target == c.target &&
                        a.needs.urgency[0] == 0,
                    "trazas deterministas sin agua");
    }
    double time = a.time;
    float rest = a.needs.urgency[NEED_REST];
    CreatureMind_Update(&a, &w, &b, 0);
    TEST_ASSERT(a.time == time && a.needs.urgency[NEED_REST] == rest, "pausa sin avance");
    for (int shape = 0; shape < 2; ++shape) {
        AIWorld world = AI_TestWorld();
        AI_TestWater(&world, Vec3_Create(0, 0, 0));
        CreatureBodyState body = AI_TestBody();
        body.position = Vec3_Create(-10, 0, 0);
        body.yaw = body.visionYaw = 1.5707963f;
        body.capabilities.clearance = shape ? .65f : .95f;
        body.capabilities.bankClearance = body.capabilities.clearance;
        body.capabilities.mouthForward = shape ? .7f : 1.2f;
        body.capabilities.maximumSpeed = shape ? .75f : 1.35f;
        CreatureMind mind;
        CreatureMind_Init(&mind, BrainProfile_Find("balanced"), 20 + shape, 42);
        mind.needs.urgency[0] = .9f;
        for (int frame = 0; frame < 1800 && !mind.completedDrinks; ++frame) {
            body.eye = Vec3_Add(body.position, Vec3_Create(0, 1, 0));
            body.mouth = Vec3_Add(body.position,
                                  Vec3_Create(sinf(body.yaw) * body.capabilities.mouthForward, .2f,
                                              cosf(body.yaw) * body.capabilities.mouthForward));
            if(mind.intent.interact) {
                body.mouth=mind.intent.interaction.position;
                body.mouthDirection=mind.intent.interaction.approach;
                body.reach=(ReachResult){.status=REACH_REACHED,.objectId=mind.target};
            }
            CreatureMind_Update(&mind, &world, &body, 1.f / 60);
            CreatureMotorCommand command = CreatureMotor_Resolve(&mind.intent, &body);
            float turn = atan2f(sinf(command.heading - body.yaw), cosf(command.heading - body.yaw));
            body.yaw += fmaxf(-2.f / 60, fminf(2.f / 60, turn));
            body.visionYaw = body.yaw;
            body.speed = command.speed;
            Vector3 next =
                Vec3_Add(body.position, Vec3_Create(sinf(body.yaw) * command.speed / 60, 0,
                                                    cosf(body.yaw) * command.speed / 60));
            TEST_ASSERT(AIWorld_SegmentWalkable(&world, body.position, next,
                                                body.capabilities.bankClearance, false),
                        "camino nunca atraviesa agua");
            TEST_ASSERT(command.speed <= body.capabilities.maximumSpeed, "velocidad corporal");
            body.position = next;
            if (mind.action == ACTION_DRINK)
                TEST_ASSERT(!mind.intent.moving && command.speed == 0, "bebida detenida");
        }
        TEST_ASSERT(mind.completedDrinks == 1 && mind.needs.urgency[0] <= .15f,
                    "dos capacidades alcanzan banco y beben");
    }
}
