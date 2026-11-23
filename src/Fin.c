/**
 * @file Fin.c
 * @brief Implementación del módulo anatómico genérico de aletas y series de pínulas.
 */

#include "Fin.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

static float FinPos(float x, float fallback) {
    return (isfinite(x) && x > 0.0f) ? x : fallback;
}

void FinPhenotype_Normalize(FinPhenotype* f) {
    if (!f) return;
    if (f->family < FIN_FAMILY_RAYED_PAIRED || f->family > FIN_FAMILY_CAUDAL_BILOBED)
        f->family = FIN_FAMILY_MEDIAN_DORSAL;
    if (f->role < FIN_ROLE_PECTORAL || f->role > FIN_ROLE_FINLET_SERIES)
        f->role = FIN_ROLE_DORSAL_FIRST;
    if (f->side < FIN_SIDE_CENTER || f->side > FIN_SIDE_RIGHT)
        f->side = FIN_SIDE_CENTER;

    f->baseChord = Math_Clamp(FinPos(f->baseChord, 0.35f), 0.03f, 4.0f);
    f->span = Math_Clamp(FinPos(f->span, 0.45f), 0.03f, 5.0f);
    f->thickness = Math_Clamp(FinPos(f->thickness, 0.03f), 0.004f, 0.6f);
    f->rootWidth = Math_Clamp(FinPos(f->rootWidth, f->thickness * 1.4f), 0.005f, 0.8f);
    f->sweep = isfinite(f->sweep) ? Math_Clamp(f->sweep, -0.5f, 1.5f) : 0.35f;
    f->leadingCurve = isfinite(f->leadingCurve) ? Math_Clamp(f->leadingCurve, -0.5f, 1.0f) : 0.25f;
    f->trailingCurve = isfinite(f->trailingCurve) ? Math_Clamp(f->trailingCurve, -0.5f, 1.0f) : 0.25f;
    f->tipShape = isfinite(f->tipShape) ? Math_Clamp01(f->tipShape) : 0.25f;
    f->camber = isfinite(f->camber) ? Math_Clamp(f->camber, -0.35f, 0.35f) : 0.0f;
    f->dihedral = isfinite(f->dihedral) ? Math_Clamp(f->dihedral, -1.25f, 1.25f) : 0.0f;
    f->cant = isfinite(f->cant) ? Math_Clamp(f->cant, -1.25f, 1.25f) : 0.0f;
    if (f->rayCount > 32) f->rayCount = 32;
    if (f->finletCount > 16) f->finletCount = 16;
    f->seriesLength = isfinite(f->seriesLength) && f->seriesLength > 0.0f
        ? Math_Clamp(f->seriesLength, 0.05f, 5.0f) : f->baseChord;
    f->development = isfinite(f->development) ? Math_Clamp01(f->development) : 1.0f;
}

FinPhenotype FinPhenotype_Interpolate(const FinPhenotype* a, const FinPhenotype* b, float t) {
    if (!a && !b) {
        FinPhenotype z = {0};
        FinPhenotype_Normalize(&z);
        return z;
    }
    if (!a) return *b;
    if (!b) return *a;
    t = Math_Clamp01(t);
    FinPhenotype out = t >= 0.5f ? *b : *a;
    out.baseChord = Math_Lerp(a->baseChord, b->baseChord, t);
    out.span = Math_Lerp(a->span, b->span, t);
    out.thickness = Math_Lerp(a->thickness, b->thickness, t);
    out.rootWidth = Math_Lerp(a->rootWidth, b->rootWidth, t);
    out.sweep = Math_Lerp(a->sweep, b->sweep, t);
    out.leadingCurve = Math_Lerp(a->leadingCurve, b->leadingCurve, t);
    out.trailingCurve = Math_Lerp(a->trailingCurve, b->trailingCurve, t);
    out.tipShape = Math_Lerp(a->tipShape, b->tipShape, t);
    out.camber = Math_Lerp(a->camber, b->camber, t);
    out.dihedral = Math_Lerp(a->dihedral, b->dihedral, t);
    out.cant = Math_Lerp(a->cant, b->cant, t);
    out.rayCount = (unsigned)lroundf(Math_Lerp((float)a->rayCount, (float)b->rayCount, t));
    out.finletCount = (unsigned)lroundf(Math_Lerp((float)a->finletCount, (float)b->finletCount, t));
    out.seriesLength = Math_Lerp(a->seriesLength, b->seriesLength, t);
    out.development = Math_Lerp(a->development, b->development, t);
    out.baseColor = Color_Lerp(a->baseColor, b->baseColor, t);
    out.tipColor = Color_Lerp(a->tipColor, b->tipColor, t);
    FinPhenotype_Normalize(&out);
    return out;
}

