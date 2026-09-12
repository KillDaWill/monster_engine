#include "MonsterAnimation.h"
#include "Monster.h"
#include <stdlib.h>
bool MonsterAnimation_Configure(Monster* m,const Rig* rig,GaitProfile gait) {
    if(!m || !rig || !Skeleton_Validate(&rig->skeleton))return false;
    MonsterAnimation* next=calloc(1,sizeof(*next));
    if(!next)return false;
    next->rigGeneration=m->animation?m->animation->rigGeneration+1:1;
    next->rig=*rig; next->restFingerprint=AnatomyGraph_Fingerprint(&m->anatomyGraph);
    Vector3 origin=Vec3_Zero();
    SkeletonPose rest; SkeletonPose_Init(&rest,&rig->skeleton);
    float height=0;
    for(size_t i=0;i<rig->limbCount;++i)if(rig->limbs[i].walking) {
        float h=rig->limbs[i].soleHeight-rest.joints[rig->limbs[i].endEffectorJoint].worldPosition.y;
        if(h>height)height=h;
    }
    next->standingHeight=height;
    if(m->animation) {
        origin=m->animation->animator.locomotion.bodyPosition;
        origin.y+=height-m->animation->standingHeight;
    } else origin.y=height;
    if(!SkeletonPose_Init(&next->pose,&rig->skeleton) ||
        !ProceduralAnimator_Init(&next->animator,rig,gait,m->world,origin)) { free(next); return false; }
    if(m->animation) {
        const ProceduralAnimator* old=&m->animation->animator;
        next->animator.desiredVelocity=old->desiredVelocity;
        next->animator.lookDirection=old->lookDirection;
        next->animator.mouthOpen=old->mouthOpen;
        next->animator.locomotion.phase=old->locomotion.phase;
        next->animator.locomotion.velocity=old->locomotion.velocity;
    }
    free(m->animation); m->animation=next; return true;
}
bool MonsterAnimation_Update(Monster* m,float dt) {
    if(!m || !m->animation || m->animation->restFingerprint!=AnatomyGraph_Fingerprint(&m->anatomyGraph))return false;
    return ProceduralAnimator_Update(&m->animation->animator,&m->animation->rig,&m->animation->pose,m->world,dt);
}
void MonsterAnimation_Free(MonsterAnimation* a) { free(a); }
