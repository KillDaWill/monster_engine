#include "Head.h"
#include "MathUtils.h"
#include <math.h>

/* Los centros se trasladan con el cuello; radios y marcos conservan su origen. */
#define LANDMARK_POINTS(X) \
    X(neckAttachment) X(skullCenter) X(leftOrbit) X(rightOrbit) \
    X(leftEyeCenter) X(rightEyeCenter) X(leftJawHinge) X(rightJawHinge) \
    X(muzzleRoot) X(muzzleTip) X(noseTip) X(leftNostril) X(rightNostril) \
    X(leftTympanum) X(rightTympanum) X(oralContactPoint) X(mandibularSymphysis) \
    X(oralCavityCenter) X(leftEarBase) X(rightEarBase) X(upperBeakAnchor) \
    X(lowerBeakAnchor) X(leftMouthCorner) X(rightMouthCorner)
#define SURFACE_POINTS(X) \
    X(craniumCenter) X(neckCollarRoot) X(cranialPostorbitalCenter) X(cranialTemporalCenter) \
    X(cranialOccipitalCenter) X(faceRoot) X(faceMid) X(faceTip) X(leftCheekCenter) \
    X(rightCheekCenter) X(leftTemporalCenter) X(rightTemporalCenter) X(leftMaxillaryCenter) \
    X(rightMaxillaryCenter) X(leftBrowCenter) X(rightBrowCenter) X(leftOrbitCenter) \
    X(rightOrbitCenter) X(leftOrbitRimCenter) X(rightOrbitRimCenter) X(noseCenter) \
    X(leftNostrilCenter) X(rightNostrilCenter) X(leftTympanumCenter) X(rightTympanumCenter) \
    X(leftEarCenter) X(rightEarCenter) X(equineNasalRoot) X(equineNasalTip)
#define SURFACE_RADII(X) \
    X(craniumRadii) X(neckCollarRadii) X(faceRootRadii) X(faceMidRadii) X(faceTipRadii) \
    X(cheekRadii) X(temporalRadii) X(maxillaryRadii) X(browRadii) X(orbitRadii) \
    X(orbitRimRadii) X(noseRadii) X(nostrilRadii) X(tympanumRadii) X(cranialPostorbitalRadii) \
    X(cranialTemporalRadii) X(cranialOccipitalRadii)

static Vector3 Multiply(Vector3 a, Vector3 b)
{
    return Vec3_Create(a.x*b.x,a.y*b.y,a.z*b.z);
}

void HeadAnatomy_CenterProfile(HeadAnatomy *head)
{
    Vector3 origin = head->landmarks.neckAttachment;
#define POINT(field) head->landmarks.field = Vec3_Sub(head->landmarks.field,origin);
    LANDMARK_POINTS(POINT)
#undef POINT
#define POINT(field) head->surface.field = Vec3_Sub(head->surface.field,origin);
    SURFACE_POINTS(POINT)
#undef POINT
    for (size_t i=0; i<6; ++i) {
        head->surface.cranialCenters[i] = Vec3_Sub(head->surface.cranialCenters[i],origin);
        head->surface.mandibularCenters[i] = Vec3_Sub(head->surface.mandibularCenters[i],origin);
    }
    /* Las primeras componentes de estos parámetros expresan amplitudes. */
    head->surface.equineNasalRoof.z -= origin.z;
    head->surface.equineCrestStart.y -= origin.y;
    head->surface.equineCrestStart.z -= origin.z;
    head->surface.equineCrestEnd.y -= origin.y;
    head->surface.equineCrestEnd.z -= origin.z;
    head->oralSystem.jawPivot = Vec3_Sub(head->oralSystem.jawPivot,origin);
    head->landmarks.headId = 0;
    head->attachmentBodyPartIndex = head->oralSystem.bodyPartIndex = 0;
}

