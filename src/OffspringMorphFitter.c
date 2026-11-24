#include "OffspringMorphFitter.h"
#include "AxialBody.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>
#include <stdlib.h>

static bool AxialArchetypesCompatible(AxialArchetype a, AxialArchetype b)
{
    if (a == b) return true;
    return (a == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD && b == AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD) ||
           (a == AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD && b == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD);
}

float OffspringMorph_HeadError(const HeadAnatomy *a, float scale,
    const HeadAnatomy *b, float targetScale)
{
    Vector3 neckA = a->landmarks.neckAttachment, neckB = b->landmarks.neckAttachment;
    float sum = 0, inverseA = 1/scale, inverseB = 1/targetScale;
    unsigned terms = 18;
#define SIZE(v) sum += Vec3_LengthSq(Vec3_Sub(Vec3_Scale(a->v,inverseA),Vec3_Scale(b->v,inverseB)))
#define POINT(v) sum += Vec3_LengthSq(Vec3_Sub(Vec3_Scale(Vec3_Sub(a->v,neckA),inverseA), \
                                              Vec3_Scale(Vec3_Sub(b->v,neckB),inverseB)))
    POINT(surface.craniumCenter); SIZE(surface.craniumRadii);
    POINT(surface.faceRoot); POINT(surface.faceMid); POINT(surface.faceTip);
    SIZE(surface.faceRootRadii); SIZE(surface.faceMidRadii); SIZE(surface.faceTipRadii);
    POINT(surface.leftCheekCenter); SIZE(surface.cheekRadii);
    POINT(surface.leftMaxillaryCenter); SIZE(surface.maxillaryRadii);
    POINT(landmarks.leftJawHinge); POINT(landmarks.mandibularSymphysis);
    POINT(landmarks.leftOrbit); SIZE(surface.orbitRadii);
    POINT(landmarks.leftEarBase); SIZE(surface.earShape);
    if (a->surface.beakWeight > 0.5f || b->surface.beakWeight > 0.5f) {
        POINT(surface.beakBase); POINT(surface.beakTip);
        Vector3 brA = Vec3_Create(a->surface.beakBaseRadii.x * inverseA, a->surface.beakBaseRadii.y * inverseA, 0);
        Vector3 brB = Vec3_Create(b->surface.beakBaseRadii.x * inverseB, b->surface.beakBaseRadii.y * inverseB, 0);
        sum += Vec3_LengthSq(Vec3_Sub(brA, brB));
        terms += 3;
    }
#undef POINT
#undef SIZE
    return sqrtf(sum/(float)terms)/fmaxf(.001f,Vec3_Length(b->surface.craniumRadii)*inverseB);
}

static const AnatomyNode *AxialStation(const AnatomyGraph *graph, unsigned index)
{
    for (size_t i=0; i<graph->nodeCount; ++i) {
        const AnatomyNode *node = &graph->nodes[i];
        if (node->localNodeId == index &&
            (node->region == ANATOMY_REGION_NECK || node->region == ANATOMY_REGION_TRUNK ||
             node->region == ANATOMY_REGION_PELVIS)) return node;
    }
    return NULL;
}

static bool AxialSamples(const AnatomyGraph *graph, float scale, Vector3 points[6], Vector3 sections[6])
{
    const AnatomyNode *anchor = AxialStation(graph,AXIAL_NODE_PECTORAL);
    if (!anchor) return false;
    for (unsigned i=0; i<6; ++i) {
        const AnatomyNode *node = AxialStation(graph,i+1);
        if (!node) return false;
        points[i] = Vec3_Scale(Vec3_Sub(node->center,anchor->center),1/scale);
        sections[i] = Vec3_Create(node->widthRadius/scale,node->heightRadius/scale,0);
    }
    return true;
}

static bool RegionalAxialSamples(const Monster parents[2], const OffspringReport *report,
    Vector3 points[6], Vector3 sections[6])
{
    Vector3 parentPoints[2][6], parentSections[2][6];
    for (unsigned p=0; p<2; ++p)
        if (!AxialSamples(&parents[p].anatomyGraph,parents[p].phenotype.axial.totalScale,
                parentPoints[p],parentSections[p])) return false;
    const float weights[] = {report->neck,report->shoulder,report->thorax,
        report->abdomen,report->abdomen,report->pelvis};
    for (size_t i=0; i<6; ++i) {
        points[i]=Vec3_Lerp(parentPoints[0][i],parentPoints[1][i],weights[i]);
        sections[i]=Vec3_Lerp(parentSections[0][i],parentSections[1][i],weights[i]);
    }
    return true;
}

