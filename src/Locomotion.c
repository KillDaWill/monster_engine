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
void Locomotion_TurnFrame(Locomotion* l,float yawDelta) {
    if(!l || !l->initialized || !isfinite(yawDelta))return;
    Quaternion inverse=Quat_FromAxisAngle(Vec3_Create(0,1,0),-yawDelta);
    Vector3 pivot=Vec3_Add(l->bodyPosition,l->supportCenter);
    for(size_t i=0;i<RIG_MAX_LIMBS;++i) {
        FootState* f=&l->feet[i];
        Vector3* points[]={&f->plantedPosition,&f->swingStart,&f->swingTarget,&f->targetPosition};
        for(size_t k=0;k<4;++k)
            *points[k]=Vec3_Add(pivot,Quat_RotateVector(inverse,Vec3_Sub(*points[k],pivot)));
        f->normal=Quat_RotateVector(inverse,f->normal);
    }
}
bool Locomotion_Init(Locomotion* state,const Rig* rig,const GaitProfile* gait,World* world,Vector3 body) {
    if(!state || !rig || rig->limbCount>RIG_MAX_LIMBS || !ValidGait(gait))return false;
    SkeletonPose rest; if(!SkeletonPose_Init(&rest,&rig->skeleton))return false;
    Locomotion l={0}; l.bodyPosition=body;size_t walking=0;
    for(size_t i=0;i<rig->limbCount;++i) {
        const LimbRig* limb=&rig->limbs[i]; FootState* f=&l.feet[i];
        if(limb->endEffectorJoint<0 || limb->endEffectorJoint>=(int)rest.jointCount)return false;
        Vector3 q=Vec3_Add(body,rest.joints[limb->endEffectorJoint].worldPosition);
        f->plantedPosition=q; f->normal=Vec3_Create(0,1,0);
        if(limb->walking) GroundTarget(world,q,limb->soleHeight,&f->plantedPosition,&f->normal);
        f->targetPosition=f->plantedPosition; f->phaseOffset=gait->limbPhase[i];
        if(limb->walking) {
            l.supportCenter=Vec3_Add(l.supportCenter,rest.joints[limb->endEffectorJoint].worldPosition);
            ++walking;
        }
    }
    if(walking)l.supportCenter=Vec3_Scale(l.supportCenter,1.f/(float)walking);
    l.supportCenter.y=0;
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
    SkeletonPose rest; SkeletonPose_Init(&rest,&rig->skeleton);
    /* También se pisa al pivotar: la pata exterior recorre un arco mayor.
     * Usar el radio respecto al centro de apoyos, sin atenuar omega. */
    float contactSpeed=speed;
    for(size_t i=0;i<rig->limbCount;++i)if(rig->limbs[i].walking) {
        Vector3 rel=Vec3_Sub(rest.joints[rig->limbs[i].endEffectorJoint].worldPosition,l->supportCenter);
        Vector3 v=Vec3_Add(l->velocity,Vec3_Create(l->angularVelocity*rel.z,0,-l->angularVelocity*rel.x));
        contactSpeed=fmaxf(contactSpeed,Vec3_Length(v));
    }
    float activity=fminf(1,contactSpeed/maxSpeed);
    l->phase=fmodf(l->phase+dt*activity/g->cycleDuration,1);

    for(size_t i=0;i<rig->limbCount;++i) {
        const LimbRig* limb=&rig->limbs[i]; if(!limb->walking)continue;
        FootState* f=&l->feet[i];
        float phase=fmodf(l->phase+f->phaseOffset,1);
        bool window=phase>=g->stanceRatio;
        Vector3 restPos=rest.joints[limb->endEffectorJoint].worldPosition;
        Vector3 home=Vec3_Add(l->bodyPosition,restPos);
        float length=0;
        for(size_t joint=1;joint<limb->chain.jointCount;++joint)
            length+=Vec3_Length(rig->skeleton.joints[limb->chain.jointIndices[joint]].restPosition);
        float vertical=rest.joints[limb->rootJoint].worldPosition.y+l->bodyPosition.y-g->supportCompression-limb->soleHeight;
        float reserve=sqrtf(fmaxf(.0001f,length*length-vertical*vertical))*.60f;
        float travelLimit=fmaxf(g->stepHeight,fminf(g->strideLength*.65f,reserve));
        Vector3 offset=Vec3_Sub(f->plantedPosition,home);offset.y=0;
        /* Un cambio de fase o un giro rápido puede agotar el alcance antes
         * de abrirse la ventana nominal. Levantar el pie conserva el contacto
         * previo sin deslizarlo ni pedir a la IK una cadena imposible. */
        bool release=Vec3_Length(offset)>travelLimit;
        if(f->phase==FOOT_STANCE && ((window && !f->wasSwingWindow)||release) && contactSpeed>.001f) {
            Vector3 rel=Vec3_Sub(restPos,l->supportCenter);
            Vector3 footVel=l->velocity;
            if(fabsf(l->angularVelocity)>.0001f) {
                footVel.x+=l->angularVelocity*rel.z;
                footVel.z-=l->angularVelocity*rel.x;
            }
            Vector3 stepOffset=Vec3_Scale(footVel,g->cycleDuration*g->stanceRatio*.5f);
            float stepLen=Vec3_Length(stepOffset);
            float maximumStep=fminf(g->strideLength,travelLimit);
            if(stepLen>maximumStep) stepOffset=Vec3_Scale(stepOffset,maximumStep/stepLen);
            Vector3 query=Vec3_Add(home,stepOffset);
            Vector3 target,normal;
            if(GroundTarget(world,query,limb->soleHeight,&target,&normal)) {
                f->phase=FOOT_SWING; f->swingStart=f->plantedPosition;
                f->swingTarget=target; f->normal=normal; f->swingProgress=0;
            }
        }
        f->wasSwingWindow=window;
        if(f->phase==FOOT_SWING) {
            f->swingProgress=fminf(1,f->swingProgress+dt/(g->cycleDuration*(1-g->stanceRatio)));
            float t=f->swingProgress,s=t*t*t*(10+t*(-15+6*t));
            f->targetPosition=Vec3_Lerp(f->swingStart,f->swingTarget,s);
            /* Velocidad y aceleración nulas al despegar y apoyar. */
            float lift=4*t*(1-t);
            f->targetPosition.y+=lift*lift*lift*g->stepHeight;
            if(t>=1) { f->phase=FOOT_STANCE; f->plantedPosition=f->swingTarget; f->targetPosition=f->plantedPosition; }
        } else f->targetPosition=f->plantedPosition;
    }
    return true;
}
