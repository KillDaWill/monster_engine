#include "ProceduralAnimator.h"
#include "Limb.h"
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
            float tailPitch=a->gait.tailCounterSwing*.45f*activity*sinf(wave*2-(float)i*.55f);
            float tailRoll=-turnRate*a->gait.bodyBank/tailCountF*0.4f*(1.0f-tProg)*activity;
            RotateEuler(&r->skeleton,p,r->tails[t].joints[i],totalTailYaw,tailPitch,tailRoll);
        }
    }

    /* 3. Cabeza y cuello: guiñada y flexión vertical acopladas progresivamente */
    int head=r->headJoint;
    if(head>=0) {
        Vector3 lookDir=a->lookDirection;
        if(fabsf(turnRate)>0.01f) lookDir.x+=turnRate*0.25f;
        if(Vec3_LengthSq(lookDir)<1e-6f) lookDir=Vec3_Create(0,0,1);
        else lookDir=Vec3_Normalize(lookDir);
        float yaw=atan2f(lookDir.x,lookDir.z);
        float pitch=-asinf(fmaxf(-1.0f,fminf(1.0f,lookDir.y)));
        bool hasBase=r->cervicalBaseJoint>=0,hasNeck=r->neckJoint>=0;
        /* Al bajar la cabeza (pitch > 0), la base cervical lidera la inclinación para que todo el cuello acompañe. */
        if (r->cervicalChainCount > 2) {
            float length = 0.0f;
            for (size_t k = 1; k < r->cervicalChainCount; ++k)
                length += Vec3_Length(r->skeleton.joints[r->cervicalChain[k]].restPosition);
            float shortNeck = fmaxf(0.0f, fminf(1.0f, 2.0f - length / fmaxf(.001f, r->cranialCollar)));
            /* En cuellos cortos el atlas acompaña al cráneo y la cintura
             * escapular conserva su postura al seguir el blanco. */
            float pHead = .16f + .15f * shortNeck, yHead = .18f;
            float remP = 1.0f - pHead, remY = 1.0f - yHead;
            float n = (float)r->cervicalChainCount;
            float slope = pitch > 0.0f ? -.65f + 1.25f * shortNeck : 0.0f;
            float intercept = pitch > 0.0f ? 1.35f - .65f * shortNeck : 1.0f;
            float sumW = n * (intercept + .5f * slope);
            for (size_t k = 0; k < r->cervicalChainCount; ++k) {
                float u = (float)k / (n - 1.0f);
                float wp = (intercept + slope * u) / sumW;
                float wy = 1.0f / n;
                RotateEuler(&r->skeleton, p, r->cervicalChain[k], yaw * remY * wy, pitch * remP * wp, 0.0f);
            }
            RotateEuler(&r->skeleton, p, head, yaw * yHead, pitch * pHead, 0.0f);
        } else {
            float pBase=pitch>0.0f?0.52f:0.34f;
            float pNeck=pitch>0.0f?0.34f:0.44f;
            float pHead=1.0f-pBase-pNeck;
            if(!hasBase) { pNeck+=pBase*0.75f; pHead+=pBase*0.25f; pBase=0.0f; }
            if(!hasNeck) { pHead+=pNeck; pNeck=0.0f; }
            float yBase=hasBase?0.40f:0.0f;
            float yNeck=hasNeck?(hasBase?0.42f:0.76f):0.0f;
            float yHead=1.0f-yBase-yNeck;
            if(hasBase) RotateEuler(&r->skeleton,p,r->cervicalBaseJoint,yaw*yBase,pitch*pBase,0.0f);
            if(hasNeck) RotateEuler(&r->skeleton,p,r->neckJoint,yaw*yNeck,pitch*pNeck,0.0f);
            RotateEuler(&r->skeleton,p,head,yaw*yHead,pitch*pHead,0.0f);
        }
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
    Vector3 mass=Vec3_Zero(),feet[RIG_MAX_LIMBS];unsigned count=0,walkingLimbs=0;
    for(size_t k=0;k<r->spineCount;++k)mass=Vec3_Add(mass,p->joints[r->spine[k]].worldPosition);
    mass=Vec3_Scale(mass,1.f/r->spineCount);
    for(size_t k=0;k<r->limbCount;++k)if(r->limbs[k].walking) {
        ++walkingLimbs;
        if(a->locomotion.feet[k].phase==FOOT_STANCE)feet[count++]=a->locomotion.feet[k].targetPosition;
    }
    if(walkingLimbs<=2)return count>=1;
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
    for(size_t i=0;i<r->limbCount;++i) {
        if(r->limbs[i].role==LIMB_WING)continue;
        for(size_t k=0;k+1<r->limbs[i].chain.jointCount;++k) {
            int j=r->limbs[i].chain.jointIndices[k];
            p->joints[j].localRotation=Skeleton_ConstrainRotation(&r->skeleton.joints[j],previous[i][k]);
        }
    }

    float phase=a->locomotion.phase*6.28318530718f;
    float speed=fminf(1,Vec3_Length(a->locomotion.velocity));
    float activity=fminf(1,Vec3_Length(a->locomotion.velocity)*a->gait.cycleDuration/fmaxf(.01f,a->gait.strideLength));
    unsigned walkingCount=0;
    for(size_t k=0;k<r->limbCount;++k)if(r->limbs[k].walking)++walkingCount;
    for(size_t j=0;j<r->skeleton.jointCount;++j) if(r->skeleton.joints[j].parentIndex<0) {
        p->joints[j].localPosition=Vec3_Add(p->joints[j].localPosition,a->locomotion.bodyPosition);
        p->joints[j].localPosition.y-=a->gait.supportCompression;
        if(walkingCount==2&&a->gait.stanceRatio<.55f) {
            float pNorm=fmodf(a->locomotion.phase,1.0f);
            if(pNorm<0)pNorm+=1.0f;
            float stance=fmaxf(.15f,fminf(.85f,a->gait.stanceRatio));
            float hopOffset=pNorm>=stance?(4.0f*((pNorm-stance)/(1.0f-stance))*(1.0f-((pNorm-stance)/(1.0f-stance)))):
                (-0.28f*sinf((pNorm/stance)*3.14159265f));
            p->joints[j].localPosition.y+=a->gait.bodyBob*activity*hopOffset;
        } else {
            p->joints[j].localPosition.y+=a->gait.bodyBob*speed*sinf(phase*2);
            p->joints[j].localPosition.x+=a->gait.bodySway*speed*sinf(phase);
        }
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
    /* Una autoridad externa sustituye también la retirada cervical al reposo;
     * debe aplicarse después del alcance y antes de resolver las patas. */
    if(a->applyAxialPose) {
        a->applyAxialPose(r,p,a->axialContext);
        SkeletonPose_UpdateWorldTransforms(p,&r->skeleton);
    }
    float curl = fmaxf(0.0f, fminf(1.0f, a->defensiveCurl));
    if (curl > 0.01f) {
        /* Enrollamiento defensivo ventroflexor: columna, cuello y cola convergen en esfera acorazada. */
        float spineBend = 0.34f * curl;
        for (size_t i = 0; i < r->spineCount; ++i) {
            int j = r->spine[i];
            if (j < 0) continue;
            Quaternion q = Quat_FromAxisAngle(Vec3_Create(1, 0, 0), spineBend);
            p->joints[j].localRotation = Skeleton_ConstrainRotation(&r->skeleton.joints[j],
                Quat_Multiply(p->joints[j].localRotation, q));
        }
        size_t nCount = r->cervicalChainCount > 0 ? r->cervicalChainCount : 2;
        for (size_t i = 0; i < nCount; ++i) {
            int j = r->cervicalChainCount > 0 ? r->cervicalChain[i] : (i == 0 ? r->cervicalBaseJoint : r->neckJoint);
            if (j < 0) continue;
            Quaternion q = Quat_FromAxisAngle(Vec3_Create(1, 0, 0), (0.95f * curl) / (float)nCount);
            p->joints[j].localRotation = Quat_Multiply(p->joints[j].localRotation, q);
        }
        if (r->headJoint >= 0) {
            p->joints[r->headJoint].localRotation = Quat_Multiply(p->joints[r->headJoint].localRotation,
                Quat_FromAxisAngle(Vec3_Create(1, 0, 0), 0.55f * curl));
        }
        for (size_t ti = 0; ti < r->tailCount; ++ti) {
            const TailRig* tr = &r->tails[ti];
            for (size_t i = 0; i < tr->jointCount; ++i) {
                int j = tr->joints[i];
                if (j < 0) continue;
                float tailAngle = -(2.75f * curl) / fmaxf(1.0f, (float)tr->jointCount);
                p->joints[j].localRotation = Quat_Multiply(p->joints[j].localRotation,
                    Quat_FromAxisAngle(Vec3_Create(1, 0, 0), tailAngle));
            }
        }
        SkeletonPose_UpdateWorldTransforms(p, &r->skeleton);
        if (curl > 0.25f) return true;
    }
    float wingFold = fmaxf(0.0f, fminf(1.0f, a->wingFold));
    float wingFlap = fmaxf(-1.0f, fminf(1.0f, a->wingFlap));
    float hindWingFlap = fmaxf(-1.0f, fminf(1.0f, a->hindWingFlap));
    float finAmp = fmaxf(0.0f, fminf(1.0f, a->finWaveAmplitude));
    float bellAmp = fmaxf(0.0f, fminf(1.0f, a->bellPulseAmplitude));

    /* Avance de fases continuas para aletas batoideas, pulsación medusoide y brazos hidrostáticos. */
    if (dt > 0.0f) {
        float cycle = fmaxf(0.15f, a->gait.cycleDuration);
        float dPhase = (6.28318530718f / cycle) * dt;
        a->finWavePhase += dPhase;
        a->bellPulsePhase += dPhase;
        a->armWavePhase += dPhase * 0.85f;
    }

    /* Aleteo automático en vuelo cuando hay velocidad deseada y las alas no están plegadas ni forzadas. */
    bool autoFlap = (speed > 0.05f && finAmp <= 0.005f && wingFold < 0.15f &&
                     fabsf(wingFlap) <= 0.005f && fabsf(hindWingFlap) <= 0.005f);
    float effWingFlap = autoFlap ? (sinf(a->finWavePhase) * 0.88f) : wingFlap;
    float effHindWingFlap = autoFlap ? (sinf(a->finWavePhase + 1.5707963f) * 0.88f) : hindWingFlap;

    /* Alas quiropteras, alas aviares emplumadas, alas de insecto (4 independientes) y aletas batoideas. */
    bool anyWingAction = wingFold > 0.005f || fabsf(effWingFlap) > 0.005f ||
                         fabsf(effHindWingFlap) > 0.005f || finAmp > 0.005f;
    if (anyWingAction) {
        unsigned wingPairIndex[2] = {0, 0}; /* Conteo por lado para distinguir ala anterior/posterior en insectos */
        for (size_t i = 0; i < r->limbCount; ++i) {
            const LimbRig* limb = &r->limbs[i];
            if (limb->role != LIMB_WING || limb->chain.jointCount < 4) continue;
            int sideSlot = (limb->side == LIMB_RIGHT) ? 1 : 0;
            unsigned pairIdx = wingPairIndex[sideSlot]++;
            float side = (limb->side == LIMB_RIGHT) ? -1.0f : 1.0f;

            int childDigits[CREATURE_MAX_DIGITS];
            unsigned childCount = 0;
            for (size_t j = 0; j < r->skeleton.jointCount && childCount < CREATURE_MAX_DIGITS; ++j) {
                if (r->skeleton.joints[j].parentIndex == limb->endEffectorJoint)
                    childDigits[childCount++] = (int)j;
            }
            int sh = limb->chain.jointIndices[0];
            int el = limb->chain.jointIndices[1];
            int mid = limb->chain.jointIndices[2];
            int wr = limb->chain.jointIndices[3];

            if (finAmp > 0.005f && childCount <= 2) {
                /* Onda sinusoidal propagada para aletas pectorales batoideas (manta ray). */
                for (size_t k = 0; k < limb->chain.jointCount; ++k) {
                    float w = sinf(a->finWavePhase - (float)k * 0.78f);
                    float amp = (0.28f + 0.10f * (float)k) * finAmp;
                    RotateEuler(&r->skeleton, p, limb->chain.jointIndices[k], 0.0f, 0.0f, side * w * amp);
                }
            } else if (childCount >= 4) {
                /* Ala quiroptera con 4-5 dedos largos sosteniendo patagios. */
                RotateEuler(&r->skeleton, p, sh, -side * .44f * wingFold, -0.18f * wingFold,
                            side * (0.72f * effWingFlap - .38f * wingFold));
                RotateEuler(&r->skeleton, p, el, side * 1.24f * wingFold, 0.18f * wingFold,
                            -side * 0.22f * effWingFlap);
                RotateEuler(&r->skeleton, p, wr, -side * .82f * wingFold, -0.26f * wingFold, 0.0f);
                unsigned firstWingDigit = (childCount >= 5) ? 1u : 0u;
                for (unsigned d = firstWingDigit; d < childCount; ++d) {
                    int root = childDigits[d];
                    RotateEuler(&r->skeleton, p, root, side * .70f * wingFold * (1.0f - 0.08f * (float)d),
                                -0.38f * wingFold, -side * 0.14f * effWingFlap);
                    int parent = root;
                    for (size_t j = (size_t)root + 1; j < r->skeleton.jointCount; ++j) {
                        if (r->skeleton.joints[j].parentIndex != parent) continue;
                        RotateEuler(&r->skeleton, p, (int)j, side * .21f * wingFold, 0.0f, 0.0f);
                        parent = (int)j;
                    }
                }
            } else if (childCount >= 1) {
                /* Ala aviar emplumada (hombro -> codo -> ulna -> carpo -> mano distal reducida). */
                RotateEuler(&r->skeleton, p, sh, -side * 0.36f * wingFold, -0.12f * wingFold,
                            side * (0.68f * effWingFlap - 0.34f * wingFold));
                RotateEuler(&r->skeleton, p, el, side * 1.05f * wingFold, 0.14f * wingFold,
                            -side * 0.18f * effWingFlap);
                RotateEuler(&r->skeleton, p, mid, 0.0f, 0.0f, side * 0.12f * effWingFlap);
                RotateEuler(&r->skeleton, p, wr, -side * 0.92f * wingFold, -0.18f * wingFold,
                            side * 0.18f * effWingFlap);
            } else {
                /* Ala fina de insecto: batido independiente para ala anterior (pairIdx=0) y posterior (pairIdx=1). */
                float flap = (pairIdx == 0) ? effWingFlap : (fabsf(effHindWingFlap) > 0.005f ? effHindWingFlap : -effWingFlap * 0.85f);
                float pitchTwist = 0.24f * flap;
                RotateEuler(&r->skeleton, p, sh, 0.0f, pitchTwist, side * 0.56f * flap);
                RotateEuler(&r->skeleton, p, el, 0.0f, 0.06f * flap, side * 0.10f * flap);
            }
        }
        SkeletonPose_UpdateWorldTransforms(p, &r->skeleton);
    }

    /* Pulsación radial de campana medusoide: contracción/relajación de los nodos marginales (localId >= 20). */
    if (bellAmp > 0.005f) {
        float pulse = sinf(a->bellPulsePhase);
        float contract = 0.38f * bellAmp * pulse;
        for (size_t j = 0; j < r->skeleton.jointCount; ++j) {
            uint16_t localId = (uint16_t)(r->skeleton.joints[j].anatomyId & 0xffffu);
            if (localId >= 20 && localId < 36 && r->skeleton.joints[j].parentIndex >= 0) {
                Vector3 rp = r->skeleton.joints[j].restPosition;
                float rHoriz = sqrtf(rp.x * rp.x + rp.z * rp.z);
                if (rHoriz > 1e-5f) {
                    /* Eje tangencial al anillo: inclinar cada radio hacia adentro/abajo al contraer. */
                    Vector3 tang = Vec3_Normalize(Vec3_Create(-rp.z, 0.0f, rp.x));
                    Rotate(&r->skeleton, p, (int)j, tang, contract);
                    p->joints[j].localPosition = Vec3_Create(
                        rp.x * (1.0f - 0.26f * bellAmp * pulse),
                        rp.y - 0.12f * rHoriz * bellAmp * pulse,
                        rp.z * (1.0f - 0.26f * bellAmp * pulse));
                }
            }
        }
        SkeletonPose_UpdateWorldTransforms(p, &r->skeleton);
    }

    /* Ondulación / rastreo de brazos hidrostáticos independientes (cuando hay 4+ cadenas flexibles). */
    if (fabsf(a->armSpread) > 0.005f || (a->gait.cycleDuration > 2.3f && a->gait.cycleDuration < 2.5f && speed > 0.01f)) {
        for (size_t j = 0; j < r->skeleton.jointCount; ++j) {
            uint32_t modId = r->skeleton.joints[j].anatomyId >> 16;
            uint16_t localId = (uint16_t)(r->skeleton.joints[j].anatomyId & 0xffffu);
            if (modId < 10 || localId < 2 || localId > 20) continue;
            if (r->skeleton.joints[j].constraint.type != JOINT_BALL) continue;
            float u = (float)(localId - 1) / 11.0f;
            float armPhase = a->armWavePhase + (float)(modId % 8u) * 0.78539816f;
            float wave = 0.22f * sinf(armPhase - u * 4.4f) * (0.35f + 0.65f * u) * (speed > 0.01f ? 1.0f : 0.45f);
            float spread = 0.10f * a->armSpread;
            Vector3 axis = r->skeleton.joints[j].constraint.hingeAxis;
            if (Vec3_LengthSq(axis) < 1e-5f) axis = Vec3_Create(1, 0, 0);
            Rotate(&r->skeleton, p, (int)j, axis, wave + spread);
        }
        SkeletonPose_UpdateWorldTransforms(p, &r->skeleton);
    }
    for(int attempt=0;attempt<2;++attempt) {
    for(size_t i=0;i<r->limbCount;++i) {
        if(!r->limbs[i].walking)continue;
        IKChain c=r->limbs[i].chain; c.targetPosition=a->locomotion.feet[i].targetPosition;
        if((r->limbs[i].digitigrade||r->limbs[i].unguligrade)&&c.jointCount==4) {
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
