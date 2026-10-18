/**
 * @file AnatomyDeformer.c
 * @brief Deformación anatómica genérica; puente morfológico y pose esquelética.
 * @author Monster Engine Team
 * @date 2026
 */

#include "AnatomyDeformer.h"
#include "MathUtils.h"
#include "AABB.h"
#include "MonsterSDF.h"
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
    if (dst == src) return true;
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
    if (src->vertexCount > 0 && (!dst->bindings || !dst->basePositions || !dst->baseNormals)) {
        dst->vertexCapacity=0; dst->vertexCount=0; dst->isBound=false; dst->poseBound=false; return false;
    }
    if (src->vertexCount > 0 && dst->bindings && dst->basePositions && dst->baseNormals) {
        memcpy(dst->bindings, src->bindings, src->vertexCount * sizeof(AnatomyDeformerBinding));
        memcpy(dst->basePositions, src->basePositions, src->vertexCount * sizeof(Vector3));
        memcpy(dst->baseNormals, src->baseNormals, src->vertexCount * sizeof(Vector3));
    }
    if (src->skinBindings && src->vertexCount > 0) {
        free(dst->skinBindings);
        dst->skinBindings = (AnatomySkinBinding*)malloc(src->vertexCount * sizeof(AnatomySkinBinding));
        if (!dst->skinBindings) { dst->isBound=false; dst->poseBound=false; return false; }
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

bool AnatomyDeformer_Bind(AnatomyDeformer* morph,const Mesh* baseMesh,
    const AnatomyGraph* refGraph) {
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

    /* 1. Precomputar datos de cada conexión anatómica para acelerar la vinculación */
    typedef struct PrecomputedConn {
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
        if (!nodeA || !nodeB || refGraph->dormantConnections[c] || (conn->kind==BODY_CONNECTION_SUPPORT && nodeA->region!=ANATOMY_REGION_ORNAMENT && nodeB->region!=ANATOMY_REGION_ORNAMENT)) {
            conns[c].valid = false;
            continue;
        }
        conns[c].valid = true;
        conns[c].a = nodeA->center;
        conns[c].ab = Vec3_Sub(nodeB->center, nodeA->center);
        float abLenSq = Vec3_LengthSq(conns[c].ab);
        conns[c].invAbLenSq = abLenSq > 1e-8f ? 1.0f / abLenSq : 0.0f;
        conns[c].radA = (nodeA->widthRadius + nodeA->heightRadius) * 0.5f;
        conns[c].radB = (nodeB->widthRadius + nodeB->heightRadius) * 0.5f;
    }

    /* El vecino de corte tiene peso cero. Cambiar el conjunto de vecinos no
     * produce saltos de influencia entre vértices adyacentes de una articulación. */
    for(size_t v=0;v<count;++v) {
        Vector3 pos=baseMesh->vertices[v].position;
        morph->basePositions[v]=pos;morph->baseNormals[v]=baseMesh->vertices[v].normal;
        float distance[ANATOMY_MORPH_INFLUENCES+1];
        uint16_t indices[ANATOMY_MORPH_INFLUENCES+1]={0};
        for(int k=0;k<=ANATOMY_MORPH_INFLUENCES;++k)distance[k]=INFINITY;
        for(size_t c=0;c<refGraph->connectionCount;++c) {
            const PrecomputedConn* pc=&conns[c];if(!pc->valid)continue;
            float t=Math_Clamp01(Vec3_Dot(Vec3_Sub(pos,pc->a),pc->ab)*pc->invAbLenSq);
            Vector3 proj=Vec3_Add(pc->a,Vec3_Scale(pc->ab,t));
            float rad=fmaxf(Math_Lerp(pc->radA,pc->radB,t),1e-6f);
            float d=Vec3_LengthSq(Vec3_Sub(pos,proj))/(rad*rad);
            for(int k=0;k<=ANATOMY_MORPH_INFLUENCES;++k)if(d<distance[k]) {
                for(int j=ANATOMY_MORPH_INFLUENCES;j>k;--j){distance[j]=distance[j-1];indices[j]=indices[j-1];}
                distance[k]=d;indices[k]=(uint16_t)c;break;
            }
        }
        if(!isfinite(distance[0])){morph->isBound=false;return false;}
        AnatomyDeformerBinding* bind=&morph->bindings[v];memset(bind,0,sizeof(*bind));
        float sum=0;
        float cutoff=isfinite(distance[ANATOMY_MORPH_INFLUENCES])?distance[ANATOMY_MORPH_INFLUENCES]:INFINITY;
        for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k) {
            if(!isfinite(distance[k]))continue;
            const PrecomputedConn* pc=&conns[indices[k]];
            float t=Math_Clamp01(Vec3_Dot(Vec3_Sub(pos,pc->a),pc->ab)*pc->invAbLenSq);
            bind->connIndex[k]=indices[k];bind->projT[k]=t;
            bind->refRadius[k]=fmaxf(Math_Lerp(pc->radA,pc->radB,t),1e-6f);
            bind->localOffset[k]=Vec3_Sub(pos,Vec3_Add(pc->a,Vec3_Scale(pc->ab,t)));
            float support=isfinite(cutoff)?fmaxf(0,1-distance[k]/fmaxf(cutoff,1e-6f)):1;
            bind->weight[k]=support*support/(distance[k]*distance[k]+1e-4f);sum+=bind->weight[k];
        }
        if(sum<1e-12f){bind->weight[0]=1;sum=1;}
        for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k)bind->weight[k]/=sum;
    }

    morph->isBound = true;
    return true;
}

