#include "SDFPrimitives.h"
#include "MathUtils.h"
#include <math.h>

float SDF_Sphere(Vector3 point, float radius) {
    if (radius <= 0.0001f) return Vec3_Length(point);
    return Vec3_Length(point) - radius;
}

float SDF_Ellipsoid(Vector3 p, Vector3 r) {
    /* Protección contra radios nulos o negativos */
    float rx = Math_Max(r.x, 0.0001f);
    float ry = Math_Max(r.y, 0.0001f);
    float rz = Math_Max(r.z, 0.0001f);

    Vector3 invR = Vec3_Create(1.0f / rx, 1.0f / ry, 1.0f / rz);
    Vector3 invR2 = Vec3_Create(invR.x * invR.x, invR.y * invR.y, invR.z * invR.z);

    Vector3 scaledP = Vec3_Create(p.x * invR.x, p.y * invR.y, p.z * invR.z);
    float k0 = Vec3_Length(scaledP);

    Vector3 scaledP2 = Vec3_Create(p.x * invR2.x, p.y * invR2.y, p.z * invR2.z);
    float k1 = Vec3_Length(scaledP2);

    if (k0 < 1e-6f || k1 < 1e-6f) {
        float minR = Math_Min(rx, Math_Min(ry, rz));
        return -minR;
    }
    return k0 * (k0 - 1.0f) / k1;
}

float SDF_Capsule(Vector3 p, Vector3 a, Vector3 b, float radius) {
    float r = Math_Max(radius, 0.0001f);
    Vector3 pa = Vec3_Sub(p, a);
    Vector3 ba = Vec3_Sub(b, a);

    float baLengthSq = Vec3_Dot(ba, ba);
    if (baLengthSq < 1e-8f) {
        return Vec3_Length(pa) - r;
    }

    float h = Math_Clamp01(Vec3_Dot(pa, ba) / baLengthSq);
    Vector3 projection = Vec3_Sub(pa, Vec3_Scale(ba, h));
    return Vec3_Length(projection) - r;
}

float SDF_TaperedCapsuleApprox(Vector3 p, Vector3 a, Vector3 b, float r1, float r2) {
    r1 = Math_Max(r1, 0.0001f);
    r2 = Math_Max(r2, 0.0001f);

    Vector3 pa = Vec3_Sub(p, a);
    Vector3 ba = Vec3_Sub(b, a);

    float baLengthSq = Vec3_Dot(ba, ba);
    if (baLengthSq < 1e-8f) {
        return Vec3_Length(pa) - r1;
    }

    float h = Math_Clamp01(Vec3_Dot(pa, ba) / baLengthSq);
    float r = Math_Lerp(r1, r2, h);
    Vector3 projection = Vec3_Sub(pa, Vec3_Scale(ba, h));
    return Vec3_Length(projection) - r;
}

float SDF_TaperedEllipticalCapsuleApprox(Vector3 p, Vector3 a, Vector3 b, Vector3 ra, Vector3 rb) {
    Vector3 ba=Vec3_Sub(b,a); float lengthSquared=Vec3_Dot(ba,ba);
    float t=lengthSquared>1e-8f?Math_Clamp01(Vec3_Dot(Vec3_Sub(p,a),ba)/lengthSquared):0.0f;
    Vector3 center=Vec3_Lerp(a,b,t);
    Vector3 radii=Vec3_Lerp(ra,rb,t);
    float body=SDF_Ellipsoid(Vec3_Sub(p,center),radii);
    float capA=SDF_Ellipsoid(Vec3_Sub(p,a),ra);
    float capB=SDF_Ellipsoid(Vec3_Sub(p,b),rb);
    return Math_Min(body,Math_Min(capA,capB));
}

float SDF_ThreeSectionEllipticalLoftApprox(Vector3 p,
                                           Vector3 root, Vector3 mid, Vector3 tip,
                                           Vector3 rootRadii, Vector3 midRadii,
                                           Vector3 tipRadii, float blend) {
    float rear = SDF_TaperedEllipticalCapsuleApprox(p, root, mid, rootRadii, midRadii);
    float front = SDF_TaperedEllipticalCapsuleApprox(p, mid, tip, midRadii, tipRadii);
    float k = Math_Max(blend, 0.0f);
    if (k <= 0.0001f) return Math_Min(rear, front);
    float h = Math_Clamp01(0.5f + 0.5f * (front - rear) / k);
    return Math_Lerp(front, rear, h) - k * h * (1.0f - h);
}

