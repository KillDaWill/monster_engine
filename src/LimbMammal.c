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
/* Una cápsula por uña; las estaciones restantes siguen siendo referencias
 * esqueléticas, sin volver a dibujar las falanges como botones de queratina. */
static bool ClovenFoot(const LimbPhenotype* p,uint32_t module,const AttachmentSlot* slot,
    AnatomyGraph* g,Vector3 foot,float r,float sole) {
    float width=r*.50f*p->footScale*p->footWidthScale*p->digitThicknessScale;
    float height=r*.90f*p->footScale*p->footHeightScale;
    for(unsigned d=0;d<2;++d) {
        float sign=d?1:-1;
        float yaw=(p->side==LIMB_RIGHT?-1:1)*p->footYaw+p->digitAngles[d];
        float offset=sign*(width+r*p->footScale*p->digitSpread*.5f);
        float length=r*1.15f*p->footScale*p->digitLengths[d];
        unsigned count=p->digitPhalanges[d]+2;
        AnatomyId previous=Anatomy_MakeId(module,4);
        for(unsigned j=0;j<count;++j) {
            Vector3 c=foot;
            float z=j==0?-.10f*r:j==1?-.17f*r:length-.17f*r;
            float x=offset*(j==0?.65f:1);
            c.x+=cosf(yaw)*x+sinf(yaw)*z;
            c.z+=-sinf(yaw)*x+cosf(yaw)*z;
            c.y+=j==0?height*.30f:-sole+height*.5f;
            float w=j==0?width*.65f:width*(j==1?1:.88f);
            float h=j==0?height*.45f:height*.5f*(j==1?p->hoofHeelHeight:1);
            /* Ambos centros conservan el mismo plano de suela; el SDF eleva
             * la corona anterior sin inclinar el apoyo del talón. */
            if(j>0)c.y=foot.y-sole+height*.5f*p->hoofHeelHeight;
            unsigned local=Limb_DigitLocalId(d,j);
            BodyConnectionKind kind=j==2?BODY_CONNECTION_DIGIT_SEGMENT:
                j!=0?BODY_CONNECTION_SUPPORT:BODY_CONNECTION_LIMB_SEGMENT;
            if(!Node(g,module,local,World(slot,c),w,h,p,true)||
               !Module_Edge(g,module,(uint16_t)local,previous,Anatomy_MakeId(module,local),kind,p->development))return false;
            BodyConnection* e=&g->connections[g->connectionCount-1];
            e->transverseAxis=Vec3_Add(Vec3_Scale(slot->side,cosf(yaw)),Vec3_Scale(slot->forward,-sinf(yaw)));
            if(j==2){e->crownInset=p->hoofWallSlope;e->toeRounding=p->hoofToeRoundness;}
            previous=Anatomy_MakeId(module,local);
        }
    }
    if(p->dewclawSize>0)for(unsigned d=0;d<2;++d) {
        float sign=d?1:-1,rr=width*p->dewclawSize;
        Vector3 a=foot,b=foot,root=foot;
        a.x+=sign*width*.9f;a.z-=r*.32f;a.y+=height*.30f;
        b=a;b.z-=rr*.8f;b.y-=rr*.4f;
        root.x+=sign*width*.5f;root.z-=r*.10f;root.y+=height*.30f;
        unsigned base=220+d*3;
        /* El pedículo usa su propio radio. Propagar el semialto completo del
         * menudillo al espolón creaba una membrana bajo la hendidura. */
        if(!Node(g,module,base,World(slot,root),rr*1.2f,rr*1.2f,p,true)||
           !Node(g,module,base+1,World(slot,a),rr,rr,p,true)||
           !Node(g,module,base+2,World(slot,b),rr*.7f,rr*.7f,p,true)||
           !Module_Edge(g,module,base,Anatomy_MakeId(module,4),Anatomy_MakeId(module,base),BODY_CONNECTION_SUPPORT,p->development)||
           !Module_Edge(g,module,base+1,Anatomy_MakeId(module,base),Anatomy_MakeId(module,base+1),BODY_CONNECTION_LIMB_SEGMENT,p->development)||
           !Module_Edge(g,module,base+2,Anatomy_MakeId(module,base+1),Anatomy_MakeId(module,base+2),BODY_CONNECTION_DIGIT_SEGMENT,p->development))return false;
        BodyConnection* e=&g->connections[g->connectionCount-1];e->crownInset=p->hoofWallSlope;e->toeRounding=p->hoofToeRoundness;
    }
    return true;
}
bool LimbMammal_Resolve(const LimbPhenotype* p,uint32_t module,const AttachmentSlot* slot,AnatomyGraph* g) {
    if (!p || !slot || !g || (p->archetype!=LIMB_ARCHETYPE_MAMMAL&&p->archetype!=LIMB_ARCHETYPE_UNGULATE) || p->digitCount>CREATURE_MAX_DIGITS) return false;
    bool hoof=p->archetype==LIMB_ARCHETYPE_UNGULATE;
    if(hoof&&p->digitCount!=2)return false;
    for (unsigned d=0;d<p->digitCount;++d) if (!p->digitPhalanges[d] || p->digitPhalanges[d]>5) return false;
    bool hind=p->role==LIMB_HIND;
    float side=p->side==LIMB_RIGHT?-1:1;
    float length=p->length*slot->scale*fmaxf(.01f,p->development);
    float r=p->thickness*slot->scale*fmaxf(.01f,p->development);
    float upper=(hoof?(hind?.32f:.29f):(hind?.36f:.35f))*p->proximalScale;
    float lower=(hoof?(hind?.36f:.40f):(hind?.40f:.53f))*p->middleScale;
    float distal=(hoof?(hind?.32f:.31f):(hind?.24f:.12f))*p->distalScale;
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
    if(hoof){
        /* El metapodio termina en un menudillo estrecho; no existe el cojín
         * común de la pata carnívora que rellenaría la hendidura del casco. */
        w[3]=r*.42f*p->distalThicknessScale;
        h[3]=r*.55f*p->footScale*p->footHeightScale;
    }
    for (unsigned i=0;i<4;++i) {
        if (!Node(g,module,i+1,World(slot,positions[i]),w[i],h[i],p,false)) return false;
        if (i && !Module_Edge(g,module,(uint16_t)i,Anatomy_MakeId(module,i),Anatomy_MakeId(module,i+1),BODY_CONNECTION_LIMB_SEGMENT,p->development)) return false;
        if (i) {
            BodyConnection* edge=&g->connections[g->connectionCount-1];
            edge->transverseAxis=slot->side;
            float fullness=i==1?r*p->proximalThicknessScale*(hoof?(hind?.15f:.08f):(hind?.50f:.36f)):i==2?r*(hoof?.035f:(hind?.18f:.12f)):0;
            edge->widthBulge=fullness;
            edge->heightBulge=fullness*(hind?1.15f:.85f);
            /* El metapodio conserva un tallo estrecho hasta el cojín distal. */
            if (i==3) edge->widthBulge=hoof?-r*.04f:-r*.28f*p->footScale;
        }
    }
    if (!Module_Edge(g,module,4,slot->hostNode,Anatomy_MakeId(module,1),BODY_CONNECTION_SUPPORT,p->development)) return false;
    if(hoof)return ClovenFoot(p,module,slot,g,positions[3],r,h[3]);
    for (unsigned d=0;d<p->digitCount;++d) {
        AnatomyId previous=Anatomy_MakeId(module,4);
        float offset=((float)d-((float)p->digitCount-1)*.5f);
        float toeR=(hoof?r*.48f*p->footScale*p->footWidthScale:w[3]*.28f)*p->digitThicknessScale;
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
            if (!Node(g,module,local,World(slot,c),radius,hoof?radius*.60f:radius,p,true) ||
                !Module_Edge(g,module,(uint16_t)local,previous,Anatomy_MakeId(module,local),
                    hoof&&j==0?BODY_CONNECTION_LIMB_SEGMENT:BODY_CONNECTION_DIGIT_SEGMENT,p->development)) return false;
            previous=Anatomy_MakeId(module,local);
        }
    }
    return true;
}
bool LimbMammal_BuildRigDescriptor(const LimbPhenotype* p,uint32_t module,const AnatomyGraph* g,LimbRigDescriptor* out) {
    if (!p || !g || !out) return false;
    *out=(LimbRigDescriptor){.moduleInstanceId=module,.jointCount=4,.role=p->role,.side=p->side,
        .archetype=p->archetype,.locomotionLimb=p->development>.05f,.flexionScale=p->flexionScale};
    for (unsigned i=0;i<4;++i) {
        out->joints[i]=Anatomy_MakeId(module,i+1);
        if (!AnatomyGraph_FindNode(g,out->joints[i])) return false;
    }
    out->endEffector=out->joints[3];
    const AnatomyNode* paw=AnatomyGraph_FindNode(g,out->endEffector);
    out->soleHeight=paw->heightRadius;
    /* En ungulígrados el plano de apoyo lo publica el menudillo. La corona
     * anterior es más alta que el talón y no define un radio bajo su centro. */
    if(p->archetype==LIMB_ARCHETYPE_UNGULATE)return true;
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
        l->unguligrade=d->archetype==LIMB_ARCHETYPE_UNGULATE;
        l->digitigrade=!l->unguligrade;
        /* Dos bisagras acopladas requieren converger también al pivotar. */
        l->chain.maxIterations=96;
        float flexion=d->flexionScale>0?d->flexionScale:1;
        for (size_t j=0;j<l->chain.jointCount;++j) {
            SkeletonJoint* joint=&rig->skeleton.joints[l->chain.jointIndices[j]];
            Vector3 axis=j+1<l->chain.jointCount?
                rig->skeleton.joints[l->chain.jointIndices[j+1]].restPosition:Vec3_Create(0,1,0);
            /* En hombro/cadera, twist sigue el hueso; X es flexión sagital,
             * no torsión. Confundir ambos ejes bloqueaba el paso del perro. */
            joint->constraint=(JointConstraint){.type=JOINT_BALL,.hingeAxis=Vec3_Normalize(axis),
                .maxSwingAngle=fminf(1.4f,(j==0?.7f:.9f)*flexion),.maxTwistAngle=j==0?.15f:.35f};
            if (j>0 && j+1<l->chain.jointCount) {
                Vector3 incoming=joint->restPosition;
                Vector3 outgoing=rig->skeleton.joints[l->chain.jointIndices[j+1]].restPosition;
                float bend=atan2f(Vec3_Cross(incoming,outgoing).x,Vec3_Dot(incoming,outgoing));
                /* Codo, rodilla y corvejón conservan el lado de flexión que
                 * resuelve la anatomía. Los límites son relativos al reposo. */
                float limit=(j+2==l->chain.jointCount?.5f:1.2f)*flexion;
                float min=-limit,max=limit;
                float margin=fminf(.02f,fabsf(bend)*.25f);
                if(bend>0)min=fmaxf(min,-bend+margin);
                if(bend<0)max=fminf(max,-bend-margin);
                joint->constraint=(JointConstraint){.type=JOINT_HINGE,.hingeAxis={1,0,0},.minAngle=min,.maxAngle=max};
            }
        }
    }
}
