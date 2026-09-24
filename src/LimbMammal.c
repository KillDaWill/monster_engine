/** @file LimbMammal.c
 * @brief Construcción de miembros digitígrados sin dependencia del reptil.
 */
#include "LimbMammal.h"
#include "Limb.h"
#include "CreatureModuleInternal.h"
#include <math.h>

static bool Node(AnatomyGraph* g,uint32_t m,unsigned id,Vector3 c,float w,float h,
    const LimbPhenotype* p,bool digit) {
    return Module_Node(g,m,(uint16_t)id,c,w,h,2,digit?ANATOMY_ROLE_DIGIT:ANATOMY_ROLE_JOINT,
        digit?ANATOMY_REGION_DIGIT:p->role==LIMB_HIND?ANATOMY_REGION_HINDLIMB:ANATOMY_REGION_FORELIMB,
        p->side==LIMB_RIGHT?ANATOMY_SIDE_RIGHT:ANATOMY_SIDE_LEFT,p->development);
}
static Vector3 World(const AttachmentSlot* slot,Vector3 p) {
    return Vec3_Add(slot->position,Vec3_Add(Vec3_Scale(slot->side,p.x),
        Vec3_Add(Vec3_Scale(slot->up,p.y),Vec3_Scale(slot->forward,p.z))));
}
bool LimbMammal_Resolve(const LimbPhenotype* p,uint32_t module,const AttachmentSlot* slot,AnatomyGraph* g) {
    if (!p || !slot || !g || p->archetype!=LIMB_ARCHETYPE_MAMMAL || p->digitCount>CREATURE_MAX_DIGITS) return false;
    for (unsigned d=0;d<p->digitCount;++d) if (!p->digitPhalanges[d] || p->digitPhalanges[d]>5) return false;
    bool hind=p->role==LIMB_HIND;
    float side=p->side==LIMB_RIGHT?-1:1;
    float length=p->length*slot->scale*fmaxf(.01f,p->development);
    float r=p->thickness*slot->scale*fmaxf(.01f,p->development);
    float upper=(hind?.36f:.35f)*p->proximalScale;
    float lower=(hind?.40f:.53f)*p->middleScale;
    float distal=(hind?.24f:.12f)*p->distalScale;
    float sum=upper+lower+distal;
    upper/=sum; lower/=sum;
    float inset=-side*r*p->attachmentInset;
    float lateral=side*length*p->sprawl*.18f;
    Vector3 positions[4]={{inset,0,0},
        {inset+lateral,-length*upper,length*((hind?.16f:-.055f)+p->proximalSweep)},
        {inset+lateral,-length*(upper+lower),length*((hind?-.10f:0)+p->distalSweep)},
        {inset+lateral,-length,length*(hind?-.075f:.025f)}};
    float w[]={r*p->rootThicknessScale,r*.78f*p->middleThicknessScale,
        r*.40f*p->distalThicknessScale,r*1.05f*p->footScale*p->footWidthScale};
    float h[]={w[0],w[1],w[2],r*.48f*p->footScale*p->footHeightScale};
    for (unsigned i=0;i<4;++i) {
        if (!Node(g,module,i+1,World(slot,positions[i]),w[i],h[i],p,false)) return false;
        if (i && !Module_Edge(g,module,(uint16_t)i,Anatomy_MakeId(module,i),Anatomy_MakeId(module,i+1),BODY_CONNECTION_LIMB_SEGMENT,p->development)) return false;
        if (i) {
            BodyConnection* edge=&g->connections[g->connectionCount-1];
            edge->transverseAxis=slot->side;
            float fullness=i==1?r*p->proximalThicknessScale*(hind?.50f:.36f):i==2?r*(hind?.18f:.12f):0;
            edge->widthBulge=fullness;
            edge->heightBulge=fullness*(hind?1.15f:.85f);
            /* El metapodio conserva un tallo estrecho hasta el cojín distal. */
            if (i==3) edge->widthBulge=-r*.28f*p->footScale;
        }
    }
    if (!Module_Edge(g,module,4,slot->hostNode,Anatomy_MakeId(module,1),BODY_CONNECTION_SUPPORT,p->development)) return false;
    for (unsigned d=0;d<p->digitCount;++d) {
        AnatomyId previous=Anatomy_MakeId(module,4);
        float offset=((float)d-((float)p->digitCount-1)*.5f);
        float toeR=w[3]*.28f*p->digitThicknessScale;
        unsigned count=p->digitPhalanges[d]+2;
        for (unsigned j=0;j<count;++j) {
            float t=(float)j/(count-1);
            float radius=toeR*(1-.25f*t);
            Vector3 c=positions[3];
            c.x+=offset*w[3]*.44f + sinf(p->digitAngles[d])*p->digitSpread*t*w[3]*.5f;
            c.z+=w[3]*(.28f+t*1.35f*p->digitLengths[d]);
            /* Cada falange apoya en la misma suela; el carpo/tarso permanece elevado. */
            c.y+=-h[3]+radius;
            float yaw=side*p->footYaw,dx=c.x-positions[3].x,dz=c.z-positions[3].z;
            c.x=positions[3].x+cosf(yaw)*dx+sinf(yaw)*dz;
            c.z=positions[3].z-sinf(yaw)*dx+cosf(yaw)*dz;
            unsigned local=Limb_DigitLocalId(d,j);
            if (!Node(g,module,local,World(slot,c),radius,radius,p,true) ||
                !Module_Edge(g,module,(uint16_t)local,previous,Anatomy_MakeId(module,local),BODY_CONNECTION_DIGIT_SEGMENT,p->development)) return false;
            previous=Anatomy_MakeId(module,local);
        }
    }
    return true;
}
bool LimbMammal_BuildRigDescriptor(const LimbPhenotype* p,uint32_t module,const AnatomyGraph* g,LimbRigDescriptor* out) {
    if (!p || !g || !out) return false;
    *out=(LimbRigDescriptor){.moduleInstanceId=module,.jointCount=4,.role=p->role,.side=p->side,
        .archetype=LIMB_ARCHETYPE_MAMMAL,.locomotionLimb=p->development>.05f};
    for (unsigned i=0;i<4;++i) {
        out->joints[i]=Anatomy_MakeId(module,i+1);
        if (!AnatomyGraph_FindNode(g,out->joints[i])) return false;
    }
    out->endEffector=out->joints[3];
    const AnatomyNode* paw=AnatomyGraph_FindNode(g,out->endEffector);
    out->soleHeight=paw->heightRadius;
    for (size_t i=0;i<g->nodeCount;++i) {
        const AnatomyNode* n=&g->nodes[i];
        if (n->moduleInstanceId==module && n->role==ANATOMY_ROLE_DIGIT)
            out->soleHeight=fmaxf(out->soleHeight,paw->center.y-n->center.y+n->heightRadius);
    }
    return true;
}
void LimbMammal_ConfigureRig(const LimbRigDescriptor* d,const AnatomyGraph* g,Rig* rig) {
    if (!d || !g || !rig) return;
    for (size_t i=0;i<rig->limbCount;++i) {
        LimbRig* l=&rig->limbs[i];
        if (rig->skeleton.joints[l->rootJoint].anatomyId!=d->joints[0]) continue;
        l->soleHeight=d->soleHeight;
        for (unsigned j=0;j<4;++j) {
            SkeletonJoint* joint=&rig->skeleton.joints[l->chain.jointIndices[j]];
            joint->constraint=(JointConstraint){.type=JOINT_BALL,.hingeAxis={1,0,0},.maxSwingAngle=.45f,.maxTwistAngle=.20f};
            if (j==1 || j==2) joint->constraint=(JointConstraint){.type=JOINT_HINGE,.hingeAxis={1,0,0},.minAngle=-.65f,.maxAngle=.85f};
        }
    }
}
