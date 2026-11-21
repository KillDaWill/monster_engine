/**
 * @file Membrane.c
 * @brief Implementación genérica de membranas interdigitales y patagios ligados al grafo anatómico.
 */
#include "Membrane.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MEMBRANE_MAX_CHAIN_NODES 16
#define MEMBRANE_GRID_U 10
#define MEMBRANE_GRID_V 8

typedef struct MembranePoint {
    Vector3 pos;
    float radius;
    AnatomyId nodeId;
    int jointIndex;
} MembranePoint;

typedef struct MembraneChain {
    MembranePoint pts[MEMBRANE_MAX_CHAIN_NODES];
    size_t count;
} MembraneChain;

typedef struct ResolvedPanel {
    MembraneChain chainA;
    MembraneChain chainB;
    float coverage;
    float thickness;
    float edgeConcavity;
    float camber;
    float fold;
    Color color;
} ResolvedPanel;

static int FindJointForNode(const AnatomyGraph* graph, const Skeleton* skeleton, AnatomyId id) {
    if (!skeleton || !id) return -1;
    int j = Skeleton_FindJoint(skeleton, id);
    if (j >= 0) return j;
    if (!graph) return -1;
    /* Recorrer conexiones incidentes hacia un ancestro esquelético válido. */
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const BodyConnection* c = &graph->connections[i];
        if (c->toId == id) {
            int pj = Skeleton_FindJoint(skeleton, c->fromId);
            if (pj >= 0) return pj;
        }
    }
    return skeleton->jointCount > 0 ? 0 : -1;
}

static bool PushNode(MembraneChain* ch, const AnatomyGraph* graph, const Skeleton* skel, AnatomyId id) {
    const AnatomyNode* node = AnatomyGraph_FindNode(graph, id);
    if (!node || ch->count >= MEMBRANE_MAX_CHAIN_NODES) return false;
    MembranePoint p;
    p.pos = node->center;
    p.radius = fmaxf(node->widthRadius, node->heightRadius);
    p.nodeId = id;
    p.jointIndex = FindJointForNode(graph, skel, id);
    ch->pts[ch->count++] = p;
    return true;
}

static bool BuildChainBFS(const AnatomyGraph* graph, const Skeleton* skel,
                          AnatomyId startId, AnatomyId endId, MembraneChain* out) {
    memset(out, 0, sizeof(*out));
    if (!graph || !startId || !endId) return false;
    if (startId == endId) {
        return PushNode(out, graph, skel, startId);
    }
    size_t n = graph->nodeCount;
    if (n == 0 || n > 512) return false;
    int startIdx = -1, endIdx = -1;
    for (size_t i = 0; i < n; ++i) {
        if (graph->nodes[i].id == startId) startIdx = (int)i;
        if (graph->nodes[i].id == endId) endIdx = (int)i;
    }
    if (startIdx < 0 || endIdx < 0) return false;

    int prev[512];
    int queue[512];
    for (size_t i = 0; i < n; ++i) prev[i] = -2;
    int head = 0, tail = 0;
    queue[tail++] = startIdx;
    prev[startIdx] = -1;

    while (head < tail) {
        int cur = queue[head++];
        if (cur == endIdx) break;
        AnatomyId cid = graph->nodes[cur].id;
        for (size_t e = 0; e < graph->connectionCount; ++e) {
            const BodyConnection* c = &graph->connections[e];
            AnatomyId nextId = 0;
            if (c->fromId == cid) nextId = c->toId;
            else if (c->toId == cid) nextId = c->fromId;
            if (!nextId) continue;
            for (size_t k = 0; k < n; ++k) {
                if (graph->nodes[k].id == nextId && prev[k] == -2) {
                    prev[k] = cur;
                    queue[tail++] = (int)k;
                    break;
                }
            }
        }
    }
    if (prev[endIdx] == -2) {
        /* Respaldo directo si los dos puntos no comparten arista inmediata. */
        return PushNode(out, graph, skel, startId) && PushNode(out, graph, skel, endId);
    }
    int path[512];
    size_t pathLen = 0;
    for (int cur = endIdx; cur >= 0 && pathLen < 512; cur = prev[cur]) {
        path[pathLen++] = cur;
    }
    for (size_t i = 0; i < pathLen && out->count < MEMBRANE_MAX_CHAIN_NODES; ++i) {
        size_t rev = pathLen - 1 - i;
        PushNode(out, graph, skel, graph->nodes[path[rev]].id);
    }
    return out->count >= 1;
}

