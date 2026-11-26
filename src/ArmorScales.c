/**
 * @file ArmorScales.c
 * @brief Generador geométrico de escamas queratínicas imbricadas reales sobre el SDF de reposo.
 */
#include "ArmorScales.h"
#include "MathUtils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ARMOR_VERTS_PER_SCALE 14
#define ARMOR_TRIS_PER_SCALE  24
#define ARMOR_MAX_SCALES      1600

typedef struct ScalePlacement {
    Vector3 pos;
    Vector3 normal;
    Vector3 forward;
    Vector3 right;
    float length;
    float width;
    float thickness;
    float overlapLift;
    float curvature;
    float tipTaper;
    uint32_t hash;
    AnatomySkinBinding skin;
} ScalePlacement;

static uint32_t ArmorHash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float ArmorHash01(uint32_t seed, uint32_t i) {
    return (float)(ArmorHash(seed ^ (i * 0x9e3779b9u)) & 0xffffffu) / 16777215.0f;
}

static Vector3 EstimateNormal(const MonsterSDF* sdf, Vector3 p, float eps) {
    float dx = MonsterSDF_EvaluateDistance(sdf, Vec3_Create(p.x + eps, p.y, p.z)) -
               MonsterSDF_EvaluateDistance(sdf, Vec3_Create(p.x - eps, p.y, p.z));
    float dy = MonsterSDF_EvaluateDistance(sdf, Vec3_Create(p.x, p.y + eps, p.z)) -
               MonsterSDF_EvaluateDistance(sdf, Vec3_Create(p.x, p.y - eps, p.z));
    float dz = MonsterSDF_EvaluateDistance(sdf, Vec3_Create(p.x, p.y, p.z + eps)) -
               MonsterSDF_EvaluateDistance(sdf, Vec3_Create(p.x, p.y, p.z - eps));
    Vector3 g = Vec3_Create(dx, dy, dz);
    float lenSq = Vec3_LengthSq(g);
    return lenSq > 1e-12f ? Vec3_Scale(g, 1.0f / sqrtf(lenSq)) : Vec3_Create(0, 1, 0);
}

static int FindJointForNode(const AnatomyGraph* graph, const Skeleton* skel, AnatomyId id) {
    if (!skel || !id) return -1;
    int j = Skeleton_FindJoint(skel, id);
    if (j >= 0) return j;
    if (!graph) return -1;
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const BodyConnection* c = &graph->connections[i];
        if (c->toId == id) {
            int pj = Skeleton_FindJoint(skel, c->fromId);
            if (pj >= 0) return pj;
        }
        if (c->fromId == id) {
            int pj = Skeleton_FindJoint(skel, c->toId);
            if (pj >= 0) return pj;
        }
    }
    return skel->jointCount > 0 ? 0 : -1;
}

static AnatomySkinBinding MakeSegmentSkin(const AnatomyGraph* graph, const Skeleton* skel,
                                          AnatomyId fromId, AnatomyId toId, float t) {
    AnatomySkinBinding b;
    memset(&b, 0, sizeof(b));
    if (!skel) return b;
    int ja = FindJointForNode(graph, skel, fromId);
    int jb = FindJointForNode(graph, skel, toId);
    if (ja < 0 && jb < 0) return b;
    if (ja < 0) { b.joints[0] = jb; b.weights[0] = 1.0f; return b; }
    if (jb < 0 || ja == jb) { b.joints[0] = ja; b.weights[0] = 1.0f; return b; }
    int pa = skel->joints[ja].parentIndex;
    int pb = skel->joints[jb].parentIndex;
    float w = Math_Clamp01(t);
    if (pa == jb) {
        int tmp = ja; ja = jb; jb = tmp;
        w = 1.0f - w;
    } else if (pb != ja) {
        w = t;
    }
    w = w * w * (3.0f - 2.0f * w);
    if (skel->joints[jb].constraint.type == JOINT_FIXED) w = 0.0f;
    b.joints[0] = ja; b.weights[0] = 1.0f - w;
    b.joints[1] = jb; b.weights[1] = w;
    return b;
}

