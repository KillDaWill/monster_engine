/**
 * @file SupplementaryMesh.c
 * @brief Implementación genérica de plumas aviares, aletas batoideas, ventosas, alas de insecto y apéndices repetidos.
 */
#include "SupplementaryMesh.h"
#include "AxialBody.h"
#include "AxialBodyMedusa.h"
#include "FlexibleAppendage.h"
#include "Limb.h"
#include "MathUtils.h"
#include "Surface.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct SkinBuffer {
    AnatomySkinBinding* items;
    size_t count;
    size_t capacity;
} SkinBuffer;

typedef struct BuildTarget {
    Mesh* mesh;
    SkinBuffer* skin;
    const Skeleton* skeleton;
    const AnatomyGraph* graph;
} BuildTarget;

static bool SkinBuffer_Push(SkinBuffer* buf, AnatomySkinBinding b) {
    if (!buf) return true;
    if (buf->count >= buf->capacity) {
        size_t next = buf->capacity ? buf->capacity * 2 : 256;
        AnatomySkinBinding* mem = realloc(buf->items, next * sizeof(*mem));
        if (!mem) return false;
        buf->items = mem;
        buf->capacity = next;
    }
    buf->items[buf->count++] = b;
    return true;
}

static int FindJointSafe(const AnatomyGraph* g, const Skeleton* skel, AnatomyId id) {
    if (!skel || !skel->jointCount) return -1;
    if (id) {
        int j = Skeleton_FindJoint(skel, id);
        if (j >= 0) return j;
        if (g) {
            for (size_t i = 0; i < g->connectionCount; ++i) {
                if (g->connections[i].toId == id) {
                    int pj = Skeleton_FindJoint(skel, g->connections[i].fromId);
                    if (pj >= 0) return pj;
                }
            }
        }
    }
    return 0;
}

static AnatomySkinBinding MakeSkin1(int j0) {
    AnatomySkinBinding b;
    memset(&b, 0, sizeof(b));
    if (j0 >= 0) { b.joints[0] = j0; b.weights[0] = 1.0f; }
    return b;
}

static AnatomySkinBinding MakeSkin2(int j0, float w0, int j1, float w1) {
    AnatomySkinBinding b;
    memset(&b, 0, sizeof(b));
    if (j0 < 0 && j1 < 0) return b;
    if (j0 < 0 || w0 <= 1e-5f) { b.joints[0] = j1; b.weights[0] = 1.0f; return b; }
    if (j1 < 0 || w1 <= 1e-5f || j0 == j1) { b.joints[0] = j0; b.weights[0] = 1.0f; return b; }
    float sum = w0 + w1;
    b.joints[0] = j0; b.weights[0] = w0 / sum;
    b.joints[1] = j1; b.weights[1] = w1 / sum;
    return b;
}

static float Hash01(uint64_t h) {
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    h *= UINT64_C(0xc4ceb9fe1a85ec53);
    h ^= h >> 33;
    return (float)(h & 0xffffffu) / (float)0x1000000u;
}

static bool EmitVertex(BuildTarget* t, Vector3 pos, Vector3 nrm, Color col,
                       SurfaceRegion region, float ventral, AnatomySkinBinding skin,
                       MeshIndex* outIdx) {
    if (!isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z)) return false;
    float nl = Vec3_Length(nrm);
    nrm = nl > 1e-6f ? Vec3_Scale(nrm, 1.0f / nl) : Vec3_Create(0, 1, 0);
    if (t->mesh) {
        MeshVertex v;
        memset(&v, 0, sizeof(v));
        v.position = pos;
        v.normal = nrm;
        v.color = col;
        v.material = SDF_MATERIAL_SKIN;
        v.surface.position = pos;
        v.surface.normal = nrm;
        v.surface.flowDirection = Vec3_Create(0, 0, -1);
        v.surface.region = (float)region;
        v.surface.secondaryRegion = (float)region;
        v.surface.blend = 0.0f;
        v.surface.ventral = Math_Clamp01(ventral);
        v.surface.integumentMask = 0.0f;
        if (!Mesh_AddVertex(t->mesh, v, outIdx)) return false;
    } else if (outIdx) {
        *outIdx = (MeshIndex)(t->skin ? t->skin->count : 0);
    }
    return SkinBuffer_Push(t->skin, skin);
}

static bool EmitTri(BuildTarget* t, MeshIndex a, MeshIndex b, MeshIndex c) {
    if (!t->mesh) return true;
    if (a == b || b == c || a == c) return true;
    if (!Mesh_TriangleHasArea(t->mesh->vertices[a].position,
                              t->mesh->vertices[b].position,
                              t->mesh->vertices[c].position)) return true;
    return Mesh_AddTriangle(t->mesh, a, b, c);
}

static bool EmitQuad(BuildTarget* t, MeshIndex a, MeshIndex b, MeshIndex c, MeshIndex d) {
    return EmitTri(t, a, b, c) && EmitTri(t, a, c, d);
}

/* ============================================================================
 * 1. PLUMAS AVIARES (PRIMARIAS, SECUNDARIAS, COBERTERAS Y ABANICO CAUDAL)
 * ============================================================================ */

static bool EmitFeatherBlade(BuildTarget* t, Vector3 root, Vector3 dir, Vector3 widthAxis,
                             Vector3 upNormal, float length, float halfWidth, float camber,
                             Color dorsalBase, Color dorsalTip, Color ventralCol,
                             SurfaceRegion region, AnatomySkinBinding skinRoot,
                             AnatomySkinBinding skinTip) {
    if (length <= 1e-5f || halfWidth <= 1e-5f) return true;
    dir = Vec3_Normalize(dir);
    upNormal = Vec3_Normalize(upNormal);
    widthAxis = Vec3_Normalize(Vec3_Sub(widthAxis, Vec3_Scale(dir, Vec3_Dot(widthAxis, dir))));
    if (Vec3_LengthSq(widthAxis) < 1e-6f) widthAxis = Vec3_Normalize(Vec3_Cross(upNormal, dir));
    upNormal = Vec3_Normalize(Vec3_Cross(dir, widthAxis));

    const unsigned segs = 5;
    float thick = fmaxf(halfWidth * 0.14f, length * 0.008f);
    MeshIndex topL[6], topM[6], topR[6], botL[6], botM[6], botR[6];

    for (unsigned i = 0; i <= segs; ++i) {
        float u = (float)i / (float)segs;
        /* Perfil asimétrico aerodinámico con punta redondeada continua (evita borde posterior en sierra). */
        float env = sinf(powf(u, 0.56f) * 3.14159265f);
        float wLead = halfWidth * (0.34f + 0.56f * env) * (1.0f - 0.14f * u);
        float wTrail = halfWidth * (0.42f + 0.80f * env) * (1.0f - 0.06f * u);
        if (i == segs) { wLead = halfWidth * 0.16f; wTrail = halfWidth * 0.22f; }
        float arch = sinf(u * 3.14159265f) * camber * length - u * u * camber * length * 0.45f;
        float h = thick * (1.0f - 0.65f * u);

        Vector3 c = Vec3_Add(root, Vec3_Add(Vec3_Scale(dir, length * u), Vec3_Scale(upNormal, arch)));
        Vector3 pL = Vec3_Add(c, Vec3_Scale(widthAxis, wLead));
        Vector3 pR = Vec3_Sub(c, Vec3_Scale(widthAxis, wTrail));
        Vector3 pMT = Vec3_Add(c, Vec3_Scale(upNormal, h));
        Vector3 pMB = Vec3_Sub(c, Vec3_Scale(upNormal, h * 0.55f));

        Color cD = Color_Lerp(dorsalBase, dorsalTip, u * u);
        /* Barrado transversal ventral realista en plumas de vuelo, equilibrado con el pecho. */
        float bar = (sinf(u * 28.0f) > 0.05f) ? 0.56f : 0.78f;
        Color cV = (Color){
            (uint8_t)Math_Clamp(ventralCol.r * bar, 0, 255),
            (uint8_t)Math_Clamp(ventralCol.g * bar, 0, 255),
            (uint8_t)Math_Clamp(ventralCol.b * bar, 0, 255), 255};

        AnatomySkinBinding sk = (u < 0.35f) ? skinRoot : skinTip;
        Vector3 nTop = upNormal;
        Vector3 nBot = Vec3_Scale(upNormal, -1.0f);
        if (!EmitVertex(t, pL, nTop, cD, region, 0.0f, sk, &topL[i]) ||
            !EmitVertex(t, pMT, nTop, cD, region, 0.0f, sk, &topM[i]) ||
            !EmitVertex(t, pR, nTop, cD, region, 0.0f, sk, &topR[i]) ||
            !EmitVertex(t, pL, nBot, cV, region, 1.0f, sk, &botL[i]) ||
            !EmitVertex(t, pMB, nBot, cV, region, 1.0f, sk, &botM[i]) ||
            !EmitVertex(t, pR, nBot, cV, region, 1.0f, sk, &botR[i])) {
            return false;
        }
    }
    for (unsigned i = 0; i < segs; ++i) {
        if (!EmitQuad(t, topL[i], topM[i], topM[i + 1], topL[i + 1]) ||
            !EmitQuad(t, topM[i], topR[i], topR[i + 1], topM[i + 1]) ||
            !EmitQuad(t, botM[i], botL[i], botL[i + 1], botM[i + 1]) ||
            !EmitQuad(t, botR[i], botM[i], botM[i + 1], botR[i + 1])) {
            return false;
        }
    }
    return true;
}