static void EvalChainAt(const MembraneChain* ch, float t, Vector3* outPos, float* outRadius,
                        int* outJ0, int* outJ1, float* outAlpha) {
    t = Math_Clamp01(t);
    if (ch->count == 0) {
        *outPos = Vec3_Zero(); *outRadius = 0.01f; *outJ0 = 0; *outJ1 = 0; *outAlpha = 0;
        return;
    }
    if (ch->count == 1) {
        *outPos = ch->pts[0].pos;
        *outRadius = ch->pts[0].radius;
        *outJ0 = *outJ1 = ch->pts[0].jointIndex;
        *outAlpha = 0.0f;
        return;
    }
    float scaled = t * (float)(ch->count - 1);
    size_t idx = (size_t)floorf(scaled);
    if (idx >= ch->count - 1) idx = ch->count - 2;
    float alpha = scaled - (float)idx;
    const MembranePoint* a = &ch->pts[idx];
    const MembranePoint* b = &ch->pts[idx + 1];
    *outPos = Vec3_Lerp(a->pos, b->pos, alpha);
    *outRadius = Math_Lerp(a->radius, b->radius, alpha);
    *outJ0 = a->jointIndex;
    *outJ1 = b->jointIndex;
    *outAlpha = alpha;
}

static void AddSkinInfluence(AnatomySkinBinding* b, int joint, float weight) {
    if (joint < 0 || weight <= 1e-6f) return;
    for (int i = 0; i < 2 * ANATOMY_MORPH_INFLUENCES; ++i) {
        if (b->weights[i] > 0.0f && b->joints[i] == joint) {
            b->weights[i] += weight;
            return;
        }
        if (b->weights[i] <= 0.0f) {
            b->joints[i] = joint;
            b->weights[i] = weight;
            return;
        }
    }
}

static void AddDigitChain(const AnatomyGraph* graph, const Skeleton* skel, uint32_t moduleId,
                          bool plantigrade, unsigned digitIdx, unsigned phalanges, MembraneChain* out) {
    memset(out, 0, sizeof(*out));
    AnatomyId rootId = Anatomy_MakeId(moduleId, plantigrade ? LIMB_NODE_PALM_BALL : LIMB_NODE_AUTOPOD);
    if (!AnatomyGraph_FindNode(graph, rootId)) {
        rootId = Anatomy_MakeId(moduleId, LIMB_NODE_AUTOPOD);
    }
    PushNode(out, graph, skel, rootId);
    unsigned count = phalanges + 2;
    for (unsigned j = 0; j < count; ++j) {
        AnatomyId did = Anatomy_MakeId(moduleId, Limb_DigitLocalId(digitIdx, j));
        if (AnatomyGraph_FindNode(graph, did)) {
            PushNode(out, graph, skel, did);
        }
    }
}