float SDF_TaperedEllipticalSegmentApprox(Vector3 p, Vector3 a, Vector3 b,
                                         float widthA, float heightA,
                                         float widthB, float heightB) {
    Vector3 axis = Vec3_Sub(b, a);
    float length = Vec3_Length(axis);
    if (length < 1e-6f) return SDF_Ellipsoid(Vec3_Sub(p, a),
        Vec3_Create(Math_Max(widthA, .0001f), Math_Max(heightA, .0001f), Math_Max(widthA, .0001f)));
    Vector3 forward = Vec3_Scale(axis, 1.0f / length);
    Vector3 referenceUp = fabsf(forward.y) > .94f ? Vec3_Create(1,0,0) : Vec3_Create(0,1,0);
    Vector3 side = Vec3_Normalize(Vec3_Cross(referenceUp, forward));
    Vector3 up = Vec3_Normalize(Vec3_Cross(forward, side));
    Vector3 pa = Vec3_Sub(p, a);
    float along = Vec3_Dot(pa, forward);
    float t = Math_Clamp01(along / length);
    float width = Math_Max(Math_Lerp(widthA, widthB, t), .0001f);
    float height = Math_Max(Math_Lerp(heightA, heightB, t), .0001f);
    float cap = Math_Min(width, height);
    float outsideAxis = along < 0.0f ? along : (along > length ? along - length : 0.0f);
    Vector3 center = Vec3_Add(a, Vec3_Scale(forward, Math_Clamp(along, 0.0f, length)));
    Vector3 radial = Vec3_Sub(p, center);
    float sx = Vec3_Dot(radial, side) / width;
    float sy = Vec3_Dot(radial, up) / height;
    float sz = outsideAxis / cap;
    return (sqrtf(sx*sx + sy*sy + sz*sz) - 1.0f) * Math_Min(width, height);
}

float SDF_RoundedTaperedWedge(Vector3 p, Vector3 root, Vector3 tip,
                             float rootHalfWidth, float tipHalfWidth,
                             float rootHalfHeight, float tipHalfHeight,
                             float rounding) {
    Vector3 axis = Vec3_Sub(tip, root);
    float length = Vec3_Length(axis);
    if (length < 1e-6f) return SDF_Ellipsoid(Vec3_Sub(p, root),
        Vec3_Create(rootHalfWidth, rootHalfHeight, rootHalfWidth));
    Vector3 forward = Vec3_Scale(axis, 1.0f / length);
    Vector3 referenceUp = fabsf(forward.y) > .94f ? Vec3_Create(1,0,0) : Vec3_Create(0,1,0);
    Vector3 side = Vec3_Normalize(Vec3_Cross(referenceUp, forward));
    Vector3 up = Vec3_Normalize(Vec3_Cross(forward, side));
    Vector3 rel = Vec3_Sub(p, root);
    float z = Vec3_Dot(rel, forward);
    float t = Math_Clamp01(z / length);
    float hw = Math_Max(Math_Lerp(rootHalfWidth, tipHalfWidth, t), .0001f);
    float hh = Math_Max(Math_Lerp(rootHalfHeight, tipHalfHeight, t), .0001f);
    float r = Math_Clamp(rounding, 0.0f, Math_Min(hw, hh) * .75f);
    float x = fabsf(Vec3_Dot(rel, side)) - (hw - r);
    float y = fabsf(Vec3_Dot(rel, up)) - (hh - r);
    /* Las tres dimensiones retroceden el radio para redondear también las tapas. */
    float dz = Math_Max(r - z, z - (length - r));
    float ox = Math_Max(x, 0.0f), oy = Math_Max(y, 0.0f), oz = Math_Max(dz, 0.0f);
    float outside = sqrtf(ox*ox + oy*oy + oz*oz);
    return outside + Math_Min(Math_Max(x, Math_Max(y, dz)), 0.0f) - r;
}

