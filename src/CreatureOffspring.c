#include "CreatureOffspring.h"
#include "CreatureOffspringInternal.h"
#include "CreatureGenome.h"
#include "CreatureMorph.h"
#include "OffspringMorphFitter.h"
#include "HeadModule.h"
#include "FlexibleAppendage.h"
#include "MathUtils.h"
#include "MonsterSDF.h"
#include "OffspringRepair.h"
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { HEAD_SAMPLES=5*5*8, SHAPE_FEATURES=17, APPEARANCE_FEATURES=4*6+8,
    MAX_PROPOSALS_PER_SLOT=12 };

typedef struct OffspringDescriptor {
    float headField[HEAD_SAMPLES];
    float shape[SHAPE_FEATURES];
    float appearance[APPEARANCE_FEATURES];
    bool hasHead;
} OffspringDescriptor;

typedef struct OffspringCandidate {
    CreatureRecipeDocument document;
    OffspringReport report;
    OffspringDescriptor descriptor;
    unsigned revision;
    size_t index;
    const CreatureOffspringBatch *owner;
} Candidate;

typedef struct ProposalPool {
    Candidate *items[MAX_PROPOSALS_PER_SLOT];
    unsigned count;
} ProposalPool;

struct CreatureOffspringBatch {
    CreatureOffspringConfig config;
    CreatureRecipeDocument parents[2];
    int matches[2][CREATURE_MAX_MODULES];
    AnatomyGraph anatomy[2][4];
    Monster adults[2];
    HeadAnatomy heads[2][4];
    AABB3D headFrame;
    Candidate *candidates[CREATURE_OFFSPRING_MAX];
};

typedef struct {
    OffspringReport report;
    float build, stance, caudal, tailCurve, limbMassGain, footGain;
    float light, chroma, frequency, contrast, sharpness, orientation, coverage;
    bool graft;
} OffspringGenome;

typedef struct {
    unsigned parent;
    unsigned module;
    int match;
} ModuleSource;

static uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    return x ^ (x >> 16);
}

CreatureOffspringConfig CreatureOffspring_Default(size_t count, uint32_t seed)
{
    return (CreatureOffspringConfig){
        .count=count,.seed=seed,.attempts=32,.repairs=6,
        .minimumContribution=.35f,.maximumContribution=.65f,
        .proposalsPerOffspring=6,.regionalVariance=1,.mutationIntensity=1
    };
}

static bool ValidConfig(CreatureOffspringConfig c)
{
    return c.count && c.count <= CREATURE_OFFSPRING_MAX && c.attempts && c.attempts <= 32 &&
        c.repairs <= 16 && c.minimumContribution >= 0 && c.maximumContribution <= 1 &&
        c.minimumContribution <= c.maximumContribution && c.proposalsPerOffspring <= MAX_PROPOSALS_PER_SLOT &&
        c.regionalVariance >= 0 && c.regionalVariance <= 2 &&
        c.mutationIntensity >= 0 && c.mutationIntensity <= 2;
}

static const CreatureModuleInstance *FindModule(const BodyPlan *plan, uint32_t id)
{
    for (size_t i=0; i<plan->moduleCount; ++i)
        if (plan->modules[i].instanceId == id) return &plan->modules[i];
    return NULL;
}

static AttachmentSlotId InheritedSlot(const CreatureModuleInstance *module)
{
    AttachmentSlotId slot = module->attachment.slotId;
    return slot >= HEAD_SLOT_FITTED_PROBOSCIS && slot <= HEAD_SLOT_FITTED_NASAL_POSTERIOR ?
        slot-(HEAD_SLOT_FITTED_PROBOSCIS-HEAD_SLOT_PROBOSCIS) : slot;
}

static const AnatomyNode *HostLandmark(const CreatureModuleInstance *module, const AnatomyGraph *graph)
{
    if (module->pathAttached || !module->attachment.hostModuleInstanceId) return NULL;
    uint32_t child = module->instanceId, host = module->attachment.hostModuleInstanceId;
    for (size_t i=0; i<graph->connectionCount; ++i) {
        const BodyConnection *edge = &graph->connections[i];
        if ((edge->toId >> 16) == child && (edge->fromId >> 16) == host)
            return AnatomyGraph_FindNode(graph,edge->fromId);
        if ((edge->fromId >> 16) == child && (edge->toId >> 16) == host)
            return AnatomyGraph_FindNode(graph,edge->toId);
    }
    return NULL;
}

/* Las ranuras son landmarks locales publicados por cada familia de anfitrión. */
static bool Equivalent(const CreatureRecipe *a, size_t i, const AnatomyNode *landmarkA,
    const CreatureRecipe *b, size_t j, const AnatomyNode *landmarkB)
{
    const CreatureModuleInstance *m = &a->bodyPlan.modules[i], *n = &b->bodyPlan.modules[j];
    if (m->kind != n->kind || m->pathAttached || n->pathAttached ||
        InheritedSlot(m) != InheritedSlot(n)) return false;
    if (m->kind == CREATURE_MODULE_AXIAL) return m->instanceId == a->bodyPlan.root.moduleInstanceId &&
        n->instanceId == b->bodyPlan.root.moduleInstanceId;
    if (!landmarkA || !landmarkB || landmarkA->region != landmarkB->region ||
        landmarkA->role != landmarkB->role || landmarkA->side != landmarkB->side)
        return false;
    const CreatureModuleInstance *hostA = FindModule(&a->bodyPlan,m->attachment.hostModuleInstanceId);
    const CreatureModuleInstance *hostB = FindModule(&b->bodyPlan,n->attachment.hostModuleInstanceId);
    if (!hostA || !hostB || hostA->kind != hostB->kind ||
        (hostA->kind != CREATURE_MODULE_HEAD && hostA->kind != CREATURE_MODULE_AXIAL))
        return false;
    if (m->kind != CREATURE_MODULE_LIMB) return true;
    const LimbPhenotype *l = &a->adult.limbs[m->phenotypeIndex], *r = &b->adult.limbs[n->phenotypeIndex];
    return l->role == r->role && l->side == r->side;
}

static void Match(CreatureOffspringBatch *batch)
{
    const CreatureRecipe *a = &batch->parents[0].recipe, *b = &batch->parents[1].recipe;
    const CreatureRecipe *parents[] = {a,b};
    const AnatomyNode *landmarks[2][CREATURE_MAX_MODULES] = {{0}};
    for (unsigned p=0; p<2; ++p) {
        for (size_t i=0; i<CREATURE_MAX_MODULES; ++i) batch->matches[p][i] = -1;
        for (size_t i=0; i<parents[p]->bodyPlan.moduleCount; ++i)
            landmarks[p][i] = HostLandmark(&parents[p]->bodyPlan.modules[i],
                &batch->anatomy[p][CREATURE_STAGE_ADULT]);
    }
    for (size_t i=0; i<a->bodyPlan.moduleCount; ++i) {
        int selected = -1;
        for (size_t j=0; j<b->bodyPlan.moduleCount; ++j) {
            if (batch->matches[1][j] >= 0 || !Equivalent(a,i,landmarks[0][i],b,j,landmarks[1][j])) continue;
            if (selected >= 0) { selected = -1; break; }
            selected = (int)j;
        }
        if (selected < 0) continue;
        unsigned reverse = 0;
        for (size_t j=0; j<a->bodyPlan.moduleCount; ++j) reverse += Equivalent(a,j,landmarks[0][j],b,(size_t)selected,landmarks[1][selected]);
        if (reverse != 1) continue;
        batch->matches[0][i] = selected;
        batch->matches[1][selected] = (int)i;
    }
}

static float Variation(uint32_t *random)
{
    return 2 * CreatureGenome_Random(random) - 1;
}

/* Dos muestras dan una distribución triangular acotada. Los hijos de un
 * sistema comparten su desplazamiento; las amplitudes locales son máximos. */
static float SampleTrait(float center, float amplitude, uint32_t *random)
{
    float first = Variation(random);
    return Math_Clamp01(center+amplitude*.5f*(first+Variation(random)));
}

