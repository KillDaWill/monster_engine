#include "HeadMorph.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CAGE_COUNT 24
/* Los índices describen funciones anatómicas, no IDs específicos de especie. */
typedef struct HeadFrame {Vector3 center,radius;} HeadFrame;
typedef struct HeadBinding {
    size_t vertex;
    unsigned char frame[4];
    float weight[4],blend;
    Vector3 position,normal;
} HeadBinding;
struct HeadMorph {HeadFrame reference[CAGE_COUNT];HeadBinding* bindings;size_t count,capacity,vertices;};
static void Frames(const HeadAnatomy* head,Vector3 origin,HeadFrame frames[CAGE_COUNT]) {
    const HeadSurfaceRecipe* r=&head->surface;
    const HeadLandmarks* l=&head->landmarks;
    const Mouth* m=&head->oralSystem;

    float chinRadiusY=fmaxf(m->jawThickness*0.5f,0.02f);
    float chinRadiusX=fmaxf(r->faceTipRadii.x*0.85f,0.02f);
    float chinRadiusZ=fmaxf(r->faceTipRadii.z*0.75f,0.02f);
    Vector3 chinRadii=Vec3_Create(chinRadiusX,chinRadiusY,chinRadiusZ);

    float hingeR=fmaxf(m->hingeRadius,0.02f);
    Vector3 hingeRadii=Vec3_Create(hingeR,hingeR,hingeR);

    Vector3 jawMidLeft=Vec3_Lerp(l->leftJawHinge,l->mandibularSymphysis,0.5f);
    Vector3 jawMidRight=Vec3_Lerp(l->rightJawHinge,l->mandibularSymphysis,0.5f);
    Vector3 jawMidRadii=Vec3_Create(r->cheekRadii.x*0.8f,chinRadiusY*1.1f,fmaxf(m->jawLength*0.3f,0.02f));

    Vector3 throatCenter=Vec3_Create(0,l->neckAttachment.y-m->throatRadius*0.35f,(l->neckAttachment.z+l->mandibularSymphysis.z)*0.35f);
    Vector3 throatRadii=Vec3_Create(r->craniumRadii.x*0.65f,fmaxf(m->throatRadius*1.2f,0.03f),fmaxf(m->jawLength*0.35f,0.02f));

    HeadFrame values[CAGE_COUNT]={
        {r->craniumCenter,r->craniumRadii},{r->faceRoot,r->faceRootRadii},
        {r->faceMid,r->faceMidRadii},{r->faceTip,r->faceTipRadii},
        {r->leftTemporalCenter,r->temporalRadii},{r->rightTemporalCenter,r->temporalRadii},
        {r->leftMaxillaryCenter,r->maxillaryRadii},{r->rightMaxillaryCenter,r->maxillaryRadii},
        {r->leftCheekCenter,r->cheekRadii},{r->rightCheekCenter,r->cheekRadii},
        {r->leftBrowCenter,r->browRadii},{r->rightBrowCenter,r->browRadii},
        {r->leftOrbitCenter,r->orbitRadii},{r->rightOrbitCenter,r->orbitRadii},
        {r->leftNostrilCenter,r->nostrilRadii},{r->rightNostrilCenter,r->nostrilRadii},
        {r->leftTympanumCenter,r->tympanumRadii},{r->rightTympanumCenter,r->tympanumRadii},
        {l->mandibularSymphysis,chinRadii},
        {l->leftJawHinge,hingeRadii},{l->rightJawHinge,hingeRadii},
        {jawMidLeft,jawMidRadii},{jawMidRight,jawMidRadii},
        {throatCenter,throatRadii}};
    values[12].radius.x=values[13].radius.x=fmaxf(r->orbitSocketDepth,1e-5f);
    values[16].radius.x=values[17].radius.x=fmaxf(r->tympanumDepth,1e-5f);
    for(unsigned i=0;i<CAGE_COUNT;++i){frames[i]=values[i];frames[i].center=Vec3_Add(values[i].center,origin);}
}
HeadMorph* HeadMorph_Create(void){return calloc(1,sizeof(HeadMorph));}
void HeadMorph_Free(HeadMorph* morph){if(morph){free(morph->bindings);free(morph);}}
bool HeadMorph_Copy(HeadMorph* dst, const HeadMorph* src) {
    if (!dst || !src) return false;
    memcpy(dst->reference, src->reference, sizeof(dst->reference));
    dst->count = src->count;
    dst->vertices = src->vertices;
    if (dst->capacity < src->count) {
        free(dst->bindings);
        dst->bindings = (HeadBinding*)malloc(src->count * sizeof(HeadBinding));
        dst->capacity = src->count;
    }
    if (src->count > 0 && dst->bindings) {
        memcpy(dst->bindings, src->bindings, src->count * sizeof(HeadBinding));
    }
    return true;
}
static float Blend(const MeshVertex* v,const HeadAnatomy* head,Vector3 origin,Vector3 lo,Vector3 hi,float margin) {
    if(v->position.x<lo.x || v->position.x>hi.x || v->position.y<lo.y || v->position.y>hi.y)return 0.0f;
    float start=origin.z+head->landmarks.neckAttachment.z;
    float end=origin.z+head->surface.craniumCenter.z;
    float tz=fmaxf(0.0f,fminf(1.0f,(v->position.z-start)/fmaxf(end-start,1e-5f)));
    float blendZ=tz*tz*(3.0f-2.0f*tz);

    float m=fmaxf(margin,1e-4f);
    float edgeX=fminf((v->position.x-lo.x)/m,(hi.x-v->position.x)/m);
    float tx=fmaxf(0.0f,fminf(1.0f,edgeX));
    float blendX=tx*tx*(3.0f-2.0f*tx);

    float edgeY=fminf((v->position.y-lo.y)/m,(hi.y-v->position.y)/m);
    float ty=fmaxf(0.0f,fminf(1.0f,edgeY));
    float blendY=ty*ty*(3.0f-2.0f*ty);

    return blendZ*blendX*blendY;
}
bool HeadMorph_Bind(HeadMorph* m,const Mesh* mesh,const HeadAnatomy* head,Vector3 origin) {
    if(!m || !mesh || !head)return false;
    m->count=0;m->vertices=mesh->vertexCount;Frames(head,origin,m->reference);
    /* El dominio cefálico no puede arrastrar manos que pasan por delante
     * del cuello. Sus límites se derivan de todos los volúmenes de la jaula. */
    Vector3 lo={INFINITY,INFINITY,INFINITY},hi={-INFINITY,-INFINITY,-INFINITY};
    for(unsigned i=0;i<CAGE_COUNT;++i) {
        HeadFrame f=m->reference[i];
        lo.x=fminf(lo.x,f.center.x-f.radius.x);lo.y=fminf(lo.y,f.center.y-f.radius.y);
        hi.x=fmaxf(hi.x,f.center.x+f.radius.x);hi.y=fmaxf(hi.y,f.center.y+f.radius.y);
    }
    float margin=head->surface.craniumRadii.y*.35f;
    lo.x-=margin;lo.y-=margin;hi.x+=margin;hi.y+=margin;
    size_t count=0;for(size_t v=0;v<mesh->vertexCount;++v)count+=Blend(&mesh->vertices[v],head,origin,lo,hi,margin)>0;
    if(count>m->capacity){void* p=realloc(m->bindings,count*sizeof(*m->bindings));if(!p)return false;m->bindings=p;m->capacity=count;}
    for(size_t v=0;v<mesh->vertexCount;++v) {
        const MeshVertex* vertex=&mesh->vertices[v];float blend=Blend(vertex,head,origin,lo,hi,margin);if(blend<=0)continue;
        HeadBinding* b=&m->bindings[m->count++];
        *b=(HeadBinding){.vertex=v,.blend=blend,.position=vertex->position,.normal=vertex->normal};
        float score[4]={INFINITY,INFINITY,INFINITY,INFINITY};
        for(unsigned i=0;i<CAGE_COUNT;++i) {
            HeadFrame f=m->reference[i];Vector3 d=Vec3_Sub(vertex->position,f.center);
            float x=d.x/fmaxf(f.radius.x,1e-5f),y=d.y/fmaxf(f.radius.y,1e-5f),z=d.z/fmaxf(f.radius.z,1e-5f);
            float distance=x*x+y*y+z*z;
            for(unsigned k=0;k<4;++k)if(distance<score[k]) {
                for(unsigned j=3;j>k;--j){score[j]=score[j-1];b->frame[j]=b->frame[j-1];}
                score[k]=distance;b->frame[k]=i;break;
            }
        }
        float sum=0;for(unsigned k=0;k<4;++k){b->weight[k]=1/(.001f+score[k]*score[k]);sum+=b->weight[k];}
        for(unsigned k=0;k<4;++k)b->weight[k]/=sum;
    }
    return true;
}
static bool Apply(const HeadMorph* m,Mesh* mesh,const HeadAnatomy* head,Vector3 origin,bool material,Vector3 mapOrigin,float unitScale) {
    if(!m || !mesh || !head || mesh->vertexCount!=m->vertices)return false;
    HeadFrame current[CAGE_COUNT];Frames(head,origin,current);
    Vector3 scales[CAGE_COUNT];
    for(unsigned i=0;i<CAGE_COUNT;++i)scales[i]=Vec3_Create(
        current[i].radius.x/fmaxf(m->reference[i].radius.x,1e-5f),
        current[i].radius.y/fmaxf(m->reference[i].radius.y,1e-5f),
        current[i].radius.z/fmaxf(m->reference[i].radius.z,1e-5f));
    for(size_t v=0;v<m->count;++v) {
        const HeadBinding* b=&m->bindings[v];Vector3 position=Vec3_Zero(),normal=Vec3_Zero();
        for(unsigned k=0;k<4;++k) {
            unsigned i=b->frame[k];Vector3 scale=scales[i],d=Vec3_Sub(b->position,m->reference[i].center);
            Vector3 p=Vec3_Add(current[i].center,Vec3_Create(d.x*scale.x,d.y*scale.y,d.z*scale.z));
            Vector3 n=Vec3_Create(b->normal.x/fmaxf(scale.x,1e-5f),b->normal.y/fmaxf(scale.y,1e-5f),b->normal.z/fmaxf(scale.z,1e-5f));
            position=Vec3_Add(position,Vec3_Scale(p,b->weight[k]));normal=Vec3_Add(normal,Vec3_Scale(n,b->weight[k]));
        }
        MeshVertex* vertex=&mesh->vertices[b->vertex];
        if(material) {
            vertex->surface.position=Vec3_Lerp(vertex->surface.position,Vec3_Scale(Vec3_Sub(position,mapOrigin),1.0f/fmaxf(unitScale,1e-6f)),b->blend);
            vertex->surface.normal=Vec3_Normalize(Vec3_Lerp(vertex->surface.normal,Vec3_Normalize(normal),b->blend));
            Vector3 sn=vertex->surface.normal;
            Vector3 tangent=Vec3_Sub(vertex->surface.flowDirection,Vec3_Scale(sn,Vec3_Dot(sn,vertex->surface.flowDirection)));
            if(Vec3_LengthSq(tangent)<1e-10f)tangent=Vec3_Cross(sn,fabsf(sn.y)<.9f?Vec3_Create(0,1,0):Vec3_Create(1,0,0));
            vertex->surface.flowDirection=Vec3_Normalize(tangent);
            vertex->surface.ventral=fmaxf(0,fminf(1,.5f-.7f*vertex->surface.normal.y));
        } else {
            vertex->position=Vec3_Lerp(vertex->position,position,b->blend);
            vertex->normal=Vec3_Normalize(Vec3_Lerp(vertex->normal,Vec3_Normalize(normal),b->blend));
        }
    }
    if(m->count){if(material)Mesh_MarkSurfaceChanged(mesh);else Mesh_MarkGeometryChanged(mesh);}
    return true;
}

bool HeadMorph_Deform(const HeadMorph* m,Mesh* mesh,const HeadAnatomy* head,Vector3 origin) {
    return Apply(m,mesh,head,origin,false,Vec3_Zero(),1);
}
bool HeadMorph_MapSurface(const HeadMorph* m,Mesh* mesh,const HeadAnatomy* head,Vector3 origin) {
    return Apply(m,mesh,head,origin,true,Vec3_Zero(),1);
}

bool HeadMorph_MapSurfaceDomain(const HeadMorph* m,Mesh* mesh,const HeadAnatomy* head,Vector3 origin,Vector3 mapOrigin,float unitScale) {
    return Apply(m,mesh,head,origin,true,mapOrigin,unitScale);
}
