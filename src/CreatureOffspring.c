#include "CreatureOffspring.h"
#include "CreatureGenome.h"
#include "HeadModule.h"
#include "MathUtils.h"
#include "OffspringRepair.h"
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct OffspringCandidate {
    CreatureRecipeDocument document;
    OffspringReport report;
    unsigned revision;
    size_t index;
    const CreatureOffspringBatch *owner;
} Candidate;

struct CreatureOffspringBatch {
    CreatureOffspringConfig config;
    CreatureRecipeDocument parents[2];
    int matches[2][CREATURE_MAX_MODULES];
    AnatomyGraph anatomy[2][4];
    Candidate *candidates[CREATURE_OFFSPRING_MAX];
};

typedef struct {
    OffspringReport report;
    float build, face, stance, caudal;
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
    return (CreatureOffspringConfig){count,seed,32,6,.35f,.65f};
}

static bool ValidConfig(CreatureOffspringConfig c)
{
    return c.count && c.count <= CREATURE_OFFSPRING_MAX && c.attempts && c.attempts <= 32 &&
        c.repairs <= 16 && c.minimumContribution >= 0 && c.maximumContribution <= 1 &&
        c.minimumContribution <= c.maximumContribution;
}

static const CreatureModuleInstance *FindModule(const BodyPlan *plan, uint32_t id)
{
    for (size_t i=0; i<plan->moduleCount; ++i)
        if (plan->modules[i].instanceId == id) return &plan->modules[i];
    return NULL;
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
        m->attachment.slotId != n->attachment.slotId) return false;
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

