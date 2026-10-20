/** @file SDFInterval.c
 * @brief Recetas de intervalos compartibles con GLSL, sin dependencias del renderizador.
 */
#include "SDFInterval.h"
#include "SDFPrimitives.h"
#include <math.h>

static SDFInterval Interval_Pad(float lo,float hi,int ulps) {
    if(isnan(lo)||isnan(hi)||lo>hi)return (SDFInterval){-INFINITY,INFINITY};
    for(int i=0;i<ulps;++i) {lo=nextafterf(lo,-INFINITY);hi=nextafterf(hi,INFINITY);}
    return (SDFInterval){lo,hi};
}
static SDFInterval Interval_Constant(float value) {return (SDFInterval){value,value};}
static SDFInterval Interval_Add(SDFInterval a,SDFInterval b) {
    return Interval_Pad((float)((double)a.lo+b.lo),(float)((double)a.hi+b.hi),2);
}
static SDFInterval Interval_Subtract(SDFInterval a,SDFInterval b) {
    return Interval_Pad((float)((double)a.lo-b.hi),(float)((double)a.hi-b.lo),2);
}
static SDFInterval Interval_Multiply(SDFInterval a,SDFInterval b) {
    double p0=(double)a.lo*b.lo,p1=(double)a.lo*b.hi,p2=(double)a.hi*b.lo,p3=(double)a.hi*b.hi;
    if(isnan(p0)||isnan(p1)||isnan(p2)||isnan(p3))return (SDFInterval){-INFINITY,INFINITY};
    return Interval_Pad((float)fmin(fmin(p0,p1),fmin(p2,p3)),(float)fmax(fmax(p0,p1),fmax(p2,p3)),2);
}
static SDFInterval Interval_Scale(SDFInterval a,float scale) {
    return Interval_Multiply(a,Interval_Constant(scale));
}
static SDFInterval Interval_Min(SDFInterval a,SDFInterval b) {
    return (SDFInterval){fminf(a.lo,b.lo),fminf(a.hi,b.hi)};
}
static SDFInterval Interval_Max(SDFInterval a,SDFInterval b) {
    return (SDFInterval){fmaxf(a.lo,b.lo),fmaxf(a.hi,b.hi)};
}
static SDFInterval Interval_Clamp(SDFInterval a,float lo,float hi) {
    return Interval_Min(Interval_Max(a,Interval_Constant(lo)),Interval_Constant(hi));
}
static SDFInterval Interval_Divide(SDFInterval a,SDFInterval b) {
    if(b.lo<=0&&b.hi>=0)return (SDFInterval){-INFINITY,INFINITY};
    double p0=(double)a.lo/b.lo,p1=(double)a.lo/b.hi,p2=(double)a.hi/b.lo,p3=(double)a.hi/b.hi;
    // GLSL 4.30 permite hasta 2,5 ULP en división, además de la conversión.
    return Interval_Pad((float)fmin(fmin(p0,p1),fmin(p2,p3)),(float)fmax(fmax(p0,p1),fmax(p2,p3)),4);
}
static SDFInterval Interval_Absolute(SDFInterval a) {
    return (SDFInterval){a.lo<=0&&a.hi>=0?0:fminf(fabsf(a.lo),fabsf(a.hi)),fmaxf(fabsf(a.lo),fabsf(a.hi))};
}
static SDFInterval Interval_Square(SDFInterval a) {
    a=Interval_Absolute(a);
    return Interval_Pad((float)((double)a.lo*a.lo),(float)((double)a.hi*a.hi),2);
}
static SDFInterval Interval_Sqrt(SDFInterval a) {
    if(a.hi<0)return (SDFInterval){-INFINITY,INFINITY};
    // sqrt hereda el error de 1/inversesqrt; se cubren ambas operaciones.
    return Interval_Pad((float)sqrt(fmax(0,a.lo)),(float)sqrt(fmax(0,a.hi)),8);
}
static SDFInterval Interval_SinHalfTurn(SDFInterval a) {
    a=Interval_Clamp(a,0,3.14159265f);
    float first=SDF_SinHalfTurn(a.lo),last=SDF_SinHalfTurn(a.hi);
    float hi=fmaxf(first,last);
    if(a.lo<=1.570796325f&&a.hi>=1.570796325f)hi=1;
    // Una cota del error del polinomio, no una constante de detalle geométrico.
    return Interval_Pad(fminf(first,last)-.000008f,hi+.000008f,2);
}
static SDFInterval Interval_Project(AABB3D b,Vector3 basis) {
    SDFInterval x={b.start.x,b.end.x},y={b.start.y,b.end.y},z={b.start.z,b.end.z};
    return Interval_Add(Interval_Add(Interval_Scale(x,basis.x),Interval_Scale(y,basis.y)),Interval_Scale(z,basis.z));
}
SDFInterval SDF_CurvedPinnaInterval(AABB3D bounds,Vector3 shape,Vector3 up,Vector3 side,Vector3 normal,
    float tipFraction,float concavity,float curve,float rootRoll,float tipRoundness,float fold,
    float rootFlare,float marginBow,float marginAsymmetry) {
    float h=fmaxf(fabsf(shape.y),.01f),w=fmaxf(fabsf(shape.x),.006f),th=fmaxf(fabsf(shape.z),.003f);
    SDFInterval y=Interval_Project(bounds,up);
    SDFInterval t=Interval_Clamp(Interval_Add(Interval_Divide(y,Interval_Constant(h)),Interval_Constant(.5f)),0,1);
    SDFInterval t2=Interval_Square(t),sinT=Interval_SinHalfTurn(Interval_Scale(t,3.14159265f));
    SDFInterval cx=Interval_Scale(Interval_Add(Interval_Scale(sinT,.45f),Interval_Scale(t2,.24f)),w*marginBow);
    SDFInterval cz=Interval_Add(Interval_Scale(sinT,curve),Interval_Scale(t2,fold));
    SDFInterval x=Interval_Subtract(Interval_Project(bounds,side),cx),z=Interval_Subtract(Interval_Project(bounds,normal),cz);
    float tipStart=.90f-.10f*fminf(1,fmaxf(0,tipRoundness));
    SDFInterval profile=Interval_Min(t,Interval_Constant(tipStart));
    SDFInterval distal=Interval_Multiply(Interval_Multiply(profile,profile),profile);distal=Interval_Square(distal);
    SDFInterval inv=Interval_Subtract(Interval_Constant(1),profile);
    SDFInterval flare=Interval_Scale(profile,rootFlare*.32f*4.0f);
    flare=Interval_Multiply(Interval_Multiply(Interval_Multiply(flare,inv),inv),inv);
    SDFInterval width=Interval_Add(Interval_Constant(.76f),Interval_Scale(Interval_SinHalfTurn(Interval_Scale(profile,3.14159265f)),.34f));
    width=Interval_Subtract(width,Interval_Scale(distal,.76f-fminf(.55f,fmaxf(.04f,tipFraction))));
    width=Interval_Scale(Interval_Add(width,flare),w);
    SDFInterval tipU=Interval_Clamp(Interval_Divide(Interval_Subtract(t,Interval_Constant(tipStart)),Interval_Constant(1-tipStart)),0,1);
    SDFInterval tipArc=Interval_Sqrt(Interval_Max(Interval_Constant(0),Interval_Subtract(Interval_Constant(1),Interval_Square(tipU))));
    width=Interval_Multiply(width,tipArc);
    SDFInterval asym=Interval_Scale(Interval_Add(Interval_Constant(.3f),Interval_Scale(t,.7f)),(marginAsymmetry-.5f)*.32f);
    SDFInterval positive=Interval_Add(Interval_Constant(1),asym),negative=Interval_Subtract(Interval_Constant(1),asym);
    SDFInterval factor=x.lo>=0?positive:x.hi<0?negative:(SDFInterval){fminf(positive.lo,negative.lo),fmaxf(positive.hi,negative.hi)};
    width=Interval_Multiply(width,factor);
    SDFInterval lateral=Interval_Divide(x,Interval_Max(width,Interval_Constant(.001f)));
    SDFInterval edge=Interval_Clamp(Interval_Divide(Interval_Subtract(Interval_Absolute(lateral),Interval_Constant(.70f)),Interval_Constant(.25f)),0,1);
    SDFInterval invT=Interval_Subtract(Interval_Constant(1),t);
    SDFInterval thick=Interval_Scale(Interval_Subtract(Interval_Constant(.92f),Interval_Scale(t,.64f)),th);
    SDFInterval roll=Interval_Scale(invT,th*rootRoll*.32f);roll=Interval_Multiply(roll,invT);
    thick=Interval_Add(thick,roll);
    SDFInterval rim=Interval_Scale(edge,th*(.18f+.12f*rootRoll));rim=Interval_Multiply(rim,Interval_Subtract(Interval_Constant(1),t2));
    thick=Interval_Multiply(Interval_Add(thick,rim),tipArc);
    SDFInterval cup=Interval_Scale(Interval_Add(Interval_Constant(.65f),Interval_Scale(invT,1.25f)),th*concavity);
    cup=Interval_Multiply(cup,Interval_Max(Interval_Constant(0),Interval_Subtract(Interval_Constant(1),Interval_Square(lateral))));
    SDFInterval transverse=Interval_Scale(Interval_Square(lateral),th*.18f*rootRoll);transverse=Interval_Multiply(transverse,invT);
    SDFInterval qx=Interval_Subtract(Interval_Absolute(x),width);
    SDFInterval qz=Interval_Subtract(Interval_Absolute(Interval_Subtract(Interval_Add(z,cup),transverse)),thick);
    SDFInterval px=Interval_Max(qx,Interval_Constant(0)),pz=Interval_Max(qz,Interval_Constant(0));
    SDFInterval shell=Interval_Add(Interval_Sqrt(Interval_Add(Interval_Square(px),Interval_Square(pz))),Interval_Min(Interval_Max(qx,qz),Interval_Constant(0)));
    SDFInterval cap=Interval_Max(Interval_Subtract(Interval_Scale(y,-1),Interval_Constant(h*.5f)),Interval_Subtract(y,Interval_Constant(h*.5f)));
    SDFInterval body=Interval_Subtract(Interval_Max(shell,cap),Interval_Constant(th*.08f));
    if(concavity>.001f) {
        SDFInterval xc=Interval_Divide(x,Interval_Constant(fmaxf(w*.73f,.002f)));
        SDFInterval yc=Interval_Divide(Interval_Add(y,Interval_Constant(h*.23f)),Interval_Constant(fmaxf(h*.34f,.002f)));
        SDFInterval zc=Interval_Divide(Interval_Subtract(z,Interval_Constant(th*.35f)),Interval_Constant(fmaxf(th*(.80f+.70f*concavity),.002f)));
        SDFInterval cavity=Interval_Subtract(Interval_Sqrt(Interval_Add(Interval_Add(Interval_Square(xc),Interval_Square(yc)),Interval_Square(zc))),Interval_Constant(1));
        cavity=Interval_Scale(cavity,fminf(w*.73f,h*.34f));body=Interval_Max(body,Interval_Scale(cavity,-1));
    }
    return body;
}