static OffspringGenome Genome(const CreatureOffspringBatch *batch, size_t index,
    unsigned revision, unsigned attempt)
{
    CreatureOffspringConfig c = batch->config;
    uint32_t seed = Mix(c.seed ^ Mix((uint32_t)index+1) ^ Mix(revision+101) ^
        Mix(attempt+1009) ^ CREATURE_OFFSPRING_VERSION);
    uint32_t globalStream=Mix(seed^1), head=Mix(seed^2), body=Mix(seed^3), limb=Mix(seed^4);
    uint32_t surface=Mix(seed^5), structural=Mix(seed^6), mutation=Mix(seed^7);
    float band = (index + .5f + .35f * Variation(&globalStream)) / c.count;
    float global = Math_Lerp(c.minimumContribution,c.maximumContribution,band);
    OffspringGenome g = {.report={.seed=seed,.mutationSeed=mutation,.global=global,
        .attempts=attempt+1,.generatorVersion=CREATURE_OFFSPRING_VERSION}};
    OffspringReport *r = &g.report;
    r->targetWeight = CreatureMorph_GeometricWeight(global);
    float variance = c.regionalVariance, intensity = c.mutationIntensity;
#define SAMPLE(center, span, stream) SampleTrait(center,(span)*variance,&stream)
#define MUTATE(span) (1+(span)*intensity*Variation(&mutation))
    r->body = SAMPLE(r->targetWeight,.15f,body);
    r->head = SAMPLE(r->targetWeight,.17f,head);
    r->limbs = SAMPLE(r->targetWeight,.18f,limb);
    r->tail = SAMPLE(global,.34f,body);
    r->surface = SAMPLE(r->targetWeight,.32f,surface);
    r->size = MUTATE(.12f);
    r->scaffoldParent = (index+revision+attempt)%2;
    unsigned choices = (unsigned)(CreatureGenome_Random(&structural)*256);
    r->headParent = choices & 1;
    r->limbParent = (choices >> 1) & 1;
    r->tailParent = (choices >> 2) & 1;
    r->surfaceParent = r->surface >= .5f;
    HeadRegionalInheritance *h = &r->headGenes;
    h->cranium = SAMPLE(r->head,.42f,head);
    h->rostrum = SAMPLE(r->head,.52f,head);
    h->jaw = SAMPLE(Math_Lerp(r->head,h->rostrum,.42f),.41f,head);
    h->cheek = SAMPLE(Math_Lerp(h->cranium,h->jaw,.4f),.32f,head);
    h->orbit = SAMPLE(h->cranium,.33f,head);
    h->ear = SAMPLE(r->head,.68f,head);
    h->neck = SAMPLE(Math_Lerp(r->body,h->cranium,.4f),.24f,head);
    h->nose = SAMPLE(h->rostrum,.36f,head);
    h->earHeight = SAMPLE(h->ear,.38f,head);
    h->earWidth = SAMPLE(h->ear,.43f,head);
    h->earCurve = SAMPLE(h->ear,.38f,head);
    h->inheritEars = CreatureGenome_Random(&structural) >= h->ear;
    h->skullWidthGain = MUTATE(.12f);
    h->muzzleLengthGain = MUTATE(.17f);
    h->jawDepthGain = MUTATE(.16f);
    h->eyeSizeGain = MUTATE(.12f);
    h->earHeightGain = MUTATE(.19f);
    h->earWidthGain = MUTATE(.20f);
    h->earThicknessGain = MUTATE(.12f);
    h->earCurveGain = MUTATE(.20f);
    h->earAngle = .14f*intensity*Variation(&mutation);
    r->neck = SAMPLE(r->body,.26f,body);
    r->shoulder = SAMPLE(r->body,.28f,body);
    r->thorax = SAMPLE(r->shoulder,.24f,body);
    r->abdomen = SAMPLE(Math_Lerp(r->thorax,r->body,.55f),.30f,body);
    r->pelvis = SAMPLE(r->body,.30f,body);
    r->upperLimb = SAMPLE(r->limbs,.30f,limb);
    r->lowerLimb = SAMPLE(Math_Lerp(r->upperLimb,r->limbs,.6f),.30f,limb);
    r->foot = SAMPLE(r->lowerLimb,.24f,limb);
    r->limbMass = SAMPLE(r->limbs,.32f,limb);
    r->dorsal = SAMPLE(r->surface,.39f,surface);
    r->ventral = SAMPLE(r->surface,.46f,surface);
    r->facial = SAMPLE(r->dorsal,.40f,surface);
    r->extremities = SAMPLE(r->dorsal,.42f,surface);
    r->tailColor = SAMPLE(r->dorsal,.42f,surface);
    r->eyeColor = SAMPLE(r->facial,.52f,surface);
    r->pattern = Math_Clamp(SAMPLE(r->surface,.60f,surface),.04f,.96f);
    r->fur = SAMPLE(r->surface,.39f,surface);
    r->relief = SAMPLE(r->surface,.34f,surface);
    g.build = MUTATE(.13f);
    g.stance = MUTATE(.10f);
    g.caudal = MUTATE(.19f);
    g.tailCurve = MUTATE(.15f);
    g.limbMassGain = MUTATE(.12f);
    g.footGain = MUTATE(.09f);
    g.light = MUTATE(.10f);
    g.chroma = MUTATE(.10f);
    g.frequency = MUTATE(.25f);
    g.contrast = MUTATE(.22f);
    g.sharpness = .10f*intensity*Variation(&mutation);
    g.orientation = .16f*intensity*Variation(&mutation);
    g.coverage = MUTATE(.15f);
    h->nasalExtension = SAMPLE(Math_Lerp(.5f,h->nose,.24f),1.05f,head);
    g.graft = attempt%4 < 2;
#undef MUTATE
#undef SAMPLE
    return g;
}

static CreaturePhenotype *Stage(CreatureRecipe *r, unsigned stage)
{
    switch (stage) {
    case 0: return &r->seed;
    case 1: return &r->larva;
    case 2: return &r->juvenile;
    default: return &r->adult;
    }
}

static uint32_t MapModule(const CreatureOffspringBatch *batch, const BodyPlan *plan,
    const ModuleSource sources[], unsigned parent, uint32_t id)
{
    for (size_t i=0; i<plan->moduleCount; ++i) {
        ModuleSource s = sources[i];
        int index = s.parent == parent ? (int)s.module : s.match;
        if (index >= 0 && batch->parents[parent].recipe.bodyPlan.modules[index].instanceId == id)
            return plan->modules[i].instanceId;
    }
    return 0;
}

static AnatomyId MapNode(const CreatureOffspringBatch *batch, const BodyPlan *plan,
    const ModuleSource sources[], unsigned parent, AnatomyId id)
{
    return Anatomy_MakeId(MapModule(batch,plan,sources,parent,id >> 16),(uint16_t)id);
}

static void MapPigment(PigmentPhenotype *pigment, const CreatureOffspringBatch *batch,
    const BodyPlan *plan, const ModuleSource sources[], unsigned parent, OffspringReport *r)
{
    size_t count = 0;
    for (size_t i=0; i<pigment->layerCount; ++i) {
        PigmentLayer layer = pigment->layers[i];
        if (layer.domain.anchorNode) {
            layer.domain.anchorNode = MapNode(batch,plan,sources,parent,layer.domain.anchorNode);
            if (!layer.domain.anchorNode) { ++r->omitted; ++r->removedAnchors; continue; }
        }
        pigment->layers[count++] = layer;
    }
    pigment->layerCount = count;
}

/* El contrato del material interpreta RGB/255 como albedo lineal. La
 * mutación cambia luminancia y saturación en ese mismo espacio. */
static Color Palette(Color a, Color b, float weight, const OffspringGenome *g)
{
    Color c = Color_Lerp(a,b,weight);
    float gray = (.2126f*c.r+.7152f*c.g+.0722f*c.b)/255;
    return Color_FromNormalized(g->light*Math_Lerp(gray,c.r/255.f,g->chroma),
        g->light*Math_Lerp(gray,c.g/255.f,g->chroma),
        g->light*Math_Lerp(gray,c.b/255.f,g->chroma),c.a/255.f);
}

static void MutatePattern(PigmentPhenotype *pattern, const OffspringGenome *g)
{
    pattern->patternStrength = Math_Clamp01(pattern->patternStrength*g->contrast);
    pattern->patternScale *= g->frequency;
    for (size_t i=0; i<pattern->layerCount; ++i) {
        PigmentLayer *layer = &pattern->layers[i];
        layer->strength = Math_Clamp01(layer->strength*g->contrast);
        layer->scale *= g->frequency;
        layer->sharpness = Math_Clamp01(layer->sharpness+g->sharpness);
        layer->patternRotation = Math_Clamp(layer->patternRotation+g->orientation,-.6f,.6f);
        layer->domain.radii = Vec3_Scale(layer->domain.radii,g->coverage);
        layer->domain.irregularity = Math_Clamp01(layer->domain.irregularity*g->coverage);
    }
}

/* Un banco por progenitor. Se retienen el orden y la semilla del campo
 * dominante. Las capas no duplicadas del otro banco compiten por las plazas
 * sobrantes por opacidad efectiva, sin reordenar las capas dominantes. */
static PigmentPhenotype InheritBank(const SurfacePhenotype *source, OffspringReport *report)
{
    if (!source->inheritedBlend || !source->transition.active) return source->pigment;
    float weight = source->transition.weight;
    const PigmentPhenotype *major = weight < .5f ? &source->transition.from : &source->transition.to;
    const PigmentPhenotype *minor = weight < .5f ? &source->transition.to : &source->transition.from;
    PigmentPhenotype out = *major;
    out.baseColor = source->pigment.baseColor;
    out.ventralColor = source->pigment.ventralColor;
    out.secondaryColor = source->pigment.secondaryColor;
    float minorWeight = fminf(weight,1-weight);
    bool used[SURFACE_MAX_PIGMENT_LAYERS] = {0};
    for (size_t i=0; i<minor->layerCount; ++i) {
        const PigmentLayer *layer = &minor->layers[i];
        for (size_t j=0; j<out.layerCount; ++j) {
            const PigmentLayer *peer = &out.layers[j];
            used[i] |= layer->pattern==peer->pattern && layer->seed==peer->seed &&
                layer->regionMask==peer->regionMask && layer->domain.anchorNode==peer->domain.anchorNode;
        }
    }
    while (out.layerCount<SURFACE_MAX_PIGMENT_LAYERS) {
        size_t best = minor->layerCount;
        for (size_t i=0; i<minor->layerCount; ++i)
            if (!used[i] && (best==minor->layerCount ||
                minor->layers[i].strength>minor->layers[best].strength)) best=i;
        if (best==minor->layerCount) break;
        used[best]=true;
        PigmentLayer layer=minor->layers[best];
        layer.strength*=minorWeight;
        out.layers[out.layerCount++]=layer;
    }
    for (size_t i=0; i<minor->layerCount; ++i) report->omitted+=!used[i];
    return out;
}