void HeadAnatomy_ScaleProfile(HeadAnatomy *head, Vector3 scale)
{
    HeadSurfaceRecipe *s = &head->surface;
    HeadLandmarks *l = &head->landmarks;
    Mouth *m = &head->oralSystem;
    float mean = cbrtf(scale.x*scale.y*scale.z);
    Vector3 inverse = {1/scale.x,1/scale.y,1/scale.z};
#define POINT(field) l->field = Multiply(l->field,scale);
    LANDMARK_POINTS(POINT)
#undef POINT
#define METRIC(field) s->field = Multiply(s->field,scale);
    SURFACE_POINTS(METRIC)
    SURFACE_RADII(METRIC)
#undef METRIC
    for (size_t i=0; i<6; ++i) {
        s->cranialCenters[i] = Multiply(s->cranialCenters[i],scale);
        s->cranialRadii[i] = Multiply(s->cranialRadii[i],scale);
        s->mandibularCenters[i] = Multiply(s->mandibularCenters[i],scale);
        s->mandibularRadii[i] = Multiply(s->mandibularRadii[i],scale);
    }
    s->leftOrbitNormal = l->leftOrbitNormal = Vec3_Normalize(Multiply(l->leftOrbitNormal,inverse));
    s->rightOrbitNormal = l->rightOrbitNormal = Vec3_Normalize(Multiply(l->rightOrbitNormal,inverse));
    s->orbitSocketDepth *= mean;
    s->tympanumDepth *= mean;
    s->faceCapDepth *= scale.z;
    s->faceRounding *= mean;
    s->equineNasalRoof.z *= scale.z;
    s->equineCrestStart.y *= scale.y;
    s->equineCrestStart.z *= scale.z;
    s->equineCrestEnd.y *= scale.y;
    s->equineCrestEnd.z *= scale.z;
    s->oralSlope *= scale.y/scale.z;
    s->unionSmoothness *= mean;
    s->headBodySmoothness *= mean;
    if (s->hasEars) {
        Vector3 earScale = {Vec3_Length(Multiply(s->leftEarSide,scale)),
            Vec3_Length(Multiply(s->earDirection,scale)),
            Vec3_Length(Multiply(s->leftEarNormal,scale))};
        s->earShape = Multiply(s->earShape,earScale);
        s->earLongitudinalCurve *= earScale.z;
        s->earFold *= earScale.z;
        HeadSurfaceRecipe_SetEarPose(s,false,l->leftEarBase,Multiply(s->earDirection,scale),
            Multiply(s->leftEarNormal,inverse));
        HeadSurfaceRecipe_SetEarPose(s,true,l->rightEarBase,Multiply(s->rightEarDirection,scale),
            Multiply(s->rightEarNormal,inverse));
    }
    head->eyeScale = Multiply(head->eyeScale,scale);
    m->offset = Multiply(m->offset,scale);
    m->jawPivot = Multiply(m->jawPivot,scale);
    m->scale = Multiply(m->scale,scale);
    m->slitThickness *= scale.y;
    m->cornerRadius *= mean;
    m->jawLength *= scale.z;
    m->jawWidth *= scale.x;
    m->jawThickness *= scale.y;
    m->jawRearMass *= scale.y;
    m->jawMuscle *= scale.y;
    m->hingeRadius *= mean;
    m->throatRadius *= mean;
}