static OffspringGenome Genome(const CreatureOffspringBatch *batch, size_t index,
    unsigned revision, unsigned attempt)
{
    CreatureOffspringConfig c = batch->config;
    uint32_t seed = Mix(c.seed ^ Mix((uint32_t)index+1) ^ Mix(revision+101) ^
        Mix(attempt+1009) ^ CREATURE_OFFSPRING_VERSION);
    uint32_t random = seed;
    float band = (index + .5f + .35f * Variation(&random)) / c.count;
    float global = Math_Lerp(c.minimumContribution,c.maximumContribution,band);
    OffspringGenome g = {.report={.seed=seed,.global=global,.attempts=attempt+1}};
    OffspringReport *r = &g.report;
    r->body = Math_Clamp01(global+.28f*Variation(&random));
    r->head = Math_Clamp01(global+.32f*Variation(&random));
    r->limbs = Math_Clamp01(global+.20f*Variation(&random));
    r->tail = Math_Clamp01(global+.36f*Variation(&random));
    r->surface = Math_Clamp01(global+.30f*Variation(&random));
    r->size = 1+.14f*Variation(&random);
    r->scaffoldParent = (index + revision + attempt) % 2;
    r->headParent = (index / 2 + revision + attempt / 2 + attempt + 1) % 2;
    r->limbParent = (index / 2 + revision + attempt / 4 + attempt) % 2;
    r->tailParent = (index / 2 + index + revision + attempt) % 2;
    r->surfaceParent = r->surface >= .5f;
    g.build = 1+.20f*Variation(&random);
    g.face = 1+.13f*Variation(&random);
    g.stance = 1+.15f*Variation(&random);
    g.caudal = 1+.23f*Variation(&random);
    g.graft = attempt % 4 < 2;
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

static void Surface(CreaturePhenotype *p, const CreaturePhenotype *a, const CreaturePhenotype *b,
    const CreatureOffspringBatch *batch, const BodyPlan *plan, const ModuleSource sources[],
    OffspringReport *r)
{
    unsigned parent = r->surfaceParent;
    p->surface = parent ? b->surface : a->surface;
    SurfacePhenotype *s = &p->surface;
    memset(&s->transition,0,sizeof(s->transition));
    const SurfacePhenotype *sa = &a->surface, *sb = &b->surface;
    s->pigment.baseColor = Color_Lerp(sa->pigment.baseColor,sb->pigment.baseColor,r->surface);
    s->pigment.ventralColor = Color_Lerp(sa->pigment.ventralColor,sb->pigment.ventralColor,r->surface);
    s->pigment.secondaryColor = Color_Lerp(sa->pigment.secondaryColor,sb->pigment.secondaryColor,r->surface);
    p->dorsalColor = Color_Lerp(a->dorsalColor,b->dorsalColor,r->surface);
    p->ventralColor = Color_Lerp(a->ventralColor,b->ventralColor,r->surface);
    p->unpigmentedVentralColor = Color_Lerp(a->unpigmentedVentralColor,b->unpigmentedVentralColor,r->surface);
    if (sa->integument.type == sb->integument.type) {
#define BLEND(field) s->field = Math_Lerp(sa->field,sb->field,r->surface)
        BLEND(integument.coverage);
        BLEND(integument.fur.length); BLEND(integument.fur.density); BLEND(integument.fur.thickness);
        BLEND(integument.scales.size); BLEND(integument.scales.relief); BLEND(wrinkleStrength);
#undef BLEND
    }
    size_t count = 0;
    for (size_t i=0; i<s->longHairCount; ++i) {
        LongHairGroup hair = s->longHair[i];
        LongHairDomain *d = &hair.domain;
        d->hostModuleId = MapModule(batch,plan,sources,parent,d->hostModuleId);
        bool valid = d->hostModuleId != 0;
        for (size_t j=0; j<d->anchorCount; ++j) {
            d->anchors[j] = MapNode(batch,plan,sources,parent,d->anchors[j]);
            valid &= d->anchors[j] != 0;
        }
        if (!valid) { ++r->omitted; ++r->removedAnchors; continue; }
        s->longHair[count++] = hair;
    }
    s->longHairCount = count;
    count = 0;
    for (size_t i=0; i<s->pigment.layerCount; ++i) {
        PigmentLayer layer = s->pigment.layers[i];
        if (layer.domain.anchorNode) {
            layer.domain.anchorNode = MapNode(batch,plan,sources,parent,layer.domain.anchorNode);
            if (!layer.domain.anchorNode) { ++r->omitted; ++r->removedAnchors; continue; }
        }
        s->pigment.layers[count++] = layer;
    }
    s->pigment.layerCount = count;
}

static void Axial(CreaturePhenotype *p, const CreaturePhenotype *a, const CreaturePhenotype *b,
    const OffspringGenome *g)
{
    const OffspringReport *r = &g->report;
    const AxialPhenotype *base = r->scaffoldParent ? &b->axial : &a->axial;
    const AxialPhenotype *other = r->scaffoldParent ? &a->axial : &b->axial;
    float w = r->scaffoldParent ? 1-r->body : r->body;
    bool compatible = base->archetype == other->archetype;
    float proportion = compatible ? w : .23f*w;
    float ratio = base->trunkLength / other->trunkLength;
    p->axial = *base;
    p->axial.totalScale = expf(Math_Lerp(logf(a->axial.totalScale),logf(b->axial.totalScale),r->global))*r->size;
    float length = Math_Lerp(base->trunkLength,other->trunkLength,proportion);
    p->axial.trunkLength = length;
#define PROPORTION(field) p->axial.field = Math_Lerp(base->field,other->field*ratio,proportion)*length/base->trunkLength
    PROPORTION(neckLength); PROPORTION(neckWidth); PROPORTION(shoulderWidth);
    PROPORTION(thoraxWidth); PROPORTION(abdomenWidth); PROPORTION(pelvicWidth);
#undef PROPORTION
    p->axial.thoraxWidth *= g->build;
    p->axial.abdomenWidth *= g->build;
    p->axial.pelvicWidth *= g->build;
    p->axial.shoulderWidth *= g->build;
    p->axial.withersHeight *= g->stance;
    float heightRatio = base->withersHeight/other->withersHeight;
#define HEIGHT(field) p->axial.field = Math_Lerp(base->field,other->field*heightRatio,proportion)*g->stance
    HEIGHT(thoraxHeight); HEIGHT(abdomenHeight); HEIGHT(pelvicHeight);
#undef HEIGHT
    p->development.pigmentation = Math_Lerp(a->development.pigmentation,b->development.pigmentation,r->surface);
    p->development.colorMaturity = Math_Lerp(a->development.colorMaturity,b->development.colorMaturity,r->surface);
    p->development.integument = r->surfaceParent ? b->development.integument : a->development.integument;
}

static void HeadSystem(CreaturePhenotype *p, const CreaturePhenotype *a,
    const CreaturePhenotype *b, const OffspringGenome *g)
{
    const CreaturePhenotype *h = g->report.headParent ? b : a;
    p->head = h->head;
    p->headEnvelope = h->headEnvelope;
    p->eyes = h->eyes;
    p->development.cephalic = h->development.cephalic;
    if (a->head.archetype == b->head.archetype &&
        a->head.cranialForm == b->head.cranialForm && a->head.muzzleForm == b->head.muzzleForm &&
        a->head.eyeLayout == b->head.eyeLayout) {
        /* Se conserva el paquete categórico y se mezclan controles correlacionados. */
#define HEAD(field) p->head.field = Math_Lerp(a->head.field,b->head.field,g->report.head)
        HEAD(skullWidth); HEAD(skullHeight); HEAD(skullLength); HEAD(muzzleLength);
        HEAD(muzzleWidth); HEAD(rostrumDepth); HEAD(jawLength); HEAD(jawDepth);
        HEAD(cheekMass); HEAD(earSize); HEAD(earWidthRatio); HEAD(earHeightRatio);
        HEAD(eyeSize); HEAD(temporalWidth); HEAD(temporalDepth);
        p->eyes.size = Math_Lerp(a->eyes.size,b->eyes.size,g->report.head);
#undef HEAD
    }
    float trunkScale = sqrtf(p->axial.trunkLength/h->axial.trunkLength);
    p->headEnvelope.scale *= Math_Clamp(trunkScale,.76f,1.30f);
    p->headEnvelope.lengthScale *= g->face;
    p->headEnvelope.widthScale *= sqrtf(g->face);
    if (a->head.archetype != b->head.archetype) {
        const HeadPhenotype *peer = g->report.headParent ? &a->head : &b->head;
        if (h->head.archetype != HEAD_ARCHETYPE_LIZARD && h->head.archetype != HEAD_ARCHETYPE_AVIAN &&
            peer->archetype != HEAD_ARCHETYPE_LIZARD && peer->archetype != HEAD_ARCHETYPE_AVIAN &&
            h->head.earSize > .1f && peer->earSize > .1f) {
            float earWeight = (g->report.headParent ? 1-g->report.head : g->report.head)*.43f;
#define EAR(field) p->head.field = Math_Lerp(h->head.field,peer->field,earWeight)
            EAR(earSize); EAR(earBaseWidth); EAR(earWidthRatio); EAR(earHeightRatio);
            EAR(earThickness); EAR(earLongitudinalCurve); EAR(earRootFlare); EAR(earOutward);
#undef EAR
        }
        /* El cuello de un cuerpo ancho puede ocultar pabellones y mejillas
         * de una cabeza pequeña. Conserva la relación cervical del donante
         * dentro del intervalo de adaptación del cráneo. */
        float ceiling = h->axial.neckWidth * Math_Clamp(trunkScale*1.18f,.85f,1.44f);
        p->axial.neckWidth = fminf(p->axial.neckWidth,ceiling);
    }
}

static bool SameFoot(const LimbPhenotype *a, const LimbPhenotype *b)
{
    return a->archetype == b->archetype && a->anatomyProfile == b->anatomyProfile &&
        a->digitCount == b->digitCount && a->nailCount == b->nailCount &&
        !memcmp(a->digitPhalanges,b->digitPhalanges,a->digitCount*sizeof(a->digitPhalanges[0]));
}

static void BlendLimb(LimbPhenotype *l, const LimbPhenotype *base, const LimbPhenotype *other,
    float weight, float thicknessScale)
{
    if (other) {
        float ratio = base->thickness / base->length;
        float target = other->thickness / other->length;
        if (!SameFoot(base,other)) {
            target = Math_Clamp(target,ratio*.78f,ratio*1.24f);
            weight *= .5f;
        } else {
            l->footScale = Math_Lerp(base->footScale,other->footScale,weight);
            l->footWidthScale = Math_Lerp(base->footWidthScale,other->footWidthScale,weight);
        }
        l->thickness = l->length * Math_Lerp(ratio,target,weight);
    }
    l->thickness *= thicknessScale;
}

static bool UsedSlot(const BodyPlan *plan, CreatureModuleKind kind, AttachmentRef attachment)
{
    for (size_t i=0; i<plan->moduleCount; ++i) {
        const CreatureModuleInstance *m = &plan->modules[i];
        if (m->kind == kind && !m->pathAttached &&
            m->attachment.hostModuleInstanceId == attachment.hostModuleInstanceId &&
            m->attachment.slotId == attachment.slotId) return true;
    }
    return false;
}

static bool SupportedGraft(const CreatureRecipe *donor, const CreatureModuleInstance *m,
    const CreatureModuleInstance *host, HeadArchetype head, HeadArchetype donorHead)
{
    if (!host || m->pathAttached || m->membranePrevious) return false;
    if (m->kind == CREATURE_MODULE_TAIL) return host->kind == CREATURE_MODULE_AXIAL;
    if (m->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE)
        return host->kind == CREATURE_MODULE_HEAD && head == donorHead;
    if (m->kind != CREATURE_MODULE_ORNAMENT) return false;
    OrnamentArchetype type = donor->adult.ornaments[m->phenotypeIndex].archetype;
    if (type == ORNAMENT_ARCHETYPE_MEMBRANE) return false;
    if (host->kind == CREATURE_MODULE_AXIAL) return true;
    return host->kind == CREATURE_MODULE_HEAD &&
        (head == donorHead || (type == ORNAMENT_ARCHETYPE_HORN &&
            (m->attachment.slotId == HEAD_SLOT_NASAL_ANTERIOR ||
             m->attachment.slotId == HEAD_SLOT_NASAL_POSTERIOR)));
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
                m->attachment.slotId >= HEAD_SLOT_PROBOSCIS &&
                m->attachment.slotId <= HEAD_SLOT_MAXILLARY_RIGHT))
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
        AttachmentRef attachment = AttachmentRef_Create(hostId,m->attachment.slotId);
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
        if (!g->graft || conservative) {
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
    for (unsigned stage=0; stage<=CREATURE_STAGE_ADULT; ++stage) {
        const CreaturePhenotype *p[] = {CreatureRecipe_GetStage(parents[0],stage),
                                       CreatureRecipe_GetStage(parents[1],stage)};
        CreaturePhenotype *result = Stage(out,stage);
        *result = *p[base];
        Axial(result,p[0],p[1],g);
        HeadSystem(result,p[0],p[1],g);
        Surface(result,p[0],p[1],batch,&out->bodyPlan,sources,r);
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
                    if (SameFoot(l,peer) && source->axial.archetype == opposite->axial.archetype) {
                        float weight = s.parent ? 1-r->limbs : r->limbs;
                        float equivalent = peer->length*result->axial.withersHeight/opposite->axial.withersHeight;
                        l->length = Math_Lerp(l->length,equivalent,weight);
                    }
                }
                BlendLimb(l,original,match ? &opposite->limbs[match->phenotypeIndex] : NULL,
                    s.parent ? 1-r->limbs : r->limbs,sqrtf(g->build));
            } else if (m->kind == CREATURE_MODULE_TAIL) {
                TailPhenotype *t = &result->tails[to];
                *t = source->tails[from];
                float scale = result->axial.trunkLength/source->axial.trunkLength;
                if (match) {
                    const TailPhenotype *peer = &opposite->tails[match->phenotypeIndex];
                    if (t->archetype == peer->archetype)
                        t->length = Math_Lerp(t->length,peer->length*source->axial.trunkLength/
                            opposite->axial.trunkLength,s.parent ? 1-r->tail : r->tail);
                }
                t->length *= scale*g->caudal;
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
                    source->head.archetype != result->head.archetype) {
                    float fromEnvelope = source->headEnvelope.scale *
                        sqrtf(source->headEnvelope.widthScale*source->headEnvelope.heightScale);
                    float toEnvelope = result->headEnvelope.scale *
                        sqrtf(result->headEnvelope.widthScale*result->headEnvelope.heightScale);
                    float scale = Math_Clamp(toEnvelope/fromEnvelope,.35f,1.25f);
                    o->length *= scale;
                    o->baseRadius *= scale;
                    o->tipRadius *= scale;
                }
            } else if (m->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE) {
                result->flexibleAppendages[to] = source->flexibleAppendages[from];
            }
        }
    }
    candidate->report = *r;
}

