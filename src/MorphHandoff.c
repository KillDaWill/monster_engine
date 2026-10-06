/** @file MorphHandoff.c
 * @brief Implementación pura de interfaces de cesión anatómica entre malla y SDF.
 */
#include "MorphHandoff.h"
#include <math.h>
#include <string.h>
#include "Quaternion.h"

/* Envolvente convexa de un módulo anatómico. Cubre también las combinaciones
 * ponderadas del anclaje; unir solo cápsulas de cada dedo deja fuera vértices
 * interpolados entre ramas. Una cápsula por representación acota el coste GPU. */
static void CollapseSupport(MorphHandoffInterface* h,size_t first) {
    if(h->segmentCount<=first+1)return;
    Vector3 a=h->origin,b=a;
    float farthest=0;
    for(size_t i=first;i<h->segmentCount;++i) {
        const MorphHandoffSegment* s=&h->segments[i];
        if(Vec3_LengthSq(Vec3_Sub(s->a,a))>farthest) {
            b=s->a;farthest=Vec3_LengthSq(Vec3_Sub(b,a));
        }
        if(Vec3_LengthSq(Vec3_Sub(s->b,a))>farthest) {
            b=s->b;farthest=Vec3_LengthSq(Vec3_Sub(b,a));
        }
    }
    Vector3 ab=Vec3_Sub(b,a);float radius=0;
    for(size_t i=first;i<h->segmentCount;++i) {
        const MorphHandoffSegment* s=&h->segments[i];
        Vector3 points[2]={s->a,s->b};
        for(int k=0;k<2;++k) {
            float t=fminf(1,fmaxf(0,Vec3_Dot(Vec3_Sub(points[k],a),ab)/fmaxf(farthest,1e-12f)));
            radius=fmaxf(radius,s->radius+Vec3_Distance(points[k],Vec3_Add(a,Vec3_Scale(ab,t))));
        }
    }
    h->segments[first]=(MorphHandoffSegment){a,b,radius};
    h->segmentCount=first+1;
}

void MorphHandoffSet_Init(MorphHandoffSet* set) {
    if(!set) return;
    memset(set, 0, sizeof(*set));
}