static bool GatherPanels(const Monster* monster, const Skeleton* skel,
                         ResolvedPanel** outPanels, size_t* outCount) {
    const CreaturePhenotype* p = &monster->phenotype;
    const AnatomyGraph* g = &monster->anatomyGraph;
    size_t cap = p->membraneCount + CREATURE_MAX_LIMBS * (CREATURE_MAX_DIGITS - 1);
    ResolvedPanel* panels = cap ? calloc(cap, sizeof(*panels)) : NULL;
    if (cap && !panels) return false;
    size_t count = 0;

    float s = fmaxf(0.01f, p->axial.totalScale);

    /* 1. Membranas interdigitales implícitas de extremidades con digitCount >= 2. */
    for (size_t mi = 0; mi < monster->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &monster->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_LIMB || mod->phenotypeIndex >= p->limbCount) continue;
        const LimbPhenotype* limb = &p->limbs[mod->phenotypeIndex];
        float dev = limb->development * p->development.appendages;
        if (limb->webbingCoverage <= 0.01f || limb->digitCount < 2 || dev <= 0.05f) continue;

        bool plantigrade = limb->anatomyProfile == LIMB_PROFILE_PLANTIGRADE;
        float effCov = Math_Clamp01(limb->webbingCoverage * dev * (1.0f - 0.62f * limb->webbingFold));
        if (effCov <= 0.02f) continue;

        Color baseCol = p->surface.regionalAlbedo[SURFACE_REGION_DIGIT].a > 0
            ? p->surface.regionalAlbedo[SURFACE_REGION_DIGIT]
            : p->surface.pigment.secondaryColor;
        Color webCol = Color_Lerp(baseCol, Color_FromRGB(38, 34, 38), 0.35f);

        for (unsigned d = 0; d + 1 < limb->digitCount; ++d) {
            /* En alas quiropteras el dedo 0 (pulgar libre) no lleva patagio interdigital al dedo II. */
            if (limb->archetype == LIMB_ARCHETYPE_WING && d == 0) continue;
            if (limb->digitLengths[d] <= 0.01f || limb->digitLengths[d + 1] <= 0.01f) continue;
            ResolvedPanel rp;
            memset(&rp, 0, sizeof(rp));
            AddDigitChain(g, skel, mod->instanceId, plantigrade, d, limb->digitPhalanges[d], &rp.chainA);
            AddDigitChain(g, skel, mod->instanceId, plantigrade, d + 1, limb->digitPhalanges[d + 1], &rp.chainB);
            if (rp.chainA.count < 2 || rp.chainB.count < 2) continue;

            float meanR = 0.5f * (rp.chainA.pts[rp.chainA.count - 1].radius +
                                  rp.chainB.pts[rp.chainB.count - 1].radius);
            rp.coverage = effCov;
            rp.thickness = fmaxf(0.002f * s, limb->webbingThickness * meanR);
            rp.edgeConcavity = limb->webbingEdgeCurve;
            rp.camber = 0.012f * meanR;
            rp.fold = limb->webbingFold;
            rp.color = webCol;
            panels[count++] = rp;
        }
    }

    /* 2. Patagios/membranas explícitas del fenotipo (dactylopatagio, plagiopatagio, etc.). */
    for (size_t i = 0; i < p->membraneCount; ++i) {
        const AnatomicalMembraneProfile* m = &p->membranes[i];
        float dev = m->development * p->development.appendages;
        if (m->coverage <= 0.01f || dev <= 0.05f) continue;

        uint32_t modAEnd = m->moduleAEndInstanceId ? m->moduleAEndInstanceId : m->moduleAInstanceId;
        uint32_t modBEnd = m->moduleBEndInstanceId ? m->moduleBEndInstanceId : m->moduleBInstanceId;
        AnatomyId sA = Anatomy_MakeId(m->moduleAInstanceId, m->chainAStartNodeId);
        AnatomyId eA = Anatomy_MakeId(modAEnd, m->chainAEndNodeId);
        AnatomyId sB = Anatomy_MakeId(m->moduleBInstanceId, m->chainBStartNodeId);
        AnatomyId eB = Anatomy_MakeId(modBEnd, m->chainBEndNodeId);

        ResolvedPanel rp;
        memset(&rp, 0, sizeof(rp));
        if (!BuildChainBFS(g, skel, sA, eA, &rp.chainA)) continue;
        if (!BuildChainBFS(g, skel, sB, eB, &rp.chainB)) continue;
        rp.coverage = Math_Clamp01(m->coverage * dev);
        rp.thickness = fmaxf(0.002f * s, m->thickness * s * (0.4f + 0.6f * dev));
        rp.edgeConcavity = m->edgeConcavity;
        rp.camber = m->camber * s;
        rp.fold = 1.0f - m->tension;
        rp.color = m->color.a > 0 ? m->color : Color_FromRGB(74, 56, 48);
        panels[count++] = rp;
    }

    *outPanels = panels;
    *outCount = count;
    return true;
}

