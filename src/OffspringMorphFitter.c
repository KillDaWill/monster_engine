#include "OffspringMorphFitter.h"
#include "AxialBody.h"
#include "Limb.h"
#include "MathUtils.h"
#include <math.h>
#include <stdlib.h>

float OffspringMorph_HeadError(const HeadAnatomy *a, float scale,
    const HeadAnatomy *b, float targetScale)
{
    Vector3 neckA = a->landmarks.neckAttachment, neckB = b->landmarks.neckAttachment;
    float sum = 0, inverseA = 1/scale, inverseB = 1/targetScale;
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
#undef POINT
#undef SIZE
    return sqrtf(sum/18)/fmaxf(.001f,Vec3_Length(b->surface.craniumRadii)*inverseB);
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

static void FitAxial(CreatureRecipe *recipe, const Monster *target)
{
    Vector3 targetPoints[6], targetSections[6];
    if (!AxialSamples(&target->anatomyGraph,target->phenotype.axial.totalScale,targetPoints,targetSections))
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

static void FitLimbs(CreatureRecipe *recipe, const Monster parents[2], float weight, OffspringReport *report)
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
        const CreatureModuleInstance *own = FindLimb(recipe,adult,l->role,l->side);
        const CreatureModuleInstance *a = FindLimb(&parents[0].recipe,&parents[0].phenotype,l->role,l->side);
        const CreatureModuleInstance *b = FindLimb(&parents[1].recipe,&parents[1].phenotype,l->role,l->side);
        LimbShape first, second;
        if (!ReadLimb(graph,own,adult->axial.totalScale,&original[i]) ||
            !ReadLimb(&parents[0].anatomyGraph,a,parents[0].phenotype.axial.totalScale,&first) ||
            !ReadLimb(&parents[1].anatomyGraph,b,parents[1].phenotype.axial.totalScale,&second)) continue;
#define TARGET(field) wanted[i].field = Math_Lerp(first.field,second.field,weight)
        TARGET(upper); TARGET(lower); TARGET(lateral);
        TARGET(rootRadius); TARGET(middleRadius); TARGET(distalRadius);
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
            LimbCost(trial,wanted[i])>=LimbCost(original[i],wanted[i])) { *l=previous[i]; continue; }
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
        parents[0].phenotype.axial.archetype == parents[1].phenotype.axial.archetype)
        return;
    const AnatomyNode *shoulder=AxialStation(&target->anatomyGraph,AXIAL_NODE_PECTORAL);
    if (!shoulder) return;
    float height=(shoulder->center.y+shoulder->heightRadius)/target->phenotype.axial.totalScale;
    /* Conserva apoyo erguido y parte de su alcance, pero acerca la altura
     * del torso a la referencia al cruzar una postura baja con una erguida. */
    float gain=Math_Lerp(1,Math_Clamp(height/recipe->adult.axial.withersHeight,.58f,1.05f),.73f);
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
    FitAxial(recipe,target);
    FitLimbs(recipe,parents,report->targetWeight,report);
    CreaturePhenotype *adult = &recipe->adult;
    if (!adult->headHybrid.active || !target->hasHead) return;
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

void OffspringMorph_Measure(const Monster *candidate, const Monster *target, OffspringReport *report)
{
    float sa = candidate->phenotype.axial.totalScale, sb = target->phenotype.axial.totalScale;
    if (candidate->hasHead && target->hasHead)
        report->headError = OffspringMorph_HeadError(&candidate->head.anatomy,sa,&target->head.anatomy,sb);
    Vector3 pa[6], pb[6], ra[6], rb[6];
    if (AxialSamples(&candidate->anatomyGraph,sa,pa,ra) &&
        AxialSamples(&target->anatomyGraph,sb,pb,rb)) {
        float sum = 0;
        for (size_t i=0; i<6; ++i)
            sum += Vec3_LengthSq(Vec3_Sub(pa[i],pb[i]))+Vec3_LengthSq(Vec3_Sub(ra[i],rb[i]));
        report->bodyError = sqrtf(sum/12)/fmaxf(.001f,Vec3_Length(Vec3_Sub(pb[0],pb[5])));
    }
    report->limbError = SignatureError(report->signature,Creature_MorphologicalSignature(target));
}
