/** @file benchmark_ai.c @brief Coste aislado de cognición y navegación sin
 * render. */
#include "CreatureMind.h"
#include "Creature.h"
#include "CreatureRecipes.h"
#include "CreatureRig.h"
#include "GaitPresets.h"
#include "Monster.h"
#include "MonsterAnimation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
static int posture(void) {
    double timings[2];
    for(int mode=0;mode<2;++mode) {
        Monster m=Monster_Create();Rig rig;
        if(!Creature_BuildMonster(&m,CreatureRecipes_Dog(),&CreatureRecipes_Dog()->adult)||
           !CreatureRig_Build(&m,&rig)||!MonsterAnimation_Configure(&m,&rig,GaitPreset_CanidWalk(&rig)))return 2;
        m.animation->animator.oralReachActive=mode!=0;
        m.animation->animator.oralReachTarget=Vec3_Create(0,.025f,1.5f);
        for(int f=0;f<240;++f)MonsterAnimation_Update(&m,1.f/60);
        clock_t start=clock();
        for(int f=0;f<6000;++f)if(!MonsterAnimation_Update(&m,1.f/60))return 2;
        timings[mode]=1000.*(clock()-start)/CLOCKS_PER_SEC/6000;
        Monster_Free(&m);
    }
    printf("posture baseline_ms=%.6f reaching_ms=%.6f delta_ms=%.6f\n",timings[0],timings[1],timings[1]-timings[0]);
    return 0;
}
int main(int argc, char **argv) {
    if(argc>1&&!strcmp(argv[1],"--posture"))return posture();
    unsigned count = argc > 1 ? (unsigned)atoi(argv[1]) : 2;
    if (count < 1 || count > 100)
        return 2;
    AIWorld world;
    AIWorld_Init(&world, 100);
    AIWorld_Add(&world, (AIWorldObject){.type = AI_WATER,
                                        .position = {0, 0, 0},
                                        .radius = 4,
                                        .visible = true,
                                        .drinkable = true});
    CreatureMind *minds = calloc(count, sizeof(*minds));
    CreatureBodyState *bodies = calloc(count, sizeof(*bodies));
    if (!minds || !bodies)
        return 2;
    for (unsigned i = 0; i < count; ++i) {
        float angle = i * 6.2831853f / count;
        bodies[i] = (CreatureBodyState){.position = {sinf(angle) * 20, 0, cosf(angle) * 20},
                                        .eye = {sinf(angle) * 20, 1, cosf(angle) * 20},
                                        .mouth = {sinf(angle) * 19, 1, cosf(angle) * 19},
                                        .group = i % 3,
                                        .yaw = angle + 3.14159265f,
                                        .visionYaw = angle + 3.14159265f,
                                        .capabilities = {.ground = true,
                                                         .waterInteraction = true,
                                                         .clearance = 1,
                                                         .drinkReach = 1,
                                                         .mouthForward = 1,
                                                         .maximumSpeed = 2,
                                                         .maximumTurn = 2}};
        AIObjectId id = AIWorld_Add(&world, (AIWorldObject){.type = AI_CREATURE,
                                                            .position = bodies[i].position,
                                                            .visible = true,
                                                            .walkable = true,
                                                            .group = i % 3});
        CreatureMind_Init(&minds[i], BrainProfile_Find(i % 2 ? "social" : "balanced"), id, 42 + i);
        minds[i].needs.urgency[0] = .85f;
    }
    double total = 0, maximum = 0;
    unsigned frames = 3600;
    for (unsigned f = 0; f < frames; ++f) {
        clock_t start = clock();
        for (unsigned i = 0; i < count; ++i) {
            CreatureMind_Update(&minds[i], &world, &bodies[i], 1.f / 60);
            if (minds[i].intent.moving) {
                Vector3 delta = Vec3_Sub(minds[i].intent.destination, bodies[i].position);
                float distance = Vec3_Length(delta);
                Vector3 step = Vec3_Scale(Vec3_Normalize(delta), fminf(distance, 2.f / 60));
                bodies[i].position = Vec3_Add(bodies[i].position, step);
                bodies[i].eye = Vec3_Add(bodies[i].position, Vec3_Create(0, 1, 0));
                bodies[i].yaw = bodies[i].visionYaw = atan2f(delta.x, delta.z);
                bodies[i].mouth = Vec3_Add(
                    bodies[i].position, Vec3_Create(sinf(bodies[i].yaw), .3f, cosf(bodies[i].yaw)));
                AIWorld_Move(&world, minds[i].self, bodies[i].position);
            }
        }
        double ms = 1000. * (clock() - start) / CLOCKS_PER_SEC;
        total += ms;
        maximum = fmax(maximum, ms);
    }
    printf("agents=%u frames=%u mean_ms=%.6f max_ms=%.6f\n", count, frames, total / frames,
           maximum);
    free(minds);
    free(bodies);
    return 0;
}