static void FitAxial(CreatureRecipe *recipe, const Monster *target,
    const Monster parents[2], const OffspringReport *report)
{
    /* No mezclar estaciones 0..5 entre arquitecturas axiales incompatibles
     * (p. ej. manto cefalópodo vs tórax tetrápodo o segmentos de artrópodo). */
    if (!AxialArchetypesCompatible(parents[0].phenotype.axial.archetype,
            parents[1].phenotype.axial.archetype))
        return;
    Vector3 targetPoints[6], targetSections[6];
    if (!RegionalAxialSamples(parents,report,targetPoints,targetSections) &&
        !AxialSamples(&target->anatomyGraph,target->phenotype.axial.totalScale,targetPoints,targetSections))
        return;
    AxialPhenotype *adult = &recipe->adult.axial;
    AnatomyGraph graph = {0};
    AttachmentSlotSet slots = {0};
    Vector3 points[6], sections[6];
    if (!AxialBody_Resolve(adult,1,&graph,&slots) ||
        !AxialSamples(&graph,adult->totalScale,points,sections)) return;
    AxialPhenotype *stages[] = {&recipe->seed.axial,&recipe->larva.axial,&recipe->juvenile.axial,adult};
    for (unsigned i=0; i<6; ++i) {
        float width = Math_Clamp(targetSections[i].x/sections[i].x,.63f,1.58f);
        float height = Math_Clamp(targetSections[i].y/sections[i].y,.63f,1.58f);
        float length = adult->trunkLength;
        float dz = Math_Clamp((targetPoints[i].z-points[i].z)/length,-.075f,.075f);
        float dy = Math_Clamp((targetPoints[i].y-points[i].y)/length,-.10f,.10f);
        /* Un ajuste parcial conserva espacio para el anclaje de extremidades
         * y la alineación de las estaciones durante el desarrollo. */
        for (unsigned j=0; j<4; ++j) {
            AxialPhenotype *stage = stages[j];
            AxialStationMorph *station = &stage->profile[i];
            station->widthScale *= Math_Lerp(1,width,.73f);
            station->heightScale *= Math_Lerp(1,height,.73f);
            station->longitudinalOffset += dz*stage->trunkLength*.66f;
            station->verticalOffset += dy*stage->trunkLength*.62f;
            if (stage->archetype == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD && i>0)
                station->verticalOffset += sections[i].y*(Math_Lerp(1,height,.73f)-1)*
                    stage->trunkLength/length;
        }
    }
}

typedef struct LimbShape {
    float upper, lower, lateral, rootRadius, middleRadius, distalRadius;
} LimbShape;

static const CreatureModuleInstance *FindLimb(const CreatureRecipe *recipe, const CreaturePhenotype *p,
    LimbRole role, LimbSide side)
{
    const CreatureModuleInstance *result = NULL;
    for (size_t i=0; i<recipe->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance *m = &recipe->bodyPlan.modules[i];
        if (m->kind != CREATURE_MODULE_LIMB || m->pathAttached || m->phenotypeIndex>=p->limbCount) continue;
        const LimbPhenotype *l = &p->limbs[m->phenotypeIndex];
        if (l->role != role || l->side != side) continue;
        if (result) return NULL;
        result = m;
    }
    return result;
}

static bool HasMarineFlippers(const CreaturePhenotype *p)
{
    if (!p || !p->limbCount) return false;
    for (size_t i = 0; i < p->limbCount; ++i) {
        if (p->limbs[i].archetype == LIMB_ARCHETYPE_FLIPPER ||
            p->limbs[i].anatomyProfile == LIMB_PROFILE_MARINE_FLIPPER)
            return true;
    }
    return false;
}