static bool BuildAvianWingFeathers(const Monster* m, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;

    Color dorsal = p->surface.pigment.baseColor;
    Color tipDark = Color_FromRGB(
        (uint8_t)(dorsal.r * 0.45f), (uint8_t)(dorsal.g * 0.47f), (uint8_t)(dorsal.b * 0.52f));
    Color covertCol = Color_Lerp(dorsal, p->surface.pigment.secondaryColor, 0.35f);
    Color ventral = p->surface.pigment.ventralColor;

    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_LIMB || mod->phenotypeIndex >= p->limbCount) continue;
        const LimbPhenotype* limb = &p->limbs[mod->phenotypeIndex];
        if (limb->archetype != LIMB_ARCHETYPE_AVIAN || (limb->role != LIMB_WING && limb->role != LIMB_FORE))
            continue;
        float dev = limb->development * p->development.appendages;
        if (dev <= 0.12f) continue;

        const AnatomyNode* sh = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_ROOT);
        const AnatomyNode* el = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_MIDDLE);
        const AnatomyNode* ul = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_DISTAL);
        const AnatomyNode* wr = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_AUTOPOD);
        const AnatomyNode* h0 = AnatomyGraph_FindModuleNode(g, mod->instanceId, Limb_DigitLocalId(0, 0));
        const AnatomyNode* h1 = AnatomyGraph_FindModuleNode(g, mod->instanceId, Limb_DigitLocalId(0, 1));
        const AnatomyNode* h2 = AnatomyGraph_FindModuleNode(g, mod->instanceId, Limb_DigitLocalId(0, 2));
        if (!sh || !el || !ul || !wr || !h0 || !h1 || !h2) continue;

        float side = limb->side == LIMB_RIGHT ? -1.0f : 1.0f;
        float wingSpan = limb->length * s * dev;
        float aspect = limb->wingAspect > 0.3f ? limb->wingAspect : 2.35f;
        float pointed = Math_Clamp01(limb->wingPointedness > 0.01f ? limb->wingPointedness : 0.78f);
        float baseChord = wingSpan / aspect;
        float fold = Math_Clamp01(limb->sprawl);

        int jSh = FindJointSafe(g, opaque->skeleton, sh->id);
        int jEl = FindJointSafe(g, opaque->skeleton, el->id);
        int jUl = FindJointSafe(g, opaque->skeleton, ul->id);
        int jWr = FindJointSafe(g, opaque->skeleton, wr->id);
        int jH0 = FindJointSafe(g, opaque->skeleton, h0->id);
        int jH1 = FindJointSafe(g, opaque->skeleton, h1->id);
        int jH2 = FindJointSafe(g, opaque->skeleton, h2->id);

        Vector3 wingUp = Vec3_Create(0, 1, 0);
        Vector3 wingBack = Vec3_Normalize(Vec3_Create(-side * 0.06f, -0.04f, -1.0f));
        Vector3 handDir = Vec3_Normalize(Vec3_Sub(h2->center, wr->center));
        float ext = 1.0f - fold;

        /* 0. Perfil alar emplumado continuo (propatagio + cubierta dorsal/ventral + raíz escapular):
         * envuelve completamente el esqueleto proximal (húmero/ulna/carpo) formando un borde de ataque
         * aerodinámico continuo desde el hombro hasta la punta, sin varillas óseas desnudas ni huecos. */
        {
            const unsigned nu = 18, nv = 6;
            MeshIndex topGrid[19][7], botGrid[19][7];
            Vector3 rootIn = Vec3_Add(sh->center, Vec3_Create(-side * sh->widthRadius * 0.95f, sh->heightRadius * 0.12f, sh->widthRadius * 0.25f));
            Vector3 wristLead = Vec3_Add(wr->center, Vec3_Create(0.0f, wr->heightRadius * 0.22f, wr->widthRadius * 0.85f * ext));
            Vector3 shLead = Vec3_Add(rootIn, Vec3_Create(0.0f, sh->heightRadius * 0.28f, baseChord * 0.18f * ext));

            for (unsigned iu = 0; iu <= nu; ++iu) {
                float u = (float)iu / (float)nu; /* 0 = raíz torso, 0.56 = carpo, 1.0 = punta mano */
                Vector3 leadPt, bonePt;
                float localThick;
                AnatomySkinBinding sk;
                if (u < 0.28f) {
                    float a = u / 0.28f;
                    bonePt = Vec3_Lerp(rootIn, el->center, a);
                    /* Propatagio continuo: borde anterior recto/suave de raíz escapular hacia muñeca. */
                    leadPt = Vec3_Lerp(shLead, wristLead, u / 0.56f);
                    localThick = Math_Lerp(sh->heightRadius * 1.15f, el->heightRadius * 1.18f, a);
                    sk = MakeSkin2(jSh, 1.0f - a * 0.85f, jEl, a * 0.85f);
                } else if (u < 0.56f) {
                    float a = (u - 0.28f) / 0.28f;
                    bonePt = Vec3_Lerp(el->center, wr->center, a);
                    leadPt = Vec3_Lerp(shLead, wristLead, u / 0.56f);
                    /* Leve arqueamiento convexo del borde de ataque en el antebrazo. */
                    leadPt.z += sinf(u / 0.56f * 3.14159265f) * baseChord * 0.05f * ext;
                    localThick = Math_Lerp(el->heightRadius * 1.18f, wr->heightRadius * 1.25f, a);
                    sk = (a < 0.5f) ? MakeSkin2(jEl, 1.0f - a * 2.0f, jUl, a * 2.0f)
                                    : MakeSkin2(jUl, 1.0f - (a - 0.5f) * 2.0f, jWr, (a - 0.5f) * 2.0f);
                } else {
                    float a = (u - 0.56f) / 0.44f;
                    if (a < 0.34f) {
                        float b = a / 0.34f;
                        bonePt = Vec3_Lerp(wr->center, h0->center, b);
                        sk = MakeSkin2(jWr, 1.0f - b, jH0, b);
                    } else if (a < 0.68f) {
                        float b = (a - 0.34f) / 0.34f;
                        bonePt = Vec3_Lerp(h0->center, h1->center, b);
                        sk = MakeSkin2(jH0, 1.0f - b, jH1, b);
                    } else {
                        float b = (a - 0.68f) / 0.32f;
                        bonePt = Vec3_Lerp(h1->center, h2->center, b);
                        sk = MakeSkin2(jH1, 1.0f - b, jH2, b);
                    }
                    leadPt = Vec3_Lerp(wristLead, h2->center, a);
                    localThick = wr->heightRadius * (1.22f * (1.0f - 0.72f * a));
                }
                /* En postura plegada, la sección envuelve pegada al flanco sin sobresalir lateralmente. */
                Vector3 chordDir = Vec3_Normalize(Vec3_Lerp(
                    wingBack,
                    Vec3_Create(-side * 0.14f, -0.18f, -1.0f),
                    fold));
                if (u > 0.56f) {
                    float a = (u - 0.56f) / 0.44f;
                    chordDir = Vec3_Normalize(Vec3_Lerp(chordDir, handDir, a * 0.25f * ext));
                }
                float coverChord = baseChord * (u < 0.56f
                    ? Math_Lerp(0.70f, 0.58f, u / 0.56f)
                    : Math_Lerp(0.58f, 0.10f, (u - 0.56f) / 0.44f));
                coverChord *= Math_Lerp(0.54f, 1.0f, ext);
                Vector3 leadUse = Vec3_Lerp(bonePt, leadPt, ext);
                Vector3 trailPt = Vec3_Add(leadUse, Vec3_Scale(chordDir, coverChord));
                /* Asegurar que la envoltura cubra el hueso del codo por detrás. */
                float thickScale = Math_Lerp(0.75f, 1.0f, ext);
                Vector3 surfUp = Vec3_Normalize(Vec3_Lerp(
                    Vec3_Create(0.0f, 1.0f, 0.0f),
                    Vec3_Create(side * 0.78f, 0.62f, 0.0f),
                    fold * 0.85f));

                for (unsigned iv = 0; iv <= nv; ++iv) {
                    float v = (float)iv / (float)nv; /* 0 = borde de ataque, 1 = borde de coberteras */
                    Vector3 pt = Vec3_Lerp(leadUse, trailPt, v);
                    /* Perfil NACA aviar: borde anterior lleno redondeado, dorso convexo, afinado atrás. */
                    float foil = sinf(powf(v, 0.58f) * 3.14159265f);
                    float hTop = localThick * (0.28f + 1.05f * foil) * (1.0f - 0.65f * v * v) * thickScale;
                    float hBot = localThick * (0.22f + 0.52f * foil) * (1.0f - 0.75f * v * v) * thickScale;
                    if (iv == 0) { hTop = localThick * 0.18f * thickScale; hBot = hTop; }
                    if (iv == nv) { hTop = localThick * 0.05f * thickScale; hBot = hTop * 0.5f; }

                    Vector3 pTop = Vec3_Add(pt, Vec3_Scale(surfUp, hTop));
                    Vector3 pBot = Vec3_Sub(pt, Vec3_Scale(surfUp, hBot));
                    Color cTop = Color_Lerp(covertCol, dorsal, Math_Clamp01(v * 0.75f + u * 0.25f));
                    if (u > 0.72f) cTop = Color_Lerp(cTop, tipDark, (u - 0.72f) / 0.28f * 0.65f);
                    Color cBot = Color_Lerp(ventral, covertCol, Math_Clamp01(0.18f + 0.14f * (1.0f - v)));
                    Vector3 nTop = Vec3_Normalize(Vec3_Add(surfUp, Vec3_Scale(chordDir, (v - 0.25f) * 0.35f)));
                    Vector3 nBot = Vec3_Scale(surfUp, -1.0f);
                    if (!EmitVertex(opaque, pTop, nTop, cTop, SURFACE_REGION_FORELIMB, 0.0f, sk, &topGrid[iu][iv]) ||
                        !EmitVertex(opaque, pBot, nBot, cBot, SURFACE_REGION_FORELIMB, 1.0f, sk, &botGrid[iu][iv])) {
                        return false;
                    }
                }
            }
            for (unsigned iu = 0; iu < nu; ++iu) {
                for (unsigned iv = 0; iv < nv; ++iv) {
                    if (side > 0.0f) {
                        if (!EmitQuad(opaque, topGrid[iu][iv], topGrid[iu + 1][iv], topGrid[iu + 1][iv + 1], topGrid[iu][iv + 1]) ||
                            !EmitQuad(opaque, botGrid[iu][iv], botGrid[iu][iv + 1], botGrid[iu + 1][iv + 1], botGrid[iu + 1][iv]))
                            return false;
                    } else {
                        if (!EmitQuad(opaque, topGrid[iu][iv], topGrid[iu][iv + 1], topGrid[iu + 1][iv + 1], topGrid[iu + 1][iv]) ||
                            !EmitQuad(opaque, botGrid[iu][iv], botGrid[iu + 1][iv], botGrid[iu + 1][iv + 1], botGrid[iu][iv + 1]))
                            return false;
                    }
                }
            }
        }

        Vector3 featherUp = Vec3_Normalize(Vec3_Lerp(
            wingUp, Vec3_Create(side * 0.82f, 0.56f, 0.0f), fold * 0.85f));

        /* A. Rémiges primarias (10 plumas anchas imbricadas formando punta falcada continua). */
        unsigned nPrim = (limb->featherCount >= 6 && limb->featherCount <= 16) ? limb->featherCount : 10u;
        for (unsigned i = 0; i < nPrim; ++i) {
            float u = (float)i / (float)(nPrim - 1u);
            Vector3 anchor;
            AnatomySkinBinding skR, skT;
            if (u < 0.35f) {
                float a = u / 0.35f;
                anchor = Vec3_Lerp(wr->center, h0->center, a);
                skR = MakeSkin2(jWr, 1.0f - a, jH0, a);
                skT = MakeSkin2(jH0, 0.7f, jH1, 0.3f);
            } else if (u < 0.70f) {
                float a = (u - 0.35f) / 0.35f;
                anchor = Vec3_Lerp(h0->center, h1->center, a);
                skR = MakeSkin2(jH0, 1.0f - a, jH1, a);
                skT = MakeSkin2(jH1, 0.6f, jH2, 0.4f);
            } else {
                float a = (u - 0.70f) / 0.30f;
                anchor = Vec3_Lerp(h1->center, h2->center, a);
                skR = MakeSkin2(jH1, 1.0f - a, jH2, a);
                skT = MakeSkin1(jH2);
            }
            /* Abanico progresivo continuo: P1 enlaza sin escalón con secundarias, P8-P10 forman punta falcada. */
            float fanT = powf(u, 0.86f);
            Vector3 fDir = Vec3_Normalize(Vec3_Lerp(wingBack, handDir, Math_Lerp(0.10f, 0.90f, fanT)));
            if (fold > 0.2f) {
                /* Al plegar, todas las primarias se apilan ordenadamente hacia atrás a lo largo del flanco. */
                Vector3 foldedDir = Vec3_Normalize(Vec3_Create(-side * (0.18f + 0.07f * u), -0.22f - 0.04f * u, -1.0f));
                fDir = Vec3_Normalize(Vec3_Lerp(fDir, foldedDir, Math_Clamp01((fold - 0.2f) / 0.65f)));
            }
            float peak = Math_Lerp(0.55f, 0.80f, pointed);
            float distPeak = fabsf(u - peak);
            float lenFactor = 1.0f - distPeak * Math_Lerp(0.32f, 0.52f, pointed);
            float fLen = baseChord * (0.98f + 0.46f * pointed) * fmaxf(0.72f, lenFactor) * Math_Lerp(1.0f, 0.74f, fold);
            float fHalfW = baseChord * (0.230f - 0.060f * u) * Math_Lerp(1.0f, 0.72f, fold);
            anchor = Vec3_Sub(anchor, Vec3_Scale(featherUp, (float)i * fHalfW * 0.020f));
            Vector3 wAxis = Vec3_Normalize(Vec3_Cross(featherUp, fDir));
            if (!EmitFeatherBlade(opaque, anchor, fDir, wAxis, featherUp, fLen, fHalfW, 0.055f,
                                  dorsal, tipDark, ventral, SURFACE_REGION_FORELIMB, skR, skT))
                return false;
        }

        /* B. Rémiges secundarias (12 plumas anchas superpuestas formando borde posterior suave). */
        unsigned nSec = 12u;
        for (unsigned i = 0; i < nSec; ++i) {
            float u = (float)i / (float)(nSec - 1u);
            Vector3 anchor;
            AnatomySkinBinding skR, skT;
            if (u < 0.5f) {
                float a = u * 2.0f;
                anchor = Vec3_Lerp(el->center, ul->center, a);
                skR = MakeSkin2(jEl, 1.0f - a, jUl, a);
                skT = skR;
            } else {
                float a = (u - 0.5f) * 2.0f;
                anchor = Vec3_Lerp(ul->center, wr->center, a);
                skR = MakeSkin2(jUl, 1.0f - a, jWr, a);
                skT = skR;
            }
            Vector3 fDir = Vec3_Normalize(Vec3_Add(wingBack, Vec3_Create(side * (0.02f + 0.09f * u) * ext - side * 0.16f * fold, -0.05f - 0.18f * fold, 0.0f)));
            float fLen = baseChord * (0.94f + 0.14f * sinf(u * 3.14159265f) + 0.06f * u) * Math_Lerp(1.0f, 0.62f, fold);
            float fHalfW = baseChord * 0.210f * Math_Lerp(1.0f, 0.72f, fold);
            anchor = Vec3_Sub(anchor, Vec3_Scale(featherUp, (float)i * fHalfW * 0.018f));
            Vector3 wAxis = Vec3_Normalize(Vec3_Cross(featherUp, fDir));
            if (!EmitFeatherBlade(opaque, anchor, fDir, wAxis, featherUp, fLen, fHalfW, 0.06f,
                                  dorsal, tipDark, ventral, SURFACE_REGION_FORELIMB, skR, skT))
                return false;
        }

        /* B2. Terciarias y escapulares (7 plumas anchas que integran el nacimiento del ala al dorso/flanco). */
        const unsigned nTert = 7u;
        for (unsigned i = 0; i < nTert; ++i) {
            float u = (float)i / (float)(nTert - 1u);
            Vector3 rootStart = Vec3_Add(sh->center, Vec3_Create(-side * sh->widthRadius * 0.65f, sh->heightRadius * 0.15f, 0.0f));
            Vector3 anchor = Vec3_Lerp(rootStart, el->center, u);
            AnatomySkinBinding sk = MakeSkin2(jSh, 1.0f - u * 0.85f, jEl, u * 0.85f);
            Vector3 fDir = Vec3_Normalize(Vec3_Add(wingBack, Vec3_Create(-side * (0.16f * (1.0f - u) + 0.08f * fold), -0.06f - 0.12f * fold, 0.0f)));
            float fLen = baseChord * (0.82f + 0.14f * u) * Math_Lerp(1.0f, 0.60f, fold);
            float fHalfW = baseChord * 0.22f * Math_Lerp(1.0f, 0.75f, fold);
            anchor = Vec3_Add(anchor, Vec3_Scale(featherUp, 0.010f * wingSpan * 0.25f - (float)i * fHalfW * 0.016f));
            Vector3 wAxis = Vec3_Normalize(Vec3_Cross(featherUp, fDir));
            if (!EmitFeatherBlade(opaque, anchor, fDir, wAxis, featherUp, fLen, fHalfW, 0.07f,
                                  covertCol, dorsal, ventral, SURFACE_REGION_FORELIMB, sk, sk))
                return false;
        }

        /* C. Coberteras mayores y medianas imbricadas sobre el perfil alar (relieve emplumado real). */
        for (int row = 0; row < 2; ++row) {
            unsigned nCov = row == 0 ? 12u : 10u;
            for (unsigned i = 0; i < nCov; ++i) {
                float u = (float)i / (float)(nCov - 1u);
                Vector3 anchor;
                AnatomySkinBinding sk;
                if (u < 0.33f) {
                    float a = u / 0.33f;
                    anchor = Vec3_Lerp(sh->center, el->center, a);
                    sk = MakeSkin2(jSh, 1.0f - a, jEl, a);
                } else if (u < 0.72f) {
                    float a = (u - 0.33f) / 0.39f;
                    anchor = Vec3_Lerp(el->center, wr->center, a);
                    sk = MakeSkin2(jEl, 1.0f - a, jWr, a);
                } else {
                    float a = (u - 0.72f) / 0.28f;
                    anchor = Vec3_Lerp(wr->center, h1->center, a);
                    sk = MakeSkin2(jWr, 1.0f - a, jH1, a);
                }
                anchor = Vec3_Add(anchor, Vec3_Scale(featherUp, (row == 0 ? 0.022f : 0.032f) * wingSpan * 0.22f));
                Vector3 fDir = Vec3_Normalize(Vec3_Lerp(wingBack, handDir, u * 0.42f * ext));
                if (fold > 0.2f) {
                    fDir = Vec3_Normalize(Vec3_Lerp(fDir, Vec3_Create(-side * 0.12f, -0.18f, -1.0f), Math_Clamp01((fold - 0.2f) / 0.65f)));
                }
                float fLen = baseChord * (row == 0 ? 0.60f : 0.40f) * (1.0f - 0.26f * u) * Math_Lerp(1.0f, 0.65f, fold);
                float fHalfW = baseChord * (row == 0 ? 0.175f : 0.165f) * Math_Lerp(1.0f, 0.75f, fold);
                Vector3 wAxis = Vec3_Normalize(Vec3_Cross(featherUp, fDir));
                if (!EmitFeatherBlade(opaque, anchor, fDir, wAxis, featherUp, fLen, fHalfW, 0.08f,
                                      covertCol, dorsal, ventral, SURFACE_REGION_FORELIMB, sk, sk))
                    return false;
            }
        }
    }
    return true;
}

