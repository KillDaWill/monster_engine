/** @file MonsterSDFRayBounds.h
 * @brief Cotas de subnivel para descartar fases completas por rayo, sin cortar el campo.
 * Sólo modifica una copia destinada al empaquetado GPU; no cambia la anatomía.
 */
#ifndef MONSTER_SDF_RAY_BOUNDS_H
#define MONSTER_SDF_RAY_BOUNDS_H
#include "MonsterSDF.h"
#include <math.h>

/** @brief Encierra d_elipsoide <= nivel, incluida su anisotropía. */
static void RayBounds_Ellipsoid(AABB3D* box,Vector3 center,Vector3 radii,float level) {
    radii.x=fmaxf(radii.x,.0001f);radii.y=fmaxf(radii.y,.0001f);radii.z=fmaxf(radii.z,.0001f);
    float r=fminf(radii.x,fminf(radii.y,radii.z));
    AABB_ExpandRadius(box,center,Vec3_Scale(radii,1.f+level/r));
}
/** @brief Cota de un cúbico Hermite mediante sus cuatro controles de Bézier. */
static void RayBounds_Cubic(float a,float b,float da,float db,float h,float* lo,float* hi) {
    float c=a+h*da/3.f,d=b-h*db/3.f;
    *lo=fminf(fminf(a,b),fminf(c,d));*hi=fmaxf(fmaxf(a,b),fmaxf(c,d));
}
/** @brief Transporta una caja local a mundo mediante sus ocho esquinas. */
static AABB3D RayBounds_World(const MonsterSDFMouth* m,AABB3D local) {
    AABB3D box=AABB_Empty();
    for(int i=0;i<8;++i) {
        Vector3 p=Vec3_Create(i&1?local.end.x:local.start.x,i&2?local.end.y:local.start.y,i&4?local.end.z:local.start.z);
        Vector3 q=Vec3_Add(m->center,Vec3_Add(Vec3_Scale(m->inverseRotation.row0,p.x),
            Vec3_Add(Vec3_Scale(m->inverseRotation.row1,p.y),Vec3_Scale(m->inverseRotation.row2,p.z))));
        AABB_ExpandPoint(&box,q);
    }
    return box;
}
/** @brief Soporte de mandíbula barrida y costura bajo cualquier peso articular.
 * La última inversión de la costura siempre usa un ángulo entre cero y el
 * ángulo mandibular. La envolvente angular incluye también iteraciones que
 * no converjan. Las restas mandibulares no amplían el sólido.
 */
