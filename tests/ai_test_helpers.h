#ifndef AI_TEST_HELPERS_H
#define AI_TEST_HELPERS_H
#include "CreatureActions.h"
#include "CreatureDecision.h"
#include "CreatureMind.h"
#include "test_utils.h"
#include <math.h>
#include <string.h>
static inline AIWorld AI_TestWorld(void) {
    AIWorld w;
    AIWorld_Init(&w, 28);
    return w;
}
static inline AIObjectId AI_TestWater(AIWorld *w, Vector3 p) {
    return AIWorld_Add(
        w, (AIWorldObject){
               .type = AI_WATER, .position = p, .radius = 3, .visible = true, .drinkable = true});
}
static inline CreatureBodyState AI_TestBody(void) {
    return (CreatureBodyState){.position = {0, 0, -10},
                               .eye = {0, 1, -10},
                               .mouth = {0, .2f, -9},
                               .mouthDirection={0,-1,0},
                               .group = 7,
                               .capabilities = {.ground = true,
                                                .waterInteraction = true,
                                                .clearance = 1,
                                                .bankClearance = 1,
                                                .drinkReach = 1,
                                                .contactTolerance = .025f,
                                                .maximumOralReach = 1.2f,
                                                .maximumSpeed = 2,
                                                .maximumTurn = 2}};
}
static inline void AI_TestObserve(CreatureMind *m, const AIWorld *w, CreatureBodyState b) {
    m->perception.nextRefresh = 0;
    CreaturePerception_Update(&m->perception, &m->profile->perception, w, m->self, b.eye, b.yaw,
                              m->time);
}
#endif