static Vector3 PanelSampleMid(const ResolvedPanel* rp, float u, float v, float* outRadius,
                              AnatomySkinBinding* outSkin, bool wantSkin) {
    float scallop = rp->edgeConcavity * 4.0f * u * (1.0f - u) * powf(v, 1.35f);
    float vEff = Math_Clamp01(v * rp->coverage * (1.0f - scallop));
    Vector3 pA, pB;
    float rA, rB;
    int jA0, jA1, jB0, jB1;
    float aA, aB;
    EvalChainAt(&rp->chainA, vEff, &pA, &rA, &jA0, &jA1, &aA);
    EvalChainAt(&rp->chainB, vEff, &pB, &rB, &jB0, &jB1, &aB);

    Vector3 center = Vec3_Lerp(pA, pB, u);
    if (outRadius) *outRadius = Math_Lerp(rA, rB, u);

    if (wantSkin && outSkin) {
        memset(outSkin, 0, sizeof(*outSkin));
        AddSkinInfluence(outSkin, jA0, (1.0f - u) * (1.0f - aA));
        AddSkinInfluence(outSkin, jA1, (1.0f - u) * aA);
        AddSkinInfluence(outSkin, jB0, u * (1.0f - aB));
        AddSkinInfluence(outSkin, jB1, u * aB);
        float sum = 0.0f;
        for (int k = 0; k < 2 * ANATOMY_MORPH_INFLUENCES; ++k) sum += outSkin->weights[k];
        if (sum > 1e-6f) {
            for (int k = 0; k < 2 * ANATOMY_MORPH_INFLUENCES; ++k) outSkin->weights[k] /= sum;
        }
    }
    return center;
}

