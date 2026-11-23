/**
 * @file SupplementaryMesh.c
 * @brief Implementación genérica de plumas aviares, aletas batoideas, ventosas, alas de insecto y apéndices repetidos.
 */
#include "SupplementaryMesh.h"
#include "AxialBody.h"
#include "AxialBodyMedusa.h"
#include "FlexibleAppendage.h"
#include "Fin.h"
#include "Limb.h"
#include "MathUtils.h"
#include "MonsterSDF.h"
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

typedef struct SurfaceProjection {
    Vector3 point;
    Vector3 normal;
    bool hit;
} SurfaceProjection;

/* Proyección reutilizable sobre la superficie SDF resuelta mediante sphere-tracing + bisección
 * (opérculo, espiráculo, branquias, placas cefálicas y anillos óseos). */
static SurfaceProjection ProjectOntoSDFSurface(const MonsterSDF* sdf,
                                               Vector3 anchor,
                                               Vector3 dir,
                                               float maxDist,
                                               float surfaceOffset) {
    SurfaceProjection res = {anchor, Vec3_Create(0.0f, 1.0f, 0.0f), false};
    if (!sdf || maxDist <= 1e-5f) return res;
    float dl = Vec3_Length(dir);
    if (dl <= 1e-6f) return res;
    Vector3 d = Vec3_Scale(dir, 1.0f / dl);
    res.normal = d;

    float minStep = fmaxf(0.002f, maxDist * 0.035f);
    float prevT = 0.0f;
    float prevVal = MonsterSDF_EvaluateDistance(sdf, anchor);
    float t = 0.0f;
    float tLo = 0.0f, tHi = 0.0f;
    bool bracketed = false;

    for (unsigned i = 0; i < 18 && t < maxDist; ++i) {
        float step = (prevVal < 0.0f) ? fmaxf(minStep, fabsf(prevVal) * 0.85f) : minStep;
        t = fminf(maxDist, t + step);
        float val = MonsterSDF_EvaluateDistance(sdf, Vec3_Add(anchor, Vec3_Scale(d, t)));
        if (prevVal <= 0.0f && val > 0.0f) {
            tLo = prevT;
            tHi = t;
            bracketed = true;
            break;
        }
        prevT = t;
        prevVal = val;
    }
    if (!bracketed) {
        float bestT = 0.0f;
        float bestAbs = fabsf(MonsterSDF_EvaluateDistance(sdf, anchor));
        for (unsigned i = 1; i <= 10; ++i) {
            float ti = maxDist * ((float)i / 10.0f);
            float val = MonsterSDF_EvaluateDistance(sdf, Vec3_Add(anchor, Vec3_Scale(d, ti)));
            if (fabsf(val) < bestAbs) {
                bestAbs = fabsf(val);
                bestT = ti;
            }
        }
        if (bestAbs > maxDist * 0.25f) return res;
        tLo = tHi = bestT;
    } else {
        for (unsigned iter = 0; iter < 6; ++iter) {
            float mid = 0.5f * (tLo + tHi);
            float val = MonsterSDF_EvaluateDistance(sdf, Vec3_Add(anchor, Vec3_Scale(d, mid)));
            if (val <= 0.0f) tLo = mid;
            else tHi = mid;
        }
    }

    float hitT = 0.5f * (tLo + tHi);
    Vector3 hitP = Vec3_Add(anchor, Vec3_Scale(d, hitT));
    float eps = fmaxf(0.002f, maxDist * 0.004f);
    float gx = MonsterSDF_EvaluateDistance(sdf, Vec3_Create(hitP.x + eps, hitP.y, hitP.z)) -
               MonsterSDF_EvaluateDistance(sdf, Vec3_Create(hitP.x - eps, hitP.y, hitP.z));
    float gy = MonsterSDF_EvaluateDistance(sdf, Vec3_Create(hitP.x, hitP.y + eps, hitP.z)) -
               MonsterSDF_EvaluateDistance(sdf, Vec3_Create(hitP.x, hitP.y - eps, hitP.z));
    float gz = MonsterSDF_EvaluateDistance(sdf, Vec3_Create(hitP.x, hitP.y, hitP.z + eps)) -
               MonsterSDF_EvaluateDistance(sdf, Vec3_Create(hitP.x, hitP.y, hitP.z - eps));
    Vector3 grad = Vec3_Create(gx, gy, gz);
    float gl = Vec3_Length(grad);
    res.normal = (gl > 1e-6f) ? Vec3_Scale(grad, 1.0f / gl) : d;
    res.point = Vec3_Add(hitP, Vec3_Scale(res.normal, surfaceOffset));
    res.hit = true;
    return res;
}

static bool NeedsAquaticSDFProjection(const Monster* m) {
    if (!m || !m->hasCreaturePhenotype) return false;
    const CreaturePhenotype* p = &m->phenotype;
    if (p->axial.archetype == AXIAL_ARCHETYPE_CURVED_VERTICAL && p->axial.abdomenSegments > 0)
        return true;
    if (m->hasHead) {
        HeadArchetype ha = p->head.archetype;
        if (ha == HEAD_ARCHETYPE_TELEOST || ha == HEAD_ARCHETYPE_ELASMOBRANCH ||
            ha == HEAD_ARCHETYPE_SYNGNATHID || ha == HEAD_ARCHETYPE_TESTUDINE ||
            ha == HEAD_ARCHETYPE_CETACEAN) {
            return true;
        }
    }
    return false;
}

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

/* ============================================================================
 * 7. ALETAS GENÉRICAS Y SERIES DE PÍNULAS (CREATURE_MODULE_FIN)
 * ============================================================================ */

static float SuppHash2D(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h ^ (h >> 16)) * (1.0f / 4294967295.0f);
}

static float SuppNoise2D(float x, float y, uint32_t seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float a = SuppHash2D(ix, iy, seed);
    float b = SuppHash2D(ix + 1, iy, seed);
    float c = SuppHash2D(ix, iy + 1, seed);
    float d = SuppHash2D(ix + 1, iy + 1, seed);
    return Math_Lerp(Math_Lerp(a, b, ux), Math_Lerp(c, d, ux), uy);
}

