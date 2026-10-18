#include "SimulationClock.h"
#include <math.h>
bool SimulationClock_Init(SimulationClock *c,double step,double day,unsigned budget) {
    if(!c||!isfinite(step)||step<=0||step>1||!isfinite(day)||day<=0||!budget)return false;
    *c=(SimulationClock){.fixedStep=step,.secondsPerDay=day,.speed=1,.maxStepsPerFrame=budget};
    return true;
}
void SimulationClock_Accumulate(SimulationClock *c,double dt) {
    if(!c||c->paused||!isfinite(dt)||dt<0||!isfinite(c->speed)||c->speed<0)return;
    double total=c->accumulator+dt*c->speed;
    if(!isfinite(total))return;
    c->accumulator=total;
    c->behind=total+1e-12>=c->fixedStep*c->maxStepsPerFrame;
}
bool SimulationClock_Tick(SimulationClock *c) {
    if(!c||c->paused||c->tick==UINT64_MAX)return false;
    ++c->tick;c->seconds=c->tick*c->fixedStep;return true;
}
bool SimulationClock_Pop(SimulationClock *c) {
    if(!c||c->paused||c->accumulator+1e-12<c->fixedStep)return false;
    if(!SimulationClock_Tick(c))return false;
    c->accumulator=fmax(0,c->accumulator-c->fixedStep);
    c->behind=c->accumulator+1e-12>=c->fixedStep;
    return true;
}
double SimulationClock_Days(const SimulationClock *c) {return c->seconds/c->secondsPerDay;}
double SimulationClock_TimeOfDay(const SimulationClock *c) {double d=SimulationClock_Days(c);return d-floor(d);}
