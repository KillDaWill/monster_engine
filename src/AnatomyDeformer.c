/**
 * @file AnatomyDeformer.c
 * @brief Deformación anatómica genérica; puente morfológico y pose esquelética.
 * @author Monster Engine Team
 * @date 2026
 */

#include "AnatomyDeformer.h"
#include "MathUtils.h"
#include "AABB.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/**
 * @struct ConnTransform
 * @brief Transformación rígida y de escala de un segmento anatómico.
 */
typedef struct ConnTransform {
    Vector3 newA;
    Vector3 newDir;
    float radScaleA;
    float radScaleB;
    Vector3 rotRow[3];
    bool hasRotation;
} ConnTransform;

AnatomyDeformer* AnatomyDeformer_Create(void) {
    AnatomyDeformer* morph = (AnatomyDeformer*)calloc(1, sizeof(AnatomyDeformer));
    return morph;
}

void AnatomyDeformer_Free(AnatomyDeformer* morph) {
    if (!morph) return;
    free(morph->skinBindings);
    free(morph->bindings);
    free(morph->basePositions);
    free(morph->baseNormals);
    free(morph);
}

bool AnatomyDeformer_Copy(AnatomyDeformer* dst, const AnatomyDeformer* src) {
    if (!dst || !src) return false;
    dst->refGraph = src->refGraph;
    dst->vertexCount = src->vertexCount;
    if (dst->vertexCapacity < src->vertexCount) {
        free(dst->bindings);
        free(dst->basePositions);
        free(dst->baseNormals);
        dst->bindings = (AnatomyDeformerBinding*)malloc(src->vertexCount * sizeof(AnatomyDeformerBinding));
        dst->basePositions = (Vector3*)malloc(src->vertexCount * sizeof(Vector3));
        dst->baseNormals = (Vector3*)malloc(src->vertexCount * sizeof(Vector3));
        dst->vertexCapacity = src->vertexCount;
    }
    if (src->vertexCount > 0 && dst->bindings && dst->basePositions && dst->baseNormals) {
        memcpy(dst->bindings, src->bindings, src->vertexCount * sizeof(AnatomyDeformerBinding));
        memcpy(dst->basePositions, src->basePositions, src->vertexCount * sizeof(Vector3));
        memcpy(dst->baseNormals, src->baseNormals, src->vertexCount * sizeof(Vector3));
    }
    if (src->skinBindings && src->vertexCount > 0) {
        if (!dst->skinBindings) dst->skinBindings = (AnatomySkinBinding*)malloc(src->vertexCount * sizeof(AnatomySkinBinding));
        if (dst->skinBindings) memcpy(dst->skinBindings, src->skinBindings, src->vertexCount * sizeof(AnatomySkinBinding));
    } else {
        free(dst->skinBindings);
        dst->skinBindings = NULL;
    }
    dst->bindPose = src->bindPose;
    dst->poseBound = src->poseBound;
    dst->isBound = src->isBound;
    return true;
}

bool AnatomyDeformer_IsBound(const AnatomyDeformer* morph) {
    return morph && morph->isBound && morph->vertexCount > 0 && morph->bindings != NULL;
}

size_t AnatomyDeformer_GetVertexCount(const AnatomyDeformer* morph) {
    return (morph && morph->isBound) ? morph->vertexCount : 0;
}