static Vector3 Orthogonal(Vector3 v) {
    Vector3 a = fabsf(v.y) < 0.85f ? Vec3_Create(0, 1, 0) : Vec3_Create(1, 0, 0);
    Vector3 r = Vec3_Cross(a, v);
    float l2 = Vec3_LengthSq(r);
    return l2 > 1e-10f ? Vec3_Scale(r, 1.0f / sqrtf(l2)) : Vec3_Create(1, 0, 0);
}

static bool ProjectToSurface(const MonsterSDF* sdf, Vector3 guess, Vector3 radialDir,
                             float eps, Vector3* outPos, Vector3* outNormal, SDFMaterial* outMat) {
    Vector3 p = guess;
    for (int step = 0; step < 14; ++step) {
        float d = MonsterSDF_EvaluateDistance(sdf, p);
        if (fabsf(d) < eps * 0.30f) break;
        Vector3 n = step == 0 ? radialDir : EstimateNormal(sdf, p, eps);
        p = Vec3_Sub(p, Vec3_Scale(n, d * 0.92f));
    }
    SDFSample samp = MonsterSDF_Evaluate(sdf, p);
    if (fabsf(samp.distance) > eps * 6.0f) return false;
    *outPos = p;
    *outNormal = EstimateNormal(sdf, p, eps);
    *outMat = samp.material;
    return true;
}