static bool BuildAvianTailFan(const Monster* m, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;
    Color dorsal = p->surface.pigment.baseColor;
    Color tipBand = Color_FromRGB((uint8_t)(dorsal.r * 0.32f), (uint8_t)(dorsal.g * 0.34f), (uint8_t)(dorsal.b * 0.38f));
    Color ventral = p->surface.pigment.ventralColor;

    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_TAIL || mod->phenotypeIndex >= p->tailCount) continue;
        const TailPhenotype* tail = &p->tails[mod->phenotypeIndex];
        if (tail->archetype != TAIL_ARCHETYPE_FEATHER_FAN) continue;
        float dev = tail->development * p->development.appendages;
        if (dev <= 0.10f) continue;

        const AnatomyNode* base = AnatomyGraph_FindModuleNode(g, mod->instanceId, 1);
        const AnatomyNode* tip = AnatomyGraph_FindModuleNode(g, mod->instanceId, (uint16_t)tail->segmentCount);
        if (!base || !tip) continue;

        int jBase = FindJointSafe(g, opaque->skeleton, base->id);
        int jTip = FindJointSafe(g, opaque->skeleton, tip->id);
        Vector3 axis = Vec3_Normalize(Vec3_Sub(tip->center, base->center));
        if (Vec3_LengthSq(axis) < 1e-6f) axis = Vec3_Create(0, -0.18f, -1.0f);
        Vector3 side = Vec3_Create(1, 0, 0);
        Vector3 up = Vec3_Normalize(Vec3_Cross(side, axis));
        side = Vec3_Normalize(Vec3_Cross(axis, up));

        /* 12 rectrices superpuestas desde la base corta del uropigio (sin tallo largo desnudo). */
        const unsigned nRect = 12u;
        float fanHalf = Math_Clamp(0.08f + fabsf(tail->curvature) * 0.42f, 0.06f, 0.65f);
        float boneLen = Vec3_Distance(base->center, tip->center);
        float rectLen = fmaxf(tail->length * s * dev * 2.48f, boneLen * 2.60f);
        float rectHalfW = fmaxf(tail->baseWidth * s * 0.46f, rectLen * 0.100f);
        if (p->axial.archetype != AXIAL_ARCHETYPE_AVIAN) {
            /* En andamiajes no aviares, normalizar la longitud y anchura del
             * abanico caudal al tronco y base de la cola para evitar palas gigantes. */
            float trunkWorld = p->axial.trunkLength * s;
            rectLen = Math_Clamp(fminf(boneLen * 1.15f, trunkWorld * 0.72f),
                trunkWorld * 0.18f, trunkWorld * 0.78f);
            rectHalfW = fminf(fmaxf(base->widthRadius * 0.42f, rectLen * 0.075f),
                fminf(base->widthRadius * 0.60f, rectLen * 0.12f));
            fanHalf = Math_Clamp(fanHalf, 0.10f, 0.42f);
        }
        Vector3 rootOrigin = Vec3_Sub(base->center, Vec3_Scale(axis, base->widthRadius * 0.55f));

        for (unsigned i = 0; i < nRect; ++i) {
            float u = ((float)i / (float)(nRect - 1u)) * 2.0f - 1.0f; /* -1..+1 */
            float angle = u * fanHalf;
            Vector3 fDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(axis, cosf(angle)), Vec3_Scale(side, sinf(angle))));
            Vector3 anchor = Vec3_Add(rootOrigin,
                                      Vec3_Add(Vec3_Scale(side, u * base->widthRadius * 0.52f),
                                               Vec3_Scale(up, -fabsf(u) * rectHalfW * 0.06f)));
            float len = rectLen * (1.0f - 0.08f * u * u);
            Vector3 wAxis = Vec3_Normalize(Vec3_Cross(up, fDir));
            if (!EmitFeatherBlade(opaque, anchor, fDir, wAxis, up, len, rectHalfW, 0.025f,
                                  dorsal, tipBand, ventral, SURFACE_REGION_TAIL,
                                  MakeSkin2(jBase, 0.55f, jTip, 0.45f), MakeSkin1(jTip)))
                return false;
        }
        /* Supracoberteras e infracoberteras caudales (6 plumas que funden la rabadilla con el abanico). */
        const unsigned nCov = 6u;
        Color rumpCol = Color_Lerp(dorsal, p->surface.pigment.secondaryColor, 0.25f);
        for (unsigned i = 0; i < nCov; ++i) {
            float u = ((float)i / (float)(nCov - 1u)) * 2.0f - 1.0f;
            float angle = u * fanHalf * 0.55f;
            Vector3 fDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(axis, cosf(angle)), Vec3_Scale(side, sinf(angle))));
            Vector3 anchor = Vec3_Add(Vec3_Sub(rootOrigin, Vec3_Scale(axis, base->widthRadius * 0.28f)),
                                      Vec3_Add(Vec3_Scale(side, u * base->widthRadius * 0.58f),
                                               Vec3_Scale(up, base->heightRadius * 0.36f - fabsf(u) * rectHalfW * 0.05f)));
            Vector3 wAxis = Vec3_Normalize(Vec3_Cross(up, fDir));
            if (!EmitFeatherBlade(opaque, anchor, fDir, wAxis, up, rectLen * 0.42f, rectHalfW * 1.05f, 0.05f,
                                  rumpCol, dorsal, ventral, SURFACE_REGION_TAIL,
                                  MakeSkin1(jBase), MakeSkin2(jBase, 0.7f, jTip, 0.3f)))
                return false;
        }
    }
    return true;
}