bool AnatomyDeformer_Bind(AnatomyDeformer* morph, const Mesh* baseMesh, const AnatomyGraph* refGraph) {
    if (!morph || !baseMesh || !refGraph) return false;
    if (baseMesh->vertexCount == 0 || !baseMesh->vertices || refGraph->connectionCount == 0) return false;

    if (!AnatomyGraph_Validate(refGraph)) return false;
    morph->poseBound = false;
    size_t count = baseMesh->vertexCount;
    if (count > morph->vertexCapacity) {
        free(morph->bindings);
        free(morph->basePositions);
        free(morph->baseNormals);

        morph->bindings = (AnatomyDeformerBinding*)malloc(count * sizeof(AnatomyDeformerBinding));
        morph->basePositions = (Vector3*)malloc(count * sizeof(Vector3));
        morph->baseNormals = (Vector3*)malloc(count * sizeof(Vector3));
        if (!morph->bindings || !morph->basePositions || !morph->baseNormals) {
            free(morph->bindings);
            free(morph->basePositions);
            free(morph->baseNormals);
            morph->bindings = NULL;
            morph->basePositions = NULL;
            morph->baseNormals = NULL;
            morph->vertexCapacity = 0;
            morph->vertexCount = 0;
            morph->isBound = false;
            return false;
        }
        morph->vertexCapacity = count;
    }

    morph->refGraph = *refGraph;
    morph->vertexCount = count;

    /* 1. Precomputar datos y AABB de cada conexión anatómica para acelerar la vinculación */
    typedef struct PrecomputedConn {
        AABB3D box;
        Vector3 a;
        Vector3 ab;
        float invAbLenSq;
        float radA;
        float radB;
        bool valid;
    } PrecomputedConn;
    PrecomputedConn conns[ANATOMY_MAX_CONNECTIONS];
    for (size_t c = 0; c < refGraph->connectionCount; ++c) {
        const BodyConnection* conn = &refGraph->connections[c];
        const AnatomyNode* nodeA = AnatomyGraph_FindNode(refGraph, conn->fromId);
        const AnatomyNode* nodeB = AnatomyGraph_FindNode(refGraph, conn->toId);
        if (!nodeA || !nodeB || refGraph->dormantConnections[c]) {
            conns[c].valid = false;
            continue;
        }
        conns[c].valid = true;
        float pad = fmaxf(nodeA->widthRadius, fmaxf(nodeA->heightRadius,
                    fmaxf(nodeB->widthRadius, nodeB->heightRadius))) * 2.2f;
        conns[c].box.start.x = fminf(nodeA->center.x, nodeB->center.x) - pad;
        conns[c].box.end.x   = fmaxf(nodeA->center.x, nodeB->center.x) + pad;
        conns[c].box.start.y = fminf(nodeA->center.y, nodeB->center.y) - pad;
        conns[c].box.end.y   = fmaxf(nodeA->center.y, nodeB->center.y) + pad;
        conns[c].box.start.z = fminf(nodeA->center.z, nodeB->center.z) - pad;
        conns[c].box.end.z   = fmaxf(nodeA->center.z, nodeB->center.z) + pad;
        conns[c].a = nodeA->center;
        conns[c].ab = Vec3_Sub(nodeB->center, nodeA->center);
        float abLenSq = Vec3_LengthSq(conns[c].ab);
        conns[c].invAbLenSq = abLenSq > 1e-8f ? 1.0f / abLenSq : 0.0f;
        conns[c].radA = (nodeA->widthRadius + nodeA->heightRadius) * 0.5f;
        conns[c].radB = (nodeB->widthRadius + nodeB->heightRadius) * 0.5f;
    }

    /* 2. Vincular cada vértice a los 2 mejores segmentos anatómicos */
    for (size_t v = 0; v < count; ++v) {
        Vector3 pos = baseMesh->vertices[v].position;
        morph->basePositions[v] = pos;
        morph->baseNormals[v] = baseMesh->vertices[v].normal;

        float bestDist[2] = {1e9f, 1e9f};
        uint16_t bestIdx[2] = {0, 0};
        float bestT[2] = {0.0f, 0.0f};
        Vector3 bestProj[2] = {Vec3_Zero(), Vec3_Zero()};
        float bestRad[2] = {1.0f, 1.0f};

        for (size_t c = 0; c < refGraph->connectionCount; ++c) {
            const PrecomputedConn* pc = &conns[c];
            if (!pc->valid) continue;

            /* Descarte por AABB */
            if (pos.x < pc->box.start.x || pos.x > pc->box.end.x ||
                pos.y < pc->box.start.y || pos.y > pc->box.end.y ||
                pos.z < pc->box.start.z || pos.z > pc->box.end.z) {
                continue;
            }

            float t = 0.0f;
            if (pc->invAbLenSq > 0.0f) {
                t = Vec3_Dot(Vec3_Sub(pos, pc->a), pc->ab) * pc->invAbLenSq;
                if (t < 0.0f) t = 0.0f;
                else if (t > 1.0f) t = 1.0f;
            }
            Vector3 proj = Vec3_Add(pc->a, Vec3_Scale(pc->ab, t));
            float rad = (1.0f - t) * pc->radA + t * pc->radB;
            if (rad < 1e-4f) rad = 1e-4f;

            float distNorm = Vec3_Distance(pos, proj) / rad;
            if (distNorm < bestDist[0]) {
                bestDist[1] = bestDist[0];
                bestIdx[1] = bestIdx[0];
                bestT[1] = bestT[0];
                bestProj[1] = bestProj[0];
                bestRad[1] = bestRad[0];

                bestDist[0] = distNorm;
                bestIdx[0] = (uint16_t)c;
                bestT[0] = t;
                bestProj[0] = proj;
                bestRad[0] = rad;
            } else if (distNorm < bestDist[1]) {
                bestDist[1] = distNorm;
                bestIdx[1] = (uint16_t)c;
                bestT[1] = t;
                bestProj[1] = proj;
                bestRad[1] = rad;
            }
        }

        /* Si ningún AABB coincidió (caso excepcional en márgenes), buscar en todos */
        if (bestDist[0] >= 1e8f) {
            for (size_t c = 0; c < refGraph->connectionCount; ++c) {
                const PrecomputedConn* pc = &conns[c];
                if (!pc->valid) continue;

                float t = 0.0f;
                if (pc->invAbLenSq > 0.0f) {
                    t = Vec3_Dot(Vec3_Sub(pos, pc->a), pc->ab) * pc->invAbLenSq;
                    if (t < 0.0f) t = 0.0f;
                    else if (t > 1.0f) t = 1.0f;
                }
                Vector3 proj = Vec3_Add(pc->a, Vec3_Scale(pc->ab, t));
                float rad = (1.0f - t) * pc->radA + t * pc->radB;
                if (rad < 1e-4f) rad = 1e-4f;

                float distNorm = Vec3_Distance(pos, proj) / rad;
                if (distNorm < bestDist[0]) {
                    bestDist[1] = bestDist[0];
                    bestIdx[1] = bestIdx[0];
                    bestT[1] = bestT[0];
                    bestProj[1] = bestProj[0];
                    bestRad[1] = bestRad[0];

                    bestDist[0] = distNorm;
                    bestIdx[0] = (uint16_t)c;
                    bestT[0] = t;
                    bestProj[0] = proj;
                    bestRad[0] = rad;
                } else if (distNorm < bestDist[1]) {
                    bestDist[1] = distNorm;
                    bestIdx[1] = (uint16_t)c;
                    bestT[1] = t;
                    bestProj[1] = proj;
                    bestRad[1] = rad;
                }
            }
        }

        AnatomyDeformerBinding* b = &morph->bindings[v];
        b->connIndex[0] = bestIdx[0];
        b->connIndex[1] = bestIdx[1];
        b->projT[0] = bestT[0];
        b->projT[1] = bestT[1];
        b->refRadius[0] = bestRad[0];
        b->refRadius[1] = bestRad[1];
        b->localOffset[0] = Vec3_Sub(pos, bestProj[0]);
        b->localOffset[1] = Vec3_Sub(pos, bestProj[1]);

        float w0 = 1.0f / (powf(bestDist[0], 4.0f) + 1e-4f);
        float w1 = 1.0f / (powf(bestDist[1], 4.0f) + 1e-4f);
        if (bestDist[0] > 0.0f && bestDist[1] / bestDist[0] > 2.5f) {
            w1 = 0.0f;
        }
        float sumW = w0 + w1;
        b->weight[0] = w0 / sumW;
        b->weight[1] = w1 / sumW;
    }

    morph->isBound = true;
    return true;
}

