/**
 * @file CreaturePhenotype.c
 * @brief Implementación de normalización e interpolación morfológica modular.
 * @author Monster Engine Team
 * @date 2026
 */

#include "CreaturePhenotype.h"
#include "FlexibleAppendage.h"
#include "Fin.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

static float Positive(float x, float fallback) { return isfinite(x) && x > 0 ? x : fallback; }
static float Unit(float x, float fallback) { return isfinite(x) ? Math_Clamp01(x) : fallback; }

void CreaturePhenotype_Normalize(CreaturePhenotype* p) {
    if (!p) return;
    if (p->limbCount > CREATURE_MAX_LIMBS) p->limbCount = CREATURE_MAX_LIMBS;
    if (p->tailCount > CREATURE_MAX_TAILS) p->tailCount = CREATURE_MAX_TAILS;
    if (p->flexibleAppendageCount > CREATURE_MAX_FLEXIBLE_APPENDAGES)
        p->flexibleAppendageCount = CREATURE_MAX_FLEXIBLE_APPENDAGES;
    if (p->ornamentCount > CREATURE_MAX_ORNAMENTS) p->ornamentCount = CREATURE_MAX_ORNAMENTS;
    if (p->membraneCount > CREATURE_MAX_MEMBRANES) p->membraneCount = CREATURE_MAX_MEMBRANES;
    if (p->repeatedAppendageCount > CREATURE_MAX_REPEATED_APPENDAGES)
        p->repeatedAppendageCount = CREATURE_MAX_REPEATED_APPENDAGES;
    if (p->finCount > CREATURE_MAX_FINS) p->finCount = CREATURE_MAX_FINS;

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
    p->axial.withersRoundness=Unit(p->axial.withersRoundness,0);
    p->axial.dewlapDepth=Unit(p->axial.dewlapDepth,0);
    p->axial.brisketDepth=Unit(p->axial.brisketDepth,0);
    if (p->axial.cervicalSegments > 8) p->axial.cervicalSegments = 8;
    p->axial.cervicalPitch = isfinite(p->axial.cervicalPitch) ? Math_Clamp(p->axial.cervicalPitch, 0.0f, 1.45f) : 0.0f;
    p->axial.spineFlexion = isfinite(p->axial.spineFlexion) ? Math_Clamp(p->axial.spineFlexion, 0.0f, 0.65f) : 0.0f;
    if (p->axial.radialSymmetryOrder > 16) p->axial.radialSymmetryOrder = 16;
    p->axial.bellConcavity = Unit(p->axial.bellConcavity, 0.0f);
    p->axial.bellScallopAmplitude = Unit(p->axial.bellScallopAmplitude, 0.0f);
    if (p->axial.abdomenSegments > 12) p->axial.abdomenSegments = 12;
    p->axial.discSweep = Unit(p->axial.discSweep, 0.0f);
    p->axial.mantleElongation = isfinite(p->axial.mantleElongation) ? Math_Clamp(p->axial.mantleElongation, 0.0f, 2.5f) : 0.0f;
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
        l->flexionScale=Math_Clamp(Positive(l->flexionScale,1),.5f,2);
        l->hoofWallSlope=Math_Clamp(Positive(l->hoofWallSlope,.22f),.02f,.45f);
        l->hoofToeRoundness=Math_Clamp(Positive(l->hoofToeRoundness,.8f),.3f,1);
        l->hoofHeelHeight=Math_Clamp(Positive(l->hoofHeelHeight,.75f),.4f,1);
        l->clawScale=Math_Clamp(isfinite(l->clawScale)?l->clawScale:0,0,3);
        l->webbingCoverage=Unit(l->webbingCoverage,0);
        l->webbingThickness=Math_Clamp(isfinite(l->webbingThickness)&&l->webbingThickness>0?l->webbingThickness:0.22f,0.06f,0.55f);
        l->webbingEdgeCurve=Math_Clamp(isfinite(l->webbingEdgeCurve)?l->webbingEdgeCurve:0.18f,-0.30f,0.55f);
        l->webbingFold=Unit(l->webbingFold,0);
        if (l->featherCount > 32) l->featherCount = 32;
        l->wingAspect = isfinite(l->wingAspect) ? Math_Clamp(l->wingAspect, 0.0f, 3.5f) : 0.0f;
        l->wingPointedness = Unit(l->wingPointedness, 0.0f);
        l->finSweep = isfinite(l->finSweep) ? Math_Clamp(l->finSweep, -0.6f, 1.0f) : 0.0f;
        l->finCamber = isfinite(l->finCamber) ? Math_Clamp(l->finCamber, 0.0f, 0.45f) : 0.0f;
        l->dewclawSize=Unit(l->dewclawSize,0);
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
        t->droop=Unit(t->droop,0);
        t->tipLift=Math_Clamp(isfinite(t->tipLift)?t->tipLift:0,-1,1);
        t->length = Positive(t->length, 1.0f);
        t->baseWidth = Positive(t->baseWidth, 0.6f);
        t->baseHeight = Positive(t->baseHeight, 0.45f);
        float tipWidthCap = t->archetype == TAIL_ARCHETYPE_PADDLE ? 1.50f : 0.45f;
        t->tipWidth = Math_Min(Positive(t->tipWidth, 0.05f), t->baseWidth * tipWidthCap);
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
        if (t->caudalFinType < CAUDAL_FIN_NONE || t->caudalFinType > CAUDAL_FIN_ROUNDED_VERTICAL)
            t->caudalFinType = CAUDAL_FIN_NONE;
        t->caudalSpan = isfinite(t->caudalSpan) && t->caudalSpan > 0.0f ? Math_Clamp(t->caudalSpan, 0.02f, 8.0f) : 0.0f;
        t->caudalChord = isfinite(t->caudalChord) && t->caudalChord > 0.0f ? Math_Clamp(t->caudalChord, 0.02f, 6.0f) : 0.0f;
        t->caudalUpperLobeRatio = isfinite(t->caudalUpperLobeRatio) && t->caudalUpperLobeRatio > 0.0f
            ? Math_Clamp(t->caudalUpperLobeRatio, 0.5f, 2.2f) : 1.0f;
        t->caudalNotchDepth = isfinite(t->caudalNotchDepth) ? Math_Clamp01(t->caudalNotchDepth) : 0.35f;
        t->caudalSweep = isfinite(t->caudalSweep) ? Math_Clamp(t->caudalSweep, 0.0f, 1.4f) : 0.45f;
        t->caudalThickness = isfinite(t->caudalThickness) && t->caudalThickness > 0.0f
            ? Math_Clamp(t->caudalThickness, 0.004f, 0.6f) : 0.03f;
    }

    for (size_t i = 0; i < p->ornamentCount; ++i) {
        OrnamentPhenotype* o = &p->ornaments[i];
        o->length = Positive(o->length, 1.0f);
        o->baseRadius = Positive(o->baseRadius, 0.2f);
        o->tipRadius = Positive(o->tipRadius, 0.05f);
        o->development = Unit(o->development, 1.0f);
        if (!isfinite(o->curvature)) o->curvature = 0.0f;
        if(!isfinite(o->growthDirection.x)||!isfinite(o->growthDirection.y)||!isfinite(o->growthDirection.z))o->growthDirection=Vec3_Zero();
        if(!isfinite(o->lateralSweep))o->lateralSweep=0;
        o->lateralSweep=Math_Clamp(o->lateralSweep,-1,1);
    }

    for (size_t i = 0; i < p->flexibleAppendageCount; ++i) {
        FlexibleAppendagePhenotype* f = &p->flexibleAppendages[i];
        f->length = Positive(f->length, 1);
        f->baseRadius = Positive(f->baseRadius, .2f);
        f->tipRadius = Math_Min(f->baseRadius, Positive(f->tipRadius, .08f));
        f->aspect = Math_Clamp(Positive(f->aspect, 1), .6f, 1.4f);
        f->taper = Math_Clamp(Positive(f->taper, 1), .5f, 4);
        f->initialPitch = isfinite(f->initialPitch) ? Math_Clamp(f->initialPitch, -1.57f, 1.57f) : 0;
        f->restBend = isfinite(f->restBend) ? Math_Clamp(f->restBend, -1.57f, 1.57f) : 0;
        if (f->profile < FLEXIBLE_PROFILE_PROBOSCIS || f->profile > FLEXIBLE_PROFILE_SIPHON)
            f->profile = FLEXIBLE_PROFILE_PROBOSCIS;
        f->yawAngle = isfinite(f->yawAngle) ? Math_Clamp(f->yawAngle, -3.14159265f, 3.14159265f) : 0.0f;
        f->coilBias = isfinite(f->coilBias) ? Math_Clamp(f->coilBias, -2.5f, 2.5f) : 0.0f;
        if (f->suckerRows > 2) f->suckerRows = 2;
        if (f->suckerCount > 40) f->suckerCount = 40;
        f->suckerScale = isfinite(f->suckerScale) ? Math_Clamp(f->suckerScale, 0.0f, 1.2f) : 0.0f;
    }

    for (size_t i = 0; i < p->repeatedAppendageCount; ++i) {
        RepeatedAppendageGroup* r = &p->repeatedAppendages[i];
        if (!r->groupId) r->groupId = (uint32_t)(i + 1u);
        if (r->kind < REPEATED_APPENDAGE_NONE || r->kind > REPEATED_APPENDAGE_FRILLED_RIBBON)
            r->kind = REPEATED_APPENDAGE_FILAMENT;
        if (r->count > 64) r->count = 64;
        r->ringRadius = isfinite(r->ringRadius) && r->ringRadius >= 0.0f ? Math_Clamp(r->ringRadius, 0.0f, 10.0f) : 0.5f;
        r->ringOffsetY = isfinite(r->ringOffsetY) ? Math_Clamp(r->ringOffsetY, -10.0f, 10.0f) : 0.0f;
        r->arcStart = isfinite(r->arcStart) ? r->arcStart : 0.0f;
        r->arcSpan = isfinite(r->arcSpan) ? Math_Clamp(r->arcSpan, 0.0f, 6.28318530718f) : 6.28318530718f;
        r->length = Positive(r->length, 1.0f);
        r->lengthVariation = isfinite(r->lengthVariation) ? Math_Clamp(r->lengthVariation, 0.0f, 0.65f) : 0.0f;
        r->baseRadius = Positive(r->baseRadius, 0.02f);
        r->tipRadius = Math_Min(r->baseRadius, Positive(r->tipRadius, 0.005f));
        r->taper = Math_Clamp(Positive(r->taper, 1.1f), 0.35f, 3.5f);
        r->ribbonWidth = isfinite(r->ribbonWidth) && r->ribbonWidth >= 0.0f ? Math_Clamp(r->ribbonWidth, 0.0f, 3.0f) : 0.0f;
        r->ruffleAmplitude = isfinite(r->ruffleAmplitude) ? Math_Clamp(r->ruffleAmplitude, 0.0f, 2.0f) : 0.0f;
        r->ruffleFrequency = isfinite(r->ruffleFrequency) ? Math_Clamp(r->ruffleFrequency, 0.0f, 32.0f) : 4.0f;
        r->curlAmplitude = isfinite(r->curlAmplitude) ? Math_Clamp(r->curlAmplitude, 0.0f, 3.0f) : 0.0f;
        r->waveFrequency = isfinite(r->waveFrequency) ? Math_Clamp(r->waveFrequency, 0.0f, 16.0f) : 2.0f;
        r->phaseSpread = isfinite(r->phaseSpread) ? r->phaseSpread : 0.0f;
        r->development = Unit(r->development, 1.0f);
        r->opacity = isfinite(r->opacity) && r->opacity > 0.0f ? Math_Clamp(r->opacity, 0.08f, 1.0f) : 0.85f;
    }

    for (size_t i = 0; i < p->membraneCount; ++i) {
        AnatomicalMembraneProfile* m = &p->membranes[i];
        m->coverage = Unit(m->coverage, 1.0f);
        m->thickness = Math_Clamp(isfinite(m->thickness) && m->thickness > 0 ? m->thickness : 0.012f, 0.002f, 0.08f);
        m->edgeConcavity = Math_Clamp(isfinite(m->edgeConcavity) ? m->edgeConcavity : 0.16f, -0.25f, 0.55f);
        m->camber = Math_Clamp(isfinite(m->camber) ? m->camber : 0.04f, -0.20f, 0.30f);
        m->tension = Unit(m->tension, 0.85f);
        m->development = Unit(m->development, 1.0f);
    }

    for (size_t i = 0; i < p->finCount; ++i) {
        FinPhenotype_Normalize(&p->fins[i]);
    }

    p->shell.development = isfinite(p->shell.development) ? Math_Clamp01(p->shell.development) : 0.0f;
    if (p->shell.development > 0.0f) {
        p->shell.length = Math_Clamp(Positive(p->shell.length, p->axial.trunkLength * 1.15f), 0.1f, 12.0f);
        p->shell.width = Math_Clamp(Positive(p->shell.width, p->axial.thoraxWidth * 1.35f), 0.1f, 10.0f);
        p->shell.height = Math_Clamp(Positive(p->shell.height, p->axial.thoraxHeight * 0.95f), 0.05f, 6.0f);
        p->shell.dorsalConvexity = Math_Clamp(Positive(p->shell.dorsalConvexity, 0.55f), 0.1f, 1.0f);
        p->shell.heartTaper = Unit(p->shell.heartTaper, 0.25f);
        p->shell.plastronWidth = Math_Clamp(Positive(p->shell.plastronWidth, p->shell.width * 0.72f), 0.05f, 9.0f);
        p->shell.plastronLength = Math_Clamp(Positive(p->shell.plastronLength, p->shell.length * 0.78f), 0.05f, 11.0f);
        p->shell.plastronDepth = Math_Clamp(Positive(p->shell.plastronDepth, p->shell.height * 0.42f), 0.02f, 4.0f);
        p->shell.bridgeWidth = Math_Clamp(Positive(p->shell.bridgeWidth, p->shell.length * 0.38f), 0.05f, 8.0f);
        p->shell.shellThickness = Math_Clamp(Positive(p->shell.shellThickness, 0.06f), 0.01f, 0.6f);
        if (p->shell.costalScutePairs > 8) p->shell.costalScutePairs = 8;
        if (p->shell.vertebralScutes > 10) p->shell.vertebralScutes = 10;
        if (p->shell.marginalScutes > 18) p->shell.marginalScutes = 18;
        p->shell.scuteRelief = Unit(p->shell.scuteRelief, 0.45f);
        p->shell.cervicalNotch = Math_Clamp(Unit(p->shell.cervicalNotch, 0.12f), 0.0f, 0.45f);
        p->shell.limbClearance = Math_Clamp(Unit(p->shell.limbClearance, 0.22f), 0.0f, 0.55f);
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
    unsigned cervA = a->axial.cervicalSegments > 1 ? a->axial.cervicalSegments : 1;
    unsigned cervB = b->axial.cervicalSegments > 1 ? b->axial.cervicalSegments : 1;
    if (cervA != cervB) return false;
    if (a->axial.abdomenSegments != b->axial.abdomenSegments ||
        a->axial.radialSymmetryOrder != b->axial.radialSymmetryOrder) return false;
    if (a->flexibleAppendageCount != b->flexibleAppendageCount ||
        a->flexibleAppendageCount > CREATURE_MAX_FLEXIBLE_APPENDAGES) return false;
    if (a->membraneCount != b->membraneCount ||
        a->membraneCount > CREATURE_MAX_MEMBRANES) return false;
    if (a->repeatedAppendageCount != b->repeatedAppendageCount ||
        a->repeatedAppendageCount > CREATURE_MAX_REPEATED_APPENDAGES) return false;
    if (a->finCount != b->finCount || a->finCount > CREATURE_MAX_FINS) return false;
    for (size_t i = 0; i < a->finCount; ++i) {
        if (a->fins[i].role != b->fins[i].role || a->fins[i].side != b->fins[i].side) return false;
    }
    for (size_t i = 0; i < a->repeatedAppendageCount; ++i) {
        if (a->repeatedAppendages[i].kind != b->repeatedAppendages[i].kind ||
            a->repeatedAppendages[i].count != b->repeatedAppendages[i].count) return false;
    }
    for (size_t i = 0; i < a->flexibleAppendageCount; ++i)
        if (a->flexibleAppendages[i].terminal != b->flexibleAppendages[i].terminal) return false;
    for (size_t i = 0; i < a->limbCount; ++i) {
        const LimbPhenotype *x = &a->limbs[i], *y = &b->limbs[i];
        if (x->role != y->role || x->side != y->side || x->digitCount != y->digitCount ||
            x->anatomyProfile != y->anatomyProfile || x->nailCount != y->nailCount ||
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
    if (a->axial.contourProfile != b->axial.contourProfile) return false;
    if (a->head.archetype != b->head.archetype || a->headHybrid.active != b->headHybrid.active ||
        a->headHybrid.hasRegionalProfile != b->headHybrid.hasRegionalProfile ||
        (a->headHybrid.active &&
            (a->headHybrid.secondary.archetype != b->headHybrid.secondary.archetype ||
             a->headHybrid.secondary.cranialForm != b->headHybrid.secondary.cranialForm ||
             a->headHybrid.secondary.muzzleForm != b->headHybrid.secondary.muzzleForm ||
             a->headHybrid.secondary.eyeLayout != b->headHybrid.secondary.eyeLayout)))
        return false;
    /* Resolver de nuevo una categoría a mitad de Ager cambia el volumen de
     * golpe. Estas transiciones necesitan mezclar anatomías ya resueltas. */
    if (a->head.cranialForm != b->head.cranialForm ||
        a->head.muzzleForm != b->head.muzzleForm ||
        a->head.eyeLayout != b->head.eyeLayout) return false;
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
    p.surface = SurfacePhenotype_Develop(&a->surface, &b->surface, t);

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
    LERP_BODY(axial.cervicalCurvature); LERP_BODY(axial.cervicalDorsalMass);
    LERP_BODY(axial.cervicalMidNarrowing); LERP_BODY(axial.withersElevation);
    LERP_BODY(axial.withersRoundness);
    LERP_BODY(axial.dewlapDepth); LERP_BODY(axial.brisketDepth);
    LERP_BODY(axial.cervicalPitch); LERP_BODY(axial.spineFlexion);
    LERP_BODY(axial.bellConcavity); LERP_BODY(axial.bellScallopAmplitude);
    LERP_BODY(axial.discSweep); LERP_BODY(axial.mantleElongation);
    LERP_BODY(development.colorMaturity); LERP_BODY(development.pigmentation);
    LERP_BODY(development.appendages); LERP_BODY(development.cephalic); LERP_BODY(development.integument);
    LERP_BODY(eyes.size); LERP_BODY(eyes.protrusion); LERP_BODY(eyes.irisScale);
    LERP_BODY(eyes.pupilScale); LERP_BODY(eyes.pupilAspect);
    p.eyes.scleraColor = Color_Lerp(a->eyes.scleraColor, b->eyes.scleraColor, t);
    p.eyes.irisColor = Color_Lerp(a->eyes.irisColor, b->eyes.irisColor, t);
    p.eyes.pupilColor = Color_Lerp(a->eyes.pupilColor, b->eyes.pupilColor, t);
    p.head.nasalColor = Color_Lerp(a->head.nasalColor, b->head.nasalColor, t);

    for (size_t i = 0; i < p.limbCount; ++i) {
        LERP_BODY(limbs[i].proximalScale); LERP_BODY(limbs[i].middleScale); LERP_BODY(limbs[i].distalScale);
        LERP_BODY(limbs[i].rootThicknessScale); LERP_BODY(limbs[i].proximalThicknessScale);
        LERP_BODY(limbs[i].middleThicknessScale); LERP_BODY(limbs[i].distalThicknessScale);
        LERP_BODY(limbs[i].footWidthScale); LERP_BODY(limbs[i].footHeightScale);
        LERP_BODY(limbs[i].digitThicknessScale);
        LERP_BODY(limbs[i].attachmentInset);
        LERP_BODY(limbs[i].proximalSweep); LERP_BODY(limbs[i].distalSweep); LERP_BODY(limbs[i].footYaw);
        LERP_BODY(limbs[i].flexionScale);
        LERP_BODY(limbs[i].hoofWallSlope); LERP_BODY(limbs[i].hoofToeRoundness);
        LERP_BODY(limbs[i].hoofHeelHeight); LERP_BODY(limbs[i].dewclawSize);
        LERP_BODY(limbs[i].length); LERP_BODY(limbs[i].sprawl); LERP_BODY(limbs[i].development);
        p.limbs[i].thickness = Math_Lerp(a->limbs[i].thickness, b->limbs[i].thickness, mature);
        LERP_BODY(limbs[i].digitSpread); LERP_BODY(limbs[i].footScale);
        LERP_BODY(limbs[i].clawScale);
        LERP_BODY(limbs[i].webbingCoverage);
        LERP_BODY(limbs[i].webbingThickness);
        LERP_BODY(limbs[i].webbingEdgeCurve);
        LERP_BODY(limbs[i].webbingFold);
        LERP_BODY(limbs[i].wingAspect);
        LERP_BODY(limbs[i].wingPointedness);
        LERP_BODY(limbs[i].finSweep);
        LERP_BODY(limbs[i].finCamber);
        for (unsigned j = 0; j < p.limbs[i].digitCount; ++j) {
            LERP_BODY(limbs[i].digitLengths[j]); LERP_BODY(limbs[i].digitAngles[j]);
        }
    }
    for (size_t i = 0; i < p.tailCount; ++i) {
        LERP_BODY(tails[i].length); LERP_BODY(tails[i].baseWidth); LERP_BODY(tails[i].baseHeight);
        LERP_BODY(tails[i].tipWidth); LERP_BODY(tails[i].tipHeight); LERP_BODY(tails[i].taperCurve); LERP_BODY(tails[i].droop);
        LERP_BODY(tails[i].curvature); LERP_BODY(tails[i].development); LERP_BODY(tails[i].tipLift);
        LERP_BODY(tails[i].rootMatchStrength);
        LERP_BODY(tails[i].caudalSpan); LERP_BODY(tails[i].caudalChord);
        LERP_BODY(tails[i].caudalUpperLobeRatio); LERP_BODY(tails[i].caudalNotchDepth);
        LERP_BODY(tails[i].caudalSweep); LERP_BODY(tails[i].caudalThickness);
        p.tails[i].caudalColor = Color_Lerp(a->tails[i].caudalColor, b->tails[i].caudalColor, t);
    }
    for (size_t i = 0; i < p.finCount; ++i)
        p.fins[i] = FinPhenotype_Interpolate(&a->fins[i], &b->fins[i], t);
    LERP_BODY(shell.development);
    LERP_BODY(shell.length); LERP_BODY(shell.width); LERP_BODY(shell.height);
    LERP_BODY(shell.dorsalConvexity); LERP_BODY(shell.heartTaper);
    LERP_BODY(shell.plastronWidth); LERP_BODY(shell.plastronLength); LERP_BODY(shell.plastronDepth);
    LERP_BODY(shell.bridgeWidth); LERP_BODY(shell.shellThickness);
    LERP_BODY(shell.scuteRelief); LERP_BODY(shell.cervicalNotch); LERP_BODY(shell.limbClearance);
    p.shell.carapaceColor = Color_Lerp(a->shell.carapaceColor, b->shell.carapaceColor, t);
    p.shell.scuteBorderColor = Color_Lerp(a->shell.scuteBorderColor, b->shell.scuteBorderColor, t);
    p.shell.plastronColor = Color_Lerp(a->shell.plastronColor, b->shell.plastronColor, t);
    for (size_t i = 0; i < p.flexibleAppendageCount; ++i)
        p.flexibleAppendages[i]=FlexibleAppendage_Interpolate(
            &a->flexibleAppendages[i],&b->flexibleAppendages[i],t,t);
    for (size_t i = 0; i < p.ornamentCount; ++i) {
        LERP_BODY(ornaments[i].length); LERP_BODY(ornaments[i].baseRadius); LERP_BODY(ornaments[i].tipRadius);
        LERP_BODY(ornaments[i].curvature); LERP_BODY(ornaments[i].development);
        LERP_BODY(ornaments[i].growthDirection.x);LERP_BODY(ornaments[i].growthDirection.y);LERP_BODY(ornaments[i].growthDirection.z);
        LERP_BODY(ornaments[i].lateralSweep);
    }
    for (size_t i = 0; i < p.membraneCount; ++i) {
        LERP_BODY(membranes[i].coverage);
        LERP_BODY(membranes[i].thickness);
        LERP_BODY(membranes[i].edgeConcavity);
        LERP_BODY(membranes[i].camber);
        LERP_BODY(membranes[i].tension);
        LERP_BODY(membranes[i].development);
        p.membranes[i].color = Color_Lerp(a->membranes[i].color, b->membranes[i].color, t);
    }
    for (size_t i = 0; i < p.repeatedAppendageCount; ++i) {
        LERP_BODY(repeatedAppendages[i].ringRadius);
        LERP_BODY(repeatedAppendages[i].ringOffsetY);
        LERP_BODY(repeatedAppendages[i].arcStart);
        LERP_BODY(repeatedAppendages[i].arcSpan);
        LERP_BODY(repeatedAppendages[i].length);
        LERP_BODY(repeatedAppendages[i].lengthVariation);
        LERP_BODY(repeatedAppendages[i].baseRadius);
        LERP_BODY(repeatedAppendages[i].tipRadius);
        LERP_BODY(repeatedAppendages[i].taper);
        LERP_BODY(repeatedAppendages[i].ribbonWidth);
        LERP_BODY(repeatedAppendages[i].ruffleAmplitude);
        LERP_BODY(repeatedAppendages[i].ruffleFrequency);
        LERP_BODY(repeatedAppendages[i].curlAmplitude);
        LERP_BODY(repeatedAppendages[i].waveFrequency);
        LERP_BODY(repeatedAppendages[i].phaseSpread);
        LERP_BODY(repeatedAppendages[i].development);
        LERP_BODY(repeatedAppendages[i].opacity);
        p.repeatedAppendages[i].baseColor = Color_Lerp(a->repeatedAppendages[i].baseColor, b->repeatedAppendages[i].baseColor, t);
        p.repeatedAppendages[i].tipColor = Color_Lerp(a->repeatedAppendages[i].tipColor, b->repeatedAppendages[i].tipColor, t);
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
    LERP_HEAD(earWidthRatio);
    LERP_HEAD(earHeightRatio);
    LERP_HEAD(noseScale); LERP_HEAD(earSize); LERP_HEAD(earPointiness);
    p.head.cheekMass = Math_Lerp(a->head.cheekMass, b->head.cheekMass, mature);
    LERP_HEAD(beakLength); LERP_HEAD(beakDepth);
    LERP_HEAD(beakTaper); LERP_HEAD(beakCurvature); LERP_HEAD(nostrilPosition);
    LERP_HEAD(orbitDepth); LERP_HEAD(tympanumSize);
#undef LERP_HEAD

    if (a->headHybrid.active && b->headHybrid.active) {
        p.head = HeadPhenotype_Interpolate(&a->head,&b->head,t);
        p.headHybrid.secondary = HeadPhenotype_Interpolate(&a->headHybrid.secondary,&b->headHybrid.secondary,t);
        p.headHybrid.primaryRadii = Vec3_Lerp(a->headHybrid.primaryRadii,b->headHybrid.primaryRadii,t);
        p.headHybrid.secondaryRadii = Vec3_Lerp(a->headHybrid.secondaryRadii,b->headHybrid.secondaryRadii,t);
        p.headHybrid.blend = Math_Lerp(a->headHybrid.blend,b->headHybrid.blend,t);
        if (p.headHybrid.hasRegionalProfile) {
            HeadAnatomy *head = &p.headHybrid.regional;
            HeadAnatomy_Blend(&a->headHybrid.regional,&b->headHybrid.regional,t,head);
            /* La pendiente interpolada multiplicada por la distancia al
             * cóndilo no equivale a interpolar las dos alturas labiales. */
            HeadLandmarks *l = &head->landmarks;
            float oralLine = l->leftMouthCorner.y + head->surface.oralSlope *
                (l->leftJawHinge.z-l->leftMouthCorner.z);
            float drop = l->leftJawHinge.y-oralLine;
            if (drop >= 0) {
                drop += head->surface.craniumRadii.y*.002f;
                l->leftJawHinge.y -= drop;
                l->rightJawHinge.y -= drop;
                head->oralSystem.jawPivot.y -= drop;
                head->surface.mandibularCenters[4].y -= drop*.55f;
                head->surface.mandibularCenters[5].y -= drop;
            }
#define CONTROL(field) p.headHybrid.referenceControls.field = Math_Lerp( \
                a->headHybrid.referenceControls.field,b->headHybrid.referenceControls.field,t)
            CONTROL(skullWidth); CONTROL(muzzleLength); CONTROL(jawDepth); CONTROL(earSize); CONTROL(eyeSize);
#undef CONTROL
        }
    }

    CreaturePhenotype_Normalize(&p);
    return p;
}