static float ShellSmoothstep(float e0, float e1, float x) {
    float t = Math_Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static Color ShadeColor(Color c, float factor) {
    float r = Math_Clamp((float)c.r * factor, 0.0f, 255.0f);
    float g = Math_Clamp((float)c.g * factor, 0.0f, 255.0f);
    float b = Math_Clamp((float)c.b * factor, 0.0f, 255.0f);
    return (Color){(uint8_t)r, (uint8_t)g, (uint8_t)b, c.a};
}

static bool EmitHydrofoilBlade(BuildTarget* t,
                               Vector3 origin, Vector3 spanDir, Vector3 chordDir, Vector3 normalDir,
                               float baseChord, float span, float thickness, float rootWidth,
                               float sweep, float leadCurve, float trailCurve, float freeRearTip,
                               float tipShape, float camber, unsigned rayCount,
                               Color colBase, Color colTip, SurfaceRegion region,
                               int jRoot, int jMid, int jTip) {
    if (span <= 1e-5f || baseChord <= 1e-5f) return true;
    const unsigned nu = 14, nv = 10;
    MeshIndex topG[15][11], botG[15][11];

    for (unsigned iu = 0; iu <= nu; ++iu) {
        float u = (float)iu / (float)nu;
        /* Perfil de contorno hidrodinámico: borde de ataque convexo, salida falcada y punta libre posterior opcional. */
        float tipWidth = 0.06f + 0.28f * tipShape;
        float leadArch = sinf(u * 3.14159265f) * leadCurve * baseChord * 0.35f;
        float trailArch = sinf(u * 3.14159265f) * trailCurve * baseChord * 0.42f;
        float rearTipExt = 0.0f;
        if (freeRearTip > 0.001f) {
            float tipBand = expf(-powf((u - 0.16f) / 0.16f, 2.0f));
            rearTipExt = freeRearTip * baseChord * tipBand;
        }
        float zLead  =  baseChord * (0.50f * (1.0f - u) + 0.50f * tipWidth * u) - span * sweep * powf(u, 1.15f) + leadArch;
        float zTrail = -baseChord * (0.50f * (1.0f - u) + 0.50f * tipWidth * u) - span * sweep * powf(u, 1.05f) + trailArch - rearTipExt;
        if (zTrail > zLead - baseChord * 0.04f) zTrail = zLead - baseChord * 0.04f;

        float rootFlare = (1.0f - u) * (1.0f - u) * (1.0f - u);
        float maxH = Math_Lerp(thickness, rootWidth, rootFlare) * (1.0f - 0.82f * powf(u, 1.1f));
        float spanCoord = -rootWidth * 0.35f * (1.0f - u) + span * u;
        Color colU = Color_Lerp(colBase, colTip, powf(u, 0.85f));
        AnatomySkinBinding sk = (u < 0.5f)
            ? MakeSkin2(jRoot, 1.0f - u * 2.0f, jMid, u * 2.0f)
            : MakeSkin2(jMid, 2.0f - u * 2.0f, jTip, u * 2.0f - 1.0f);

        for (unsigned iv = 0; iv <= nv; ++iv) {
            float v = (float)iv / (float)nv; /* 0 = borde de ataque, 1 = borde de salida */
            float chordZ = Math_Lerp(zLead, zTrail, v);
            float foil = sinf(powf(v, 0.72f) * 3.14159265f);
            float rayWave = (rayCount >= 4) ? sinf(v * (float)rayCount * 6.2831853f) : 0.0f;
            float rayRidge = (rayCount >= 4 && u > 0.08f)
                ? (1.0f + 0.24f * rayWave * (0.3f + 0.7f * u))
                : 1.0f;
            float h = maxH * foil * rayRidge;
            float cambOff = sinf(v * 3.14159265f) * camber * baseChord * (1.0f - 0.4f * u);
            Vector3 baseP = Vec3_Add(origin,
                Vec3_Add(Vec3_Scale(spanDir, spanCoord), Vec3_Scale(chordDir, chordZ)));
            Vector3 pTop = Vec3_Add(baseP, Vec3_Scale(normalDir,  h + cambOff));
            Vector3 pBot = Vec3_Add(baseP, Vec3_Scale(normalDir, -h + cambOff));

            /* Textura procedural de radios/membrana o micrograno dérmico en aletas. */
            float grain = SuppNoise2D(u * 14.0f, v * 10.0f, 307u + rayCount);
            float leadDark = 1.0f - 0.14f * expf(-v * 6.0f);
            float rayTint = (rayCount >= 4)
                ? (1.0f + 0.11f * rayWave * (0.25f + 0.75f * u) + (grain - 0.5f) * 0.08f)
                : (1.0f + (grain - 0.5f) * 0.10f);
            Color cTopV = ShadeColor(colU, leadDark * rayTint);
            Color cBotV = ShadeColor(colU, leadDark * rayTint * 0.94f);

            float nTilt = (v - 0.35f) * 0.32f + ((rayCount >= 4) ? rayWave * 0.14f : (grain - 0.5f) * 0.06f);
            Vector3 nTop = Vec3_Normalize(Vec3_Add(normalDir, Vec3_Scale(chordDir, nTilt)));
            Vector3 nBot = Vec3_Normalize(Vec3_Add(Vec3_Scale(normalDir, -1.0f), Vec3_Scale(chordDir, nTilt)));
            if (!EmitVertex(t, pTop, nTop, cTopV, region, 0.0f, sk, &topG[iu][iv]) ||
                !EmitVertex(t, pBot, nBot, cBotV, region, 0.4f, sk, &botG[iu][iv])) {
                return false;
            }
        }
    }
    for (unsigned iu = 0; iu < nu; ++iu) {
        for (unsigned iv = 0; iv < nv; ++iv) {
            if (!EmitQuad(t, topG[iu][iv], topG[iu + 1][iv], topG[iu + 1][iv + 1], topG[iu][iv + 1]) ||
                !EmitQuad(t, botG[iu][iv], botG[iu][iv + 1], botG[iu + 1][iv + 1], botG[iu + 1][iv])) {
                return false;
            }
        }
    }
    return true;
}

static bool BuildGenericFins(const Monster* m, const MonsterSDF* sdf, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;

    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_FIN || mod->phenotypeIndex >= p->finCount) continue;
        const FinPhenotype* fin = &p->fins[mod->phenotypeIndex];
        float dev = fin->development * p->development.appendages;
        if (dev <= 0.04f) continue;

        const AnatomyNode* nRoot = AnatomyGraph_FindModuleNode(g, mod->instanceId, FIN_NODE_ROOT);
        const AnatomyNode* nMid  = AnatomyGraph_FindModuleNode(g, mod->instanceId, FIN_NODE_MID);
        const AnatomyNode* nTip  = AnatomyGraph_FindModuleNode(g, mod->instanceId, FIN_NODE_TIP);
        if (!nRoot || !nMid || !nTip) continue;

        int jRoot = FindJointSafe(g, opaque->skeleton, nRoot->id);
        int jMid  = FindJointSafe(g, opaque->skeleton, nMid->id);
        int jTip  = FindJointSafe(g, opaque->skeleton, nTip->id);

        Color cBase = fin->baseColor.a > 0 ? fin->baseColor : p->surface.pigment.baseColor;
        Color cTip  = fin->tipColor.a  > 0 ? fin->tipColor  : Color_Lerp(cBase, p->surface.pigment.secondaryColor, 0.35f);
        SurfaceRegion region = (fin->role == FIN_ROLE_PECTORAL) ? SURFACE_REGION_FORELIMB
                             : (fin->role == FIN_ROLE_PELVIC)   ? SURFACE_REGION_HINDLIMB
                                                                : SURFACE_REGION_ORNAMENT;

        /* Recuperar el eje de cuerda almacenado en la conexión de soporte. */
        Vector3 chordDir = Vec3_Create(0, 0, 1);
        for (size_t ci = 0; ci < g->connectionCount; ++ci) {
            if (g->connections[ci].moduleInstanceId == mod->instanceId &&
                Vec3_LengthSq(g->connections[ci].transverseAxis) > 1e-4f) {
                chordDir = Vec3_Normalize(g->connections[ci].transverseAxis);
                break;
            }
        }

        if (fin->role == FIN_ROLE_FINLET_SERIES) {
            unsigned count = fin->finletCount ? fin->finletCount : 8u;
            if (count > 16u) count = 16u;
            Vector3 startP = nRoot->center;
            Vector3 endP = nTip->center;
            Vector3 spanDir = Vec3_Sub(nMid->center, Vec3_Lerp(startP, endP, 0.5f));
            float sl = Vec3_Length(spanDir);
            spanDir = sl > 1e-5f ? Vec3_Scale(spanDir, 1.0f / sl) : Vec3_Create(0, 1, 0);
            Vector3 axisDir = Vec3_Normalize(Vec3_Sub(startP, endP));
            Vector3 normalDir = Vec3_Normalize(Vec3_Cross(axisDir, spanDir));
            if (Vec3_LengthSq(normalDir) < 1e-5f) normalDir = Vec3_Create(1, 0, 0);

            /* Centro de la columna posterior para proyectar cada pínula sobre la curva real del tronco/pedúnculo. */
            const AnatomyNode* pelNode = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_PELVIS);
            float spineY = pelNode ? pelNode->center.y : p->axial.withersHeight * s;

            float seriesDist = Vec3_Length(Vec3_Sub(endP, startP));
            float singleChord = fmaxf(0.015f * s, (seriesDist / (float)count) * 0.72f);
            float singleSpan  = fin->span * s * dev;
            float singleThick = fmaxf(0.004f * s, fin->thickness * s * dev);
            for (unsigned k = 0; k < count; ++k) {
                float u = count > 1 ? (float)k / (float)(count - 1) : 0.5f;
                float env = 0.55f + 0.55f * sinf(u * 3.14159265f);
                Vector3 pos = Vec3_Lerp(startP, endP, u);
                if (sdf) {
                    /* Proyectar desde el eje espinal hacia spanDir sobre el SDF real para que ninguna pínula flote. */
                    Vector3 spineAnchor = Vec3_Create(0.0f, spineY, pos.z);
                    float maxR = fmaxf(0.25f * s, p->axial.abdomenHeight * 0.75f * s);
                    SurfaceProjection sp = ProjectOntoSDFSurface(sdf, spineAnchor, spanDir, maxR, -0.014f * s);
                    if (sp.hit) {
                        pos = sp.point;
                    }
                }
                int jA = (u < 0.5f) ? jRoot : jMid;
                int jB = (u < 0.5f) ? jMid : jTip;
                if (!EmitHydrofoilBlade(opaque, pos, spanDir, axisDir, normalDir,
                                        singleChord * (0.75f + 0.25f * env), singleSpan * env,
                                        singleThick, singleThick * 1.2f,
                                        0.78f, 0.20f, 0.35f, 0.0f, 0.12f, 0.0f, 0,
                                        cBase, cTip, region, jA, jB, jB)) {
                    return false;
                }
            }
            continue;
        }

        Vector3 spanVec = Vec3_Sub(nTip->center, nRoot->center);
        /* Quitar la componente de barrido a lo largo de chordDir para obtener el eje puro de envergadura. */
        Vector3 spanOrtho = Vec3_Sub(spanVec, Vec3_Scale(chordDir, Vec3_Dot(spanVec, chordDir)));
        float spanLen = Vec3_Length(spanOrtho);
        if (spanLen <= 1e-5f) continue;
        Vector3 spanDir = Vec3_Scale(spanOrtho, 1.0f / spanLen);
        Vector3 normalDir = Vec3_Normalize(Vec3_Cross(chordDir, spanDir));
        if (Vec3_LengthSq(normalDir) < 1e-5f) normalDir = Vec3_Create(1, 0, 0);

        float rootW = fmaxf(0.006f * s, fin->rootWidth * s * dev);
        Vector3 origin = Vec3_Add(nRoot->center, Vec3_Scale(spanDir, rootW * 0.45f));
        if (!EmitHydrofoilBlade(opaque, origin, spanDir, chordDir, normalDir,
                                fin->baseChord * s * dev, fin->span * s * dev,
                                fin->thickness * s * dev, rootW,
                                fin->sweep, fin->leadingCurve, fin->trailingCurve,
                                fin->freeRearTip, fin->tipShape, fin->camber, fin->rayCount,
                                cBase, cTip, region, jRoot, jMid, jTip)) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * 8. PROPULSORES CAUDALES REUTILIZABLES (LUNADO, HETEROCERCO, FLUKES HORIZONTALES + QUILLAS)
 * ============================================================================ */

static bool EmitPeduncleKeelRidge(BuildTarget* opaque,
                                  Vector3 pStart, Vector3 pEnd,
                                  Vector3 outDir, Vector3 upDir,
                                  float maxWidth, float maxThick,
                                  Color col, int jStart, int jEnd) {
    const unsigned nu = 8;
    MeshIndex topRow[9], tipRow[9], botRow[9];
    Vector3 fwd = Vec3_Normalize(Vec3_Sub(pStart, pEnd));
    for (unsigned iu = 0; iu <= nu; ++iu) {
        float u = (float)iu / (float)nu;
        Vector3 c = Vec3_Lerp(pStart, pEnd, u);
        float env = sinf(u * 3.14159265f);
        float w = maxWidth * env;
        float th = maxThick * (0.25f + 0.75f * env);
        AnatomySkinBinding sk = MakeSkin2(jStart, 1.0f - u, jEnd, u);
        Vector3 pTop = Vec3_Add(c, Vec3_Scale(upDir,  th));
        Vector3 pBot = Vec3_Add(c, Vec3_Scale(upDir, -th));
        Vector3 pTip = Vec3_Add(c, Vec3_Scale(outDir, w));
        Vector3 nTop = Vec3_Normalize(Vec3_Add(upDir, Vec3_Scale(outDir, 0.55f)));
        Vector3 nBot = Vec3_Normalize(Vec3_Add(Vec3_Scale(upDir, -1.0f), Vec3_Scale(outDir, 0.55f)));
        Vector3 nTip = Vec3_Normalize(Vec3_Add(outDir, Vec3_Scale(fwd, 0.08f)));
        if (!EmitVertex(opaque, pTop, nTop, col, SURFACE_REGION_TAIL, 0.25f, sk, &topRow[iu]) ||
            !EmitVertex(opaque, pTip, nTip, col, SURFACE_REGION_TAIL, 0.35f, sk, &tipRow[iu]) ||
            !EmitVertex(opaque, pBot, nBot, col, SURFACE_REGION_TAIL, 0.55f, sk, &botRow[iu])) {
            return false;
        }
    }
    for (unsigned iu = 0; iu < nu; ++iu) {
        if (!EmitQuad(opaque, topRow[iu], tipRow[iu], tipRow[iu + 1], topRow[iu + 1]) ||
            !EmitQuad(opaque, tipRow[iu], botRow[iu], botRow[iu + 1], tipRow[iu + 1])) {
            return false;
        }
    }
    return true;
}

