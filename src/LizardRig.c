#include "LizardRig.h"
#include "Monster.h"
#include <math.h>
#define LIZARD_JAW_JOINT_ID 0xffff0001u
static JointConstraint Ball(Vector3 axis,float swing,float twist) {
    return (JointConstraint){.type=JOINT_BALL,.hingeAxis=axis,.maxSwingAngle=swing,.maxTwistAngle=twist};
}
bool LizardRig_Build(const Monster* m,Rig* r) {
    if(!m || !r || !m->hasAnatomyGraph || !RigBuilder_FromAnatomy(&m->anatomyGraph,ANATOMY_ID_PELVIS,r))return false;
    Skeleton* s=&r->skeleton;
    r->pelvisJoint=Skeleton_FindJoint(s,ANATOMY_ID_PELVIS);
    r->headJoint=Skeleton_FindJoint(s,ANATOMY_ID_HEAD);
    r->neckJoint=Skeleton_FindJoint(s,ANATOMY_ID_NECK);
    if(r->pelvisJoint<0 || r->headJoint<0 || r->neckJoint<0)return false;
    const AnatomyId spine[]={ANATOMY_ID_ABDOMEN,ANATOMY_ID_THORAX_POSTERIOR,ANATOMY_ID_THORAX_ANTERIOR,ANATOMY_ID_PECTORAL};
    const AnatomyId tail[]={ANATOMY_ID_TAIL_BASE,ANATOMY_ID_TAIL_MIDDLE,ANATOMY_ID_TAIL_DISTAL,ANATOMY_ID_TAIL_TIP};
    for(size_t i=0;i<4;++i) {
        r->spine[r->spineCount++]=Skeleton_FindJoint(s,spine[i]);
        r->tail[r->tailCount++]=Skeleton_FindJoint(s,tail[i]);
        if(r->spine[i]<0 || r->tail[i]<0)return false;
        s->joints[r->spine[i]].constraint=Ball(Vec3_Create(0,0,1),.12f,.08f);
        s->joints[r->tail[i]].constraint=Ball(Vec3_Create(0,0,1),.3f,.12f);
    }
    s->joints[r->headJoint].constraint=Ball(Vec3_Create(0,0,1),.35f,.15f);
    s->joints[r->neckJoint].constraint=Ball(Vec3_Create(0,0,1),.25f,.12f);
    AnatomyId bases[]={ANATOMY_ID_FORE_LEFT_SHOULDER,ANATOMY_ID_FORE_RIGHT_SHOULDER,
        ANATOMY_ID_HIND_LEFT_HIP,ANATOMY_ID_HIND_RIGHT_HIP};
    for(size_t i=0;i<4;++i) {
        AnatomyId ids[]={bases[i],bases[i]+1,bases[i]+2,bases[i]+3};
        if(!RigBuilder_AddLimb(r,ids,4,i<2?LIMB_FORE:LIMB_HIND,i%2?LIMB_RIGHT:LIMB_LEFT,
            !m->hasLizardPhenotype || m->lizardPhenotype.appendageDevelopment>.05f))return false;
        LimbRig* limb=&r->limbs[i];
        int a=limb->chain.jointIndices[0],b=limb->chain.jointIndices[1],c=limb->chain.jointIndices[2];
        Vector3 upper=s->joints[b].restPosition,lower=s->joints[c].restPosition;
        Vector3 axis=Vec3_Normalize(Vec3_Cross(upper,lower));
        float bend=acosf(fmaxf(-1,fminf(1,Vec3_Dot(Vec3_Normalize(upper),Vec3_Normalize(lower)))));
        s->joints[a].constraint=Ball(Vec3_Normalize(upper),.85f,.65f);
        /* El ángulo cero es la anatomía ya flexionada. El intervalo impide
         * atravesar la extensión recta y cambiar de lado el codo/rodilla. */
        s->joints[b].constraint=(JointConstraint){.type=JOINT_HINGE,.hingeAxis=axis,
            .minAngle=-fmaxf(0,bend-.15f),.maxAngle=fmaxf(0,2.95f-bend)};
        s->joints[c].constraint=Ball(Vec3_Normalize(s->joints[limb->endEffectorJoint].restPosition),.55f,.25f);
        s->joints[limb->endEffectorJoint].constraint=Ball(Vec3_Create(0,1,0),.65f,1.0f);
        const AnatomyNode* node=AnatomyGraph_FindNode(&m->anatomyGraph,ids[3]);
        limb->soleHeight=node->heightRadius;
        /* La suela incluye la extensión real de los dedos en reposo. */
        for(unsigned digit=0;digit<5;++digit)for(unsigned station=0;station<7;++station) {
            const AnatomyNode* toe=AnatomyGraph_FindNode(&m->anatomyGraph,Anatomy_DigitId((unsigned)i,digit,station));
            if(toe)limb->soleHeight=fmaxf(limb->soleHeight,node->center.y-toe->center.y+toe->heightRadius);
        }
    }
    if(m->hasHead && m->mouthCount) {
        const Mouth* mouth=&m->mouths[0];
        r->jawJoint=(int)s->jointCount++;
        const AnatomyNode* head=AnatomyGraph_FindNode(&m->anatomyGraph,ANATOMY_ID_HEAD);
        Vector3 host=mouth->bodyPartIndex<m->bodyPartCount?m->bodyParts[mouth->bodyPartIndex].positionRender:Vec3_Zero();
        Vector3 pivot=Vec3_Add(host,Vec3_Add(mouth->offset,mouth->jawPivot));
        r->maxJawAngle=mouth->maxJawAngle * .017453292519943295f;
        s->joints[r->jawJoint]=(SkeletonJoint){.id=LIZARD_JAW_JOINT_ID,.anatomyId=ANATOMY_ID_HEAD,
            .parentIndex=r->headJoint,.restPosition=Vec3_Sub(pivot,head->center),.restRotation=Quat_Identity(),
            .constraint={.type=JOINT_HINGE,.hingeAxis={1,0,0},.minAngle=0,.maxAngle=r->maxJawAngle}};
    }
    return Skeleton_Validate(s);
}
