/** @file LongHair.c
 * @brief Generación estable sobre estaciones anatómicas y transporte de guías. */
#include "LongHair.h"
#include "Fur.h"
#include "LongHairScheduler.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static float Clamp(float x,float lo,float hi,float fallback){return isfinite(x)?fminf(hi,fmaxf(lo,x)):fallback;}
static Vector3 Unit(Vector3 p,Vector3 fallback){float l=Vec3_Length(p);return isfinite(l)&&l>1e-6f?Vec3_Scale(p,1/l):fallback;}
static Vector3 Product(Vector3 a,Vector3 b){return Vec3_Create(a.x*b.x,a.y*b.y,a.z*b.z);}
static uint32_t Hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
LongHairGroup LongHairGroup_Default(void){return (LongHairGroup){.id=1,.seed=17,.domain={.normal={0,1,0},.end=1,.patchRadius=.15f},
    .guideCount=48,.childrenPerGuide=32,.direction={1,-.3f,0},.length=.7f,.lengthVariation=.2f,.width=.0025f,
    .rootSpread=.025f,.clumping=.55f,.stiffness=.35f,.gravityInfluence=.65f,.density=1,
    .rootColor={35,24,18,255},.tipColor={64,43,28,255}};}
void LongHairGroup_Normalize(LongHairGroup* g){
    if(!g)return;
    if(g->domain.kind<LONG_HAIR_CURVE||g->domain.kind>LONG_HAIR_REGION)g->domain.kind=LONG_HAIR_CURVE;
    if(g->domain.anchorCount>LONG_HAIR_MAX_ANCHORS)g->domain.anchorCount=LONG_HAIR_MAX_ANCHORS;
    if(g->guideCount>256)g->guideCount=256;
    if(g->childrenPerGuide>64)g->childrenPerGuide=64;
    g->domain.normal=Unit(g->domain.normal,Vec3_Create(0,1,0));g->direction=Unit(g->direction,Vec3_Create(1,0,0));
    g->domain.offset.x=Clamp(g->domain.offset.x,-4,4,0);g->domain.offset.y=Clamp(g->domain.offset.y,-4,4,0);g->domain.offset.z=Clamp(g->domain.offset.z,-4,4,0);
    g->domain.start=Clamp(g->domain.start,0,1,0);g->domain.end=Clamp(g->domain.end,g->domain.start,1,1);
    g->domain.angle=Clamp(g->domain.angle,-6.284f,6.284f,0);g->domain.angularSpread=Clamp(g->domain.angularSpread,0,6.284f,0);
    g->domain.patchRadius=Clamp(g->domain.patchRadius,0,2,.15f);
    g->length=Clamp(g->length,0,8,.7f);g->lengthVariation=Clamp(g->lengthVariation,0,.9f,.2f);
    g->width=Clamp(g->width,.0001f,.03f,.0025f);g->rootSpread=Clamp(g->rootSpread,0,.3f,.025f);
    g->clumping=Clamp(g->clumping,0,1,.55f);g->stiffness=Clamp(g->stiffness,0,1,.35f);
    g->gravityInfluence=Clamp(g->gravityInfluence,0,2,.65f);g->density=Clamp(g->density,0,1,1);
    g->rootFullness=Clamp(g->rootFullness,0,1,0);g->bend=Clamp(g->bend,-2,2,0);
    g->wave=Clamp(g->wave,0,2,0);g->transparency=Clamp(g->transparency,0,1,0);
    g->verticalBias=Clamp(g->verticalBias,-1,1,0);
}
void LongHairSet_Free(LongHairSet* s){if(s){LongHairScheduler_Destroy(s->scheduler);free(s->guides);free(s->colliders);if(s->freeAttachedContext)s->freeAttachedContext(s->attachedContext);memset(s,0,sizeof(*s));}}
static void Bytes(uint64_t* h,const void* p,size_t size){const unsigned char* b=p;for(size_t i=0;i<size;++i){*h^=b[i];*h*=1099511628211ULL;}}
uint64_t LongHairGroups_Fingerprint(const LongHairGroup* groups,size_t count){
    uint64_t h=14695981039346656037ULL;Bytes(&h,&count,sizeof(count));if(count>LONG_HAIR_MAX_GROUPS||(count&&!groups))return 0;
    for(size_t i=0;i<count;++i){LongHairGroup g=groups[i];LongHairGroup_Normalize(&g);
#define FIELD(x) Bytes(&h,&g.x,sizeof(g.x))
        FIELD(id);FIELD(seed);FIELD(domain.kind);FIELD(domain.hostModuleId);FIELD(domain.region);FIELD(domain.anchorCount);
        for(size_t j=0;j<g.domain.anchorCount;++j)FIELD(domain.anchors[j]);
        FIELD(domain.offset);FIELD(domain.normal);FIELD(domain.angle);FIELD(domain.angularSpread);FIELD(domain.start);FIELD(domain.end);FIELD(domain.patchRadius);
        FIELD(guideCount);FIELD(childrenPerGuide);FIELD(direction);FIELD(length);FIELD(lengthVariation);FIELD(width);FIELD(rootSpread);
        FIELD(clumping);FIELD(stiffness);FIELD(gravityInfluence);FIELD(density);FIELD(rootFullness);FIELD(bend);FIELD(wave);FIELD(transparency);FIELD(verticalBias);FIELD(rootColor);FIELD(tipColor);
#undef FIELD
    }return h;
}
static Vector3 Radii(const AnatomyNode* n){Vector3 r=n->envelopeRadii;if(r.x<=0)r.x=n->widthRadius;if(r.y<=0)r.y=n->heightRadius;if(r.z<=0)r.z=n->widthRadius;return r;}
static Vector3 Normal(LongHairDistance d,const void* ctx,Vector3 p,float eps){
    return Unit(Vec3_Create(d(ctx,Vec3_Add(p,Vec3_Create(eps,0,0)))-d(ctx,Vec3_Sub(p,Vec3_Create(eps,0,0))),
        d(ctx,Vec3_Add(p,Vec3_Create(0,eps,0)))-d(ctx,Vec3_Sub(p,Vec3_Create(0,eps,0))),
        d(ctx,Vec3_Add(p,Vec3_Create(0,0,eps)))-d(ctx,Vec3_Sub(p,Vec3_Create(0,0,eps)))),Vec3_Create(0,1,0));
}
static size_t Nodes(const LongHairDomain* d,const AnatomyGraph* graph,const AnatomyNode** nodes){
    size_t count=0;
    if(d->kind!=LONG_HAIR_REGION){
        for(size_t i=0;i<d->anchorCount;++i){const AnatomyNode* n=AnatomyGraph_FindNode(graph,d->anchors[i]);
            if(!n||n->moduleInstanceId!=d->hostModuleId||n->development<=.001f)return 0;
            nodes[count++]=n;}
    }else{
        for(size_t i=0;i<graph->nodeCount;++i){const AnatomyNode* n=&graph->nodes[i];
            if(n->moduleInstanceId==d->hostModuleId&&n->region==d->region&&n->role==ANATOMY_ROLE_AXIAL&&n->development>.001f){
                if(count==LONG_HAIR_MAX_ANCHORS)return 0;
                size_t j=count++;while(j&&nodes[j-1]->id>n->id){nodes[j]=nodes[j-1];--j;}nodes[j]=n;}}
    }
    return count;
}
float LongHairGroup_Envelope(const LongHairGroup* g,float scale,float t){
    float emerge=fminf(1,t*(5+15*g->rootFullness)),lock=1-g->clumping*t*.65f;
    float spread=fminf(g->rootSpread,g->length*(1-g->lengthVariation)*.12f);
    return scale*(spread*emerge*lock+g->width*1.5f*(1+g->rootFullness));
}
typedef Vector3 (*ContactNormal)(const void*,Vector3);
static void Collide(LongHairSet* s,LongHairDistance distance,ContactNormal contactNormal,const void* ctx){
    if(!s||!distance)return;
    for(size_t i=0;i<s->count;++i){LongHairGuide* v=&s->guides[i];if(!v->visible)continue;
        const LongHairGroup* g=&s->groups[v->groupIndex];Vector3 original[LONG_HAIR_POINTS];memcpy(original,v->points,sizeof(original));
        float step=v->length/(LONG_HAIR_POINTS-1);
        /* PBD cinemático acotado: la raíz no se mueve y el contacto tiene prioridad.
         * Las muestras intermedias evitan atravesar un volumen entre dos nodos. */
        for(unsigned it=0;it<8;++it){
            float maxPush=0;
            if(it)for(unsigned p=LONG_HAIR_POINTS-1;p>1;--p){Vector3 delta=Vec3_Sub(v->points[p],v->points[p-1]);float length=Vec3_Length(delta);
                if(length>1e-6f){Vector3 correction=Vec3_Scale(delta,(length-step)/length*.5f);
                    v->points[p]=Vec3_Sub(v->points[p],correction);v->points[p-1]=Vec3_Add(v->points[p-1],correction);}}
            for(unsigned p=1;p<LONG_HAIR_POINTS;++p){
                Vector3 delta=Vec3_Sub(v->points[p],v->points[p-1]);
                v->points[p]=Vec3_Add(v->points[p-1],Vec3_Scale(Unit(delta,v->root.restDirection),step));
                for(unsigned q=1;q<=2;++q){float u=q/2.f,t=(p-1+u)/(LONG_HAIR_POINTS-1);
                    Vector3 pos=q==2?v->points[p]:LongHairGuide_Evaluate(v,t);
                    float clearance=.003f*s->scale+LongHairGroup_Envelope(g,s->scale,t),d=distance(ctx,pos);
                    if(isfinite(d)&&d<clearance){Vector3 n=contactNormal?contactNormal(ctx,pos):Normal(distance,ctx,pos,.001f*s->scale);
                        float push=fminf((clearance-d)/u,.25f*s->scale);
                        maxPush=fmaxf(maxPush,push);
                        v->points[p]=Vec3_Add(v->points[p],Vec3_Scale(n,push));}
                }
            }
            if(maxPush<.0005f*s->scale)break;
        }
        /* Pulido de la curva continua: repartir el contacto entre los dos
         * controles positivos de Catmull evita dejar una panza entre nodos. */
        for(unsigned it=0;it<3;++it){float correction=0;
            for(unsigned p=0;p<LONG_HAIR_POINTS-1;++p)for(unsigned q=1;q<=4;++q){float u=q/4.f,t=(p+u)/9.f;
                Vector3 pos=LongHairGuide_Evaluate(v,t);float d=distance(ctx,pos),clearance=s->scale*(.003f+g->width*1.5f*(1+g->rootFullness)*fminf(1,t*9));
                if(!isfinite(d)||d>=clearance)continue;
                Vector3 normal=contactNormal?contactNormal(ctx,pos):Normal(distance,ctx,pos,.001f*s->scale);
                float wb=p?1-2.5f*u*u+1.5f*u*u*u:0,wc=.5f*u+2*u*u-1.5f*u*u*u,denom=wb*wb+wc*wc;
                float push=fminf(clearance-d,.1f*s->scale);if(denom<1e-5f)continue;
                if(p)v->points[p]=Vec3_Add(v->points[p],Vec3_Scale(normal,push*wb/denom));
                v->points[p+1]=Vec3_Add(v->points[p+1],Vec3_Scale(normal,push*wc/denom));correction=fmaxf(correction,push);}
            if(correction<.0002f*s->scale)break;
        }
        v->contactPenetration=0;
        for(unsigned p=0;p<LONG_HAIR_POINTS;++p){v->corrections[p]=Vec3_Sub(v->points[p],original[p]);v->clearance[p]=1e6f;}
        float rootD=distance(ctx,v->points[1]);
        v->rootClearance=isfinite(rootD)?fmaxf(0,rootD-.003f*s->scale-g->width*s->scale*1.5f*(1+g->rootFullness)):0;
        /* Certificado local del tubo sobre la curva final, no sólo sus nodos. */
        for(unsigned p=0;p<LONG_HAIR_POINTS-1;++p){float safe=1e6f;
            for(unsigned q=0;q<=4;++q){Vector3 pos=LongHairGuide_Evaluate(v,(p+q/4.f)/9.f);float d=distance(ctx,pos);
                if(!isfinite(d)){safe=0;v->rootClearance=0;v->contactPenetration=INFINITY;break;}safe=fminf(safe,d);v->contactPenetration=fmaxf(v->contactPenetration,-d);}
            Vector3 a=v->points[p?p-1:0],b=v->points[p],c=v->points[p+1],d=v->points[p+2<LONG_HAIR_POINTS?p+2:LONG_HAIR_POINTS-1];
            Vector3 d0=Vec3_Scale(Vec3_Sub(c,a),.5f),d2=Vec3_Scale(Vec3_Sub(d,b),.5f),d1=Vec3_Sub(Vec3_Scale(Vec3_Sub(c,b),3),Vec3_Add(d0,d2));
            float speed=fmaxf(Vec3_Length(d0),fmaxf(Vec3_Length(d1),Vec3_Length(d2)));
            safe=fmaxf(0,safe-speed*.125f-.003f*s->scale-g->width*s->scale*1.5f*(1+g->rootFullness));
            v->clearance[p]=fminf(v->clearance[p],safe);v->clearance[p+1]=fminf(v->clearance[p+1],safe);
        }
    }
}
void LongHairSet_Collide(LongHairSet* s,LongHairDistance distance,const void* ctx){Collide(s,distance,NULL,ctx);}
typedef struct PosedColliders {
    const LongHairCollider* volumes;size_t count;Vector3 gradient;Quaternion rotation;float limit;
    LongHairDistance distance;const void* context;Vector3 restOrigin,posedOrigin;Quaternion inverse;float epsilon;bool extraNormal;
} PosedColliders;
static float AttachedDistance(const void* context,Vector3 p){const PosedColliders* c=context;
    Vector3 rest=Vec3_Add(c->restOrigin,Quat_RotateVector(c->inverse,Vec3_Sub(p,c->posedOrigin)));
    return c->distance(c->context,rest);
}
static float ColliderDistance(const void* context,Vector3 p){
    PosedColliders* c=(PosedColliders*)context;float best=c->limit;c->extraNormal=false;
    for(size_t i=0;i<c->count;++i){const LongHairCollider* v=&c->volumes[i];
        /* Bucle caliente escalar: evita docenas de llamadas Vector/Quaternion
         * por volumen y descarta por cápsula antes de rotar la consulta. */
        float x=p.x-v->center.x,y=p.y-v->center.y,z=p.z-v->center.z;
        float ax=v->end.x-v->center.x,ay=v->end.y-v->center.y,az=v->end.z-v->center.z;
        float l2=ax*ax+ay*ay+az*az,t=l2>1e-8f?fminf(1,fmaxf(0,(x*ax+y*ay+z*az)/l2)):0;
        x-=ax*t;y-=ay*t;z-=az*t;
        float rx=v->radii.x+(v->endRadii.x-v->radii.x)*t,ry=v->radii.y+(v->endRadii.y-v->radii.y)*t,rz=v->radii.z+(v->endRadii.z-v->radii.z)*t;
        float radius=fmaxf(rx,fmaxf(ry,rz));
        if(best>=0&&x*x+y*y+z*z>(radius+best)*(radius+best))continue;
        Quaternion q=v->inverseRotation;
        float tx=2*(q.y*z-q.z*y),ty=2*(q.z*x-q.x*z),tz=2*(q.x*y-q.y*x);
        float lx=x+q.w*tx+q.y*tz-q.z*ty,ly=y+q.w*ty+q.z*tx-q.x*tz,lz=z+q.w*tz+q.x*ty-q.y*tx;
        float ix=1/(rx*rx),iy=1/(ry*ry),iz=1/(rz*rz);
        float k0=sqrtf(lx*lx*ix+ly*ly*iy+lz*lz*iz),gx=lx*ix,gy=ly*iy,gz=lz*iz,k1=sqrtf(gx*gx+gy*gy+gz*gz);
        float d=(k1>1e-6f?k0*(k0-1)/k1:-fminf(rx,fminf(ry,rz)))+v->surfaceOffset;
        if(d<best){best=d;c->gradient=(Vector3){gx,gy,gz};c->rotation=(Quaternion){-q.x,-q.y,-q.z,q.w};}
    }
    if(c->distance){float d=AttachedDistance(c,p);if(d<best){best=d;c->extraNormal=true;}}
    return best;
}
static Vector3 ColliderNormal(const void* context,Vector3 p){const PosedColliders* c=context;
    if(c->extraNormal)return Normal(AttachedDistance,c,p,c->epsilon);
    return Unit(Quat_RotateVector(c->rotation,c->gradient),Vec3_Create(0,1,0));}
