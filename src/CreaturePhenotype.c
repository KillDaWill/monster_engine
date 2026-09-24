/**
 * @file CreaturePhenotype.c
 * @brief Implementación de normalización e interpolación morfológica modular.
 * @author Monster Engine Team
 * @date 2026
 */

#include "CreaturePhenotype.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

static float Positive(float x, float fallback) { return isfinite(x) && x > 0 ? x : fallback; }
static float Unit(float x, float fallback) { return isfinite(x) ? Math_Clamp01(x) : fallback; }

void CreaturePhenotype_Normalize(CreaturePhenotype* p) {
    if (!p) return;
    if (p->limbCount > CREATURE_MAX_LIMBS) p->limbCount = CREATURE_MAX_LIMBS;
    if (p->tailCount > CREATURE_MAX_TAILS) p->tailCount = CREATURE_MAX_TAILS;
    if (p->ornamentCount > CREATURE_MAX_ORNAMENTS) p->ornamentCount = CREATURE_MAX_ORNAMENTS;

#define POS(name, value) p->axial.name = Positive(p->axial.name, value)
    POS(withersHeight, 4.0f); POS(totalScale, 1.0f); POS(neckLength, 1.0f); POS(neckWidth, 0.7f); POS(shoulderWidth, 1.5f);
    POS(thoraxWidth, 1.7f); POS(thoraxHeight, 0.8f); POS(abdomenWidth, 1.6f); POS(abdomenHeight, 0.7f);
    POS(pelvicWidth, 1.65f); POS(pelvicHeight, 0.72f); POS(trunkLength, 4.5f);
#undef POS

    for (size_t i = 0; i < AXIAL_PROFILE_STATIONS; ++i) {
        AxialStationMorph* m = &p->axial.profile[i];
        m->widthScale = Positive(m->widthScale, 1);
        m->heightScale = Positive(m->heightScale, 1);
        if (!isfinite(m->longitudinalOffset)) m->longitudinalOffset = 0;
        if (!isfinite(m->verticalOffset)) m->verticalOffset = 0;
    }
    p->axial.totalScale = Math_Clamp(p->axial.totalScale, 0.2f, 4.0f);
    p->axial.bodyFlattening = Math_Clamp(Positive(p->axial.bodyFlattening, 0.92f), 0.3f, 1.0f);
    if (!isfinite(p->axial.limbAttachmentLateral) || p->axial.limbAttachmentLateral <= 0.0f) {
        p->axial.limbAttachmentLateral = 0.86f;
    } else {
        p->axial.limbAttachmentLateral = Math_Clamp(p->axial.limbAttachmentLateral, 0.40f, 1.20f);
    }
    p->axial.cervicalCurvature = Math_Clamp(Positive(p->axial.cervicalCurvature, 0.45f), 0.0f, 1.0f);
    p->axial.cervicalDorsalMass = Math_Clamp(Positive(p->axial.cervicalDorsalMass, 1.0f), 0.2f, 2.5f);
    p->axial.cervicalMidNarrowing = Math_Clamp(Positive(p->axial.cervicalMidNarrowing, 0.82f), 0.5f, 1.0f);
    p->axial.withersElevation = Math_Clamp(isfinite(p->axial.withersElevation) && p->axial.withersElevation >= 0.0f ? p->axial.withersElevation : 0.08f, 0.0f, 0.35f);
    p->development.appendages = Unit(p->development.appendages, 1.0f);
    p->development.cephalic = Unit(p->development.cephalic, 1.0f);
    p->development.pigmentation = Unit(p->development.pigmentation, 1.0f);
    p->development.integument = Unit(p->development.integument, 1.0f);
    p->development.colorMaturity = Unit(p->development.colorMaturity, 0.0f);
    /* La positividad se valida arriba. La proporción cervical pertenece a la
     * receta: imponer aquí un cuello menor que los hombros impide perfiles
     * continuos y altera silenciosamente el resultado de los rasgos. */

    for (size_t i = 0; i < p->limbCount; ++i) {
        LimbPhenotype* l = &p->limbs[i];
        l->proximalSweep = Math_Clamp(isfinite(l->proximalSweep) ? l->proximalSweep : 0, -1, 1);
        l->distalSweep = Math_Clamp(isfinite(l->distalSweep) ? l->distalSweep : 0, -1, 1);
        l->footYaw = Math_Clamp(isfinite(l->footYaw) ? l->footYaw : 0, -3.14159265f, 3.14159265f);
        l->proximalScale = Positive(l->proximalScale, 1);
        l->middleScale = Positive(l->middleScale, 1);
        l->distalScale = Positive(l->distalScale, 1);
        l->rootThicknessScale = Positive(l->rootThicknessScale, 1.0f);
        l->proximalThicknessScale = Positive(l->proximalThicknessScale, 1.0f);
        l->middleThicknessScale = Positive(l->middleThicknessScale, 1.0f);
        l->distalThicknessScale = Positive(l->distalThicknessScale, 1.0f);
        l->footWidthScale = Positive(l->footWidthScale, 1.0f);
        l->footHeightScale = Positive(l->footHeightScale, 1.0f);
        l->digitThicknessScale = Positive(l->digitThicknessScale, 1.0f);
        if (!isfinite(l->attachmentInset) || l->attachmentInset <= 0.0f) {
            l->attachmentInset = 0.45f;
        } else {
            l->attachmentInset = Math_Clamp(l->attachmentInset, 0.10f, 0.90f);
        }
        l->length = Positive(l->length, 2.0f);
        l->thickness = Positive(l->thickness, 0.2f);
        l->development = Unit(l->development, 1.0f);
        l->sprawl = Unit(l->sprawl, 1.0f);
        l->footScale = Positive(l->footScale, 1.0f);
        l->digitSpread = Positive(l->digitSpread, 1.0f);
        if (l->digitCount > CREATURE_MAX_DIGITS) l->digitCount = CREATURE_MAX_DIGITS;
        for (unsigned j = 0; j < l->digitCount; ++j) {
            l->digitLengths[j] = Math_Clamp(Positive(l->digitLengths[j], 1.0f), 0.35f, 1.5f);
            if (!l->digitPhalanges[j]) l->digitPhalanges[j] = 3;
            if (l->digitPhalanges[j] > 5) l->digitPhalanges[j] = 5;
            if (!isfinite(l->digitAngles[j])) l->digitAngles[j] = 0.0f;
        }
    }

    for (size_t i = 0; i < p->tailCount; ++i) {
        TailPhenotype* t = &p->tails[i];
        t->length = Positive(t->length, 1.0f);
        t->baseWidth = Positive(t->baseWidth, 0.6f);
        t->baseHeight = Positive(t->baseHeight, 0.45f);
        t->tipWidth = Math_Min(Positive(t->tipWidth, 0.05f), t->baseWidth * 0.45f);
        t->tipHeight = Math_Min(Positive(t->tipHeight, 0.04f), t->baseHeight * 0.45f);
        t->taperCurve = Math_Clamp(Positive(t->taperCurve, 1.35f), 0.6f, 2.5f);
        t->development = Unit(t->development, 1.0f);
        if (!isfinite(t->curvature)) t->curvature = 0.0f;
        if (!isfinite(t->rootMatchStrength)) {
            t->rootMatchStrength = 0.0f;
        } else {
            t->rootMatchStrength = Math_Clamp01(t->rootMatchStrength);
        }
        if (t->segmentCount < 2) t->segmentCount = 2;
        if (t->segmentCount > CREATURE_MAX_TAIL_STATIONS) t->segmentCount = CREATURE_MAX_TAIL_STATIONS;
    }

    for (size_t i = 0; i < p->ornamentCount; ++i) {
        OrnamentPhenotype* o = &p->ornaments[i];
        o->length = Positive(o->length, 1.0f);
        o->baseRadius = Positive(o->baseRadius, 0.2f);
        o->tipRadius = Positive(o->tipRadius, 0.05f);
        o->development = Unit(o->development, 1.0f);
        if (!isfinite(o->curvature)) o->curvature = 0.0f;
    }

    p->eyes.size = isfinite(p->eyes.size) ? Math_Clamp(p->eyes.size, 0.0f, 4.0f) : 1.0f;
    p->eyes.protrusion = isfinite(p->eyes.protrusion) ? Math_Clamp(p->eyes.protrusion, 0.0f, 2.0f) : 1.0f;
    p->eyes.irisScale = Unit(p->eyes.irisScale, 0.72f);
    p->eyes.pupilScale = Unit(p->eyes.pupilScale, 0.34f);
    p->eyes.pupilAspect = Positive(p->eyes.pupilAspect, 1.0f);

#define ENVELOPE(name) p->headEnvelope.name = Positive(p->headEnvelope.name, 1.0f)
    ENVELOPE(scale); ENVELOPE(widthScale); ENVELOPE(heightScale); ENVELOPE(lengthScale);
#undef ENVELOPE
    HeadPhenotype_Normalize(&p->head);
    SurfacePhenotype_Normalize(&p->surface);
}

