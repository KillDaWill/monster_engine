/**
 * @file LizardMorph.c
 * @brief Implementación del deformador morfológico en tiempo real para lagartos.
 * @author Monster Engine Team
 * @date 2026
 */

#include "LizardMorph.h"
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

LizardMorph* LizardMorph_Create(void) {
    LizardMorph* morph = (LizardMorph*)calloc(1, sizeof(LizardMorph));
    return morph;
}

void LizardMorph_Free(LizardMorph* morph) {
    if (!morph) return;
    free(morph->bindings);
    free(morph->basePositions);
    free(morph->baseNormals);
    free(morph);
}

bool LizardMorph_IsBound(const LizardMorph* morph) {
    return morph && morph->isBound && morph->vertexCount > 0 && morph->bindings != NULL;
}

size_t LizardMorph_GetVertexCount(const LizardMorph* morph) {
    return (morph && morph->isBound) ? morph->vertexCount : 0;
}

bool LizardMorph_Bind(LizardMorph* morph, const Mesh* baseMesh, const AnatomyGraph* refGraph) {
    if (!morph || !baseMesh || !refGraph) return false;
    if (baseMesh->vertexCount == 0 || !baseMesh->vertices || refGraph->connectionCount == 0) return false;

    size_t count = baseMesh->vertexCount;
    if (count > morph->vertexCapacity) {
        free(morph->bindings);
        free(morph->basePositions);
        free(morph->baseNormals);

        morph->bindings = (LizardMorphBinding*)malloc(count * sizeof(LizardMorphBinding));
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

    /* 1. Precomputar AABB de cada conexión anatómica para poda espacial rápida */
    AABB3D connBoxes[ANATOMY_MAX_CONNECTIONS];
    for (size_t c = 0; c < refGraph->connectionCount; ++c) {
        const BodyConnection* conn = &refGraph->connections[c];
        const AnatomyNode* nodeA = AnatomyGraph_FindNode(refGraph, conn->fromId);
        const AnatomyNode* nodeB = AnatomyGraph_FindNode(refGraph, conn->toId);
        if (!nodeA || !nodeB) {
            connBoxes[c].start = Vec3_Zero();
            connBoxes[c].end = Vec3_Zero();
            continue;
        }
        float pad = fmaxf(nodeA->widthRadius, fmaxf(nodeA->heightRadius,
                    fmaxf(nodeB->widthRadius, nodeB->heightRadius))) * 2.2f;
        connBoxes[c].start.x = fminf(nodeA->center.x, nodeB->center.x) - pad;
        connBoxes[c].end.x   = fmaxf(nodeA->center.x, nodeB->center.x) + pad;
        connBoxes[c].start.y = fminf(nodeA->center.y, nodeB->center.y) - pad;
        connBoxes[c].end.y   = fmaxf(nodeA->center.y, nodeB->center.y) + pad;
        connBoxes[c].start.z = fminf(nodeA->center.z, nodeB->center.z) - pad;
        connBoxes[c].end.z   = fmaxf(nodeA->center.z, nodeB->center.z) + pad;
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
            /* Descarte por AABB */
            if (pos.x < connBoxes[c].start.x || pos.x > connBoxes[c].end.x ||
                pos.y < connBoxes[c].start.y || pos.y > connBoxes[c].end.y ||
                pos.z < connBoxes[c].start.z || pos.z > connBoxes[c].end.z) {
                continue;
            }

            const BodyConnection* conn = &refGraph->connections[c];
            const AnatomyNode* nodeA = AnatomyGraph_FindNode(refGraph, conn->fromId);
            const AnatomyNode* nodeB = AnatomyGraph_FindNode(refGraph, conn->toId);
            if (!nodeA || !nodeB) continue;

            Vector3 a = nodeA->center;
            Vector3 b = nodeB->center;
            Vector3 ab = Vec3_Sub(b, a);
            float abLenSq = Vec3_LengthSq(ab);
            float t = 0.0f;
            if (abLenSq > 1e-8f) {
                t = Vec3_Dot(Vec3_Sub(pos, a), ab) / abLenSq;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
            }
            Vector3 proj = Vec3_Add(a, Vec3_Scale(ab, t));
            float radA = (nodeA->widthRadius + nodeA->heightRadius) * 0.5f;
            float radB = (nodeB->widthRadius + nodeB->heightRadius) * 0.5f;
            float rad = (1.0f - t) * radA + t * radB;
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
                const BodyConnection* conn = &refGraph->connections[c];
                const AnatomyNode* nodeA = AnatomyGraph_FindNode(refGraph, conn->fromId);
                const AnatomyNode* nodeB = AnatomyGraph_FindNode(refGraph, conn->toId);
                if (!nodeA || !nodeB) continue;

                Vector3 a = nodeA->center;
                Vector3 b = nodeB->center;
                Vector3 ab = Vec3_Sub(b, a);
                float abLenSq = Vec3_LengthSq(ab);
                float t = 0.0f;
                if (abLenSq > 1e-8f) {
                    t = Vec3_Dot(Vec3_Sub(pos, a), ab) / abLenSq;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                }
                Vector3 proj = Vec3_Add(a, Vec3_Scale(ab, t));
                float radA = (nodeA->widthRadius + nodeA->heightRadius) * 0.5f;
                float radB = (nodeB->widthRadius + nodeB->heightRadius) * 0.5f;
                float rad = (1.0f - t) * radA + t * radB;
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

        LizardMorphBinding* b = &morph->bindings[v];
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

bool LizardMorph_Deform(const LizardMorph* morph, const AnatomyGraph* newGraph, Mesh* targetMesh) {
    if (!morph || !morph->isBound || !newGraph || !targetMesh) return false;
    if (targetMesh->vertexCount != morph->vertexCount || !targetMesh->vertices) return false;

    /* 1. Precomputar transformaciones para cada conexión */
    ConnTransform xforms[ANATOMY_MAX_CONNECTIONS];
    for (size_t c = 0; c < morph->refGraph.connectionCount; ++c) {
        const BodyConnection* connRef = &morph->refGraph.connections[c];
        const BodyConnection* connNew = &newGraph->connections[c];
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

        x->radScaleA = refRadA > 1e-4f ? newRadA / refRadA : 1.0f;
        x->radScaleB = refRadB > 1e-4f ? newRadB / refRadB : 1.0f;

        Vector3 refDir = Vec3_Sub(refB->center, refA->center);
        float refLen = Vec3_Length(refDir);
        float newLen = Vec3_Length(x->newDir);

        if (refLen > 1e-5f && newLen > 1e-5f) {
            Vector3 uRef = Vec3_Scale(refDir, 1.0f / refLen);
            Vector3 uNew = Vec3_Scale(x->newDir, 1.0f / newLen);
            Vector3 axis = Vec3_Cross(uRef, uNew);
            float axisLen = Vec3_Length(axis);
            float dot = Vec3_Dot(uRef, uNew);
            if (axisLen > 1e-5f) {
                Vector3 n = Vec3_Scale(axis, 1.0f / axisLen);
                float cVal = dot;
                float sVal = axisLen;
                float oneMinusC = 1.0f - cVal;

                x->rotRow[0] = Vec3_Create(
                    cVal + n.x * n.x * oneMinusC,
                    n.x * n.y * oneMinusC - n.z * sVal,
                    n.x * n.z * oneMinusC + n.y * sVal
                );
                x->rotRow[1] = Vec3_Create(
                    n.y * n.x * oneMinusC + n.z * sVal,
                    cVal + n.y * n.y * oneMinusC,
                    n.y * n.z * oneMinusC - n.x * sVal
                );
                x->rotRow[2] = Vec3_Create(
                    n.z * n.x * oneMinusC - n.y * sVal,
                    n.z * n.y * oneMinusC + n.x * sVal,
                    cVal + n.z * n.z * oneMinusC
                );
                x->hasRotation = true;
            }
        }
    }

    /* 2. Deformar vértices y actualizar normales */
    for (size_t v = 0; v < morph->vertexCount; ++v) {
        const LizardMorphBinding* b = &morph->bindings[v];
        Vector3 newPos = Vec3_Zero();
        Vector3 newNorm = Vec3_Zero();
        Vector3 baseNorm = morph->baseNormals[v];

        for (int k = 0; k < 2; ++k) {
            if (b->weight[k] <= 0.0f) continue;
            uint16_t cIdx = b->connIndex[k];
            const ConnTransform* x = &xforms[cIdx];

            float t = b->projT[k];
            Vector3 newProj = Vec3_Add(x->newA, Vec3_Scale(x->newDir, t));
            float radScale = (1.0f - t) * x->radScaleA + t * x->radScaleB;

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

    return true;
}