static bool CollectPlacements(const Monster* monster, const MonsterSDF* sdf, const Skeleton* skel,
                              ScalePlacement** outList, size_t* outCount) {
    *outList = NULL; *outCount = 0;
    if (!monster || !sdf || !monster->hasSurface || !monster->hasAnatomyGraph) return false;
    const SurfacePhenotype* surf = &monster->surface;
    if (surf->integument.type != INTEGUMENT_PLATES || surf->integument.plates.armorScale <= 0.01f) {
        return true;
    }

    const PlatePhenotype* pl = &surf->integument.plates;
    const CreaturePhenotype* phen = &monster->phenotype;
    const AnatomyGraph* g = &monster->anatomyGraph;
    float bodyScale = fmaxf(0.05f, phen->axial.totalScale);
    float armorWeight = Math_Clamp01(pl->armorScale * surf->integument.coverage * phen->development.integument);
    if (armorWeight <= 0.02f) return true;

    float baseSize = Math_Clamp(pl->size, 0.04f, 0.80f) * bodyScale * (0.45f + 0.55f * armorWeight);
    float aspect = Math_Clamp(pl->aspect, 0.65f, 2.2f);
    float baseLen = baseSize * (0.65f + 0.45f * aspect);
    float baseWidth = baseSize / fmaxf(0.65f, sqrtf(aspect));
    float baseThick = fmaxf(0.004f * bodyScale, baseSize * Math_Clamp(pl->thickness * 2.5f + 0.14f, 0.10f, 0.38f));
    float eps = fmaxf(0.004f * bodyScale, baseSize * 0.04f);

    /* Buscar centro de cabeza para orientar flujo rostral->caudal y proteger hocico/ojos. */
    const AnatomyNode* headNode = AnatomyGraph_FindFirstRegion(g, ANATOMY_REGION_HEAD);
    Vector3 headPos = headNode ? headNode->center : Vec3_Create(0, 0, bodyScale);
    float headR = headNode ? fmaxf(headNode->widthRadius, headNode->heightRadius) : 0.3f * bodyScale;

    ScalePlacement* list = calloc(ARMOR_MAX_SCALES, sizeof(*list));
    if (!list) return false;
    size_t count = 0;
    uint32_t seed = surf->integument.scales.seed ? surf->integument.scales.seed : 1013u;

    for (size_t ci = 0; ci < g->connectionCount && count < ARMOR_MAX_SCALES; ++ci) {
        const BodyConnection* c = &g->connections[ci];
        if (c->development < 0.15f) continue;

        const AnatomyNode* na = AnatomyGraph_FindNode(g, c->fromId);
        const AnatomyNode* nb = AnatomyGraph_FindNode(g, c->toId);
        if (!na || !nb || na->development < 0.1f || nb->development < 0.1f) continue;

        bool isTail = (nb->region == ANATOMY_REGION_TAIL && c->kind == BODY_CONNECTION_AXIAL_LOFT);
        bool isTrunk = ((nb->region == ANATOMY_REGION_TRUNK || nb->region == ANATOMY_REGION_NECK ||
                         nb->region == ANATOMY_REGION_PELVIS || na->region == ANATOMY_REGION_PELVIS) &&
                        !isTail && c->kind == BODY_CONNECTION_AXIAL_LOFT);
        bool isLimb = ((nb->region == ANATOMY_REGION_FORELIMB || nb->region == ANATOMY_REGION_HINDLIMB) &&
                       c->kind == BODY_CONNECTION_LIMB_SEGMENT);
        if (!isTail && !isTrunk && !isLimb) continue;

        Vector3 segVec = Vec3_Sub(nb->center, na->center);
        float segLen = Vec3_Length(segVec);
        if (segLen < 1e-4f) continue;
        Vector3 dir = Vec3_Scale(segVec, 1.0f / segLen);

        /* Asegurar que la imbricación apunte siempre caudalmente (-Z en tronco/cola) o distalmente en patas. */
        Vector3 flowDir = dir;
        if ((isTrunk || isTail) && dir.z > 0.05f) flowDir = Vec3_Scale(dir, -1.0f);
        if (isLimb && dir.y > 0.05f) flowDir = Vec3_Scale(dir, -1.0f);

        float rStart = fmaxf(na->widthRadius, na->heightRadius);
        float rEnd = fmaxf(nb->widthRadius, nb->heightRadius);
        float rMean = 0.5f * (rStart + rEnd);
        if (rMean < 0.015f * bodyScale) continue;

        float localScaleFactor = isTail ? Math_Clamp(rMean / (0.18f * bodyScale), 0.36f, 1.00f)
                               : isLimb ? Math_Clamp(rMean / (0.20f * bodyScale), 0.42f, 0.70f)
                                        : Math_Clamp(rMean / (0.42f * bodyScale), 0.68f, 0.96f);
        float ringPitch = fmaxf(0.024f * bodyScale, baseLen * localScaleFactor * (1.0f - 0.56f * pl->overlap));
        unsigned rings = (unsigned)fmaxf(1.0f, floorf(segLen / ringPitch + 0.5f));
        if (rings > 16) rings = 16;

        Vector3 uAxis = Orthogonal(flowDir);
        /* Alinear uAxis al plano dorsal para consistencia angular alrededor del eje. */
        if (fabsf(flowDir.y) < 0.9f) {
            Vector3 upRef = Vec3_Create(0, 1, 0);
            Vector3 rx = Vec3_Cross(upRef, flowDir);
            if (Vec3_LengthSq(rx) > 1e-6f) uAxis = Vec3_Normalize(rx);
        }
        Vector3 vAxis = Vec3_Cross(flowDir, uAxis);

        float circ = 6.2831853f * rMean;
        float arcPitch = fmaxf(0.022f * bodyScale, baseWidth * localScaleFactor * 0.92f);
        unsigned slots = (unsigned)Math_Clamp(floorf(circ / arcPitch + 0.5f), 5.0f, 22.0f);

        for (unsigned ri = 0; ri < rings && count < ARMOR_MAX_SCALES; ++ri) {
            float stagger = (ri & 1u) ? 0.5f : 0.0f;

            for (unsigned si = 0; si < slots && count < ARMOR_MAX_SCALES; ++si) {
                uint32_t h = ArmorHash(seed ^ c->id ^ ArmorHash(ri * 32u + si));
                float along = (ArmorHash01(h, 4) - 0.5f) * 0.38f * pl->irregularity;
                float across = (ArmorHash01(h, 5) - 0.5f) * 0.44f * pl->irregularity;
                float tRaw = ((float)ri + 0.5f + along) / (float)rings;
                float tFlow = Vec3_Dot(flowDir, dir) > 0.0f ? tRaw : 1.0f - tRaw;
                Vector3 center = Vec3_Lerp(na->center, nb->center, tFlow);
                float localW = Math_Lerp(na->widthRadius, nb->widthRadius, tFlow);
                float localH = Math_Lerp(na->heightRadius, nb->heightRadius, tFlow);
                float theta = (((float)si + stagger + across) / (float)slots) * 6.2831853f;
                Vector3 radial = Vec3_Normalize(Vec3_Add(Vec3_Scale(uAxis, cosf(theta) * fmaxf(localW, 0.01f)),
                                                         Vec3_Scale(vAxis, sinf(theta) * fmaxf(localH, 0.01f))));
                float minDotY = isTail ? (-0.95f + 0.50f * (1.0f - pl->ventralCoverage))
                                       : (-0.78f + 0.56f * (1.0f - pl->ventralCoverage));
                if (!isLimb && radial.y < minDotY) continue;
                if (isLimb && fabsf(radial.x) < 0.25f) continue; /* Sólo cara externa de las extremidades. */

                Vector3 guess = Vec3_Add(center, Vec3_Scale(radial, fmaxf(localW, localH) * 1.40f));
                Vector3 surfPos, surfN;
                SDFMaterial mat;
                if (!ProjectToSurface(sdf, guess, radial, eps, &surfPos, &surfN, &mat)) continue;
                if (mat != SDF_MATERIAL_SKIN) continue;
                if (!isTail && surfN.y < minDotY) continue;

                /* Descartar pliegues internos donde una extremidad o flexión toca cerca de la superficie. */
                float clearProbe = baseThick * 2.0f + eps;
                if (MonsterSDF_EvaluateDistance(sdf, Vec3_Add(surfPos, Vec3_Scale(surfN, clearProbe))) < clearProbe * 0.48f) {
                    continue;
                }

                Vector3 tangF = Vec3_Sub(flowDir, Vec3_Scale(surfN, Vec3_Dot(flowDir, surfN)));
                if (Vec3_LengthSq(tangF) < 1e-6f) continue;
                tangF = Vec3_Normalize(tangF);
                Vector3 tangR = Vec3_Normalize(Vec3_Cross(surfN, tangF));

                float jitter = (ArmorHash01(h, 1) - 0.5f) * 2.0f * pl->irregularity;
                float ang = jitter * 0.23f;
                Vector3 jf = Vec3_Normalize(Vec3_Add(Vec3_Scale(tangF, cosf(ang)), Vec3_Scale(tangR, sinf(ang))));
                Vector3 jr = Vec3_Normalize(Vec3_Cross(surfN, jf));

                ScalePlacement sp;
                sp.pos = surfPos;
                sp.normal = surfN;
                sp.forward = jf;
                sp.right = jr;
                float sizeJitter = 1.0f + 0.21f * jitter;
                sp.length = baseLen * localScaleFactor * sizeJitter;
                sp.width = baseWidth * localScaleFactor * sizeJitter;
                sp.thickness = baseThick * (0.65f + 0.35f * localScaleFactor);
                sp.overlapLift = sp.length * (0.07f + 0.08f * pl->overlap);
                sp.curvature = pl->scaleCurvature;
                sp.tipTaper = pl->tipTaper;
                sp.hash = h;
                sp.skin = MakeSegmentSkin(g, skel, c->fromId, c->toId, tFlow);
                list[count++] = sp;
            }
        }
    }

    /* Placas cefálicas dorsales pequeñas en corona/occipucio (protegiendo hocico y órbitas). */
    if (headNode && sdf->mouthCount > 0 && count < ARMOR_MAX_SCALES) {
        const MonsterSDFMouth* hm = &sdf->mouths[0];
        float crownW = baseWidth * 0.42f;
        float crownL = baseLen * 0.44f;
        float crownT = baseThick * 0.58f;
        int headIdx = FindJointForNode(g, skel, headNode->id);
        AnatomySkinBinding headSkin = {0};
        if (headIdx >= 0) { headSkin.joints[0] = headIdx; headSkin.weights[0] = 1.0f; }

        unsigned cRings = 4;
        for (unsigned ri = 0; ri < cRings && count < ARMOR_MAX_SCALES; ++ri) {
            float zFrac = -0.28f + 0.37f * (float)ri;
            float zWorld = headPos.z - zFrac * headR * 0.72f;
            float taperScale = 0.72f + 0.34f * ((float)ri / (float)(cRings - 1));
            /* La cobertura empieza en la línea media y se abre hacia la nuca,
             * dejando libre el volumen lateral del cráneo y los pabellones. */
            int slots = ri < 2 ? 1 : 3;
            for (int si = -slots / 2; si <= slots / 2 && count < ARMOR_MAX_SCALES; ++si) {
                float ang = ((float)si + ((ri & 1u) ? 0.35f : 0.0f)) * 0.32f;
                Vector3 radial = Vec3_Normalize(Vec3_Create(sinf(ang), cosf(ang), 0.12f));
                Vector3 guess = Vec3_Create(headPos.x + radial.x * headR * 1.1f,
                                            headPos.y + radial.y * headR * 1.1f, zWorld);
                Vector3 surfPos, surfN;
                SDFMaterial mat;
                if (!ProjectToSurface(sdf, guess, radial, eps, &surfPos, &surfN, &mat)) continue;
                if (mat != SDF_MATERIAL_SKIN || surfN.y < 0.25f) continue;

                Vector3 local = Transform3D_ApplyRotationBasis(hm->inverseRotation, Vec3_Sub(surfPos, hm->center));
                if (local.z > hm->leftOrbitCenterLocal.z - hm->orbitRadii.z * 0.4f) continue;

                Vector3 flowDir = Vec3_Create(0, -0.10f, -1.0f);
                Vector3 tangF = Vec3_Sub(flowDir, Vec3_Scale(surfN, Vec3_Dot(flowDir, surfN)));
                if (Vec3_LengthSq(tangF) < 1e-6f) continue;
                tangF = Vec3_Normalize(tangF);
                Vector3 tangR = Vec3_Normalize(Vec3_Cross(surfN, tangF));

                ScalePlacement sp;
                sp.pos = surfPos;
                sp.normal = surfN;
                sp.forward = tangF;
                sp.right = tangR;
                sp.length = crownL * taperScale;
                sp.width = crownW * taperScale;
                sp.thickness = crownT * taperScale;
                sp.overlapLift = sp.length * (0.10f + 0.12f * pl->overlap);
                sp.curvature = pl->scaleCurvature;
                sp.tipTaper = pl->tipTaper;
                sp.hash = ArmorHash(seed ^ (uint32_t)(70000u + ri * 31u + (unsigned)(si + 8)));
                sp.skin = headSkin;
                list[count++] = sp;
            }
        }
    }

    *outList = list;
    *outCount = count;
    return true;
}