static bool SameLimbSystems(const CreaturePhenotype *a, const CreaturePhenotype *b)
{
    bool used[CREATURE_MAX_LIMBS] = {0};
    for (size_t i=0; i<a->limbCount; ++i) {
        const LimbPhenotype *left = &a->limbs[i];
        size_t j = 0;
        for (; j<b->limbCount; ++j) {
            const LimbPhenotype *right = &b->limbs[j];
            if (used[j] || left->role != right->role || left->side != right->side ||
                !SameFoot(left,right)) continue;
            used[j] = true;
            break;
        }
        if (j == b->limbCount) return false;
    }
    return true;
}

static bool Distinct(const CreatureOffspringBatch *batch, const Candidate *candidate, size_t index)
{
    const MorphologicalSignature *a = &candidate->report.signature;
    const CreaturePhenotype *p = &candidate->document.recipe.adult;
    for (size_t i=0; i<batch->config.count; ++i) {
        const Candidate *other = batch->candidates[i];
        if (!other) continue;
        const MorphologicalSignature *b = &other->report.signature;
        const CreaturePhenotype *q = &other->document.recipe.adult;
        if (i != index && candidate->document.recipe.id == other->document.recipe.id) return false;
        if (p->axial.archetype != q->axial.archetype || p->head.archetype != q->head.archetype ||
            p->ornamentCount != q->ornamentCount || p->flexibleAppendageCount != q->flexibleAppendageCount ||
            p->limbCount != q->limbCount || p->tailCount != q->tailCount) continue;
        if (!SameLimbSystems(p,q)) continue;
        int tails[TAIL_ARCHETYPE_COILED+1] = {0};
        for (size_t j=0; j<p->tailCount; ++j) {
            ++tails[p->tails[j].archetype];
            --tails[q->tails[j].archetype];
        }
        bool differentTails = false;
        for (size_t j=0; j<sizeof(tails)/sizeof(*tails); ++j) differentTails |= tails[j] != 0;
        if (differentTails) continue;
#define DELTA(field) (fabsf(a->field-b->field)/fmaxf(.12f,fmaxf(fabsf(a->field),fabsf(b->field))))
        float distance = DELTA(bodyLengthWidthRatio)+DELTA(bodyHeightWidthRatio)+DELTA(headBodyScale)+
            DELTA(headLengthWidthRatio)+DELTA(forelimbBodyRatio)+DELTA(hindlimbBodyRatio)+DELTA(footBodyRatio)+
            DELTA(tailBodyLengthRatio)+DELTA(forelimbThicknessBodyRatio)+DELTA(maxOrnamentHeightBodyRatio);
#undef DELTA
        if (distance < .12f) return false;
    }
    return true;
}