void Fin_ComputeFrame(const FinPhenotype* f,
                      const AttachmentSlot* slot,
                      Vector3* outOrigin,
                      Vector3* outSpanDir,
                      Vector3* outChordDir,
                      Vector3* outNormalDir) {
    Vector3 fwd = Vec3_LengthSq(slot->forward) > 1e-6f ? Vec3_Normalize(slot->forward) : Vec3_Create(0, 0, 1);
    Vector3 up  = Vec3_LengthSq(slot->up) > 1e-6f ? Vec3_Normalize(slot->up) : Vec3_Create(0, 1, 0);
    Vector3 side = Vec3_LengthSq(slot->side) > 1e-6f ? Vec3_Normalize(slot->side) : Vec3_Create(1, 0, 0);

    bool paired = (f->role == FIN_ROLE_PECTORAL || f->role == FIN_ROLE_PELVIC ||
                   f->side == FIN_SIDE_LEFT || f->side == FIN_SIDE_RIGHT);
    float sgn = (f->side == FIN_SIDE_RIGHT) ? -1.0f : 1.0f;

    Vector3 spanDir, chordDir, normalDir;
    if (paired) {
        /* Aleta pareada: sale lateralmente con diedro vertical y ligera inclinación. */
        float cd = cosf(f->dihedral), sd = sinf(f->dihedral);
        spanDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(side, sgn * cd), Vec3_Scale(up, sd)));
        chordDir = fwd;
        normalDir = Vec3_Normalize(Vec3_Cross(chordDir, spanDir));
        if (Vec3_LengthSq(normalDir) < 1e-6f) normalDir = up;
        if (fabsf(f->cant) > 1e-4f) {
            float cc = cosf(f->cant), sc = sinf(f->cant);
            Vector3 newChord = Vec3_Normalize(Vec3_Add(Vec3_Scale(chordDir, cc), Vec3_Scale(normalDir, sc * sgn)));
            normalDir = Vec3_Normalize(Vec3_Cross(newChord, spanDir));
            chordDir = newChord;
        }
    } else {
        /* Aleta mediana (dorsal, anal o serie de pínulas): sale a lo largo de slot->up. */
        spanDir = up;
        chordDir = fwd;
        normalDir = side;
        if (fabsf(f->cant) > 1e-4f) {
            float cc = cosf(f->cant), sc = sinf(f->cant);
            spanDir = Vec3_Normalize(Vec3_Add(Vec3_Scale(up, cc), Vec3_Scale(side, sc)));
            normalDir = Vec3_Normalize(Vec3_Cross(chordDir, spanDir));
        }
    }

    if (outOrigin) *outOrigin = slot->position;
    if (outSpanDir) *outSpanDir = spanDir;
    if (outChordDir) *outChordDir = chordDir;
    if (outNormalDir) *outNormalDir = normalDir;
}