/* ============================================================================
 * 2. SUPERFICIE CONTINUA DE DISCO BATOIDEO Y HENDIDURAS BRANQUIALES VENTRALES
 * ============================================================================ */

static bool BuildBatoidFinSurfaces(const Monster* m, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    if (p->axial.archetype != AXIAL_ARCHETYPE_DISC_RAY) return true;
    float s = p->axial.totalScale;

    Color dorsalCol = p->surface.pigment.baseColor;
    Color chevronCol = p->surface.pigment.secondaryColor;
    Color ventralCol = p->surface.pigment.ventralColor;

    const AnatomyNode* axFront = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_NECK);
    const AnatomyNode* axMid = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_THORAX_ANTERIOR);
    const AnatomyNode* axRear = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_ABDOMEN);

    /* A. Superficie hidrodinámica continua de cada aleta pectoral en diamante. */
    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_LIMB || mod->phenotypeIndex >= p->limbCount) continue;
        const LimbPhenotype* limb = &p->limbs[mod->phenotypeIndex];
        if (limb->archetype != LIMB_ARCHETYPE_FLIPPER || (limb->role != LIMB_FORE && limb->role != LIMB_WING))
            continue;
        float dev = limb->development * p->development.appendages;
        if (dev <= 0.10f) continue;

        const AnatomyNode* n0 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_ROOT);
        const AnatomyNode* n1 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_MIDDLE);
        const AnatomyNode* n2 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_DISTAL);
        const AnatomyNode* n3 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_AUTOPOD);
        if (!n0 || !n1 || !n2 || !n3) continue;

        int j0 = FindJointSafe(g, opaque->skeleton, n0->id);
        int j1 = FindJointSafe(g, opaque->skeleton, n1->id);
        int j2 = FindJointSafe(g, opaque->skeleton, n2->id);
        int j3 = FindJointSafe(g, opaque->skeleton, n3->id);
        int jBody = axMid ? FindJointSafe(g, opaque->skeleton, axMid->id) : j0;

        float side = limb->side == LIMB_RIGHT ? -1.0f : 1.0f;
        const unsigned nu = 16, nv = 10;
        MeshIndex topGrid[17][11], botGrid[17][11];

        for (unsigned iu = 0; iu <= nu; ++iu) {
            float u = (float)iu / (float)nu; /* 0 = raíz cuerpo central, 1 = punta aleta */
            /* Interpolar línea central spanwise a través de n0 -> n1 -> n2 -> n3 */
            Vector3 c;
            float chord, thick;
            AnatomySkinBinding sk;
            if (u < 0.333f) {
                float a = u / 0.333f;
                c = Vec3_Lerp(n0->center, n1->center, a);
                chord = Math_Lerp(n0->widthRadius, n1->widthRadius, a);
                thick = Math_Lerp(n0->heightRadius, n1->heightRadius, a);
                sk = (iu == 0) ? MakeSkin2(jBody, 0.65f, j0, 0.35f) : MakeSkin2(j0, 1.0f - a, j1, a);
            } else if (u < 0.666f) {
                float a = (u - 0.333f) / 0.333f;
                c = Vec3_Lerp(n1->center, n2->center, a);
                chord = Math_Lerp(n1->widthRadius, n2->widthRadius, a);
                thick = Math_Lerp(n1->heightRadius, n2->heightRadius, a);
                sk = MakeSkin2(j1, 1.0f - a, j2, a);
            } else {
                float a = (u - 0.666f) / 0.334f;
                c = Vec3_Lerp(n2->center, n3->center, a);
                chord = Math_Lerp(n2->widthRadius, n3->widthRadius, a);
                thick = Math_Lerp(n2->heightRadius, n3->heightRadius, a);
                sk = MakeSkin2(j2, 1.0f - a, j3, a);
            }
            /* Cuerda amplia de borde de ataque suave y borde de salida falcado/cóncavo. */
            float leadZ = c.z + chord * (1.05f - 0.22f * u);
            float trailZ = c.z - chord * (1.18f - 0.48f * sinf(u * 3.14159265f) * 0.45f) * (1.0f - 0.52f * u);
            if (iu == nu) { leadZ = c.z + chord * 0.14f; trailZ = c.z - chord * 0.14f; }

            for (unsigned iv = 0; iv <= nv; ++iv) {
                float v = (float)iv / (float)nv; /* 0 = borde ataque, 1 = borde salida */
                float z = Math_Lerp(leadZ, trailZ, v);
                /* Perfil aerodinámico NACA-like: dorso convexo, vientre casi plano. */
                float airfoil = sinf(powf(v, 0.68f) * 3.14159265f);
                float hTop = thick * (0.20f + 1.18f * airfoil) * (1.0f - 0.55f * u * u);
                float hBot = thick * (0.14f + 0.36f * airfoil) * (1.0f - 0.55f * u * u);
                Vector3 pTop = Vec3_Create(c.x, c.y + hTop, z);
                Vector3 pBot = Vec3_Create(c.x, c.y - hBot, z);

                /* Pigmentación realista de Mobula birostris:
                 * Dorso oscuro con parches pálidos en hombros (u en [0.06..0.38], v en [0.08..0.46]);
                 * Vientre blanco cremoso con sutil borde posterior grisáceo y moteado ventral. */
                float shoulderPatch = (u > 0.04f && u < 0.40f && v > 0.06f && v < 0.48f)
                    ? sinf((u - 0.04f) / 0.36f * 3.14159265f) * sinf((v - 0.06f) / 0.42f * 3.14159265f) : 0.0f;
                Color cTop = Color_Lerp(dorsalCol, chevronCol, Math_Clamp01(shoulderPatch * 0.92f));
                float marginShade = Math_Clamp01((v - 0.65f) / 0.35f) * 0.38f + Math_Clamp01((u - 0.78f) / 0.22f) * 0.35f;
                Color ventralMatch = Color_FromRGB((uint8_t)(ventralCol.r * 0.79f), (uint8_t)(ventralCol.g * 0.80f), (uint8_t)(ventralCol.b * 0.81f));
                Color cBot = Color_Lerp(ventralMatch, dorsalCol, Math_Clamp01(marginShade));

                Vector3 nTop = Vec3_Normalize(Vec3_Create(side * 0.08f * u, 1.0f, 0.14f * (0.35f - v)));
                Vector3 nBot = Vec3_Normalize(Vec3_Create(side * 0.05f * u, -1.0f, 0.06f * (0.35f - v)));
                if (!EmitVertex(opaque, pTop, nTop, cTop, SURFACE_REGION_DORSAL_TRUNK, 0.0f, sk, &topGrid[iu][iv]) ||
                    !EmitVertex(opaque, pBot, nBot, cBot, SURFACE_REGION_VENTRAL_TRUNK, 1.0f, sk, &botGrid[iu][iv])) {
                    return false;
                }
            }
        }
        for (unsigned iu = 0; iu < nu; ++iu) {
            for (unsigned iv = 0; iv < nv; ++iv) {
                if (side > 0.0f) {
                    if (!EmitQuad(opaque, topGrid[iu][iv], topGrid[iu + 1][iv], topGrid[iu + 1][iv + 1], topGrid[iu][iv + 1]) ||
                        !EmitQuad(opaque, botGrid[iu][iv], botGrid[iu][iv + 1], botGrid[iu + 1][iv + 1], botGrid[iu + 1][iv]))
                        return false;
                } else {
                    if (!EmitQuad(opaque, topGrid[iu][iv], topGrid[iu][iv + 1], topGrid[iu + 1][iv + 1], topGrid[iu + 1][iv]) ||
                        !EmitQuad(opaque, botGrid[iu][iv], botGrid[iu + 1][iv], botGrid[iu + 1][iv + 1], botGrid[iu][iv + 1]))
                        return false;
                }
            }
        }
    }

    /* B. Cinco pares de hendiduras branquiales en la cara VENTRAL del disco central (nunca dorsal). */
    if (axFront && axMid && axRear) {
        int jMid = FindJointSafe(g, opaque->skeleton, axMid->id);
        AnatomySkinBinding sk = MakeSkin1(jMid);
        Color darkGill = Color_FromRGB(34, 30, 36);
        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            float sign = sideIdx == 0 ? 1.0f : -1.0f;
            for (unsigned slit = 0; slit < 5; ++slit) {
                float u = (float)slit / 4.0f;
                float z = Math_Lerp(axFront->center.z - axFront->heightRadius * 0.25f,
                                    Math_Lerp(axMid->center.z, axRear->center.z, 0.48f), u);
                float xInner = sign * axMid->widthRadius * Math_Lerp(0.18f, 0.13f, u);
                float xOuter = sign * axMid->widthRadius * Math_Lerp(0.58f, 0.40f, u);
                /* Ligeramente debajo de la superficie ventral del SDF para visibilidad clara y limpia. */
                float yBase = axMid->center.y - axMid->heightRadius * (1.04f - 0.06f * u) - 0.008f * s;
                float halfZ = axMid->widthRadius * 0.026f;
                MeshIndex v0, v1, v2, v3;
                Vector3 nDown = Vec3_Create(0, -1, 0);
                if (!EmitVertex(opaque, Vec3_Create(xInner, yBase, z + halfZ), nDown, darkGill, SURFACE_REGION_VENTRAL_TRUNK, 1.0f, sk, &v0) ||
                    !EmitVertex(opaque, Vec3_Create(xOuter, yBase, z + halfZ * 0.72f), nDown, darkGill, SURFACE_REGION_VENTRAL_TRUNK, 1.0f, sk, &v1) ||
                    !EmitVertex(opaque, Vec3_Create(xOuter, yBase, z - halfZ * 0.72f), nDown, darkGill, SURFACE_REGION_VENTRAL_TRUNK, 1.0f, sk, &v2) ||
                    !EmitVertex(opaque, Vec3_Create(xInner, yBase, z - halfZ), nDown, darkGill, SURFACE_REGION_VENTRAL_TRUNK, 1.0f, sk, &v3))
                    return false;
                if (sign > 0.0f) {
                    if (!EmitQuad(opaque, v0, v3, v2, v1)) return false;
                } else {
                    if (!EmitQuad(opaque, v0, v1, v2, v3)) return false;
                }
            }
        }
    }
    return true;
}