static bool EmitPanels(const Monster* monster, const ResolvedPanel* panels, size_t panelCount,
                       bool wantSkin, Mesh* outMesh, AnatomySkinBinding** outSkin) {
    Mesh_Clear(outMesh);
    if (!panelCount) {
        if (outSkin) *outSkin = NULL;
        return true;
    }

    const size_t rows = MEMBRANE_GRID_U + 1;
    const size_t cols = MEMBRANE_GRID_V + 1;
    const size_t sheetVerts = rows * cols;
    const size_t vertsPerPanel = 2 * sheetVerts;
    const size_t sheetTris = 2 * MEMBRANE_GRID_U * MEMBRANE_GRID_V;
    const size_t rimQuads = 2 * (MEMBRANE_GRID_U + MEMBRANE_GRID_V);
    const size_t trisPerPanel = 2 * sheetTris + 2 * rimQuads;

    size_t totalVerts = panelCount * vertsPerPanel;
    size_t totalIndices = panelCount * trisPerPanel * 3;
    if (!Mesh_ReserveVertices(outMesh, totalVerts) || !Mesh_ReserveIndices(outMesh, totalIndices)) {
        return false;
    }
    AnatomySkinBinding* skin = wantSkin ? calloc(totalVerts, sizeof(*skin)) : NULL;
    if (wantSkin && !skin) return false;

    for (size_t pi = 0; pi < panelCount; ++pi) {
        const ResolvedPanel* rp = &panels[pi];
        size_t baseV = outMesh->vertexCount;

        Vector3 midPts[MEMBRANE_GRID_U + 1][MEMBRANE_GRID_V + 1];
        Vector3 normals[MEMBRANE_GRID_U + 1][MEMBRANE_GRID_V + 1];
        float radii[MEMBRANE_GRID_U + 1][MEMBRANE_GRID_V + 1];
        AnatomySkinBinding cellSkin[MEMBRANE_GRID_U + 1][MEMBRANE_GRID_V + 1];

        for (size_t iu = 0; iu <= MEMBRANE_GRID_U; ++iu) {
            float u = (float)iu / (float)MEMBRANE_GRID_U;
            for (size_t iv = 0; iv <= MEMBRANE_GRID_V; ++iv) {
                float v = (float)iv / (float)MEMBRANE_GRID_V;
                midPts[iu][iv] = PanelSampleMid(rp, u, v, &radii[iu][iv], &cellSkin[iu][iv], wantSkin);
            }
        }

        /* Normales locales y arqueamiento / plegamiento por diferenciación finita en malla. */
        for (size_t iu = 0; iu <= MEMBRANE_GRID_U; ++iu) {
            size_t u0 = iu > 0 ? iu - 1 : 0;
            size_t u1 = iu < MEMBRANE_GRID_U ? iu + 1 : MEMBRANE_GRID_U;
            float u = (float)iu / (float)MEMBRANE_GRID_U;
            for (size_t iv = 0; iv <= MEMBRANE_GRID_V; ++iv) {
                size_t v0 = iv > 0 ? iv - 1 : 0;
                size_t v1 = iv < MEMBRANE_GRID_V ? iv + 1 : MEMBRANE_GRID_V;
                float v = (float)iv / (float)MEMBRANE_GRID_V;
                Vector3 du = Vec3_Sub(midPts[u1][iv], midPts[u0][iv]);
                Vector3 dv = Vec3_Sub(midPts[iu][v1], midPts[iu][v0]);
                Vector3 n = Vec3_Cross(du, dv);
                if (Vec3_LengthSq(n) < 1e-10f) n = Vec3_Create(0, 1, 0);
                else n = Vec3_Normalize(n);
                normals[iu][iv] = n;

                float interior = sinf(3.14159265f * u) * sinf(3.14159265f * v);
                float offset = rp->camber * interior;
                if (rp->fold > 0.02f) {
                    offset += rp->fold * rp->thickness * 1.4f * sinf(6.2831853f * u) * interior;
                }
                midPts[iu][iv] = Vec3_Add(midPts[iu][iv], Vec3_Scale(n, offset));
            }
        }

        for (int side = 0; side < 2; ++side) {
            float sign = side == 0 ? 1.0f : -1.0f;
            for (size_t iu = 0; iu <= MEMBRANE_GRID_U; ++iu) {
                float u = (float)iu / (float)MEMBRANE_GRID_U;
                float edgeFactor = sinf(3.14159265f * u);
                for (size_t iv = 0; iv <= MEMBRANE_GRID_V; ++iv) {
                    float v = (float)iv / (float)MEMBRANE_GRID_V;
                    float edgeTaper = (1.0f - 0.35f * v) * (0.65f + 0.35f * edgeFactor);
                    float h = 0.5f * rp->thickness * edgeTaper;
                    Vector3 n = Vec3_Scale(normals[iu][iv], sign);
                    Vector3 pos = Vec3_Add(midPts[iu][iv], Vec3_Scale(normals[iu][iv], sign * h));

                    MeshVertex mv;
                    memset(&mv, 0, sizeof(mv));
                    mv.position = pos;
                    mv.normal = n;
                    mv.color = Color_Lerp(rp->color, Color_FromRGB(92, 68, 56), 0.14f * edgeFactor * v);
                    mv.material = SDF_MATERIAL_NASAL_PAD;
                    mv.surface.region = SURFACE_REGION_ORAL;
                    mv.surface.secondaryRegion = SURFACE_REGION_ORAL;
                    mv.surface.blend = 0.0f;
                    mv.surface.position = pos;
                    mv.surface.normal = n;
                    mv.surface.flowDirection = Vec3_Create(0, 0, 1);
                    mv.surface.ventral = 0.0f;
                    mv.surface.integumentMask = 0.0f;

                    size_t idx = outMesh->vertexCount;
                    outMesh->vertices[outMesh->vertexCount++] = mv;
                    if (wantSkin) skin[idx] = cellSkin[iu][iv];
                }
            }
        }

        /* Triángulos de cara superior e inferior. */
        for (size_t iu = 0; iu < MEMBRANE_GRID_U; ++iu) {
            for (size_t iv = 0; iv < MEMBRANE_GRID_V; ++iv) {
                MeshIndex t00 = (MeshIndex)(baseV + iu * cols + iv);
                MeshIndex t10 = (MeshIndex)(baseV + (iu + 1) * cols + iv);
                MeshIndex t01 = (MeshIndex)(baseV + iu * cols + (iv + 1));
                MeshIndex t11 = (MeshIndex)(baseV + (iu + 1) * cols + (iv + 1));
                outMesh->indices[outMesh->indexCount++] = t00;
                outMesh->indices[outMesh->indexCount++] = t10;
                outMesh->indices[outMesh->indexCount++] = t11;
                outMesh->indices[outMesh->indexCount++] = t00;
                outMesh->indices[outMesh->indexCount++] = t11;
                outMesh->indices[outMesh->indexCount++] = t01;

                MeshIndex b00 = (MeshIndex)(baseV + sheetVerts + iu * cols + iv);
                MeshIndex b10 = (MeshIndex)(baseV + sheetVerts + (iu + 1) * cols + iv);
                MeshIndex b01 = (MeshIndex)(baseV + sheetVerts + iu * cols + (iv + 1));
                MeshIndex b11 = (MeshIndex)(baseV + sheetVerts + (iu + 1) * cols + (iv + 1));
                outMesh->indices[outMesh->indexCount++] = b00;
                outMesh->indices[outMesh->indexCount++] = b11;
                outMesh->indices[outMesh->indexCount++] = b10;
                outMesh->indices[outMesh->indexCount++] = b00;
                outMesh->indices[outMesh->indexCount++] = b01;
                outMesh->indices[outMesh->indexCount++] = b11;
            }
        }

        /* Cierre perimetral hermético (u=0, u=1, v=0, v=1). */
        for (size_t iv = 0; iv < MEMBRANE_GRID_V; ++iv) {
            MeshIndex ta = (MeshIndex)(baseV + 0 * cols + iv);
            MeshIndex tb = (MeshIndex)(baseV + 0 * cols + (iv + 1));
            MeshIndex ba = (MeshIndex)(baseV + sheetVerts + 0 * cols + iv);
            MeshIndex bb = (MeshIndex)(baseV + sheetVerts + 0 * cols + (iv + 1));
            outMesh->indices[outMesh->indexCount++] = ta;
            outMesh->indices[outMesh->indexCount++] = tb;
            outMesh->indices[outMesh->indexCount++] = bb;
            outMesh->indices[outMesh->indexCount++] = ta;
            outMesh->indices[outMesh->indexCount++] = bb;
            outMesh->indices[outMesh->indexCount++] = ba;

            MeshIndex t2a = (MeshIndex)(baseV + MEMBRANE_GRID_U * cols + iv);
            MeshIndex t2b = (MeshIndex)(baseV + MEMBRANE_GRID_U * cols + (iv + 1));
            MeshIndex b2a = (MeshIndex)(baseV + sheetVerts + MEMBRANE_GRID_U * cols + iv);
            MeshIndex b2b = (MeshIndex)(baseV + sheetVerts + MEMBRANE_GRID_U * cols + (iv + 1));
            outMesh->indices[outMesh->indexCount++] = t2a;
            outMesh->indices[outMesh->indexCount++] = b2b;
            outMesh->indices[outMesh->indexCount++] = t2b;
            outMesh->indices[outMesh->indexCount++] = t2a;
            outMesh->indices[outMesh->indexCount++] = b2a;
            outMesh->indices[outMesh->indexCount++] = b2b;
        }
        for (size_t iu = 0; iu < MEMBRANE_GRID_U; ++iu) {
            MeshIndex ta = (MeshIndex)(baseV + iu * cols + 0);
            MeshIndex tb = (MeshIndex)(baseV + (iu + 1) * cols + 0);
            MeshIndex ba = (MeshIndex)(baseV + sheetVerts + iu * cols + 0);
            MeshIndex bb = (MeshIndex)(baseV + sheetVerts + (iu + 1) * cols + 0);
            outMesh->indices[outMesh->indexCount++] = ta;
            outMesh->indices[outMesh->indexCount++] = bb;
            outMesh->indices[outMesh->indexCount++] = tb;
            outMesh->indices[outMesh->indexCount++] = ta;
            outMesh->indices[outMesh->indexCount++] = ba;
            outMesh->indices[outMesh->indexCount++] = bb;

            MeshIndex t2a = (MeshIndex)(baseV + iu * cols + MEMBRANE_GRID_V);
            MeshIndex t2b = (MeshIndex)(baseV + (iu + 1) * cols + MEMBRANE_GRID_V);
            MeshIndex b2a = (MeshIndex)(baseV + sheetVerts + iu * cols + MEMBRANE_GRID_V);
            MeshIndex b2b = (MeshIndex)(baseV + sheetVerts + (iu + 1) * cols + MEMBRANE_GRID_V);
            outMesh->indices[outMesh->indexCount++] = t2a;
            outMesh->indices[outMesh->indexCount++] = t2b;
            outMesh->indices[outMesh->indexCount++] = b2b;
            outMesh->indices[outMesh->indexCount++] = t2a;
            outMesh->indices[outMesh->indexCount++] = b2b;
            outMesh->indices[outMesh->indexCount++] = b2a;
        }
    }

    if (monster->hasSurface) {
        SurfacePhenotype flat = monster->surface;
        flat.integument.type = INTEGUMENT_SMOOTH_SKIN;
        flat.pigment.patternStrength = 0.0f;
        flat.pigment.layerCount = 0;
        flat.tuftCount = 0;
        flat.longHairCount = 0;
        outMesh->surfaceRecipe = SurfaceRecipe_CompileScaled(&flat, monster->surfaceMapping.unitScale);
        outMesh->hasSurface = true;
    } else {
        outMesh->hasSurface = false;
    }
    Mesh_MarkGeometryChanged(outMesh);
    if (outSkin) *outSkin = skin;
    return true;
}

