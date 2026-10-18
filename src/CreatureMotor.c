#include "CreatureMotor.h"
#include <math.h>
CreatureMotorCommand CreatureMotor_Resolve(const CreatureIntent *i, const CreatureBodyState *b) {
    CreatureMotorCommand c = {b->yaw, 0};
    Vector3 d = Vec3_Sub(i->moving ? i->destination : i->lookTarget, b->position);
    d.y=0;
    if ((i->moving || i->lookAt) && Vec3_LengthSq(d)>1e-6f)
        c.heading = atan2f(d.x, d.z) + (i->reverse ? 3.14159265f : 0);
    if (i->moving && b->capabilities.ground)
        c.speed = fminf(i->speed, b->capabilities.maximumSpeed) * fminf(1, Vec3_Length(d));
    if (i->reverse)
        c.speed = -c.speed;
    return c;
}
