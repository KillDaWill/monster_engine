#include "WorldInterface.h"
#include <math.h>
bool World_SampleGround(World* w,Vector3 q,SurfaceHit* hit) {
    if(!hit || !isfinite(q.x) || !isfinite(q.y) || !isfinite(q.z))return false;
    SurfaceHit h={q,{0,1,0}}; h.position.y=0;
    if(w && w->sampleGround) { if(!w->sampleGround(w,q,&h))return false; }
    else if(w && w->getWalkingHeight) {
        h.position.y=w->getWalkingHeight(w,q.x,q.z);
        const float e=.01f;
        float dx=w->getWalkingHeight(w,q.x+e,q.z)-w->getWalkingHeight(w,q.x-e,q.z);
        float dz=w->getWalkingHeight(w,q.x,q.z+e)-w->getWalkingHeight(w,q.x,q.z-e);
        h.normal=Vec3_Normalize(Vec3_Create(-dx,2*e,-dz));
    }
    if(!isfinite(h.position.y) || !isfinite(h.position.x) || !isfinite(h.position.z) ||
        !isfinite(h.normal.x) || !isfinite(h.normal.y) || !isfinite(h.normal.z) || Vec3_LengthSq(h.normal)<1e-8f)return false;
    h.normal=Vec3_Normalize(h.normal); *hit=h; return true;
}