/* Estas operaciones conservan el orden de evaluación escalar y ensanchan
 * cada operación. Un cociente que cruza cero permanece desconocido. */
static SDFInterval Interval_Lerp(SDFInterval a,SDFInterval b,SDFInterval t) {
    SDFInterval first=Interval_Add(a,Interval_Multiply(Interval_Subtract(b,a),t));
    /* GLSL mix permite la forma distributiva además de a+t*(b-a). */
    SDFInterval second=Interval_Add(Interval_Multiply(a,Interval_Subtract(Interval_Constant(1),t)),Interval_Multiply(b,t));
    return (SDFInterval){fminf(first.lo,second.lo),fmaxf(first.hi,second.hi)};
}
static SDFInterval Interval_Length(SDFInterval x,SDFInterval y,SDFInterval z) {
    return Interval_Sqrt(Interval_Add(Interval_Add(Interval_Square(x),Interval_Square(y)),Interval_Square(z)));
}
static SDFInterval Interval_Component(AABB3D box,int component) {
    if(component==0)return (SDFInterval){box.start.x,box.end.x};
    if(component==1)return (SDFInterval){box.start.y,box.end.y};
    return (SDFInterval){box.start.z,box.end.z};
}
static SDFInterval Interval_Hull(SDFInterval a,SDFInterval b) {
    return (SDFInterval){fminf(a.lo,b.lo),fmaxf(a.hi,b.hi)};
}
SDFInterval SDF_EllipsoidInterval(AABB3D box,Vector3 radii) {
    float rx=fmaxf(radii.x,.0001f),ry=fmaxf(radii.y,.0001f),rz=fmaxf(radii.z,.0001f);
    SDFInterval ix=Interval_Divide(Interval_Constant(1),Interval_Constant(rx));
    SDFInterval iy=Interval_Divide(Interval_Constant(1),Interval_Constant(ry));
    SDFInterval iz=Interval_Divide(Interval_Constant(1),Interval_Constant(rz));
    SDFInterval x=Interval_Component(box,0),y=Interval_Component(box,1),z=Interval_Component(box,2);
    SDFInterval k0=Interval_Length(Interval_Multiply(x,ix),Interval_Multiply(y,iy),Interval_Multiply(z,iz));
    SDFInterval k1=Interval_Length(Interval_Multiply(x,Interval_Square(ix)),Interval_Multiply(y,Interval_Square(iy)),Interval_Multiply(z,Interval_Square(iz)));
    SDFInterval center=Interval_Constant(-fminf(rx,fminf(ry,rz)));
    if(k0.hi<.000001f||k1.hi<.000001f)return center;
    /* Separar la rama singular antes de dividir: evita infinitos evitables,
     * pero nunca estrecha el intervalo suponiendo un cociente geométrico. */
    bool includesCenter=k0.lo<.000001f||k1.lo<.000001f;
    k0=Interval_Max(k0,Interval_Constant(.000001f));
    k1=Interval_Max(k1,Interval_Constant(.000001f));
    SDFInterval value=Interval_Divide(Interval_Multiply(k0,Interval_Subtract(k0,Interval_Constant(1))),k1);
    return includesCenter?Interval_Hull(value,center):value;
}
SDFInterval SDF_CapsuleInterval(AABB3D box,Vector3 a,Vector3 b,float radius) {
    Vector3 ba=Vec3_Sub(b,a);
    float lengthValue=Vec3_Dot(ba,ba);
    SDFInterval lengthSquared=Interval_Pad(lengthValue,lengthValue,8);
    SDFInterval x=Interval_Subtract(Interval_Component(box,0),Interval_Constant(a.x));
    SDFInterval y=Interval_Subtract(Interval_Component(box,1),Interval_Constant(a.y));
    SDFInterval z=Interval_Subtract(Interval_Component(box,2),Interval_Constant(a.z));
    SDFInterval sphere=Interval_Subtract(Interval_Length(x,y,z),Interval_Constant(fmaxf(radius,.0001f)));
    if(lengthSquared.hi>=.00000001f) {
        lengthSquared=Interval_Max(lengthSquared,Interval_Constant(.00000001f));
        SDFInterval dot=Interval_Add(Interval_Add(Interval_Scale(x,ba.x),Interval_Scale(y,ba.y)),Interval_Scale(z,ba.z));
        SDFInterval t=Interval_Clamp(Interval_Divide(dot,lengthSquared),0,1);
        x=Interval_Subtract(x,Interval_Scale(t,ba.x));y=Interval_Subtract(y,Interval_Scale(t,ba.y));z=Interval_Subtract(z,Interval_Scale(t,ba.z));
    }
    SDFInterval capsule=Interval_Subtract(Interval_Length(x,y,z),Interval_Constant(fmaxf(radius,.0001f)));
    if(Interval_Pad(lengthValue,lengthValue,8).lo<.00000001f)return Interval_Hull(capsule,sphere);
    return capsule;
}
SDFInterval SDF_RoundedSlotInterval(AABB3D box,float halfWidth,float halfHeight,float halfDepth) {
    float hw=fmaxf(halfWidth,.0001f),hh=fmaxf(halfHeight,.0001f),hd=fmaxf(halfDepth,.0001f);
    SDFInterval qx=Interval_Max(Interval_Subtract(Interval_Absolute(Interval_Component(box,0)),Interval_Constant(fmaxf(hw-hh,0))),Interval_Constant(0));
    SDFInterval profile=Interval_Subtract(Interval_Length(qx,Interval_Component(box,1),Interval_Constant(0)),Interval_Constant(hh));
    SDFInterval depth=Interval_Subtract(Interval_Absolute(Interval_Component(box,2)),Interval_Constant(hd));
    SDFInterval outside=Interval_Length(Interval_Max(profile,Interval_Constant(0)),Interval_Max(depth,Interval_Constant(0)),Interval_Constant(0));
    return Interval_Add(outside,Interval_Min(Interval_Max(profile,depth),Interval_Constant(0)));
}
SDFInterval SDF_SmoothUnionInterval(SDFInterval a,SDFInterval b,float k) {
    if(k<=.0001f)return Interval_Min(a,b);
    SDFInterval h=Interval_Clamp(Interval_Add(Interval_Constant(.5f),Interval_Divide(Interval_Scale(Interval_Subtract(b,a),.5f),Interval_Constant(k))),0,1);
    if(h.lo>=1)return a;
    if(h.hi<=0)return b;
    SDFInterval blended=Interval_Subtract(Interval_Lerp(b,a,h),Interval_Multiply(Interval_Scale(h,k),Interval_Subtract(Interval_Constant(1),h)));
    if(h.lo<=0)blended=Interval_Hull(blended,b);
    if(h.hi>=1)blended=Interval_Hull(blended,a);
    return blended;
}
SDFInterval SDF_SmoothSubtractInterval(SDFInterval body,SDFInterval cutter,float k) {
    if(k<=.0001f)return Interval_Max(body,Interval_Scale(cutter,-1));
    SDFInterval h=Interval_Clamp(Interval_Subtract(Interval_Constant(.5f),Interval_Divide(Interval_Scale(Interval_Add(body,cutter),.5f),Interval_Constant(k))),0,1);
    return Interval_Add(Interval_Lerp(body,Interval_Scale(cutter,-1),h),Interval_Multiply(Interval_Scale(h,k),Interval_Subtract(Interval_Constant(1),h)));
}