static Candidate *Generate(const CreatureOffspringBatch *batch, size_t index, unsigned revision,
    CreatureOffspringCancel cancel, void *context, char *error, size_t capacity)
{
    Candidate *candidate = malloc(sizeof(*candidate));
    if (!candidate) { snprintf(error,capacity,"Cannot allocate offspring"); return NULL; }
    char detail[128] = "No distinct morphology";
    for (unsigned attempt=0; attempt<batch->config.attempts; ++attempt) {
        if (cancel && cancel(context)) { strcpy(detail,"Generation cancelled"); break; }
        OffspringGenome g = Genome(batch,index,revision,attempt);
        const CreatureRecipe *reference = &batch->parents[g.report.scaffoldParent].recipe;
        Build(batch,&g,candidate,index,attempt % 4 == 3);
        unsigned repairs = 0;
        for (unsigned step=0; step<=batch->config.repairs; ++step) {
            if (cancel && cancel(context)) break;
            if (OffspringRepair_Validate(&candidate->document.recipe,reference,batch->anatomy[g.report.scaffoldParent],.75f,
                step < batch->config.repairs,&candidate->report,cancel,context,detail,sizeof(detail))) {
                candidate->report.repairs += repairs;
                if (Distinct(batch,candidate,index)) {
                    if (!CreatureRecipeIO_ValidateFields(&candidate->document.recipe)) {
                        strcpy(detail,"Recipe exceeds the JSON format limits");
                        break;
                    }
                    candidate->revision = revision;
                    candidate->index = index;
                    candidate->owner = batch;
                    if (capacity) error[0] = 0;
                    return candidate;
                }
                strcpy(detail,"No distinct morphology");
                break;
            }
            if (!candidate->report.repairs) break;
            repairs += candidate->report.repairs;
            candidate->report.repairs = 0;
        }
    }
    snprintf(error,capacity,"Candidate %zu: %s",index+1,detail);
    free(candidate);
    return NULL;
}