/* ============================================================================
 * 3. VENTOSAS EN DOBLE HILERA LONGITUDINAL PARA BRAZOS DE CEFALÓPODO
 * ============================================================================ */

static bool EmitSuckerCup(BuildTarget* t, Vector3 baseCenter, Vector3 outward, Vector3 tangent,
                          float radius, Color rimCol, Color cupCol, AnatomySkinBinding skin) {
    if (radius <= 1e-5f) return true;
    outward = Vec3_Normalize(outward);
    Vector3 uAxis = Vec3_Normalize(Vec3_Sub(tangent, Vec3_Scale(outward, Vec3_Dot(tangent, outward))));
    if (Vec3_LengthSq(uAxis) < 1e-6f) uAxis = Vec3_Create(1, 0, 0);
    Vector3 vAxis = Vec3_Normalize(Vec3_Cross(outward, uAxis));

    const unsigned segs = 8;
    float stalkH = radius * 0.42f;
    float rimH = radius * 0.68f;
    float innerR = radius * 0.56f;
    float cupDepth = radius * 0.34f;

    MeshIndex baseIdx[8], rimOutIdx[8], rimInIdx[8], pitCenterIdx;
    Vector3 pitCenter = Vec3_Add(baseCenter, Vec3_Scale(outward, rimH - cupDepth));
    if (!EmitVertex(t, pitCenter, outward, cupCol, SURFACE_REGION_FORELIMB, 1.0f, skin, &pitCenterIdx))
        return false;

    for (unsigned k = 0; k < segs; ++k) {
        float a = 6.28318530718f * (float)k / (float)segs;
        Vector3 radial = Vec3_Add(Vec3_Scale(uAxis, cosf(a)), Vec3_Scale(vAxis, sinf(a)));
        Vector3 pBase = Vec3_Add(baseCenter, Vec3_Scale(radial, radius * 0.62f));
        Vector3 pRimOut = Vec3_Add(baseCenter, Vec3_Add(Vec3_Scale(outward, stalkH), Vec3_Scale(radial, radius)));
        Vector3 pRimIn = Vec3_Add(baseCenter, Vec3_Add(Vec3_Scale(outward, rimH), Vec3_Scale(radial, innerR)));
        if (!EmitVertex(t, pBase, radial, rimCol, SURFACE_REGION_FORELIMB, 1.0f, skin, &baseIdx[k]) ||
            !EmitVertex(t, pRimOut, Vec3_Normalize(Vec3_Add(radial, Vec3_Scale(outward, 0.35f))), rimCol, SURFACE_REGION_FORELIMB, 1.0f, skin, &rimOutIdx[k]) ||
            !EmitVertex(t, pRimIn, outward, rimCol, SURFACE_REGION_FORELIMB, 1.0f, skin, &rimInIdx[k]))
            return false;
    }
    for (unsigned k = 0; k < segs; ++k) {
        unsigned kn = (k + 1) % segs;
        if (!EmitQuad(t, baseIdx[k], baseIdx[kn], rimOutIdx[kn], rimOutIdx[k]) ||
            !EmitQuad(t, rimOutIdx[k], rimOutIdx[kn], rimInIdx[kn], rimInIdx[k]) ||
            !EmitTri(t, rimInIdx[k], rimInIdx[kn], pitCenterIdx))
            return false;
    }
    return true;
}