static SDFInterval Interval_Hermite(float a,float b,float da,float db,SDFInterval span,SDFInterval t) {
    SDFInterval one=Interval_Constant(1),t2=Interval_Square(t),t3=Interval_Multiply(t2,t);
    SDFInterval h0=Interval_Add(Interval_Subtract(Interval_Scale(t3,2),Interval_Scale(t2,3)),one);
    SDFInterval h1=Interval_Add(Interval_Subtract(t3,Interval_Scale(t2,2)),t);
    SDFInterval h2=Interval_Add(Interval_Scale(t3,-2),Interval_Scale(t2,3));
    SDFInterval h3=Interval_Subtract(t3,t2);
    return Interval_Add(Interval_Add(Interval_Add(Interval_Scale(h0,a),Interval_Scale(Interval_Multiply(h1,span),da)),Interval_Scale(h2,b)),Interval_Scale(Interval_Multiply(h3,span),db));
}
SDFInterval SDF_SweepSegmentInterval(AABB3D box,Vector3 centerA,Vector3 centerB,
    float widthA,float widthB,float heightA,float heightB,float widthSlopeA,float widthSlopeB,
    float heightSlopeA,float heightSlopeB,float centerSlopeA,float centerSlopeB,float firstZ,float lastZ) {
    SDFInterval span=Interval_Subtract(Interval_Constant(centerB.z),Interval_Constant(centerA.z));
    if(span.lo<=0&&span.hi>=0)return (SDFInterval){-INFINITY,INFINITY};
    SDFInterval z=Interval_Component(box,2);
    SDFInterval t=Interval_Clamp(Interval_Divide(Interval_Subtract(z,Interval_Constant(centerA.z)),span),0,1);
    SDFInterval width=Interval_Max(Interval_Hermite(widthA,widthB,widthSlopeA,widthSlopeB,span,t),Interval_Constant(.0001f));
    SDFInterval height=Interval_Max(Interval_Hermite(heightA,heightB,heightSlopeA,heightSlopeB,span,t),Interval_Constant(.0001f));
    SDFInterval cy=Interval_Hermite(centerA.y,centerB.y,centerSlopeA,centerSlopeB,span,t);
    SDFInterval cx=Interval_Lerp(Interval_Constant(centerA.x),Interval_Constant(centerB.x),t);
    SDFInterval outside=Interval_Add(Interval_Max(Interval_Subtract(z,Interval_Constant(firstZ)),Interval_Constant(0)),
        Interval_Min(Interval_Subtract(z,Interval_Constant(lastZ)),Interval_Constant(0)));
    SDFInterval r=Interval_Min(width,height);
    SDFInterval dx=Interval_Divide(Interval_Subtract(Interval_Component(box,0),cx),width);
    SDFInterval dy=Interval_Divide(Interval_Subtract(Interval_Component(box,1),cy),height);
    SDFInterval dz=Interval_Divide(outside,r);
    return Interval_Multiply(Interval_Subtract(Interval_Length(dx,dy,dz),Interval_Constant(1)),r);
}

