#include "FlexibleAppendage.h"
#include "HeadModule.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

FlexibleAppendagePhenotype FlexibleAppendage_Interpolate(const FlexibleAppendagePhenotype* a,
    const FlexibleAppendagePhenotype* b,float profileWeight,float tipWeight)
{
    FlexibleAppendagePhenotype result=profileWeight<.5f?*a:*b;
    result.length=Math_Lerp(a->length,b->length,profileWeight);
    result.baseRadius=Math_Lerp(a->baseRadius,b->baseRadius,profileWeight);
    result.tipRadius=Math_Lerp(a->tipRadius,b->tipRadius,tipWeight);
    result.aspect=Math_Lerp(a->aspect,b->aspect,tipWeight);
    result.taper=Math_Lerp(a->taper,b->taper,profileWeight);
    result.initialPitch=Math_Lerp(a->initialPitch,b->initialPitch,profileWeight);
    result.restBend=Math_Lerp(a->restBend,b->restBend,profileWeight);
    result.yawAngle=Math_Lerp(a->yawAngle,b->yawAngle,profileWeight);
    result.coilBias=Math_Lerp(a->coilBias,b->coilBias,profileWeight);
    result.suckerScale=Math_Lerp(a->suckerScale,b->suckerScale,tipWeight);
    return result;
}

float FlexibleAppendage_MinNasalLength(const FlexibleAppendagePhenotype* p,float supportHeight) {
    return Math_Max(supportHeight*.95f,p->tipRadius*2.8f);
}

static bool Section(AnatomyGraph* graph, uint32_t module, unsigned id, Vector3 center,
    float width, float height, AnatomyId previous, BodyConnectionKind kind, Vector3 lateral) {
    if (!Module_Node(graph, module, id, center, width, height, 3, ANATOMY_ROLE_FLEXIBLE,
                     ANATOMY_REGION_FLEXIBLE, ANATOMY_SIDE_CENTER, 1) ||
        !Module_Edge(graph, module, id, previous, Anatomy_MakeId(module, id), kind, 1)) return false;
    BodyConnection* edge = &graph->connections[graph->connectionCount-1];
    edge->transverseAxis = lateral;
    edge->jointBlend = .26f;
    return true;
}

static Vector3 Tangent(const AttachmentSlot* slot, float pitch) {
    return Vec3_Add(Vec3_Scale(slot->up, -cosf(pitch)), Vec3_Scale(slot->forward, sinf(pitch)));
}