static bool EmitCetaceanFlukes(BuildTarget* opaque,
                               Vector3 rootCenter, Vector3 rightDir, Vector3 fwdDir, Vector3 upDir,
                               float totalSpan, float chord, float thick, float notch, float sweep,
                               Color colDorsal, Color colVentral, Color colTip,
                               int jPrev, int jTip) {
    /* Geometría dedicada para flukes cetáceos horizontales: puente continuo en el pedúnculo,
     * dos lóbulos curvos y una escotadura mediana abierta sin muñón sobresaliente. */
    float halfSpan = totalSpan * 0.50f;
    const unsigned nu = 12, nv = 6;
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        float sgn = (sideIdx == 0) ? 1.0f : -1.0f;
        Vector3 spanDir = Vec3_Scale(rightDir, sgn);
        MeshIndex topG[13][7], botG[13][7];

        for (unsigned iu = 0; iu <= nu; ++iu) {
            float u = (float)iu / (float)nu;
            /* En u=0 el borde de ataque abraza el pedúnculo (+0.48*chord) y el borde de salida
             * forma una escotadura mediana genuinamente abierta (zTrail > -0.18*chord). */
            float notchPinch = expf(-powf(u / 0.14f, 2.0f));
            float zLead = chord * (0.48f * (1.0f - u) + 0.08f * u) - halfSpan * sweep * powf(u, 1.25f)
                        + sinf(u * 3.14159265f) * chord * 0.18f;
            float zTrailBase = -chord * (0.44f * (1.0f - u) + 0.04f * u) - halfSpan * sweep * powf(u, 1.08f)
                             + sinf(u * 3.14159265f) * chord * 0.24f * notch;
            float zTrail = zTrailBase + notchPinch * chord * (0.28f + 0.14f * notch);
            if (zTrail > zLead - chord * 0.04f) zTrail = zLead - chord * 0.04f;

            float spanCoord = halfSpan * u;
            float maxH = thick * (1.30f * (1.0f - u) * (1.0f - u) + 0.55f * (1.0f - u)) * (1.0f - 0.85f * powf(u, 1.1f));
            maxH = fmaxf(maxH, thick * 0.08f);
            Color cTop = Color_Lerp(colDorsal, colTip, powf(u, 0.85f));
            Color cBot = Color_Lerp(colVentral, colTip, powf(u, 0.75f));
            AnatomySkinBinding sk = (u < 0.22f)
                ? MakeSkin2(jPrev, (0.22f - u) / 0.22f * 0.45f, jTip, 1.0f - (0.22f - u) / 0.22f * 0.45f)
                : MakeSkin1(jTip);

            for (unsigned iv = 0; iv <= nv; ++iv) {
                float v = (float)iv / (float)nv;
                float chordZ = Math_Lerp(zLead, zTrail, v);
                float foil = sinf(powf(v, 0.70f) * 3.14159265f);
                float h = maxH * foil;
                Vector3 bp = Vec3_Add(rootCenter,
                    Vec3_Add(Vec3_Scale(spanDir, spanCoord), Vec3_Scale(fwdDir, chordZ)));
                Vector3 pT = Vec3_Add(bp, Vec3_Scale(upDir,  h));
                Vector3 pB = Vec3_Add(bp, Vec3_Scale(upDir, -h));
                Vector3 nT = Vec3_Normalize(Vec3_Add(upDir, Vec3_Scale(fwdDir, (v - 0.35f) * 0.24f)));
                Vector3 nB = Vec3_Normalize(Vec3_Add(Vec3_Scale(upDir, -1.0f), Vec3_Scale(fwdDir, (v - 0.35f) * 0.24f)));
                if (!EmitVertex(opaque, pT, nT, cTop, SURFACE_REGION_TAIL, 0.05f, sk, &topG[iu][iv]) ||
                    !EmitVertex(opaque, pB, nB, cBot, SURFACE_REGION_TAIL, 0.85f, sk, &botG[iu][iv])) {
                    return false;
                }
            }
        }
        for (unsigned iu = 0; iu < nu; ++iu) {
            for (unsigned iv = 0; iv < nv; ++iv) {
                if (sideIdx == 0) {
                    if (!EmitQuad(opaque, topG[iu][iv], topG[iu + 1][iv], topG[iu + 1][iv + 1], topG[iu][iv + 1]) ||
                        !EmitQuad(opaque, botG[iu][iv], botG[iu][iv + 1], botG[iu + 1][iv + 1], botG[iu + 1][iv]))
                        return false;
                } else {
                    if (!EmitQuad(opaque, topG[iu][iv], topG[iu][iv + 1], topG[iu + 1][iv + 1], topG[iu + 1][iv]) ||
                        !EmitQuad(opaque, botG[iu][iv], botG[iu + 1][iv], botG[iu + 1][iv + 1], botG[iu][iv + 1]))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool BuildCaudalPropulsors(const Monster* m, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;

    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_TAIL || mod->phenotypeIndex >= p->tailCount) continue;
        const TailPhenotype* tail = &p->tails[mod->phenotypeIndex];
        if (tail->caudalFinType == CAUDAL_FIN_NONE) continue;
        float dev = tail->development * p->development.appendages;
        if (dev <= 0.05f || tail->caudalSpan <= 1e-4f) continue;

        unsigned segN = tail->segmentCount >= 2 ? tail->segmentCount : 2;
        const AnatomyNode* tipNode = AnatomyGraph_FindModuleNode(g, mod->instanceId, (uint16_t)segN);
        const AnatomyNode* prevNode = AnatomyGraph_FindModuleNode(g, mod->instanceId, (uint16_t)(segN - 1));
        if (!tipNode || !prevNode) continue;

        Vector3 tailDir = Vec3_Sub(tipNode->center, prevNode->center);
        float tLen = Vec3_Length(tailDir);
        tailDir = tLen > 1e-5f ? Vec3_Scale(tailDir, 1.0f / tLen) : Vec3_Create(0, 0, -1);
        Vector3 fwdDir = Vec3_Scale(tailDir, -1.0f); /* +Z hacia la cabeza */

        Vector3 upRef = fabsf(fwdDir.y) < 0.9f ? Vec3_Create(0, 1, 0) : Vec3_Create(0, 0, 1);
        Vector3 rightDir = Vec3_Normalize(Vec3_Cross(upRef, fwdDir));
        Vector3 upDir = Vec3_Normalize(Vec3_Cross(fwdDir, rightDir));

        int jPrev = FindJointSafe(g, opaque->skeleton, prevNode->id);
        int jTip  = FindJointSafe(g, opaque->skeleton, tipNode->id);

        Color colDorsal = tail->caudalColor.a > 0 ? tail->caudalColor : p->surface.pigment.baseColor;
        Color colTip = Color_Lerp(colDorsal, p->surface.pigment.secondaryColor, 0.28f);
        Color colVentral = Color_Lerp(p->surface.pigment.ventralColor, colDorsal, 0.35f);

        float totalSpan = tail->caudalSpan * s * dev;
        float chord = fmaxf(0.04f * s, tail->caudalChord * s * dev);
        float thick = fmaxf(0.008f * s, tail->caudalThickness * s * dev);
        float notch = Math_Clamp01(tail->caudalNotchDepth);
        float sweep = Math_Clamp(tail->caudalSweep, 0.0f, 1.4f);

        /* Anclar el propulsor caudal justo sobre la raíz terminal ahusada para que el pedúnculo
         * entre en el borde de ataque y la escotadura posterior quede completamente libre. */
        Vector3 rootCenter = Vec3_Add(tipNode->center, Vec3_Scale(tailDir, chord * 0.14f));

        if (tail->caudalFinType == CAUDAL_FIN_HORIZONTAL_FLUKES) {
            if (!EmitCetaceanFlukes(opaque, rootCenter, rightDir, fwdDir, upDir,
                                    totalSpan, chord, thick, notch, sweep,
                                    colDorsal, colVentral, colTip, jPrev, jTip)) {
                return false;
            }
        } else if (tail->caudalFinType == CAUDAL_FIN_ROUNDED_VERTICAL) {
            /* Aleta caudal vertical pequeña y redondeada (juveniles). */
            float halfSpan = totalSpan * 0.50f;
            for (int lobe = 0; lobe < 2; ++lobe) {
                float sgn = lobe == 0 ? 1.0f : -1.0f;
                Vector3 lobeSpan = Vec3_Scale(upDir, sgn);
                Vector3 lobeNorm = Vec3_Scale(rightDir, sgn);
                if (!EmitHydrofoilBlade(opaque, rootCenter, lobeSpan, fwdDir, lobeNorm,
                                        chord, halfSpan, thick, thick * 1.2f,
                                        0.18f, 0.35f, -0.25f, 0.0f, 0.85f, 0.0f, 8,
                                        colDorsal, colTip, SURFACE_REGION_TAIL,
                                        jPrev, jTip, jTip)) {
                    return false;
                }
            }
        } else {
            /* HOMOCERCAL_LUNATE (simétrica) o HETEROCERCAL (lóbulo superior alargado de tiburón). */
            bool hetero = (tail->caudalFinType == CAUDAL_FIN_HETEROCERCAL);
            float ratio = hetero ? Math_Clamp(tail->caudalUpperLobeRatio, 1.15f, 2.2f) : 1.0f;
            float lowerSpan = totalSpan / (1.0f + ratio);
            float upperSpan = totalSpan - lowerSpan;

            /* Lóbulo superior */
            if (!EmitHydrofoilBlade(opaque, rootCenter, upDir, fwdDir, rightDir,
                                    chord * (hetero ? 1.12f : 0.96f), upperSpan,
                                    thick, thick * 1.45f,
                                    sweep * (hetero ? 1.16f : 1.0f),
                                    hetero ? 0.28f : 0.42f,
                                    hetero ? 0.42f : (0.58f * notch + 0.16f),
                                    0.0f, hetero ? 0.16f : 0.10f, 0.0f, hetero ? 0 : 12,
                                    colDorsal, colTip, SURFACE_REGION_TAIL,
                                    jPrev, jTip, jTip)) {
                return false;
            }
            /* Lóbulo inferior */
            if (!EmitHydrofoilBlade(opaque, rootCenter, Vec3_Scale(upDir, -1.0f), fwdDir, Vec3_Scale(rightDir, -1.0f),
                                    chord * (hetero ? 0.84f : 0.96f), lowerSpan,
                                    thick * 0.92f, thick * 1.38f,
                                    sweep * (hetero ? 0.82f : 1.0f),
                                    hetero ? 0.24f : 0.42f,
                                    hetero ? 0.36f : (0.58f * notch + 0.16f),
                                    0.0f, 0.12f, 0.0f, hetero ? 0 : 12,
                                    colDorsal, colTip, SURFACE_REGION_TAIL,
                                    jPrev, jTip, jTip)) {
                return false;
            }
        }

        /* Quillas laterales del pedúnculo caudal (túnidos / escómbridos): quilla central prominente
         * a cada lado + dos quillas accesorias oblicuas junto a las bases de los lóbulos caudales. */
        if (tail->caudalKeelStrength > 0.02f && segN >= 2) {
            const AnatomyNode* midNode = segN >= 3
                ? AnatomyGraph_FindModuleNode(g, mod->instanceId, (uint16_t)(segN - 2))
                : prevNode;
            if (!midNode) midNode = prevNode;
            int jMid = FindJointSafe(g, opaque->skeleton, midNode->id);
            float ks = Math_Clamp01(tail->caudalKeelStrength) * dev;
            Color keelCol = Color_Lerp(p->surface.pigment.baseColor, colDorsal, 0.50f);
            float pedW = fmaxf(prevNode->widthRadius, 0.02f * s);
            float pedH = fmaxf(prevNode->heightRadius, 0.02f * s);
            Vector3 kStart = Vec3_Lerp(midNode->center, prevNode->center, 0.35f);
            Vector3 kEnd   = Vec3_Lerp(prevNode->center, tipNode->center, 0.92f);
            for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
                float sgn = (sideIdx == 0) ? 1.0f : -1.0f;
                Vector3 outDir = Vec3_Scale(rightDir, sgn);
                Vector3 pS = Vec3_Add(kStart, Vec3_Scale(outDir, pedW * 0.72f));
                Vector3 pE = Vec3_Add(kEnd,   Vec3_Scale(outDir, tipNode->widthRadius * 0.65f));
                if (!EmitPeduncleKeelRidge(opaque, pS, pE, outDir, upDir,
                                           pedW * 0.85f * ks, pedH * 0.22f * ks,
                                           keelCol, jMid, jTip)) {
                    return false;
                }
                /* Quillas accesorias pequeñas superior e inferior en la base de los lóbulos caudales. */
                for (int acc = -1; acc <= 1; acc += 2) {
                    Vector3 aS = Vec3_Add(Vec3_Lerp(prevNode->center, tipNode->center, 0.72f),
                                          Vec3_Add(Vec3_Scale(outDir, tipNode->widthRadius * 0.60f),
                                                   Vec3_Scale(upDir, (float)acc * pedH * 0.42f)));
                    Vector3 aE = Vec3_Add(Vec3_Add(tipNode->center, Vec3_Scale(tailDir, chord * 0.16f)),
                                          Vec3_Add(Vec3_Scale(outDir, thick * 0.40f),
                                                   Vec3_Scale(upDir, (float)acc * pedH * 0.95f)));
                    if (!EmitPeduncleKeelRidge(opaque, aS, aE, outDir, upDir,
                                               pedW * 0.36f * ks, pedH * 0.11f * ks,
                                               keelCol, jPrev, jTip)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

/* ============================================================================
 * 9. ALETAS DE TETRÁPODO MARINO (TORTUGA MARINA Y DELFÍN) + UÑA ANTERIOR OPCIONAL
 * ============================================================================ */

static bool BuildMarineFlippers(const Monster* m, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;
    Color dorsal = p->surface.pigment.baseColor;
    Color ventral = p->surface.pigment.ventralColor;

    for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
        const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
        if (mod->kind != CREATURE_MODULE_LIMB || mod->phenotypeIndex >= p->limbCount) continue;
        const LimbPhenotype* limb = &p->limbs[mod->phenotypeIndex];
        if (limb->archetype != LIMB_ARCHETYPE_FLIPPER || limb->anatomyProfile != LIMB_PROFILE_MARINE_FLIPPER)
            continue;
        float dev = limb->development * p->development.appendages;
        if (dev <= 0.05f) continue;

        const AnatomyNode* n[4];
        for (unsigned k = 0; k < 4; ++k) {
            n[k] = AnatomyGraph_FindModuleNode(g, mod->instanceId, (uint16_t)(k + 1));
            if (!n[k]) break;
        }
        if (!n[0] || !n[1] || !n[2] || !n[3]) continue;

        int jn[4];
        for (unsigned k = 0; k < 4; ++k) jn[k] = FindJointSafe(g, opaque->skeleton, n[k]->id);

        bool fore = (limb->role == LIMB_FORE || limb->role == LIMB_WING);
        float side = (limb->side == LIMB_RIGHT) ? -1.0f : 1.0f;
        float span = limb->length * s * dev;
        float aspect = limb->wingAspect > 0.5f ? limb->wingAspect : (fore ? 3.2f : 2.1f);
        float maxChord = fmaxf(0.035f * s, span / aspect);
        float maxThick = fmaxf(0.008f * s, limb->thickness * s * dev);
        SurfaceRegion region = fore ? SURFACE_REGION_FORELIMB : SURFACE_REGION_HINDLIMB;

        const unsigned nu = 20, nv = 12;
        MeshIndex topG[21][13], botG[21][13];
        bool scaledFlipper = (p->surface.integument.type == INTEGUMENT_SCALES);
        Color scaleSeamCol = Color_Lerp(p->surface.pigment.ventralColor, Color_FromRGB(168, 160, 126), 0.40f);
        Color scaleDarkCol = Color_Lerp(dorsal, p->surface.pigment.secondaryColor, 0.55f);
        Vector3 clawAnchor = n[1]->center;
        Vector3 clawFwd = Vec3_Create(0, 0, 1);
        Vector3 clawOut = Vec3_Create(side, 0, 0);
        Vector3 clawUp = Vec3_Create(0, 1, 0);
        int clawJointA = jn[1], clawJointB = jn[2];

        for (unsigned iu = 0; iu <= nu; ++iu) {
            float u = (float)iu / (float)nu;
            float segF = u * 3.0f;
            unsigned seg = segF >= 3.0f ? 2u : (unsigned)segF;
            float lt = segF - (float)seg;
            Vector3 c = Vec3_Lerp(n[seg]->center, n[seg + 1]->center, lt);
            Vector3 tanDir = Vec3_Normalize(Vec3_Sub(n[seg + 1]->center, n[seg]->center));
            Vector3 chordAxis = Vec3_Create(0, 0, 1);
            Vector3 upAxis = Vec3_Normalize(Vec3_Cross(chordAxis, Vec3_Scale(tanDir, side)));
            if (Vec3_LengthSq(upAxis) < 1e-5f) upAxis = Vec3_Create(0, 1, 0);

            /* Raíz proximal ligeramente redondeada e integrada al hombro sin barra sobresaliente. */
            float env = sinf(powf(u, 0.62f) * 3.14159265f);
            float chordU = maxChord * (0.44f * (1.0f - u) + 0.82f * env);
            if (iu == nu) chordU = maxChord * 0.14f;
            float thickU = maxThick * (1.15f * (1.0f - u) * (1.0f - u) + 0.85f * (1.0f - 0.76f * powf(u, 0.9f)));
            AnatomySkinBinding sk = MakeSkin2(jn[seg], 1.0f - lt, jn[seg + 1], lt);

            if (iu == 8) {
                clawAnchor = Vec3_Add(c, Vec3_Scale(chordAxis, chordU * 0.52f));
                clawFwd = chordAxis;
                clawOut = tanDir;
                clawUp = upAxis;
                clawJointA = jn[seg];
                clawJointB = jn[seg + 1];
            }

            for (unsigned iv = 0; iv <= nv; ++iv) {
                float v = (float)iv / (float)nv;
                float cz = chordU * (0.55f - v);
                float foil = sinf(powf(v, 0.72f) * 3.14159265f);
                float h = thickU * foil;
                Vector3 bp = Vec3_Add(c, Vec3_Scale(chordAxis, cz));

                Color cD, cV;
                Vector3 nT = upAxis, nB = Vec3_Scale(upAxis, -1.0f);
                if (scaledFlipper) {
                    /* Escamas reptilianas poligonales oscuras de Chelonia mydas con suturas crema-ocre. */
                    float su = u * (fore ? 13.0f : 8.0f);
                    float sv = v * (fore ? 7.0f : 5.5f) + 0.5f * ((int)floorf(su) & 1);
                    float fu = su - floorf(su) - 0.5f;
                    float fv = sv - floorf(sv) - 0.5f;
                    float cellDist = fmaxf(fabsf(fu), fabsf(fv));
                    float scaleMask = 1.0f - ShellSmoothstep(0.30f, 0.46f, cellDist);
                    float var = SuppNoise2D(su * 0.9f, sv * 0.9f, 521u);
                    float bump = thickU * 0.14f * scaleMask * foil;
                    h += bump;
                    Color darkScale = Color_Lerp(scaleDarkCol, Color_FromRGB(34, 34, 22), 0.38f);
                    Color plateCol = Color_Lerp(darkScale, dorsal, 0.30f + 0.50f * var);
                    Color seamCol = Color_Lerp(scaleSeamCol, Color_FromRGB(142, 132, 96), 0.55f);
                    cD = Color_Lerp(seamCol, plateCol, scaleMask * 0.94f);
                    Color vWarm = Color_Lerp(ventral, Color_FromRGB(168, 154, 112), 0.35f);
                    Color vPlate = Color_Lerp(vWarm, seamCol, 0.30f + 0.35f * var);
                    cV = Color_Lerp(Color_Lerp(vWarm, dorsal, 0.24f), vPlate, scaleMask * 0.88f);
                    nT = Vec3_Normalize(Vec3_Add(upAxis, Vec3_Add(Vec3_Scale(tanDir, fu * 0.42f), Vec3_Scale(chordAxis, fv * 0.42f))));
                    nB = Vec3_Normalize(Vec3_Add(Vec3_Scale(upAxis, -1.0f), Vec3_Add(Vec3_Scale(tanDir, fu * 0.28f), Vec3_Scale(chordAxis, fv * 0.28f))));
                } else {
                    /* Piel hidrodinámica suave con contracoloración natural (cetáceos). */
                    float grain = SuppNoise2D(u * 12.0f, v * 8.0f, 809u);
                    cD = ShadeColor(Color_Lerp(dorsal, p->surface.pigment.secondaryColor, 0.20f * u), 0.96f + 0.08f * grain);
                    cV = ShadeColor(Color_Lerp(ventral, dorsal, 0.32f * (0.35f + 0.65f * u)), 0.95f + 0.08f * grain);
                }

                Vector3 pT = Vec3_Add(bp, Vec3_Scale(upAxis,  h));
                Vector3 pB = Vec3_Add(bp, Vec3_Scale(upAxis, -h));
                if (!EmitVertex(opaque, pT, nT, cD, region, 0.0f, sk, &topG[iu][iv]) ||
                    !EmitVertex(opaque, pB, nB, cV, region, 1.0f, sk, &botG[iu][iv])) {
                    return false;
                }
            }
        }
        for (unsigned iu = 0; iu < nu; ++iu) {
            for (unsigned iv = 0; iv < nv; ++iv) {
                if (!EmitQuad(opaque, topG[iu][iv], topG[iu + 1][iv], topG[iu + 1][iv + 1], topG[iu][iv + 1]) ||
                    !EmitQuad(opaque, botG[iu][iv], botG[iu][iv + 1], botG[iu + 1][iv + 1], botG[iu + 1][iv])) {
                    return false;
                }
            }
        }

        /* Uña única queratinizada en el borde de ataque de la aleta (Chelonia mydas: clawScale > 0). */
        if (limb->clawScale > 0.02f) {
            float clLen = fmaxf(0.022f * s, maxChord * 0.24f * limb->clawScale);
            float clRad = fmaxf(0.008f * s, maxThick * 0.52f * limb->clawScale);
            Vector3 cDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(clawFwd, 0.72f), Vec3_Scale(clawOut, 0.68f)));
            Vector3 cSide = Vec3_Normalize(Vec3_Cross(clawUp, cDir));
            if (Vec3_LengthSq(cSide) < 1e-5f) cSide = clawOut;
            Color clawCol = Color_FromRGB(46, 40, 30);
            AnatomySkinBinding skClaw = MakeSkin2(clawJointA, 0.55f, clawJointB, 0.45f);
            const unsigned nRing = 6;
            MeshIndex baseR[7], midR[7], tipIdx = 0;
            Vector3 baseC = Vec3_Sub(clawAnchor, Vec3_Scale(cDir, clLen * 0.22f));
            Vector3 midC  = Vec3_Add(clawAnchor, Vec3_Add(Vec3_Scale(cDir, clLen * 0.45f), Vec3_Scale(clawUp, -clLen * 0.08f)));
            Vector3 tipP  = Vec3_Add(clawAnchor, Vec3_Add(Vec3_Scale(cDir, clLen), Vec3_Scale(clawUp, -clLen * 0.22f)));
            if (!EmitVertex(opaque, tipP, cDir, clawCol, region, 0.2f, skClaw, &tipIdx)) return false;
            for (unsigned k = 0; k <= nRing; ++k) {
                float ang = (float)k / (float)nRing * 6.2831853f;
                Vector3 rad = Vec3_Add(Vec3_Scale(cSide, cosf(ang)), Vec3_Scale(clawUp, sinf(ang)));
                Vector3 p0 = Vec3_Add(baseC, Vec3_Scale(rad, clRad));
                Vector3 p1 = Vec3_Add(midC,  Vec3_Scale(rad, clRad * 0.58f));
                if (!EmitVertex(opaque, p0, rad, clawCol, region, 0.2f, skClaw, &baseR[k]) ||
                    !EmitVertex(opaque, p1, rad, clawCol, region, 0.2f, skClaw, &midR[k]))
                    return false;
            }
            for (unsigned k = 0; k < nRing; ++k) {
                if (!EmitQuad(opaque, baseR[k], baseR[k + 1], midR[k + 1], midR[k]) ||
                    !EmitTri(opaque, midR[k], midR[k + 1], tipIdx))
                    return false;
            }
        }
    }
    return true;
}

/* ============================================================================
 * 10. CAPARAZÓN ANATÓMICO REUTILIZABLE (ESPALDAR LISO + PUENTE CURVO + PLASTRÓN CON SUTURAS)
 * ============================================================================ */

static bool BuildAnatomicalShell(const Monster* m, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const ShellPhenotype* sh = &p->shell;
    if (sh->development <= 0.02f) return true;

    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;
    float dev = Math_Clamp01(sh->development);

    const AnatomyNode* pec = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_PECTORAL);
    const AnatomyNode* thx = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* pel = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_PELVIS);
    if (!thx) thx = AnatomyGraph_FindFirstRegion(g, ANATOMY_REGION_TRUNK);
    if (!thx) return true;

    Vector3 center = (pec && pel) ? Vec3_Lerp(pec->center, pel->center, 0.48f) : thx->center;
    int jTrunk = FindJointSafe(g, opaque->skeleton, thx->id);
    AnatomySkinBinding rigidSkin = MakeSkin1(jTrunk);

    float halfL = sh->length * 0.50f * s * dev;
    float halfW = sh->width  * 0.50f * s * dev;
    float domeH = sh->height * s * dev;
    float plastD = sh->plastronDepth * s * dev;
    float convex = Math_Clamp(sh->dorsalConvexity, 0.15f, 1.0f);
    float heart = Math_Clamp01(sh->heartTaper);
    float relief = Math_Clamp01(sh->scuteRelief);
    unsigned costals = sh->costalScutePairs ? sh->costalScutePairs : 4u;
    unsigned vertebrals = sh->vertebralScutes ? sh->vertebralScutes : 5u;

    Color cCara = sh->carapaceColor.a > 0 ? sh->carapaceColor : p->dorsalColor;
    Color cBord = sh->scuteBorderColor.a > 0 ? sh->scuteBorderColor : Color_FromRGB(26, 20, 12);
    Color cPlasRaw = sh->plastronColor.a > 0 ? sh->plastronColor : p->ventralColor;
    /* Plastrón crema-amarillento cálido natural sin sobreexposición. */
    Color cPlas = Color_Lerp(cPlasRaw, Color_FromRGB(168, 152, 108), 0.36f);
    Color cPlasSeam = Color_Lerp(cPlas, cBord, 0.55f);

    /* 1. Espaldar dorsal (carapace): caparazón queratinoso rígido con perímetro liso,
     * surcos incisivos nítidos, biseles de escudo y patrón radial castaño/ámbar/oliva (Chelonia mydas). */
    const unsigned nZ = 36, nX = 28;
    MeshIndex caraTop[37][29], caraRim[37][29];
    Color cAmber = Color_FromRGB(138, 104, 52);
    Color cOliveDark = Color_FromRGB(46, 50, 30);
    for (unsigned iz = 0; iz <= nZ; ++iz) {
        float u = (float)iz / (float)nZ;
        float zNorm = 1.0f - 2.0f * u;
        float oval = sqrtf(fmaxf(0.0f, 1.0f - zNorm * zNorm));
        float heartMul = 1.0f - heart * 0.34f * Math_Clamp01((u - 0.35f) / 0.65f);
        float notch = (u < 0.12f) ? (sh->cervicalNotch * 0.42f * (1.0f - u / 0.12f)) : 0.0f;
        float rowW = halfW * oval * heartMul;
        float zPos = center.z + (zNorm - notch) * halfL;

        for (unsigned ix = 0; ix <= nX; ++ix) {
            float v = (float)ix / (float)nX;
            float xNorm = 2.0f * v - 1.0f;
            float ax = fabsf(xNorm);
            float xPos = center.x + xNorm * rowW;
            float domeProfile = powf(fmaxf(0.0f, 1.0f - xNorm * xNorm), 0.56f * convex + 0.26f) *
                                powf(fmaxf(0.0f, 1.0f - zNorm * zNorm), 0.48f);

            /* Patrón de escudos (5 vertebrales, 4 pares costales, 11 marginales) con surcos nítidos y bisel duro. */
            float suture = 0.0f;
            float localU = 0.0f, localV = 0.0f;
            if (ax < 0.30f) {
                float uz = u * (float)vertebrals;
                float seamZ = fabsf(sinf(uz * 3.14159265f));
                float seamX = Math_Clamp01((0.30f - ax) / 0.025f);
                suture = fmaxf(1.0f - ShellSmoothstep(0.0f, 0.09f, seamZ), 1.0f - seamX);
                localU = uz - floorf(uz) - 0.5f;
                localV = xNorm / 0.30f * 0.5f;
            } else if (ax < 0.80f) {
                float uz = u * (float)costals + 0.14f;
                float seamZ = fabsf(sinf(uz * 3.14159265f));
                float seamX = fminf(Math_Clamp01((ax - 0.30f) / 0.025f), Math_Clamp01((0.80f - ax) / 0.025f));
                suture = fmaxf(1.0f - ShellSmoothstep(0.0f, 0.09f, seamZ), 1.0f - seamX);
                localU = uz - floorf(uz) - 0.5f;
                localV = (ax - 0.55f) / 0.25f * 0.5f;
            } else {
                float uz = u * 11.0f;
                float seamZ = fabsf(sinf(uz * 3.14159265f));
                suture = (1.0f - ShellSmoothstep(0.0f, 0.08f, seamZ)) * 0.78f;
                localU = uz - floorf(uz) - 0.5f;
                localV = (ax - 0.90f) / 0.10f * 0.5f;
            }
            /* Estrías radiales queratinosas de alto contraste + anillos de crecimiento rígidos por escudo. */
            float radAng = atan2f(localV, localU);
            float radDist = sqrtf(localU * localU + localV * localV);
            float rays = 0.5f + 0.5f * sinf(radAng * 13.0f + SuppNoise2D(u * 16.0f, v * 16.0f, 911u) * 2.8f);
            float rings = 0.5f + 0.5f * sinf(radDist * 28.0f);
            float mottle = SuppNoise2D(u * 20.0f, v * 15.0f, 947u);

            float edgeFade = ShellSmoothstep(0.96f, 0.76f, ax) * ShellSmoothstep(0.96f, 0.78f, fabsf(zNorm));
            /* Placas córneas ligeramente convexas separadas por surcos incisivos (sensación de coraza rígida). */
            float scuteCrown = (1.0f - Math_Clamp01(radDist * 1.45f)) * 0.45f;
            float scuteBump = relief * domeH * 0.032f * edgeFade * (scuteCrown - suture * 1.15f + 0.08f * rings);
            float yTop = center.y + domeH * domeProfile + scuteBump;
            float yBot = center.y - plastD * 0.24f * (1.0f - ax * 0.88f);

            /* Normales biseladas por escudo para que la luz especular marque las placas duras de queratina. */
            float bevelX = localV * 0.38f * edgeFade + (rays - 0.5f) * 0.12f * edgeFade;
            float bevelZ = -localU * 0.38f * edgeFade + (rings - 0.5f) * 0.10f * edgeFade;
            Vector3 nrm = Vec3_Normalize(Vec3_Create(xNorm * 0.65f + bevelX, 1.0f, zNorm * 0.45f + bevelZ));

            Color scuteTint = Color_Lerp(cCara, cAmber, 0.48f * rays * (0.45f + 0.55f * mottle));
            scuteTint = Color_Lerp(scuteTint, cOliveDark, 0.42f * (1.0f - rays));
            scuteTint = Color_Lerp(scuteTint, cBord, 0.35f * mottle * (1.0f - rays));
            Color col = Color_Lerp(scuteTint, cBord, Math_Clamp01(suture * 0.88f + (ax > 0.82f ? 0.16f : 0.0f)));
            if (!EmitVertex(opaque, Vec3_Create(xPos, yTop, zPos), nrm, col,
                            SURFACE_REGION_DORSAL_TRUNK, 0.0f, rigidSkin, &caraTop[iz][ix]) ||
                !EmitVertex(opaque, Vec3_Create(xPos, yBot, zPos), Vec3_Scale(nrm, -1.0f), cPlas,
                            SURFACE_REGION_VENTRAL_TRUNK, 1.0f, rigidSkin, &caraRim[iz][ix])) {
                return false;
            }
        }
    }
    for (unsigned iz = 0; iz < nZ; ++iz) {
        for (unsigned ix = 0; ix < nX; ++ix) {
            if (!EmitQuad(opaque, caraTop[iz][ix], caraTop[iz + 1][ix], caraTop[iz + 1][ix + 1], caraTop[iz][ix + 1]) ||
                !EmitQuad(opaque, caraRim[iz][ix], caraRim[iz][ix + 1], caraRim[iz + 1][ix + 1], caraRim[iz + 1][ix])) {
                return false;
            }
        }
    }
    /* 2. Plastrón ventral contorneado + puente lateral inframarginal con suturas y textura ósea/queratinosa. */
    float pHalfL = sh->plastronLength * 0.50f * s * dev;
    float pHalfW = sh->plastronWidth  * 0.50f * s * dev;
    const unsigned pnZ = 24, pnX = 20;
    MeshIndex plasG[25][21];
    for (unsigned iz = 0; iz <= pnZ; ++iz) {
        float u = (float)iz / (float)pnZ;
        float zn = 1.0f - 2.0f * u;
        float oval = sqrtf(fmaxf(0.04f, 1.0f - zn * zn * 0.96f));
        float bridge = (u > 0.24f && u < 0.74f) ? sinf((u - 0.24f) / 0.50f * 3.14159265f) : 0.0f;
        float rw = Math_Lerp(pHalfW * oval, halfW * 0.86f, bridge * 0.62f);
        float zPos = center.z + zn * pHalfL;
        float endLift = powf(fabsf(zn), 2.8f) * plastD * 0.22f;
        for (unsigned ix = 0; ix <= pnX; ++ix) {
            float v = (float)ix / (float)pnX;
            float xn = 2.0f * v - 1.0f;
            float ax = fabsf(xn);
            float sideLift = bridge * powf(ax, 1.8f) * plastD * 0.62f + (1.0f - bridge) * powf(ax, 2.2f) * plastD * 0.34f;
            float yPos = center.y - plastD * 0.72f * (1.0f - 0.12f * ax * ax) + sideLift + endLift;

            float midSeam = 1.0f - ShellSmoothstep(0.0f, 0.055f, ax);
            float transWave = fabsf(sinf(u * 6.0f * 3.14159265f));
            float transSeam = (1.0f - ShellSmoothstep(0.0f, 0.11f, transWave)) * ShellSmoothstep(0.95f, 0.78f, ax);
            float bridgeSeam = bridge * (1.0f - ShellSmoothstep(0.0f, 0.055f, fabsf(ax - 0.72f)));
            float pSuture = fmaxf(midSeam * 0.78f, fmaxf(transSeam * 0.85f, bridgeSeam * 0.68f));
            float pGrain = SuppNoise2D(u * 18.0f, v * 14.0f, 1013u);
            yPos += pSuture * plastD * 0.015f;

            Vector3 nrm = Vec3_Normalize(Vec3_Create(xn * (0.28f + 0.35f * bridge) + (pGrain - 0.5f) * 0.06f,
                                                     -1.0f,
                                                     zn * 0.25f));
            Color pcBase = ShadeColor(cPlas, 0.92f + 0.14f * pGrain);
            Color pc = Color_Lerp(pcBase, cPlasSeam, Math_Clamp01(pSuture));
            if (!EmitVertex(opaque, Vec3_Create(center.x + xn * rw, yPos, zPos), nrm, pc,
                            SURFACE_REGION_VENTRAL_TRUNK, 1.0f, rigidSkin, &plasG[iz][ix])) {
                return false;
            }
        }
    }
    for (unsigned iz = 0; iz < pnZ; ++iz) {
        for (unsigned ix = 0; ix < pnX; ++ix) {
            if (!EmitQuad(opaque, plasG[iz][ix], plasG[iz][ix + 1], plasG[iz + 1][ix + 1], plasG[iz + 1][ix])) {
                return false;
            }
        }
    }
    return true;
}

/* ============================================================================
 * 11. DETALLES CEFÁLICOS ACUÁTICOS PROYECTADOS AL SDF + CORONETA + ANILLOS ÓSEOS
 * ============================================================================ */

static bool EmitProjectedQuadPatch(BuildTarget* opaque, const MonsterSDF* sdf,
                                   Vector3 anchor, Vector3 dir, Vector3 tanU, Vector3 tanV,
                                   float halfU, float halfV, float maxDist, float offset,
                                   Color cCenter, Color cEdge, SurfaceRegion region,
                                   AnatomySkinBinding sk) {
    /* Emite una placa orgánica proyectada punto por punto sobre el SDF resuelto. */
    const unsigned nu = 4, nv = 4;
    MeshIndex grid[5][5];
    for (unsigned iu = 0; iu <= nu; ++iu) {
        float fu = 2.0f * ((float)iu / (float)nu) - 1.0f;
        for (unsigned iv = 0; iv <= nv; ++iv) {
            float fv = 2.0f * ((float)iv / (float)nv) - 1.0f;
            /* Recorte elíptico suave de esquinas para placas cefálicas orgánicas. */
            float r = sqrtf(fu * fu + fv * fv);
            float scale = r > 1.0f ? (1.0f / r) : 1.0f;
            float eu = fu * Math_Lerp(1.0f, scale, 0.42f);
            float ev = fv * Math_Lerp(1.0f, scale, 0.42f);
            Vector3 pA = Vec3_Add(anchor, Vec3_Add(Vec3_Scale(tanU, eu * halfU), Vec3_Scale(tanV, ev * halfV)));
            float edge = fmaxf(fabsf(fu), fabsf(fv));
            float bump = offset * (1.0f - 0.55f * edge * edge);
            SurfaceProjection sp = ProjectOntoSDFSurface(sdf, pA, dir, maxDist, bump);
            if (!sp.hit) {
                sp.point = Vec3_Add(pA, Vec3_Scale(dir, maxDist * 0.5f + bump));
                sp.normal = dir;
            }
            Color col = Color_Lerp(cCenter, cEdge, edge * edge);
            if (!EmitVertex(opaque, sp.point, sp.normal, col, region, 0.08f, sk, &grid[iu][iv]))
                return false;
        }
    }
    for (unsigned iu = 0; iu < nu; ++iu) {
        for (unsigned iv = 0; iv < nv; ++iv) {
            if (!EmitQuad(opaque, grid[iu][iv], grid[iu + 1][iv], grid[iu + 1][iv + 1], grid[iu][iv + 1]))
                return false;
        }
    }
    return true;
}

static bool BuildAquaticHeadAndArmorDetails(const Monster* m, const MonsterSDF* sdf, BuildTarget* opaque) {
    const CreaturePhenotype* p = &m->phenotype;
    const AnatomyGraph* g = &m->anatomyGraph;
    float s = p->axial.totalScale;

    const AnatomyNode* headNode = AnatomyGraph_FindFirstRegion(g, ANATOMY_REGION_HEAD);
    if (headNode && m->hasHead) {
        int jHead = FindJointSafe(g, opaque->skeleton, headNode->id);
        AnatomySkinBinding skHead = MakeSkin1(jHead);
        const HeadSurfaceRecipe* hr = &m->head.anatomy.surface;
        float maxProbe = fmaxf(hr->craniumRadii.x, fmaxf(hr->craniumRadii.y, hr->craniumRadii.z)) * 2.2f;

        if (p->head.archetype == HEAD_ARCHETYPE_ELASMOBRANCH) {
            /* 5 hendiduras branquiales subdivididas y proyectadas punto por punto sobre el SDF del cuello. */
            const AnatomyNode* neck = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_NECK);
            const AnatomyNode* pec  = AnatomyGraph_FindModuleNode(g, 1, AXIAL_NODE_PECTORAL);
            if (neck && pec) {
                int jNeck = FindJointSafe(g, opaque->skeleton, neck->id);
                AnatomySkinBinding skNeck = MakeSkin1(jNeck);
                Color slitCol = Color_FromRGB(24, 26, 30);
                for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
                    float sgn = sideIdx == 0 ? 1.0f : -1.0f;
                    for (int gIdx = 0; gIdx < 5; ++gIdx) {
                        float u = (float)gIdx / 4.0f;
                        Vector3 baseC = Vec3_Lerp(neck->center, pec->center, 0.15f + 0.68f * u);
                        float rx = Math_Lerp(neck->widthRadius, pec->widthRadius, 0.15f + 0.68f * u);
                        float ry = Math_Lerp(neck->heightRadius, pec->heightRadius, 0.15f + 0.68f * u);
                        float slitH = ry * 0.44f;
                        float slitW = fmaxf(0.006f * s, rx * 0.055f);
                        float off = fmaxf(0.005f * s, rx * 0.016f);
                        const unsigned nSub = 5;
                        MeshIndex prevF = 0, prevB = 0;
                        for (unsigned k = 0; k <= nSub; ++k) {
                            float f = 2.0f * ((float)k / (float)nSub) - 1.0f;
                            Vector3 anchor = Vec3_Create(baseC.x, baseC.y - ry * 0.06f + f * slitH, baseC.z);
                            Vector3 dir = Vec3_Normalize(Vec3_Create(sgn, f * 0.20f, 0.0f));
                            SurfaceProjection sp = ProjectOntoSDFSurface(sdf, anchor, dir, rx * 2.4f, off);
                            Vector3 pt = sp.hit ? sp.point : Vec3_Create(sgn * rx, anchor.y, anchor.z);
                            Vector3 nrm = sp.hit ? sp.normal : dir;
                            MeshIndex iF, iB;
                            if (!EmitVertex(opaque, Vec3_Create(pt.x, pt.y, pt.z + slitW), nrm, slitCol, SURFACE_REGION_NECK, 0.2f, skNeck, &iF) ||
                                !EmitVertex(opaque, Vec3_Create(pt.x, pt.y, pt.z - slitW), nrm, slitCol, SURFACE_REGION_NECK, 0.2f, skNeck, &iB))
                                return false;
                            if (k > 0) {
                                if (sideIdx == 0) {
                                    if (!EmitQuad(opaque, prevF, prevB, iB, iF)) return false;
                                } else {
                                    if (!EmitQuad(opaque, prevF, iF, iB, prevB)) return false;
                                }
                            }
                            prevF = iF; prevB = iB;
                        }
                    }
                }
            }
        } else if (p->head.archetype == HEAD_ARCHETYPE_TELEOST) {
            /* Margen opercular curvo sutil proyectado directamente sobre el SDF craneal detrás del ojo (T02). */
            Color opDark = Color_Lerp(p->surface.pigment.baseColor, Color_FromRGB(12, 18, 28), 0.78f);
            Color opEdge = Color_Lerp(p->surface.pigment.baseColor, p->surface.pigment.secondaryColor, 0.35f);
            Vector3 opBase = Vec3_Add(headNode->center, Vec3_Lerp(hr->cranialCenters[3], hr->cranialCenters[4], 0.55f));
            float ry = hr->cranialRadii[4].y * 0.72f;
            float off = fmaxf(0.004f * s, hr->cranialRadii[4].x * 0.012f);
            float stripW = fmaxf(0.008f * s, hr->cranialRadii[4].y * 0.030f);
            for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
                float sgn = sideIdx == 0 ? 1.0f : -1.0f;
                MeshIndex prevA = 0, prevB = 0, prevC = 0;
                const unsigned nSeg = 10;
                for (unsigned k = 0; k <= nSeg; ++k) {
                    float f = (float)k / (float)nSeg;
                    float aAng = -1.18f + 2.36f * f;
                    float y = opBase.y + sinf(aAng) * ry;
                    float z = opBase.z - cosf(aAng) * ry * 0.34f;
                    Vector3 anchor = Vec3_Create(opBase.x, y, z);
                    Vector3 dir = Vec3_Normalize(Vec3_Create(sgn, sinf(aAng) * 0.22f, -0.10f));
                    SurfaceProjection sp = ProjectOntoSDFSurface(sdf, anchor, dir, maxProbe, off);
                    if (!sp.hit) continue;
                    Vector3 pFront = Vec3_Add(sp.point, Vec3_Create(0.0f, 0.0f,  stripW * 0.75f));
                    Vector3 pMid   = Vec3_Add(sp.point, Vec3_Scale(sp.normal, off * 0.35f));
                    Vector3 pRear  = Vec3_Add(sp.point, Vec3_Create(0.0f, 0.0f, -stripW * 0.75f));
                    MeshIndex iA, iB, iC;
                    if (!EmitVertex(opaque, pFront, sp.normal, opEdge, SURFACE_REGION_HEAD, 0.25f, skHead, &iA) ||
                        !EmitVertex(opaque, pMid,   sp.normal, opDark, SURFACE_REGION_HEAD, 0.20f, skHead, &iB) ||
                        !EmitVertex(opaque, pRear,  sp.normal, opEdge, SURFACE_REGION_HEAD, 0.25f, skHead, &iC))
                        return false;
                    if (k > 0) {
                        if (sideIdx == 0) {
                            if (!EmitQuad(opaque, prevA, prevB, iB, iA) ||
                                !EmitQuad(opaque, prevB, prevC, iC, iB)) return false;
                        } else {
                            if (!EmitQuad(opaque, prevA, iA, iB, prevB) ||
                                !EmitQuad(opaque, prevB, iB, iC, prevC)) return false;
                        }
                    }
                    prevA = iA; prevB = iB; prevC = iC;
                }
            }
        } else if (p->head.archetype == HEAD_ARCHETYPE_CETACEAN) {
            /* Espiráculo dorsal único en media luna detrás del melón, proyectado al SDF real (D02). */
            Color bhRim  = Color_Lerp(p->surface.pigment.baseColor, Color_FromRGB(74, 84, 94), 0.35f);
            Color bhSlit = Color_FromRGB(18, 22, 26);
            Vector3 bhAnchor = Vec3_Add(headNode->center, Vec3_Lerp(hr->cranialCenters[3], hr->cranialCenters[4], 0.45f));
            float bw = fmaxf(0.022f * s, hr->cranialRadii[4].x * 0.15f);
            float bl = bw * 0.44f;
            float off = fmaxf(0.004f * s, hr->cranialRadii[4].y * 0.012f);
            const unsigned nArc = 12;
            MeshIndex innerG[13], outerG[13];
            SurfaceProjection spCenter = ProjectOntoSDFSurface(sdf, bhAnchor, Vec3_Create(0, 1, 0), maxProbe, off * 0.8f);
            Vector3 cPt = spCenter.hit ? spCenter.point : Vec3_Add(bhAnchor, Vec3_Create(0, hr->cranialRadii[4].y, 0));
            Vector3 cNrm = spCenter.hit ? spCenter.normal : Vec3_Create(0, 1, 0);
            MeshIndex centerIdx = 0;
            if (!EmitVertex(opaque, cPt, cNrm, bhSlit, SURFACE_REGION_HEAD, 0.0f, skHead, &centerIdx)) return false;
            for (unsigned k = 0; k <= nArc; ++k) {
                float ang = (float)k / (float)nArc * 6.2831853f;
                float cx = cosf(ang);
                float sz = sinf(ang);
                float crescentZ = sz * bl - (1.0f - cx * cx) * bl * 0.48f;
                Vector3 qIn  = Vec3_Add(bhAnchor, Vec3_Create(cx * bw * 0.72f, 0.0f, crescentZ * 0.72f));
                Vector3 qOut = Vec3_Add(bhAnchor, Vec3_Create(cx * bw * 1.25f, 0.0f, crescentZ * 1.25f));
                SurfaceProjection spIn  = ProjectOntoSDFSurface(sdf, qIn,  Vec3_Create(0, 1, 0), maxProbe, off * 1.10f);
                SurfaceProjection spOut = ProjectOntoSDFSurface(sdf, qOut, Vec3_Create(0, 1, 0), maxProbe, off * 0.50f);
                Vector3 pIn  = spIn.hit  ? spIn.point  : Vec3_Add(cPt, Vec3_Create(cx * bw * 0.72f, 0.0f, crescentZ * 0.72f));
                Vector3 pOut = spOut.hit ? spOut.point : Vec3_Add(cPt, Vec3_Create(cx * bw * 1.25f, 0.0f, crescentZ * 1.25f));
                if (!EmitVertex(opaque, pIn,  spIn.hit  ? spIn.normal  : cNrm, bhSlit, SURFACE_REGION_HEAD, 0.0f, skHead, &innerG[k]) ||
                    !EmitVertex(opaque, pOut, spOut.hit ? spOut.normal : cNrm, bhRim,  SURFACE_REGION_HEAD, 0.0f, skHead, &outerG[k]))
                    return false;
            }
            for (unsigned k = 0; k < nArc; ++k) {
                if (!EmitTri(opaque, centerIdx, innerG[k + 1], innerG[k]) ||
                    !EmitQuad(opaque, innerG[k], innerG[k + 1], outerG[k + 1], outerG[k]))
                    return false;
            }
        } else if (p->head.archetype == HEAD_ARCHETYPE_TESTUDINE) {
            /* Placas cefálicas grandes y planas de Chelonia mydas ajustadas al cráneo (C02):
             * 1 PAR de escamas prefrontales diagnósticas entre los ojos, placa frontal,
             * placas supraoculares y parietales/postorbitales con suturas oscuras estrechas. */
            Color plateCenter = Color_Lerp(p->dorsalColor, Color_FromRGB(84, 88, 60), 0.45f);
            Color plateEdge   = Color_FromRGB(30, 30, 20);
            float off = fmaxf(0.009f * s, hr->craniumRadii.y * 0.032f);
            Vector3 cMid = Vec3_Add(headNode->center, Vec3_Lerp(hr->cranialCenters[1], hr->cranialCenters[2], 0.55f));
            Vector3 cRear = Vec3_Add(headNode->center, Vec3_Lerp(hr->cranialCenters[3], hr->cranialCenters[4], 0.42f));
            float rx = hr->craniumRadii.x;
            float rz = hr->craniumRadii.z;

            /* 1) Par único de escamas prefrontales alargadas entre los ojos (rasgo diagnóstico de C. mydas). */
            for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
                float sgn = sideIdx == 0 ? 1.0f : -1.0f;
                Vector3 pfAnchor = Vec3_Add(cMid, Vec3_Create(sgn * rx * 0.23f, 0.0f, rz * 0.16f));
                Vector3 pfDir = Vec3_Normalize(Vec3_Create(sgn * 0.22f, 0.96f, 0.14f));
                if (!EmitProjectedQuadPatch(opaque, sdf, pfAnchor, pfDir,
                                            Vec3_Create(1, 0, 0), Vec3_Create(0, 0, 1),
                                            rx * 0.24f, rz * 0.34f, maxProbe, off,
                                            plateCenter, plateEdge, SURFACE_REGION_HEAD, skHead))
                    return false;
            }
            /* 2) Placa frontal central detrás de las prefrontales. */
            if (!EmitProjectedQuadPatch(opaque, sdf, Vec3_Add(cRear, Vec3_Create(0.0f, 0.0f, rz * 0.20f)),
                                        Vec3_Create(0, 1, 0), Vec3_Create(1, 0, 0), Vec3_Create(0, 0, 1),
                                        rx * 0.34f, rz * 0.28f, maxProbe, off,
                                        plateCenter, plateEdge, SURFACE_REGION_HEAD, skHead))
                return false;
            /* 3) Placas supraoculares, parietales y postorbitales bilaterales ajustadas. */
            for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
                float sgn = sideIdx == 0 ? 1.0f : -1.0f;
                Vector3 supAnchor = Vec3_Add(cMid, Vec3_Create(sgn * rx * 0.56f, 0.0f, -rz * 0.04f));
                Vector3 supDir = Vec3_Normalize(Vec3_Create(sgn * 0.54f, 0.84f, 0.06f));
                if (!EmitProjectedQuadPatch(opaque, sdf, supAnchor, supDir,
                                            Vec3_Create(1, 0, 0), Vec3_Create(0, 0, 1),
                                            rx * 0.22f, rz * 0.28f, maxProbe, off,
                                            plateCenter, plateEdge, SURFACE_REGION_HEAD, skHead))
                    return false;
                Vector3 parAnchor = Vec3_Add(cRear, Vec3_Create(sgn * rx * 0.44f, 0.0f, -rz * 0.20f));
                Vector3 parDir = Vec3_Normalize(Vec3_Create(sgn * 0.40f, 0.91f, -0.06f));
                if (!EmitProjectedQuadPatch(opaque, sdf, parAnchor, parDir,
                                            Vec3_Create(1, 0, 0), Vec3_Create(0, 0, 1),
                                            rx * 0.32f, rz * 0.30f, maxProbe, off,
                                            plateCenter, plateEdge, SURFACE_REGION_HEAD, skHead))
                    return false;
                Vector3 poAnchor = Vec3_Add(cRear, Vec3_Create(sgn * rx * 0.68f, -hr->craniumRadii.y * 0.04f, -rz * 0.04f));
                Vector3 poDir = Vec3_Normalize(Vec3_Create(sgn * 0.94f, 0.20f, 0.0f));
                if (!EmitProjectedQuadPatch(opaque, sdf, poAnchor, poDir,
                                            Vec3_Create(0, 1, 0), Vec3_Create(0, 0, 1),
                                            hr->craniumRadii.y * 0.34f, rz * 0.28f, maxProbe, off,
                                            plateCenter, plateEdge, SURFACE_REGION_HEAD, skHead))
                    return false;
            }
        } else if (p->head.archetype == HEAD_ARCHETYPE_SYNGNATHID) {
            /* Coroneta ósea orgánica y continua sobre la bóveda craneal posterior (S04). */
            Color crBaseCol = Color_Lerp(p->dorsalColor, p->surface.pigment.secondaryColor, 0.48f);
            Color crTopCol  = Color_Lerp(p->dorsalColor, p->surface.pigment.secondaryColor, 0.22f);
            Vector3 crAnchor = Vec3_Add(headNode->center, Vec3_Lerp(hr->cranialCenters[3], hr->cranialCenters[4], 0.55f));
            SurfaceProjection spCr = ProjectOntoSDFSurface(sdf, crAnchor, Vec3_Create(0, 1, -0.18f), maxProbe, 0.0f);
            Vector3 crBase = spCr.hit ? Vec3_Sub(spCr.point, Vec3_Create(0, hr->cranialRadii[4].y * 0.16f, 0))
                                      : Vec3_Add(crAnchor, Vec3_Create(0, hr->cranialRadii[4].y * 0.72f, 0));
            float crH = hr->cranialRadii[4].y * 0.34f;
            float crR = hr->cranialRadii[4].x * 0.44f;
            const unsigned segs = 16;
            MeshIndex ringB[17], ringM[17], ringT[17];
            MeshIndex capCenter = 0;
            Vector3 capP = Vec3_Add(crBase, Vec3_Create(0.0f, crH * 0.88f, -crH * 0.24f));
            if (!EmitVertex(opaque, capP, Vec3_Create(0, 1, -0.2f), crTopCol, SURFACE_REGION_HEAD, 0.0f, skHead, &capCenter))
                return false;
            for (unsigned k = 0; k <= segs; ++k) {
                float ang = (float)k / (float)segs * 6.2831853f;
                /* 5 lóbulos óseos bajos y suavemente redondeados (sin puntas rígidas). */
                float lobe = 0.94f + 0.07f * cosf(ang * 5.0f);
                float ca = cosf(ang), sa = sinf(ang);
                Vector3 nrm = Vec3_Normalize(Vec3_Create(ca, 0.45f, sa));
                Vector3 pB = Vec3_Create(crBase.x + ca * crR * 1.05f, crBase.y, crBase.z + sa * crR * 1.15f);
                Vector3 pM = Vec3_Create(crBase.x + ca * crR * 0.78f * lobe, crBase.y + crH * 0.56f,
                                         crBase.z + sa * crR * 0.84f * lobe - crH * 0.14f);
                Vector3 pT = Vec3_Create(crBase.x + ca * crR * 0.54f * lobe, crBase.y + crH * lobe,
                                         crBase.z + sa * crR * 0.58f * lobe - crH * 0.26f);
                if (!EmitVertex(opaque, pB, nrm, crBaseCol, SURFACE_REGION_HEAD, 0.0f, skHead, &ringB[k]) ||
                    !EmitVertex(opaque, pM, nrm, crBaseCol, SURFACE_REGION_HEAD, 0.0f, skHead, &ringM[k]) ||
                    !EmitVertex(opaque, pT, Vec3_Normalize(Vec3_Add(nrm, Vec3_Create(0, 0.7f, 0))), crTopCol,
                                SURFACE_REGION_HEAD, 0.0f, skHead, &ringT[k]))
                    return false;
            }
            for (unsigned k = 0; k < segs; ++k) {
                if (!EmitQuad(opaque, ringB[k], ringB[k + 1], ringM[k + 1], ringM[k]) ||
                    !EmitQuad(opaque, ringM[k], ringM[k + 1], ringT[k + 1], ringT[k]) ||
                    !EmitTri(opaque, ringT[k], ringT[k + 1], capCenter))
                    return false;
            }
        }
    }

    /* Armadura ósea segmentada parametrizada por longitud de arco (caballito de mar, S02):
     * 11 anillos troncales + ~35 anillos caudales con crestas longitudinales continuas
     * proyectados punto por punto (borde inferior, cresta y borde superior) sobre el SDF real. */
    if (p->axial.archetype == AXIAL_ARCHETYPE_CURVED_VERTICAL && p->axial.abdomenSegments > 0) {
        typedef struct SpineSample {
            Vector3 center;
            float rx, ry;
            int jIdx;
            bool isTail;
        } SpineSample;
        SpineSample spine[32];
        unsigned spineCount = 0;

        for (unsigned i = 1; i <= 6 && spineCount < 32; ++i) {
            const AnatomyNode* n = AnatomyGraph_FindModuleNode(g, 1, (uint16_t)i);
            if (!n) continue;
            spine[spineCount++] = (SpineSample){n->center, n->widthRadius, n->heightRadius,
                                                FindJointSafe(g, opaque->skeleton, n->id), false};
        }
        unsigned trunkEndIdx = spineCount > 0 ? spineCount - 1 : 0;
        for (size_t mi = 0; mi < m->recipe.bodyPlan.moduleCount; ++mi) {
            const CreatureModuleInstance* mod = &m->recipe.bodyPlan.modules[mi];
            if (mod->kind != CREATURE_MODULE_TAIL || mod->phenotypeIndex >= p->tailCount) continue;
            unsigned segN = p->tails[mod->phenotypeIndex].segmentCount;
            for (unsigned ti = 1; ti <= segN && spineCount < 32; ++ti) {
                const AnatomyNode* tn = AnatomyGraph_FindModuleNode(g, mod->instanceId, (uint16_t)ti);
                if (!tn) continue;
                spine[spineCount++] = (SpineSample){tn->center, tn->widthRadius, tn->heightRadius,
                                                    FindJointSafe(g, opaque->skeleton, tn->id), true};
            }
            break;
        }

        if (spineCount >= 3) {
            float cumLen[32];
            cumLen[0] = 0.0f;
            for (unsigned i = 1; i < spineCount; ++i) {
                cumLen[i] = cumLen[i - 1] + Vec3_Length(Vec3_Sub(spine[i].center, spine[i - 1].center));
            }
            float trunkArc = cumLen[trunkEndIdx];
            float totalArc = cumLen[spineCount - 1];
            float tailArc = fmaxf(0.0f, totalArc - trunkArc);
            unsigned trunkRings = p->axial.abdomenSegments;
            if (trunkRings > 16) trunkRings = 16;
            unsigned tailRings = (tailArc > 0.05f * s) ? 35u : 0u;
            unsigned totalRings = trunkRings + tailRings;
            if (totalRings > 54) totalRings = 54;

            Color plateCol = Color_Lerp(p->dorsalColor, p->surface.pigment.secondaryColor, 0.25f);
            Color crestCol = Color_Lerp(p->dorsalColor, p->ventralColor, 0.26f);
            Color grooveCol = Color_Lerp(p->surface.pigment.secondaryColor, Color_FromRGB(62, 34, 12), 0.42f);

            const unsigned nPts = 16;
            MeshIndex prevRing[17] = {0};
            bool hasPrev = false;

            for (unsigned rIdx = 0; rIdx < totalRings; ++rIdx) {
                bool isTailRing = (rIdx >= trunkRings);
                float targetArc;
                if (!isTailRing) {
                    float u = ((float)rIdx + 0.5f) / (float)trunkRings;
                    targetArc = trunkArc * (0.04f + 0.94f * u);
                } else {
                    float u = ((float)(rIdx - trunkRings) + 0.5f) / (float)tailRings;
                    targetArc = trunkArc + tailArc * (0.02f + 0.95f * u);
                }

                unsigned seg = 0;
                while (seg + 1 < spineCount - 1 && cumLen[seg + 1] < targetArc) ++seg;
                float segL = fmaxf(cumLen[seg + 1] - cumLen[seg], 1e-5f);
                float lt = Math_Clamp01((targetArc - cumLen[seg]) / segL);
                Vector3 c = Vec3_Lerp(spine[seg].center, spine[seg + 1].center, lt);
                float rx = Math_Lerp(spine[seg].rx, spine[seg + 1].rx, lt);
                float ry = Math_Lerp(spine[seg].ry, spine[seg + 1].ry, lt);
                Vector3 tangent = Vec3_Normalize(Vec3_Sub(spine[seg + 1].center, spine[seg].center));
                Vector3 sideA = Vec3_Create(1.0f, 0.0f, 0.0f);
                Vector3 sagA = Vec3_Normalize(Vec3_Cross(sideA, tangent));
                if (Vec3_LengthSq(sagA) < 1e-5f) sagA = Vec3_Create(0.0f, 0.0f, 1.0f);

                float ringSpacing = isTailRing ? (tailArc / (float)tailRings) : (trunkArc / (float)trunkRings);
                float halfBand = fminf(ringSpacing * 0.44f, fmaxf(rx, ry) * 0.48f);
                float probeMax = fmaxf(rx, ry) * 2.4f;
                float baseOff = isTailRing
                    ? fmaxf(0.005f * s, fminf(rx, ry) * 0.075f)
                    : fmaxf(0.011f * s, fminf(rx, ry) * 0.12f);
                float ridgeAmp = fmaxf(0.0045f * s, fminf(rx, ry) * (isTailRing ? 0.12f : 0.095f));
                float crestHarmonic = isTailRing ? 4.0f : 6.0f;
                AnatomySkinBinding sk = MakeSkin2(spine[seg].jIdx, 1.0f - lt, spine[seg + 1].jIdx, lt);
                SurfaceRegion reg = isTailRing ? SURFACE_REGION_TAIL : SURFACE_REGION_DORSAL_TRUNK;

                Vector3 cLo = Vec3_Sub(c, Vec3_Scale(tangent, halfBand));
                Vector3 cHi = Vec3_Add(c, Vec3_Scale(tangent, halfBand));
                MeshIndex lo[17], mid[17], hi[17];
                for (unsigned k = 0; k <= nPts; ++k) {
                    float ang = (float)k / (float)nPts * 6.2831853f;
                    float ca = cosf(ang), sa = sinf(ang);
                    Vector3 outDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(sideA, ca), Vec3_Scale(sagA, sa)));
                    float crest = 0.5f + 0.5f * cosf(ang * crestHarmonic + 0.785398f);
                    float crestPeak = powf(crest, 2.0f);
                    float outOffMid = baseOff + ridgeAmp * (0.45f + 0.85f * crestPeak);
                    float outOffEdge = baseOff * 0.85f + ridgeAmp * 0.30f * crestPeak;

                    SurfaceProjection spM = ProjectOntoSDFSurface(sdf, c,   outDir, probeMax, outOffMid);
                    SurfaceProjection spL = ProjectOntoSDFSurface(sdf, cLo, outDir, probeMax, outOffEdge);
                    SurfaceProjection spH = ProjectOntoSDFSurface(sdf, cHi, outDir, probeMax, outOffEdge);
                    Vector3 fallbackRad = Vec3_Add(Vec3_Scale(sideA, ca * rx), Vec3_Scale(sagA, sa * ry));
                    Vector3 pMid = spM.hit ? spM.point : Vec3_Add(c,   Vec3_Add(fallbackRad, Vec3_Scale(outDir, outOffMid)));
                    Vector3 pLo  = spL.hit ? spL.point : Vec3_Add(cLo, Vec3_Add(fallbackRad, Vec3_Scale(outDir, outOffEdge)));
                    Vector3 pHi  = spH.hit ? spH.point : Vec3_Add(cHi, Vec3_Add(fallbackRad, Vec3_Scale(outDir, outOffEdge)));
                    Vector3 nMid = spM.hit ? spM.normal : outDir;
                    Vector3 nLo  = spL.hit ? spL.normal : outDir;
                    Vector3 nHi  = spH.hit ? spH.normal : outDir;

                    Color cMid = Color_Lerp(plateCol, crestCol, crestPeak * 0.70f);
                    Color cEdge = Color_Lerp(grooveCol, plateCol, crestPeak * 0.45f);
                    if (!EmitVertex(opaque, pLo,  nLo,  cEdge, reg, 0.18f, sk, &lo[k]) ||
                        !EmitVertex(opaque, pMid, nMid, cMid,  reg, 0.10f, sk, &mid[k]) ||
                        !EmitVertex(opaque, pHi,  nHi,  cEdge, reg, 0.18f, sk, &hi[k])) {
                        return false;
                    }
                }
                for (unsigned k = 0; k < nPts; ++k) {
                    if (!EmitQuad(opaque, lo[k], lo[k + 1], mid[k + 1], mid[k]) ||
                        !EmitQuad(opaque, mid[k], mid[k + 1], hi[k + 1], hi[k]))
                        return false;
                    if (hasPrev && !EmitQuad(opaque, prevRing[k], prevRing[k + 1], lo[k + 1], lo[k]))
                        return false;
                }
                for (unsigned k = 0; k <= nPts; ++k) prevRing[k] = hi[k];
                hasPrev = true;
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
    MonsterSDF tempSdf = MonsterSDF_Create();
    const MonsterSDF* sdfPtr = NULL;
    if (NeedsAquaticSDFProjection(m) && MonsterSDF_Build(&tempSdf, m, MonsterSDF_DefaultConfig())) {
        sdfPtr = &tempSdf;
    }
    bool ok = BuildGenericFins(m, sdfPtr, &op) &&
              BuildCaudalPropulsors(m, &op) &&
              BuildMarineFlippers(m, &op) &&
              BuildAnatomicalShell(m, &op) &&
              BuildAquaticHeadAndArmorDetails(m, sdfPtr, &op);
    MonsterSDF_Free(&tempSdf);
    if (!ok) return false;
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