static void EmitScaleSolid(const ScalePlacement* sp, Color baseCol, Color tipCol, Mesh* out) {
    /* Malla cerrada de 14 vértices y 24 triángulos con quilla central y borde biselado. */
    float W = sp->width * 0.5f;
    float L = sp->length;
    float T = sp->thickness;
    float lift = sp->overlapLift;
    float shoulderW = W;
    float rootW = W * 0.82f;
    float distalW = W * (0.44f + 0.47f * (1.0f - sp->tipTaper));
    float keelH = T * (0.44f + 0.42f * sp->curvature);

    Vector3 O = Vec3_Sub(sp->pos, Vec3_Scale(sp->normal, T * 0.28f));
    Vector3 F = sp->forward;
    Vector3 R = sp->right;
    Vector3 N = sp->normal;

    #define PT(x, z, y) Vec3_Add(O, Vec3_Add(Vec3_Scale(R, (x)), Vec3_Add(Vec3_Scale(F, (z)), Vec3_Scale(N, (y)))))
    Vector3 p[ARMOR_VERTS_PER_SCALE];
    /* Capa superior (0..7) */
    p[0] = PT(-rootW, 0.0f,       T * 0.15f);
    p[1] = PT( 0.0f,  0.0f,       T * 0.35f);
    p[2] = PT(+rootW, 0.0f,       T * 0.15f);
    p[3] = PT(-shoulderW, L * 0.38f, lift * 0.36f + T * 0.12f);
    p[4] = PT( 0.0f,      L * 0.38f, lift * 0.36f + keelH);
    p[5] = PT(+shoulderW, L * 0.38f, lift * 0.36f + T * 0.12f);
    p[6] = PT(-distalW,   L * 0.84f, lift * 0.82f + T * 0.08f);
    p[7] = PT(+distalW,   L * 0.84f, lift * 0.82f + T * 0.08f);
    Vector3 tipPt = PT(0.0f, L, lift + T * 0.06f);
    /* Capa inferior (8..13) */
    p[8]  = PT(-rootW * 0.90f,     L * 0.02f, -T * 0.28f);
    p[9]  = PT(+rootW * 0.90f,     L * 0.02f, -T * 0.28f);
    p[10] = PT(-shoulderW * 0.88f, L * 0.38f,  lift * 0.34f - T * 0.32f);
    p[11] = PT(+shoulderW * 0.88f, L * 0.38f,  lift * 0.34f - T * 0.32f);
    p[12] = PT(0.0f,               L * 0.94f,  lift * 0.92f - T * 0.20f);
    p[13] = tipPt;
    #undef PT

    float hueJit = (ArmorHash01(sp->hash, 3) - 0.5f) * 0.14f;
    Color cRoot = Color_Lerp(baseCol, tipCol, Math_Clamp01(0.10f + hueJit));
    Color cMid  = Color_Lerp(baseCol, tipCol, Math_Clamp01(0.45f + hueJit));
    Color cTip  = Color_Lerp(baseCol, tipCol, Math_Clamp01(0.88f + hueJit));
    Color vcols[ARMOR_VERTS_PER_SCALE] = {
        cRoot, cRoot, cRoot, cMid, cMid, cMid, cTip, cTip,
        cRoot, cRoot, cMid, cMid, cTip, cTip
    };
    Vector3 vnorms[ARMOR_VERTS_PER_SCALE] = {
        Vec3_Normalize(Vec3_Sub(N, Vec3_Scale(R, 0.35f))),
        N,
        Vec3_Normalize(Vec3_Add(N, Vec3_Scale(R, 0.35f))),
        Vec3_Normalize(Vec3_Sub(N, Vec3_Scale(R, 0.42f))),
        Vec3_Normalize(Vec3_Add(N, Vec3_Scale(F, 0.15f))),
        Vec3_Normalize(Vec3_Add(N, Vec3_Scale(R, 0.42f))),
        Vec3_Normalize(Vec3_Sub(N, Vec3_Scale(R, 0.25f))),
        Vec3_Normalize(Vec3_Add(N, Vec3_Scale(R, 0.25f))),
        Vec3_Scale(N, -1.0f), Vec3_Scale(N, -1.0f),
        Vec3_Scale(N, -1.0f), Vec3_Scale(N, -1.0f),
        Vec3_Scale(N, -1.0f),
        Vec3_Normalize(Vec3_Add(N, Vec3_Scale(F, 0.35f)))
    };

    MeshIndex baseV = (MeshIndex)out->vertexCount;
    for (int i = 0; i < ARMOR_VERTS_PER_SCALE; ++i) {
        MeshVertex mv = {0};
        memset(&mv, 0, sizeof(mv));
        mv.position = p[i];
        mv.normal = vnorms[i];
        mv.color = vcols[i];
        mv.material = SDF_MATERIAL_HORN;
        mv.surface.region = SURFACE_REGION_ORNAMENT;
        mv.surface.secondaryRegion = SURFACE_REGION_ORNAMENT;
        mv.surface.position = p[i];
        mv.surface.normal = vnorms[i];
        mv.surface.flowDirection = F;
        mv.surface.integumentMask = 0.0f;
        out->vertices[out->vertexCount++] = mv;
    }

    static const MeshIndex tris[ARMOR_TRIS_PER_SCALE * 3] = {
        /* Caras dorsales (8 tris) */
        0, 3, 4,   0, 4, 1,   1, 4, 5,   1, 5, 2,
        3, 6, 4,   4, 6, 13,  4, 13, 7,  4, 7, 5,
        /* Caras ventrales (7 tris) */
        12, 10, 8,  12, 6, 10,  12, 13, 6,  12, 7, 13,
        12, 11, 7,  12, 9, 11,  12, 8, 9,
        /* Biseles laterales y raíz (9 tris) */
        0, 8, 3,   3, 8, 10,  3, 10, 6,
        2, 5, 9,   5, 11, 9,  5, 7, 11,
        0, 1, 8,   1, 9, 8,   1, 2, 9
    };
    for (size_t k = 0; k < ARMOR_TRIS_PER_SCALE * 3; ++k) {
        out->indices[out->indexCount++] = (MeshIndex)(baseV + tris[k]);
    }
}