static bool BuildArmSuckers(const Monster* m, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    Color rimCol = Color_Lerp(p->surface.pigment.ventralColor, Color_FromRGB(238, 214, 192), 0.55f);
    Color cupCol = Color_FromRGB(142, 86, 70);

    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_FLEXIBLE_APPENDAGE || mod->phenotypeIndex >= p->flexibleAppendageCount)
            continue;
        const FlexibleAppendagePhenotype* fa = &p->flexibleAppendages[mod->phenotypeIndex];
        if (fa->suckerRows == 0 || fa->suckerCount == 0 || fa->suckerScale <= 0.01f) continue;

        /* Recoger línea central resuelta del brazo (estaciones 1..FLEXIBLE_APPENDAGE_STATIONS). */
        const AnatomyNode* stations[FLEXIBLE_APPENDAGE_STATIONS];
        int joints[FLEXIBLE_APPENDAGE_STATIONS];
        unsigned count = 0;
        for (unsigned id = 1; id <= FLEXIBLE_APPENDAGE_STATIONS; ++id) {
            const AnatomyNode* n = AnatomyGraph_FindModuleNode(g, mod->instanceId, (uint16_t)id);
            if (!n) break;
            stations[count] = n;
            joints[count] = FindJointSafe(g, opaque->skeleton, n->id);
            ++count;
        }
        if (count < 3) continue;

        /* Marco paralelo consistente orientado hacia la cara ventral/oral (-Y) del brazo. */
        unsigned pairs = fa->suckerCount;
        unsigned rows = fa->suckerRows > 2 ? 2u : fa->suckerRows;
        for (unsigned sIdx = 0; sIdx < pairs; ++sIdx) {
            for (unsigned row = 0; row < rows; ++row) {
                /* Disposición alterna (biserial escalonada) a lo largo del brazo [0.08..0.90]. */
                float stagger = (rows == 2 && row == 1) ? (0.45f / (float)pairs) : 0.0f;
                float u = Math_Clamp(0.08f + 0.82f * ((float)sIdx / (float)pairs) + stagger, 0.06f, 0.92f);
                float scaled = u * (float)(count - 1u);
                unsigned i0 = (unsigned)floorf(scaled);
                if (i0 >= count - 1u) i0 = count - 2u;
                float a = scaled - (float)i0;

                Vector3 center = Vec3_Lerp(stations[i0]->center, stations[i0 + 1]->center, a);
                float localR = Math_Lerp(stations[i0]->widthRadius, stations[i0 + 1]->widthRadius, a);
                float localH = Math_Lerp(stations[i0]->heightRadius, stations[i0 + 1]->heightRadius, a);
                Vector3 tang = Vec3_Normalize(Vec3_Sub(stations[i0 + 1]->center, stations[i0]->center));
                /* Vector ventral coherente: proyectar -Y ortogonal a la tangente del brazo. */
                Vector3 down = Vec3_Create(0.0f, -1.0f, 0.0f);
                Vector3 ventral = Vec3_Sub(down, Vec3_Scale(tang, Vec3_Dot(down, tang)));
                if (Vec3_LengthSq(ventral) < 1e-4f) ventral = Vec3_Create(0.0f, 0.0f, 1.0f);
                ventral = Vec3_Normalize(ventral);
                Vector3 lat = Vec3_Normalize(Vec3_Cross(tang, ventral));

                float rowSign = (rows == 2) ? (row == 0 ? -1.0f : 1.0f) : 0.0f;
                float suckerR = localR * fa->suckerScale * (0.52f - 0.14f * u);
                Vector3 attachDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(ventral, 0.88f), Vec3_Scale(lat, rowSign * 0.44f)));
                /* Embeber la base ligeramente dentro del brazo para continuidad sólida. */
                Vector3 suckerPos = Vec3_Add(center, Vec3_Add(
                    Vec3_Scale(ventral, localH * 0.76f),
                    Vec3_Scale(lat, rowSign * localR * 0.36f)));
                AnatomySkinBinding sk = MakeSkin2(joints[i0], 1.0f - a, joints[i0 + 1], a);
                if (!EmitSuckerCup(opaque, suckerPos, attachDir, tang, suckerR, rimCol, cupCol, sk))
                    return false;
            }
        }
    }

    /* Membrana interbraquial muscular proximal (une el 18% basal de los brazos contiguos del cefalópodo). */
    const AnatomyNode* armRoot[12], *armMid[12];
    int armJ0[12], armJ2[12];
    float armAngle[12];
    unsigned armCount = 0;
    Vector3 crownCenter = Vec3_Zero();
    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount && armCount < 12; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_FLEXIBLE_APPENDAGE || mod->phenotypeIndex >= p->flexibleAppendageCount) continue;
        if (p->flexibleAppendages[mod->phenotypeIndex].profile != FLEXIBLE_PROFILE_MUSCULAR_ARM) continue;
        const AnatomyNode* n0 = AnatomyGraph_FindModuleNode(g, mod->instanceId, 1);
        const AnatomyNode* n2 = AnatomyGraph_FindModuleNode(g, mod->instanceId, 3);
        if (!n0 || !n2) continue;
        armRoot[armCount] = n0;
        armMid[armCount] = n2;
        armJ0[armCount] = FindJointSafe(g, opaque->skeleton, n0->id);
        armJ2[armCount] = FindJointSafe(g, opaque->skeleton, n2->id);
        crownCenter = Vec3_Add(crownCenter, n0->center);
        ++armCount;
    }
    if (armCount >= 4) {
        crownCenter = Vec3_Scale(crownCenter, 1.0f / (float)armCount);
        for (unsigned i = 0; i < armCount; ++i) {
            Vector3 d = Vec3_Sub(armMid[i]->center, crownCenter);
            armAngle[i] = atan2f(d.z, d.x);
        }
        /* Ordenar brazos por ángulo acimutal alrededor de la corona oral. */
        unsigned order[12];
        for (unsigned i = 0; i < armCount; ++i) order[i] = i;
        for (unsigned i = 0; i < armCount; ++i) {
            for (unsigned j = i + 1; j < armCount; ++j) {
                if (armAngle[order[j]] < armAngle[order[i]]) {
                    unsigned tmp = order[i]; order[i] = order[j]; order[j] = tmp;
                }
            }
        }
        Color webTop = p->surface.pigment.baseColor;
        Color webBot = Color_Lerp(p->surface.pigment.ventralColor, p->surface.pigment.baseColor, 0.40f);
        /* Membrana interbraquial teselada suavemente con curvatura festoneada
         * entre raíces y segmento medio de cada par de brazos adyacentes. */
        enum { WEB_U = 5, WEB_V = 4 };
        for (unsigned k = 0; k < armCount; ++k) {
            unsigned a = order[k], b = order[(k + 1) % armCount];
            Vector3 rA = armRoot[a]->center, rB = armRoot[b]->center;
            Vector3 mA = armMid[a]->center, mB = armMid[b]->center;
            float baseThick = 0.5f * (armMid[a]->heightRadius + armMid[b]->heightRadius) * 0.34f;
            Vector3 gridP[WEB_V][WEB_U];
            float gridT[WEB_V][WEB_U];
            for (unsigned vi = 0; vi < WEB_V; ++vi) {
                float v = (float)vi / (float)(WEB_V - 1);
                for (unsigned ui = 0; ui < WEB_U; ++ui) {
                    float u = (float)ui / (float)(WEB_U - 1);
                    float arch = sinf(3.14159265f * u);
                    float vEff = v * (1.0f - 0.22f * arch);
                    Vector3 pA = Vec3_Lerp(rA, mA, vEff);
                    Vector3 pB = Vec3_Lerp(rB, mB, vEff);
                    Vector3 pt = Vec3_Lerp(pA, pB, u);
                    pt.y -= baseThick * (0.40f * arch * v + 0.12f * v * v);
                    gridP[vi][ui] = pt;
                    gridT[vi][ui] = baseThick * (1.0f - 0.52f * v) * (1.0f - 0.18f * arch);
                }
            }
            MeshIndex topIdx[WEB_V][WEB_U], botIdx[WEB_V][WEB_U];
            for (unsigned vi = 0; vi < WEB_V; ++vi) {
                float v = (float)vi / (float)(WEB_V - 1);
                for (unsigned ui = 0; ui < WEB_U; ++ui) {
                    float u = (float)ui / (float)(WEB_U - 1);
                    unsigned u0 = ui > 0 ? ui - 1 : 0, u1 = ui + 1 < WEB_U ? ui + 1 : WEB_U - 1;
                    unsigned v0 = vi > 0 ? vi - 1 : 0, v1 = vi + 1 < WEB_V ? vi + 1 : WEB_V - 1;
                    Vector3 dU = Vec3_Sub(gridP[vi][u1], gridP[vi][u0]);
                    Vector3 dV = Vec3_Sub(gridP[v1][ui], gridP[v0][ui]);
                    Vector3 n = Vec3_Cross(dV, dU);
                    if (Vec3_LengthSq(n) < 1e-10f) n = Vec3_Create(0, 1, 0);
                    else n = Vec3_Normalize(n);
                    if (n.y < 0.0f) n = Vec3_Scale(n, -1.0f);
                    Vector3 off = Vec3_Scale(n, gridT[vi][ui]);
                    int jA = v < 0.45f ? armJ0[a] : armJ2[a];
                    int jB = v < 0.45f ? armJ0[b] : armJ2[b];
                    AnatomySkinBinding sk = MakeSkin2(jA, 1.0f - u, jB, u);
                    if (!EmitVertex(opaque, Vec3_Add(gridP[vi][ui], off), n, webTop,
                            SURFACE_REGION_FORELIMB, 0.0f, sk, &topIdx[vi][ui]) ||
                        !EmitVertex(opaque, Vec3_Sub(gridP[vi][ui], off), Vec3_Scale(n, -1.0f), webBot,
                            SURFACE_REGION_FORELIMB, 1.0f, sk, &botIdx[vi][ui]))
                        return false;
                }
            }
            for (unsigned vi = 0; vi + 1 < WEB_V; ++vi) {
                for (unsigned ui = 0; ui + 1 < WEB_U; ++ui) {
                    if (!EmitTri(opaque, topIdx[vi][ui], topIdx[vi][ui + 1], topIdx[vi + 1][ui + 1]) ||
                        !EmitTri(opaque, topIdx[vi][ui], topIdx[vi + 1][ui + 1], topIdx[vi + 1][ui]) ||
                        !EmitTri(opaque, botIdx[vi][ui], botIdx[vi + 1][ui + 1], botIdx[vi][ui + 1]) ||
                        !EmitTri(opaque, botIdx[vi][ui], botIdx[vi + 1][ui], botIdx[vi + 1][ui + 1]))
                        return false;
                }
            }
            /* Cierre del borde libre distal entre ambos brazos. */
            for (unsigned ui = 0; ui + 1 < WEB_U; ++ui) {
                if (!EmitTri(opaque, topIdx[WEB_V - 1][ui], topIdx[WEB_V - 1][ui + 1], botIdx[WEB_V - 1][ui + 1]) ||
                    !EmitTri(opaque, topIdx[WEB_V - 1][ui], botIdx[WEB_V - 1][ui + 1], botIdx[WEB_V - 1][ui]))
                    return false;
            }
        }
    }
    return true;
}

/* ============================================================================
 * 4. ALAS DE INSECTO FINAS, SEMITRANSPARENTES Y CON VENACIÓN ESTRUCTURAL
 * ============================================================================ */

static bool EmitVeinStrip(BuildTarget* t, Vector3 a, Vector3 b, Vector3 up,
                          float radius, Color col, AnatomySkinBinding skA, AnatomySkinBinding skB) {
    Vector3 dir = Vec3_Sub(b, a);
    if (Vec3_LengthSq(dir) < 1e-10f) return true;
    Vector3 side = Vec3_Normalize(Vec3_Cross(up, dir));
    if (Vec3_LengthSq(side) < 1e-6f) side = Vec3_Create(0, 0, 1);
    Vector3 nUp = Vec3_Normalize(up);
    Vector3 offS = Vec3_Scale(side, radius);
    Vector3 offU = Vec3_Scale(nUp, radius * 0.9f);
    MeshIndex i0, i1, i2, i3, j0, j1, j2, j3;
    if (!EmitVertex(t, Vec3_Add(Vec3_Sub(a, offS), offU), nUp, col, SURFACE_REGION_FORELIMB, 0, skA, &i0) ||
        !EmitVertex(t, Vec3_Add(Vec3_Add(a, offS), offU), nUp, col, SURFACE_REGION_FORELIMB, 0, skA, &i1) ||
        !EmitVertex(t, Vec3_Add(Vec3_Add(b, offS), offU), nUp, col, SURFACE_REGION_FORELIMB, 0, skB, &i2) ||
        !EmitVertex(t, Vec3_Add(Vec3_Sub(b, offS), offU), nUp, col, SURFACE_REGION_FORELIMB, 0, skB, &i3) ||
        !EmitVertex(t, Vec3_Sub(Vec3_Sub(a, offS), offU), Vec3_Scale(nUp, -1), col, SURFACE_REGION_FORELIMB, 1, skA, &j0) ||
        !EmitVertex(t, Vec3_Sub(Vec3_Add(a, offS), offU), Vec3_Scale(nUp, -1), col, SURFACE_REGION_FORELIMB, 1, skA, &j1) ||
        !EmitVertex(t, Vec3_Sub(Vec3_Add(b, offS), offU), Vec3_Scale(nUp, -1), col, SURFACE_REGION_FORELIMB, 1, skB, &j2) ||
        !EmitVertex(t, Vec3_Sub(Vec3_Sub(b, offS), offU), Vec3_Scale(nUp, -1), col, SURFACE_REGION_FORELIMB, 1, skB, &j3))
        return false;
    return EmitQuad(t, i0, i1, i2, i3) && EmitQuad(t, j0, j3, j2, j1);
}