bool CreaturePhenotype_TopologyCompatible(const CreaturePhenotype* a, const CreaturePhenotype* b) {
    if (!a || !b || a->limbCount != b->limbCount || a->tailCount != b->tailCount ||
        a->ornamentCount != b->ornamentCount || a->limbCount > CREATURE_MAX_LIMBS ||
        a->tailCount > CREATURE_MAX_TAILS || a->ornamentCount > CREATURE_MAX_ORNAMENTS) {
        return false;
    }
    for (size_t i = 0; i < a->limbCount; ++i) {
        const LimbPhenotype *x = &a->limbs[i], *y = &b->limbs[i];
        if (x->role != y->role || x->side != y->side || x->digitCount != y->digitCount ||
            x->digitCount > CREATURE_MAX_DIGITS) {
            return false;
        }
        for (unsigned d = 0; d < x->digitCount; ++d) {
            if (x->digitPhalanges[d] != y->digitPhalanges[d]) return false;
        }
    }
    for (size_t i = 0; i < a->tailCount; ++i) {
        if (a->tails[i].segmentCount != b->tails[i].segmentCount) return false;
    }
    return true;
}

bool CreaturePhenotype_MorphCompatible(const CreaturePhenotype* a, const CreaturePhenotype* b) {
    if (!CreaturePhenotype_TopologyCompatible(a, b)) return false;
    if (a->axial.archetype != b->axial.archetype) return false;
    if (a->head.archetype != b->head.archetype) return false;
    for (size_t i = 0; i < a->limbCount; ++i) {
        if (a->limbs[i].archetype != b->limbs[i].archetype) return false;
    }
    for (size_t i = 0; i < a->tailCount; ++i) {
        if (a->tails[i].archetype != b->tails[i].archetype) return false;
    }
    for (size_t i = 0; i < a->ornamentCount; ++i) {
        if (a->ornaments[i].archetype != b->ornaments[i].archetype) return false;
    }
    return true;
}