bool FlexibleAppendage_Resolve(const FlexibleAppendagePhenotype* p, uint32_t module,
    const AttachmentSlot* slot, AnatomyGraph* graph) {
    if (!p || !slot || !graph) return false;
    const unsigned last = FLEXIBLE_APPENDAGE_STATIONS;
    float step = p->length * slot->scale / (last-1);
    Vector3 position = slot->position;
    AnatomyId parent = slot->hostNode;
    bool nasal = HeadModule_IsNasalExtension(slot->id);
    if (p->profile == FLEXIBLE_PROFILE_MUSCULAR_ARM ||
        p->profile == FLEXIBLE_PROFILE_CEPHALIC_LOBE ||
        p->profile == FLEXIBLE_PROFILE_SIPHON) {
        /* Perfiles genéricos de apéndice hidrostático / lóbulo / sifón:
         * parten en dirección forward del anclaje (con giro acimutal yawAngle),
         * conservan raíz muscular bien integrada al anfitrión y curvatura orgánica sin quiebros. */
        float cY = cosf(p->yawAngle), sY = sinf(p->yawAngle);
        Vector3 fwd = Vec3_Normalize(Vec3_Add(Vec3_Scale(slot->forward, cY), Vec3_Scale(slot->side, sY)));
        Vector3 lat = Vec3_Normalize(Vec3_Cross(slot->up, fwd));
        if (Vec3_LengthSq(lat) < 1e-6f) lat = slot->side;
        Vector3 up = Vec3_Normalize(Vec3_Cross(fwd, lat));

        float rootW = p->baseRadius * slot->scale;
        float rootH = rootW * p->aspect;
        float tipW = p->tipRadius * slot->scale;
        float tipH = tipW * p->aspect;
        /* Inserción proximal dentro de la corona anfitriona para evitar cilindros desconectados. */
        Vector3 pos = Vec3_Sub(slot->position, Vec3_Scale(fwd, rootW * (p->profile == FLEXIBLE_PROFILE_SIPHON ? 0.35f : 0.65f)));
        float totalLen = p->length * slot->scale;
        float ds = totalLen / (float)(last - 1);

        for (unsigned id = 1; id <= last; ++id) {
            float t = (float)(id - 1) / (float)(last - 1);
            float taper = powf(fmaxf(0.0f, 1.0f - t), p->taper);
            float w = Math_Lerp(tipW, rootW, taper);
            float h = Math_Lerp(tipH, rootH, taper);
            if (p->profile == FLEXIBLE_PROFILE_CEPHALIC_LOBE) {
                /* Lóbulo cefálico aplanado y ligeramente enrollable hacia el centro oral. */
                float blade = 0.65f + 0.45f * sinf(t * 3.14159265f);
                w *= blade;
                h *= 0.42f;
            }
            if (id > 1) {
                float u = t - 0.5f / (float)(last - 1);
                /* Curvatura continua: initialPitch dirige la salida, restBend arquea en vertical
                 * y coilBias introduce un enrollamiento lateral-distal suave en el tercio final. */
                float pitch = p->initialPitch * (1.0f - 0.55f * u) + p->restBend * u * u;
                float swirl = p->coilBias * u * u * 1.45f;
                Vector3 dir = Vec3_Normalize(Vec3_Add(
                    Vec3_Scale(fwd, cosf(pitch) * cosf(swirl)),
                    Vec3_Add(Vec3_Scale(up, sinf(pitch)),
                             Vec3_Scale(lat, cosf(pitch) * sinf(swirl)))));
                pos = Vec3_Add(pos, Vec3_Scale(dir, ds));
            }
            BodyConnectionKind kind = (id == 1) ? BODY_CONNECTION_SUPPORT : BODY_CONNECTION_FLEXIBLE_SEGMENT;
            if (!Section(graph, module, id, pos, w, h, parent, kind, lat)) return false;
            graph->connections[graph->connectionCount - 1].jointBlend =
                (p->profile == FLEXIBLE_PROFILE_MUSCULAR_ARM) ? 0.62f : 0.34f;
            parent = Anatomy_MakeId(module, id);
        }
        return true;
    }
    float length=p->length*slot->scale;
    float extent=nasal?Math_Clamp01(length/Math_Max(slot->hostRadii.y*2,.001f)):1;
    float rootWidth=nasal?Math_Lerp(slot->hostRadii.x*.68f,p->baseRadius*slot->scale,extent):p->baseRadius*slot->scale;
    float rootHeight=nasal?Math_Lerp(slot->hostRadii.y*.72f,rootWidth*p->aspect,.60f*extent):rootWidth*p->aspect;
    float tipWidth=p->tipRadius*slot->scale;
    float tipHeight=tipWidth*p->aspect;
    if(nasal && extent<1) {
        /* Al acortar el órgano, la sección terminal vuelve gradualmente al
         * planum ancho. Reducir longitud y radio a la vez forma una espiga. */
        float emergence=extent*extent*(3-2*extent);
        tipWidth=Math_Lerp(Math_Min(slot->hostRadii.x*.47f,rootWidth),tipWidth,emergence);
        tipHeight=Math_Lerp(Math_Min(slot->hostRadii.y*.49f,rootHeight),tipHeight,emergence);
    }
    float initialPitch=Math_Lerp(1.49f,p->initialPitch,extent);
    for (unsigned id = 1; id <= last; ++id) {
        float t = (float)(id-1)/(last-1);
        /* El arranque nasal mantiene sección y cambia el radio con tangente
         * continua antes de adoptar el ahusado del resto del órgano. */
        float proximal = Math_Clamp01(t/.18f);
        float taperT = nasal ? t*proximal*(2-proximal) : t;
        float taper=powf(1-taperT,p->taper);
        float width=Math_Lerp(tipWidth,rootWidth,taper);
        float height=Math_Lerp(tipHeight,rootHeight,taper);
        if (id > 1) {
            float u = t - .5f/(last-1);
            float pitch = nasal ?
                Math_Lerp(initialPitch,initialPitch*expf(-7*u)+p->restBend*u,extent) :
                p->initialPitch*expf(-7*u)+p->restBend*u;
            position = Vec3_Add(position, Vec3_Scale(Tangent(slot,pitch),step));
        }
        BodyConnectionKind kind = id == 1 ? BODY_CONNECTION_SUPPORT : BODY_CONNECTION_FLEXIBLE_SEGMENT;
        if (id == last && p->terminal == FLEXIBLE_TERMINAL_PAIRED_OPENINGS) kind = BODY_CONNECTION_PAIRED_OPENINGS;
        if (!Section(graph,module,id,position,width,height,parent,kind,slot->side)) return false;
        parent = Anatomy_MakeId(module,id);
    }
    if (p->terminal == FLEXIBLE_TERMINAL_PAIRED_OPENINGS) {
        float r = nasal ? tipWidth : slot->scale*p->tipRadius;
        float terminalPitch=initialPitch*expf(-7)+p->restBend;
        Vector3 tangent = Tangent(slot,Math_Lerp(initialPitch,terminalPitch,extent));
        Vector3 lipAxis = Vec3_Normalize(Vec3_Cross(tangent,slot->side));
        for (unsigned lip = 0; lip < 2; ++lip) {
            float sign = lip == 0 ? 1 : -1;
            Vector3 base = Vec3_Add(position, Vec3_Scale(lipAxis, sign*tipHeight*.72f));
            Vector3 tip = Vec3_Add(base, Vec3_Scale(tangent,r*(lip==0?.94f:.67f)));
            unsigned id = last+1+lip*2;
            if (!Section(graph,module,id,base,r*.58f,r*.30f,parent,BODY_CONNECTION_SUPPORT,slot->side) ||
                !Section(graph,module,id+1,tip,r*.30f,r*.16f,Anatomy_MakeId(module,id),
                    BODY_CONNECTION_FLEXIBLE_SEGMENT,slot->side)) return false;
        }
    }
    return true;
}

