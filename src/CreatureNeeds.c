#include "CreatureNeeds.h"
#include <math.h>
void CreatureNeeds_Update(CreatureNeeds *n, const BrainProfile *p, float dt) {
    if (!isfinite(dt) || dt < 0)
        return;
    for (int i = 0; i < NEED_COUNT; ++i)
        n->urgency[i] =
            p->needs[i].enabled ? fminf(1, fmaxf(0, n->urgency[i] + p->needs[i].rate * dt)) : 0;
}
void CreatureNeeds_Satisfy(CreatureNeeds *n, const BrainProfile *p, CreatureNeed k, float amount) {
    if (k < 0 || k >= NEED_COUNT || amount < 0)
        return;
    n->urgency[k] = p->needs[k].enabled ? fmaxf(0, n->urgency[k] - amount) : 0;
}
float CreatureNeeds_Utility(const CreatureNeeds *n, const BrainProfile *p, CreatureNeed k) {
    return p->needs[k].enabled ? powf(n->urgency[k], fmaxf(.1f, p->needs[k].exponent)) : 0;
}