bool AnatomyDeformer_Deform(const AnatomyDeformer* morph, const AnatomyGraph* newGraph, Mesh* targetMesh) {
    if (!morph || !morph->isBound || !newGraph || !targetMesh) return false;
    if (targetMesh->vertexCount != morph->vertexCount || !targetMesh->vertices) return false;

    if (!AnatomyGraph_Validate(newGraph)||!AnatomyGraph_TopologyCompatible(&morph->refGraph,newGraph)) return false;
    for(size_t c=0;c<morph->refGraph.connectionCount;++c) {
        const BodyConnection* edge=&morph->refGraph.connections[c];
        if(!AnatomyGraph_FindNode(newGraph,edge->fromId) || !AnatomyGraph_FindNode(newGraph,edge->toId))return false;
    }
    /* 1. Precomputar transformaciones para cada conexión */
    ConnTransform xforms[ANATOMY_MAX_CONNECTIONS];
    for (size_t c = 0; c < morph->refGraph.connectionCount; ++c) {
        const BodyConnection* connRef = &morph->refGraph.connections[c];
        const BodyConnection* connNew = connRef;
        const AnatomyNode* refA = AnatomyGraph_FindNode(&morph->refGraph, connRef->fromId);
        const AnatomyNode* refB = AnatomyGraph_FindNode(&morph->refGraph, connRef->toId);
        const AnatomyNode* newA = AnatomyGraph_FindNode(newGraph, connNew->fromId);
        const AnatomyNode* newB = AnatomyGraph_FindNode(newGraph, connNew->toId);

        ConnTransform* x = &xforms[c];
        memset(x, 0, sizeof(*x));
        if (!refA || !refB || !newA || !newB) continue;

        x->newA = newA->center;
        x->newDir = Vec3_Sub(newB->center, newA->center);

        float refRadA = (refA->widthRadius + refA->heightRadius) * 0.5f;
        float refRadB = (refB->widthRadius + refB->heightRadius) * 0.5f;
        float newRadA = (newA->widthRadius + newA->heightRadius) * 0.5f;
        float newRadB = (newB->widthRadius + newB->heightRadius) * 0.5f;

        float ratioA = refRadA > 1e-4f ? newRadA / refRadA : 1.0f;
        float ratioB = refRadB > 1e-4f ? newRadB / refRadB : 1.0f;
        x->radScaleA = Math_Clamp(ratioA, 0.0f, 4.0f);
        x->radScaleB = Math_Clamp(ratioB, 0.0f, 4.0f);

        Vector3 refDir = Vec3_Sub(refB->center, refA->center);
        float refLen = Vec3_Length(refDir);
        float newLen = Vec3_Length(x->newDir);

        if (refLen > 1e-5f && newLen > 1e-6f) {
            Quaternion q=Quat_FromTo(refDir,x->newDir);
            if (newLen < 0.005f) {
                float blend = (newLen - 1e-6f) / (0.005f - 1e-6f);
                blend = blend * blend * (3.0f - 2.0f * blend);
                q = Quat_Slerp(Quat_Identity(), q, blend);
            }
            Vector3 cx=Quat_RotateVector(q,Vec3_Create(1,0,0));
            Vector3 cy=Quat_RotateVector(q,Vec3_Create(0,1,0));
            Vector3 cz=Quat_RotateVector(q,Vec3_Create(0,0,1));
            x->rotRow[0]=Vec3_Create(cx.x,cy.x,cz.x);
            x->rotRow[1]=Vec3_Create(cx.y,cy.y,cz.y);
            x->rotRow[2]=Vec3_Create(cx.z,cy.z,cz.z);
            x->hasRotation=fabsf(q.x)+fabsf(q.y)+fabsf(q.z)>1e-6f;
        }
    }

    /* 2. Deformar vértices y actualizar normales */
    for (size_t v = 0; v < morph->vertexCount; ++v) {
        const AnatomyDeformerBinding* b = &morph->bindings[v];
        Vector3 newPos = Vec3_Zero();
        Vector3 newNorm = Vec3_Zero();
        Vector3 baseNorm = morph->baseNormals[v];

        for (int k = 0; k < 2; ++k) {
            if (b->weight[k] <= 0.0f) continue;
            uint16_t cIdx = b->connIndex[k];
            const ConnTransform* x = &xforms[cIdx];

            float t = b->projT[k];
            Vector3 newProj = Vec3_Add(x->newA, Vec3_Scale(x->newDir, t));
            float radScale = Math_Clamp((1.0f - t) * x->radScaleA + t * x->radScaleB, 0.0f, 4.0f);

            Vector3 offset = b->localOffset[k];
            Vector3 rotNorm = baseNorm;
            if (x->hasRotation) {
                offset = Vec3_Create(
                    Vec3_Dot(x->rotRow[0], offset),
                    Vec3_Dot(x->rotRow[1], offset),
                    Vec3_Dot(x->rotRow[2], offset)
                );
                rotNorm = Vec3_Create(
                    Vec3_Dot(x->rotRow[0], baseNorm),
                    Vec3_Dot(x->rotRow[1], baseNorm),
                    Vec3_Dot(x->rotRow[2], baseNorm)
                );
            }

            Vector3 contribution = Vec3_Add(newProj, Vec3_Scale(offset, radScale));
            newPos = Vec3_Add(newPos, Vec3_Scale(contribution, b->weight[k]));
            newNorm = Vec3_Add(newNorm, Vec3_Scale(rotNorm, b->weight[k]));
        }

        targetMesh->vertices[v].position = newPos;
        float normLenSq = Vec3_LengthSq(newNorm);
        if (normLenSq > 1e-6f) {
            targetMesh->vertices[v].normal = Vec3_Scale(newNorm, 1.0f / sqrtf(normLenSq));
        } else {
            targetMesh->vertices[v].normal = baseNorm;
        }
    }

    Mesh_MarkGeometryChanged(targetMesh);
    return true;
}