static void InheritHair(SurfacePhenotype *surface, const SurfacePhenotype input[2], const OffspringGenome *g)
{
    surface->longHairCount=0;
    unsigned first=g->report.fur>=.5f;
    for (unsigned bank=0; bank<2; ++bank) {
        unsigned p=first^bank;
        float contribution=p ? g->report.fur : 1-g->report.fur;
        for (size_t i=0; i<input[p].longHairCount; ++i) {
            LongHairGroup group=input[p].longHair[i];
            size_t j=0;
            for (; j<surface->longHairCount; ++j)
                if (surface->longHair[j].id==group.id) break;
            if (j<surface->longHairCount) {
                LongHairGroup *old=&surface->longHair[j];
                if (old->domain.hostModuleId==group.domain.hostModuleId &&
                    old->domain.kind==group.domain.kind && old->domain.region==group.domain.region) {
                    old->density=Math_Clamp01(old->density+group.density*contribution);
                    old->length=Math_Lerp(old->length,group.length*g->coverage,contribution);
                    old->clumping=Math_Lerp(old->clumping,group.clumping,contribution);
                    old->rootColor=Color_Lerp(old->rootColor,group.rootColor,contribution);
                    old->tipColor=Color_Lerp(old->tipColor,group.tipColor,contribution);
                    continue;
                }
                uint32_t id=group.id;
                bool available;
                do {
                    id=id==UINT32_MAX ? 1 : id+1;
                    available=true;
                    for (size_t k=0; k<surface->longHairCount; ++k)
                        available &= surface->longHair[k].id!=id;
                } while (!available);
                group.id=id;
            }
            if (surface->longHairCount==LONG_HAIR_MAX_GROUPS) break;
            group.density*=contribution;
            group.length*=g->coverage;
            group.clumping=Math_Clamp01(group.clumping*g->contrast);
            surface->longHair[surface->longHairCount++]=group;
        }
    }
}

static Color RegionColor(const SurfacePhenotype *surface, unsigned region)
{
    Color fallback = region==SURFACE_REGION_VENTRAL_TRUNK ? surface->pigment.ventralColor :
        surface->pigment.baseColor;
    Color color=surface->regionalAlbedo[region];
    return Color_Lerp(fallback,color,color.a/255.f);
}

static void Surface(CreaturePhenotype *p, const CreaturePhenotype *a, const CreaturePhenotype *b,
    const CreatureOffspringBatch *batch, const BodyPlan *plan, const ModuleSource sources[],
    OffspringGenome *g)
{
    OffspringReport *r=&g->report;
    SurfacePhenotype input[] = {a->surface,b->surface};
    for (unsigned parent=0; parent<2; ++parent) {
        SurfacePhenotype *source = &input[parent];
        source->pigment=InheritBank(source,r);
        MapPigment(&source->pigment,batch,plan,sources,parent,r);
        MutatePattern(&source->pigment,g);
        const CreatureRecipe *donor=&batch->parents[parent].recipe;
        bool compatibleTail=false;
        for (size_t i=0; i<plan->moduleCount; ++i) {
            if (plan->modules[i].kind!=CREATURE_MODULE_TAIL) continue;
            ModuleSource inherited=sources[i];
            const CreatureRecipe *owner=&batch->parents[inherited.parent].recipe;
            const CreatureModuleInstance *tail=&owner->bodyPlan.modules[inherited.module];
            TailArchetype archetype=owner->adult.tails[tail->phenotypeIndex].archetype;
            for (size_t j=0; j<donor->adult.tailCount; ++j)
                compatibleTail|=donor->adult.tails[j].archetype==archetype;
        }
        /* Las recetas antiguas también describen mechones de cola por región,
         * sin un identificador de módulo que se pueda reasignar. */
        if (!compatibleTail) {
            size_t kept=0;
            for (size_t i=0; i<source->tuftCount; ++i)
                if (source->tufts[i].region!=SURFACE_REGION_TAIL)
                    source->tufts[kept++]=source->tufts[i];
            source->tuftCount=kept;
        }
        size_t count = 0;
        for (size_t i=0; i<source->longHairCount; ++i) {
            LongHairGroup hair = source->longHair[i];
            LongHairDomain *d = &hair.domain;
            const CreatureModuleInstance *oldHost=FindModule(&donor->bodyPlan,d->hostModuleId);
            d->hostModuleId = MapModule(batch,plan,sources,parent,d->hostModuleId);
            bool valid = d->hostModuleId != 0;
            if(valid && oldHost && oldHost->kind==CREATURE_MODULE_TAIL) {
                const CreatureModuleInstance *host=FindModule(plan,d->hostModuleId);
                if(host) {
                    ModuleSource inherited=sources[host-plan->modules];
                    const CreatureRecipe *owner=&batch->parents[inherited.parent].recipe;
                    const CreatureModuleInstance *tail=&owner->bodyPlan.modules[inherited.module];
                    valid=donor->adult.tails[oldHost->phenotypeIndex].archetype==
                        owner->adult.tails[tail->phenotypeIndex].archetype;
                }
            }
            for (size_t j=0; j<d->anchorCount; ++j) {
                d->anchors[j] = MapNode(batch,plan,sources,parent,d->anchors[j]);
                valid &= d->anchors[j] != 0;
            }
            if (!valid) { ++r->omitted; ++r->removedAnchors; continue; }
            source->longHair[count++] = hair;
        }
        source->longHairCount = count;
    }
    p->surface = SurfacePhenotype_Interpolate(&input[0],&input[1],r->pattern);
    SurfacePhenotype *surface = &p->surface;
    surface->inheritedBlend = surface->transition.active;
    surface->pigment = input[r->surfaceParent].pigment;
    surface->pigment.baseColor = Palette(input[0].pigment.baseColor,input[1].pigment.baseColor,r->dorsal,g);
    surface->pigment.ventralColor = Palette(input[0].pigment.ventralColor,input[1].pigment.ventralColor,r->ventral,g);
    surface->pigment.secondaryColor = Palette(input[0].pigment.secondaryColor,input[1].pigment.secondaryColor,r->surface,g);
    const float weights[] = {r->dorsal,r->facial,(r->dorsal+r->facial)*.5f,r->dorsal,r->ventral,
        r->extremities,r->extremities,r->tailColor,r->extremities,r->facial,r->extremities};
    for (unsigned i=0; i<SURFACE_REGION_COUNT; ++i) {
        Color color=Palette(RegionColor(&input[0],i),RegionColor(&input[1],i),weights[i],g);
        color.a=255;
        surface->regionalAlbedo[i]=color;
    }
    surface->transition.from.ventralColor=surface->transition.to.ventralColor=surface->pigment.ventralColor;
    const IntegumentPhenotype *ia = &input[0].integument, *ib = &input[1].integument;
    float furA = ia->type == INTEGUMENT_FUR ? ia->coverage : 0;
    float furB = ib->type == INTEGUMENT_FUR ? ib->coverage : 0;
    bool scaleA = ia->type == INTEGUMENT_SCALES || ia->type == INTEGUMENT_PLATES;
    bool scaleB = ib->type == INTEGUMENT_SCALES || ib->type == INTEGUMENT_PLATES;
    if (scaleA != scaleB) {
        const IntegumentPhenotype *substrate = scaleA ? ia : ib;
        surface->integument.scales = substrate->scales;
        surface->integument.plates = substrate->plates;
        surface->transition.targetScaleSeed = substrate->scales.seed;
    }
    surface->transition.scaleCoverage=Math_Lerp(scaleA ? ia->coverage : input[0].transition.scaleCoverage,
        scaleB ? ib->coverage : input[1].transition.scaleCoverage,r->relief);
    surface->integument.scales.size*=g->frequency;
    surface->integument.scales.relief*=g->coverage;
    float furCoverage = Math_Lerp(furA,furB,r->fur);
    if (furCoverage > 0) {
        surface->integument.type = INTEGUMENT_FUR;
        surface->integument.coverage = Math_Clamp01(furCoverage*g->coverage);
        if (!furA) surface->integument.fur = ib->fur;
        if (!furB) surface->integument.fur = ia->fur;
        if (furA && furB) {
            surface->integument.fur.length=Math_Lerp(ia->fur.length,ib->fur.length,r->fur);
            surface->integument.fur.density=Math_Lerp(ia->fur.density,ib->fur.density,r->fur);
            surface->integument.fur.thickness=Math_Lerp(ia->fur.thickness,ib->fur.thickness,r->fur);
        }
        surface->integument.fur.length*=g->coverage;
        surface->integument.fur.density*=g->contrast;
        surface->integument.fur.thickness*=g->frequency;
    } else {
        surface->integument.type = input[r->surfaceParent].integument.type;
    }
    for (unsigned i=0; i<SURFACE_REGION_COUNT; ++i) {
        float w=weights[i];
        float adjustment=1+.45f*(w-r->fur);
        float coverage = Math_Lerp(furA,furB,w);
        surface->furRegions[i].densityMultiplier *= furCoverage>0 ? coverage/furCoverage : 1;
        surface->furRegions[i].lengthMultiplier *= adjustment;
        surface->regions[i].roughness *= 1+.38f*(w-r->relief);
        surface->regions[i].relief *= 1+.45f*(w-r->relief);
    }
    InheritHair(surface,input,g);
    p->dorsalColor = Palette(a->dorsalColor,b->dorsalColor,r->dorsal,g);
    p->ventralColor = Palette(a->ventralColor,b->ventralColor,r->ventral,g);
    p->unpigmentedVentralColor = Palette(a->unpigmentedVentralColor,b->unpigmentedVentralColor,r->ventral,g);
}

