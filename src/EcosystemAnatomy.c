/** @file EcosystemAnatomy.c
 * @brief Alcance cervical autoritativo derivado de recetas, sin SDF ni render. */
#include "EcosystemAnatomy.h"
#include "Creature.h"
#include "Monster.h"
#include "CreatureRecipes.h"
#include "VisualVariation.h"
#include <stdlib.h>
#include <math.h>

static Vector3 Yaw(Vector3 v,float a) {
    return Vec3_Create(cosf(a)*v.x+sinf(a)*v.z,v.y,-sinf(a)*v.x+cosf(a)*v.z);
}
float EcosystemAnatomy_ModelLength(const CreaturePhenotype* p) {
    return (p->axial.trunkLength+p->axial.neckLength+2.2f*p->headEnvelope.scale*p->headEnvelope.lengthScale)*p->axial.totalScale;
}
float EcosystemAnatomy_Scale(const EcosystemAnatomy* a,const EcosystemEntity* e) {
    return 2.05f/a->modelLength*e->genome.size*e->developmentScale;
}
bool EcosystemAnatomy_Configure(EcosystemEntity* e,const EcosystemSpecies* s) {
    if(!s->anatomicalOral)return true;
    unsigned stage=e->physiology.stage==LIFE_JUVENILE?CREATURE_STAGE_JUVENILE:CREATURE_STAGE_ADULT;
    unsigned bucket=VisualVariation_Profile(&e->genome,e->seed).bucket;
    EcosystemAnatomy* a=e->anatomicalBody;
    double maturity=s->physiology.maturityAge*e->genome.maturation;
    double sample=e->physiology.age>=maturity?maturity:floor(e->physiology.age*2)*.5;
    if(a&&a->stage==stage&&a->bucket==bucket&&a->growthSample==sample)return true;
    const CreatureRecipe* base=CreatureRecipes_Find(s->recipeId);
    if(!base)return false;
    CreatureRecipe recipe;CreaturePhenotype p;
    CreaturePhenotype development=CreatureRecipe_EvaluateDevelopment(base,(float)fmin(1,sample/maturity));
    if(!VisualVariation_ApplyBucket(base,&development,bucket,0,&recipe,&p))return false;
    Monster m=Monster_Create();
    if(!Creature_BuildMonster(&m,&recipe,&p)||!m.animation) {Monster_Free(&m);return false;}
    EcosystemAnatomy* next=calloc(1,sizeof(*next));
    if(!next){Monster_Free(&m);return false;}
    next->rig=m.animation->rig;next->standingHeight=m.animation->standingHeight-m.animation->animator.gait.supportCompression;
    next->modelLength=EcosystemAnatomy_ModelLength(&p);
    next->center=m.animation->animator.locomotion.supportCenter;
    next->stage=stage;next->bucket=bucket;
    next->growthSample=sample;next->generation=a?a->generation+1:1;
    next->phenotype=p;next->graph=m.anatomyGraph;
    next->pose=m.animation->pose;
    SkeletonPose_Init(&next->rest,&next->rig.skeleton);
    ReachPreset preset=AnatomicalReach_DefaultPreset();preset.torsoJoints=0;preset.crouchFraction=.25f;
    bool ok=AnatomicalReach_Compile(&next->profile,&next->rig,preset);
    Monster_Free(&m);
    if(!ok){free(next);return false;}
    if(a) {
        next->crouch=a->crouch;
        for(unsigned k=0;k<next->profile.count;++k) {
            int j=next->profile.joints[k].index;
            next->pose.joints[j].localRotation=a->pose.joints[j].localRotation;
        }
        AnatomicalReach_Reset(&next->state,&next->profile,&next->pose);
    }
    free(a);e->anatomicalBody=next;return true;
}
void EcosystemAnatomy_Update(EcosystemEntity* e,float dt) {
    EcosystemAnatomy* a=e->anatomicalBody;if(!a)return;
    float scale=EcosystemAnatomy_Scale(a,e);
    SkeletonPose_ResetToRest(&a->pose,&a->rig.skeleton);
    for(size_t j=0;j<a->rig.skeleton.jointCount;++j)if(a->rig.skeleton.joints[j].parentIndex<0)
        a->pose.joints[j].localPosition.y+=a->standingHeight;
    SkeletonPose_UpdateWorldTransforms(&a->pose,&a->rig.skeleton);
    InteractionTarget target=e->mind.intent.interaction;
    bool active=e->mind.intent.interact&&target.valid;
    if(active) {
        target.position=Vec3_Add(a->center,Vec3_Scale(Yaw(Vec3_Sub(target.position,e->body.position),-e->body.yaw),1/scale));
        target.approach=Yaw(target.approach,-e->body.yaw);target.normal=Yaw(target.normal,-e->body.yaw);
        target.positionTolerance/=scale;
    }
    float desiredCrouch=e->mind.action==ACTION_REST?a->profile.maximumCrouch*.45f:0;
    if(active) {
        int base=a->profile.joints[0].index;
        Vector3 delta=Vec3_Sub(a->pose.joints[base].worldPosition,target.position);
        float reach=a->profile.maximumReach*.85f,planar=delta.x*delta.x+delta.z*delta.z;
        if(planar<reach*reach)
            desiredCrouch=fmaxf(0,delta.y-sqrtf(reach*reach-planar)+.04f);
    }
    desiredCrouch=fminf(a->profile.maximumCrouch,desiredCrouch);
    a->crouch+=(desiredCrouch-a->crouch)*(1-expf(-5*dt));
    for(size_t j=0;j<a->rig.skeleton.jointCount;++j)if(a->rig.skeleton.joints[j].parentIndex<0)
        a->pose.joints[j].localPosition.y-=a->crouch;
    SkeletonPose_UpdateWorldTransforms(&a->pose,&a->rig.skeleton);
    ReachResult reach=AnatomicalReach_Update(&a->profile,&a->state,&a->rig.skeleton,&a->pose,active?&target:NULL,dt);
    const JointPose* head=&a->pose.joints[a->rig.headJoint];
    Vector3 point=Vec3_Add(head->worldPosition,Quat_RotateVector(head->worldRotation,a->rig.oralPoint));
    Vector3 mouth=Vec3_Add(e->body.position,Yaw(Vec3_Scale(Vec3_Sub(point,a->center),scale),e->body.yaw));
    e->oralMotion=dt>0?Vec3_Distance(mouth,e->body.mouth)/dt:0;
    e->body.mouth=mouth;
    e->body.mouthDirection=Yaw(Quat_RotateVector(head->worldRotation,a->rig.oralDirection),e->body.yaw);
    int base=a->rig.cervicalBaseJoint>=0?a->rig.cervicalBaseJoint:a->rig.neckJoint;
    e->oralPivot=Vec3_Add(e->body.position,Yaw(Vec3_Scale(Vec3_Sub(a->pose.joints[base].worldPosition,a->center),scale),e->body.yaw));
    e->oralElbow=Vec3_Add(e->body.position,Yaw(Vec3_Scale(Vec3_Sub(a->pose.joints[a->rig.neckJoint].worldPosition,a->center),scale),e->body.yaw));
    reach.mouth=mouth;reach.direction=e->body.mouthDirection;reach.error*=scale;
    e->body.reach=reach;
    if(active)e->body.reach.objectId=target.objectId;
}