typedef struct CollideWork {LongHairSet* set;LongHairDistance distance;ContactNormal normal;const void* context;} CollideWork;
static bool CollideRange(void* context,int start,int end,int threadIndex){(void)threadIndex;CollideWork* w=context;
    for(int i=start;i<end;++i){LongHairSet one=*w->set;one.guides=&w->set->guides[i];one.count=1;Collide(&one,w->distance,w->normal,w->context);}return true;}
typedef struct ChildWork {LongHairSet* set;LongHairDistance distance;const void* context;float scale;} ChildWork;
static bool ChildrenRange(void* context,int start,int end,int threadIndex){(void)threadIndex;ChildWork* w=context;LongHairSet* next=w->set;
    for(int ii=start;ii<end;++ii){size_t i=(size_t)ii;LongHairGuide* v=&next->guides[i];const LongHairGroup* g=&next->groups[v->groupIndex];
        if(!v->visible||g->rootFullness<=0||g->domain.kind!=LONG_HAIR_CURVE||g->childrenPerGuide<2)continue;
        size_t neighbors[4]={i,i,i,i};float distances[4]={INFINITY,INFINITY,INFINITY,INFINITY};
        for(size_t j=0;j<next->count;++j){const LongHairGuide* other=&next->guides[j];if(j==i||!other->visible||other->groupIndex!=v->groupIndex)continue;
            if(Vec3_Dot(v->posedNormal,other->posedNormal)<.9f)continue;
            Vector3 flow=Unit(Vec3_Sub(v->points[9],v->points[0]),v->root.restDirection),otherFlow=Unit(Vec3_Sub(other->points[9],other->points[0]),other->root.restDirection);
            if(Vec3_Dot(flow,otherFlow)<.9f)continue;
            float d=Vec3_Distance(v->points[0],other->points[0]);if(d>g->length*w->scale*.35f)continue;
            for(unsigned n=0;n<4;++n)if(d<distances[n]){for(unsigned k=3;k>n;--k){neighbors[k]=neighbors[k-1];distances[k]=distances[k-1];}neighbors[n]=j;distances[n]=d;break;}}
        for(unsigned c=0;c<g->childrenPerGuide;++c){size_t j=neighbors[c%4];if(j==i)j=neighbors[0];if(j==i)continue;const LongHairGuide* other=&next->guides[j];
            uint32_t seed=Hash(v->root.stableRootId^g->seed^Hash(c+1));float weight=(.05f+.9f*Fur_Random(seed))*g->rootFullness;
            for(unsigned attempt=0;attempt<5;++attempt,weight*=.5f){Vector3 root=Vec3_Lerp(v->points[0],other->points[0],weight),normal=Unit(Vec3_Lerp(v->posedNormal,other->posedNormal,weight),v->posedNormal);float lift=0;
                if(w->distance)for(unsigned it=0;it<12;++it){float d=w->distance(w->context,Vec3_Add(root,Vec3_Scale(normal,lift)));if(!isfinite(d))break;float delta=.002f*w->scale-d;lift+=Clamp(delta,-.02f*w->scale,.02f*w->scale,0);if(fabsf(delta)<.00001f*w->scale)break;}
                if(w->distance&&fabsf(w->distance(w->context,Vec3_Add(root,Vec3_Scale(normal,lift)))-.002f*w->scale)>.0001f*w->scale)continue;
                bool safe=true;if(w->distance)for(unsigned q=1;q<=72;++q){float t=q/72.f;Vector3 point=Vec3_Add(Vec3_Lerp(LongHairGuide_Evaluate(v,t),LongHairGuide_Evaluate(other,t),weight),Vec3_Scale(normal,lift));float d=w->distance(w->context,point);if(!isfinite(d)||d<.0005f*w->scale){safe=false;break;}}
                if(!safe)continue;
                v->follicles[c][0]=(float)j;v->follicles[c][1]=weight;v->follicles[c][2]=lift;v->follicles[c][3]=1;break;}}
    }return true;}