static bool FinitePoint(Vector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool Section(Vector3 v)
{
    return FinitePoint(v) && v.x>0 && v.y>0 && v.z>0;
}

static bool ProfileSection(Vector3 v)
{
    return FinitePoint(v) && v.x>0 && v.y>0 && v.z>=0;
}

bool HeadAnatomy_ValidateProfile(const HeadAnatomy *head)
{
    const HeadSurfaceRecipe *s=&head->surface;
    const HeadLandmarks *l=&head->landmarks;
    uint32_t errors=HeadAnatomy_Validate(head);
    if (!Head_Supports(head->archetype) ||
        (errors & (HEAD_INVALID_FINITE|HEAD_INVALID_SYMMETRY|HEAD_INVALID_ORDER|HEAD_INVALID_NECK)) ||
        !Section(s->craniumRadii) || !Section(head->eyeScale) || !s->hasCranialProfile ||
        !s->hasMandibularProfile || Vec3_LengthSq(l->neckAttachment)>1e-8f) return false;
#define POINT(field) if (!FinitePoint(l->field)) return false;
    LANDMARK_POINTS(POINT)
#undef POINT
#define METRIC(field) if (!FinitePoint(s->field)) return false;
    SURFACE_POINTS(METRIC)
    SURFACE_RADII(METRIC)
#undef METRIC
    for (size_t i=0; i<6; ++i) {
        if (!FinitePoint(s->cranialCenters[i]) || !FinitePoint(s->mandibularCenters[i]) ||
            !ProfileSection(s->cranialRadii[i]) || !ProfileSection(s->mandibularRadii[i])) return false;
        if (i && (s->cranialCenters[i-1].z<=s->cranialCenters[i].z ||
                  s->mandibularCenters[i-1].z<=s->mandibularCenters[i].z)) return false;
    }
    if (s->hasEars) {
        if (!Section(s->earShape) || !Section(s->earRadii) ||
            fabsf(Vec3_LengthSq(s->earDirection)-1)>.002f ||
            fabsf(Vec3_LengthSq(s->rightEarDirection)-1)>.002f ||
            fabsf(Vec3_Dot(s->earDirection,s->leftEarNormal))>.002f ||
            fabsf(Vec3_Dot(s->rightEarDirection,s->rightEarNormal))>.002f) return false;
    }
    const Mouth *mouth=&head->oralSystem;
    return Section(mouth->scale) && FinitePoint(mouth->jawPivot) &&
        mouth->jawLength>0 && mouth->jawThickness>0;
}

static float Gain(float requested,float reference,float slope)
{
    return 1+slope*(Math_Clamp01(requested)-Math_Clamp01(reference));
}

void HeadAnatomy_EditProfile(HeadAnatomy *head, const HeadPhenotype *p, const HeadRegionalControls *ref)
{
    float skull=Gain(p->skullWidth,ref->skullWidth,.40f);
    float length=Gain(p->muzzleLength,ref->muzzleLength,.55f);
    float jaw=Gain(p->jawDepth,ref->jawDepth,.42f);
    float ear=Gain(p->earSize,ref->earSize,.65f);
    float eye=Gain(p->eyeSize,ref->eyeSize,.48f);
    if (skull!=1) HeadAnatomy_ScaleProfile(head,Vec3_Create(skull,1,1));
    HeadSurfaceRecipe *s=&head->surface;
    HeadLandmarks *l=&head->landmarks;
    Mouth *m=&head->oralSystem;
    if (length!=1) {
        /* Estirar el rostro desde la unión facial conserva el asiento
         * de las órbitas, la nuca y el cóndilo posterior. */
        float root=s->faceRoot.z;
#define STRETCH(z) (z) += fmaxf(0,(z)-root)*(length-1)
#define FACE(field) STRETCH(s->field.z)
        FACE(faceMid); FACE(faceTip); FACE(noseCenter); FACE(leftNostrilCenter); FACE(rightNostrilCenter);
        FACE(equineNasalRoot); FACE(equineNasalTip); FACE(equineNasalRoof); FACE(equineCrestStart); FACE(equineCrestEnd);
#undef FACE
#define LANDMARK(field) STRETCH(l->field.z)
        LANDMARK(muzzleRoot); LANDMARK(muzzleTip); LANDMARK(noseTip); LANDMARK(leftNostril); LANDMARK(rightNostril);
        LANDMARK(leftMouthCorner); LANDMARK(rightMouthCorner); LANDMARK(oralContactPoint); LANDMARK(oralCavityCenter);
        LANDMARK(upperBeakAnchor); LANDMARK(lowerBeakAnchor); LANDMARK(mandibularSymphysis);
#undef LANDMARK
        for (unsigned i=0; i<6; ++i) {
            STRETCH(s->cranialCenters[i].z);
            STRETCH(s->mandibularCenters[i].z);
        }
#undef STRETCH
        s->faceCapDepth*=length;
        m->jawLength=l->mandibularSymphysis.z-m->jawPivot.z;
    }
    if (jaw!=1) {
        for (size_t i=0; i<6; ++i) {
            float depth=s->mandibularRadii[i].y*(jaw-1);
            s->mandibularRadii[i].y+=depth;
            s->mandibularCenters[i].y-=depth*.70f;
        }
        m->jawThickness*=jaw;
    }
    if (ear!=1 && s->hasEars) {
        s->earShape=Vec3_Scale(s->earShape,ear);
        s->earFold*=ear;
        s->earLongitudinalCurve*=ear;
        HeadSurfaceRecipe_SetEarPose(s,false,l->leftEarBase,s->earDirection,s->leftEarNormal);
        HeadSurfaceRecipe_SetEarPose(s,true,l->rightEarBase,s->rightEarDirection,s->rightEarNormal);
    }
    head->eyeScale=Vec3_Scale(head->eyeScale,eye);
    s->orbitRadii=Vec3_Scale(s->orbitRadii,eye);
    s->orbitRimRadii=Vec3_Scale(s->orbitRimRadii,eye);
    s->orbitSocketDepth*=eye;
}