static bool ReadLimb(const AnatomyGraph *g, const CreatureModuleInstance *m, float scale, LimbShape *out)
{
    if (!m) return false;
    const AnatomyNode *root = AnatomyGraph_FindModuleNode(g,m->instanceId,LIMB_NODE_ROOT);
    const AnatomyNode *middle = AnatomyGraph_FindModuleNode(g,m->instanceId,LIMB_NODE_MIDDLE);
    const AnatomyNode *distal = AnatomyGraph_FindModuleNode(g,m->instanceId,LIMB_NODE_DISTAL);
    const AnatomyNode *foot = AnatomyGraph_FindModuleNode(g,m->instanceId,LIMB_NODE_AUTOPOD);
    if (!root || !middle || !distal || !foot) return false;
    float reach = root->center.y-foot->center.y;
    if (reach < scale*.06f) return false;
    *out = (LimbShape){
        (root->center.y-middle->center.y)/reach,
        (root->center.y-distal->center.y)/reach,
        fabsf(root->center.x-foot->center.x)/reach,
        root->widthRadius/scale,middle->widthRadius/scale,distal->widthRadius/scale
    };
    return true;
}

static float LimbCost(LimbShape a, LimbShape b)
{
    return fabsf(a.upper-b.upper)+fabsf(a.lower-b.lower)+.55f*fabsf(a.lateral-b.lateral)+
        .85f*(fabsf(a.rootRadius-b.rootRadius)+fabsf(a.middleRadius-b.middleRadius)+
               fabsf(a.distalRadius-b.distalRadius));
}

static float BoundedGain(float current, float target)
{
    return current>.01f ? Math_Lerp(1,Math_Clamp(target/current,.72f,1.36f),.63f) : 1;
}

static void FitLimbs(CreatureRecipe *recipe, const Monster parents[2], OffspringReport *report)
{
    AnatomyGraph *graph = malloc(sizeof(*graph));
    if (!graph) return;
    CreaturePhenotype *adult = &recipe->adult;
    if (!Creature_ResolveAnatomy(recipe,adult,graph)) { free(graph); return; }
    LimbPhenotype previous[CREATURE_MAX_LIMBS];
    LimbShape wanted[CREATURE_MAX_LIMBS], original[CREATURE_MAX_LIMBS];
    bool fitted[CREATURE_MAX_LIMBS] = {0};
    for (size_t i=0; i<adult->limbCount; ++i) {
        LimbPhenotype *l = &adult->limbs[i];
        if (l->archetype == LIMB_ARCHETYPE_FLIPPER || l->anatomyProfile == LIMB_PROFILE_MARINE_FLIPPER)
            continue;
        const CreatureModuleInstance *own = FindLimb(recipe,adult,l->role,l->side);
        const CreatureModuleInstance *a = FindLimb(&parents[0].recipe,&parents[0].phenotype,l->role,l->side);
        const CreatureModuleInstance *b = FindLimb(&parents[1].recipe,&parents[1].phenotype,l->role,l->side);
        if (!a || !b ||
            parents[0].phenotype.limbs[a->phenotypeIndex].archetype == LIMB_ARCHETYPE_FLIPPER ||
            parents[1].phenotype.limbs[b->phenotypeIndex].archetype == LIMB_ARCHETYPE_FLIPPER)
            continue;
        LimbShape first, second;
        if (!ReadLimb(graph,own,adult->axial.totalScale,&original[i]) ||
            !ReadLimb(&parents[0].anatomyGraph,a,parents[0].phenotype.axial.totalScale,&first) ||
            !ReadLimb(&parents[1].anatomyGraph,b,parents[1].phenotype.axial.totalScale,&second)) continue;
#define TARGET(field,weight) wanted[i].field = Math_Lerp(first.field,second.field,weight)
        TARGET(upper,report->upperLimb); TARGET(lower,report->lowerLimb);
        TARGET(lateral,report->limbs);
        TARGET(rootRadius,report->limbMass); TARGET(middleRadius,report->limbMass);
        TARGET(distalRadius,report->limbMass);
#undef TARGET
        LimbShape x=original[i], y=wanted[i];
        previous[i]=*l;
        l->proximalScale *= BoundedGain(x.upper,y.upper);
        l->middleScale *= BoundedGain(x.lower-x.upper,y.lower-y.upper);
        l->distalScale *= BoundedGain(1-x.lower,1-y.lower);
        l->sprawl = Math_Clamp01(l->sprawl*BoundedGain(x.lateral,y.lateral));
        l->rootThicknessScale *= BoundedGain(x.rootRadius,y.rootRadius);
        l->middleThicknessScale *= BoundedGain(x.middleRadius,y.middleRadius);
        l->distalThicknessScale *= BoundedGain(x.distalRadius,y.distalRadius);
        fitted[i]=true;
    }
    bool valid=Creature_ResolveAnatomy(recipe,adult,graph);
    ++report->fitEvaluations;
    CreaturePhenotype *young[] = {&recipe->seed,&recipe->larva,&recipe->juvenile};
    for (size_t i=0; i<adult->limbCount; ++i) {
        if (!fitted[i]) continue;
        LimbPhenotype *l=&adult->limbs[i];
        const CreatureModuleInstance *module=FindLimb(recipe,adult,l->role,l->side);
        LimbShape trial;
        if (!valid || !ReadLimb(graph,module,adult->axial.totalScale,&trial) ||
            LimbCost(trial,wanted[i])+1e-6f>=LimbCost(original[i],wanted[i])) { *l=previous[i]; continue; }
        for (unsigned j=0; j<3; ++j) {
            LimbPhenotype *stage=&young[j]->limbs[i];
#define APPLY(field) stage->field *= l->field/previous[i].field
            APPLY(proximalScale); APPLY(middleScale); APPLY(distalScale); APPLY(rootThicknessScale);
            APPLY(middleThicknessScale); APPLY(distalThicknessScale);
#undef APPLY
            stage->sprawl=Math_Clamp01(stage->sprawl+l->sprawl-previous[i].sprawl);
        }
    }
    free(graph);
}