bool FlexibleAppendage_ApplyPose(uint32_t module, FlexibleAppendagePose gesture, float weight,
    const Skeleton* skeleton, SkeletonPose* pose) {
    if (!skeleton || !pose || gesture < 0 || gesture >= FLEXIBLE_POSE_COUNT || !isfinite(weight)) return false;
    int root=Skeleton_FindJoint(skeleton,Anatomy_MakeId(module,1));
    int second=Skeleton_FindJoint(skeleton,Anatomy_MakeId(module,2));
    int middle=Skeleton_FindJoint(skeleton,Anatomy_MakeId(module,FLEXIBLE_APPENDAGE_STATIONS/2));
    if(root<0||second<0||middle<0)return false;
    Vector3 lateral=skeleton->joints[second].constraint.hingeAxis;
    if(Vec3_LengthSq(lateral)<1e-6f)lateral=Vec3_Create(1,0,0);
    Vector3 vertical=Vec3_Normalize(Vec3_Scale(skeleton->joints[middle].restPosition,-1));
    Quaternion previous=Quat_Identity();
    weight=Math_Clamp01(weight);
    if (gesture==FLEXIBLE_POSE_FEEDING || gesture==FLEXIBLE_POSE_CURLED) {
        /* Un ápice ancho y corto no puede volver hacia la boca con el mismo
         * arco que una trompa fina sin enterrar las narinas en la raíz.
         * El primer labio terminal mide la sección incluso durante el morph. */
        int lip=Skeleton_FindJoint(skeleton,Anatomy_MakeId(module,FLEXIBLE_APPENDAGE_STATIONS+1));
        if (lip>=0) {
            float tipHeight=Vec3_Length(skeleton->joints[lip].restPosition)/.72f;
            float reach=Vec3_Length(skeleton->joints[second].restPosition)*(FLEXIBLE_APPENDAGE_STATIONS-1);
            float slenderness=reach/fmaxf(tipHeight,1e-6f);
            float fullReach=gesture==FLEXIBLE_POSE_FEEDING ? 17.0f : 12.0f;
            float clearance=Math_Clamp01((slenderness-4)/(fullReach-4));
            weight*=Math_Lerp(.26f,1,clearance*clearance*(3-2*clearance));
        }
    }
    for (unsigned i=1; i<=FLEXIBLE_APPENDAGE_STATIONS; ++i) {
        int joint=Skeleton_FindJoint(skeleton,Anatomy_MakeId(module,i));
        if(joint<0)return false;
        float t=(float)(i-1)/(FLEXIBLE_APPENDAGE_STATIONS-1);
        float bend=0,steer=0;
        switch (gesture) {
        case FLEXIBLE_POSE_CURLED: bend=-4.0f*t*t*t; break;
        case FLEXIBLE_POSE_REACHING: bend=-.86f*t*(2-t); break;
        case FLEXIBLE_POSE_FEEDING: {
            /* El arco gira fuera del plano sagital antes de volver hacia la
             * boca, dejando paso entre la raíz gruesa y el extremo. */
            float lift=Math_Clamp01((t-.21f)/.64f);
            float span=sinf(t*3.14159265f);
            bend=-4.98f*lift*lift*(3-2*lift);
            steer=.65f*span*span*tanhf((.61f-t)*10);
            break;
        }
        case FLEXIBLE_POSE_SPREADING: {
            bend = 0.42f * t * (1.4f - 0.5f * t);
            steer = 0.25f * sinf(t * 3.14159265f);
            break;
        }
        case FLEXIBLE_POSE_CRAWLING: {
            bend = -1.25f * sinf(t * 3.14159265f) + 0.55f * t;
            steer = 0.58f * sinf(t * 6.2831853f * 0.75f);
            break;
        }
        default: break;
        }
        Quaternion target=Quat_Multiply(Quat_FromAxisAngle(vertical,steer*weight),
            Quat_FromAxisAngle(lateral,bend*weight));
        Quaternion local=Quat_Multiply(Quat_Inverse(previous),target);
        pose->joints[joint].localRotation=Quat_Multiply(skeleton->joints[joint].restRotation,local);
        previous=target;
    }
    return SkeletonPose_UpdateSubtree(pose,skeleton,root);
}