static AABB3D RayBounds_PosedJaw(const MonsterSDFMouth* m,float tolerance) {
    if(!m->sweptSkull||m->visualJawScale<.0001f) {
        AABB3D box=AABB_Empty();float r=m->visualJawBoundRadius+8*tolerance;
        AABB_ExpandRadius(&box,m->visualJawPivot,Vec3_Create(r,r,r));
        return RayBounds_World(m,box);
    }
    float scale=m->visualJawScale,level=8*tolerance/scale;
    float unionK=fmaxf(m->seamScale*.08f,.005f);
    float jawLevel=level+(m->taperedMandible?unionK*.25f:0);
    float lip=m->entranceCenterLocal.y-m->entranceHalfExtents.y;
    float depth=fminf(fmaxf(m->jawRadii.y*2/fmaxf(m->faceRootRadii.y,.0001f),.4f),1.4f);
    float sweepLevel=jawLevel/fminf(depth,1.f);
    AABB3D jaw=AABB_Empty();
    for(int i=0;i<5;++i) {
        const SDFSweepStation *a=&m->headStations[i],*b=&m->headStations[i+1];
        float span=b->center.z-a->center.z,w0,w1,h0,h1,y0,y1;
        RayBounds_Cubic(a->width,b->width,a->widthSlope,b->widthSlope,span,&w0,&w1);
        RayBounds_Cubic(a->height,b->height,a->heightSlope,b->heightSlope,span,&h0,&h1);
        RayBounds_Cubic(a->center.y,b->center.y,a->centerSlope,b->centerSlope,span,&y0,&y1);
        float factor=1+sweepLevel/fmaxf(fminf(w0,h0),.0001f);
        float wx=fmaxf(w1,.0001f)*factor,hy=fmaxf(h1,.0001f)*factor;
        float cap=fmaxf(w1,h1)+sweepLevel;
        AABB_ExpandPoint(&jaw,Vec3_Create(fminf(a->center.x,b->center.x)-wx,lip+(y0-hy-lip)*depth,b->center.z-cap));
        AABB_ExpandPoint(&jaw,Vec3_Create(fmaxf(a->center.x,b->center.x)+wx,lip+(y1+hy-lip)*depth,a->center.z+cap));
    }
    jaw.end.y=fminf(jaw.end.y,lip+jawLevel);
    jaw.start.z=fmaxf(jaw.start.z,m->hingeCenterLocal.z-jawLevel);
    if(m->taperedMandible) {
        float dev=fminf(fmaxf((m->cephalicDevelopment-.25f)/.5f,0),1);
        if(dev>.001f) {
            float h=m->faceMidRadii.y*.08f*dev;
            Vector3 a=m->faceRootLocal,b=m->faceTipLocal;
            a.y=lip-h*.3f;b.y=a.y;b.z=a.z+(b.z-a.z)*.75f;
            float r=fmaxf(m->faceRootRadii.x*.32f*dev,m->faceTipRadii.x*.30f*dev);
            r=fmaxf(r,h)*(1+jawLevel/fmaxf(fminf(r,h*.65f),.0001f));
            Vector3 radii=Vec3_Create(r,r,r);
            AABB_ExpandRadius(&jaw,a,radii);AABB_ExpandRadius(&jaw,b,radii);
        }
    }
    float h=m->seamScale;
    if(h<.0001f)h=fmaxf(m->hingeRadius,fmaxf(m->throatRadius,m->entranceHalfExtents.y*2));
    float k=fminf(fmaxf(h*.26f,.012f),h*.38f);
    float seamLevel=level+7*k*.25f;
    AABB3D seam=AABB_Empty();
    Vector3 anchors[]={m->seamSkullLeftLocal,m->seamSkullRightLocal,m->seamJawLeftClosedLocal,
        m->seamJawRightClosedLocal,m->seamJawAnchorLocal,m->seamGularLocal,m->hingeCenterLocal};
    float r=h*.30f+seamLevel;
    for(int i=0;i<7;++i)AABB_ExpandRadius(&seam,anchors[i],Vec3_Create(r,r,r));
    RayBounds_Ellipsoid(&seam,m->seamGularLocal,
        m->taperedMandible?Vec3_Create(h*1.58f,h*.62f,h*.88f):Vec3_Create(h*.74f,h*.68f,h*.72f),seamLevel);
    r=h*.38f+seamLevel;
    AABB_ExpandRadius(&seam,m->hingeCenterLocal,Vec3_Create(r,r,r));
    AABB3D posed=AABB_Empty(),boxes[]={jaw,seam};float maxRadius=0;
    for(int part=0;part<2;++part)for(int corner=0;corner<8;++corner) {
        AABB3D b=boxes[part];
        Vector3 p=Vec3_Create(corner&1?b.end.x:b.start.x,corner&2?b.end.y:b.start.y,corner&4?b.end.z:b.start.z);
        Vector3 rel=Vec3_Scale(Vec3_Sub(p,m->visualJawPivot),scale);
        maxRadius=fmaxf(maxRadius,Vec3_Length(rel));
        for(int step=0;step<=8;++step) {
            float a=m->visualJawAngle*step/8.f,c=cosf(a),s=sinf(a);
            AABB_ExpandPoint(&posed,Vec3_Add(m->visualJawPivot,Vec3_Create(rel.x,c*rel.y-s*rel.z,s*rel.y+c*rel.z)));
        }
    }
    AABB_Pad(&posed,maxRadius*fabsf(m->visualJawAngle)/16.f+2*tolerance);
    return RayBounds_World(m,posed);
}
/** @brief Amplía las cajas existentes para cubrir subniveles y el stencil normal.
 * Una unión polinómica resta como máximo k/4. Se suma el presupuesto de todas
 * las uniones internas, incluso ramas inactivas; cada hoja encierra ese nivel.
 * Las restas sólo aumentan el campo y no amplían su subnivel exterior.
 */
