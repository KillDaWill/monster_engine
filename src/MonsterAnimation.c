#include "MonsterAnimation.h"
#include "Monster.h"
#include <stdlib.h>
#include <math.h>
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
        SkeletonPose oldRest;if(!SkeletonPose_Init(&oldRest,&m->animation->rig.skeleton)){free(next);return false;}
        next->animator.desiredVelocity=old->desiredVelocity;
        next->animator.angularVelocity=old->angularVelocity;
        next->animator.currentAngularVelocity=old->currentAngularVelocity;
        next->animator.lookDirection=old->lookDirection;
        next->animator.mouthOpen=old->mouthOpen;
        next->animator.reachCrouch=fminf(old->reachCrouch,next->animator.reachProfile.maximumCrouch);
        next->animator.reachCrouchVelocity=old->reachCrouchVelocity;
        next->animator.oralReachTime=old->oralReachTime;
        next->animator.oralReachActive=old->oralReachActive;
        next->animator.oralReachTarget=old->oralReachTarget;
        next->animator.interactionTarget=old->interactionTarget;
        next->animator.validateInteractionPose=old->validateInteractionPose;
        next->animator.interactionContext=old->interactionContext;
        for(int k=0;k<3;++k)next->animator.cervicalRotation[k]=old->cervicalRotation[k];
        next->animator.locomotion.phase=old->locomotion.phase;
        next->animator.locomotion.velocity=old->locomotion.velocity;
        next->animator.locomotion.angularVelocity=old->locomotion.angularVelocity;
        /* La morfología cambia el reposo, no el contacto ya plantado. Transportar
         * por identidad evita reiniciar apoyos y saltos al reconfigurar el rig. */
        for(size_t i=0;i<rig->limbCount;++i) {
            AnatomyId id=rig->skeleton.joints[rig->limbs[i].endEffectorJoint].anatomyId;
            for(size_t j=0;j<m->animation->rig.limbCount;++j) {
                int effector=m->animation->rig.limbs[j].endEffectorJoint;
                if(m->animation->rig.skeleton.joints[effector].anatomyId!=id)continue;
                if(Vec3_Distance(rest.joints[rig->limbs[i].endEffectorJoint].worldPosition,
                    oldRest.joints[effector].worldPosition)>gait.strideLength)break;
                next->animator.locomotion.feet[i]=old->locomotion.feet[j];
                next->animator.locomotion.feet[i].phaseOffset=gait.limbPhase[i];
                break;
            }
        }
    }
    /* Publicar inmediatamente una pose en el marco locomotor actual. Dejarla
     * en reposo mientras bodyPosition avanza desplaza la huella consultada por
     * navegación entre Configure y Update. Transportar rotaciones por identidad
     * conserva el gesto sin resolver otra vez la IK ni avanzar el reloj. */
    for(size_t j=0;j<rig->skeleton.jointCount;++j) {
        const SkeletonJoint* joint=&rig->skeleton.joints[j];
        Vector3 residual=Vec3_Zero();
        if(m->animation) {
            int previous=Skeleton_FindJoint(&m->animation->rig.skeleton,joint->id);
            if(previous>=0) {
                const SkeletonJoint* oldJoint=&m->animation->rig.skeleton.joints[previous];
                const JointPose* oldPose=&m->animation->pose.joints[previous];
                Quaternion delta=Quat_Multiply(Quat_Inverse(oldJoint->restRotation),oldPose->localRotation);
                next->pose.joints[j].localRotation=Skeleton_ConstrainRotation(joint,Quat_Multiply(joint->restRotation,delta));
                if(joint->parentIndex<0&&oldJoint->parentIndex<0)
                    residual=Vec3_Sub(Vec3_Sub(oldPose->localPosition,oldJoint->restPosition),m->animation->animator.locomotion.bodyPosition);
            }
        }
        if(joint->parentIndex<0)next->pose.joints[j].localPosition=Vec3_Add(joint->restPosition,Vec3_Add(origin,residual));
    }
    if(!SkeletonPose_UpdateWorldTransforms(&next->pose,&rig->skeleton)){free(next);return false;}
    AnatomicalReach_Reset(&next->animator.reachState,&next->animator.reachProfile,&next->pose);
    if(m->animation)for(unsigned k=0;k<next->animator.reachProfile.count;++k) {
        const ReachJoint *joint=&next->animator.reachProfile.joints[k];
        for(unsigned j=0;j<m->animation->animator.reachProfile.count;++j) {
            const ReachJoint *previous=&m->animation->animator.reachProfile.joints[j];
            if(joint->id==previous->id&&joint->bounded.constraint.type==previous->bounded.constraint.type)
                next->animator.reachState.velocity[k]=m->animation->animator.reachState.velocity[j];
        }
    }
    free(m->animation); m->animation=next; return true;
}
bool MonsterAnimation_Update(Monster* m,float dt) {
    if(!m || !m->animation || m->animation->restFingerprint!=AnatomyGraph_Fingerprint(&m->anatomyGraph))return false;
    return ProceduralAnimator_Update(&m->animation->animator,&m->animation->rig,&m->animation->pose,m->world,dt);
}
void MonsterAnimation_Free(MonsterAnimation* a) { free(a); }
