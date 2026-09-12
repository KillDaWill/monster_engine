#include "Quaternion.h"
#include <math.h>

Quaternion Quat_Identity(void) { return (Quaternion){0,0,0,1}; }
Quaternion Quat_Normalize(Quaternion q) {
    double n=(double)q.x*q.x+(double)q.y*q.y+(double)q.z*q.z+(double)q.w*q.w;
    if (!isfinite(n) || n<1e-24) return Quat_Identity();
    float s=(float)(1.0/sqrt(n));
    return (Quaternion){q.x*s,q.y*s,q.z*s,q.w*s};
}
Quaternion Quat_Multiply(Quaternion a, Quaternion b) {
    return (Quaternion){a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
Quaternion Quat_Conjugate(Quaternion q) { return (Quaternion){-q.x,-q.y,-q.z,q.w}; }
Quaternion Quat_Inverse(Quaternion q) {
    double n=(double)q.x*q.x+(double)q.y*q.y+(double)q.z*q.z+(double)q.w*q.w;
    if (!isfinite(n) || n<1e-24) return Quat_Identity();
    return (Quaternion){-q.x/n,-q.y/n,-q.z/n,q.w/n};
}
Quaternion Quat_FromAxisAngle(Vector3 axis, float angle) {
    float n=Vec3_Length(axis);
    if (!isfinite(n) || !isfinite(angle) || n<1e-12f) return Quat_Identity();
    float s=sinf(angle*.5f)/n;
    return Quat_Normalize((Quaternion){axis.x*s,axis.y*s,axis.z*s,cosf(angle*.5f)});
}
Quaternion Quat_FromTo(Vector3 from, Vector3 to) {
    float a=Vec3_Length(from),b=Vec3_Length(to);
    if (!isfinite(a) || !isfinite(b) || a<1e-12f || b<1e-12f) return Quat_Identity();
    from=Vec3_Scale(from,1/a); to=Vec3_Scale(to,1/b);
    float d=fmaxf(-1,fminf(1,Vec3_Dot(from,to)));
    if (d < -.999999f) {
        Vector3 basis=fabsf(from.x)<.7f?Vec3_Create(1,0,0):Vec3_Create(0,1,0);
        return Quat_FromAxisAngle(Vec3_Cross(from,basis),3.14159265358979323846f);
    }
    Vector3 c=Vec3_Cross(from,to);
    return Quat_Normalize((Quaternion){c.x,c.y,c.z,1+d});
}
Vector3 Quat_RotateVector(Quaternion q, Vector3 v) {
    q=Quat_Normalize(q);
    Vector3 u=Vec3_Create(q.x,q.y,q.z),t=Vec3_Scale(Vec3_Cross(u,v),2);
    return Vec3_Add(v,Vec3_Add(Vec3_Scale(t,q.w),Vec3_Cross(u,t)));
}
Quaternion Quat_Slerp(Quaternion a, Quaternion b, float t) {
    a=Quat_Normalize(a); b=Quat_Normalize(b);
    t=isfinite(t)?fmaxf(0,fminf(1,t)):0;
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if (d<0) { b=(Quaternion){-b.x,-b.y,-b.z,-b.w}; d=-d; }
    float x=1-t,y=t;
    if (d<.9995f) {
        float angle=acosf(fminf(1,d)),s=sinf(angle);
        x=sinf((1-t)*angle)/s; y=sinf(t*angle)/s;
    }
    return Quat_Normalize((Quaternion){a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y,a.w*x+b.w*y});
}