CreatureOffspringBatch *CreatureOffspring_GenerateProgress(const CreatureRecipe *a, const CreatureRecipe *b,
    CreatureOffspringConfig config, CreatureOffspringCancel cancel, CreatureOffspringProgress progress,
    void *context, char *error, size_t capacity)
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
        }
        for (unsigned s=1; s<=CREATURE_STAGE_ADULT; ++s) {
            if (!CreaturePhenotype_MorphCompatible(Stage(snapshot,0),Stage(snapshot,s))) {
                snprintf(error,capacity,"Parent %.72s: incompatible developmental topology",snapshot->name);
                CreatureOffspring_Destroy(batch);
                return NULL;
            }
        }
    }
    Match(batch);
    for (size_t i=0; i<config.count; ++i) {
        batch->candidates[i] = Generate(batch,i,0,cancel,context,error,capacity);
        if (!batch->candidates[i]) {
            CreatureOffspring_Destroy(batch);
            return NULL;
        }
        if (progress) progress(context,i+1);
    }
    return batch;
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
    return Generate(batch,index,batch->candidates[index]->revision+1,cancel,context,error,capacity);
}

bool CreatureOffspring_AcceptReroll(CreatureOffspringBatch *batch, size_t index, OffspringCandidate *result)
{
    if (!result || !batch || result->owner != batch || index >= batch->config.count ||
        result->index != index || result->revision != batch->candidates[index]->revision+1)
        return false;
    free(batch->candidates[index]);
    batch->candidates[index] = result;
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