float SDF_Box(Vector3 p, Vector3 b) {
    Vector3 d = Vec3_Create(
        fabsf(p.x) - b.x,
        fabsf(p.y) - b.y,
        fabsf(p.z) - b.z
    );

    Vector3 maxD = Vec3_Create(
        Math_Max(d.x, 0.0f),
        Math_Max(d.y, 0.0f),
        Math_Max(d.z, 0.0f)
    );

    float outsideDistance = Vec3_Length(maxD);
    float insideDistance = Math_Min(Math_Max(d.x, Math_Max(d.y, d.z)), 0.0f);

    return outsideDistance + insideDistance;
}

float SDF_RoundedSlotExtruded(Vector3 p, float halfWidth, float halfHeight, float halfDepth) {
    float hw = Math_Max(halfWidth, 0.0001f);
    float hh = Math_Max(halfHeight, 0.0001f);
    float hd = Math_Max(halfDepth, 0.0001f);

    float radius = hh;
    float straightHalf = Math_Max(hw - radius, 0.0f);

    float qx = Math_Max(fabsf(p.x) - straightHalf, 0.0f);
    float profileDist = sqrtf(qx * qx + p.y * p.y) - radius;
    float depthDist = fabsf(p.z) - hd;

    float maxP = Math_Max(profileDist, 0.0f);
    float maxD = Math_Max(depthDist, 0.0f);
    float outside = sqrtf(maxP * maxP + maxD * maxD);
    float inside = Math_Min(Math_Max(profileDist, depthDist), 0.0f);

    return outside + inside;
}

static float SDF_Hermite(float a,float b,float da,float db,float h,float t) {
    float t2=t*t,t3=t2*t;
    return (2*t3-3*t2+1)*a+(t3-2*t2+t)*h*da+(-2*t3+3*t2)*b+(t3-t2)*h*db;
}

float SDF_EllipticalSweepZ(Vector3 p,const SDFSweepStation* stations,int count) {
    if (!stations || count<2) return 1e6f;
    int i=0;
    while(i<count-2 && p.z<stations[i+1].center.z) ++i;
    const SDFSweepStation *a=&stations[i],*b=&stations[i+1];
    float h=b->center.z-a->center.z;
    float t=Math_Clamp01((p.z-a->center.z)/h);
    float w=Math_Max(SDF_Hermite(a->width,b->width,a->widthSlope,b->widthSlope,h,t),.0001f);
    float v=Math_Max(SDF_Hermite(a->height,b->height,a->heightSlope,b->heightSlope,h,t),.0001f);
    float y=SDF_Hermite(a->center.y,b->center.y,a->centerSlope,b->centerSlope,h,t);
    float x=Math_Lerp(a->center.x,b->center.x,t);
    float z=p.z>stations[0].center.z?p.z-stations[0].center.z:
            p.z<stations[count-1].center.z?p.z-stations[count-1].center.z:0;
    float r=Math_Min(w,v),dx=(p.x-x)/w,dy=(p.y-y)/v,dz=z/r;
    return (sqrtf(dx*dx+dy*dy+dz*dz)-1)*r;
}

bool SDF_SweepResolveTangents(SDFSweepStation* stations,int count) {
    if(!stations || count<2)return false;
    for(int i=0;i<count;++i) {
        SDFSweepStation* st=&stations[i];
        if(!isfinite(st->center.x)||!isfinite(st->center.y)||!isfinite(st->center.z)||
           !isfinite(st->width)||!isfinite(st->height)||st->width<=0||st->height<=0||
           (i>0 && st->center.z>=stations[i-1].center.z))return false;
    }
    for(int i=0;i<count;++i) {
        SDFSweepStation* st=&stations[i];
        SDFSweepStation* prev=&stations[i>0?i-1:i];
        SDFSweepStation* next=&stations[i+1<count?i+1:i];
#define TANGENT(value,slope) { \
        float left=i>0?(st->value-prev->value)/(st->center.z-prev->center.z):0; \
        float right=i+1<count?(next->value-st->value)/(next->center.z-st->center.z):0; \
        st->slope=i==0?right:i==count-1?left:left*right>0?2*left*right/(left+right):0; }
        TANGENT(width,widthSlope);TANGENT(height,heightSlope);TANGENT(center.y,centerSlope);
#undef TANGENT
    }
    return true;
}

/* Perfil continuo equivalente a secciones raíz, concha, escafa y ápice.
 * El ancho conserva masa en la mitad y converge sólo en el tercio distal. */
