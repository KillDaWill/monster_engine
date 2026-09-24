#include "Fur.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
static uint32_t Hash(uint32_t x) { x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16); }
float Fur_Random(uint32_t x) {return (Hash(x)>>8)*(1.0f/16777216.0f);}
void FurRootSet_Free(FurRootSet* s) {if(s){free(s->roots);memset(s,0,sizeof(*s));}}
bool FurRootSet_Build(FurRootSet* out,const Mesh* mesh,uint32_t seed,float perArea,size_t limit) {
    if(!out||!mesh||!isfinite(perArea)||perArea<0)return false;
    size_t triangles=mesh->indexCount/3;
    double* cdf=calloc(triangles+1,sizeof(*cdf));if(!cdf)return false;
    for(size_t t=0;t<triangles;++t) {
        uint32_t a=mesh->indices[3*t],b=mesh->indices[3*t+1],c=mesh->indices[3*t+2];
        if(a>=mesh->vertexCount||b>=mesh->vertexCount||c>=mesh->vertexCount){free(cdf);return false;}
        Vector3 pa=mesh->vertices[a].surface.position,pb=mesh->vertices[b].surface.position,pc=mesh->vertices[c].surface.position;
        float area=.5f*Vec3_Length(Vec3_Cross(Vec3_Sub(pb,pa),Vec3_Sub(pc,pa)));
        cdf[t+1]=cdf[t]+(isfinite(area)?area:0);
    }
    FurRootSet s={.area=(float)cdf[triangles]};
    size_t candidates=(size_t)fmin((double)limit,floor(cdf[triangles]*perArea+.5));
    if(candidates){s.roots=calloc(candidates,sizeof(*s.roots));if(!s.roots){free(cdf);return false;}}
    for(size_t i=0;i<candidates;++i) {
        uint32_t id=Hash(seed^(uint32_t)i); double target=Fur_Random(id)*cdf[triangles];
        size_t lo=0,hi=triangles;
        while(lo+1<hi){size_t mid=(lo+hi)/2;if(cdf[mid]<=target)lo=mid;else hi=mid;}
        float u=sqrtf(Fur_Random(id^0x1234u)),v=Fur_Random(id^0x4321u);
        Vector3 bary={1-u,u*(1-v),u*v};
        const MeshVertex *a=&mesh->vertices[mesh->indices[3*lo]],*b=&mesh->vertices[mesh->indices[3*lo+1]],*c=&mesh->vertices[mesh->indices[3*lo+2]];
        /* Rechazo conservador en fronteras tisulares: ninguna hebra cruza trufa, boca o uña. */
        if(a->material!=SDF_MATERIAL_SKIN||b->material!=SDF_MATERIAL_SKIN||c->material!=SDF_MATERIAL_SKIN)continue;
        float mask=fminf(a->surface.integumentMask,fminf(b->surface.integumentMask,c->surface.integumentMask));
        if(mask<.999f)continue;
        s.roots[s.count++]=(FurRoot){(uint32_t)lo,bary,id};
    }
    free(cdf);FurRootSet_Free(out);*out=s;return true;
}
FurCurve Fur_EvaluateCurve(const FurPhenotype* f,Vector3 n,Vector3 flow,float r,float h) {
    h=fmaxf(0,fminf(1,h));
    float bend=f->lay*(1-.8f*f->stiffness),length=f->length*(1+f->irregularity*(r-.5f)*.6f);
    float side=f->clumping*(r-.5f)*.35f;
    float wave=f->irregularity*(r-.5f)*.12f;
    Vector3 lateral=Vec3_Cross(n,flow);
    FurCurve c;
    c.offset=Vec3_Scale(Vec3_Add(Vec3_Scale(n,h),Vec3_Add(Vec3_Scale(flow,bend*h*h),Vec3_Scale(lateral,side*h*h+wave*h*h*(1-h)))),length);
    c.derivative=Vec3_Scale(Vec3_Add(n,Vec3_Add(Vec3_Scale(flow,2*bend*h),Vec3_Scale(lateral,2*side*h+wave*(2*h-3*h*h)))),length);
    c.radius=.0015f*f->thickness*(1-h)*(1-h);
    return c;
}
float Fur_OpticalAlpha(float d,float o,float dh,float cosine) {
    return -expm1f(-fmaxf(0,d)*fmaxf(0,o)*fmaxf(0,dh)/fmaxf(.25f,fabsf(cosine)));
}
float Fur_ShellResolution(float p,float g,float q) {
    if(!isfinite(p)||!isfinite(g)||!isfinite(q))return 0;
    return fminf(32,fmaxf(0,(p-.5f)*q*(1+.5f*fmaxf(0,fminf(1,g)))));
}

int Fur_ShellSamples(float resolution,float samples[32][2]) {
    if(!samples||!isfinite(resolution)||resolution<=0)return 0;
    resolution=fminf(32,resolution);
    int low=4;while(low<32&&resolution>low*2)low*=2;
    int high=resolution>low?low*2:low;if(high>32)high=32;
    float t=fmaxf(0,fminf(1,(resolution-low)/(float)low));
    for(int i=1;i<=high;++i) {
        float weight=high==low?1.0f/high:(i%2==0?(1-t)/(float)low+t/high:t/high);
        samples[i-1][0]=(float)i/high;samples[i-1][1]=weight*fminf(1,resolution/4);
    }
    return high;
}
