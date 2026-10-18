/** @file NavigationProgress.c
 * @brief Seguimiento geométrico sin confundir recorrido circular con avance al objetivo.
 */
#include "NavigationProgress.h"
#include <math.h>
NavigationGoalStatus NavigationProgress_Update(NavigationProgress* p,Vector3 position,Vector3 goal,
    float yaw,float speed,float turnRate,float arrivalRadius,float dt) {
    if(!p||!isfinite(dt)||dt<0||!isfinite(yaw)||!isfinite(speed)||!isfinite(turnRate)||
       !isfinite(arrivalRadius)||arrivalRadius<0)return NAVIGATION_STALLED;
    position.y=goal.y=0;
    float distance=Vec3_Distance(position,goal);
    if(!isfinite(distance))return NAVIGATION_STALLED;
    if(distance<=arrivalRadius)return NAVIGATION_ARRIVED;
    if(!p->initialized) {
        *p=(NavigationProgress){.initialized=true,.bestDistance=distance,.previousYaw=yaw};
    }
    p->elapsed+=dt;p->withoutProgress+=dt;
    float change=remainderf(yaw-p->previousYaw,6.28318530718f);
    p->previousYaw=yaw;p->winding+=change;
    /* Exigir avance acumulado evita que el ruido de contacto renueve el plazo. */
    if(distance<p->bestDistance-fmaxf(.05f,fabsf(speed)*.1f)) {
        p->bestDistance=distance;p->withoutProgress=0;p->winding=0;
    }
    float turnTime=3.14159265359f/fmaxf(.1f,fabsf(turnRate));
    float timeout=fmaxf(3.f,turnTime*1.5f);
    if(p->withoutProgress>timeout || (p->withoutProgress>turnTime&&fabsf(p->winding)>4.712389f))
        return NAVIGATION_STALLED;
    return NAVIGATION_ADVANCING;
}