bool AnatomyDeformer_Reanchor(AnatomyDeformer* morph,const Mesh* mesh,
    const AnatomyGraph* newGraph,const uint32_t* oldVertexOfNew,
    size_t* newlyBoundVertices) {
    if(newlyBoundVertices)*newlyBoundVertices=0;
    if(!AnatomyDeformer_IsBound(morph)||!mesh||!mesh->vertices||
       !mesh->vertexCount||!newGraph||!oldVertexOfNew||
       !AnatomyGraph_Validate(newGraph)||
       !AnatomyGraph_TopologyCompatible(&morph->refGraph,newGraph))return false;

    /* La compatibilidad permite permutar conexiones: traducir por identidad,
     * nunca reutilizar ciegamente el índice numérico del grafo antiguo. */
    uint16_t connectionMap[ANATOMY_MAX_CONNECTIONS];
    for(size_t old=0;old<morph->refGraph.connectionCount;++old) {
        const BodyConnection* source=&morph->refGraph.connections[old];
        size_t found=newGraph->connectionCount;
        for(size_t next=0;next<newGraph->connectionCount;++next) {
            const BodyConnection* target=&newGraph->connections[next];
            if(source->id==target->id&&source->fromId==target->fromId&&
               source->toId==target->toId&&source->kind==target->kind&&
               source->moduleInstanceId==target->moduleInstanceId) {
                found=next;break;
            }
        }
        if(found==newGraph->connectionCount)return false;
        connectionMap[old]=(uint16_t)found;
    }

    size_t newCount=0;
    for(size_t i=0;i<mesh->vertexCount;++i)
        newCount+=oldVertexOfNew[i]>=morph->vertexCount;
    Mesh newMesh=Mesh_Create();
    AnatomyDeformer* fresh=NULL;
    if(newCount) {
        if(!Mesh_ReserveVertices(&newMesh,newCount))return false;
        for(size_t i=0;i<mesh->vertexCount;++i)
            if(oldVertexOfNew[i]>=morph->vertexCount)
                newMesh.vertices[newMesh.vertexCount++]=mesh->vertices[i];
        fresh=AnatomyDeformer_Create();
        if(!fresh||!AnatomyDeformer_Bind(fresh,&newMesh,newGraph)) {
            Mesh_Free(&newMesh);AnatomyDeformer_Free(fresh);return false;
        }
    }
    AnatomyDeformerBinding* bindings=malloc(mesh->vertexCount*sizeof(*bindings));
    Vector3* positions=malloc(mesh->vertexCount*sizeof(*positions));
    Vector3* normals=malloc(mesh->vertexCount*sizeof(*normals));
    if(!bindings||!positions||!normals) {
        free(bindings);free(positions);free(normals);
        Mesh_Free(&newMesh);AnatomyDeformer_Free(fresh);return false;
    }
    size_t freshIndex=0;
    bool valid=true;
    for(size_t i=0;i<mesh->vertexCount;++i) {
        Vector3 position=mesh->vertices[i].position;
        positions[i]=position;normals[i]=mesh->vertices[i].normal;
        uint32_t old=oldVertexOfNew[i];
        if(old>=morph->vertexCount) {
            bindings[i]=fresh->bindings[freshIndex++];
            continue;
        }
        AnatomyDeformerBinding binding=morph->bindings[old];
        for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k) {
            if(binding.weight[k]<=0)continue;
            uint16_t sourceIndex=binding.connIndex[k];
            if(sourceIndex>=morph->refGraph.connectionCount){valid=false;break;}
            uint16_t targetIndex=connectionMap[sourceIndex];
            const BodyConnection* conn=&newGraph->connections[targetIndex];
            const AnatomyNode* a=AnatomyGraph_FindNode(newGraph,conn->fromId);
            const AnatomyNode* b=AnatomyGraph_FindNode(newGraph,conn->toId);
            if(!a||!b){valid=false;break;}
            Vector3 axis=Vec3_Sub(b->center,a->center);
            float lengthSquared=Vec3_Dot(axis,axis);
            float t=lengthSquared>1e-8f?
                Math_Clamp01(Vec3_Dot(Vec3_Sub(position,a->center),axis)/lengthSquared):0.0f;
            Vector3 projection=Vec3_Add(a->center,Vec3_Scale(axis,t));
            float radiusA=(a->widthRadius+a->heightRadius)*.5f;
            float radiusB=(b->widthRadius+b->heightRadius)*.5f;
            binding.connIndex[k]=targetIndex;
            binding.projT[k]=t;
            binding.refRadius[k]=fmaxf(Math_Lerp(radiusA,radiusB,t),1e-6f);
            binding.localOffset[k]=Vec3_Sub(position,projection);
        }
        if(!valid)break;
        bindings[i]=binding;
    }
    Mesh_Free(&newMesh);AnatomyDeformer_Free(fresh);
    if(!valid){free(bindings);free(positions);free(normals);return false;}
    free(morph->bindings);free(morph->basePositions);free(morph->baseNormals);
    free(morph->skinBindings);morph->skinBindings=NULL;
    morph->bindings=bindings;morph->basePositions=positions;morph->baseNormals=normals;
    morph->vertexCount=morph->vertexCapacity=mesh->vertexCount;
    morph->refGraph=*newGraph;morph->poseBound=false;morph->isBound=true;
    if(newlyBoundVertices)*newlyBoundVertices=newCount;
    return true;
}

