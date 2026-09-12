#include "Skeleton.h"
#include <math.h>
#include <string.h>

bool Skeleton_Validate(const Skeleton* s) {
    if (!s || !s->jointCount || s->jointCount>SKELETON_MAX_JOINTS) return false;
    for (size_t i=0;i<s->jointCount;++i) {
        const SkeletonJoint* j=&s->joints[i];
        if (j->parentIndex < -1 || j->parentIndex >= (int)i || !j->id ||
            !isfinite(j->restPosition.x) || !isfinite(j->restPosition.y) || !isfinite(j->restPosition.z)) return false;
        Quaternion q=j->restRotation;
        const JointConstraint* c=&j->constraint;
        if(!isfinite(q.x)||!isfinite(q.y)||!isfinite(q.z)||!isfinite(q.w)||
           q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w<1e-12f ||
           c->type<JOINT_FIXED || c->type>JOINT_BALL ||
           !isfinite(c->hingeAxis.x)||!isfinite(c->hingeAxis.y)||!isfinite(c->hingeAxis.z)||
           !isfinite(c->minAngle)||!isfinite(c->maxAngle)||c->minAngle>c->maxAngle||
           !isfinite(c->maxSwingAngle)||!isfinite(c->maxTwistAngle)||c->maxSwingAngle<0||c->maxTwistAngle<0)return false;
        for (size_t k=0;k<i;++k) if (s->joints[k].id==j->id) return false;
    }
    return true;
}
int Skeleton_FindJoint(const Skeleton* s, AnatomyId id) {
    if (!s || s->jointCount>SKELETON_MAX_JOINTS) return -1;
    for (size_t i=0;i<s->jointCount;++i) if (s->joints[i].id==id) return (int)i;
    return -1;
}
bool SkeletonPose_UpdateWorldTransforms(SkeletonPose* p, const Skeleton* s) {
    if (!p || !s || p->jointCount!=s->jointCount || p->jointCount>SKELETON_MAX_JOINTS) return false;
    for (size_t i=0;i<s->jointCount;++i) {
        int parent=s->joints[i].parentIndex;
        if (parent < -1 || parent >= (int)i) return false;
        JointPose* j=&p->joints[i];
        if(!isfinite(j->localPosition.x)||!isfinite(j->localPosition.y)||!isfinite(j->localPosition.z))return false;
        j->localRotation=Quat_Normalize(j->localRotation);
        if (parent<0) { j->worldPosition=j->localPosition; j->worldRotation=j->localRotation; }
        else {
            const JointPose* a=&p->joints[parent];
            j->worldPosition=Vec3_Add(a->worldPosition,Quat_RotateVector(a->worldRotation,j->localPosition));
            j->worldRotation=Quat_Normalize(Quat_Multiply(a->worldRotation,j->localRotation));
        }
        if(!isfinite(j->worldPosition.x)||!isfinite(j->worldPosition.y)||!isfinite(j->worldPosition.z))return false;
    }
    return true;
}
bool SkeletonPose_ResetToRest(SkeletonPose* p, const Skeleton* s) {
    if (!p || !Skeleton_Validate(s)) return false;
    p->jointCount=s->jointCount;
    for (size_t i=0;i<s->jointCount;++i) {
        p->joints[i].localPosition=s->joints[i].restPosition;
        p->joints[i].localRotation=s->joints[i].restRotation;
    }
    return SkeletonPose_UpdateWorldTransforms(p,s);
}
bool SkeletonPose_Init(SkeletonPose* p, const Skeleton* s) { return SkeletonPose_ResetToRest(p,s); }
void SkeletonPose_Copy(SkeletonPose* dst, const SkeletonPose* src) { if (dst && src) *dst=*src; }
static float Clamp(float a,float lo,float hi) { return fmaxf(lo,fminf(hi,a)); }
Quaternion Skeleton_ConstrainRotation(const SkeletonJoint* j, Quaternion rotation) {
    if (!j) return Quat_Identity();
    const JointConstraint* c=&j->constraint;
    if (c->type==JOINT_FIXED) return j->restRotation;
    Quaternion q=Quat_Normalize(Quat_Multiply(Quat_Inverse(j->restRotation),rotation));
    if (q.w<0) q=(Quaternion){-q.x,-q.y,-q.z,-q.w};
    Vector3 axis=Vec3_Normalize(c->hingeAxis);
    if (Vec3_LengthSq(axis)<.5f) axis=Vec3_Create(0,0,1);
    float projection=q.x*axis.x+q.y*axis.y+q.z*axis.z;
    Quaternion twist=Quat_Normalize((Quaternion){axis.x*projection,axis.y*projection,axis.z*projection,q.w});
    float angle=2*atan2f(twist.x*axis.x+twist.y*axis.y+twist.z*axis.z,twist.w);
    if (c->type==JOINT_HINGE) q=Quat_FromAxisAngle(axis,Clamp(angle,c->minAngle,c->maxAngle));
    else {
        Quaternion swing=Quat_Normalize(Quat_Multiply(q,Quat_Inverse(twist)));
        float a=2*acosf(Clamp(swing.w,-1,1));
        if (a>c->maxSwingAngle && a>1e-6f) swing=Quat_Slerp(Quat_Identity(),swing,fmaxf(0,c->maxSwingAngle)/a);
        twist=Quat_FromAxisAngle(axis,Clamp(angle,-c->maxTwistAngle,c->maxTwistAngle));
        q=Quat_Multiply(swing,twist);
    }
    return Quat_Normalize(Quat_Multiply(j->restRotation,q));
}
bool SkeletonPose_ToAnatomy(const Skeleton* s,const SkeletonPose* p,const AnatomyGraph* rest,AnatomyGraph* posed) {
    if (!s || !p || !rest || !posed || rest==posed || rest->nodeCount>ANATOMY_MAX_NODES || p->jointCount>SKELETON_MAX_JOINTS || p->jointCount!=s->jointCount) return false;
    *posed=*rest;
    for (size_t i=0;i<rest->nodeCount;++i) {
        int j=Skeleton_FindJoint(s,rest->nodes[i].id);
        if (j<0) return false;
        posed->nodes[i].center=p->joints[j].worldPosition;
    }
    return true;
}
