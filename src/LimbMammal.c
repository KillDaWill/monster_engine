/** @file LimbMammal.c
 * @brief Construcción de miembros digitígrados sin dependencia del reptil.
 */
#include "LimbMammal.h"
#include "Limb.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
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
static bool UngulateFoot(const LimbPhenotype* p,uint32_t module,const AttachmentSlot* slot,
    AnatomyGraph* g,Vector3 foot,float r,float sole) {
    bool single=p->digitCount==1;
    bool three=p->digitCount==3;
    bool capsule=single&&p->anatomyProfile==LIMB_PROFILE_CURSORIAL;
    float width=r*(capsule?.68f:single?.95f:three?.43f:.50f)*p->footScale*p->footWidthScale*p->digitThicknessScale;
    float height=r*.90f*p->footScale*p->footHeightScale;
    for(unsigned d=0;d<p->digitCount;++d) {
        float sign=d?1:-1;
        float yaw=(p->side==LIMB_RIGHT?-1:1)*p->footYaw+p->digitAngles[d];
        float offset=single?0:sign*(width+r*p->footScale*p->digitSpread*.5f);
        float length=r*(capsule?1.0f:single?1.65f:1.15f)*p->footScale*p->digitLengths[d];
        float toeWidth=width;
        if(three) {
            float lateral=(float)d-1;
            offset=lateral*(width*1.78f+r*p->footScale*p->digitSpread*.4f);
            length*=d==1?1.27f:.95f;
            toeWidth*=d==1?1.13f:.88f;
        }
        unsigned count=p->digitPhalanges[d]+2;
        AnatomyId previous=Anatomy_MakeId(module,4);
        for(unsigned j=0;j<count;++j) {
            Vector3 c=foot;
            float z=j==0?-.10f*r:j==1?-.17f*r:length-.17f*r;
            if(three && d!=1)z-=r*.14f;
            float x=offset*(j==0?(three?.55f:.65f):1);
            c.x+=cosf(yaw)*x+sinf(yaw)*z;
            c.z+=-sinf(yaw)*x+cosf(yaw)*z;
            c.y+=j==0?height*.30f:-sole+height*.5f;
            float w=j==0?toeWidth*(three?.94f:.65f):toeWidth*(j==1?1:.88f);
            float h=j==0?height*.45f:height*.5f*(j==1?p->hoofHeelHeight:1);
            /* Ambos centros conservan el mismo plano de suela; el SDF eleva
             * la corona anterior sin inclinar el apoyo del talón. */
            if(j>0)c.y=foot.y-sole+height*.5f*p->hoofHeelHeight;
            unsigned local=Limb_DigitLocalId(d,j);
            BodyConnectionKind kind=j==2?BODY_CONNECTION_DIGIT_SEGMENT:
                j!=0?BODY_CONNECTION_SUPPORT:BODY_CONNECTION_LIMB_SEGMENT;
            if(j==2&&(capsule||three))kind=BODY_CONNECTION_HOOF_CAPSULE;
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
/* Cuatro controles IK conservan sus IDs. Las estaciones intermedias son
 * referencias de piel; los soportes primarios mantienen el árbol cinemático
 * sin emitir tubos duplicados. Todos los puntos se expresan en el marco local. */
static bool Cursorial(const LimbPhenotype* p,uint32_t module,const AttachmentSlot* slot,AnatomyGraph* g) {
    bool hind=p->role==LIMB_HIND;
    float side=p->side==LIMB_RIGHT?-1:1;
    float length=p->length*slot->scale*fmaxf(.01f,p->development);
    float r=p->thickness*slot->scale*fmaxf(.01f,p->development);
    float x=-side*r*p->attachmentInset;
    float lateral=side*length*p->sprawl*.18f;
    float upper=.25f*p->proximalScale,lower=.35f*p->middleScale,distal=.40f*p->distalScale;
    float total=upper+lower+distal;upper/=total;lower/=total;
    float sole=r*.55f*p->footScale*p->footHeightScale;
    Vector3 v[10]={
        {x,0,0},
        {x+lateral,-length*upper,length*((hind?.075f:-.065f)+p->proximalSweep)},
        {x+lateral,-length*(upper+lower),length*((hind?-.10f:-.015f)+p->distalSweep)},
        {x+lateral,-length,length*(hind?-.045f:.045f)},
        /* El ancla muscular proximal debe quedar dentro de la grupa; elevarla
         * por encima del lomo convertía el cuarto trasero en una esfera. */
        {x-side*r*.30f,r*(hind?.55f:.85f),-r*(hind?.35f:1.40f)}
    };
    v[5]=Vec3_Lerp(v[0],v[1],.45f);
    v[6]=Vec3_Lerp(v[1],v[2],.30f);
    v[8]=Vec3_Add(v[3],Vec3_Create(0,r*1.12f,-r*.88f));
    v[7]=Vec3_Lerp(v[2],v[8],.55f);
    v[9]=Vec3_Add(v[3],Vec3_Create(0,r*.48f,-r*.38f));
    float w[]={r*.75f*p->rootThicknessScale,r*.95f,r*.60f*p->distalThicknessScale,
        r*.43f*p->distalThicknessScale,r*(hind?.85f:.68f)*p->proximalThicknessScale,
        r*(hind?1.25f:1.0f)*p->proximalThicknessScale,r*(hind?1.02f:.85f)*p->middleThicknessScale,
        r*.46f*p->distalThicknessScale,r*.50f*p->distalThicknessScale,r*.44f*p->distalThicknessScale};
    float h[]={w[0]*(hind?1.25f:1.30f),w[1]*1.14f,w[2]*1.10f,sole,
        r*(hind?.85f:1.0f),w[5]*1.22f,w[6]*1.24f,w[7]*1.24f,w[8]*1.04f,w[9]};
    if(!hind) {
        /* Escápula oblicua sobre costillas: origen alto y medial, sección
         * aplanada en vez de una bola junto a la articulación del hombro. */
        v[4]=Vec3_Create(x-side*r*1.15f,length*.20f,-length*.17f);
        /* La piel proximal entra en el tórax sin desplazar el pivote IK.
         * La sección máxima queda arriba, no en una estación intermedia. */
        v[5]=Vec3_Lerp(v[0],v[1],.42f);
        v[5].x-=side*r*.22f;
        v[6]=Vec3_Lerp(v[1],v[2],.36f);
        w[4]=r*.40f*p->proximalThicknessScale;h[4]=r*.66f;
        w[0]=r*.58f*p->rootThicknessScale;h[0]=r*.80f;
        w[5]=r*.62f*p->proximalThicknessScale;h[5]=w[5]*1.12f;
        w[1]=r*.68f;h[1]=w[1]*1.08f;
        w[6]=r*.55f*p->middleThicknessScale;h[6]=w[6]*1.15f;
        w[2]=r*.46f*p->distalThicknessScale;h[2]=w[2]*1.08f;
    } else {
        /* Muslo, nalga, babilla y pierna siguen el eje locomotor real: el vientre
         * femoral acompaña la nalga hacia el centro sin sobresalir atrás ni abajo
         * delante del flanco, y la pierna se ahúsa progresivamente al corvejón. */
        v[4]=Vec3_Create(x-side*r*1.15f,length*.12f,length*.015f);
        v[5]=Vec3_Lerp(v[0],v[1],.44f);
        v[5].x-=side*r*.26f;
        v[5].z-=r*.44f;
        v[6]=Vec3_Lerp(v[1],v[2],.38f);
        v[6].z-=r*.04f;
        w[4]=r*.55f*p->proximalThicknessScale;h[4]=r*.88f;
        w[0]=r*.72f*p->rootThicknessScale;h[0]=r*1.32f;
        w[5]=r*.74f*p->proximalThicknessScale;h[5]=w[5]*1.34f;
        w[1]=r*.62f*Math_Lerp(1.f,p->middleThicknessScale,.5f);
        h[1]=r*.90f*Math_Lerp(1.f,p->proximalThicknessScale,.25f);
        w[6]=r*.52f*p->middleThicknessScale;h[6]=r*.72f*p->middleThicknessScale;
        w[2]=r*.42f*p->distalThicknessScale;h[2]=w[2]*1.60f;
    }
    for(unsigned i=0;i<10;++i)if(!Node(g,module,i+1,World(slot,v[i]),w[i],h[i],p,false))return false;
    for(unsigned i=1;i<4;++i)
        if(!Module_Edge(g,module,i,Anatomy_MakeId(module,i),Anatomy_MakeId(module,i+1),BODY_CONNECTION_SUPPORT,p->development))return false;
    if(!Module_Edge(g,module,4,slot->hostNode,Anatomy_MakeId(module,1),BODY_CONNECTION_SUPPORT,p->development)||
       !Module_Edge(g,module,5,slot->hostNode,Anatomy_MakeId(module,5),BODY_CONNECTION_SUPPORT,p->development))return false;
    const unsigned chain[]={5,1,6,2,7,3,8,9,10,4};
    for(unsigned i=0;i<9;++i) {
        /* Los tramos proximales del cuarto trasero se funden con pelvis y flanco
         * sin afectar la definición anatómica de babilla, corvejón y caña. */
        bool proximalMuscle=(i==0)||(hind&&i<=2);
        if(!Module_Edge(g,module,10+i,Anatomy_MakeId(module,chain[i]),Anatomy_MakeId(module,chain[i+1]),
            proximalMuscle?BODY_CONNECTION_MUSCLE_UNION:BODY_CONNECTION_LIMB_SEGMENT,p->development))return false;
        BodyConnection* edge=&g->connections[g->connectionCount-1];edge->transverseAxis=slot->side;
        /* El vientre muscular ya está en las estaciones: añadir otra plenitud
         * y suavizarla por separado duplicaba volumen junto al anfitrión. */
        if(proximalMuscle){edge->widthBulge=0;edge->heightBulge=0;}
    }
    return UngulateFoot(p,module,slot,g,v[3],r,sole);
}

/* Apoyo de suela plana, con placas ungueales radiales sobre el borde libre. */
static bool Columnar(const LimbPhenotype* p, uint32_t module, const AttachmentSlot* slot, AnatomyGraph* g) {
    bool hind=p->role==LIMB_HIND;
    float side=p->side==LIMB_RIGHT?-1:1;
    float scale=slot->scale*fmaxf(.01f,p->development);
    float length=p->length*scale;
    float r=p->thickness*scale;
    float x=-side*r*p->attachmentInset;
    float padWidth=r*p->footScale*p->footWidthScale;
    float sole=r*.48f*p->footScale*p->footHeightScale;
    const Vector3 joints[]={
        {x,0,0},
        {x,-length*.37f,length*((hind?.11f:-.035f)+p->proximalSweep)},
        {x,-length*.75f,length*((hind?-.07f:.008f)+p->distalSweep)},
        {x,-length,0}
    };
    const float w[]={r*p->rootThicknessScale,r*.83f*p->middleThicknessScale,
        r*.83f*p->distalThicknessScale,padWidth*.87f};
    const float h[]={w[0]*1.14f,w[1]*1.14f,w[2]*1.06f,sole};
    AnatomyId previous=slot->hostNode;
    for(unsigned i=0;i<4;++i) {
        AnatomyId id=Anatomy_MakeId(module,i+1);
        if(!Node(g,module,i+1,World(slot,joints[i]),w[i],h[i],p,false) ||
           !Module_Edge(g,module,i?i:4,previous,id,i?BODY_CONNECTION_LIMB_SEGMENT:BODY_CONNECTION_SUPPORT,p->development)) return false;
        BodyConnection* e=&g->connections[g->connectionCount-1];
        e->transverseAxis=slot->side;
        e->jointBlend=i==3?.05f:.82f;
        if(i==1)e->heightBulge=r*.18f*p->proximalThicknessScale;
        previous=id;
    }
    float halfSpan=padWidth*(hind?.57f:.46f);
    Vector3 rear=Vec3_Add(joints[3],Vec3_Create(0,0,-halfSpan));
    Vector3 front=Vec3_Add(joints[3],Vec3_Create(0,0,halfSpan));
    AnatomyId rearId=Anatomy_MakeId(module,210), frontId=Anatomy_MakeId(module,211);
    if(!Node(g,module,210,World(slot,rear),padWidth,sole,p,false) ||
       !Node(g,module,211,World(slot,front),padWidth,sole,p,false) ||
       !Module_Edge(g,module,210,previous,rearId,BODY_CONNECTION_SUPPORT,p->development) ||
       !Module_Edge(g,module,211,rearId,frontId,BODY_CONNECTION_PAD,p->development)) return false;
    BodyConnection* pad=&g->connections[g->connectionCount-1];
    pad->transverseAxis=slot->side; pad->crownInset=.035f; pad->toeRounding=.97f;

    float nailWidth=padWidth*.70f/fmaxf((float)p->nailCount,3);
    float nailHeight=sole*.43f;
    float padDepth=halfSpan+padWidth*.41f;
    for(unsigned i=0;i<p->nailCount;++i) {
        float angle=p->nailCount>1?Math_Lerp(-1.04f,1.04f,(float)i/(p->nailCount-1)):0;
        Vector3 radial=Vec3_Create(sinf(angle),0,cosf(angle));
        Vector3 a=Vec3_Add(joints[3],Vec3_Create(radial.x*padWidth*.90f,-sole+nailHeight,radial.z*padDepth*.87f));
        Vector3 b=Vec3_Add(a,Vec3_Scale(radial,padWidth*.098f));
        unsigned base=240+2*i;
        AnatomyId nail=Anatomy_MakeId(module,base);
        if(!Node(g,module,base,World(slot,a),nailWidth,nailHeight,p,true) ||
           !Node(g,module,base+1,World(slot,b),nailWidth*.91f,nailHeight*.90f,p,true) ||
           !Module_Edge(g,module,base,previous,nail,BODY_CONNECTION_SUPPORT,p->development) ||
           !Module_Edge(g,module,base+1,nail,Anatomy_MakeId(module,base+1),BODY_CONNECTION_NAIL,p->development)) return false;
        BodyConnection* e=&g->connections[g->connectionCount-1];
        e->transverseAxis=Vec3_Sub(Vec3_Scale(slot->side,radial.z),Vec3_Scale(slot->forward,radial.x));
        e->crownInset=.30f; e->toeRounding=.90f;
    }
    return true;
}

static Vector3 PawPoint(const AttachmentSlot* slot,Vector3 foot,float yaw,
    float x,float y,float z) {
    return World(slot,Vec3_Add(foot,Vec3_Create(
        cosf(yaw)*x+sinf(yaw)*z,y,cosf(yaw)*z-sinf(yaw)*x)));
}

/* El tejido plantar termina bajo las primeras falanges. Sus tramos de
 * soporte son articulaciones del grafo y no vuelven a dibujar la palma. */
static bool Palm(const LimbPhenotype* p,uint32_t module,const AttachmentSlot* slot,
    AnatomyGraph* g,Vector3 foot,float radius,float sole,float yaw) {
    bool hind=p->role==LIMB_HIND;
    float heelHeight=sole*.79f,ballHeight=sole*.92f;
    Vector3 heel=PawPoint(slot,foot,yaw,0,heelHeight-sole,-radius*(hind?.30f:.18f));
    Vector3 ball=PawPoint(slot,foot,yaw,0,ballHeight-sole,radius*(hind?.67f:.52f));
    unsigned a=LIMB_NODE_PALM_HEEL,b=LIMB_NODE_PALM_BALL;
    if(!Node(g,module,a,heel,radius*(hind?.44f:.53f),heelHeight,p,true) ||
       !Node(g,module,b,ball,radius*(hind?.93f:.83f),ballHeight,p,true) ||
       !Module_Edge(g,module,(uint16_t)a,Anatomy_MakeId(module,LIMB_NODE_AUTOPOD),
                    Anatomy_MakeId(module,a),BODY_CONNECTION_LIMB_SEGMENT,p->development) ||
       !Module_Edge(g,module,(uint16_t)b,Anatomy_MakeId(module,a),
                    Anatomy_MakeId(module,b),BODY_CONNECTION_MUSCLE_UNION,p->development))return false;
    BodyConnection* pad=&g->connections[g->connectionCount-1];
    pad->transverseAxis=Vec3_Sub(Vec3_Scale(slot->side,cosf(yaw)),Vec3_Scale(slot->forward,sinf(yaw)));
    return true;
}

bool LimbMammal_Resolve(const LimbPhenotype* p,uint32_t module,const AttachmentSlot* slot,AnatomyGraph* g) {
    if (!p || !slot || !g || (p->archetype!=LIMB_ARCHETYPE_MAMMAL&&p->archetype!=LIMB_ARCHETYPE_UNGULATE) || p->digitCount>CREATURE_MAX_DIGITS) return false;
    if(p->anatomyProfile==LIMB_PROFILE_COLUMNAR)
        return p->archetype==LIMB_ARCHETYPE_MAMMAL && Columnar(p,module,slot,g);
    bool hoof=p->archetype==LIMB_ARCHETYPE_UNGULATE;
    bool plantigrade=p->anatomyProfile==LIMB_PROFILE_PLANTIGRADE;
    bool macropod=p->anatomyProfile==LIMB_PROFILE_MACROPOD;
    if((plantigrade||macropod)&&(hoof||!p->digitCount))return false;
    if(hoof&&(p->digitCount<1||p->digitCount>3))return false;
    if(hoof&&p->digitCount==1&&p->dewclawSize>0)return false;
    for (unsigned d=0;d<p->digitCount;++d) if (!p->digitPhalanges[d] || p->digitPhalanges[d]>5) return false;
    if(p->anatomyProfile==LIMB_PROFILE_CURSORIAL)return hoof&&Cursorial(p,module,slot,g);
    bool compact=p->anatomyProfile==LIMB_PROFILE_COMPACT_UNGULATE;
    if(compact&&!hoof)return false;
    bool hind=p->role==LIMB_HIND;
    float side=p->side==LIMB_RIGHT?-1:1;
    float length=p->length*slot->scale*fmaxf(.01f,p->development);
    float r=p->thickness*slot->scale*fmaxf(.01f,p->development);
    float upper=(hoof?(hind?.32f:.29f):(macropod&&hind?.25f:(hind?.36f:.35f)))*p->proximalScale;
    float lower=(hoof?(hind?.36f:.40f):(macropod&&hind?.44f:(hind?.40f:.53f)))*p->middleScale;
    float distal=(hoof?(hind?.32f:.31f):(macropod&&hind?.31f:(hind?.24f:.12f)))*p->distalScale;
    float sum=upper+lower+distal;
    upper/=sum; lower/=sum;
    float inset=-side*r*p->attachmentInset;
    float lateral=side*length*p->sprawl*.18f;
    Vector3 positions[4]={{inset,0,0},
        {inset+lateral,-length*upper,length*((hind?(macropod?.24f:.16f):-.055f)+p->proximalSweep)},
        {inset+lateral,-length*(macropod&&hind?.90f:(upper+lower)),length*((hind?(macropod?-.16f:-.10f):0)+p->distalSweep)},
        {inset+lateral,macropod&&hind?(-length+r*.24f*p->footHeightScale):-length,length*(hind?(macropod?.32f*p->footScale:-.075f):.025f)}};
    float w[]={r*p->rootThicknessScale*(macropod&&hind?1.24f:1.0f),
        r*(macropod&&hind?.68f:.78f)*p->middleThicknessScale,
        r*(macropod&&hind?.38f:.40f)*p->distalThicknessScale,
        r*(macropod&&hind?.50f:1.05f)*p->footScale*p->footWidthScale};
    float h[]={w[0]*(macropod&&hind?1.10f:1.0f),
        w[1]*(macropod&&hind?1.14f:1.0f),
        w[2]*(macropod&&hind?1.18f:1.0f),
        r*(macropod&&hind?.28f:.48f)*p->footScale*p->footHeightScale};
    if(hoof){
        /* El metapodio termina en un menudillo estrecho; no existe el cojín
         * común de la pata carnívora que rellenaría la hendidura del casco. */
        w[3]=r*.42f*p->distalThicknessScale;
        if(p->digitCount==3)w[3]=r*.65f*p->distalThicknessScale;
        h[3]=r*.55f*p->footScale*p->footHeightScale;
    }
    float pawRadius=w[3];
    if(plantigrade)w[3]*=.56f;
    if(compact) {
        /* El corvejón conserva una sección sagital firme y la caña
         * se ahúsa hasta el menudillo sin ensancharse entre articulaciones. */
        w[0]*=hind?1.08f:.92f;
        h[0]*=hind?1.20f:1.03f;
        w[1]*=hind?1.08f:.80f;
        h[1]*=hind?1.12f:.93f;
        w[2]*=1.15f;
        h[2]*=1.28f;
    }
    for (unsigned i=0;i<4;++i) {
        if (!Node(g,module,i+1,World(slot,positions[i]),w[i],h[i],p,false)) return false;
        if (i && !Module_Edge(g,module,(uint16_t)i,Anatomy_MakeId(module,i),Anatomy_MakeId(module,i+1),BODY_CONNECTION_LIMB_SEGMENT,p->development)) return false;
        if (i) {
            BodyConnection* edge=&g->connections[g->connectionCount-1];
            edge->transverseAxis=slot->side;
            float fullness=i==1?r*p->proximalThicknessScale*(hoof?(hind?.15f:.08f):(macropod&&hind?.30f:(hind?.50f:.36f))):i==2?r*(hoof?.035f:(hind?.18f:.12f)):0;
            edge->widthBulge=fullness;
            edge->heightBulge=fullness*(hind?1.15f:.85f);
            if(compact)edge->jointBlend=1.0f;
            /* El metapodio conserva un tallo estrecho hasta el cojín distal; en macrópodos forma una viga plantar continua. */
            if (i==3 && !compact) edge->widthBulge=hoof?-r*.04f:(macropod&&hind?r*.04f:-r*.28f*p->footScale);
        }
    }
    if (!Module_Edge(g,module,4,slot->hostNode,Anatomy_MakeId(module,1),BODY_CONNECTION_SUPPORT,p->development)) return false;
    if(hoof)return UngulateFoot(p,module,slot,g,positions[3],r,h[3]);
    float footYaw=side*p->footYaw;
    if(plantigrade && !Palm(p,module,slot,g,positions[3],pawRadius,h[3],footYaw))return false;
    float rootSpacing=macropod&&hind?.34f:.44f;
    if(plantigrade) {
        /* Más dedos comparten la anchura de la palma en lugar de nacer fuera. */
        float halfSpan=fmaxf(1,((float)p->digitCount-1)*.5f);
        rootSpacing=fminf(rootSpacing,(hind?.84f:.73f)/halfSpan);
    }
    for (unsigned d=0;d<p->digitCount;++d) {
        AnatomyId previous=Anatomy_MakeId(module,plantigrade?LIMB_NODE_PALM_BALL:LIMB_NODE_AUTOPOD);
        float offset=((float)d-((float)p->digitCount-1)*.5f);
        float toeR=pawRadius*.28f*p->digitThicknessScale;
        unsigned count=p->digitPhalanges[d]+2;
        for (unsigned j=0;j<count;++j) {
            float t=(float)j/(count-1);
            float radius=toeR*(1-.25f*t);
            float x=(offset*rootSpacing+sinf(p->digitAngles[d])*p->digitSpread*t*.5f)*pawRadius;
            float base=plantigrade?(hind?.62f:.48f):.28f;
            float z=Math_Lerp(base,.28f+1.35f*p->digitLengths[d],t)*pawRadius;
            /* En el pie macrópodo la longitud es independiente del ancho. */
            if(macropod&&hind) {
                float digitSpan=length*.13f*p->footScale;
                z=Math_Lerp(base*pawRadius,.28f*pawRadius+digitSpan*p->digitLengths[d],t);
            }
            /* Cada falange apoya en la misma suela; el carpo/tarso permanece elevado. */
            Vector3 c=PawPoint(slot,positions[3],footYaw,x,radius-h[3],z);
            unsigned local=Limb_DigitLocalId(d,j);
            if (!Node(g,module,local,c,radius,radius,p,true) ||
                !Module_Edge(g,module,(uint16_t)local,previous,Anatomy_MakeId(module,local),
                    plantigrade&&j==0?BODY_CONNECTION_SUPPORT:BODY_CONNECTION_DIGIT_SEGMENT,p->development)) return false;
            previous=Anatomy_MakeId(module,local);
        }
        if(p->clawScale>0) {
            const AnatomyNode* toe=AnatomyGraph_FindNode(g,previous);
            float radius=toeR*.75f;
            float length=radius*p->clawScale;
            float yaw=side*p->footYaw+p->digitAngles[d]*.35f;
            Vector3 tip=Vec3_Add(toe->center,Vec3_Add(Vec3_Scale(slot->up,-radius*.30f),
                Vec3_Add(Vec3_Scale(slot->forward,cosf(yaw)*length),Vec3_Scale(slot->side,sinf(yaw)*length))));
            unsigned local=Limb_DigitLocalId(d,count);
            if(!Node(g,module,local,tip,radius*.17f,radius*.22f,p,true) ||
               !Module_Edge(g,module,(uint16_t)local,previous,Anatomy_MakeId(module,local),
                   BODY_CONNECTION_NAIL,p->development))return false;
            BodyConnection* edge=&g->connections[g->connectionCount-1];
            edge->crownInset=.40f;
            edge->toeRounding=.91f;
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
                float limit=(j+2==l->chain.jointCount?(d->archetype==LIMB_ARCHETYPE_UNGULATE?.5f:(d->role==LIMB_HIND?.78f:.46f)):1.2f)*flexion;
                float min=-limit,max=limit;
                float margin=fminf(.02f,fabsf(bend)*.25f);
                if(bend>0)min=fmaxf(min,-bend+margin);
                if(bend<0)max=fminf(max,-bend-margin);
                joint->constraint=(JointConstraint){.type=JOINT_HINGE,.hingeAxis={1,0,0},.minAngle=min,.maxAngle=max};
            }
        }
    }
}