float SDF_CurvedPinna(Vector3 p,Vector3 shape,Vector3 up,Vector3 side,Vector3 normal,
                      float tipFraction,float concavity,float curve,float rootRoll,
                      float tipRoundness,float fold,float rootFlare,float marginBow,float marginAsymmetry) {
    float h=Math_Max(fabsf(shape.y),.01f),w=Math_Max(fabsf(shape.x),.006f),th=Math_Max(fabsf(shape.z),.003f);
    float y=Vec3_Dot(p,up),t=Math_Clamp01(y/h+.5f);
    float centerX=w*marginBow*(.45f*sinf(3.14159265f*t)+.24f*t*t);
    float centerZ=curve*sinf(3.14159265f*t)+fold*t*t;
    float x=Vec3_Dot(p,side)-centerX,z=Vec3_Dot(p,normal)-centerZ;
    float tipStart=.90f-.10f*Math_Clamp01(tipRoundness);
    float profileT=Math_Min(t,tipStart);
    float distal=profileT*profileT*profileT;
    distal*=distal;
    float flare=rootFlare*.32f*4.0f*profileT*(1.0f-profileT)*(1.0f-profileT)*(1.0f-profileT);
    float width=w*(.76f+.34f*sinf(3.14159265f*profileT)-
                   (.76f-Math_Clamp(tipFraction,.04f,.55f))*distal+flare);
    float tipU=Math_Clamp01((t-tipStart)/(1.0f-tipStart));
    float tipArc=sqrtf(Math_Max(0.0f,1.0f-tipU*tipU));
    width*=tipArc;
    float asym=(marginAsymmetry-.5f)*.32f*(.3f+.7f*t);
    width*=x>=0?1.0f+asym:1.0f-asym;
    float lateral=x/Math_Max(width,.001f);
    float edge=Math_Clamp01((fabsf(lateral)-.70f)/.25f);
    float thick=th*(.92f-.64f*t)+th*rootRoll*.32f*(1.0f-t)*(1.0f-t);
    thick+=th*(.18f+.12f*rootRoll)*edge*(1.0f-t*t);
    thick*=tipArc;
    /* La superficie media retrocede en el centro: interior cóncavo,
     * dorso convexo y reborde libre más grueso. */
    float cup=th*concavity*(.65f+1.25f*(1.0f-t))*Math_Max(0.0f,1.0f-lateral*lateral);
    float transverse=th*.18f*rootRoll*lateral*lateral*(1.0f-t);
    float qx=fabsf(x)-width,qz=fabsf(z+cup-transverse)-thick;
    float shell=sqrtf(Math_Max(qx,0.0f)*Math_Max(qx,0.0f)+Math_Max(qz,0.0f)*Math_Max(qz,0.0f))+
        Math_Min(Math_Max(qx,qz),0.0f);
    float cap=Math_Max(-y-h*.5f,y-h*.5f);
    float body=Math_Max(shell,cap)-th*.08f;
    if(concavity>.001f) {
        /* La concha resta un cuenco frontal real sin atravesar la pared
         * posterior; el margen libre conserva su cartílago redondeado. */
        float cx=x/Math_Max(w*.73f,.002f);
        float cy=(y+h*.23f)/Math_Max(h*.34f,.002f);
        float cz=(z-th*.35f)/Math_Max(th*(.80f+.70f*concavity),.002f);
        float cavity=(sqrtf(cx*cx+cy*cy+cz*cz)-1.0f)*Math_Min(w*.73f,h*.34f);
        body=Math_Max(body,-cavity);
    }
    return body;
}
float SDF_TaperedPinna(Vector3 point,Vector3 shape,Vector3 direction,float tipFraction,float concavity) {
    Vector3 up=Vec3_Normalize(direction);
    Vector3 reference=fabsf(up.z)<.9f?Vec3_Create(0,0,1):Vec3_Create(1,0,0);
    Vector3 side=Vec3_Normalize(Vec3_Cross(up,reference));
    Vector3 normal=Vec3_Cross(side,up);
    return SDF_CurvedPinna(point,shape,up,side,normal,tipFraction,concavity,
                            shape.x*.05f,.25f,1.0f,0.0f,.35f,.4f,.5f);
}