SDFInterval SDF_EllipticalSweepInterval(AABB3D box,const SDFSweepStation* stations,int count) {
    if(!stations||count<2)return Interval_Constant(1e6f);
    SDFInterval result={INFINITY,-INFINITY};
    for(int i=0;i<count-1;++i) {
        const SDFSweepStation* a=&stations[i];const SDFSweepStation* b=&stations[i+1];
        if(!(b->center.z<a->center.z))return (SDFInterval){-INFINITY,INFINITY};
        AABB3D piece=box;
        if(i<count-2)piece.start.z=fmaxf(piece.start.z,b->center.z);
        if(i>0)piece.end.z=fminf(piece.end.z,a->center.z);
        if(piece.start.z>piece.end.z)continue;
        SDFInterval next=SDF_SweepSegmentInterval(piece,a->center,b->center,a->width,b->width,a->height,b->height,
            a->widthSlope,b->widthSlope,a->heightSlope,b->heightSlope,a->centerSlope,b->centerSlope,
            stations[0].center.z,stations[count-1].center.z);
        result=Interval_Hull(result,next);
    }
    return result;
}

static SDFInterval Interval_RelativeProject(AABB3D box,Vector3 origin,Vector3 basis) {
    SDFInterval x=Interval_Subtract(Interval_Component(box,0),Interval_Constant(origin.x));
    SDFInterval y=Interval_Subtract(Interval_Component(box,1),Interval_Constant(origin.y));
    SDFInterval z=Interval_Subtract(Interval_Component(box,2),Interval_Constant(origin.z));
    return Interval_Add(Interval_Add(Interval_Scale(x,basis.x),Interval_Scale(y,basis.y)),Interval_Scale(z,basis.z));
}
SDFInterval SDF_ConnectorPowerInterval(AABB3D box,Vector3 origin,Vector3 forward,Vector3 side,Vector3 up,
    float length,float widthA,float widthB,float heightA,float heightB,float widthBulge,float heightBulge,bool hoof) {
    if(length<.000001f) {
        SDFInterval x=Interval_Subtract(Interval_Component(box,0),Interval_Constant(origin.x));
        SDFInterval y=Interval_Subtract(Interval_Component(box,1),Interval_Constant(origin.y));
        SDFInterval z=Interval_Subtract(Interval_Component(box,2),Interval_Constant(origin.z));
        AABB3D relative={Vec3_Create(x.lo,y.lo,z.lo),Vec3_Create(x.hi,y.hi,z.hi)};
        return SDF_EllipsoidInterval(relative,Vec3_Create(widthA,heightA,widthA));
    }
    SDFInterval along=Interval_RelativeProject(box,origin,forward);
    SDFInterval t=Interval_Clamp(Interval_Divide(along,Interval_Constant(length)),0,1);
    SDFInterval inv=Interval_Subtract(Interval_Constant(1),t);
    SDFInterval width=Interval_Add(Interval_Lerp(Interval_Constant(widthA),Interval_Constant(widthB),t),Interval_Multiply(Interval_Scale(t,widthBulge*4),inv));
    SDFInterval height=Interval_Add(Interval_Lerp(Interval_Constant(heightA),Interval_Constant(heightB),t),Interval_Multiply(Interval_Scale(t,heightBulge*4),inv));
    width=Interval_Max(width,Interval_Constant(.0001f));height=Interval_Max(height,Interval_Constant(.0001f));
    SDFInterval r=Interval_Min(width,height);
    SDFInterval outside=Interval_Add(Interval_Min(along,Interval_Constant(0)),Interval_Max(Interval_Subtract(along,Interval_Constant(length)),Interval_Constant(0)));
    SDFInterval x=Interval_Divide(Interval_RelativeProject(box,origin,side),width);
    SDFInterval y=Interval_Divide(Interval_RelativeProject(box,origin,up),height);
    SDFInterval z=Interval_Divide(outside,r);
    SDFInterval norm=hoof?Interval_Sqrt(Interval_Sqrt(Interval_Add(Interval_Add(
        Interval_Square(Interval_Square(x)),Interval_Square(Interval_Square(y))),Interval_Square(Interval_Square(z))))):Interval_Length(x,y,z);
    return Interval_Multiply(Interval_Subtract(norm,Interval_Constant(1)),r);
}

SDFInterval SDF_ConnectorInterval(AABB3D box,Vector3 origin,Vector3 forward,Vector3 side,Vector3 up,
    float length,float widthA,float widthB,float heightA,float heightB,float widthBulge,float heightBulge) {
    return SDF_ConnectorPowerInterval(box,origin,forward,side,up,length,widthA,widthB,heightA,heightB,widthBulge,heightBulge,false);
}