bool AnatomyDeformer_BindSkeleton(AnatomyDeformer* d,const Mesh* mesh,const AnatomyGraph* rest,const Skeleton* s) {
    if (!d || !Skeleton_Validate(s) || !AnatomyDeformer_Bind(d,mesh,rest) ||
        !SkeletonPose_Init(&d->bindPose,s)) return false;
    AnatomySkinBinding* skin=calloc(mesh->vertexCount,sizeof(*skin));
    if(!skin)return false;
    for(size_t v=0;v<mesh->vertexCount;++v) {
        const AnatomyDeformerBinding* binding=&d->bindings[v];
        for(int k=0;k<2;++k) {
            const BodyConnection* c=&rest->connections[binding->connIndex[k]];
            int a=Skeleton_FindJoint(s,c->fromId),b=Skeleton_FindJoint(s,c->toId);
            if(a<0 || b<0) { free(skin); return false; }
            float t=binding->projT[k];
            if(s->joints[a].parentIndex==b) { int tmp=a;a=b;b=tmp;t=1-t; }
            else if(s->joints[b].parentIndex!=a) { free(skin); return false; }
            /* Transición suave alrededor del extremo distal; el centro de
             * la articulación coincide en ambos transformados. Incluye twist. */
            t=fmaxf(0,(t-.5f)*2); t=t*t*(3-2*t);
            if(s->joints[b].constraint.type==JOINT_FIXED)t=0;
            skin[v].joints[k*2]=a; skin[v].weights[k*2]=binding->weight[k]*(1-t);
            skin[v].joints[k*2+1]=b; skin[v].weights[k*2+1]=binding->weight[k]*t;
        }
    }
    free(d->skinBindings); d->skinBindings=skin;
    d->poseBound=true; return true;
}
bool AnatomyDeformer_DeformPose(const AnatomyDeformer* d,const Skeleton* s,const SkeletonPose* p,Mesh* mesh) {
    if(!d || !s || !p || !mesh || !d->poseBound || !mesh->vertices ||
       mesh->vertexCount!=d->vertexCount || p->jointCount>SKELETON_MAX_JOINTS || p->jointCount!=d->bindPose.jointCount || s->jointCount!=p->jointCount)return false;
    /* Matrices de hueso precomputadas: O(J), bucle de vértices O(V), sin malloc. */
    Vector3 rows[SKELETON_MAX_JOINTS][3],offset[SKELETON_MAX_JOINTS];
    for(size_t j=0;j<p->jointCount;++j) {
        Quaternion q=Quat_Multiply(p->joints[j].worldRotation,Quat_Inverse(d->bindPose.joints[j].worldRotation));
        Vector3 x=Quat_RotateVector(q,Vec3_Create(1,0,0)),y=Quat_RotateVector(q,Vec3_Create(0,1,0)),z=Quat_RotateVector(q,Vec3_Create(0,0,1));
        rows[j][0]=Vec3_Create(x.x,y.x,z.x); rows[j][1]=Vec3_Create(x.y,y.y,z.y); rows[j][2]=Vec3_Create(x.z,y.z,z.z);
        offset[j]=Vec3_Sub(p->joints[j].worldPosition,Quat_RotateVector(q,d->bindPose.joints[j].worldPosition));
    }
    for(size_t v=0;v<d->vertexCount;++v) {
        Vector3 position=Vec3_Zero(),normal=Vec3_Zero();
        const AnatomySkinBinding* b=&d->skinBindings[v];
        for(int k=0;k<4;++k) {
            if(b->weights[k]<=0)continue;
            int j=b->joints[k];
            Vector3 bp=d->basePositions[v],bn=d->baseNormals[v];
            const Vector3* row=rows[j]; float w=b->weights[k];
            position.x+=(row[0].x*bp.x+row[0].y*bp.y+row[0].z*bp.z+offset[j].x)*w;
            position.y+=(row[1].x*bp.x+row[1].y*bp.y+row[1].z*bp.z+offset[j].y)*w;
            position.z+=(row[2].x*bp.x+row[2].y*bp.y+row[2].z*bp.z+offset[j].z)*w;
            normal.x+=(row[0].x*bn.x+row[0].y*bn.y+row[0].z*bn.z)*w;
            normal.y+=(row[1].x*bn.x+row[1].y*bn.y+row[1].z*bn.z)*w;
            normal.z+=(row[2].x*bn.x+row[2].y*bn.y+row[2].z*bn.z)*w;
        }
        float length=sqrtf(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
        if(length>1e-12f) { normal.x/=length; normal.y/=length; normal.z/=length; }
        mesh->vertices[v].position=position; mesh->vertices[v].normal=normal;
    }
    Mesh_MarkGeometryChanged(mesh);
    return true;
}