static void Axial(CreaturePhenotype *p, const CreaturePhenotype *a, const CreaturePhenotype *b,
    const OffspringGenome *g)
{
    const OffspringReport *r = &g->report;
    const AxialPhenotype *base = r->scaffoldParent ? &b->axial : &a->axial;
    const AxialPhenotype *other = r->scaffoldParent ? &a->axial : &b->axial;
    float w = r->scaffoldParent ? 1-r->body : r->body;
    bool compatible = base->archetype == other->archetype;
    float proportion = compatible ? w : .76f*w;
    float ratio = base->trunkLength / other->trunkLength;
    p->axial = *base;
    p->axial.totalScale = expf(Math_Lerp(logf(a->axial.totalScale),logf(b->axial.totalScale),r->global))*r->size;
    float length = Math_Lerp(base->trunkLength,other->trunkLength,proportion);
    p->axial.trunkLength = length;
#define LOCAL(weight) ((r->scaffoldParent ? 1-(weight) : (weight))*(compatible ? 1 : .76f))
#define PROPORTION(field, weight) p->axial.field = Math_Lerp(base->field,other->field*ratio, \
    LOCAL(weight))*length/base->trunkLength
    PROPORTION(neckLength,r->neck); PROPORTION(neckWidth,r->neck);
    PROPORTION(shoulderWidth,r->shoulder); PROPORTION(thoraxWidth,r->thorax);
    PROPORTION(abdomenWidth,r->abdomen); PROPORTION(pelvicWidth,r->pelvis);
#undef PROPORTION
    p->axial.thoraxWidth *= g->build;
    p->axial.abdomenWidth *= g->build;
    p->axial.pelvicWidth *= g->build;
    p->axial.shoulderWidth *= g->build;
    if (compatible && base->archetype == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD)
        p->axial.withersHeight = Math_Lerp(base->withersHeight,other->withersHeight,w);
    if (!compatible) {
        /* Conserva la cadena locomotora y ajusta en conjunto la altura
         * de apoyo y la longitud del torso en el marco del plan elegido. */
        float baseStance = base->withersHeight/base->trunkLength;
        float peerStance = other->withersHeight/other->trunkLength;
        float stance = Math_Lerp(baseStance,peerStance,.34f*w);
        p->axial.withersHeight = Math_Clamp(stance*length,
            base->withersHeight*.86f,base->withersHeight*1.14f);
    }
    p->axial.withersHeight *= g->stance;
    float heightRatio = base->withersHeight/other->withersHeight;
    float newHeightRatio = p->axial.withersHeight/base->withersHeight;
    /* Las alturas de sección se expresan respecto a la cruz del descendiente.
     * Usar la del plan base puede enterrar las patas en un torso más bajo. */
#define HEIGHT(field,weight) p->axial.field = Math_Lerp(base->field,other->field*heightRatio, \
    LOCAL(weight))*newHeightRatio
    HEIGHT(thoraxHeight,r->thorax); HEIGHT(abdomenHeight,r->abdomen); HEIGHT(pelvicHeight,r->pelvis);
#undef HEIGHT
    const float stationWeights[] = {r->neck,r->shoulder,r->thorax,r->abdomen,r->abdomen,r->pelvis};
    for (size_t i=0; i<6; ++i) {
        float local = LOCAL(stationWeights[i]);
        AxialStationMorph *station = &p->axial.profile[i];
        const AxialStationMorph *peer = &other->profile[i];
        station->widthScale = Math_Lerp(station->widthScale,peer->widthScale,local);
        station->heightScale = Math_Lerp(station->heightScale,peer->heightScale,local);
        station->verticalOffset = Math_Lerp(station->verticalOffset,peer->verticalOffset*ratio,local);
        station->longitudinalOffset = Math_Lerp(station->longitudinalOffset,peer->longitudinalOffset*ratio,local);
    }
#undef LOCAL
    p->development.pigmentation = Math_Lerp(a->development.pigmentation,b->development.pigmentation,r->surface);
    p->development.colorMaturity = Math_Lerp(a->development.colorMaturity,b->development.colorMaturity,r->surface);
    p->development.integument = Math_Lerp(a->development.integument,b->development.integument,r->surface);
}

static void HeadSystem(CreaturePhenotype *p, const CreaturePhenotype *a,
    const CreaturePhenotype *b, const CreatureOffspringBatch *batch, unsigned stage, OffspringGenome *g)
{
    OffspringReport *r = &g->report;
    const CreaturePhenotype *primary = r->headParent ? b : a;
    const CreaturePhenotype *secondary = r->headParent ? a : b;
    const AnatomyNode *head[2] = {
        AnatomyGraph_FindFirstRegion(&batch->anatomy[0][stage],ANATOMY_REGION_HEAD),
        AnatomyGraph_FindFirstRegion(&batch->anatomy[1][stage],ANATOMY_REGION_HEAD)
    };
    p->head = primary->head;
    p->headEnvelope = primary->headEnvelope;
    p->headHybrid = primary->headHybrid;
    p->eyes = primary->eyes;
    p->development.cephalic = primary->development.cephalic;
    if (!head[0] || !head[1]) return;
    float targetScale = Math_Lerp(a->axial.totalScale,b->axial.totalScale,r->targetWeight);
    float inverse = 1/targetScale;
    HeadAnatomy first = batch->heads[0][stage], second = batch->heads[1][stage];
    HeadAnatomy_ScaleProfile(&first,Vec3_Create(inverse,inverse,inverse));
    HeadAnatomy_ScaleProfile(&second,Vec3_Create(inverse,inverse,inverse));
    HeadRegionalInheritance genes = r->headGenes;
    p->development.cephalic = Math_Lerp(a->development.cephalic,b->development.cephalic,genes.cranium);
    genes.facialSeatStrength = Math_Clamp01((p->development.cephalic-.58f)/.42f);
    const HeadAnatomy *adultA = &batch->heads[0][CREATURE_STAGE_ADULT];
    const HeadAnatomy *adultB = &batch->heads[1][CREATURE_STAGE_ADULT];
    if (adultA->surface.hasEars && adultB->surface.hasEars) genes.inheritEars = true;
    else if (!adultA->surface.hasEars && adultB->surface.hasEars) genes.inheritEars = !genes.inheritEars;
    p->headEnvelope = (HeadEnvelopePhenotype){1,1,1,1};
    p->headHybrid = (HeadHybridProfile){
        .active=true,.blend=r->headParent ? 1-genes.cranium : genes.cranium,
        .primaryRadii={1,1,1},.secondaryRadii={1,1,1},.secondary=secondary->head,.hasRegionalProfile=true
    };
    HeadAnatomy *regional = &p->headHybrid.regional;
    HeadAnatomy_BlendRegional(&first,&second,&genes,regional);
    regional->archetype = primary->head.archetype;
    const HeadAnatomy *structure = r->headParent ? &second : &first;
    regional->surface.isBeak = structure->surface.isBeak;
    regional->oralSystem.shape = structure->oralSystem.shape;
    p->head.nasalColor = Palette(first.surface.nasalColor,second.surface.nasalColor,r->facial,g);
    regional->surface.nasalColor=p->head.nasalColor;
    regional->oralSystem.lipColor=Palette(first.oralSystem.lipColor,second.oralSystem.lipColor,r->facial,g);
    regional->oralSystem.insideColor=Palette(first.oralSystem.insideColor,second.oralSystem.insideColor,r->facial,g);
    r->resolvedHeadWeight = genes.cranium;
#define EYE(field) p->eyes.field = Math_Lerp(a->eyes.field,b->eyes.field,genes.orbit)
    EYE(size); p->head.eyeSize = p->eyes.size; EYE(protrusion); EYE(irisScale); EYE(pupilScale); EYE(pupilAspect);
#undef EYE
#define EYE(color) p->eyes.color = Palette(a->eyes.color,b->eyes.color,r->eyeColor,g)
    EYE(scleraColor); EYE(irisColor); EYE(pupilColor);
#undef EYE
    p->headHybrid.referenceControls=(HeadRegionalControls){p->head.skullWidth,p->head.muzzleLength,
        p->head.jawDepth,p->head.earSize,p->head.eyeSize};
}

static bool SameFoot(const LimbPhenotype *a, const LimbPhenotype *b)
{
    return a->archetype == b->archetype && a->anatomyProfile == b->anatomyProfile &&
        a->digitCount == b->digitCount && a->nailCount == b->nailCount &&
        !memcmp(a->digitPhalanges,b->digitPhalanges,a->digitCount*sizeof(a->digitPhalanges[0]));
}

static void BlendLimb(LimbPhenotype *l, const LimbPhenotype *base, const LimbPhenotype *other,
    const OffspringGenome *g, unsigned source)
{
    const OffspringReport *r = &g->report;
#define FROM(w) (source ? 1-(w) : (w))
    float weight = FROM(r->limbMass);
    if (other) {
        float ratio = base->thickness/base->length;
        float target = Math_Clamp(other->thickness/other->length,ratio*.45f,ratio*2.10f);
        l->thickness = l->length*Math_Lerp(ratio,target,weight);
        bool compatible = base->archetype == other->archetype;
        float compatibility = compatible ? 1 : .50f;
#define SHAPE(field,w) l->field = Math_Lerp(base->field,other->field,FROM(w)*compatibility)
        SHAPE(proximalScale,r->upperLimb); SHAPE(middleScale,r->lowerLimb);
        SHAPE(distalScale,r->lowerLimb); SHAPE(sprawl,r->limbs);
        SHAPE(proximalSweep,r->upperLimb); SHAPE(distalSweep,r->lowerLimb);
        SHAPE(attachmentInset,r->upperLimb); SHAPE(rootThicknessScale,r->limbMass);
        SHAPE(proximalThicknessScale,r->limbMass); SHAPE(middleThicknessScale,r->limbMass);
        SHAPE(distalThicknessScale,r->limbMass); SHAPE(flexionScale,r->lowerLimb);
#undef SHAPE
        if (SameFoot(base,other)) {
            weight = FROM(r->foot);
            l->footScale = Math_Lerp(base->footScale,other->footScale,weight);
            l->footWidthScale = Math_Lerp(base->footWidthScale,other->footWidthScale,weight);
            l->footHeightScale = Math_Lerp(base->footHeightScale,other->footHeightScale,weight);
        }
    }
    l->thickness *= g->limbMassGain;
    l->footWidthScale *= g->footGain;
#undef FROM
}