static bool LongHairSet_BuildInternal(LongHairSet* dst,const LongHairGroup* groups,size_t count,const AnatomyGraph* graph,float scale,LongHairDistance distance,const void* context,LongHairScheduler* scheduler){
    if(!dst||!graph||graph->nodeCount>ANATOMY_MAX_NODES||graph->connectionCount>ANATOMY_MAX_CONNECTIONS||count>LONG_HAIR_MAX_GROUPS||(count&&!groups)||!isfinite(scale)||scale<=0)return false;
    LongHairSet next={.groupCount=count,.scale=scale,.generation=dst->generation+1};size_t capacity=0;
    for(size_t k=0;k<count;++k){next.groups[k]=groups[k];LongHairGroup_Normalize(&next.groups[k]);capacity+=next.groups[k].guideCount;
        if(!groups[k].id)return false;
        for(size_t j=0;j<k;++j)if(groups[k].id==groups[j].id)return false;}
    if(!capacity){LongHairScheduler* owner=dst->scheduler;dst->scheduler=NULL;LongHairSet_Free(dst);next.scheduler=owner;*dst=next;return true;}
    next.guides=calloc(capacity?capacity:1,sizeof(*next.guides));if(!next.guides)return false;
    next.colliders=calloc(graph->nodeCount+graph->connectionCount+1,sizeof(*next.colliders));
    if(!next.colliders){LongHairSet_Free(&next);return false;}
    for(size_t i=0;i<graph->nodeCount;++i){const AnatomyNode* n=&graph->nodes[i];Vector3 r=Radii(n);
        if(n->development>.001f&&fminf(r.x,fminf(r.y,r.z))>.001f)
            next.colliders[next.colliderCount++]=(LongHairCollider){.id=n->id,.endId=n->id,.center=n->center,.end=n->center,
                .radii=r,.endRadii=r,.inverseRotation={0,0,0,1}};}
    for(size_t i=0;i<graph->connectionCount;++i){const BodyConnection* edge=&graph->connections[i];
        if(graph->dormantConnections[i]||edge->development<=.001f||edge->kind==BODY_CONNECTION_SUPPORT||edge->kind==BODY_CONNECTION_MUSCLE_UNION)continue;
        const AnatomyNode* a=AnatomyGraph_FindNode(graph,edge->fromId),*b=AnatomyGraph_FindNode(graph,edge->toId);
        if(!a||!b)continue;
        Vector3 ra=Radii(a),rb=Radii(b);
        if(fminf(ra.x,fminf(ra.y,ra.z))<=.001f||fminf(rb.x,fminf(rb.y,rb.z))<=.001f)continue;
        next.colliders[next.colliderCount++]=(LongHairCollider){.id=a->id,.endId=b->id,.center=a->center,.end=b->center,
            .radii=ra,.endRadii=rb,.inverseRotation={0,0,0,1}};
    }
    for(size_t k=0;k<count;++k){const LongHairGroup* g=&next.groups[k];const LongHairDomain* d=&g->domain;
        const AnatomyNode* nodes[LONG_HAIR_MAX_ANCHORS];size_t n=Nodes(d,graph,nodes);
        if(!n||!g->childrenPerGuide||g->density<=0||g->length<=0)continue;
        for(unsigned i=0;i<g->guideCount;++i){LongHairGuide v={.groupIndex=(unsigned)k,.visible=true,.visibility=1};
            uint32_t id=Hash(g->id^Hash(i+1)^g->seed);float u=(i+Fur_Random(id))/g->guideCount,a=d->start+(d->end-d->start)*u;
            float f=a*(n-1);size_t j=n==1?0:(size_t)f;if(j>=n-1)j=n>1?n-2:0;
            float t=n==1?0:f-j;const AnatomyNode *na=nodes[j],*nb=nodes[n==1?0:j+1];
            Vector3 center=Vec3_Lerp(na->center,nb->center,t),radii=Vec3_Lerp(Radii(na),Radii(nb),t);
            Vector3 tangent=Unit(Vec3_Sub(nb->center,na->center),Vec3_Create(0,0,1));
            Vector3 fallback=fabsf(tangent.y)>.9f?Vec3_Create(1,0,0):Vec3_Create(0,1,0);
            fallback=Unit(Vec3_Sub(fallback,Vec3_Scale(tangent,Vec3_Dot(fallback,tangent))),Vec3_Create(1,0,0));
            Vector3 normal=Unit(Vec3_Sub(d->normal,Vec3_Scale(tangent,Vec3_Dot(d->normal,tangent))),fallback);
            Vector3 side=Unit(Vec3_Cross(tangent,normal),Vec3_Create(1,0,0));
            float angle=d->angle+(Fur_Random(id^123u)-.5f)*d->angularSpread;
            normal=Vec3_Add(Vec3_Scale(normal,cosf(angle)),Vec3_Scale(side,sinf(angle)));
            Vector3 root=Vec3_Add(center,Product(radii,Vec3_Add(d->offset,normal)));
            if(d->kind==LONG_HAIR_PATCH){float rr=sqrtf(u)*d->patchRadius,theta=6.2831853f*Fur_Random(id^321u);
                root=Vec3_Add(root,Product(radii,Vec3_Add(Vec3_Scale(side,rr*cosf(theta)),Vec3_Scale(tangent,rr*sinf(theta)))));}
            Vector3 candidate=root;
            if(distance){
                for(unsigned it=0;it<12;++it){float s=distance(context,root);if(!isfinite(s)){v.visible=false;break;}
                    if(fabsf(s)<.0001f*scale)break;
                    Vector3 grad=Normal(distance,context,root,.001f*scale);
                    root=Vec3_Sub(root,Vec3_Scale(grad,Clamp(s,-.2f*scale,.2f*scale,0)));}
                normal=Normal(distance,context,root,.001f*scale);
                if(fabsf(distance(context,root))>.01f*scale)v.visible=false;
                /* Una estación tapada no debe trasladar sus raíces a la piel de
                 * otro volumen próximo (p. ej., soporte insertado en el tronco). */
                float reach=fminf(radii.x,fminf(radii.y,radii.z));
                if(Vec3_Distance(root,candidate)>reach)v.visible=false;
            }
            root=Vec3_Add(root,Vec3_Scale(normal,.002f*scale));
            v.posedNormal=normal;
            Vector3 axis=fabsf(normal.y)>.9f?Vec3_Create(1,0,0):Vec3_Create(0,1,0);
            v.restSide=v.posedSide=Unit(Vec3_Cross(tangent,normal),Unit(Vec3_Cross(normal,axis),Vec3_Create(0,0,1)));
            Vector3 direction=Unit(Vec3_Add(g->direction,Vec3_Scale(normal,.35f)),normal);
            v.root=(LongHairRoot){.groupId=g->id,.moduleId=d->hostModuleId,.stableRootId=Hash(g->id^Hash(i+1)),
                .fromId=na->id,.toId=nb->id,.restPosition=root,.restNormal=normal,.restDirection=direction,.rootParameter=a,.segmentParameter=t};
            v.length=g->length*scale*(1+g->lengthVariation*(2*Fur_Random(id^981u)-1));
            v.restPoints[0]=root;
            for(unsigned p=1;p<LONG_HAIR_POINTS;++p){float h=(float)p/(LONG_HAIR_POINTS-1);
                float phase=Fur_Random(id^717u)*6.2831853f;
                Vector3 flow=Vec3_Add(Vec3_Scale(direction,1-h*(1-g->stiffness)*.7f),Vec3_Create(0,-g->gravityInfluence*h*h*(1-g->stiffness)*3,0));
                flow=Vec3_Add(flow,Vec3_Scale(normal,(1-h)*( .12f+.25f*Fur_Random(id^615u))));
                flow=Vec3_Add(flow,Vec3_Scale(side,.24f*sinf(h*5+phase)*h+.25f*(Fur_Random(id^311u)-.5f)));
                flow=Vec3_Add(flow,Vec3_Scale(v.restSide,g->bend*h+g->wave*sinf(h*12.5663706f)*h));
                /* La caída se incorpora progresivamente para salir de la piel;
                 * el extremo erguido puede ser recto desde la propia inserción. */
                if(g->verticalBias!=0){float vertical=fabsf(g->verticalBias)*(g->verticalBias<0?fminf(1,h*4):1);
                    flow=Vec3_Lerp(Unit(flow,direction),Vec3_Create(0,g->verticalBias<0?-1:1,0),vertical);}
                v.restPoints[p]=Vec3_Add(v.restPoints[p-1],Vec3_Scale(Unit(flow,direction),v.length/(LONG_HAIR_POINTS-1)));}
            memcpy(v.points,v.restPoints,sizeof(v.points));v.rootValid=v.visible;next.guides[next.count++]=v;
            for(unsigned p=0;p<LONG_HAIR_POINTS;++p)next.guides[next.count-1].clearance[p]=1e6f;
            next.guides[next.count-1].rootClearance=1e6f;
        }
    }
    if(distance){if(scheduler){CollideWork work={&next,distance,NULL,context};
            if(LongHairScheduler_ParallelFor(scheduler,(int)next.count,CollideRange,&work)!=LONG_HAIR_SCHEDULER_OK){LongHairSet_Free(&next);return false;}}
        else LongHairSet_Collide(&next,distance,context);
        for(size_t i=0;i<next.count;++i)memcpy(next.guides[i].restPoints,next.guides[i].points,sizeof(next.guides[i].points));}
    /* Los hijos de una curva frondosa nacen entre inserciones próximas, no en
     * el eje de un tubo. Proyectar cada folículo al SDF sólo durante la construcción;
     * la GPU interpola las dos guías ya transportadas durante la animación. */
    if(scheduler){ChildWork work={&next,distance,context,scale};
        if( next.count>(size_t)INT32_MAX || LongHairScheduler_ParallelFor(scheduler,(int)next.count,ChildrenRange,&work)!=LONG_HAIR_SCHEDULER_OK){LongHairSet_Free(&next);return false;}}
    else for(size_t i=0;i<next.count;++i){LongHairGuide* v=&next.guides[i];const LongHairGroup* g=&next.groups[v->groupIndex];
        if(!v->visible||g->rootFullness<=0||g->domain.kind!=LONG_HAIR_CURVE||g->childrenPerGuide<2)continue;
        size_t neighbors[4]={i,i,i,i};float distances[4]={INFINITY,INFINITY,INFINITY,INFINITY};
        for(size_t j=0;j<next.count;++j){const LongHairGuide* other=&next.guides[j];
            if(j==i||!other->visible||other->groupIndex!=v->groupIndex)continue;
            if(Vec3_Dot(v->posedNormal,other->posedNormal)<.9f)continue;
            Vector3 flow=Unit(Vec3_Sub(v->points[9],v->points[0]),v->root.restDirection);
            Vector3 otherFlow=Unit(Vec3_Sub(other->points[9],other->points[0]),other->root.restDirection);
            if(Vec3_Dot(flow,otherFlow)<.9f)continue;
            float d=Vec3_Distance(v->points[0],other->points[0]);
            if(d>g->length*scale*.35f)continue;
            for(unsigned n=0;n<4;++n)if(d<distances[n]){
                for(unsigned k=3;k>n;--k){neighbors[k]=neighbors[k-1];distances[k]=distances[k-1];}
                neighbors[n]=j;distances[n]=d;break;}}
        for(unsigned c=0;c<g->childrenPerGuide;++c){size_t j=neighbors[c%4];if(j==i)j=neighbors[0];if(j==i)continue;
            const LongHairGuide* other=&next.guides[j];uint32_t seed=Hash(v->root.stableRootId^g->seed^Hash(c+1));
            float weight=(.05f+.9f*Fur_Random(seed))*g->rootFullness;
            /* Acercar el folículo a su guía si el cuello es demasiado convexo;
             * conservar una inserción propia antes de recurrir al tubo. */
            for(unsigned attempt=0;attempt<5;++attempt,weight*=.5f){
            Vector3 root=Vec3_Lerp(v->points[0],other->points[0],weight);
            Vector3 normal=Unit(Vec3_Lerp(v->posedNormal,other->posedNormal,weight),v->posedNormal);
            float lift=0;
            if(distance)for(unsigned it=0;it<12;++it){float d=distance(context,Vec3_Add(root,Vec3_Scale(normal,lift)));
                if(!isfinite(d))break;
                float delta=.002f*scale-d;lift+=Clamp(delta,-.02f*scale,.02f*scale,0);
                if(fabsf(delta)<.00001f*scale)break;}
            if(distance&&fabsf(distance(context,Vec3_Add(root,Vec3_Scale(normal,lift)))-.002f*scale)>.0001f*scale)continue;
            /* Dos guías válidas pueden rodear un obstáculo por lados distintos.
             * No aceptar su cuerda interior: conservar el hijo tubular existente
             * cuando la interpolación no supera la auditoría del campo de reposo. */
            bool safe=true;
            if(distance)for(unsigned q=1;q<=72;++q){float t=q/72.f;
                Vector3 point=Vec3_Add(Vec3_Lerp(LongHairGuide_Evaluate(v,t),LongHairGuide_Evaluate(other,t),weight),Vec3_Scale(normal,lift));
                float d=distance(context,point);
                if(!isfinite(d)||d<.0005f*scale){safe=false;break;}}
            if(!safe)continue;
            v->follicles[c][0]=(float)j;v->follicles[c][1]=weight;v->follicles[c][2]=lift;v->follicles[c][3]=1;
            break;
            }
        }
    }
    for(size_t i=0;i<next.count;++i){memcpy(next.guides[i].restClearance,next.guides[i].clearance,sizeof(next.guides[i].clearance));next.guides[i].restPenetration=next.guides[i].contactPenetration;next.guides[i].restRootClearance=next.guides[i].rootClearance;}
    LongHairScheduler* owner=dst->scheduler;dst->scheduler=NULL;LongHairSet_Free(dst);next.scheduler=owner;*dst=next;return true;
}
bool LongHairSet_Build(LongHairSet* dst,const LongHairGroup* groups,size_t count,const AnatomyGraph* graph,float scale,LongHairDistance distance,const void* context){
    return LongHairSet_BuildInternal(dst,groups,count,graph,scale,distance,context,NULL);
}
bool LongHairSet_BuildParallel(LongHairSet* dst,const LongHairGroup* groups,size_t count,const AnatomyGraph* graph,float scale,LongHairDistance distance,const void* context,LongHairScheduler* scheduler){
    return LongHairSet_BuildInternal(dst,groups,count,graph,scale,distance,context,scheduler);
}
bool LongHairSet_Pose(LongHairSet* s,const Skeleton* skeleton,const SkeletonPose* pose){
    if(!s||!skeleton||!pose||pose->jointCount!=skeleton->jointCount)return false;
    if(!s->count)return true;
    SkeletonPose rest;if(!SkeletonPose_Init(&rest,skeleton))return false;bool valid=true;
    bool exactRest=true;
    for(size_t i=0;i<skeleton->jointCount;++i){Quaternion a=rest.joints[i].worldRotation,b=pose->joints[i].worldRotation;
        float sign=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0?-1:1;
        if(Vec3_Distance(rest.joints[i].worldPosition,pose->joints[i].worldPosition)>1e-6f*s->scale||
            fabsf(a.x-sign*b.x)>1e-7f||fabsf(a.y-sign*b.y)>1e-7f||fabsf(a.z-sign*b.z)>1e-7f||fabsf(a.w-sign*b.w)>1e-7f){exactRest=false;break;}}
    PosedColliders attached={0};
    if(s->attachedDistance){
        attached.distance=s->attachedDistance;attached.context=s->attachedContext;attached.epsilon=.001f*s->scale;
        attached.inverse=Quat_Identity();
        if(s->collisionJoint){int j=Skeleton_FindJoint(skeleton,s->collisionJoint);if(j<0)return false;
        attached.restOrigin=rest.joints[j].worldPosition;attached.posedOrigin=pose->joints[j].worldPosition;
        attached.inverse=Quat_Inverse(Quat_Multiply(pose->joints[j].worldRotation,Quat_Inverse(rest.joints[j].worldRotation)));}}
    for(size_t i=0;i<s->count;++i){LongHairGuide* g=&s->guides[i];int a=Skeleton_FindJoint(skeleton,g->root.fromId),b=Skeleton_FindJoint(skeleton,g->root.toId);
        if(g->attachmentJoint)a=b=Skeleton_FindJoint(skeleton,g->attachmentJoint);
        if(a<0||b<0){g->visible=false;valid=false;continue;}
        g->visible=g->rootValid;g->visibility=1;
        Quaternion qa=Quat_Multiply(pose->joints[a].worldRotation,Quat_Inverse(rest.joints[a].worldRotation));
        Quaternion qb=Quat_Multiply(pose->joints[b].worldRotation,Quat_Inverse(rest.joints[b].worldRotation));
        g->posedNormal=Unit(Vec3_Lerp(Quat_RotateVector(qa,g->root.restNormal),Quat_RotateVector(qb,g->root.restNormal),g->root.segmentParameter),g->root.restNormal);
        Vector3 side=Vec3_Lerp(Quat_RotateVector(qa,g->restSide),Quat_RotateVector(qb,g->restSide),g->root.segmentParameter);
        g->posedSide=Unit(Vec3_Sub(side,Vec3_Scale(g->posedNormal,Vec3_Dot(side,g->posedNormal))),g->restSide);
        const LongHairGroup* groom=&s->groups[g->groupIndex];Vector3 gravity=Vec3_Create(0,-1,0);
        Vector3 rotatedGravity=Vec3_Lerp(Quat_RotateVector(qa,gravity),Quat_RotateVector(qb,gravity),g->root.segmentParameter);
        Vector3 sag=Vec3_Scale(Vec3_Sub(gravity,rotatedGravity),g->length*groom->gravityInfluence*(1-groom->stiffness)*.25f);
        for(unsigned p=0;p<LONG_HAIR_POINTS;++p){Vector3 pa=Vec3_Add(pose->joints[a].worldPosition,Quat_RotateVector(qa,Vec3_Sub(g->restPoints[p],rest.joints[a].worldPosition)));
            Vector3 pb=Vec3_Add(pose->joints[b].worldPosition,Quat_RotateVector(qb,Vec3_Sub(g->restPoints[p],rest.joints[b].worldPosition)));
            float t=p/(float)(LONG_HAIR_POINTS-1);
            g->points[p]=Vec3_Add(Vec3_Lerp(pa,pb,g->root.segmentParameter),Vec3_Scale(sag,t*t));}
        if(attached.distance&&g->visible){float d=AttachedDistance(&attached,g->points[0]);
            g->visibility=Clamp(d/(.002f*s->scale),0,1,0);
            /* Una inserción cubierta por la mandíbula no debe emitir fibras a
             * través de ella. La identidad y el anclaje se conservan al destaparse. */
            if(d<=.0001f*s->scale){g->visible=false;g->contactPenetration=0;}}
    }
    if(exactRest){for(size_t i=0;i<s->count;++i){LongHairGuide* g=&s->guides[i];memcpy(g->points,g->restPoints,sizeof(g->points));
        memcpy(g->clearance,g->restClearance,sizeof(g->clearance));g->contactPenetration=g->restPenetration;g->rootClearance=g->restRootClearance;}return valid;}
    LongHairCollider volumes[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS+1];
    for(size_t i=0;i<s->colliderCount;++i){volumes[i]=s->colliders[i];int j=Skeleton_FindJoint(skeleton,volumes[i].id);
        if(j>=0){Quaternion q=Quat_Multiply(pose->joints[j].worldRotation,Quat_Inverse(rest.joints[j].worldRotation));
            volumes[i].center=Vec3_Add(pose->joints[j].worldPosition,Quat_RotateVector(q,Vec3_Sub(volumes[i].center,rest.joints[j].worldPosition)));
            volumes[i].inverseRotation=Quat_Multiply(volumes[i].inverseRotation,Quat_Inverse(q));}
        j=Skeleton_FindJoint(skeleton,volumes[i].endId);
        if(j>=0){Quaternion q=Quat_Multiply(pose->joints[j].worldRotation,Quat_Inverse(rest.joints[j].worldRotation));
            volumes[i].end=Vec3_Add(pose->joints[j].worldPosition,Quat_RotateVector(q,Vec3_Sub(volumes[i].end,rest.joints[j].worldPosition)));}}
    for(size_t i=0;i<s->count;++i){LongHairGuide* guide=&s->guides[i];if(!guide->visible)continue;
        Vector3 lo=guide->points[0],hi=lo;float margin=guide->length*.25f+s->groups[guide->groupIndex].rootSpread*s->scale;
        for(unsigned p=1;p<LONG_HAIR_POINTS;++p){Vector3 v=guide->points[p];
            lo=Vec3_Create(fminf(lo.x,v.x),fminf(lo.y,v.y),fminf(lo.z,v.z));hi=Vec3_Create(fmaxf(hi.x,v.x),fmaxf(hi.y,v.y),fmaxf(hi.z,v.z));}
        LongHairCollider local[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS+1];size_t count=0;
        for(size_t k=0;k<s->colliderCount;++k){LongHairCollider v=volumes[k];
            float r=fmaxf(fmaxf(v.radii.x,fmaxf(v.radii.y,v.radii.z)),fmaxf(v.endRadii.x,fmaxf(v.endRadii.y,v.endRadii.z)))+margin;
            if(fminf(v.center.x,v.end.x)-r>hi.x||fmaxf(v.center.x,v.end.x)+r<lo.x||
               fminf(v.center.y,v.end.y)-r>hi.y||fmaxf(v.center.y,v.end.y)+r<lo.y||
               fminf(v.center.z,v.end.z)-r>hi.z||fmaxf(v.center.z,v.end.z)+r<lo.z)continue;
            /* Ajuste estático a la raíz exacta de reposo: un proxy que la contiene
             * sobreestima la piel. El sesgo NO depende de la pose actual, así
             * que una pata o mandíbula que se acerque sigue produciendo contacto. */
            PosedColliders calibration={.volumes=&s->colliders[k],.count=1,.limit=1e6f};
            float d=ColliderDistance(&calibration,guide->root.restPosition);v.surfaceOffset=fmaxf(0,.002f*s->scale-d);
            /* La raíz transportada es el dato de inserción, no el proxy. Si una
             * envolvente la contiene al doblarse el cuello, ajustar su superficie
             * evita un contacto imposible en el punto inmóvil. Esto no desactiva
             * el volumen: sigue resolviendo contacto a lo largo del resto del pelo. */
            calibration.volumes=&v;d=ColliderDistance(&calibration,guide->points[0]);v.surfaceOffset+=fmaxf(0,.002f*s->scale-d);
            local[count++]=v;}
        const LongHairGroup* group=&s->groups[guide->groupIndex];
        PosedColliders context=attached;context.volumes=local;context.count=count;context.limit=(group->rootSpread+group->width*1.5f*(1+group->rootFullness)+.004f)*s->scale;
        LongHairSet single=*s;single.guides=guide;single.count=1;
        Collide(&single,ColliderDistance,ColliderNormal,&context);
    }
    return valid;
}
Vector3 LongHairGuide_Evaluate(const LongHairGuide* g,float t){
    if(!g)return Vec3_Create(0,0,0);
    float x=Clamp(t,0,1,0)*(LONG_HAIR_POINTS-1);int i=(int)x;if(i>=LONG_HAIR_POINTS-1)i=LONG_HAIR_POINTS-2;float u=x-i;
    Vector3 a=g->points[i?i-1:0],b=g->points[i],c=g->points[i+1],d=g->points[i+2<LONG_HAIR_POINTS?i+2:LONG_HAIR_POINTS-1];
    float u2=u*u,u3=u2*u,wa=-.5f*u+u2-.5f*u3,wb=1-2.5f*u2+1.5f*u3,wc=.5f*u+2*u2-1.5f*u3,wd=-.5f*u2+.5f*u3;
    return (Vector3){a.x*wa+b.x*wb+c.x*wc+d.x*wd,a.y*wa+b.y*wb+c.y*wc+d.y*wd,a.z*wa+b.z*wb+c.z*wc+d.z*wd};
}
size_t LongHairSet_Pack(const LongHairSet* s,float (*rows)[4],size_t capacity){
    if(!s||!rows||capacity<s->count*LONG_HAIR_GPU_ROWS)return 0;
    for(size_t i=0;i<s->count;++i){const LongHairGuide* g=&s->guides[i];const LongHairGroup* p=&s->groups[g->groupIndex];float (*r)[4]=rows+i*LONG_HAIR_GPU_ROWS;memset(r,0,LONG_HAIR_GPU_ROWS*sizeof(*r));
        for(unsigned j=0;j<LONG_HAIR_POINTS;++j){r[j][0]=g->points[j].x;r[j][1]=g->points[j].y;r[j][2]=g->points[j].z;r[j][3]=g->clearance[j];}
        Color colors[]={p->rootColor,p->tipColor};for(unsigned c=0;c<2;++c){r[10+c][0]=colors[c].r/255.f;r[10+c][1]=colors[c].g/255.f;r[10+c][2]=colors[c].b/255.f;r[10+c][3]=colors[c].a/255.f*(1-p->transparency);}
        Vector3 side=g->posedSide;
        r[12][0]=side.x;r[12][1]=side.y;r[12][2]=side.z;r[12][3]=fminf(p->rootSpread,p->length*(1-p->lengthVariation)*.12f)*s->scale;
        r[13][0]=g->posedNormal.x;r[13][1]=g->posedNormal.y;r[13][2]=g->posedNormal.z;
        r[13][3]=p->width*s->scale;r[14][0]=p->lengthVariation;r[14][1]=p->clumping;r[14][2]=(float)p->childrenPerGuide;r[14][3]=g->visible?p->density*g->visibility:0;
        r[15][0]=(float)(Hash(g->root.stableRootId^p->seed)&65535u);
        r[15][2]=p->rootFullness;
        r[15][3]=g->rootClearance;
        memcpy(r+16,g->follicles,sizeof(g->follicles));
        for(unsigned j=0;j<LONG_HAIR_POINTS;++j)r[15][1]=fmaxf(r[15][1],Vec3_Length(g->corrections[j])/s->scale);
    }return s->count*LONG_HAIR_GPU_ROWS;
}