static void FitStance(CreatureRecipe *recipe,const Monster *target,const Monster parents[2])
{
    if (recipe->adult.axial.archetype != AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD ||
        parents[0].phenotype.axial.archetype == parents[1].phenotype.axial.archetype ||
        !AxialArchetypesCompatible(parents[0].phenotype.axial.archetype,
            parents[1].phenotype.axial.archetype))
        return;
    const AnatomyNode *shoulder=AxialStation(&target->anatomyGraph,AXIAL_NODE_PECTORAL);
    if (!shoulder) return;
    float height=(shoulder->center.y+shoulder->heightRadius)/target->phenotype.axial.totalScale;
    /* Conserva apoyo erguido y parte de su alcance, pero acerca la altura
     * del torso a la referencia al cruzar una postura baja con una erguida. */
    float gain=Math_Lerp(1,Math_Clamp(height/recipe->adult.axial.withersHeight,.65f,1.05f),.73f);
    CreaturePhenotype *stages[]={&recipe->seed,&recipe->larva,&recipe->juvenile,&recipe->adult};
    for (unsigned i=0; i<4; ++i) {
        CreaturePhenotype *p=stages[i];
        p->axial.withersHeight*=gain;
        for (size_t j=0; j<p->limbCount; ++j) p->limbs[j].length*=gain;
    }
}

static float HeadObjective(const CreaturePhenotype *p, Vector3 factor,
    const Monster *target, Vector3 initial)
{
    float scale = p->axial.totalScale;
    HeadAnatomy h;
    if (!HeadAnatomy_ResolveHybrid(&p->head,&p->headHybrid,0,Vec3_Scale(factor,scale),&h))
        return INFINITY;
    return OffspringMorph_HeadError(&h,scale,&target->head.anatomy,target->phenotype.axial.totalScale)
        + .055f*Vec3_Distance(factor,initial);
}

void OffspringMorph_Fit(CreatureRecipe *recipe, const Monster *target, const Monster parents[2],
    OffspringReport *report)
{
    FitStance(recipe,target,parents);
    FitAxial(recipe,target,parents,report);
    FitLimbs(recipe,parents,report);
    CreaturePhenotype *adult = &recipe->adult;
    if (!adult->headHybrid.active || adult->headHybrid.hasRegionalProfile || !target->hasHead) return;
    HeadEnvelopePhenotype *envelope = &adult->headEnvelope;
    Vector3 initial = {envelope->widthScale,envelope->heightScale,envelope->lengthScale};
    Vector3 factor = initial;
    float best = HeadObjective(adult,factor,target,initial);
    for (float step=.10f; step>.011f; step*=.5f) {
        for (unsigned axis=0; axis<3; ++axis) {
            for (int direction=-1; direction<=1; direction+=2) {
                Vector3 trial = factor;
                float *value = axis == 0 ? &trial.x : axis == 1 ? &trial.y : &trial.z;
                *value = Math_Clamp(*value + direction*step,.75f,1.28f);
                float cost = HeadObjective(adult,trial,target,initial);
                ++report->fitEvaluations;
                if (cost < best) { factor = trial; best = cost; }
            }
        }
    }
    CreaturePhenotype *stages[] = {&recipe->seed,&recipe->larva,&recipe->juvenile,adult};
    for (unsigned i=0; i<4; ++i) {
        HeadEnvelopePhenotype *e = &stages[i]->headEnvelope;
        e->widthScale *= factor.x/initial.x;
        e->heightScale *= factor.y/initial.y;
        e->lengthScale *= factor.z/initial.z;
    }
}