static bool UsedSlot(const BodyPlan *plan, CreatureModuleKind kind, AttachmentRef attachment)
{
    for (size_t i=0; i<plan->moduleCount; ++i) {
        const CreatureModuleInstance *m = &plan->modules[i];
        if (m->kind == kind && !m->pathAttached &&
            m->attachment.hostModuleInstanceId == attachment.hostModuleInstanceId &&
            InheritedSlot(m) == attachment.slotId) return true;
    }
    return false;
}

static bool SupportedGraft(const CreatureRecipe *donor, const CreatureModuleInstance *m,
    const CreatureModuleInstance *host, HeadArchetype head, HeadArchetype donorHead)
{
    if (!host || m->pathAttached || m->membranePrevious) return false;
    if (m->kind == CREATURE_MODULE_TAIL) return host->kind == CREATURE_MODULE_AXIAL;
    if (m->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE)
        return host->kind == CREATURE_MODULE_HEAD &&
            (HeadModule_IsNasalExtension(InheritedSlot(m)) || head == donorHead);
    if (m->kind != CREATURE_MODULE_ORNAMENT) return false;
    OrnamentArchetype type = donor->adult.ornaments[m->phenotypeIndex].archetype;
    if (type == ORNAMENT_ARCHETYPE_MEMBRANE) return false;
    if (host->kind == CREATURE_MODULE_AXIAL) return true;
    return host->kind == CREATURE_MODULE_HEAD &&
        (head == donorHead || (type == ORNAMENT_ARCHETYPE_HORN &&
            (InheritedSlot(m) == HEAD_SLOT_NASAL_ANTERIOR ||
             InheritedSlot(m) == HEAD_SLOT_NASAL_POSTERIOR)));
}

static int NameBytes(const char *name)
{
    size_t length = strlen(name);
    if (length <= 76) return (int)length;
    size_t limit = 76;
    while (limit && ((unsigned char)name[limit] & 0xc0u) == 0x80u) --limit;
    return (int)limit;
}

static void Build(const CreatureOffspringBatch *batch, OffspringGenome *g, Candidate *candidate,
    size_t index, bool conservative)
{
    OffspringReport *r = &g->report;
    CreatureRecipe *out = &candidate->document.recipe;
    const CreatureRecipe *parents[] = {&batch->parents[0].recipe,&batch->parents[1].recipe};
    unsigned base = r->scaffoldParent, donor = 1-base;
    const CreatureRecipe *scaffold = parents[base];
    const CreatureRecipe *other = parents[donor];
    out->bodyPlan = scaffold->bodyPlan;
    out->gait = scaffold->gait;
    if (conservative) r->headParent = r->limbParent = base;
    bool completeLimbs = scaffold->gait == other->gait &&
        scaffold->adult.axial.archetype == other->adult.axial.archetype &&
        scaffold->adult.limbCount == other->adult.limbCount;
    for (size_t i=0; i<out->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance *m = &out->bodyPlan.modules[i];
        int match = batch->matches[base][i];
        if (m->kind == CREATURE_MODULE_LIMB && match < 0) completeLimbs = false;
        if (m->kind == CREATURE_MODULE_HEAD && match < 0) r->headParent = base;
        /* Un anfitrión cefálico con dependencias especializadas se hereda entero. */
        const CreatureModuleInstance *host = FindModule(&out->bodyPlan,m->attachment.hostModuleInstanceId);
        if (m->pathAttached || (host && host->kind == CREATURE_MODULE_HEAD &&
                InheritedSlot(m) >= HEAD_SLOT_PROBOSCIS &&
                InheritedSlot(m) <= HEAD_SLOT_MAXILLARY_RIGHT))
            r->headParent = base;
    }
    if (!completeLimbs) r->limbParent = base;
    ModuleSource sources[CREATURE_MAX_MODULES];
    for (size_t i=0; i<out->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance *m = &out->bodyPlan.modules[i];
        int match = batch->matches[base][i];
        unsigned parent = base;
        if (match >= 0 && m->kind == CREATURE_MODULE_LIMB) parent = r->limbParent;
        if (match >= 0 && m->kind == CREATURE_MODULE_TAIL) parent = r->tailParent;
        sources[i] = parent == base ? (ModuleSource){base,(unsigned)i,match} :
            (ModuleSource){donor,(unsigned)match,(int)i};
    }
    size_t tails = scaffold->adult.tailCount, ornaments = scaffold->adult.ornamentCount;
    size_t flexible = scaffold->adult.flexibleAppendageCount;
    for (size_t i=0; i<other->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance *m = &other->bodyPlan.modules[i];
        if (batch->matches[donor][i] >= 0 || (m->kind != CREATURE_MODULE_TAIL &&
            m->kind != CREATURE_MODULE_ORNAMENT && m->kind != CREATURE_MODULE_FLEXIBLE_APPENDAGE))
            continue;
        uint32_t hostId = MapModule(batch,&out->bodyPlan,sources,donor,m->attachment.hostModuleInstanceId);
        AttachmentRef attachment = AttachmentRef_Create(hostId,InheritedSlot(m));
        size_t *count = m->kind == CREATURE_MODULE_TAIL ? &tails :
            m->kind == CREATURE_MODULE_ORNAMENT ? &ornaments : &flexible;
        size_t limit = m->kind == CREATURE_MODULE_TAIL ? CREATURE_MAX_TAILS :
            m->kind == CREATURE_MODULE_ORNAMENT ? CREATURE_MAX_ORNAMENTS : CREATURE_MAX_FLEXIBLE_APPENDAGES;
        if (!SupportedGraft(other,m,FindModule(&out->bodyPlan,hostId),
                parents[r->headParent]->adult.head.archetype,other->adult.head.archetype)) {
            ++r->unsupportedGrafts;
            ++r->omitted;
            continue;
        }
        if (*count == limit || out->bodyPlan.moduleCount == CREATURE_MAX_MODULES ||
            UsedSlot(&out->bodyPlan,m->kind,attachment)) {
            ++r->constrainedGrafts;
            ++r->omitted;
            continue;
        }
        bool nasal=m->kind==CREATURE_MODULE_FLEXIBLE_APPENDAGE &&
            HeadModule_IsNasalExtension(attachment.slotId);
        float nasalContribution=donor ? r->headGenes.nasalExtension : 1-r->headGenes.nasalExtension;
        if (nasal ? nasalContribution<.015f : (!g->graft || conservative)) {
            ++r->unselectedGrafts;
            ++r->omitted;
            continue;
        }
        uint32_t id = CreatureRecipe_NextFreeModuleId(out);
        size_t at = out->bodyPlan.moduleCount++;
        out->bodyPlan.modules[at] = (CreatureModuleInstance){
            .instanceId=id,.kind=m->kind,.attachment=attachment,.phenotypeIndex=(unsigned)(*count)++
        };
        sources[at] = (ModuleSource){donor,(unsigned)i,-1};
        ++r->grafts;
    }
    out->id = UINT32_C(0x80000000) | (r->seed & UINT32_C(0x7fffffff));
    while (out->id == parents[0]->id || out->id == parents[1]->id)
        out->id = out->id == UINT32_MAX ? UINT32_C(0x80000000) : out->id+1;
    const char *a = parents[0]->name, *b = parents[1]->name;
    snprintf(candidate->document.name,sizeof(candidate->document.name),"%.*s × %.*s #%zu",
        NameBytes(a),a,NameBytes(b),b,index+1);
    out->name = candidate->document.name;
    out->offspringSeed = r->seed;
    out->offspringVersion = CREATURE_OFFSPRING_VERSION;
    for (unsigned stage=0; stage<=CREATURE_STAGE_ADULT; ++stage) {
        const CreaturePhenotype *p[] = {CreatureRecipe_GetStage(parents[0],stage),
                                       CreatureRecipe_GetStage(parents[1],stage)};
        CreaturePhenotype *result = Stage(out,stage);
        *result = *p[base];
        Axial(result,p[0],p[1],g);
        HeadSystem(result,p[0],p[1],batch,stage,g);
        Surface(result,p[0],p[1],batch,&out->bodyPlan,sources,g);
        result->development.appendages = p[r->limbParent]->development.appendages;
        if (result->tailCount == 0 && tails) r->tailParent = donor;
        result->tailCount = tails;
        result->ornamentCount = ornaments;
        result->flexibleAppendageCount = flexible;
        for (size_t i=0; i<out->bodyPlan.moduleCount; ++i) {
            ModuleSource s = sources[i];
            const CreatureModuleInstance *m = &out->bodyPlan.modules[i];
            unsigned from = parents[s.parent]->bodyPlan.modules[s.module].phenotypeIndex;
            unsigned to = m->phenotypeIndex;
            const CreaturePhenotype *source = p[s.parent];
            const CreaturePhenotype *opposite = p[1-s.parent];
            const CreatureModuleInstance *match = s.match < 0 ? NULL :
                &parents[1-s.parent]->bodyPlan.modules[s.match];
            if (m->kind == CREATURE_MODULE_LIMB) {
                LimbPhenotype *l = &result->limbs[to];
                const LimbPhenotype *original = &source->limbs[from];
                *l = *original;
                l->length *= result->axial.withersHeight/source->axial.withersHeight;
                if (match) {
                    const LimbPhenotype *peer = &opposite->limbs[match->phenotypeIndex];
                    float weight = s.parent ? 1-r->limbs : r->limbs;
                    bool samePosture = source->axial.archetype == opposite->axial.archetype;
                    float equivalent = samePosture ?
                        peer->length*result->axial.withersHeight/opposite->axial.withersHeight :
                        peer->length*result->axial.trunkLength/opposite->axial.trunkLength;
                    equivalent = Math_Clamp(equivalent,l->length*.65f,l->length*1.36f);
                    l->length = Math_Lerp(l->length,equivalent,weight);
                }
                BlendLimb(l,original,match ? &opposite->limbs[match->phenotypeIndex] : NULL,
                    g,s.parent);
                if (source->axial.archetype != opposite->axial.archetype) {
                    l->sprawl = original->sprawl;
                    l->proximalSweep = original->proximalSweep;
                    l->distalSweep = original->distalSweep;
                    l->flexionScale = original->flexionScale;
                    /* El apoyo sigue el espesor de la cadena, con la
                     * topología de dedos y pie del arquetipo seleccionado. */
                    float massGain = l->thickness/original->thickness;
                    float reachGain = l->length/original->length;
                    l->footWidthScale *= Math_Clamp(sqrtf(massGain/reachGain),.88f,1.18f);
                    l->rootThicknessScale = fmaxf(l->rootThicknessScale,original->rootThicknessScale);
                }
            } else if (m->kind == CREATURE_MODULE_TAIL) {
                TailPhenotype *t = &result->tails[to];
                *t = source->tails[from];
                float scale = result->axial.trunkLength/source->axial.trunkLength;
                if (match) {
                    const TailPhenotype *peer = &opposite->tails[match->phenotypeIndex];
                    if (t->archetype == peer->archetype) {
                        float w = s.parent ? 1-r->tail : r->tail;
                        t->length = Math_Lerp(t->length,peer->length*source->axial.trunkLength/
                            opposite->axial.trunkLength,w);
                        t->curvature = Math_Lerp(t->curvature,peer->curvature,w);
                        t->droop = Math_Lerp(t->droop,peer->droop,w);
                        t->tipLift = Math_Lerp(t->tipLift,peer->tipLift,w);
                    }
                }
                t->length *= scale*g->caudal;
                t->curvature *= g->tailCurve;
                float pelvis = result->axial.pelvicWidth/source->axial.pelvicWidth;
                t->baseWidth *= pelvis;
                t->baseHeight *= result->axial.pelvicHeight/source->axial.pelvicHeight;
                t->tipWidth *= pelvis;
                t->tipHeight *= pelvis;
            } else if (m->kind == CREATURE_MODULE_ORNAMENT) {
                OrnamentPhenotype *o = &result->ornaments[to];
                *o = source->ornaments[from];
                const CreatureModuleInstance *host = FindModule(&out->bodyPlan,m->attachment.hostModuleInstanceId);
                if (host && host->kind == CREATURE_MODULE_HEAD && !m->pathAttached &&
                    InheritedSlot(m) < HEAD_SLOT_PROBOSCIS &&
                    source->head.archetype != result->head.archetype) {
                    float fromEnvelope = source->headEnvelope.scale *
                        sqrtf(source->headEnvelope.widthScale*source->headEnvelope.heightScale);
                    float toEnvelope = result->headEnvelope.scale *
                        sqrtf(result->headEnvelope.widthScale*result->headEnvelope.heightScale);
                    if (result->headHybrid.active) {
                        Vector3 a = result->headHybrid.primaryRadii, b = result->headHybrid.secondaryRadii;
                        Vector3 mixed = result->headHybrid.hasRegionalProfile ?
                            result->headHybrid.regional.surface.craniumRadii : Vec3_Lerp(a,b,result->headHybrid.blend);
                        toEnvelope *= sqrtf(mixed.x*mixed.y)/sqrtf(1.025f*.60f);
                    }
                    float scale = Math_Clamp(toEnvelope/fromEnvelope,.35f,1.25f);
                    o->length *= scale;
                    o->baseRadius *= scale;
                    o->tipRadius *= scale;
                }
            } else if (m->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE) {
                FlexibleAppendagePhenotype *f = &result->flexibleAppendages[to];
                *f = source->flexibleAppendages[from];
                if (HeadModule_IsNasalExtension(InheritedSlot(m))) {
                    float extension=s.parent ? 1-r->headGenes.nasalExtension : r->headGenes.nasalExtension;
                    float terminal=s.parent ? 1-r->headGenes.nose : r->headGenes.nose;
                    if (match) {
                        const FlexibleAppendagePhenotype *peer=&opposite->flexibleAppendages[match->phenotypeIndex];
                        *f=FlexibleAppendage_Interpolate(f,peer,extension,terminal);
                    } else {
                        f->length*=fmaxf(1-extension,.01f);
                        f->tipRadius*=Math_Lerp(.72f,1,1-terminal);
                    }
                }
            }
        }
    }
    candidate->report = *r;
}

