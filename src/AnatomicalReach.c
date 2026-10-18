#include "AnatomicalReach.h"
#include <math.h>
#include <string.h>
static float clamp(float x,float a,float b) { return fmaxf(a,fminf(b,x)); }
static bool finiteVector(Vector3 v) { return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z); }
bool InteractionTarget_Validate(const InteractionTarget *t) {
    return t&&t->valid&&finiteVector(t->position)&&finiteVector(t->normal)&&finiteVector(t->approach)&&
        finiteVector(t->velocity)&&Vec3_LengthSq(t->normal)>.9f&&Vec3_LengthSq(t->normal)<1.1f&&
        Vec3_LengthSq(t->approach)>.9f&&Vec3_LengthSq(t->approach)<1.1f&&
        isfinite(t->positionTolerance)&&t->positionTolerance>0&&
        isfinite(t->orientationTolerance)&&t->orientationTolerance>0&&t->orientationTolerance<=3.141593f&&
        (t->type==INTERACTION_DRINK||t->type==INTERACTION_EAT);
}
static Vector3 logarithm(Quaternion q) {
    q=Quat_Normalize(q); if(q.w<0)q=(Quaternion){-q.x,-q.y,-q.z,-q.w};
    float length=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z);
    return length>1e-7f?Vec3_Scale(Vec3_Create(q.x,q.y,q.z),2*atan2f(length,q.w)/length):Vec3_Zero();
}
static float angle(Quaternion a,Quaternion b) { return Vec3_Length(logarithm(Quat_Multiply(Quat_Inverse(a),b))); }
static float saturation(const ReachJoint *joint,Quaternion rotation) {
    const JointConstraint *c=&joint->bounded.constraint;
    Quaternion q=Quat_Normalize(Quat_Multiply(Quat_Inverse(joint->neutral),rotation));
    Vector3 axis=Vec3_Normalize(c->hingeAxis);float projection=q.x*axis.x+q.y*axis.y+q.z*axis.z;
    Quaternion twist=Quat_Normalize((Quaternion){axis.x*projection,axis.y*projection,axis.z*projection,q.w});
    float a=2*atan2f(twist.x*axis.x+twist.y*axis.y+twist.z*axis.z,twist.w);
    if(c->type==JOINT_HINGE) {
        float limit=a<0?-c->minAngle:c->maxAngle;return limit>0?fabsf(a)/limit:0;
    }
    Quaternion swing=Quat_Multiply(q,Quat_Inverse(twist));
    float value=c->maxSwingAngle>0?2*acosf(clamp(fabsf(swing.w),0,1))/c->maxSwingAngle:0;
    if(c->maxTwistAngle>0)value=fmaxf(value,fabsf(a)/c->maxTwistAngle);
    return value;
}
ReachPreset AnatomicalReach_DefaultPreset(void) {
    return (ReachPreset){.comfortFraction=.65f,.velocity=1.2f,.acceleration=4,
        .crouchFraction=.2f,.strainWeight=.00002f,.continuityWeight=.00001f,.iterations=12,.torsoJoints=2};
}
bool AnatomicalReach_Compile(AnatomicalReachProfile *p,const Rig *r,ReachPreset preset) {
    if(!p||!r||!Skeleton_Validate(&r->skeleton)||r->headJoint<0||
       r->headJoint>=(int)r->skeleton.jointCount||!finiteVector(r->oralPoint)||!finiteVector(r->oralDirection)||
       !isfinite(preset.comfortFraction)||preset.comfortFraction<=0||preset.comfortFraction>1||
       !isfinite(preset.velocity)||preset.velocity<=0||!isfinite(preset.acceleration)||preset.acceleration<=0||
       !isfinite(preset.strainWeight)||preset.strainWeight<0||!isfinite(preset.continuityWeight)||preset.continuityWeight<0||
       !isfinite(preset.crouchFraction)||preset.crouchFraction<0||preset.crouchFraction>.25f||preset.torsoJoints>2||!preset.iterations||preset.iterations>24)return false;
    memset(p,0,sizeof(*p));p->head=r->headJoint;p->oralPoint=r->oralPoint;p->oralDirection=Vec3_LengthSq(r->oralDirection)>.5f?Vec3_Normalize(r->oralDirection):Vec3_Create(0,0,1);p->preset=preset;
    int reverse[REACH_MAX_JOINTS];unsigned count=0;int j=r->headJoint;
    int base=r->cervicalBaseJoint>=0?r->cervicalBaseJoint:r->neckJoint>=0?r->neckJoint:r->headJoint;
    while(j>=0&&count<REACH_MAX_JOINTS) {
        reverse[count++]=j;if(j==base)break;j=r->skeleton.joints[j].parentIndex;
    }
    if(!count||reverse[count-1]!=base)return false;
    int parent=r->skeleton.joints[base].parentIndex;
    while(parent>=0&&count<REACH_MAX_JOINTS&&p->torsoCount<preset.torsoJoints) {
        bool torso=false;
        for(size_t k=0;k<r->spineCount;++k)torso|=r->spine[k]==parent;
        if(!torso)break;
        reverse[count++]=parent;++p->torsoCount;parent=r->skeleton.joints[parent].parentIndex;
    }
    p->count=count;p->maximumReach=Vec3_Length(r->oralPoint);
    for(unsigned k=0;k<count;++k) {
        j=reverse[count-1-k];ReachJoint *a=&p->joints[k];a->index=j;a->id=r->skeleton.joints[j].id;
        a->bounded=r->skeleton.joints[j];a->neutral=a->bounded.restRotation;
        a->length=k+1<count?Vec3_Length(r->skeleton.joints[reverse[count-2-k]].restPosition):Vec3_Length(r->oralPoint);
        if(k+1<count)p->maximumReach+=a->length;
        JointConstraint c=a->bounded.constraint;
        a->comfort=preset.comfortFraction*(c.type==JOINT_HINGE?fmaxf(fabsf(c.minAngle),fabsf(c.maxAngle)):c.maxSwingAngle);
        Vector3 segment=k+1<count?r->skeleton.joints[reverse[count-2-k]].restPosition:r->oralPoint;
        a->preferredBend=c.type==JOINT_HINGE?Vec3_Normalize(c.hingeAxis):
            Vec3_Normalize(Vec3_Cross(segment,Vec3_Create(0,-1,0)));
    }
    float shortest=INFINITY;
    for(size_t k=0;k<r->limbCount;++k)if(r->limbs[k].walking) {
        float length=0;const IKChain *chain=&r->limbs[k].chain;
        for(size_t j=1;j<chain->jointCount;++j)length+=Vec3_Length(r->skeleton.joints[chain->jointIndices[j]].restPosition);
        shortest=fminf(shortest,length);
    }
    if(isfinite(shortest))p->maximumCrouch=shortest*preset.crouchFraction;
    return true;
}
void AnatomicalReach_Reset(AnatomicalReachState *s,const AnatomicalReachProfile *p,const SkeletonPose *pose) {
    memset(s,0,sizeof(*s));s->initialized=true;
    for(unsigned k=0;k<p->count;++k)s->desired[k]=s->rotation[k]=Skeleton_ConstrainRotation(&p->joints[k].bounded,
        pose?pose->joints[p->joints[k].index].localRotation:p->joints[k].neutral);
}
static Vector3 mouth(const AnatomicalReachProfile *p,const SkeletonPose *pose) {
    return Vec3_Add(pose->joints[p->head].worldPosition,Quat_RotateVector(pose->joints[p->head].worldRotation,p->oralPoint));
}
static float orientation(const AnatomicalReachProfile *p,const SkeletonPose *pose,const InteractionTarget *t) {
    Vector3 direction=Quat_RotateVector(pose->joints[p->head].worldRotation,p->oralDirection);
    return acosf(clamp(Vec3_Dot(direction,t->approach),-1,1));
}
static float objective(const AnatomicalReachProfile *p,const AnatomicalReachState *s,const SkeletonPose *pose,const InteractionTarget *t) {
    float error=Vec3_Distance(mouth(p,pose),t->position),o=fmaxf(0,orientation(p,pose,t)-t->orientationTolerance);
    float cervical=0;for(unsigned k=p->torsoCount;k<p->count;++k)cervical+=p->joints[k].length;
    float scale=fmaxf(.01f,cervical*cervical);
    float score=error*error+scale*.04f*o*o;
    for(unsigned k=0;k<p->count;++k) {
        const ReachJoint *j=&p->joints[k];Quaternion q=pose->joints[j->index].localRotation;
        Vector3 bend=logarithm(Quat_Multiply(Quat_Inverse(j->neutral),q));
        float strain=fmaxf(0,Vec3_Length(bend)-j->comfort),change=angle(q,s->rotation[k]);
        float reverse=fmaxf(0,-Vec3_Dot(bend,j->preferredBend));
        score+=scale*(p->preset.strainWeight*(strain*strain+reverse*reverse+.05f*Vec3_LengthSq(bend))+
            p->preset.continuityWeight*change*change);
    }
    return score;
}
static void solve(const AnatomicalReachProfile *p,AnatomicalReachState *s,const Skeleton *sk,
    SkeletonPose *pose,const InteractionTarget *t,unsigned *iterations) {
    for(unsigned k=0;k<p->count;++k)pose->joints[p->joints[k].index].localRotation=s->rotation[k];
    SkeletonPose_UpdateSubtree(pose,sk,p->joints[0].index);
    {
        float best=objective(p,s,pose,t);
        /* Descenso acotado sobre ejes locales, sin semillas de flexión invertida. */
        for(unsigned pass=0;pass<p->preset.iterations;++pass) {
            float step=.22f*powf(.78f,(float)pass);
            for(int k=(int)p->count-1;k>=(s->compensationTime>2.f?0:(int)p->torsoCount);--k) {
                int j=p->joints[k].index,parent=sk->joints[j].parentIndex;
                Quaternion old=pose->joints[j].localRotation,chosen=old;
                Vector3 from=Vec3_Sub(mouth(p,pose),pose->joints[j].worldPosition);
                Vector3 to=Vec3_Sub(t->position,pose->joints[j].worldPosition);
                Quaternion world=Quat_Multiply(Quat_FromTo(from,to),pose->joints[j].worldRotation);
                Quaternion local=parent>=0?Quat_Multiply(Quat_Inverse(pose->joints[parent].worldRotation),world):world;
                for(int trial=1;trial<=3;++trial) {
                    pose->joints[j].localRotation=Skeleton_ConstrainRotation(&p->joints[k].bounded,
                        Quat_Slerp(old,local,trial/3.f));
                    SkeletonPose_UpdateSubtree(pose,sk,j);float score=objective(p,s,pose,t);
                    if(score<best) {best=score;chosen=pose->joints[j].localRotation;}
                }
                pose->joints[j].localRotation=chosen;SkeletonPose_UpdateSubtree(pose,sk,j);
            }
            for(int k=(int)p->count-1;k>=(s->compensationTime>2.f?0:(int)p->torsoCount);--k)for(int axis=0;axis<3;++axis) {
                int j=p->joints[k].index;Quaternion old=pose->joints[j].localRotation,chosen=old;
                Vector3 v=axis==0?Vec3_Create(1,0,0):axis==1?Vec3_Create(0,1,0):Vec3_Create(0,0,1);
                for(int sign=-1;sign<=1;sign+=2) {
                    pose->joints[j].localRotation=Skeleton_ConstrainRotation(&p->joints[k].bounded,
                        Quat_Multiply(old,Quat_FromAxisAngle(v,step*sign)));
                    SkeletonPose_UpdateSubtree(pose,sk,j);
                    float score=objective(p,s,pose,t);
                    if(score<best){best=score;chosen=pose->joints[j].localRotation;}
                }
                pose->joints[j].localRotation=chosen;SkeletonPose_UpdateSubtree(pose,sk,j);
            }
            ++*iterations;
        }
        for(unsigned k=0;k<p->count;++k)s->desired[k]=pose->joints[p->joints[k].index].localRotation;
    }
}
ReachResult AnatomicalReach_Update(const AnatomicalReachProfile *p,AnatomicalReachState *s,
    const Skeleton *sk,SkeletonPose *pose,const InteractionTarget *t,float dt) {
    ReachResult result={.status=REACH_INVALID};
    if(!p||!s||!sk||!pose||!p->count||pose->jointCount!=sk->jointCount||!isfinite(dt)||dt<0||dt>.25f)return result;
    for(unsigned k=0;k<p->count;++k) {
        const ReachJoint *joint=&p->joints[k];
        if(joint->index<0||joint->index>=(int)sk->jointCount)return result;
        const SkeletonJoint *current=&sk->joints[joint->index];
        if(current->id!=joint->id||current->parentIndex!=joint->bounded.parentIndex||
           Vec3_Distance(current->restPosition,joint->bounded.restPosition)>1e-7f||
           current->restRotation.x!=joint->bounded.restRotation.x||current->restRotation.y!=joint->bounded.restRotation.y||
           current->restRotation.z!=joint->bounded.restRotation.z||current->restRotation.w!=joint->bounded.restRotation.w||
           Vec3_Distance(current->constraint.hingeAxis,joint->bounded.constraint.hingeAxis)>1e-7f||
           current->constraint.type!=joint->bounded.constraint.type||
           current->constraint.maxSwingAngle!=joint->bounded.constraint.maxSwingAngle||
           current->constraint.maxTwistAngle!=joint->bounded.constraint.maxTwistAngle||
           current->constraint.minAngle!=joint->bounded.constraint.minAngle||
           current->constraint.maxAngle!=joint->bounded.constraint.maxAngle)return result;
    }
    if(!s->initialized)AnatomicalReach_Reset(s,p,pose);
    bool valid=InteractionTarget_Validate(t),blocked=valid&&t->blocked;
    bool outside=valid&&Vec3_Distance(pose->joints[p->joints[0].index].worldPosition,t->position)>p->maximumReach+t->positionTolerance;
    Quaternion desired[REACH_MAX_JOINTS];
    for(unsigned k=0;k<p->count;++k) {
        int j=p->joints[k].index;desired[k]=t?p->joints[k].neutral:pose->joints[j].localRotation;
        pose->joints[j].localRotation=s->rotation[k];
    }
    SkeletonPose_UpdateSubtree(pose,sk,p->joints[0].index);
    /* Integración acotada, hasta 30 subpasos. Aceleración vectorial en rad/s². */
    s->remainder+=dt;const float h=1.f/120;
    while(s->remainder+1e-9>=h) {
        s->remainder-=h;
        if(valid&&!blocked&&!outside) {
            if(s->tick%4==0)solve(p,s,sk,pose,t,&result.iterations);
        } else for(unsigned k=0;k<p->count;++k)s->desired[k]=desired[k];
        ++s->tick;
        if(valid&&!outside&&!blocked)s->compensationTime+=h;
        for(unsigned k=0;k<p->count;++k) {
            Vector3 delta=logarithm(Quat_Multiply(Quat_Inverse(s->rotation[k]),s->desired[k]));float distance=Vec3_Length(delta);
            (void)distance;
            Vector3 change=Vec3_Scale(Vec3_Sub(Vec3_Scale(delta,64),Vec3_Scale(s->velocity[k],16)),h);
            float length=Vec3_Length(change);
            if(length>p->preset.acceleration*h)change=Vec3_Scale(change,p->preset.acceleration*h/length);
            s->velocity[k]=Vec3_Add(s->velocity[k],change);
            float speed=Vec3_Length(s->velocity[k]);
            if(speed>p->preset.velocity)s->velocity[k]=Vec3_Scale(s->velocity[k],p->preset.velocity/speed);
            float travel=Vec3_Length(s->velocity[k])*h;
            Quaternion q=Quat_Multiply(s->rotation[k],Quat_FromAxisAngle(Vec3_Normalize(s->velocity[k]),travel));
            s->rotation[k]=Skeleton_ConstrainRotation(&p->joints[k].bounded,q);
        }
    }
    for(unsigned k=0;k<p->count;++k) {
        pose->joints[p->joints[k].index].localRotation=s->rotation[k];
        result.stability=fmaxf(result.stability,Vec3_Length(s->velocity[k]));
        result.saturation=fmaxf(result.saturation,saturation(&p->joints[k],s->rotation[k]));
    }
    SkeletonPose_UpdateSubtree(pose,sk,p->joints[0].index);
    result.mouth=mouth(p,pose);result.direction=Quat_RotateVector(pose->joints[p->head].worldRotation,p->oralDirection);
    if(!valid){s->stableTime=0;s->stalledTime=0;return result;}
    result.objectId=t->objectId;result.error=Vec3_Distance(result.mouth,t->position);result.orientationError=orientation(p,pose,t);
    if(s->objectId!=t->objectId||Vec3_Distance(s->previousTarget,t->position)>t->positionTolerance) s->stableTime=s->stalledTime=s->compensationTime=0;
    s->objectId=t->objectId;s->previousTarget=t->position;
    bool contact=!blocked&&!outside&&result.error<=t->positionTolerance&&result.orientationError<=t->orientationTolerance&&result.stability<.15f;
    if(contact)s->compensationTime=0;
    s->stableTime=contact?s->stableTime+dt:0;
    s->stalledTime=!contact&&result.stability<.03f?s->stalledTime+dt:0;
    result.stableTime=s->stableTime;
    result.status=blocked?REACH_BLOCKED:outside?REACH_UNREACHABLE:contact?REACH_REACHED:
        s->stalledTime>1?REACH_BLOCKED:REACH_APPROACHING;
    result.blockedReason=blocked?1:(result.status==REACH_BLOCKED?5:0);
    return result;
}