bool CreaturePhenotype_Compatible(const CreaturePhenotype* a, const CreaturePhenotype* b) {
    return CreaturePhenotype_MorphCompatible(a, b);
}

CreaturePhenotype CreaturePhenotype_Interpolate(const CreaturePhenotype* a,
                                               const CreaturePhenotype* b,
                                               float age) {
    if (!a && !b) return (CreaturePhenotype){0};
    if (!a) return *b;
    if (!b) return *a;
    if (!CreaturePhenotype_TopologyCompatible(a, b)) return *a;

    if (!isfinite(age) || age <= 0.0f) return *a;
    if (age >= 1.0f) return *b;

    float t = Math_Clamp01(age);
    float linear = t;
    t = t * t * (3.0f - 2.0f * t);
    float mature = t * t;

    /* Base categórica discreta: selección clara t < 0.5 -> A, t >= 0.5 -> B */
    const CreaturePhenotype* cat = (linear < 0.5f) ? a : b;
    CreaturePhenotype p = *cat;

    p.dorsalColor = Color_Lerp(a->dorsalColor, b->dorsalColor, t);
    p.ventralColor = Color_Lerp(a->ventralColor, b->ventralColor, t);
    p.unpigmentedVentralColor = Color_Lerp(a->unpigmentedVentralColor, b->unpigmentedVentralColor, t);
    p.surface = SurfacePhenotype_Interpolate(&a->surface, &b->surface, t);

#define LERP_BODY(name) p.name = Math_Lerp(a->name, b->name, t)
    for (size_t i = 0; i < AXIAL_PROFILE_STATIONS; ++i) {
        LERP_BODY(axial.profile[i].widthScale); LERP_BODY(axial.profile[i].heightScale);
        LERP_BODY(axial.profile[i].longitudinalOffset); LERP_BODY(axial.profile[i].verticalOffset);
    }
    LERP_BODY(headEnvelope.scale); LERP_BODY(headEnvelope.widthScale);
    LERP_BODY(headEnvelope.heightScale); LERP_BODY(headEnvelope.lengthScale);
    LERP_BODY(axial.withersHeight); LERP_BODY(axial.totalScale); LERP_BODY(axial.neckLength); LERP_BODY(axial.neckWidth);
    LERP_BODY(axial.shoulderWidth); LERP_BODY(axial.thoraxWidth); LERP_BODY(axial.thoraxHeight);
    LERP_BODY(axial.abdomenWidth); LERP_BODY(axial.abdomenHeight); LERP_BODY(axial.pelvicWidth);
    LERP_BODY(axial.pelvicHeight); LERP_BODY(axial.trunkLength); LERP_BODY(axial.bodyFlattening);
    LERP_BODY(axial.limbAttachmentLateral);
    LERP_BODY(development.colorMaturity); LERP_BODY(development.pigmentation);
    LERP_BODY(development.appendages); LERP_BODY(development.cephalic); LERP_BODY(development.integument);
    LERP_BODY(eyes.size); LERP_BODY(eyes.protrusion); LERP_BODY(eyes.irisScale);
    LERP_BODY(eyes.pupilScale); LERP_BODY(eyes.pupilAspect);
    p.eyes.scleraColor = Color_Lerp(a->eyes.scleraColor, b->eyes.scleraColor, t);
    p.eyes.irisColor = Color_Lerp(a->eyes.irisColor, b->eyes.irisColor, t);
    p.eyes.pupilColor = Color_Lerp(a->eyes.pupilColor, b->eyes.pupilColor, t);

    for (size_t i = 0; i < p.limbCount; ++i) {
        LERP_BODY(limbs[i].proximalScale); LERP_BODY(limbs[i].middleScale); LERP_BODY(limbs[i].distalScale);
        LERP_BODY(limbs[i].rootThicknessScale); LERP_BODY(limbs[i].proximalThicknessScale);
        LERP_BODY(limbs[i].middleThicknessScale); LERP_BODY(limbs[i].distalThicknessScale);
        LERP_BODY(limbs[i].footWidthScale); LERP_BODY(limbs[i].footHeightScale);
        LERP_BODY(limbs[i].digitThicknessScale);
        LERP_BODY(limbs[i].attachmentInset);
        LERP_BODY(limbs[i].proximalSweep); LERP_BODY(limbs[i].distalSweep); LERP_BODY(limbs[i].footYaw);
        LERP_BODY(limbs[i].length); LERP_BODY(limbs[i].sprawl); LERP_BODY(limbs[i].development);
        p.limbs[i].thickness = Math_Lerp(a->limbs[i].thickness, b->limbs[i].thickness, mature);
        LERP_BODY(limbs[i].digitSpread); LERP_BODY(limbs[i].footScale);
        for (unsigned j = 0; j < p.limbs[i].digitCount; ++j) {
            LERP_BODY(limbs[i].digitLengths[j]); LERP_BODY(limbs[i].digitAngles[j]);
        }
    }
    for (size_t i = 0; i < p.tailCount; ++i) {
        LERP_BODY(tails[i].length); LERP_BODY(tails[i].baseWidth); LERP_BODY(tails[i].baseHeight);
        LERP_BODY(tails[i].tipWidth); LERP_BODY(tails[i].tipHeight); LERP_BODY(tails[i].taperCurve);
        LERP_BODY(tails[i].curvature); LERP_BODY(tails[i].development);
        LERP_BODY(tails[i].rootMatchStrength);
    }
    for (size_t i = 0; i < p.ornamentCount; ++i) {
        LERP_BODY(ornaments[i].length); LERP_BODY(ornaments[i].baseRadius); LERP_BODY(ornaments[i].tipRadius);
        LERP_BODY(ornaments[i].curvature); LERP_BODY(ornaments[i].development);
    }
#undef LERP_BODY

#define LERP_HEAD(name) p.head.name = Math_Lerp(a->head.name, b->head.name, t)
    LERP_HEAD(skullWidth); LERP_HEAD(skullHeight); LERP_HEAD(skullLength);
    LERP_HEAD(muzzleLength); LERP_HEAD(muzzleWidth); LERP_HEAD(muzzleTaper);
    LERP_HEAD(rostrumDepth); LERP_HEAD(rostrumDorsalSlope);
    LERP_HEAD(temporalWidth); LERP_HEAD(temporalDepth);
    p.head.eyeSize = Math_Lerp(a->head.eyeSize, b->head.eyeSize, linear);
    LERP_HEAD(eyeLaterality); LERP_HEAD(eyeForwardness);
    LERP_HEAD(eyelidCoverage); LERP_HEAD(eyeCompression); LERP_HEAD(eyeDorsality); LERP_HEAD(eyeExposure); LERP_HEAD(browProminence);
    LERP_HEAD(snoutBluntness);
    LERP_HEAD(jawLength); LERP_HEAD(jawDepth);
    p.head.jawStrength = Math_Lerp(a->head.jawStrength, b->head.jawStrength, mature);
    LERP_HEAD(earConcavity); LERP_HEAD(earBaseWidth); LERP_HEAD(earThickness); LERP_HEAD(earOutward); LERP_HEAD(earForward);
    LERP_HEAD(noseScale); LERP_HEAD(earSize); LERP_HEAD(earPointiness);
    p.head.cheekMass = Math_Lerp(a->head.cheekMass, b->head.cheekMass, mature);
    LERP_HEAD(beakLength); LERP_HEAD(beakDepth);
    LERP_HEAD(beakTaper); LERP_HEAD(beakCurvature); LERP_HEAD(nostrilPosition);
    LERP_HEAD(orbitDepth); LERP_HEAD(tympanumSize);
#undef LERP_HEAD

    CreaturePhenotype_Normalize(&p);
    return p;
}