static bool HasHeadFrame(AABB3D frame)
{
    Vector3 size=AABB_Size(frame);
    return size.x>0 && size.y>0 && size.z>0;
}

static bool DescribeHead(const Monster *monster, AABB3D frame, OffspringDescriptor *d)
{
    if (!monster->hasHead || !HasHeadFrame(frame)) return true;
    MonsterSDF sdf = MonsterSDF_Create();
    bool ok = MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig()) && sdf.mouthCount;
    if (ok) {
        MonsterSDFHeadField context;
        SDFField field = MonsterSDF_GetHeadField(&sdf,0,&context);
        Vector3 span = AABB_Size(frame);
        float scale = monster->phenotype.axial.totalScale;
        Vector3 neck = Vec3_Add(monster->bodyParts[monster->head.anatomy.attachmentBodyPartIndex].positionRender,
            monster->head.anatomy.landmarks.neckAttachment);
        float band = Vec3_Length(span)*.13f;
        unsigned i=0;
        for (unsigned z=0; z<8; ++z) for (unsigned y=0; y<5; ++y) for (unsigned x=0; x<5; ++x) {
            Vector3 p = Vec3_Add(frame.start,Vec3_Create(span.x*(x+.5f)/5,
                span.y*(y+.5f)/5,span.z*(z+.5f)/8));
            float distance=field.evaluateDistance(field.context,Vec3_Add(neck,Vec3_Scale(p,scale)));
            d->headField[i++]=Math_Clamp(distance/(scale*band),-1,1);
        }
        d->hasHead = true;
    }
    MonsterSDF_Free(&sdf);
    return ok;
}

static bool Describe(const Monster *actual, const CreatureOffspringBatch *batch, Candidate *candidate)
{
    OffspringDescriptor *d=&candidate->descriptor;
    *d=(OffspringDescriptor){0};
    const MorphologicalSignature *s=&candidate->report.signature;
    float appendage=0;
    for (size_t i=0; i<actual->phenotype.flexibleAppendageCount; ++i)
        appendage=fmaxf(appendage,actual->phenotype.flexibleAppendages[i].length);
    float body[] = {s->bodyLengthWidthRatio*.40f,s->bodyHeightWidthRatio*.55f,
        s->forelimbBodyRatio,s->hindlimbBodyRatio,s->footBodyRatio*3,s->tailBodyLengthRatio,
        s->forelimbThicknessBodyRatio*3,s->distalTailCurvature*.4f,s->maxOrnamentHeightBodyRatio,appendage*.55f};
    memcpy(d->shape,body,sizeof(body));
    if (actual->hasHead) {
        const HeadAnatomy *h=&actual->head.anatomy;
        const HeadSurfaceRecipe *r=&h->surface;
        HeadResolvedMeasurements m=HeadAnatomy_Measure(h);
        float size=fmaxf(.001f,m.actualHeadLength);
        float head[] = {m.actualWidthLengthRatio,r->craniumRadii.y/size,
            (r->faceTip.z-r->faceRoot.z)/size,h->oralSystem.jawThickness/size,
            m.interEyeDistance/size,r->earShape.y/size,.25f*r->earDirection.y};
        memcpy(d->shape+sizeof(body)/sizeof(*body),head,sizeof(head));
    }
    const SurfacePhenotype *p=&actual->surface;
    const SurfaceRegion regions[]={SURFACE_REGION_DORSAL_TRUNK,SURFACE_REGION_VENTRAL_TRUNK,
        SURFACE_REGION_HEAD,SURFACE_REGION_FORELIMB,SURFACE_REGION_TAIL,SURFACE_REGION_NECK};
    size_t at=0;
    for (size_t i=0; i<sizeof(regions)/sizeof(*regions); ++i) {
        SurfaceCoordinate c={.position={.27f,.16f,1.39f},.region=regions[i],.secondaryRegion=regions[i],
            .ventral=regions[i]==SURFACE_REGION_VENTRAL_TRUNK};
        Vector3 pigment=Surface_EvaluatePigmentAlbedo(p,&c);
        d->appearance[at++]=pigment.x;
        d->appearance[at++]=pigment.y;
        d->appearance[at++]=pigment.z;
        d->appearance[at++]=p->furRegions[regions[i]].densityMultiplier*p->integument.coverage;
    }
    const PigmentPhenotype *banks[]={&p->transition.from,&p->transition.to};
    for (unsigned i=0; i<2; ++i) {
        float weight=i ? p->transition.weight : 1-p->transition.weight;
        float intensity=banks[i]->patternStrength, frequency=banks[i]->patternScale*.05f;
        for (size_t j=0; j<banks[i]->layerCount; ++j) {
            intensity+=banks[i]->layers[j].strength;
            frequency+=banks[i]->layers[j].scale*.05f*banks[i]->layers[j].strength;
        }
        d->appearance[at++]=weight*intensity*.25f;
        d->appearance[at++]=weight*frequency*.25f;
    }
    d->appearance[at++]=p->transition.scaleCoverage;
    d->appearance[at++]=p->integument.scales.relief*4;
    d->appearance[at++]=p->integument.fur.density;
    d->appearance[at++]=p->integument.fur.length*5;
    return DescribeHead(actual,batch->headFrame,d);
}

