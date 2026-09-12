#include "Locomotion.h"
#include <math.h>
#include <string.h>
static bool ValidGait(const GaitProfile* g) {
    return g && isfinite(g->cycleDuration) && g->cycleDuration>.001f &&
        isfinite(g->stanceRatio) && g->stanceRatio>0 && g->stanceRatio<1 &&
        isfinite(g->strideLength) && g->strideLength>0 && isfinite(g->stepHeight) && g->stepHeight>=0;
}
static bool GroundTarget(World* w,Vector3 query,float sole,Vector3* target,Vector3* normal) {
    SurfaceHit hit;
    if(!World_SampleGround(w,query,&hit))return false;
    *target=Vec3_Add(hit.position,Vec3_Scale(hit.normal,sole)); *normal=hit.normal; return true;
}
bool Locomotion_Init(Locomotion* state,const Rig* rig,const GaitProfile* gait,World* world,Vector3 body) {
    if(!state || !rig || rig->limbCount>RIG_MAX_LIMBS || !ValidGait(gait))return false;
    SkeletonPose rest; if(!SkeletonPose_Init(&rest,&rig->skeleton))return false;
    Locomotion l={0}; l.bodyPosition=body;
    for(size_t i=0;i<rig->limbCount;++i) {
        const LimbRig* limb=&rig->limbs[i]; FootState* f=&l.feet[i];
        if(limb->endEffectorJoint<0 || limb->endEffectorJoint>=(int)rest.jointCount)return false;
        Vector3 q=Vec3_Add(body,rest.joints[limb->endEffectorJoint].worldPosition);
        f->plantedPosition=q; f->normal=Vec3_Create(0,1,0);
        if(limb->walking) GroundTarget(world,q,limb->soleHeight,&f->plantedPosition,&f->normal);
        f->targetPosition=f->plantedPosition; f->phaseOffset=gait->limbPhase[i];
    }
    l.initialized=true; *state=l; return true;
}
bool Locomotion_Update(Locomotion* l,const Rig* rig,const GaitProfile* g,World* world,Vector3 desired,float dt) {
    if(!l || !rig || rig->limbCount>RIG_MAX_LIMBS || !l->initialized || !ValidGait(g) || !isfinite(dt) || dt<0 ||
        !isfinite(desired.x) || !isfinite(desired.y) || !isfinite(desired.z))return false;
    if(dt==0)return true;
    /* Subpasos acotados evitan saltarse un apoyo al pausar la ventana. */
    if(dt>.025f) {
        float remaining=fminf(dt,.25f);
        while(remaining>0) { float step=fminf(.025f,remaining); Locomotion_Update(l,rig,g,world,desired,step); remaining-=step; }
        return true;
    }
    desired.y=0;
    float maxSpeed=g->strideLength/(g->cycleDuration*g->stanceRatio),speed=Vec3_Length(desired);
    if(speed>maxSpeed) desired=Vec3_Scale(desired,maxSpeed/speed);
    l->velocity=Vec3_Lerp(l->velocity,desired,1-expf(-10*dt));
    speed=Vec3_Length(l->velocity);
    if(speed<.0001f)l->velocity=Vec3_Zero();
    l->bodyPosition=Vec3_Add(l->bodyPosition,Vec3_Scale(l->velocity,dt));
    float activity=fminf(1,speed/maxSpeed);
    l->phase=fmodf(l->phase+dt*activity/g->cycleDuration,1);
    SkeletonPose rest; SkeletonPose_Init(&rest,&rig->skeleton);
    for(size_t i=0;i<rig->limbCount;++i) {
        const LimbRig* limb=&rig->limbs[i]; if(!limb->walking)continue;
        FootState* f=&l->feet[i];
        float phase=fmodf(l->phase+f->phaseOffset,1);
        bool window=phase>=g->stanceRatio;
        if(f->phase==FOOT_STANCE && window && !f->wasSwingWindow && speed>.001f) {
            Vector3 home=Vec3_Add(l->bodyPosition,rest.joints[limb->endEffectorJoint].worldPosition);
            Vector3 query=Vec3_Add(home,Vec3_Scale(l->velocity,g->cycleDuration*(1-g->stanceRatio*.5f)));
            Vector3 target,normal;
            if(GroundTarget(world,query,limb->soleHeight,&target,&normal)) {
                f->phase=FOOT_SWING; f->swingStart=f->plantedPosition;
                f->swingTarget=target; f->normal=normal; f->swingProgress=0;
            }
        }
        f->wasSwingWindow=window;
        if(f->phase==FOOT_SWING) {
            f->swingProgress=fminf(1,f->swingProgress+dt/(g->cycleDuration*(1-g->stanceRatio)));
            float t=f->swingProgress,s=t*t*(3-2*t);
            f->targetPosition=Vec3_Lerp(f->swingStart,f->swingTarget,s);
            f->targetPosition.y+=sinf(t*3.14159265358979323846f)*g->stepHeight;
            if(t>=1) { f->phase=FOOT_STANCE; f->plantedPosition=f->swingTarget; f->targetPosition=f->plantedPosition; }
        } else f->targetPosition=f->plantedPosition;
    }
    return true;
}
