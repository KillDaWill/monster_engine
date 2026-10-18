#include "ai_test_helpers.h"
#include "CreatureBoundary.h"
void run_creature_navigation_tests(void) {
    AIWorld w = AI_TestWorld();
    AI_TestWater(&w, Vec3_Create(0, 0, 0));
    CreatureNavigation n = {0};
    Vector3 from = Vec3_Create(-10, 0, 0), goal = Vec3_Create(10, 0, 0);
    TEST_ASSERT(CreatureNavigation_Plan(&n, &w, from, goal, 1, false) && n.count > 1,
                "ruta rodea estanque");
    Vector3 p = from;
    for (unsigned i = 0; i < n.count; ++i) {
        TEST_ASSERT(AIWorld_SegmentWalkable(&w, p, n.points[i], 1, false), "aristas libres");
        p = n.points[i];
    }
    TEST_ASSERT(!CreatureNavigation_Plan(&n, &w, from, Vec3_Create(0, 0, 0), 1, false) && n.failed,
                "objetivo bloqueado falla");
    TEST_ASSERT(CreatureNavigation_Plan(&n, &w, from, goal, 1, true),
                "capacidad acuática independiente");
    CreatureBodyState b = AI_TestBody();
    CreatureIntent i = {.moving = true, .destination = {10, 0, 10}, .speed = 999};
    CreatureMotorCommand c = CreatureMotor_Resolve(&i, &b);
    TEST_ASSERT(c.speed <= b.capabilities.maximumSpeed, "límite motor");
    TEST_ASSERT(!AIWorld_Walkable(&w, Vec3_Create(27.5f, 0, 0), 1, false), "volumen en límites");
    CreatureNavigation_Plan(&n, &w, from, goal, 1, false);
    Vector3 waypoint;
    NavigationGoalStatus status = NAVIGATION_ADVANCING;
    for (int j = 0; j < 1000 && status == NAVIGATION_ADVANCING; ++j)
        status = CreatureNavigation_Update(&n, from, 0, 2, 2, .05f, &waypoint);
    TEST_ASSERT(status == NAVIGATION_STALLED, "no persigue indefinidamente");
    TEST_ASSERT(CreatureNavigation_Plan(&n,&w,Vec3_Create(8,0,10),Vec3_Create(10,0,10),1,false),
                "ruta de llegada con contacto oscilante");
    for(int frame=0;frame<100;++frame) {
        Vector3 jitter=Vec3_Create(10+.08f*sinf(frame),0,10+.05f*cosf(frame));
        TEST_ASSERT(CreatureNavigation_Update(&n,jitter,0,1,1,.016f,&waypoint)==NAVIGATION_ARRIVED,
                    "oscilación plantar dentro del margen no persigue punto cruzado");
    }
    CreatureBoundary bounds={.jointCount=2};MonsterAnimation animation={0};
    bounds.support[0]=(AABB3D){.start={-.2f,-.2f,9.8f},.end={.2f,.2f,10.2f}};
    bounds.support[1]=(AABB3D){.start={9.8f,-.2f,-.2f},.end={10.2f,.2f,.2f}};
    for(int j=0;j<2;++j){bounds.inverseRest[j]=Quat_Identity();animation.pose.joints[j].worldRotation=Quat_Identity();}
    float turning=CreatureBoundary_TurningRadius(&bounds,&animation,1,0);
    TEST_ASSERT(turning>10.2f&&turning<10.3f,"giro contiene apéndices sin esquina ficticia");
    TEST_ASSERT(CreatureBoundary_TurningRadius(&bounds,&animation,2,.1f)>turning*2,"giro incluye escala y margen");

}