static bool BuildInsectWings(const Monster* m, BuildTarget* opaque, BuildTarget* translucent) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;

    Color veinCol = Color_FromRGB(36, 44, 40);
    Color stigmaCol = Color_FromRGB(46, 36, 26);
    Color memTint = (Color){205, 232, 238, 86};
    Color memBaseTint = (Color){186, 214, 178, 118};

    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_LIMB || mod->phenotypeIndex >= p->limbCount) continue;
        const LimbPhenotype* limb = &p->limbs[mod->phenotypeIndex];
        if (limb->archetype != LIMB_ARCHETYPE_INSECT_WING) continue;
        float dev = limb->development * p->development.appendages;
        float emerged = Math_Clamp01((dev - 0.25f) / 0.45f);
        if (emerged <= 0.02f) continue;

        const AnatomyNode* n0 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_ROOT);
        const AnatomyNode* n1 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_MIDDLE);
        const AnatomyNode* n2 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_DISTAL);
        const AnatomyNode* n3 = AnatomyGraph_FindModuleNode(g, mod->instanceId, LIMB_NODE_AUTOPOD);
        if (!n0 || !n1 || !n2 || !n3) continue;

        int j0 = FindJointSafe(g, opaque->skeleton, n0->id);
        int j1 = FindJointSafe(g, opaque->skeleton, n1->id);
        int j2 = FindJointSafe(g, opaque->skeleton, n2->id);
        int j3 = FindJointSafe(g, opaque->skeleton, n3->id);

        float span = Vec3_Distance(n0->center, n3->center);
        if (span <= 1e-4f) continue;
        float aspect = limb->wingAspect > 0.5f ? limb->wingAspect : 4.2f;
        float maxChord = (span / aspect) * fmaxf(0.65f, limb->footWidthScale) * emerged;
        float side = limb->side == LIMB_RIGHT ? -1.0f : 1.0f;
        Vector3 wingUp = Vec3_Create(0, 1, 0);
        Vector3 backDir = Vec3_Normalize(Vec3_Create(-side * 0.06f, -0.02f, -1.0f));
        float veinR = fmaxf(0.0010f * s, maxChord * 0.015f);

        const unsigned nu = 16, nv = 4;
        MeshIndex memGrid[17][5];
        Vector3 posGrid[17][5];
        AnatomySkinBinding skGrid[17];

        for (unsigned iu = 0; iu <= nu; ++iu) {
            float u = (float)iu / (float)nu;
            Vector3 c;
            AnatomySkinBinding sk;
            if (u < 0.333f) {
                float a = u / 0.333f;
                c = Vec3_Lerp(n0->center, n1->center, a);
                sk = MakeSkin2(j0, 1.0f - a, j1, a);
            } else if (u < 0.666f) {
                float a = (u - 0.333f) / 0.333f;
                c = Vec3_Lerp(n1->center, n2->center, a);
                sk = MakeSkin2(j1, 1.0f - a, j2, a);
            } else {
                float a = (u - 0.666f) / 0.334f;
                c = Vec3_Lerp(n2->center, n3->center, a);
                sk = MakeSkin2(j2, 1.0f - a, j3, a);
            }
            skGrid[iu] = sk;

            /* Contorno alar de odonato: estrecho en la raíz, ancho en el tercio proximal-medio
             * (especialmente en alas posteriores con footWidthScale > 1.1) y ápice redondeado. */
            float basalBroad = limb->footWidthScale > 1.05f ? 0.32f * sinf(Math_Clamp01(u / 0.55f) * 3.14159265f) : 0.0f;
            float chordEnv = sinf(powf(u, 0.72f) * 3.14159265f) * (0.88f + 0.16f * sinf(u * 3.14159265f)) + basalBroad;
            float chord = maxChord * fmaxf(0.08f, chordEnv);
            /* Pequeña muesca costal en el nodus (u ~ 0.48) */
            float nodusDip = expf(-powf((u - 0.48f) * 14.0f, 2.0f)) * maxChord * 0.06f;

            for (unsigned iv = 0; iv <= nv; ++iv) {
                float v = (float)iv / (float)nv; /* 0 = costa anterior, 1 = margen posterior */
                /* Corrugación aerodinámica sutil de ala de libélula */
                float corrug = sinf(v * 6.2831853f) * maxChord * 0.020f * sinf(u * 3.14159265f);
                float camber = sinf(v * 3.14159265f) * maxChord * fmaxf(0.02f, limb->finCamber * 0.22f);
                Vector3 pt = Vec3_Add(c, Vec3_Add(
                    Vec3_Scale(backDir, chord * v + nodusDip * (1.0f - v)),
                    Vec3_Scale(wingUp, camber + corrug)));
                posGrid[iu][iv] = pt;
                Color cMem = Color_Lerp(memBaseTint, memTint, Math_Clamp01(u * 2.4f));
                if (!EmitVertex(translucent, pt, wingUp, cMem, SURFACE_REGION_FORELIMB, 0.0f, sk, &memGrid[iu][iv]))
                    return false;
            }
        }
        /* Triángulos de membrana transparente (doble cara manejada por translucentMesh.doubleSided). */
        for (unsigned iu = 0; iu < nu; ++iu) {
            for (unsigned iv = 0; iv < nv; ++iv) {
                if (!EmitQuad(translucent, memGrid[iu][iv], memGrid[iu + 1][iv],
                              memGrid[iu + 1][iv + 1], memGrid[iu][iv + 1]))
                    return false;
            }
        }
        /* Venación estructural opaca: costa (iv=0), radio/media (iv=1,2), borde posterior (iv=4),
         * venas transversales y pterostigma oscuro cerca del ápice costal. */
        for (unsigned iu = 0; iu < nu; ++iu) {
            float uMid = ((float)iu + 0.5f) / (float)nu;
            bool isStigma = (uMid > 0.74f && uMid < 0.88f);
            float costaR = isStigma ? veinR * 2.4f : veinR * 1.45f;
            Color cCost = isStigma ? stigmaCol : veinCol;
            if (!EmitVeinStrip(opaque, posGrid[iu][0], posGrid[iu + 1][0], wingUp, costaR, cCost, skGrid[iu], skGrid[iu + 1]) ||
                !EmitVeinStrip(opaque, posGrid[iu][1], posGrid[iu + 1][1], wingUp, veinR * 0.95f, veinCol, skGrid[iu], skGrid[iu + 1]) ||
                !EmitVeinStrip(opaque, posGrid[iu][2], posGrid[iu + 1][2], wingUp, veinR * 0.72f, veinCol, skGrid[iu], skGrid[iu + 1]) ||
                !EmitVeinStrip(opaque, posGrid[iu][nv], posGrid[iu + 1][nv], wingUp, veinR * 0.65f, veinCol, skGrid[iu], skGrid[iu + 1]))
                return false;
            /* Venas transversales periódicas */
            if (iu > 0 && iu < nu) {
                if (!EmitVeinStrip(opaque, posGrid[iu][0], posGrid[iu][nv], wingUp, veinR * 0.52f, veinCol, skGrid[iu], skGrid[iu]))
                    return false;
            }
        }
    }
    return true;
}

/* ============================================================================
 * 5. APÉNDICES REPETIDOS (TENTÁCULOS MARGINALES Y BRAZOS ORALES RIZADOS)
 * ============================================================================ */