bool AnatomyDeformer_DeformMasked(const AnatomyDeformer* morph,
    const AnatomyGraph* newGraph,Mesh* targetMesh,const uint8_t* frozenVertices) {
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
        if(frozenVertices&&frozenVertices[v])continue;
        const AnatomyDeformerBinding* b = &morph->bindings[v];
        Vector3 newPos = Vec3_Zero();
        Vector3 newNorm = Vec3_Zero();
        Vector3 baseNorm = morph->baseNormals[v];

        for (int k = 0; k < ANATOMY_MORPH_INFLUENCES; ++k) {
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

bool AnatomyDeformer_Deform(const AnatomyDeformer* morph,
    const AnatomyGraph* newGraph,Mesh* targetMesh) {
    return AnatomyDeformer_DeformMasked(morph,newGraph,targetMesh,NULL);
}

bool AnatomyDeformer_AnchorFrozenVertices(const AnatomyDeformer* morph,
    const AnatomyGraph* newGraph,const Mesh* certifiedMesh,
    Mesh* targetMesh,const uint8_t* frozenMask) {
    if (!morph || !morph->isBound || !newGraph || !certifiedMesh || !targetMesh || !frozenMask) return false;
    if (certifiedMesh->vertexCount != morph->vertexCount || targetMesh->vertexCount != morph->vertexCount) return false;
    if (!certifiedMesh->vertices || !targetMesh->vertices) return false;

    typedef struct AnchorTransform {
        Vector3 refA;
        Vector3 newA;
        Vector3 rotRow[3];
        bool valid;
    } AnchorTransform;

    AnchorTransform xforms[ANATOMY_MAX_CONNECTIONS];
    for (size_t c = 0; c < morph->refGraph.connectionCount; ++c) {
        const BodyConnection* connRef = &morph->refGraph.connections[c];
        const AnatomyNode* refA = AnatomyGraph_FindNode(&morph->refGraph, connRef->fromId);
        const AnatomyNode* refB = AnatomyGraph_FindNode(&morph->refGraph, connRef->toId);
        const AnatomyNode* newA = AnatomyGraph_FindNode(newGraph, connRef->fromId);
        const AnatomyNode* newB = AnatomyGraph_FindNode(newGraph, connRef->toId);

        AnchorTransform* x = &xforms[c];
        x->valid = false;
        if (!refA || !newA) continue;

        x->refA = refA->center;
        x->newA = newA->center;
        x->rotRow[0] = Vec3_Create(1.0f, 0.0f, 0.0f);
        x->rotRow[1] = Vec3_Create(0.0f, 1.0f, 0.0f);
        x->rotRow[2] = Vec3_Create(0.0f, 0.0f, 1.0f);

        if (refB && newB) {
            Vector3 refDir = Vec3_Sub(refB->center, refA->center);
            Vector3 newDir = Vec3_Sub(newB->center, newA->center);
            float refLen = Vec3_Length(refDir);
            float newLen = Vec3_Length(newDir);
            if (refLen > 1e-5f && newLen > 1e-6f) {
                Quaternion q = Quat_FromTo(refDir, newDir);
                Vector3 cx = Quat_RotateVector(q, Vec3_Create(1.0f, 0.0f, 0.0f));
                Vector3 cy = Quat_RotateVector(q, Vec3_Create(0.0f, 1.0f, 0.0f));
                Vector3 cz = Quat_RotateVector(q, Vec3_Create(0.0f, 0.0f, 1.0f));
                x->rotRow[0] = Vec3_Create(cx.x, cy.x, cz.x);
                x->rotRow[1] = Vec3_Create(cx.y, cy.y, cz.y);
                x->rotRow[2] = Vec3_Create(cx.z, cy.z, cz.z);
            }
        }
        x->valid = true;
    }

    for (size_t v = 0; v < morph->vertexCount; ++v) {
        if (!frozenMask[v]) continue;

        const AnatomyDeformerBinding* b = &morph->bindings[v];
        Vector3 certPos = certifiedMesh->vertices[v].position;
        Vector3 certNorm = certifiedMesh->vertices[v].normal;

        Vector3 accumPos = Vec3_Zero();
        Vector3 accumNorm = Vec3_Zero();
        float totalWeight = 0.0f;

        for (int k = 0; k < ANATOMY_MORPH_INFLUENCES; ++k) {
            if (b->weight[k] <= 0.0f) continue;
            uint16_t cIdx = b->connIndex[k];
            if (cIdx >= morph->refGraph.connectionCount) continue;
            const AnchorTransform* x = &xforms[cIdx];
            if (!x->valid) continue;

            Vector3 delta = Vec3_Sub(certPos, x->refA);
            Vector3 rotDelta = Vec3_Create(
                Vec3_Dot(x->rotRow[0], delta),
                Vec3_Dot(x->rotRow[1], delta),
                Vec3_Dot(x->rotRow[2], delta)
            );
            Vector3 rotNorm = Vec3_Create(
                Vec3_Dot(x->rotRow[0], certNorm),
                Vec3_Dot(x->rotRow[1], certNorm),
                Vec3_Dot(x->rotRow[2], certNorm)
            );

            Vector3 pAnchored = Vec3_Add(x->newA, rotDelta);
            accumPos = Vec3_Add(accumPos, Vec3_Scale(pAnchored, b->weight[k]));
            accumNorm = Vec3_Add(accumNorm, Vec3_Scale(rotNorm, b->weight[k]));
            totalWeight += b->weight[k];
        }

        if (totalWeight > 1e-5f) {
            float invWeight = 1.0f / totalWeight;
            targetMesh->vertices[v].position = Vec3_Scale(accumPos, invWeight);
            float normLenSq = Vec3_LengthSq(accumNorm);
            if (normLenSq > 1e-6f) {
                targetMesh->vertices[v].normal = Vec3_Normalize(accumNorm);
            } else {
                targetMesh->vertices[v].normal = certNorm;
            }
        } else {
            targetMesh->vertices[v].position = certPos;
            targetMesh->vertices[v].normal = certNorm;
        }
    }

    return true;
}

bool AnatomyDeformer_BindSkeleton(AnatomyDeformer* d,const Mesh* mesh,const AnatomyGraph* rest,const Skeleton* s) {
    if (!d || !Skeleton_Validate(s) || !AnatomyDeformer_Bind(d,mesh,rest) ||
        !SkeletonPose_Init(&d->bindPose,s)) return false;
    AnatomySkinBinding* skin=calloc(mesh->vertexCount,sizeof(*skin));
    if(!skin)return false;
    for(size_t v=0;v<mesh->vertexCount;++v) {
        const AnatomyDeformerBinding* binding=&d->bindings[v];
        for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k) {
            const BodyConnection* c=&rest->connections[binding->connIndex[k]];
            int a=Skeleton_FindJoint(s,c->fromId),b=Skeleton_FindJoint(s,c->toId);
            if(a<0 || b<0) { free(skin); return false; }
            float t=binding->projT[k];
            /* Los pivotes auxiliares sin volumen pueden interponerse entre
             * nodos anatómicos; la superficie conserva ambos extremos reales. */
            int pa=s->joints[a].parentIndex,pb=s->joints[b].parentIndex;
            bool reverse=pa==b||(pa>=0&&!s->joints[pa].anatomyId&&s->joints[pa].parentIndex==b);
            bool forward=pb==a||(pb>=0&&!s->joints[pb].anatomyId&&s->joints[pb].parentIndex==a);
            if(reverse) { int tmp=a;a=b;b=tmp;t=1-t; }
            else if(!forward) { free(skin); return false; }
            /* Un pivote interpuesto también transporta el segmento: omitirlo
             * dejaba vértices cervicales anclados al tórax durante la flexión. */
            int auxiliary=s->joints[b].parentIndex;
            if(auxiliary>=0&&!s->joints[auxiliary].anatomyId&&s->joints[auxiliary].parentIndex==a)a=auxiliary;
            /* Transición suave alrededor del extremo distal; el centro de
             * la articulación coincide en ambos transformados. Incluye twist. */
            t=fmaxf(0,(t-.5f)*2); t=t*t*(3-2*t);
            if(s->joints[b].constraint.type==JOINT_FIXED)t=0;
            skin[v].joints[k*2]=a; skin[v].weights[k*2]=binding->weight[k]*(1-t);
            skin[v].joints[k*2+1]=b; skin[v].weights[k*2+1]=binding->weight[k]*t;
        }
        /* Conexiones vecinas comparten articulaciones. Fusionar sus pesos
         * conserva el mismo skinning y evita aplicar varias veces un hueso. */
        AnatomySkinBinding compact={0};int count=0;
        for(int k=0;k<2*ANATOMY_MORPH_INFLUENCES;++k) {
            if(skin[v].weights[k]<=0)continue;
            int slot=0;
            while(slot<count && compact.joints[slot]!=skin[v].joints[k])++slot;
            if(slot==count)compact.joints[count++]=skin[v].joints[k];
            compact.weights[slot]+=skin[v].weights[k];
        }
        skin[v]=compact;
    }
    free(d->skinBindings); d->skinBindings=skin;
    d->poseBound=true; return true;
}
float AnatomyDeformer_CranialWeight(Vector3 p,Vector3 attachment,Vector3 axis,float collar) {
    float t=Math_Clamp01(1+Vec3_Dot(Vec3_Sub(p,attachment),axis)/fmaxf(collar,1e-6f));
    return t*t*(3-2*t);
}
bool AnatomyDeformer_BindCranium(AnatomyDeformer* d,const Skeleton* s,
    int head,Vector3 attachment,Vector3 axis,float collar,const MonsterSDF* sdf) {
    if(!d||!d->poseBound||!d->skinBindings||!s||head<0||(size_t)head>=s->jointCount||
       !isfinite(collar)||collar<=0||!isfinite(Vec3_LengthSq(attachment))||
       !isfinite(Vec3_LengthSq(axis))||fabsf(Vec3_LengthSq(axis)-1)>1e-3f)return false;
    AnatomyId headId=s->joints[head].anatomyId;
    MonsterSDFHeadField context;SDFField field={0};
    if(sdf&&sdf->mouthCount)field=MonsterSDF_GetHeadField(sdf,0,&context);
    for(size_t v=0;v<d->vertexCount;++v) {
        float w=AnatomyDeformer_CranialWeight(d->basePositions[v],attachment,axis,collar);
        if(w<=0)continue;
        const BodyConnection* c=&d->refGraph.connections[d->bindings[v].connIndex[0]];
        /* La propiedad anatómica evita que un pie cercano al morro siga al cráneo. */
        if(c->fromId!=headId&&c->toId!=headId) {
            /* La cara puede estar más cerca de un segmento ancho del tórax
             * tras mutar. Su superficie cefálica tiene prioridad semántica. */
            if(!field.evaluateDistance||field.evaluateDistance(field.context,d->basePositions[v])>
               sdf->mouths[0].headBodySmoothness*.5f)continue;
        }
        AnatomySkinBinding old=d->skinBindings[v],next={0};
        next.joints[0]=head;next.weights[0]=w;int count=1;
        for(int k=0;k<2*ANATOMY_MORPH_INFLUENCES;++k) {
            float weight=old.weights[k]*(1-w);if(weight<=0)continue;
            if(old.joints[k]==head)next.weights[0]+=weight;
            else if(count<2*ANATOMY_MORPH_INFLUENCES) {
                next.joints[count]=old.joints[k];next.weights[count++]=weight;
            } else return false;
        }
        d->skinBindings[v]=next;
    }
    return true;
}
bool AnatomyDeformer_BuildPosePalette(const AnatomyDeformer* d,const Skeleton* s,
    const SkeletonPose* p,AnatomyPosePalette* palette) {
    if(!palette)return false;
    palette->jointCount=0;
    if(!d||!s||!p||!d->poseBound||p->jointCount>SKELETON_MAX_JOINTS||
       p->jointCount!=d->bindPose.jointCount||s->jointCount!=p->jointCount)return false;
    for(size_t j=0;j<p->jointCount;++j) {
        Quaternion q=Quat_Multiply(p->joints[j].worldRotation,Quat_Inverse(d->bindPose.joints[j].worldRotation));
        Vector3 x=Quat_RotateVector(q,Vec3_Create(1,0,0)),y=Quat_RotateVector(q,Vec3_Create(0,1,0)),z=Quat_RotateVector(q,Vec3_Create(0,0,1));
        Vector3 offset=Vec3_Sub(p->joints[j].worldPosition,Quat_RotateVector(q,d->bindPose.joints[j].worldPosition));
        float rows[3][4]={{x.x,y.x,z.x,offset.x},{x.y,y.y,z.y,offset.y},{x.z,y.z,z.z,offset.z}};
        memcpy(palette->rows[j],rows,sizeof(rows));
    }
    palette->jointCount=p->jointCount;return true;
}
static bool EvaluatePoseVertex(const AnatomyDeformer* d,const AnatomyPosePalette* palette,
    size_t vertex,Vector3* position,Vector3* normal) {
    *position=Vec3_Zero();*normal=Vec3_Zero();
    const AnatomySkinBinding* b=&d->skinBindings[vertex];
    Vector3 bp=d->basePositions[vertex],bn=d->baseNormals[vertex];
    for(int k=0;k<2*ANATOMY_MORPH_INFLUENCES;++k) {
        if(b->weights[k]<=0)break;
        int j=b->joints[k];if(j<0||(size_t)j>=palette->jointCount)return false;
        const float (*row)[4]=palette->rows[j];float w=b->weights[k];
        position->x+=(row[0][0]*bp.x+row[0][1]*bp.y+row[0][2]*bp.z+row[0][3])*w;
        position->y+=(row[1][0]*bp.x+row[1][1]*bp.y+row[1][2]*bp.z+row[1][3])*w;
        position->z+=(row[2][0]*bp.x+row[2][1]*bp.y+row[2][2]*bp.z+row[2][3])*w;
        normal->x+=(row[0][0]*bn.x+row[0][1]*bn.y+row[0][2]*bn.z)*w;
        normal->y+=(row[1][0]*bn.x+row[1][1]*bn.y+row[1][2]*bn.z)*w;
        normal->z+=(row[2][0]*bn.x+row[2][1]*bn.y+row[2][2]*bn.z)*w;
    }
    float length=sqrtf(normal->x*normal->x+normal->y*normal->y+normal->z*normal->z);
    if(length>1e-12f){float inverse=1/length;normal->x*=inverse;normal->y*=inverse;normal->z*=inverse;}
    return true;
}
bool AnatomyDeformer_EvaluatePoseVertex(const AnatomyDeformer* d,const AnatomyPosePalette* palette,
    size_t vertex,Vector3* position,Vector3* normal) {
    if(!d||!palette||!position||!normal||!d->poseBound||vertex>=d->vertexCount||
       !d->skinBindings||!d->basePositions||!d->baseNormals||palette->jointCount!=d->bindPose.jointCount)return false;
    return EvaluatePoseVertex(d,palette,vertex,position,normal);
}
bool AnatomyDeformer_DeformPose(const AnatomyDeformer* d,const Skeleton* s,const SkeletonPose* p,Mesh* mesh) {
    if(!mesh||!mesh->vertices||!d||mesh->vertexCount!=d->vertexCount)return false;
    AnatomyPosePalette palette;
    if(!AnatomyDeformer_BuildPosePalette(d,s,p,&palette))return false;
    for(size_t v=0;v<d->vertexCount;++v)
        if(!EvaluatePoseVertex(d,&palette,v,&mesh->vertices[v].position,&mesh->vertices[v].normal))return false;
    Mesh_MarkGeometryChanged(mesh);return true;
}