static float Distance(const float *a, const float *b, size_t count)
{
    float sum=0;
    for (size_t i=0; i<count; ++i) {
        float delta=a[i]-b[i];
        sum+=delta*delta;
    }
    return sqrtf(sum/count);
}

static bool Score(const CreatureOffspringBatch *batch, Candidate *candidate)
{
    float shape=INFINITY, appearance=INFINITY;
    for (size_t i=0; i<batch->config.count; ++i) {
        const Candidate *other=batch->candidates[i];
        if (!other || other==candidate) continue;
        if (candidate->document.recipe.id==other->document.recipe.id) return false;
        const OffspringDescriptor *a=&candidate->descriptor, *b=&other->descriptor;
        float geometry = Distance(a->shape,b->shape,SHAPE_FEATURES);
        if (a->hasHead && b->hasHead)
            geometry = .58f*geometry+.42f*Distance(a->headField,b->headField,HEAD_SAMPLES);
        float color = Distance(a->appearance,b->appearance,APPEARANCE_FEATURES);
        if (geometry<.008f) return false;
        shape=fminf(shape,geometry);
        appearance=fminf(appearance,color);
    }
    candidate->report.shapeDiversity=isfinite(shape) ? shape : 0;
    candidate->report.appearanceDiversity=isfinite(appearance) ? appearance : 0;
    /* La morfología domina la selección; el color desempata familias de
     * forma parecida y no puede rescatar una propuesta casi idéntica. */
    return true;
}

static float SelectionValue(const CreatureOffspringBatch *batch, const Candidate *candidate)
{
    const OffspringReport *r=&candidate->report;
    unsigned same=0, different=0;
    for (size_t i=0; i<batch->config.count; ++i) {
        const Candidate *peer=batch->candidates[i];
        if (!peer || i==candidate->index) continue;
        if (peer->report.scaffoldParent==r->scaffoldParent) ++same;
        else ++different;
    }
    float balance=.036f*((float)different-(float)same)/(1+same+different);
    return r->shapeDiversity + .20f*fminf(r->appearanceDiversity,r->shapeDiversity)+.12f*r->quality+balance;
}

static bool Proposal(const CreatureOffspringBatch *batch, Candidate *candidate, size_t index,
    unsigned revision, unsigned attempt, Monster *target, Monster *actual,
    CreatureOffspringCancel cancel, void *context, char *detail, size_t capacity)
{
    OffspringGenome genome=Genome(batch,index,revision,attempt);
    const CreatureRecipe *reference=&batch->parents[genome.report.scaffoldParent].recipe;
    Build(batch,&genome,candidate,index,attempt%4==3);
    OffspringReport *report=&candidate->report;
    CreatureRecipe *recipe=&candidate->document.recipe;
    report->hasMorphTarget=CreatureMorph_Interpolate(&batch->adults[0],&batch->adults[1],
        report->global,target);
    if (report->hasMorphTarget) {
        if (recipe->adult.headHybrid.hasRegionalProfile) {
            float scale=target->phenotype.axial.totalScale;
            target->head.anatomy=recipe->adult.headHybrid.regional;
            HeadAnatomy_ScaleProfile(&target->head.anatomy,Vec3_Create(scale,scale,scale));
        }
        OffspringMorph_Fit(recipe,target,batch->adults,report);
    }
    OffspringRepair_Integrate(recipe);
    unsigned repairs=0;
    for (unsigned step=0; step<=batch->config.repairs; ++step) {
        if (cancel && cancel(context)) return false;
        if (!OffspringRepair_Validate(recipe,reference,batch->anatomy[report->scaffoldParent],.75f,
                step<batch->config.repairs,report,cancel,context,detail,capacity)) {
            if (!report->repairs) return false;
            repairs+=report->repairs;
            report->repairs=0;
            continue;
        }
        report->repairs+=repairs;
        if (!CreatureRecipeIO_ValidateFields(recipe) || !Creature_ResolveAppearance(actual,recipe,&recipe->adult)) {
            snprintf(detail,capacity,"Cannot serialize or resolve fitted recipe");
            return false;
        }
        if (report->hasMorphTarget) OffspringMorph_MeasureCandidate(actual,target,batch->adults,report);
        if (!Describe(actual,batch,candidate)) {
            snprintf(detail,capacity,"Cannot sample head geometry");
            return false;
        }
        report->quality=1/(1+2*report->headError+report->bodyError+.35f*report->limbError
            +.035f*report->repairs+.12f*(1-report->anatomicalCoherence));
        candidate->revision=revision;
        candidate->index=index;
        candidate->owner=batch;
        return true;
    }
    return false;
}

static void FreePool(ProposalPool *pool)
{
    for (unsigned i=0; i<pool->count; ++i) free(pool->items[i]);
    pool->count=0;
}

static bool Collect(const CreatureOffspringBatch *batch, ProposalPool *pool, size_t index,
    unsigned revision, CreatureOffspringCancel cancel, void *context, char *error, size_t capacity)
{
    Candidate *trial=calloc(1,sizeof(*trial));
    Monster *target=calloc(1,sizeof(*target)), *actual=calloc(1,sizeof(*actual));
    char detail[128]="No valid morphology";
    unsigned attempts=0;
    unsigned budget=batch->config.proposalsPerOffspring ? batch->config.proposalsPerOffspring : 6;
    bool ok=false;
    if (!trial || !target || !actual) {
        snprintf(detail,sizeof(detail),"Cannot allocate offspring and morph reference");
        goto done;
    }
    while (attempts<batch->config.attempts && pool->count<budget) {
        if (cancel && cancel(context)) break;
        unsigned attempt=attempts++;
        if (!Proposal(batch,trial,index,revision,attempt,target,actual,cancel,context,detail,sizeof(detail)))
            continue;
        Candidate *saved=malloc(sizeof(*saved));
        if (!saved) {
            snprintf(detail,sizeof(detail),"Cannot retain offspring proposal");
            goto done;
        }
        *saved=*trial;
        saved->document.recipe.name=saved->document.name;
        pool->items[pool->count++]=saved;
    }
    ok=pool->count>0;
done:
    if (cancel && cancel(context)) {
        ok=false;
        snprintf(detail,sizeof(detail),"Generation cancelled");
    }
    if (ok) {
        for (unsigned i=0; i<pool->count; ++i) {
            pool->items[i]->report.proposalsEvaluated=attempts;
            pool->items[i]->report.proposalsValid=pool->count;
        }
        if (capacity) error[0]=0;
    } else {
        FreePool(pool);
        snprintf(error,capacity,"Candidate %zu: %s",index+1,detail);
    }
    if (target) Monster_Free(target);
    if (actual) Monster_Free(actual);
    free(target); free(actual); free(trial);
    return ok;
}

static Candidate **BestProposal(const CreatureOffspringBatch *batch, ProposalPool pools[],
    size_t count)
{
    Candidate **best=NULL;
    float value=-INFINITY;
    for (size_t slot=0; slot<count; ++slot) {
        for (unsigned i=0; i<pools[slot].count; ++i) {
            Candidate **proposal=&pools[slot].items[i];
            if (!*proposal || !Score(batch,*proposal)) continue;
            float score=SelectionValue(batch,*proposal);
            if (score>value+1e-6f) {
                value=score;
                best=proposal;
            }
        }
    }
    return best;
}

static bool SelectBatch(CreatureOffspringBatch *batch, CreatureOffspringCancel cancel,
    CreatureOffspringProgress progress, void *context, char *error, size_t capacity)
{
    ProposalPool pools[CREATURE_OFFSPRING_MAX]={0};
    size_t count=batch->config.count;
    bool ok=false;
    for (size_t i=0; i<count; ++i) {
        if (!Collect(batch,&pools[i],i,0,cancel,context,error,capacity)) goto done;
        if (progress) progress(context,i+1);
    }
    for (size_t i=0; i<count; ++i) {
        if (cancel && cancel(context)) {
            snprintf(error,capacity,"Generation cancelled");
            goto done;
        }
        Candidate **best=BestProposal(batch,pools,count);
        if (!best) {
            snprintf(error,capacity,"No distinct morphology in the remaining contribution bands");
            goto done;
        }
        size_t slot=(*best)->index;
        batch->candidates[slot]=*best;
        *best=NULL;
        FreePool(&pools[slot]);
    }
    for (size_t i=0; i<count; ++i) Score(batch,batch->candidates[i]);
    ok=true;
done:
    for (size_t i=0; i<count; ++i) FreePool(&pools[i]);
    return ok;
}

