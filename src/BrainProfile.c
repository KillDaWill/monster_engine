#include "BrainProfile.h"
#include <math.h>
#include <string.h>
#define COMMON(n, s, h)                                                                            \
    {.name = n,                                                                                    \
     .needs = {{h, h?.025f:0, 2, .9f, h?.22f:0, .15f},                                                     \
               {true, .008f, 2, .95f, .16f, .15f},                                                 \
               {true, .018f, 1, .95f, .2f, .05f},                                                  \
               {false, 0, 1, 1, 0, .15f}},                                                         \
     .perception = {32, 4.2f, 0, .15f, 25},                                                        \
     .weights = {.16f, h ? 1.4f : 0, h ? 1.5f : 0, h ? 1.6f : 0, .8f, .65f, .95f, .6f},            \
     .social = s,                                                                                  \
     .socialDistance = 5,                                                                          \
     .decisionInterval = .3f,                                                                      \
     .switchMargin = .12f,                                                                         \
     .commitment = 1,                                                                              \
     .explorationRadius = 14}

static const BrainProfile profiles[] = {
    {.name="forager",.needs={{true,.025f,2,.9f,.22f,.15f},{true,.008f,2,.95f,.16f,.15f},
        {true,.018f,1,.95f,.2f,.05f},{true,.025f,2,.9f,.22f,.15f}},
     .perception={32,4.2f,0,.15f,25},.weights={[ACTION_WANDER]=.16f,[ACTION_SEARCH_WATER]=1.4f,
        [ACTION_APPROACH_WATER]=1.5f,[ACTION_DRINK]=1.6f,[ACTION_REST]=.8f,[ACTION_SEEK_COMPANY]=.65f,
        [ACTION_AVOID_COMPANY]=.95f,[ACTION_FOLLOW_TARGET]=.6f,[ACTION_SEARCH_FOOD]=1.4f,
        [ACTION_APPROACH_FOOD]=1.5f,[ACTION_EAT]=1.6f},.social=SOCIAL_NEUTRAL,.socialDistance=5,
     .decisionInterval=.3f,.switchMargin=.12f,.commitment=1,.explorationRadius=14},
    COMMON("balanced", SOCIAL_NEUTRAL, true), COMMON("solitary", SOCIAL_SOLITARY, true),
    COMMON("waterless", SOCIAL_NEUTRAL, false), COMMON("social", SOCIAL_AFFILIATIVE, true)};
const BrainProfile *BrainProfile_Find(const char *name) {
    if (!name)
        return NULL;
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i)
        if (!strcmp(name, profiles[i].name))
            return &profiles[i];
    return NULL;
}
BrainProfile BrainProfile_Default(void) { return *BrainProfile_Find("balanced"); }
const char *CreatureAction_Name(CreatureAction a) {
    static const char *names[] = {"wander", "search_water", "approach_water", "drink",
                                  "rest",   "seek_company", "avoid_company",  "follow_target", "idle", "search_food", "approach_food", "eat", "chase", "attack", "escape", "approach_mate"};
    return a >= 0 && a < ACTION_COUNT ? names[a] : "invalid";
}

bool BrainProfile_Validate(const BrainProfile *p) {
    if (!p || !p->name || !isfinite(p->dangerDistance) || p->dangerDistance<0 || !isfinite(p->perception.range) || p->perception.range < 0 ||
        !isfinite(p->perception.horizontalFOV) || p->perception.horizontalFOV < 0 ||
        p->perception.horizontalFOV > 6.283186f || !isfinite(p->perception.verticalFOV) ||
        p->perception.verticalFOV < 0 || p->perception.verticalFOV > 3.141593f ||
        !isfinite(p->perception.interval) || p->perception.interval <= 0 ||
        !isfinite(p->perception.memoryDuration) || p->perception.memoryDuration < 0 ||
        !isfinite(p->decisionInterval) || p->decisionInterval <= 0 || !isfinite(p->commitment) ||
        p->commitment < 0 || !isfinite(p->switchMargin) || p->switchMargin < 0 ||
        !isfinite(p->explorationRadius) || p->explorationRadius <= 0 ||
        !isfinite(p->socialDistance) || p->socialDistance <= 0 || p->social < SOCIAL_NEUTRAL ||
        p->social > SOCIAL_TERRITORIAL)
        return false;
    for (int i = 0; i < NEED_COUNT; ++i)
        if (!isfinite(p->needs[i].rate) || p->needs[i].rate < 0 ||
            !isfinite(p->needs[i].exponent) || p->needs[i].exponent <= 0 ||
            !isfinite(p->needs[i].critical) || p->needs[i].critical < 0 ||
            p->needs[i].critical > 1 || !isfinite(p->needs[i].satisfactionRate) ||
            p->needs[i].satisfactionRate < 0 || !isfinite(p->needs[i].desiredLevel) ||
            p->needs[i].desiredLevel < 0 || p->needs[i].desiredLevel >= 1)
            return false;
    for (int i = 0; i < ACTION_COUNT; ++i)
        if (!isfinite(p->weights[i]) || p->weights[i] < 0)
            return false;
    return true;
}

const char *CreatureGoal_Name(CreatureGoal goal) {
    static const char *names[]={"explorar","hidratar","descansar","socializar","evitar",
        "seguir","alimentar","cazar","huir","reproducir","recuperar"};
    return goal>=GOAL_EXPLORE&&goal<=GOAL_RECOVER?names[goal]:"invalid";
}