size_t MorphHandoff_BuildFromGraph(const AnatomyGraph* graph,
    const uint32_t* fallbackModuleIds, size_t fallbackModuleCount,
    MorphHandoffSet* set) {
    if(!set) return 0;
    MorphHandoffSet_Init(set);
    if(!graph || !fallbackModuleIds || !fallbackModuleCount) return 0;

    for(size_t m = 0; m < fallbackModuleCount && set->count < MORPH_HANDOFF_MAX_INTERFACES; ++m) {
        uint32_t moduleId = fallbackModuleIds[m];
        if(!moduleId) continue;

        /* La raíz es la entrada semántica desde otro módulo, no un índice local. */
        const AnatomyNode* rootNode=NULL;
        const AnatomyNode* nextNode=NULL;
        for(size_t c=0;c<graph->connectionCount;++c) {
            const BodyConnection* e=&graph->connections[c];
            const AnatomyNode* a=AnatomyGraph_FindNode(graph,e->fromId);
            const AnatomyNode* b=AnatomyGraph_FindNode(graph,e->toId);
            if(a&&b&&b->moduleInstanceId==moduleId&&a->moduleInstanceId!=moduleId) {
                rootNode=b;break;
            }
        }
        if(!rootNode)rootNode=AnatomyGraph_FindModuleNode(graph,moduleId,1);
        if(!rootNode)continue;
        for(size_t c=0;c<graph->connectionCount;++c) {
            const BodyConnection* e=&graph->connections[c];
            if(e->fromId==rootNode->id&&e->moduleInstanceId==moduleId&&
               !graph->dormantConnections[c]) {
                nextNode=AnatomyGraph_FindNode(graph,e->toId);break;
            }
        }

        /* 2. Determinar normal saliente (eje proximal -> distal) */
        Vector3 normal = Vec3_Create(0.0f, -1.0f, 0.0f);
        if(nextNode) {
            Vector3 diff = Vec3_Sub(nextNode->center, rootNode->center);
            float len = Vec3_Length(diff);
            if(len > 1e-4f) {
                normal = Vec3_Scale(diff, 1.0f / len);
            }
        } else {
            /* Buscar conexión entrante desde el cuerpo */
            for(size_t c = 0; c < graph->connectionCount; ++c) {
                const BodyConnection* conn = &graph->connections[c];
                if(conn->toId == rootNode->id) {
                    const AnatomyNode* parent = AnatomyGraph_FindNode(graph, conn->fromId);
                    if(parent) {
                        Vector3 diff = Vec3_Sub(rootNode->center, parent->center);
                        float len = Vec3_Length(diff);
                        if(len > 1e-4f) {
                            normal = Vec3_Scale(diff, 1.0f / len);
                            break;
                        }
                    }
                }
            }
        }

        /* 3. Determinar radio de la sección */
        float radius = fmaxf(rootNode->widthRadius, rootNode->heightRadius);
        if(radius < 0.04f) {
            radius = fmaxf(radius, fmaxf(rootNode->envelopeRadii.x, rootNode->envelopeRadii.y));
        }
        if(radius < 0.04f) radius = 0.08f;

        /* 4. Registrar interfaz configurada */
        MorphHandoffInterface* h = &set->interfaces[set->count++];
        h->moduleId = moduleId;
        h->chunkId = (uint32_t)m;
        h->origin = rootNode->center;
        h->normal = normal;
        h->radius = radius;
        h->hardClipOffset = 0.0f;
        h->blendWidth = fmaxf(radius*.15f, .006f);
        h->transitionFactor = 1.0f;
        h->active = true;
        for(size_t c=0;c<graph->connectionCount;++c) {
            const BodyConnection* e=&graph->connections[c];
            if(e->moduleInstanceId!=moduleId||graph->dormantConnections[c])continue;
            const AnatomyNode* a=AnatomyGraph_FindNode(graph,e->fromId);
            const AnatomyNode* b=AnatomyGraph_FindNode(graph,e->toId);
            if(!a||!b||a->moduleInstanceId!=moduleId)continue;
            if(h->segmentCount==MORPH_HANDOFF_MAX_SEGMENTS) {h->active=false;break;}
            float r=fmaxf(fmaxf(a->widthRadius,a->heightRadius),
                          fmaxf(b->widthRadius,b->heightRadius));
            r+=fmaxf(e->widthBulge,e->heightBulge)+2*h->blendWidth;
            h->segments[h->segmentCount++]=(MorphHandoffSegment){a->center,b->center,r};
        }
    }

    return set->count;
}

float MorphHandoff_EvaluateDomainDistance(const MorphHandoffInterface* handoff, Vector3 point) {
    if(!handoff || !handoff->active) return 0.0f;
    Vector3 diff=Vec3_Sub(point,handoff->origin);
    float longitudinal=Vec3_Dot(diff,handoff->normal)-handoff->hardClipOffset;
    if(!handoff->segmentCount) {
        Vector3 radial=Vec3_Sub(diff,Vec3_Scale(handoff->normal,Vec3_Dot(diff,handoff->normal)));
        return fminf(longitudinal,handoff->radius*1.8f-Vec3_Length(radial));
    }
    float support=-1e6f;
    for(size_t i=0;i<handoff->segmentCount;++i) {
        const MorphHandoffSegment* s=&handoff->segments[i];
        Vector3 ab=Vec3_Sub(s->b,s->a);
        float t=fminf(1,fmaxf(0,Vec3_Dot(Vec3_Sub(point,s->a),ab)/fmaxf(Vec3_LengthSq(ab),1e-12f)));
        support=fmaxf(support,s->radius-Vec3_Distance(point,Vec3_Add(s->a,Vec3_Scale(ab,t))));
    }
    /* El plano solo actúa cerca de la inserción; una cola curvada puede volver
     * al lado proximal sin quedar seccionada por un plano infinito. */
    float proximal=fmaxf(longitudinal,Vec3_Length(diff)-2*handoff->radius);
    return fminf(support,proximal);
}