bool Membrane_BuildMesh(const Monster* monster, Mesh* outMesh) {
    if (!monster || !outMesh) return false;
    ResolvedPanel* panels = NULL;
    size_t count = 0;
    if (!GatherPanels(monster, NULL, &panels, &count)) return false;
    bool ok = EmitPanels(monster, panels, count, false, outMesh, NULL);
    free(panels);
    return ok;
}

bool Membrane_BuildAndBind(const Monster* monster, const Skeleton* skeleton,
                           Mesh* outMesh, AnatomyDeformer* deformer) {
    if (!monster || !outMesh) return false;
    ResolvedPanel* panels = NULL;
    size_t count = 0;
    if (!GatherPanels(monster, skeleton, &panels, &count)) return false;
    AnatomySkinBinding* skin = NULL;
    bool ok = EmitPanels(monster, panels, count, skeleton != NULL && deformer != NULL, outMesh, &skin);
    if (ok && deformer && skeleton && outMesh->vertexCount > 0 && skin) {
        ok = AnatomyDeformer_BindExplicitSkin(deformer, outMesh, &monster->anatomyGraph, skeleton, skin);
    }
    free(skin);
    free(panels);
    return ok;
}

bool Membrane_BindDeformer(const Monster* monster, const Skeleton* skeleton,
                           const Mesh* mesh, AnatomyDeformer* deformer) {
    if (!monster || !mesh || !deformer) return false;
    if (mesh->vertexCount == 0 || !skeleton) return true;
    ResolvedPanel* panels = NULL;
    size_t count = 0;
    if (!GatherPanels(monster, skeleton, &panels, &count)) return false;
    const size_t vertsPerPanel = 2 * (MEMBRANE_GRID_U + 1) * (MEMBRANE_GRID_V + 1);
    if (count * vertsPerPanel != mesh->vertexCount) {
        free(panels);
        return false;
    }
    AnatomySkinBinding* skin = calloc(mesh->vertexCount, sizeof(*skin));
    if (!skin) { free(panels); return false; }
    size_t cursor = 0;
    for (size_t pi = 0; pi < count; ++pi) {
        AnatomySkinBinding cellSkin[MEMBRANE_GRID_U + 1][MEMBRANE_GRID_V + 1];
        for (size_t iu = 0; iu <= MEMBRANE_GRID_U; ++iu) {
            float u = (float)iu / (float)MEMBRANE_GRID_U;
            for (size_t iv = 0; iv <= MEMBRANE_GRID_V; ++iv) {
                float v = (float)iv / (float)MEMBRANE_GRID_V;
                PanelSampleMid(&panels[pi], u, v, NULL, &cellSkin[iu][iv], true);
            }
        }
        for (int side = 0; side < 2; ++side) {
            for (size_t iu = 0; iu <= MEMBRANE_GRID_U; ++iu) {
                for (size_t iv = 0; iv <= MEMBRANE_GRID_V; ++iv) {
                    skin[cursor++] = cellSkin[iu][iv];
                }
            }
        }
    }
    bool ok = AnatomyDeformer_BindExplicitSkin(deformer, mesh, &monster->anatomyGraph, skeleton, skin);
    free(skin);
    free(panels);
    return ok;
}