bool ArmorScales_BuildMesh(const Monster* monster, const MonsterSDF* restSdf, Mesh* outMesh) {
    if (!monster || !outMesh) return false;
    Mesh_Clear(outMesh);
    ScalePlacement* list = NULL;
    size_t count = 0;
    if (!CollectPlacements(monster, restSdf, NULL, &list, &count)) return false;
    if (!count) { free(list); return true; }

    size_t vCount = count * ARMOR_VERTS_PER_SCALE;
    size_t iCount = count * ARMOR_TRIS_PER_SCALE * 3;
    if (!Mesh_ReserveVertices(outMesh, vCount) || !Mesh_ReserveIndices(outMesh, iCount)) {
        free(list);
        return false;
    }

    const PlatePhenotype* pl = &monster->surface.integument.plates;
    Color baseCol = pl->scaleColor.a > 0 ? pl->scaleColor : Color_FromRGB(108, 80, 56);
    Color tipCol = pl->scaleTipColor.a > 0 ? pl->scaleTipColor : Color_FromRGB(152, 124, 92);

    for (size_t i = 0; i < count; ++i) {
        EmitScaleSolid(&list[i], baseCol, tipCol, outMesh);
    }

    SurfacePhenotype flat = monster->surface;
    flat.integument.type = INTEGUMENT_SMOOTH_SKIN;
    flat.pigment.patternStrength = 0.0f;
    flat.pigment.layerCount = 0;
    flat.tuftCount = 0;
    flat.longHairCount = 0;
    outMesh->surfaceRecipe = SurfaceRecipe_CompileScaled(&flat, monster->surfaceMapping.unitScale);
    outMesh->hasSurface = true;
    Mesh_MarkGeometryChanged(outMesh);
    free(list);
    return true;
}

bool ArmorScales_BindDeformer(const Monster* monster, const MonsterSDF* restSdf,
                              const Skeleton* skeleton, const Mesh* mesh,
                              AnatomyDeformer* deformer) {
    if (!monster || !restSdf || !mesh || !deformer) return false;
    if (mesh->vertexCount == 0 || !skeleton) return true;
    ScalePlacement* list = NULL;
    size_t count = 0;
    if (!CollectPlacements(monster, restSdf, skeleton, &list, &count)) return false;
    if (count * ARMOR_VERTS_PER_SCALE != mesh->vertexCount) {
        free(list);
        return false;
    }
    AnatomySkinBinding* skin = calloc(mesh->vertexCount, sizeof(*skin));
    if (!skin) { free(list); return false; }
    for (size_t i = 0; i < count; ++i) {
        for (size_t k = 0; k < ARMOR_VERTS_PER_SCALE; ++k) {
            skin[i * ARMOR_VERTS_PER_SCALE + k] = list[i].skin;
        }
    }
    bool ok = AnatomyDeformer_BindExplicitSkin(deformer, mesh, &monster->anatomyGraph, skeleton, skin);
    free(skin);
    free(list);
    return ok;
}
