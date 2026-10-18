#include "IK.h"
#include <math.h>
static Vector3 AtDistance(Vector3 origin,Vector3 toward,float length,Vector3 fallback) {
    Vector3 d=Vec3_Sub(toward,origin);
    if (Vec3_LengthSq(d)<1e-16f) d=fallback;
    if (Vec3_LengthSq(d)<1e-16f) d=Vec3_Create(1,0,0);
    return Vec3_Add(origin,Vec3_Scale(Vec3_Normalize(d),length));
}
static Quaternion Aim(const SkeletonJoint* j,Vector3 rest,Vector3 direction,Quaternion current) {
    /* Una bisagra se resuelve en su plano; extraer twist de FromTo perdería
     * parte del ángulo cuando el objetivo queda fuera del plano. */
    if (j->constraint.type==JOINT_HINGE) {
        Vector3 axis=Vec3_Normalize(j->constraint.hingeAxis);
        direction=Quat_RotateVector(Quat_Inverse(j->restRotation),direction);
        rest=Vec3_Sub(rest,Vec3_Scale(axis,Vec3_Dot(rest,axis)));
        direction=Vec3_Sub(direction,Vec3_Scale(axis,Vec3_Dot(direction,axis)));
        if (Vec3_LengthSq(rest)<1e-12f || Vec3_LengthSq(direction)<1e-12f) return current;
        float a=atan2f(Vec3_Dot(axis,Vec3_Cross(rest,direction)),Vec3_Dot(rest,direction));
        return Skeleton_ConstrainRotation(j,Quat_Multiply(j->restRotation,Quat_FromAxisAngle(axis,a)));
    }
    Quaternion delta=Quat_FromTo(Quat_RotateVector(current,rest),direction);
    return Skeleton_ConstrainRotation(j,Quat_Multiply(delta,current));
}
/** @brief Actualiza sólo la cadena durante las iteraciones; publica sus ramas al finalizar. */
static void UpdateChain(const Skeleton* s,SkeletonPose* p,const IKChain* c,size_t start) {
    for(size_t i=start;i<c->jointCount;++i) {
        int j=c->jointIndices[i],parent=s->joints[j].parentIndex;
        JointPose* pose=&p->joints[j];
        pose->localRotation=Quat_Normalize(pose->localRotation);
        if(parent<0) {pose->worldPosition=pose->localPosition;pose->worldRotation=pose->localRotation;}
        else {
            const JointPose* a=&p->joints[parent];
            pose->worldPosition=Vec3_Add(a->worldPosition,Quat_RotateVector(a->worldRotation,pose->localPosition));
            pose->worldRotation=Quat_Normalize(Quat_Multiply(a->worldRotation,pose->localRotation));
        }
    }
}
IKResult IK_SolveFABRIK(const Skeleton* s,SkeletonPose* p,const IKChain* c) {
    IKResult result={0};
    if (!s || !p || !c || s->jointCount>SKELETON_MAX_JOINTS || c->jointCount<2 || c->jointCount>IK_MAX_CHAIN_JOINTS ||
        c->maxIterations<0 || !isfinite(c->tolerance) || c->tolerance<0 ||
        !isfinite(c->targetPosition.x) || !isfinite(c->targetPosition.y) || !isfinite(c->targetPosition.z) ||
        p->jointCount!=s->jointCount) return result;
    size_t n=c->jointCount;
    for (size_t i=0;i<n;++i) {
        int j=c->jointIndices[i];
        if (j<0 || j>=(int)s->jointCount || (i && s->joints[j].parentIndex!=c->jointIndices[i-1])) return result;
    }
    if (c->endEffectorJoint!=c->jointIndices[n-1] || !SkeletonPose_UpdateWorldTransforms(p,s)) return result;
    Vector3 positions[IK_MAX_CHAIN_JOINTS]; float lengths[IK_MAX_CHAIN_JOINTS];
    Quaternion best[IK_MAX_CHAIN_JOINTS];
    Vector3 root=p->joints[c->jointIndices[0]].worldPosition;
    result.valid=true;
    result.error=Vec3_Distance(p->joints[c->endEffectorJoint].worldPosition,c->targetPosition);
    if(!isfinite(result.error))return (IKResult){0};
    for(size_t i=0;i<n;++i) best[i]=p->joints[c->jointIndices[i]].localRotation;
    int cap=c->maxIterations>256?256:c->maxIterations;
    for (int iter=0;iter<cap && result.error>c->tolerance;++iter) {
        for (size_t i=0;i<n;++i) {
            positions[i]=p->joints[c->jointIndices[i]].worldPosition;
            lengths[i]=Vec3_Length(p->joints[c->jointIndices[i]].localPosition);
        }
        positions[n-1]=c->targetPosition;
        for (size_t i=n-1;i>0;--i)
            positions[i-1]=AtDistance(positions[i],positions[i-1],lengths[i],Vec3_Create(1,0,0));
        positions[0]=root;
        /* Pasada hacia fuera: proyectar cada rotación y usar la posición REAL
         * resultante para el siguiente segmento. Así restricciones y FK coinciden. */
        for (size_t i=0;i+1<n;++i) {
            int j=c->jointIndices[i],child=c->jointIndices[i+1],parent=s->joints[j].parentIndex;
            JointPose* jp=&p->joints[j];
            Quaternion pr=parent<0?Quat_Identity():p->joints[parent].worldRotation;
            jp->worldPosition=positions[i];
            Vector3 direction=Quat_RotateVector(Quat_Inverse(pr),Vec3_Sub(positions[i+1],positions[i]));
            jp->localRotation=Aim(&s->joints[j],p->joints[child].localPosition,direction,jp->localRotation);
            jp->worldRotation=Quat_Normalize(Quat_Multiply(pr,jp->localRotation));
            positions[i+1]=Vec3_Add(positions[i],Quat_RotateVector(jp->worldRotation,p->joints[child].localPosition));
        }
        UpdateChain(s,p,c,0);
        /* Corrección angular del residuo tras proyectar FABRIK. La proyección
         * de una bisagra puede estancar la pasada posicional fuera de su plano;
         * esta relajación usa el efector real y mantiene todas las restricciones. */
        for (size_t k=n-1;k>0;--k) {
            int j=c->jointIndices[k-1],parent=s->joints[j].parentIndex;
            JointPose* jp=&p->joints[j];
            Quaternion pr=parent<0?Quat_Identity():p->joints[parent].worldRotation;
            Vector3 from=Quat_RotateVector(Quat_Inverse(pr),Vec3_Sub(p->joints[c->endEffectorJoint].worldPosition,jp->worldPosition));
            Vector3 to=Quat_RotateVector(Quat_Inverse(pr),Vec3_Sub(c->targetPosition,jp->worldPosition));
            Quaternion delta;
            if(s->joints[j].constraint.type==JOINT_HINGE) {
                Vector3 axis=Quat_RotateVector(s->joints[j].restRotation,s->joints[j].constraint.hingeAxis);
                from=Vec3_Sub(from,Vec3_Scale(axis,Vec3_Dot(from,axis)));
                to=Vec3_Sub(to,Vec3_Scale(axis,Vec3_Dot(to,axis)));
                delta=Quat_FromAxisAngle(axis,atan2f(Vec3_Dot(axis,Vec3_Cross(from,to)),Vec3_Dot(from,to)));
            } else delta=Quat_FromTo(from,to);
            jp->localRotation=Skeleton_ConstrainRotation(&s->joints[j],Quat_Multiply(delta,jp->localRotation));
            UpdateChain(s,p,c,k-1);
        }
        float error=Vec3_Distance(p->joints[c->endEffectorJoint].worldPosition,c->targetPosition);
        result.iterations=iter+1;
        if (error<result.error) {
            result.error=error;
            for(size_t i=0;i<n;++i) best[i]=p->joints[c->jointIndices[i]].localRotation;
        }
    }
    for(size_t i=0;i<n;++i) p->joints[c->jointIndices[i]].localRotation=best[i];
    SkeletonPose_UpdateSubtree(p,s,c->jointIndices[0]);
    result.converged=result.error<=c->tolerance;
    return result;
}