static void BuildHeadFrame(CreatureOffspringBatch *batch)
{
    batch->headFrame=AABB_Empty();
    for (unsigned i=0; i<2; ++i) {
        if (!batch->adults[i].hasHead) continue;
        HeadAnatomy head=batch->heads[i][CREATURE_STAGE_ADULT];
        float unit=1/batch->adults[i].phenotype.axial.totalScale;
        HeadAnatomy_ScaleProfile(&head,Vec3_Create(unit,unit,unit));
        const HeadSurfaceRecipe *s=&head.surface;
        AABB_ExpandRadius(&batch->headFrame,s->craniumCenter,s->craniumRadii);
        AABB_ExpandRadius(&batch->headFrame,s->faceTip,s->faceTipRadii);
        if (s->hasEars) AABB_ExpandRadius(&batch->headFrame,s->leftEarCenter,s->earRadii);
    }
    if (HasHeadFrame(batch->headFrame)) {
        AABB_Pad(&batch->headFrame,.12f);
        batch->headFrame.start.x=0;
    }
}

static CreatureOffspringBatch *AnalyzeParents(const CreatureRecipe *a, const CreatureRecipe *b,
    CreatureOffspringConfig config, CreatureOffspringCancel cancel, void *context, char *error, size_t capacity)
{
    if (!a || !b || !ValidConfig(config)) {
        snprintf(error,capacity,"Invalid parents or generation configuration");
        return NULL;
    }
    CreatureOffspringBatch *batch = calloc(1,sizeof(*batch));
    if (!batch) {
        snprintf(error,capacity,"Cannot allocate parent analysis");
        return NULL;
    }
    batch->config = config;
    const CreatureRecipe *parents[] = {a,b};
    for (unsigned p=0; p<2; ++p) {
        if (!CreatureRecipeIO_ValidateFields(parents[p])) {
            snprintf(error,capacity,"Parent %c has invalid or non-finite recipe fields",'X'+p);
            CreatureOffspring_Destroy(batch);
            return NULL;
        }
        CreatureRecipeDocument_Copy(&batch->parents[p],parents[p]);
        CreatureRecipe *snapshot = &batch->parents[p].recipe;
        for (unsigned s=0; s<=CREATURE_STAGE_ADULT; ++s) {
            CreaturePhenotype *stage = Stage(snapshot,s);
            if ((cancel && cancel(context)) || stage->surface.pigment.layerCount > SURFACE_MAX_PIGMENT_LAYERS ||
                stage->surface.tuftCount > SURFACE_MAX_FUR_TUFTS ||
                !Creature_ResolveAnatomy(snapshot,stage,&batch->anatomy[p][s])) {
                snprintf(error,capacity,"Parent %.72s: invalid or cancelled at stage %u",snapshot->name,s);
                CreatureOffspring_Destroy(batch);
                return NULL;
            }
            CreaturePhenotype_Normalize(stage);
            const AnatomyNode *head = AnatomyGraph_FindFirstRegion(&batch->anatomy[p][s],
                ANATOMY_REGION_HEAD);
            if (head) {
                Vector3 radii = head->envelopeRadii;
                if (stage->headHybrid.active) {
                    Vector3 ratio = HeadHybridProfile_EnvelopeRatio(&stage->headHybrid);
                    radii = Vec3_Create(radii.x/ratio.x,radii.y/ratio.y,radii.z/ratio.z);
                }
                if (!HeadAnatomy_ResolveHybrid(&stage->head,&stage->headHybrid,0,radii,&batch->heads[p][s])) {
                    snprintf(error,capacity,"Parent %.72s: cannot resolve stage %u head",snapshot->name,s);
                    CreatureOffspring_Destroy(batch);
                    return NULL;
                }
                HeadAnatomy_CenterProfile(&batch->heads[p][s]);
            }
        }
        for (unsigned s=1; s<=CREATURE_STAGE_ADULT; ++s) {
            if (!CreaturePhenotype_MorphCompatible(Stage(snapshot,0),Stage(snapshot,s))) {
                snprintf(error,capacity,"Parent %.72s: incompatible developmental topology",snapshot->name);
                CreatureOffspring_Destroy(batch);
                return NULL;
            }
        }
        if (!Creature_BuildMonster(&batch->adults[p],snapshot,&snapshot->adult)) {
            snprintf(error,capacity,"Parent %.72s: cannot construct adult reference",snapshot->name);
            CreatureOffspring_Destroy(batch);
            return NULL;
        }
    }
    Match(batch);
    BuildHeadFrame(batch);
    return batch;
}

CreatureOffspringBatch *CreatureOffspring_GenerateProgress(const CreatureRecipe *a, const CreatureRecipe *b,
    CreatureOffspringConfig config, CreatureOffspringCancel cancel, CreatureOffspringProgress progress,
    void *context, char *error, size_t capacity)
{
    CreatureOffspringBatch *batch = AnalyzeParents(a,b,config,cancel,context,error,capacity);
    if (!batch) return NULL;
    if (!SelectBatch(batch,cancel,progress,context,error,capacity)) {
        CreatureOffspring_Destroy(batch);
        return NULL;
    }
    return batch;
}

bool CreatureOffspring_ReplayProposal(const CreatureRecipe *a, const CreatureRecipe *b,
    CreatureOffspringConfig config, size_t index, unsigned attempt, CreatureRecipeDocument *out,
    char *error, size_t capacity)
{
    if (!out || index>=config.count || attempt>=config.attempts) return false;
    CreatureOffspringBatch *batch = AnalyzeParents(a,b,config,NULL,NULL,error,capacity);
    if (!batch) return false;
    Candidate *candidate = calloc(1,sizeof(*candidate));
    Monster *target = calloc(1,sizeof(*target)), *actual = calloc(1,sizeof(*actual));
    bool ok = false;
    if (candidate && target && actual) {
        ok = Proposal(batch,candidate,index,0,attempt,target,actual,NULL,NULL,error,capacity);
        if (ok) CreatureRecipeDocument_Copy(out,&candidate->document.recipe);
    }
    if (target) Monster_Free(target);
    if (actual) Monster_Free(actual);
    free(target);
    free(actual);
    free(candidate);
    CreatureOffspring_Destroy(batch);
    return ok;
}

CreatureOffspringBatch *CreatureOffspring_Generate(const CreatureRecipe *a, const CreatureRecipe *b,
    CreatureOffspringConfig config, CreatureOffspringCancel cancel, void *context,
    char *error, size_t capacity)
{
    return CreatureOffspring_GenerateProgress(a,b,config,cancel,NULL,context,error,capacity);
}

OffspringCandidate *CreatureOffspring_PrepareReroll(const CreatureOffspringBatch *batch, size_t index,
    CreatureOffspringCancel cancel, void *context, char *error, size_t capacity)
{
    if (!batch || index >= batch->config.count) {
        snprintf(error,capacity,"Candidate does not exist");
        return NULL;
    }
    if (batch->candidates[index]->revision == UINT_MAX) {
        snprintf(error,capacity,"Candidate reroll limit exhausted");
        return NULL;
    }
    ProposalPool pool={0};
    if (!Collect(batch,&pool,index,batch->candidates[index]->revision+1,cancel,context,error,capacity))
        return NULL;
    Candidate **best=BestProposal(batch,&pool,1);
    Candidate *result=best ? *best : NULL;
    if (cancel && cancel(context)) {
        snprintf(error,capacity,"Generation cancelled");
        FreePool(&pool);
        return NULL;
    }
    if (best) *best=NULL;
    else snprintf(error,capacity,"No distinct morphology for candidate %zu",index+1);
    FreePool(&pool);
    return result;
}

bool CreatureOffspring_AcceptReroll(CreatureOffspringBatch *batch, size_t index, OffspringCandidate *result)
{
    if (!result || !batch || result->owner != batch || index >= batch->config.count ||
        result->index != index || result->revision != batch->candidates[index]->revision+1)
        return false;
    free(batch->candidates[index]);
    batch->candidates[index] = result;
    for (size_t i=0; i<batch->config.count; ++i) Score(batch,batch->candidates[i]);
    return true;
}

void CreatureOffspring_DiscardReroll(OffspringCandidate *candidate) { free(candidate); }

bool CreatureOffspring_Reroll(CreatureOffspringBatch *batch, size_t index,
    CreatureOffspringCancel cancel, void *context, char *error, size_t capacity)
{
    Candidate *replacement = CreatureOffspring_PrepareReroll(batch,index,cancel,context,error,capacity);
    if (!replacement) return false;
    if (CreatureOffspring_AcceptReroll(batch,index,replacement)) return true;
    CreatureOffspring_DiscardReroll(replacement);
    snprintf(error,capacity,"Candidate changed before reroll publication");
    return false;
}

void CreatureOffspring_Destroy(CreatureOffspringBatch *batch)
{
    if (!batch) return;
    for (size_t i=0; i<batch->config.count; ++i) free(batch->candidates[i]);
    for (unsigned p=0; p<2; ++p) Monster_Free(&batch->adults[p]);
    free(batch);
}

size_t CreatureOffspring_Count(const CreatureOffspringBatch *batch) { return batch ? batch->config.count : 0; }
uint32_t CreatureOffspring_Seed(const CreatureOffspringBatch *batch) { return batch ? batch->config.seed : 0; }

const CreatureRecipe *CreatureOffspring_Get(const CreatureOffspringBatch *batch, size_t index,
    const OffspringReport **report)
{
    const Candidate *candidate = index < CreatureOffspring_Count(batch) ? batch->candidates[index] : NULL;
    if (report) *report = candidate ? &candidate->report : NULL;
    return candidate ? &candidate->document.recipe : NULL;
}

const CreatureRecipe *CreatureOffspring_Parent(const CreatureOffspringBatch *batch, unsigned parent)
{
    return batch && parent < 2 ? &batch->parents[parent].recipe : NULL;
}