void MorphHandoff_GetMeshClipPlane(const MorphHandoffInterface* handoff, float outPlane[4]) {
    if(!handoff || !outPlane) return;
    /* Queremos que n.p + d >= 0 conserve los fragmentos en el lado del cuerpo.
     * d_parallel = dot(p - origin, normal) - hardClipOffset.
     * Conservar si d_parallel <= blendWidth <=> -d_parallel + blendWidth >= 0.
     * -dot(p, normal) + dot(origin, normal) + hardClipOffset + blendWidth >= 0.
     */
    outPlane[0] = -handoff->normal.x;
    outPlane[1] = -handoff->normal.y;
    outPlane[2] = -handoff->normal.z;
    outPlane[3] = Vec3_Dot(handoff->origin, handoff->normal) +
                  handoff->hardClipOffset + handoff->blendWidth;
}

float MorphHandoff_EvaluateBlend(const MorphHandoffSet* set, Vector3 point) {
    if(!set || !set->count) return 0.0f;
    float maxBlend = 0.0f;

    for(size_t i = 0; i < set->count; ++i) {
        const MorphHandoffInterface* h = &set->interfaces[i];
        if(!h->active) continue;

        float dLong=MorphHandoff_EvaluateDomainDistance(h,point);
        float bw = fmaxf(h->blendWidth, 0.01f);
        float t = (dLong + bw) / (2.0f * bw);
        t = fminf(fmaxf(t, 0.0f), 1.0f);
        float s = t * t * (3.0f - 2.0f * t); /* smoothstep cúbico */

        float interfaceBlend = s * h->transitionFactor;
        if(interfaceBlend > maxBlend) maxBlend = interfaceBlend;
    }

    return maxBlend;
}

AABB3D MorphHandoff_WorkBounds(const MorphHandoffInterface* h) {
    AABB3D box=AABB_Empty();
    if(!h)return box;
    for(size_t i=0;i<h->segmentCount;++i) {
        const MorphHandoffSegment* s=&h->segments[i];
        Vector3 r=Vec3_Create(s->radius,s->radius,s->radius);
        AABB_ExpandRadius(&box,s->a,r);AABB_ExpandRadius(&box,s->b,r);
    }
    AABB_Pad(&box,h->blendWidth+.004f);
    return box;
}

bool MorphHandoff_AddReference(MorphHandoffInterface* h,const AnatomyGraph* ref,const AnatomyGraph* current) {
    if(!h||!ref||!current)return false;
    size_t first=h->segmentCount;
    for(size_t c=0;c<ref->connectionCount;++c) {
        const BodyConnection* e=&ref->connections[c];
        if(e->moduleInstanceId!=h->moduleId||ref->dormantConnections[c])continue;
        const AnatomyNode* a=AnatomyGraph_FindNode(ref,e->fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(ref,e->toId);
        const AnatomyNode* na=AnatomyGraph_FindNode(current,e->fromId);
        const AnatomyNode* nb=AnatomyGraph_FindNode(current,e->toId);
        if(!a||!b||!na||!nb||a->moduleInstanceId!=h->moduleId)continue;
        if(h->segmentCount==MORPH_HANDOFF_MAX_SEGMENTS)return false;
        Quaternion q=Quat_FromTo(Vec3_Sub(b->center,a->center),Vec3_Sub(nb->center,na->center));
        Vector3 end=Vec3_Add(na->center,Quat_RotateVector(q,Vec3_Sub(b->center,a->center)));
        float r=fmaxf(fmaxf(a->widthRadius,a->heightRadius),fmaxf(b->widthRadius,b->heightRadius));
        r+=fmaxf(e->widthBulge,e->heightBulge)+2*h->blendWidth;
        h->segments[h->segmentCount++]=(MorphHandoffSegment){na->center,end,r};
    }
    CollapseSupport(h,first);
    return true;
}

float MorphHandoff_EvaluateDistance(const MorphHandoffInterface* h,Vector3 p) {
    return h&&h->active?Vec3_Dot(Vec3_Sub(p,h->origin),h->normal)-h->hardClipOffset:0;
}
