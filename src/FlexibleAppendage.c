#include "FlexibleAppendage.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

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
    for (unsigned id = 1; id <= last; ++id) {
        float t = (float)(id-1)/(last-1);
        float radius = slot->scale * Math_Lerp(p->tipRadius, p->baseRadius, powf(1-t,p->taper));
        if (id > 1) {
            float u = t - .5f/(last-1);
            float pitch = p->initialPitch * expf(-7*u) + p->restBend*u;
            position = Vec3_Add(position, Vec3_Scale(Tangent(slot,pitch),step));
        }
        BodyConnectionKind kind = id == 1 ? BODY_CONNECTION_SUPPORT : BODY_CONNECTION_FLEXIBLE_SEGMENT;
        if (id == last && p->terminal == FLEXIBLE_TERMINAL_PAIRED_OPENINGS) kind = BODY_CONNECTION_PAIRED_OPENINGS;
        if (!Section(graph,module,id,position,radius,radius*p->aspect,parent,kind,slot->side)) return false;
        parent = Anatomy_MakeId(module,id);
    }
    if (p->terminal == FLEXIBLE_TERMINAL_PAIRED_OPENINGS) {
        float r = slot->scale*p->tipRadius;
        Vector3 tangent = Tangent(slot,p->initialPitch*expf(-7)+p->restBend);
        Vector3 lipAxis = Vec3_Normalize(Vec3_Cross(tangent,slot->side));
        for (unsigned lip = 0; lip < 2; ++lip) {
            float sign = lip == 0 ? 1 : -1;
            Vector3 base = Vec3_Add(position, Vec3_Scale(lipAxis, sign*r*p->aspect*.72f));
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