static float SignatureError(MorphologicalSignature a, MorphologicalSignature b)
{
#define REL(field) fabsf(a.field-b.field)/fmaxf(.1f,fmaxf(fabsf(a.field),fabsf(b.field)))
    return (REL(forelimbBodyRatio)+REL(hindlimbBodyRatio)+REL(footBodyRatio)+
        REL(forelimbThicknessBodyRatio))*.25f;
#undef REL
}

static float AxialError(const Vector3 pa[6],const Vector3 pb[6],
    const Vector3 ra[6],const Vector3 rb[6])
{
    float sum=0;
    for (size_t i=0; i<6; ++i)
        sum+=Vec3_LengthSq(Vec3_Sub(pa[i],pb[i]))+Vec3_LengthSq(Vec3_Sub(ra[i],rb[i]));
    return sqrtf(sum/12)/fmaxf(.001f,Vec3_Length(Vec3_Sub(pb[0],pb[5])));
}

static float SemanticHeadPenalty(const Monster *candidate, const OffspringReport *report)
{
    if (!candidate->hasHead) return 0.0f;
    const HeadAnatomy *h = &candidate->head.anatomy;
    const HeadSurfaceRecipe *s = &h->surface;
    float penalty = 0.0f;
    float craniumR = fmaxf(0.001f, fmaxf(s->craniumRadii.x, s->craniumRadii.y));
    /* Colapso o pérdida de pico aviar cuando el arquetipo/estructura lo requiere. */
    if (h->archetype == HEAD_ARCHETYPE_AVIAN || s->isBeak) {
        float culmen = s->beakTip.z - s->beakBase.z;
        if (s->beakWeight < 0.5f) penalty += 0.65f;
        else if (culmen < s->craniumRadii.z * 0.35f) penalty += 0.45f;
        else if (culmen > s->craniumRadii.z * 2.40f) penalty += 0.35f;
    }
    /* Alargamiento cefálico extremo respecto a la bóveda. */
    float faceLen = s->faceTip.z - s->craniumCenter.z;
    if (faceLen > s->craniumRadii.z * 4.8f)
        penalty += Math_Clamp01((faceLen / s->craniumRadii.z - 4.8f) * 0.25f) * 0.45f;
    /* Ojos desproporcionados en cráneos no artrópodos. */
    if (h->archetype != HEAD_ARCHETYPE_ARTHROPOD) {
        float eyeRatio = fmaxf(h->eyeScale.x, h->eyeScale.y) / craniumR;
        if (eyeRatio > 0.60f)
            penalty += Math_Clamp01((eyeRatio - 0.60f) * 2.0f) * 0.40f;
    }
    /* Penetración o baja exposición auricular. */
    if (s->hasEars && report && report->earExposedFraction < 0.75f)
        penalty += (0.75f - report->earExposedFraction) * 0.90f;
    return penalty;
}