IKResult IK_SolveDigitigrade(const Skeleton* s,SkeletonPose* p,const IKChain* c,Vector3 distal,float dt) {
    if(!s||!p||!c||s->jointCount>SKELETON_MAX_JOINTS||c->maxIterations<0||c->jointCount!=4||
        !isfinite(distal.x)||!isfinite(distal.y)||!isfinite(distal.z)||
        Vec3_LengthSq(distal)<1e-12f||!isfinite(dt)||dt<0)return (IKResult){0};
    if(c->maxIterations==0)return IK_SolveFABRIK(s,p,c);
    int wrist=c->jointIndices[2],foot=c->jointIndices[3];
    if(wrist<0||foot<0||wrist>=(int)s->jointCount||foot>=(int)s->jointCount||
        s->joints[foot].parentIndex!=wrist||c->endEffectorJoint!=foot)return (IKResult){0};
    distal=Vec3_Scale(Vec3_Normalize(distal),Vec3_Length(s->joints[foot].restPosition));
    IKChain proximal=*c;proximal.jointCount=3;proximal.endEffectorJoint=wrist;
    proximal.targetPosition=Vec3_Sub(c->targetPosition,distal);
    int budget=proximal.maxIterations;proximal.maxIterations=0;
    IKResult result=IK_SolveFABRIK(s,p,&proximal);
    if(!result.valid)return result;
    Quaternion previous[3];
    for(size_t i=0;i<3;++i)previous[i]=p->joints[c->jointIndices[i]].localRotation;
    int root=c->jointIndices[0],knee=c->jointIndices[1],rootParent=s->joints[root].parentIndex;
    Quaternion frame=rootParent>=0?p->joints[rootParent].worldRotation:Quat_Identity();
    Quaternion restRoot=s->joints[root].restRotation;
    Vector3 upper=Quat_RotateVector(restRoot,s->joints[knee].restPosition);
    Vector3 lower=Quat_RotateVector(Quat_Multiply(restRoot,s->joints[knee].restRotation),s->joints[wrist].restPosition);
    Vector3 plane=Vec3_Cross(upper,lower);
    float a=Vec3_Length(upper),b=Vec3_Length(lower);
    Vector3 target=Quat_RotateVector(Quat_Inverse(frame),Vec3_Sub(proximal.targetPosition,p->joints[root].worldPosition));
    float distance=Vec3_Length(target);
    if(a>1e-6f&&b>1e-6f&&distance>1e-6f&&Vec3_LengthSq(plane)>1e-12f) {
        Vector3 direction=Vec3_Scale(target,1/distance);
        Vector3 normal=Vec3_Normalize(Vec3_Sub(plane,Vec3_Scale(direction,Vec3_Dot(plane,direction))));
        float d=fmaxf(fabsf(a-b)+1e-5f,fminf(a+b-1e-5f,distance));
        float along=(a*a-b*b+d*d)/(2*d);
        float height=sqrtf(fmaxf(0,a*a-along*along));
        Vector3 bent=Vec3_Add(Vec3_Scale(direction,along),Vec3_Scale(Vec3_Cross(direction,normal),height));
        Quaternion aim=Quat_FromTo(upper,bent);
        Quaternion align=Quat_FromTo(Quat_RotateVector(aim,Vec3_Normalize(plane)),normal);
        p->joints[root].localRotation=Skeleton_ConstrainRotation(&s->joints[root],Quat_Multiply(Quat_Multiply(align,aim),restRoot));
        UpdateChain(s,p,c,0);
        Vector3 kneeDirection=Quat_RotateVector(Quat_Inverse(p->joints[root].worldRotation),
            Vec3_Sub(proximal.targetPosition,p->joints[knee].worldPosition));
        p->joints[knee].localRotation=Aim(&s->joints[knee],s->joints[wrist].restPosition,kneeDirection,p->joints[knee].localRotation);
        UpdateChain(s,p,c,1);
    } else {
        proximal.maxIterations=budget;result=IK_SolveFABRIK(s,p,&proximal);
    }
    int parent=s->joints[wrist].parentIndex;
    Vector3 direction=Quat_RotateVector(Quat_Inverse(p->joints[parent].worldRotation),
        Vec3_Sub(c->targetPosition,p->joints[wrist].worldPosition));
    p->joints[wrist].localRotation=Aim(&s->joints[wrist],p->joints[foot].localPosition,direction,p->joints[wrist].localRotation);
    float blend=dt>0?1-expf(-8*dt):1;
    for(size_t i=0;i<3;++i) {
        int j=c->jointIndices[i];
        p->joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],Quat_Slerp(previous[i],p->joints[j].localRotation,blend));
    }
    if(!SkeletonPose_UpdateSubtree(p,s,root))return (IKResult){0};
    /* La orientación distal es una preferencia de postura. La cadena completa
     * corrige después el contacto, respetando los límites de las bisagras. */
    IKResult contact=IK_SolveFABRIK(s,p,c);
    contact.iterations+=result.iterations;
    return contact;
}