bool Fin_Resolve(const FinPhenotype* source,
                 uint32_t module,
                 const AttachmentSlot* slot,
                 AnatomyGraph* g) {
    if (!source || !slot || !g || !Anatomy_MakeId(module, FIN_NODE_ROOT)) return false;
    FinPhenotype f = *source;
    FinPhenotype_Normalize(&f);

    float dev = fmaxf(0.01f, f.development);
    float s = slot->scale;
    Vector3 origin, spanDir, chordDir, normalDir;
    Fin_ComputeFrame(&f, slot, &origin, &spanDir, &chordDir, &normalDir);

    float span = f.span * s * dev;
    float chord = f.baseChord * s * dev;
    float thick = fmaxf(0.004f * s, f.thickness * s * dev);
    float rootW = fmaxf(thick, f.rootWidth * s * dev);

    AnatomySide aSide = (f.side == FIN_SIDE_LEFT) ? ANATOMY_SIDE_LEFT
                      : (f.side == FIN_SIDE_RIGHT) ? ANATOMY_SIDE_RIGHT
                                                   : ANATOMY_SIDE_CENTER;
    AnatomyRegion region = (f.role == FIN_ROLE_PECTORAL) ? ANATOMY_REGION_FORELIMB
                         : (f.role == FIN_ROLE_PELVIC)   ? ANATOMY_REGION_HINDLIMB
                         : (f.role == FIN_ROLE_CAUDAL)   ? ANATOMY_REGION_TAIL
                                                         : ANATOMY_REGION_ORNAMENT;

    if (f.role == FIN_ROLE_FINLET_SERIES) {
        /* Serie compacta de pínulas: 3 nodos de soporte a lo largo de la cresta posterior. */
        float seriesL = fmaxf(chord, f.seriesLength * s * dev);
        Vector3 pRoot = origin;
        Vector3 pMid  = Vec3_Add(origin, Vec3_Add(Vec3_Scale(chordDir, -seriesL * 0.50f),
                                                  Vec3_Scale(spanDir, span * 0.45f)));
        Vector3 pTip  = Vec3_Add(origin, Vec3_Add(Vec3_Scale(chordDir, -seriesL),
                                                  Vec3_Scale(spanDir, span * 0.35f)));
        if (!Module_Node(g, module, FIN_NODE_ROOT, pRoot, thick, chord * 0.25f, 1,
                         ANATOMY_ROLE_JOINT, region, aSide, f.development) ||
            !Module_Node(g, module, FIN_NODE_MID, pMid, thick * 0.8f, span * 0.5f, 1,
                         ANATOMY_ROLE_JOINT, region, aSide, f.development) ||
            !Module_Node(g, module, FIN_NODE_TIP, pTip, thick * 0.6f, span * 0.4f, 1,
                         ANATOMY_ROLE_JOINT, region, aSide, f.development)) {
            return false;
        }
        return Module_Edge(g, module, 1, slot->hostNode, Anatomy_MakeId(module, FIN_NODE_ROOT),
                           BODY_CONNECTION_SUPPORT, f.development) &&
               Module_Edge(g, module, 2, Anatomy_MakeId(module, FIN_NODE_ROOT),
                           Anatomy_MakeId(module, FIN_NODE_MID), BODY_CONNECTION_SUPPORT, f.development) &&
               Module_Edge(g, module, 3, Anatomy_MakeId(module, FIN_NODE_MID),
                           Anatomy_MakeId(module, FIN_NODE_TIP), BODY_CONNECTION_SUPPORT, f.development);
    }

    /* Aleta hidrodinámica individual: raíz embebida ligeramente en el anfitrión, estación media y punta. */
    Vector3 pRoot = Vec3_Sub(origin, Vec3_Scale(spanDir, rootW * 0.45f));
    Vector3 pMid  = Vec3_Add(origin,
                        Vec3_Add(Vec3_Scale(spanDir, span * 0.50f),
                                 Vec3_Scale(chordDir, -span * f.sweep * 0.38f)));
    Vector3 pTip  = Vec3_Add(origin,
                        Vec3_Add(Vec3_Scale(spanDir, span),
                                 Vec3_Scale(chordDir, -span * f.sweep)));

    if (!Module_Node(g, module, FIN_NODE_ROOT, pRoot, rootW * 0.55f, chord * 0.48f, 2,
                     ANATOMY_ROLE_JOINT, region, aSide, f.development) ||
        !Module_Node(g, module, FIN_NODE_MID, pMid, thick * 0.45f, chord * 0.34f, 1,
                     ANATOMY_ROLE_JOINT, region, aSide, f.development) ||
        !Module_Node(g, module, FIN_NODE_TIP, pTip, thick * 0.20f, chord * 0.14f, 1,
                     ANATOMY_ROLE_JOINT, region, aSide, f.development)) {
        return false;
    }

    /* Todas las aletas usan soporte esquelético limpio y delegan la lámina hidrodinámica
     * continua a SupplementaryMesh (sin artefactos voxel ni raíces esféricas). */
    if (!Module_Edge(g, module, 1, slot->hostNode, Anatomy_MakeId(module, FIN_NODE_ROOT),
                     BODY_CONNECTION_SUPPORT, f.development) ||
        !Module_Edge(g, module, 2, Anatomy_MakeId(module, FIN_NODE_ROOT),
                     Anatomy_MakeId(module, FIN_NODE_MID), BODY_CONNECTION_SUPPORT, f.development) ||
        !Module_Edge(g, module, 3, Anatomy_MakeId(module, FIN_NODE_MID),
                     Anatomy_MakeId(module, FIN_NODE_TIP), BODY_CONNECTION_SUPPORT, f.development)) {
        return false;
    }
    /* Almacenar el eje de cuerda en transverseAxis para referencia en SupplementaryMesh y rig. */
    g->connections[g->connectionCount - 2].transverseAxis = chordDir;
    g->connections[g->connectionCount - 1].transverseAxis = chordDir;
    return true;
}