void OffspringMorph_MeasureCandidate(const Monster *candidate, const Monster *reference,
    const Monster parents[2], OffspringReport *report)
{
    OffspringMorph_Measure(candidate,reference,report);
    if (AxialArchetypesCompatible(parents[0].phenotype.axial.archetype,
            parents[1].phenotype.axial.archetype)) {
        Vector3 pa[6],pb[6],ra[6],rb[6];
        if (AxialSamples(&candidate->anatomyGraph,candidate->phenotype.axial.totalScale,pa,ra) &&
            RegionalAxialSamples(parents,report,pb,rb)) {
            report->bodyError=AxialError(pa,pb,ra,rb);
            report->bodyErrorAvailable=true;
        }
    }
    bool sameLimbs = parents[0].phenotype.limbCount > 0 && parents[1].phenotype.limbCount > 0 &&
        candidate->phenotype.limbCount > 0;
    if (sameLimbs) {
        bool flipA = HasMarineFlippers(&parents[0].phenotype);
        bool flipB = HasMarineFlippers(&parents[1].phenotype);
        bool flipCand = HasMarineFlippers(&candidate->phenotype);
        MorphologicalSignature a=Creature_MorphologicalSignature(&parents[0]);
        MorphologicalSignature b=Creature_MorphologicalSignature(&parents[1]);
        if (flipA != flipB) {
            /* Modo funcional diferenciado: una aleta marina no se evalúa como una pata terrestre.
             * Medir respecto al progenitor funcionalmente homólogo con influencia acotada del donante. */
            const MorphologicalSignature *primary = (flipCand == flipA) ? &a : &b;
            const MorphologicalSignature *secondary = (flipCand == flipA) ? &b : &a;
            float w = 0.22f * ((flipCand == flipA) ? report->limbs : (1.0f - report->limbs));
            MorphologicalSignature wanted = {
                .forelimbBodyRatio = Math_Lerp(primary->forelimbBodyRatio, secondary->forelimbBodyRatio, w),
                .hindlimbBodyRatio = (primary->hindlimbBodyRatio > 0.01f && secondary->hindlimbBodyRatio > 0.01f)
                    ? Math_Lerp(primary->hindlimbBodyRatio, secondary->hindlimbBodyRatio, w)
                    : primary->hindlimbBodyRatio,
                .footBodyRatio = Math_Lerp(primary->footBodyRatio, secondary->footBodyRatio, w * 0.5f),
                .forelimbThicknessBodyRatio = Math_Lerp(primary->forelimbThicknessBodyRatio,
                    secondary->forelimbThicknessBodyRatio, w)
            };
            report->limbError = SignatureError(report->signature, wanted);
            report->limbErrorAvailable = true;
        } else {
            MorphologicalSignature wanted={
                .forelimbBodyRatio=Math_Lerp(a.forelimbBodyRatio,b.forelimbBodyRatio,report->limbs),
                .hindlimbBodyRatio=Math_Lerp(a.hindlimbBodyRatio,b.hindlimbBodyRatio,report->limbs),
                .footBodyRatio=Math_Lerp(a.footBodyRatio,b.footBodyRatio,report->foot),
                .forelimbThicknessBodyRatio=Math_Lerp(a.forelimbThicknessBodyRatio,b.forelimbThicknessBodyRatio,report->limbMass)
            };
            report->limbError=SignatureError(report->signature,wanted);
            report->limbErrorAvailable=true;
        }
    } else {
        report->limbError=0.0f;
        report->limbErrorAvailable=false;
    }
    float headPenalty = SemanticHeadPenalty(candidate, report);
    if (report->headErrorAvailable) {
        report->headError += headPenalty;
        report->headQuality = 1.0f / (1.0f + 2.0f * report->headError);
    } else if (candidate->hasHead) {
        report->headError = headPenalty;
        report->headQuality = 1.0f / (1.0f + 2.0f * headPenalty);
    } else {
        report->headError = 0.0f;
        report->headQuality = -1.0f;
    }
    report->bodyQuality = report->bodyErrorAvailable ? 1.0f / (1.0f + 1.5f * report->bodyError) : -1.0f;
    report->limbQuality = report->limbErrorAvailable ? 1.0f / (1.0f + 1.2f * report->limbError) : -1.0f;
}

void OffspringMorph_Measure(const Monster *candidate, const Monster *target, OffspringReport *report)
{
    float sa = candidate->phenotype.axial.totalScale, sb = target->phenotype.axial.totalScale;
    report->headErrorAvailable = candidate->hasHead && target->hasHead;
    if (report->headErrorAvailable)
        report->headError = OffspringMorph_HeadError(&candidate->head.anatomy,sa,&target->head.anatomy,sb);
    else
        report->headError = 0.0f;
    Vector3 pa[6], pb[6], ra[6], rb[6];
    report->bodyErrorAvailable = AxialSamples(&candidate->anatomyGraph,sa,pa,ra) &&
        AxialSamples(&target->anatomyGraph,sb,pb,rb);
    if (report->bodyErrorAvailable)
        report->bodyError=AxialError(pa,pb,ra,rb);
    else
        report->bodyError=0.0f;
    report->limbErrorAvailable = candidate->phenotype.limbCount > 0 && target->phenotype.limbCount > 0;
    report->limbError = report->limbErrorAvailable ?
        SignatureError(report->signature,Creature_MorphologicalSignature(target)) : 0.0f;
}