static bool BuildRepeatedAppendages(const Monster* m, BuildTarget* opaque, BuildTarget* translucent) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;

    for (size_t gi = 0; gi < p->repeatedAppendageCount; ++gi) {
        const RepeatedAppendageGroup* rg = &p->repeatedAppendages[gi];
        if (rg->kind == REPEATED_APPENDAGE_NONE || rg->count == 0) continue;
        float dev = rg->development * p->development.appendages;
        if (dev <= 0.05f) continue;

        uint32_t hostMod = rg->hostModuleInstanceId ? rg->hostModuleInstanceId : 1u;
        uint16_t hostLocal = rg->hostNodeId ? rg->hostNodeId : AXIAL_NODE_THORAX_ANTERIOR;
        const AnatomyNode* host = AnatomyGraph_FindModuleNode(g, hostMod, hostLocal);
        if (!host) host = g->nodeCount ? &g->nodes[0] : NULL;
        if (!host) continue;

        /* Buscar nodos marginales radiales si el anfitrión es una campana medusoide. */
        unsigned rimOrder = 0;
        const AnatomyNode* rimNodes[16];
        int rimJoints[16];
        if (p->axial.archetype == AXIAL_ARCHETYPE_MEDUSA_RADIAL) {
            for (unsigned r = 0; r < 16; ++r) {
                const AnatomyNode* rn = AnatomyGraph_FindModuleNode(g, hostMod, (uint16_t)(AXIAL_MEDUSA_RIM_NODE_BASE + r));
                if (!rn) break;
                rimNodes[rimOrder] = rn;
                rimJoints[rimOrder] = FindJointSafe(g, opaque->skeleton, rn->id);
                ++rimOrder;
            }
        }
        int jHost = FindJointSafe(g, opaque->skeleton, host->id);
        float arcSpan = rg->arcSpan > 1e-3f ? rg->arcSpan : 6.28318530718f;
        bool fullCircle = fabsf(arcSpan - 6.28318530718f) < 1e-2f;
        Vector3 ringCenter = Vec3_Add(host->center, Vec3_Create(0.0f, rg->ringOffsetY * s, 0.0f));
        float ringR = rg->ringRadius * s;

        for (unsigned inst = 0; inst < rg->count; ++inst) {
            uint64_t instId = RepeatedAppendage_InstanceId(rg->groupId, inst) ^ ((uint64_t)rg->seed * 2654435761ULL);
            float rndA = Hash01(instId);
            float rndB = Hash01(instId ^ UINT64_C(0x9e3779b97f4a7c15));
            float frac = (fullCircle || rg->count == 1)
                ? ((float)inst / (float)rg->count)
                : ((float)inst / (float)(rg->count - 1u));
            float angle = rg->arcStart + arcSpan * frac;
            Vector3 radial = Vec3_Create(cosf(angle), 0.0f, sinf(angle));
            Vector3 tangent = Vec3_Create(-sinf(angle), 0.0f, cosf(angle));
            Vector3 root = Vec3_Add(ringCenter, Vec3_Scale(radial, ringR));

            /* Vincular al par de nodos marginales más cercanos si hay anillo radial. */
            AnatomySkinBinding skRoot = MakeSkin1(jHost);
            if (rimOrder >= 2 && rg->kind == REPEATED_APPENDAGE_FILAMENT) {
                float normA = fmodf(angle, 6.28318530718f);
                if (normA < 0.0f) normA += 6.28318530718f;
                float fIdx = (normA / 6.28318530718f) * (float)rimOrder;
                unsigned r0 = (unsigned)floorf(fIdx) % rimOrder;
                unsigned r1 = (r0 + 1u) % rimOrder;
                float ra = fIdx - floorf(fIdx);
                skRoot = MakeSkin2(rimJoints[r0], 1.0f - ra, rimJoints[r1], ra);
                /* Asentar la raíz ligeramente dentro del margen subumbrelar para evitar puntas salientes. */
                Vector3 rimInterp = Vec3_Lerp(rimNodes[r0]->center, rimNodes[r1]->center, ra);
                root = Vec3_Add(ringCenter, Vec3_Scale(radial, ringR * 0.93f));
                root.y = rimInterp.y + rg->ringOffsetY * s * 0.15f;
            }

            float len = rg->length * s * dev * (1.0f + (rndA * 2.0f - 1.0f) * rg->lengthVariation);
            float baseR = rg->baseRadius * s * dev;
            float tipR = rg->tipRadius * s * dev;
            float phase = rg->phaseSpread * (float)inst + rndB * 6.2831853f;

            if (rg->kind == REPEATED_APPENDAGE_FILAMENT) {
                /* Filamento / tentáculo delgado tubular (4 caras, 12 tramos) con curvatura natural. */
                const unsigned segs = 12;
                MeshIndex ring[13][4];
                for (unsigned k = 0; k <= segs; ++k) {
                    float u = (float)k / (float)segs;
                    float sway = rg->curlAmplitude * s * (u * u * 0.55f + 0.45f * sinf(u * rg->waveFrequency * 3.14159265f + phase) * u);
                    float swirl = rg->curlAmplitude * s * 0.35f * cosf(u * rg->waveFrequency * 2.4f + phase) * u;
                    Vector3 center = Vec3_Add(root, Vec3_Add(
                        Vec3_Create(0.0f, -len * u, 0.0f),
                        Vec3_Add(Vec3_Scale(radial, sway), Vec3_Scale(tangent, swirl))));
                    float rLocal = Math_Lerp(tipR, baseR, powf(fmaxf(0.0f, 1.0f - u), rg->taper));
                    Color col = Color_Lerp(rg->baseColor, rg->tipColor, u);
                    col.a = (uint8_t)Math_Clamp(rg->opacity * 255.0f, 32, 255);
                    for (unsigned side = 0; side < 4; ++side) {
                        float sa = 1.5707963f * (float)side;
                        Vector3 n = Vec3_Normalize(Vec3_Add(Vec3_Scale(radial, cosf(sa)), Vec3_Scale(tangent, sinf(sa))));
                        Vector3 pt = Vec3_Add(center, Vec3_Scale(n, rLocal));
                        if (!EmitVertex(opaque, pt, n, col, SURFACE_REGION_TAIL, u, skRoot, &ring[k][side]))
                            return false;
                    }
                }
                for (unsigned k = 0; k < segs; ++k) {
                    for (unsigned side = 0; side < 4; ++side) {
                        unsigned sn = (side + 1u) % 4u;
                        if (!EmitQuad(opaque, ring[k][side], ring[k][sn], ring[k + 1][sn], ring[k + 1][side]))
                            return false;
                    }
                }
            } else if (rg->kind == REPEATED_APPENDAGE_FRILLED_RIBBON) {
                /* Brazo oral ancho en cinta rizada/ondulada de alta resolución (72 tramos, sin dientes de sierra). */
                const unsigned segs = 72;
                float maxW = (rg->ribbonWidth > 1e-4f ? rg->ribbonWidth : rg->baseRadius * 4.5f) * s * dev;
                float rufAmp = rg->ruffleAmplitude * maxW * 0.42f;
                float rufFreq = fmaxf(2.0f, rg->ruffleFrequency);
                MeshIndex rowIdx[73][5]; /* 5 muestras transversales: borde L, medio L, eje, medio R, borde R */

                for (unsigned k = 0; k <= segs; ++k) {
                    float u = (float)k / (float)segs;
                    float swayR = rg->curlAmplitude * s * (0.22f * u + 0.36f * sinf(u * rg->waveFrequency * 3.14159265f + phase) * u);
                    float swayT = rg->curlAmplitude * s * 0.32f * cosf(u * rg->waveFrequency * 2.1f + phase) * u;
                    Vector3 center = Vec3_Add(root, Vec3_Add(
                        Vec3_Create(0.0f, -len * u, 0.0f),
                        Vec3_Add(Vec3_Scale(radial, swayR), Vec3_Scale(tangent, swayT))));
                    /* Perfil de anchura de cinta: ancha en tercio superior-medio, ahusada al final. */
                    float wEnv = sinf(powf(Math_Clamp(u, 0.02f, 0.99f), 0.58f) * 3.14159265f);
                    float halfW = maxW * 0.5f * (0.24f + 0.88f * wEnv) * powf(fmaxf(0.05f, 1.0f - u * 0.70f), rg->taper * 0.42f);
                    /* Giro espiral suave a lo largo del brazo oral */
                    float twist = u * 2.4f + phase * 0.35f;
                    Vector3 wDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(tangent, cosf(twist)), Vec3_Scale(radial, sinf(twist))));
                    Vector3 nDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(radial, cosf(twist)), Vec3_Scale(tangent, -sinf(twist))));

                    for (unsigned j = 0; j < 5; ++j) {
                        float v = ((float)j / 4.0f) * 2.0f - 1.0f; /* -1..+1 */
                        float edge = fabsf(v);
                        /* Pliegues/volantes orgánicos suaves en los bordes libres de la cinta oral. */
                        float wave1 = sinf(u * rufFreq * 6.2831853f + v * 1.2f + phase);
                        float wave2 = 0.32f * sinf(u * rufFreq * 11.2f - v * 1.8f + phase * 1.7f);
                        float ruffle = (wave1 + wave2) * rufAmp * edge * edge;
                        float arch = (1.0f - edge * edge) * halfW * 0.24f;
                        Vector3 pt = Vec3_Add(center, Vec3_Add(
                            Vec3_Scale(wDir, v * halfW),
                            Vec3_Scale(nDir, ruffle + arch)));
                        /* Centro ligeramente más denso, bordes rizados más translúcidos y luminosos. */
                        Color c = Color_Lerp(rg->baseColor, rg->tipColor, Math_Clamp01(u * 0.75f + edge * 0.25f));
                        float alpha = rg->opacity * Math_Lerp(0.95f, 0.48f, edge * 0.65f + u * 0.25f);
                        c.a = (uint8_t)Math_Clamp(alpha * 255.0f, 36, 255);
                        if (!EmitVertex(translucent, pt, nDir, c, SURFACE_REGION_ORAL, u, skRoot, &rowIdx[k][j]))
                            return false;
                    }
                }
                for (unsigned k = 0; k < segs; ++k) {
                    for (unsigned j = 0; j < 4; ++j) {
                        if (!EmitQuad(translucent, rowIdx[k][j], rowIdx[k][j + 1],
                                      rowIdx[k + 1][j + 1], rowIdx[k + 1][j]))
                            return false;
                    }
                }
            }
        }
    }
    return true;
}

static bool RunBuild(const Monster* m, const Skeleton* skel,
                     Mesh* opaqueMesh, SkinBuffer* opaqueSkin,
                     Mesh* translucentMesh, SkinBuffer* translucentSkin) {
    BuildTarget op = {opaqueMesh, opaqueSkin, skel, &m->anatomyGraph};
    BuildTarget tr = {translucentMesh, translucentSkin, skel, &m->anatomyGraph};

    if (!BuildAvianWingFeathers(m, &op)) return false;
    if (!BuildAvianTailFan(m, &op)) return false;
    if (!BuildBatoidFinSurfaces(m, &op)) return false;
    if (!BuildArmSuckers(m, &op)) return false;
    if (!BuildInsectWings(m, &op, &tr)) return false;
    if (!BuildRepeatedAppendages(m, &op, &tr)) return false;
    return true;
}

bool SupplementaryMesh_Build(const Monster* monster, Mesh* opaqueOut, Mesh* translucentOut) {
    if (!monster) return false;
    if (opaqueOut) {
        Mesh_Clear(opaqueOut);
        opaqueOut->hasSurface = false;
        opaqueOut->translucent = false;
        opaqueOut->doubleSided = false;
    }
    if (translucentOut) {
        Mesh_Clear(translucentOut);
        translucentOut->hasSurface = false;
        translucentOut->translucent = true;
        translucentOut->doubleSided = true;
    }
    if (!monster->hasCreaturePhenotype || !monster->hasAnatomyGraph) return true;
    bool ok = RunBuild(monster, NULL, opaqueOut, NULL, translucentOut, NULL);
    if (ok) {
        if (opaqueOut && opaqueOut->vertexCount > 0) {
            opaqueOut->doubleSided = true;
            Mesh_MarkGeometryChanged(opaqueOut);
        }
        if (translucentOut && translucentOut->vertexCount > 0) {
            translucentOut->translucent = true;
            translucentOut->doubleSided = true;
            Mesh_MarkGeometryChanged(translucentOut);
        }
    }
    return ok;
}

bool SupplementaryMesh_BindDeformers(const Monster* monster,
                                     const Skeleton* skeleton,
                                     const Mesh* opaqueMesh,
                                     AnatomyDeformer* opaqueDeformer,
                                     const Mesh* translucentMesh,
                                     AnatomyDeformer* translucentDeformer) {
    if (!monster || !skeleton) return true;
    bool needOp = opaqueMesh && opaqueMesh->vertexCount > 0 && opaqueDeformer;
    bool needTr = translucentMesh && translucentMesh->vertexCount > 0 && translucentDeformer;
    if (!needOp && !needTr) return true;

    SkinBuffer opSkin = {0}, trSkin = {0};
    bool ok = RunBuild(monster, skeleton,
                       NULL, needOp ? &opSkin : NULL,
                       NULL, needTr ? &trSkin : NULL);
    if (ok && needOp) {
        ok = (opSkin.count == opaqueMesh->vertexCount) &&
             AnatomyDeformer_BindExplicitSkin(opaqueDeformer, opaqueMesh, &monster->anatomyGraph, skeleton, opSkin.items);
    }
    if (ok && needTr) {
        ok = (trSkin.count == translucentMesh->vertexCount) &&
             AnatomyDeformer_BindExplicitSkin(translucentDeformer, translucentMesh, &monster->anatomyGraph, skeleton, trSkin.items);
    }
    free(opSkin.items);
    free(trSkin.items);
    return ok;
}
