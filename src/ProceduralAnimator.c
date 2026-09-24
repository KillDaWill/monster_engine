#include "ProceduralAnimator.h"
#include <math.h>
#include <string.h>
bool ProceduralAnimator_Init(ProceduralAnimator* a,const Rig* rig,GaitProfile gait,World* world,Vector3 position) {
    if(!a)return false;
    memset(a,0,sizeof(*a)); a->gait=gait; a->lookDirection=Vec3_Create(0,0,1);
    return Locomotion_Init(&a->locomotion,rig,&gait,world,position);
}
static void Rotate(const Skeleton* s,SkeletonPose* p,int j,Vector3 axis,float angle) {
    if(j<0 || j>=(int)s->jointCount)return;
    p->joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],
        Quat_Multiply(s->joints[j].restRotation,Quat_FromAxisAngle(axis,angle)));
}
static void AxialControllers(ProceduralAnimator* a,const Rig* r,SkeletonPose* p) {
    float wave=a->locomotion.phase*6.28318530718f;
    float activity=fminf(1,Vec3_Length(a->locomotion.velocity)*a->gait.cycleDuration/a->gait.strideLength);
    for(size_t i=0;i<r->spineCount;++i) Rotate(&r->skeleton,p,r->spine[i],Vec3_Create(0,1,0),
        a->gait.spineWave*activity*sinf(wave-(float)i*.45f));
    for(size_t t=0;t<r->tailCount;++t) {
        for(size_t i=0;i<r->tails[t].jointCount;++i) {
            Rotate(&r->skeleton,p,r->tails[t].joints[i],Vec3_Create(0,1,0),
                -a->gait.tailCounterSwing*activity*sinf(wave-(float)i*.65f));
        }
    }
    int head=r->headJoint;
    if(head>=0) {
        Quaternion look=Quat_FromTo(Vec3_Create(0,0,1),a->lookDirection);
        if(r->neckJoint>=0)p->joints[r->neckJoint].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[r->neckJoint],Quat_Slerp(Quat_Identity(),look,.4f));
        p->joints[head].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[head],Quat_Slerp(Quat_Identity(),look,.6f));
    }
    if(r->jawJoint>=0)Rotate(&r->skeleton,p,r->jawJoint,r->skeleton.joints[r->jawJoint].constraint.hingeAxis,
        fmaxf(0,fminf(1,a->mouthOpen))*r->maxJawAngle);
}
bool ProceduralAnimator_Update(ProceduralAnimator* a,const Rig* r,SkeletonPose* p,World* world,float dt) {
    if(!a || !r || !p || !Locomotion_Update(&a->locomotion,r,&a->gait,world,a->desiredVelocity,dt) ||
        !SkeletonPose_ResetToRest(p,&r->skeleton))return false;
    float phase=a->locomotion.phase*6.28318530718f;
    float speed=fminf(1,Vec3_Length(a->locomotion.velocity));
    for(size_t j=0;j<r->skeleton.jointCount;++j) if(r->skeleton.joints[j].parentIndex<0) {
        p->joints[j].localPosition=Vec3_Add(p->joints[j].localPosition,a->locomotion.bodyPosition);
        p->joints[j].localPosition.y+=a->gait.bodyBob*speed*sinf(phase*2);
        p->joints[j].localPosition.x+=a->gait.bodySway*speed*sinf(phase);
    }
    AxialControllers(a,r,p);
    SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
    for(size_t i=0;i<r->limbCount;++i) {
        if(!r->limbs[i].walking)continue;
        IKChain c=r->limbs[i].chain; c.targetPosition=a->locomotion.feet[i].targetPosition;
        a->limbResults[i]=IK_SolveFABRIK(&r->skeleton,p,&c);
        int j=c.endEffectorJoint,parent=r->skeleton.joints[j].parentIndex;
        /* Orientación del autopodio independiente del objetivo posicional. */
        Quaternion orientation=Quat_FromTo(Vec3_Create(0,1,0),a->locomotion.feet[i].normal);
        Quaternion local=Quat_Multiply(Quat_Inverse(p->joints[parent].worldRotation),orientation);
        p->joints[j].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[j],local);
    }
    return SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
}
