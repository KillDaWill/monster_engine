/** @file LongHair.c
 * @brief Generación estable sobre estaciones anatómicas y transporte de guías. */
#include "LongHair.h"
#include "Fur.h"
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
}
void LongHairSet_Free(LongHairSet* s){if(s){free(s->guides);memset(s,0,sizeof(*s));}}
static void Bytes(uint64_t* h,const void* p,size_t size){const unsigned char* b=p;for(size_t i=0;i<size;++i){*h^=b[i];*h*=1099511628211ULL;}}
uint64_t LongHairGroups_Fingerprint(const LongHairGroup* groups,size_t count){
    uint64_t h=14695981039346656037ULL;Bytes(&h,&count,sizeof(count));if(count>LONG_HAIR_MAX_GROUPS||(count&&!groups))return 0;
    for(size_t i=0;i<count;++i){LongHairGroup g=groups[i];LongHairGroup_Normalize(&g);
#define FIELD(x) Bytes(&h,&g.x,sizeof(g.x))
        FIELD(id);FIELD(seed);FIELD(domain.kind);FIELD(domain.hostModuleId);FIELD(domain.region);FIELD(domain.anchorCount);
        for(size_t j=0;j<g.domain.anchorCount;++j)FIELD(domain.anchors[j]);
        FIELD(domain.offset);FIELD(domain.normal);FIELD(domain.angle);FIELD(domain.angularSpread);FIELD(domain.start);FIELD(domain.end);FIELD(domain.patchRadius);
        FIELD(guideCount);FIELD(childrenPerGuide);FIELD(direction);FIELD(length);FIELD(lengthVariation);FIELD(width);FIELD(rootSpread);
        FIELD(clumping);FIELD(stiffness);FIELD(gravityInfluence);FIELD(density);FIELD(rootColor);FIELD(tipColor);
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
bool LongHairSet_Build(LongHairSet* dst,const LongHairGroup* groups,size_t count,const AnatomyGraph* graph,float scale,LongHairDistance distance,const void* context){
    if(!dst||!graph||count>LONG_HAIR_MAX_GROUPS||(count&&!groups)||!isfinite(scale)||scale<=0)return false;
    LongHairSet next={.groupCount=count,.scale=scale,.generation=dst->generation+1};size_t capacity=0;
    for(size_t k=0;k<count;++k){next.groups[k]=groups[k];LongHairGroup_Normalize(&next.groups[k]);capacity+=next.groups[k].guideCount;
        if(!groups[k].id)return false;
        for(size_t j=0;j<k;++j)if(groups[k].id==groups[j].id)return false;}
    next.guides=calloc(capacity?capacity:1,sizeof(*next.guides));if(!next.guides)return false;
    for(size_t k=0;k<count;++k){const LongHairGroup* g=&next.groups[k];const LongHairDomain* d=&g->domain;
        const AnatomyNode* nodes[LONG_HAIR_MAX_ANCHORS];size_t n=Nodes(d,graph,nodes);
        if(!n||!g->childrenPerGuide||g->density<=0||g->length<=0)continue;
        for(unsigned i=0;i<g->guideCount;++i){LongHairGuide v={.groupIndex=(unsigned)k,.visible=true};
            uint32_t id=Hash(g->id^Hash(i+1)^g->seed);float u=Fur_Random(id),a=d->start+(d->end-d->start)*u;
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
            Vector3 direction=Unit(Vec3_Add(g->direction,Vec3_Scale(normal,.25f)),normal);
            v.root=(LongHairRoot){.groupId=g->id,.moduleId=d->hostModuleId,.stableRootId=Hash(g->id^Hash(i+1)),
                .fromId=na->id,.toId=nb->id,.restPosition=root,.restNormal=normal,.restDirection=direction,.rootParameter=a,.segmentParameter=t};
            v.length=g->length*scale*(1+g->lengthVariation*(2*Fur_Random(id^981u)-1));
            v.restPoints[0]=root;
            for(unsigned p=1;p<LONG_HAIR_POINTS;++p){float h=(float)p/(LONG_HAIR_POINTS-1);
                Vector3 flow=Vec3_Add(direction,Vec3_Create(0,-g->gravityInfluence*h*(1-g->stiffness)*2,0));
                flow=Vec3_Add(flow,Vec3_Scale(side,.12f*sinf(h*3+Fur_Random(id^717u)*6)));
                v.restPoints[p]=Vec3_Add(v.restPoints[p-1],Vec3_Scale(Unit(flow,direction),v.length/(LONG_HAIR_POINTS-1)));}
            memcpy(v.points,v.restPoints,sizeof(v.points));v.rootValid=v.visible;next.guides[next.count++]=v;
        }
    }
    LongHairSet_Free(dst);*dst=next;return true;
}
bool LongHairSet_Pose(LongHairSet* s,const Skeleton* skeleton,const SkeletonPose* pose){
    if(!s||!skeleton||!pose||pose->jointCount!=skeleton->jointCount)return false;
    SkeletonPose rest;if(!SkeletonPose_Init(&rest,skeleton))return false;bool valid=true;
    for(size_t i=0;i<s->count;++i){LongHairGuide* g=&s->guides[i];int a=Skeleton_FindJoint(skeleton,g->root.fromId),b=Skeleton_FindJoint(skeleton,g->root.toId);
        if(a<0||b<0){g->visible=false;valid=false;continue;}
        g->visible=g->rootValid;
        Quaternion qa=Quat_Multiply(pose->joints[a].worldRotation,Quat_Inverse(rest.joints[a].worldRotation));
        Quaternion qb=Quat_Multiply(pose->joints[b].worldRotation,Quat_Inverse(rest.joints[b].worldRotation));
        g->posedNormal=Unit(Vec3_Lerp(Quat_RotateVector(qa,g->root.restNormal),Quat_RotateVector(qb,g->root.restNormal),g->root.segmentParameter),g->root.restNormal);
        for(unsigned p=0;p<LONG_HAIR_POINTS;++p){Vector3 pa=Vec3_Add(pose->joints[a].worldPosition,Quat_RotateVector(qa,Vec3_Sub(g->restPoints[p],rest.joints[a].worldPosition)));
            Vector3 pb=Vec3_Add(pose->joints[b].worldPosition,Quat_RotateVector(qb,Vec3_Sub(g->restPoints[p],rest.joints[b].worldPosition)));
            g->points[p]=Vec3_Lerp(pa,pb,g->root.segmentParameter);}
    }return valid;
}
Vector3 LongHairGuide_Evaluate(const LongHairGuide* g,float t){
    if(!g)return Vec3_Create(0,0,0);
    float x=Clamp(t,0,1,0)*(LONG_HAIR_POINTS-1);int i=(int)x;if(i>=LONG_HAIR_POINTS-1)i=LONG_HAIR_POINTS-2;float u=x-i;
    Vector3 a=g->points[i?i-1:0],b=g->points[i],c=g->points[i+1],d=g->points[i+2<LONG_HAIR_POINTS?i+2:LONG_HAIR_POINTS-1];
    return Vec3_Scale(Vec3_Add(Vec3_Add(Vec3_Scale(b,2),Vec3_Scale(Vec3_Sub(c,a),u)),
        Vec3_Add(Vec3_Scale(Vec3_Add(Vec3_Sub(Vec3_Scale(a,2),Vec3_Scale(b,5)),Vec3_Sub(Vec3_Scale(c,4),d)),u*u),
        Vec3_Scale(Vec3_Add(Vec3_Sub(Vec3_Scale(b,3),a),Vec3_Sub(d,Vec3_Scale(c,3))),u*u*u))),.5f);
}
size_t LongHairSet_Pack(const LongHairSet* s,float (*rows)[4],size_t capacity){
    if(!s||!rows||capacity<s->count*LONG_HAIR_GPU_ROWS)return 0;
    for(size_t i=0;i<s->count;++i){const LongHairGuide* g=&s->guides[i];const LongHairGroup* p=&s->groups[g->groupIndex];float (*r)[4]=rows+i*LONG_HAIR_GPU_ROWS;memset(r,0,LONG_HAIR_GPU_ROWS*sizeof(*r));
        for(unsigned j=0;j<LONG_HAIR_POINTS;++j){r[j][0]=g->points[j].x;r[j][1]=g->points[j].y;r[j][2]=g->points[j].z;}
        Color colors[]={p->rootColor,p->tipColor};for(unsigned c=0;c<2;++c){r[10+c][0]=colors[c].r/255.f;r[10+c][1]=colors[c].g/255.f;r[10+c][2]=colors[c].b/255.f;r[10+c][3]=colors[c].a/255.f;}
        Vector3 tangent=Unit(Vec3_Sub(g->points[1],g->points[0]),Vec3_Create(0,0,1));
        Vector3 fallback=Unit(Vec3_Cross(g->posedNormal,fabsf(g->posedNormal.y)>.9f?Vec3_Create(1,0,0):Vec3_Create(0,1,0)),Vec3_Create(0,0,1));
        Vector3 side=Unit(Vec3_Cross(g->posedNormal,tangent),fallback);
        r[12][0]=side.x;r[12][1]=side.y;r[12][2]=side.z;r[12][3]=p->rootSpread*s->scale;
        r[13][0]=g->posedNormal.x;r[13][1]=g->posedNormal.y;r[13][2]=g->posedNormal.z;
        r[13][3]=p->width*s->scale;r[14][0]=p->lengthVariation;r[14][1]=p->clumping;r[14][2]=(float)p->childrenPerGuide;r[14][3]=g->visible?p->density:0;
        r[15][0]=(float)(Hash(g->root.stableRootId^p->seed)&65535u);
    }return s->count*LONG_HAIR_GPU_ROWS;
}