static MonsterSDFMouth RayBounds_PackedMouth(const MonsterSDFMouth* source,float outerSmoothness,float tolerance) {
    MonsterSDFMouth m=*source;
    if(!m.anatomicalHead)return m;
    float k=fmaxf(m.headUnionSmoothness,.005f);
    // 5.04 = 1 (cráneo/rostro) + 3.13 (rasgos) + .35 (nariz) + .56 (orejas).
    float level=outerSmoothness+8.f*tolerance+
        (5.04f*k+3.f*fmaxf(.24f*m.headUnionSmoothness,.002f)+fmaxf(m.faceRounding,0)+m.headBodySmoothness)*.25f;
    AABB3D head=AABB_Empty();
#define E(center,radii) RayBounds_Ellipsoid(&head,m.center,m.radii,level)
    E(craniumCenterLocal,craniumRadii);
    if(!m.sweptSkull) {
    E(faceRootLocal,faceRootRadii);E(faceMidLocal,faceMidRadii);E(faceTipLocal,faceTipRadii);
    // Las secciones interpoladas quedan en la envolvente de esferas con
    // radio máximo y anisotropía mínima de todos los extremos del rostro.
    Vector3 faceR[3]={m.faceRootRadii,m.faceMidRadii,m.faceTipRadii};
    Vector3 faceP[3]={m.faceRootLocal,m.faceMidLocal,m.faceTipLocal};
    float faceMin=1e30f,faceMax=0;
    for(int i=0;i<3;++i){faceMin=fminf(faceMin,fminf(faceR[i].x,fminf(faceR[i].y,faceR[i].z)));faceMax=fmaxf(faceMax,fmaxf(faceR[i].x,fmaxf(faceR[i].y,faceR[i].z)));}
    float faceRadius=fmaxf(faceMax,.0001f)*(1.f+level/fmaxf(faceMin,.0001f));
    for(int i=0;i<3;++i)AABB_ExpandRadius(&head,faceP[i],Vec3_Create(faceRadius,faceRadius,faceRadius));
    }
    E(leftTemporalCenterLocal,temporalRadii);E(rightTemporalCenterLocal,temporalRadii);
    E(leftMaxillaryCenterLocal,maxillaryRadii);E(rightMaxillaryCenterLocal,maxillaryRadii);
    E(cheekCenterLocal,cheekRadii);
    Vector3 right=m.cheekCenterLocal;right.x=-right.x;
    RayBounds_Ellipsoid(&head,right,m.cheekRadii,level);
    E(leftBrowCenterLocal,browRadii);E(rightBrowCenterLocal,browRadii);
    E(leftOrbitRimCenterLocal,orbitRimRadii);E(rightOrbitRimCenterLocal,orbitRimRadii);
    if(m.hasNasalPad){E(noseCenterLocal,noseRadii);}
    if(m.hasEars){
        Vector3 ear=Vec3_Add(m.earRadii,Vec3_Create(level,level,level));
        AABB_ExpandRadius(&head,m.leftEarCenterLocal,ear);
        AABB_ExpandRadius(&head,m.rightEarCenterLocal,ear);
    }
#undef E
    float collarMin=fmaxf(.0001f,fminf(fminf(m.neckCollarRootRadii.x,m.neckCollarRootRadii.y),
        fminf(fminf(m.neckCollarMidRadii.x,m.neckCollarMidRadii.y),fminf(m.neckCollarTipRadii.x,m.neckCollarTipRadii.y))));
    float collarMax=fmaxf(fmaxf(m.neckCollarRootRadii.x,m.neckCollarRootRadii.y),
        fmaxf(fmaxf(m.neckCollarMidRadii.x,m.neckCollarMidRadii.y),fmaxf(m.neckCollarTipRadii.x,m.neckCollarTipRadii.y)));
    float radius=collarMax*(1.f+level/collarMin);
    Vector3 r=Vec3_Create(radius,radius,radius);
    AABB_ExpandRadius(&head,m.neckCollarRootLocal,r);
    AABB_ExpandRadius(&head,m.neckCollarMidLocal,r);
    AABB_ExpandRadius(&head,m.neckCollarTipLocal,r);
    if(m.sweptSkull)for(int i=0;i<5;++i) {
        const SDFSweepStation *a=&m.headStations[i],*b=&m.headStations[i+1];
        float h=b->center.z-a->center.z,w0,w1,v0,v1,y0,y1;
        RayBounds_Cubic(a->width,b->width,a->widthSlope,b->widthSlope,h,&w0,&w1);
        RayBounds_Cubic(a->height,b->height,a->heightSlope,b->heightSlope,h,&v0,&v1);
        RayBounds_Cubic(a->center.y,b->center.y,a->centerSlope,b->centerSlope,h,&y0,&y1);
        float factor=1.f+level/fmaxf(fminf(w0,v0),.0001f);
        float wx=fmaxf(w1,.0001f)*factor,vy=fmaxf(v1,.0001f)*factor;
        float cap=fmaxf(w1,v1)+level;
        AABB_ExpandPoint(&head,Vec3_Create(fminf(a->center.x,b->center.x)-wx,y0-vy,b->center.z-cap));
        AABB_ExpandPoint(&head,Vec3_Create(fmaxf(a->center.x,b->center.x)+wx,y1+vy,a->center.z+cap));
    }
    // Los cutters cefálicos también deben conservarse fuera del volumen sólido.
    float cut=m.headUnionSmoothness*.06f+8.f*tolerance;
    RayBounds_Ellipsoid(&head,Vec3_Add(m.leftOrbitCenterLocal,Vec3_Scale(m.leftOrbitNormal,m.orbitRadii.x*.18f)),m.orbitRadii,cut);
    RayBounds_Ellipsoid(&head,Vec3_Add(m.rightOrbitCenterLocal,Vec3_Scale(m.rightOrbitNormal,m.orbitRadii.x*.18f)),m.orbitRadii,cut);
    Vector3 nasal=m.nostrilRadii;nasal.x+=m.nostrilCurve*(nasal.y*.35f+nasal.x*.60f);nasal.z*=1+m.nostrilCurve*.45f;
    RayBounds_Ellipsoid(&head,m.leftNostrilCenterLocal,nasal,cut);
    RayBounds_Ellipsoid(&head,m.rightNostrilCenterLocal,nasal,cut);
    if(m.hasTympana){RayBounds_Ellipsoid(&head,m.leftTympanumCenterLocal,m.tympanumRadii,cut);RayBounds_Ellipsoid(&head,m.rightTympanumCenterLocal,m.tympanumRadii,cut);}
    AABB3D world=RayBounds_World(&m,head);
    AABB_ExpandPoint(&m.headBounds,world.start);AABB_ExpandPoint(&m.headBounds,world.end);
    AABB3D mouth=AABB_Empty();
    float oral=m.rimBevel+m.entranceToCavitySmoothness*.25f+8.f*tolerance;
    Vector3 cavity=m.cavityRadii;cavity.y+=fabsf(m.oralSlope)*cavity.z;
    RayBounds_Ellipsoid(&mouth,m.cavityCenterLocal,cavity,oral);
    Vector3 ext=m.entranceHalfExtents;
    ext.x=fmaxf(ext.x,ext.y);ext=Vec3_Add(ext,Vec3_Create(oral,oral,oral));
    ext.y+=fabsf(m.oralSlope)*ext.z;
    AABB_ExpandRadius(&mouth,m.entranceCenterLocal,ext);
    world=RayBounds_World(&m,mouth);
    AABB_ExpandPoint(&m.influenceBounds,world.start);AABB_ExpandPoint(&m.influenceBounds,world.end);
    m.visualBounds=RayBounds_PosedJaw(&m,tolerance);
    AABB_Pad(&m.headBounds,2.f*tolerance);AABB_Pad(&m.influenceBounds,2.f*tolerance);AABB_Pad(&m.visualBounds,2.f*tolerance);
    return m;
}
#endif
