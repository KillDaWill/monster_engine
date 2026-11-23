#include "EyeTexture.h"
#include "MathUtils.h"
#include <math.h>

static uint64_t HashBytes(uint64_t h,const void* p,size_t n){const unsigned char* b=p;for(size_t i=0;i<n;++i){h^=b[i];h*=0x100000001b3ULL;}return h;}
uint64_t EyeTexture_Fingerprint(const Eye* e){
    if(!e)return 0;
    uint64_t h=0xcbf29ce484222325ULL;
#define H(x) h=HashBytes(h,&e->x,sizeof(e->x))
    H(scleraColor);H(irisColor);H(pupilColor);H(irisScale);H(pupilScale);H(pupilAspect);H(pupilShape);
    H(limbalRingStrength);H(irisRadialNoise);H(irisColorVariation);H(scleraPigmentation);H(pupilDilation);H(textureSeed);
#undef H
    return h?h:1;
}
static float Smooth(float a,float b,float x){float t=Math_Clamp01((x-a)/(b-a));return t*t*(3.0f-2.0f*t);}
static unsigned char Byte(float x){return (unsigned char)(Math_Clamp01(x)*255.0f+.5f);}
static uint32_t Mix(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
static float Noise(uint32_t seed,int x,int y){return (float)(Mix(seed^(uint32_t)(x*0x9e3779b9u)^(uint32_t)(y*0x85ebca6bu))&0xffffu)/32767.5f-1.0f;}
static float DistDiamond(float x,float y,float aspect){return fabsf(x)+fabsf(y)*aspect;}
static void EvalCompoundEyePixel(const Eye* e,float u,float v,float outRgb[3]){
    /* Ojo compuesto facetado hexagonal (omatidios de odonato/insecto):
     * gradiente dorsal-ventral (irisColor dorsal -> scleraColor ventral),
     * microfacetado hexagonal y pseudopupila difusa sin pupila ni iris vertebrado. */
    float dorsal[3]={e->irisColor.r/255.f,e->irisColor.g/255.f,e->irisColor.b/255.f};
    float ventral[3]={e->scleraColor.r/255.f,e->scleraColor.g/255.f,e->scleraColor.b/255.f};
    float pseudo[3]={e->pupilColor.r/255.f,e->pupilColor.g/255.f,e->pupilColor.b/255.f};
    /* Malla hexagonal determinista de omatidios sobre coordenadas (u,v). */
    const float freq=42.0f;
    const float sx=1.0f,sy=1.7320508f;
    float gx=u*freq,gy=v*freq;
    float r0x=floorf(gx/sx+.5f)*sx,r0y=floorf(gy/sy+.5f)*sy;
    float r1x=floorf((gx-sx*.5f)/sx+.5f)*sx+sx*.5f,r1y=floorf((gy-sy*.5f)/sy+.5f)*sy+sy*.5f;
    float d0=(gx-r0x)*(gx-r0x)+(gy-r0y)*(gy-r0y);
    float d1=(gx-r1x)*(gx-r1x)+(gy-r1y)*(gy-r1y);
    float cx=d0<d1?r0x:r1x,cy=d0<d1?r0y:r1y;
    float dx=fabsf(gx-cx),dy=fabsf(gy-cy);
    /* Distancia al borde hexagonal (0 en centro de faceta, ~0.5 en arista). */
    float hexD=fmaxf(dx,dx*.5f+dy*.8660254f);
    float groove=Smooth(.36f,.50f,hexD);
    int cidX=(int)floorf(cx*2.0f+.5f),cidY=(int)floorf(cy*2.0f+.5f);
    float facetVar=Noise(e->textureSeed,cidX,cidY)*.050f;
    /* Gradiente dorsal-ventral natural (Anax imperator: azul-esmeralda dorsal, verde-amarillo ventral). */
    float dv=Smooth(.14f,.86f,v);
    float px=(u-.5f)*2.0f,py=(v-.5f)*2.0f;
    float r2=px*px+py*py;
    /* Pseudopupila óptica sutil (sin borde nítido ni anillo limbal). */
    float pseudoW=expf(-r2*12.0f)*.24f;
    for(int c=0;c<3;++c){
        float col=ventral[c]*(1.0f-dv)+dorsal[c]*dv;
        col=col*(1.0f-pseudoW)+pseudo[c]*pseudoW;
        col*=(1.0f+facetVar)*(1.0f-.15f*groove);
        outRgb[c]=Math_Clamp01(col);
    }
}
bool EyeTexture_Generate(const Eye* e,unsigned n,unsigned char* out,size_t cap){
    if(!e||!out||n<8||n>1024||(size_t)n*n>cap/4)return false;
    if(e->pupilShape==PUPIL_COMPOUND){
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
            float u=((float)x+.5f)/(float)n,v=((float)y+.5f)/(float)n;
            float rgb[3];
            EvalCompoundEyePixel(e,u,v,rgb);
            size_t i=((size_t)y*n+x)*4;
            out[i+0]=Byte(rgb[0]);out[i+1]=Byte(rgb[1]);out[i+2]=Byte(rgb[2]);out[i+3]=255;
        }
        return true;
    }
    float iris=Math_Clamp(e->irisScale,.18f,.96f)*.47f;
    float ring=Math_Clamp01(e->limbalRingStrength),noise=Math_Clamp01(e->irisRadialNoise),variation=Math_Clamp01(e->irisColorVariation);
    float dilation=Math_Clamp01(e->pupilDilation),pupil=Math_Clamp(e->pupilScale,.05f,.9f)*(.72f+.56f*dilation);
    float aspect=Math_Clamp(e->pupilAspect,.12f,1.0f);PupilShape shape=e->pupilShape;
    float pupilR=iris*Math_Clamp(pupil,.12f,.86f);
    float pigmentation=Math_Clamp01(e->scleraPigmentation);
    float base[3]={e->scleraColor.r/255.f,e->scleraColor.g/255.f,e->scleraColor.b/255.f};
    float ir[3]={e->irisColor.r/255.f,e->irisColor.g/255.f,e->irisColor.b/255.f};
    float pu[3]={e->pupilColor.r/255.f,e->pupilColor.g/255.f,e->pupilColor.b/255.f};
    float maxIrisEdge=iris*(1.0f+.025f*noise)+.012f;
    for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
        float u=((float)x+.5f)/(float)n,v=((float)y+.5f)/(float)n;
        float px=(u-.5f)*2.0f,py=(v-.5f)*2.0f;
        float eyeR=sqrtf(px*px+py*py);
        float fiber=0,irisMask=0,ringMask=0,rays=0;
        /* Fuera de la máxima órbita del iris su máscara es exactamente cero.
         * La pupila se evalúa aparte (el diamante puede superar este radio). */
        if(eyeR<maxIrisEdge) {
            fiber=Noise(e->textureSeed,x,y)*noise;
            float irisEdge=iris*(1.0f+.025f*fiber);
            irisMask=1.0f-Smooth(irisEdge-.012f,irisEdge+.012f,eyeR);
            if(irisMask>0) {
                ringMask=Smooth(iris*.86f,iris*.94f,eyeR)*(1.0f-Smooth(iris*.985f,iris*1.015f,eyeR));
                float angle=atan2f(py,px);
                rays=.5f+.5f*sinf(angle*47.0f+fiber*2.7f+Noise(e->textureSeed,x/3,(int)(angle*11))*noise*2.0f);
            }
        }
        float pupilMetric;
        if(shape==PUPIL_VERTICAL)pupilMetric=sqrtf((px/aspect)*(px/aspect)+py*py);
        else if(shape==PUPIL_HORIZONTAL)pupilMetric=sqrtf(px*px+(py/aspect)*(py/aspect));
        else if(shape==PUPIL_DIAMOND)pupilMetric=DistDiamond(px,py,aspect);
        else pupilMetric=eyeR;
        float pupilMask=1.0f-Smooth(pupilR-.012f,pupilR+.012f,pupilMetric);
        float pigment=pigmentation*(0.15f+0.12f*fabsf(Noise(e->textureSeed,x/5,y/5)));
        size_t i=((size_t)y*n+x)*4;
        for(int c=0;c<3;++c){
            float scl=base[c]*(1.0f-pigment)+(c==0?.94f:.90f)*pigment;
            float col=ir[c]*(.83f+variation*.16f*rays+fiber*.08f*variation);
            col=col*(1.0f-ringMask*ring)+(.018f)*ringMask*ring;
            float value=scl*(1.0f-irisMask)+col*irisMask;
            value=value*(1.0f-pupilMask)+pu[c]*pupilMask;
            out[i+c]=Byte(value);
        }
        out[i+3]=255;
    }
    return true;
}
