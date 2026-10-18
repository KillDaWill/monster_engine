#include "ProceduralAnimator.h"
#include <math.h>
#include <string.h>
bool ProceduralAnimator_Init(ProceduralAnimator* a,const Rig* rig,GaitProfile gait,World* world,Vector3 position) {
    if(!a)return false;
    memset(a,0,sizeof(*a)); a->gait=gait; a->lookDirection=Vec3_Create(0,0,1);
    for(int i=0;i<3;++i)a->cervicalRotation[i]=Quat_Identity();
    if(!AnatomicalReach_Compile(&a->reachProfile,rig,AnatomicalReach_DefaultPreset())&&rig->headJoint>=0)return false;
    return Locomotion_Init(&a->locomotion,rig,&gait,world,position);
}
static void OralReach(ProceduralAnimator* a,const Rig* r,SkeletonPose* p,float dt) {
    InteractionTarget target=a->interactionTarget;
    if(a->oralReachActive&&!target.valid)target=(InteractionTarget){.valid=true,
        .position=a->oralReachTarget,.normal={0,1,0},.approach={0,0,1},
        .positionTolerance=.02f,.orientationTolerance=1.57f};
    a->reachResult=AnatomicalReach_Update(&a->reachProfile,&a->reachState,&r->skeleton,p,
        a->oralReachActive?&target:NULL,dt);
    for(unsigned k=0;k<a->reachProfile.count&&k<3;++k)a->cervicalRotation[k]=a->reachState.rotation[k];
}
static void Rotate(const Skeleton* s,SkeletonPose* p,int j,Vector3 axis,float angle) {
    if(j<0 || j>=(int)s->jointCount)return;
    p->joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],
        Quat_Multiply(s->joints[j].restRotation,Quat_FromAxisAngle(axis,angle)));
}
static void RotateEuler(const Skeleton* s,SkeletonPose* p,int j,float yaw,float pitch,float roll) {
    if(j<0 || j>=(int)s->jointCount)return;
    Quaternion qYaw=Quat_FromAxisAngle(Vec3_Create(0,1,0),yaw);
    Quaternion qPitch=Quat_FromAxisAngle(Vec3_Create(1,0,0),pitch);
    Quaternion qRoll=Quat_FromAxisAngle(Vec3_Create(0,0,1),roll);
    Quaternion delta=Quat_Multiply(qYaw,Quat_Multiply(qPitch,qRoll));
    p->joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],
        Quat_Multiply(s->joints[j].restRotation,delta));
}
static void AxialControllers(ProceduralAnimator* a,const Rig* r,SkeletonPose* p) {
    float wave=a->locomotion.phase*6.28318530718f;
    float speed=Vec3_Length(a->locomotion.velocity);
    float activity=fminf(1,speed*a->gait.cycleDuration/a->gait.strideLength);
    float turnRate=a->currentAngularVelocity;

    /* 1. Columna vertebral con curvatura horizontal (yaw) y peralte corporal (roll) */
    float spineTurnFactor=a->gait.spineTurn;
    float spineCountF=(float)(r->spineCount>0?r->spineCount:1);
    for(size_t i=0;i<r->spineCount;++i) {
        float prog=(float)(i+1)/spineCountF;
        float yawBend=turnRate*spineTurnFactor/spineCountF*(0.7f+0.6f*prog);
        float bankRoll=-atanf(turnRate*speed*a->gait.bodyBank)/spineCountF*(0.7f+0.6f*prog);
        float totalYaw=a->gait.spineWave*activity*sinf(wave-(float)i*.45f)+yawBend;
        RotateEuler(&r->skeleton,p,r->spine[i],totalYaw,0,bankRoll);
    }

    /* 2. Colas: contracurvatura compensatoria (látigo) y acompañamiento de inclinación */
    float tailTurnFactor=a->gait.tailTurn;
    for(size_t t=0;t<r->tailCount;++t) {
        size_t jCount=r->tails[t].jointCount;
        float tailCountF=(float)(jCount>0?jCount:1);
        float totalLength=0,weightedLength=0;
        for(size_t i=0;i<jCount;++i) {
            float length=Vec3_Length(r->skeleton.joints[r->tails[t].joints[i]].restPosition);
            totalLength+=length;weightedLength+=length*sinf((float)(i+1)/tailCountF*1.5707963f);
        }
        /* Curvatura de trayectoria, no giro rígido repartido por recuento.
         * La longitud anatómica aumenta la flexión de colas largas; atan
         * mantiene un arco finito también al girar lentamente o retroceder. */
        float forwardSpeed=a->locomotion.velocity.z;
        float referenceSpeed=.2f*a->gait.strideLength/(a->gait.cycleDuration*a->gait.stanceRatio);
        float curvature=turnRate*forwardSpeed/(forwardSpeed*forwardSpeed+referenceSpeed*referenceSpeed);
        float tailArc=atanf(curvature*tailTurnFactor*totalLength);
        for(size_t i=0;i<jCount;++i) {
            float tProg=(float)(i+1)/tailCountF;
            float length=Vec3_Length(r->skeleton.joints[r->tails[t].joints[i]].restPosition);
            float tailYawBend=-tailArc*length*sinf(tProg*1.5707963f)/fmaxf(weightedLength,1e-6f);
            float totalTailYaw=-a->gait.tailCounterSwing*activity*sinf(wave-(float)i*.65f)+tailYawBend;
            float tailRoll=-turnRate*a->gait.bodyBank/tailCountF*0.4f*(1.0f-tProg)*activity;
            RotateEuler(&r->skeleton,p,r->tails[t].joints[i],totalTailYaw,0,tailRoll);
        }
    }

    /* 3. Cabeza y cuello: anticipación hacia el ápice de la curva */
    int head=r->headJoint;
    if(head>=0) {
        Vector3 lookDir=a->lookDirection;
        if(fabsf(turnRate)>0.01f) {
            lookDir.x+=turnRate*0.25f;
            lookDir=Vec3_Normalize(lookDir);
        }
        Quaternion look=Quat_FromTo(Vec3_Create(0,0,1),lookDir);
        if(r->neckJoint>=0)p->joints[r->neckJoint].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[r->neckJoint],Quat_Slerp(Quat_Identity(),look,.45f));
        p->joints[head].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[head],Quat_Slerp(Quat_Identity(),look,.65f));
    }
    if(r->jawJoint>=0)Rotate(&r->skeleton,p,r->jawJoint,r->skeleton.joints[r->jawJoint].constraint.hingeAxis,
        fmaxf(0,fminf(1,a->mouthOpen))*r->maxJawAngle);
}
static float AreaXZ(Vector3 a,Vector3 b,Vector3 c) {
    return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
}
/* La proyección axial debe permanecer dentro de la envolvente de apoyos. */
static bool Balanced(const ProceduralAnimator *a,const Rig *r,const SkeletonPose *p) {
    if(!r->spineCount)return true;
    Vector3 mass=Vec3_Zero(),feet[RIG_MAX_LIMBS];unsigned count=0;
    for(size_t k=0;k<r->spineCount;++k)mass=Vec3_Add(mass,p->joints[r->spine[k]].worldPosition);
    mass=Vec3_Scale(mass,1.f/r->spineCount);
    for(size_t k=0;k<r->limbCount;++k)if(r->limbs[k].walking&&a->locomotion.feet[k].phase==FOOT_STANCE)
        feet[count++]=a->locomotion.feet[k].targetPosition;
    if(count<3)return false;
    for(unsigned i=0;i<count;++i)for(unsigned j=i+1;j<count;++j)for(unsigned k=j+1;k<count;++k) {
        float area=AreaXZ(feet[i],feet[j],feet[k]);if(fabsf(area)<1e-6f)continue;
        float u=AreaXZ(feet[i],feet[j],mass)/area,v=AreaXZ(feet[j],feet[k],mass)/area,
            w=AreaXZ(feet[k],feet[i],mass)/area;
        if(u>=-.001f&&v>=-.001f&&w>=-.001f)return true;
    }
    return false;
}
bool ProceduralAnimator_Update(ProceduralAnimator* a,const Rig* r,SkeletonPose* p,World* world,float dt) {
    if(!a || !r || !p || !isfinite(dt) || dt<0 || dt>.25f)return false;
    /* Suavizado continuo de la velocidad angular para transiciones orgánicas */
    if(dt>0) {
        a->currentAngularVelocity+=(a->angularVelocity-a->currentAngularVelocity)*(1.0f-expf(-12.0f*dt));
    } else {
        a->currentAngularVelocity=a->angularVelocity;
    }
    a->locomotion.angularVelocity=a->angularVelocity;

    if(!Locomotion_Update(&a->locomotion,r,&a->gait,world,a->desiredVelocity,dt))return false;
    Quaternion previous[RIG_MAX_LIMBS][IK_MAX_CHAIN_JOINTS];
    for(size_t i=0;i<r->limbCount;++i)for(size_t k=0;k<r->limbs[i].chain.jointCount;++k)
        previous[i][k]=p->joints[r->limbs[i].chain.jointIndices[k]].localRotation;
    if(!SkeletonPose_ResetToRest(p,&r->skeleton))return false;
    /* La IK arranca desde la flexión anterior, no desde una cadena casi
     * extendida en cada fotograma. Conserva codos y corvejones continuos. */
    for(size_t i=0;i<r->limbCount;++i)for(size_t k=0;k+1<r->limbs[i].chain.jointCount;++k) {
        int j=r->limbs[i].chain.jointIndices[k];
        p->joints[j].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[j],previous[i][k]);
    }

    float phase=a->locomotion.phase*6.28318530718f;
    float speed=fminf(1,Vec3_Length(a->locomotion.velocity));
    for(size_t j=0;j<r->skeleton.jointCount;++j) if(r->skeleton.joints[j].parentIndex<0) {
        p->joints[j].localPosition=Vec3_Add(p->joints[j].localPosition,a->locomotion.bodyPosition);
        p->joints[j].localPosition.y-=a->gait.supportCompression;
        p->joints[j].localPosition.y+=a->gait.bodyBob*speed*sinf(phase*2);
        p->joints[j].localPosition.x+=a->gait.bodySway*speed*sinf(phase);
    }
    AxialControllers(a,r,p);
    SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
    float desiredCrouch=0,maximumCrouch=a->reachProfile.maximumCrouch;
    bool pendingCrouch=false;
    a->oralReachTime=a->oralReachActive?a->oralReachTime+dt:0;
    if(a->oralReachActive&&a->reachProfile.count) {
        Vector3 target=a->interactionTarget.valid?a->interactionTarget.position:a->oralReachTarget;
        Vector3 anchor=p->joints[a->reachProfile.joints[0].index].worldPosition;
        Vector3 difference=Vec3_Sub(anchor,target);float horizontal=difference.x*difference.x+difference.z*difference.z;
        float reach=a->reachProfile.maximumReach;
        if(horizontal<reach*reach&&difference.y>0) {
            float required=fmaxf(0,difference.y-sqrtf(reach*reach-horizontal)+.01f);
            if(required<=maximumCrouch) {
                desiredCrouch=required;
                pendingCrouch=required>a->reachCrouch+.005f;
            }
        }
    }
    for(size_t k=0;k<r->spineCount;++k) {
        Vector3 v=p->joints[r->spine[k]].worldPosition;SurfaceHit hit={.position={0,0,0}};
        if(!world||World_SampleGround(world,v,&hit))maximumCrouch=fminf(maximumCrouch,fmaxf(0,v.y-r->spineRadius[k]-hit.position.y-.02f));
    }
    desiredCrouch=fminf(desiredCrouch,maximumCrouch);
    if(a->oralReachTime<.6f)desiredCrouch=0;
    float oldCrouch=a->reachCrouch;
    a->crouchRemainder+=dt;
    const float crouchStep=1.f/120;
    while(a->crouchRemainder+1e-9>=crouchStep) {
        a->crouchRemainder-=crouchStep;
        float change=(desiredCrouch-a->reachCrouch)*16-a->reachCrouchVelocity*8;
        float acceleration=fmaxf(.05f,a->reachProfile.maximumCrouch*4);
        a->reachCrouchVelocity+=fmaxf(-acceleration,fminf(acceleration,change))*crouchStep;
        a->reachCrouch=fmaxf(0,fminf(maximumCrouch,a->reachCrouch+a->reachCrouchVelocity*crouchStep));
    }
    for(size_t k=0;k<r->skeleton.jointCount;++k)if(r->skeleton.joints[k].parentIndex<0)p->joints[k].localPosition.y-=a->reachCrouch;
    SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
    Quaternion reachBefore[REACH_MAX_JOINTS];
    for(unsigned k=0;k<a->reachProfile.count;++k)reachBefore[k]=a->reachState.initialized?
        a->reachState.rotation[k]:p->joints[a->reachProfile.joints[k].index].localRotation;
    Quaternion torsoBefore[REACH_MAX_JOINTS];
    for(unsigned k=0;k<a->reachProfile.torsoCount;++k)
        torsoBefore[k]=p->joints[a->reachProfile.joints[k].index].localRotation;
    bool originallyBalanced=Balanced(a,r,p);
    OralReach(a,r,p,dt);
    bool safe=!a->oralReachActive||!a->reachProfile.torsoCount||!originallyBalanced||Balanced(a,r,p);
    for(size_t i=0;i<r->limbCount;++i)if(r->limbs[i].walking) {
        const IKChain *c=&r->limbs[i].chain;float length=0;
        for(size_t k=1;k<c->jointCount;++k)length+=Vec3_Length(r->skeleton.joints[c->jointIndices[k]].restPosition);
        if(Vec3_Distance(p->joints[c->jointIndices[0]].worldPosition,a->locomotion.feet[i].targetPosition)>length+.001f)safe=false;
    }
    for(unsigned k=0;k<a->reachProfile.torsoCount;++k) {
        Vector3 v=p->joints[a->reachProfile.joints[k].index].worldPosition;SurfaceHit hit;
        if(world&&World_SampleGround(world,v,&hit)&&v.y<hit.position.y+a->gait.supportCompression)safe=false;
    }
    bool collision=a->oralReachActive&&a->validateInteractionPose&&
        !a->validateInteractionPose(r,p,a->interactionContext);
    if(collision) {
        for(unsigned k=0;k<a->reachProfile.count;++k) {
            int j=a->reachProfile.joints[k].index;
            p->joints[j].localRotation=reachBefore[k];a->reachState.rotation[k]=reachBefore[k];
            a->reachState.velocity[k]=Vec3_Zero();
        }
        safe=false;
    }
    if(!safe) {
        for(size_t k=0;k<r->skeleton.jointCount;++k)if(r->skeleton.joints[k].parentIndex<0)p->joints[k].localPosition.y+=a->reachCrouch-oldCrouch;
        a->reachCrouch=oldCrouch;a->reachCrouchVelocity=0;
        for(unsigned k=0;k<a->reachProfile.torsoCount;++k) {
            int j=a->reachProfile.joints[k].index;
            p->joints[j].localRotation=torsoBefore[k];a->reachState.rotation[k]=torsoBefore[k];a->reachState.velocity[k]=Vec3_Zero();
        }
        SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
        a->reachResult.status=REACH_BLOCKED;a->reachResult.blockedReason=collision?2:3;a->reachState.stableTime=0;
    }
    for(int attempt=0;attempt<2;++attempt) {
    for(size_t i=0;i<r->limbCount;++i) {
        if(!r->limbs[i].walking)continue;
        IKChain c=r->limbs[i].chain; c.targetPosition=a->locomotion.feet[i].targetPosition;
        if(r->limbs[i].digitigrade&&c.jointCount==4) {
            int parent=r->skeleton.joints[c.jointIndices[0]].parentIndex;
            Quaternion frame=parent>=0?p->joints[parent].worldRotation:Quat_Identity();
            Vector3 distal=Quat_RotateVector(frame,r->skeleton.joints[c.endEffectorJoint].restPosition);
            a->limbResults[i]=IK_SolveDigitigrade(&r->skeleton,p,&c,distal,dt);
        } else a->limbResults[i]=IK_SolveFABRIK(&r->skeleton,p,&c);
        int j=c.endEffectorJoint,parent=r->skeleton.joints[j].parentIndex;
        /* Orientación del autopodio independiente del objetivo posicional. */
        Quaternion orientation=Quat_FromTo(Vec3_Create(0,1,0),a->locomotion.feet[i].normal);
        Quaternion local=Quat_Multiply(Quat_Inverse(p->joints[parent].worldRotation),orientation);
        p->joints[j].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[j],local);
    }
        bool feetSafe=true;
        for(size_t i=0;i<r->limbCount;++i)if(r->limbs[i].walking&&a->limbResults[i].error>.004f)feetSafe=false;
        if(feetSafe||!a->oralReachActive||!a->reachProfile.torsoCount)break;
        for(size_t k=0;k<r->skeleton.jointCount;++k)if(r->skeleton.joints[k].parentIndex<0)p->joints[k].localPosition.y+=a->reachCrouch-oldCrouch;
        a->reachCrouch=oldCrouch;a->reachCrouchVelocity=0;
        for(unsigned k=0;k<a->reachProfile.torsoCount;++k) {
            int j=a->reachProfile.joints[k].index;
            p->joints[j].localRotation=reachBefore[k];a->reachState.rotation[k]=reachBefore[k];a->reachState.velocity[k]=Vec3_Zero();
        }
        SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
        a->reachResult.status=REACH_BLOCKED;a->reachResult.blockedReason=4;a->reachState.stableTime=0;a->reachResult.stableTime=0;
    }
    bool updated=SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
    if(pendingCrouch&&a->reachResult.status==REACH_UNREACHABLE&&a->oralReachTime<2)
        a->reachResult.status=REACH_APPROACHING;
    if(a->oralReachActive&&r->headJoint>=0) {
        a->reachResult.mouth=Vec3_Add(p->joints[r->headJoint].worldPosition,
            Quat_RotateVector(p->joints[r->headJoint].worldRotation,r->oralPoint));
        a->reachResult.error=Vec3_Distance(a->reachResult.mouth,a->interactionTarget.valid?
            a->interactionTarget.position:a->oralReachTarget);
        for(size_t i=0;i<r->limbCount;++i)if(r->limbs[i].walking)a->reachResult.footError=fmaxf(a->reachResult.footError,a->limbResults[i].error);
        for(size_t i=0;i<r->limbCount;++i)if(r->limbs[i].walking&&a->limbResults[i].error>.004f) {
            a->reachResult.status=REACH_BLOCKED;a->reachResult.blockedReason=4;a->reachState.stableTime=0;a->reachResult.stableTime=0;
        }
    }
    return updated;
}
