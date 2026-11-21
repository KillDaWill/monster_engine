/**
 * @file HeadModule.c
 * @brief Implementación del módulo cefálico.
 * @author Monster Engine Team
 * @date 2026
 */

#include "HeadModule.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include "SDFPrimitives.h"
#include <math.h>

Vector3 HeadModule_NasalRoot(const HeadSurfaceRecipe* s)
{
    Vector3 tip=s->hasCranialProfile?s->cranialCenters[0]:s->faceTip;
    Vector3 mid=s->hasCranialProfile?s->cranialCenters[1]:s->faceMid;
    Vector3 support=HeadModule_FacialSupport(s,HEAD_SLOT_FITTED_PROBOSCIS);
    Vector3 root=Vec3_Lerp(tip,mid,.16f);
    root.y+=support.y*.16f;
    return root;
}

Vector3 HeadModule_FacialSupport(const HeadSurfaceRecipe *s, AttachmentSlotId id)
{
    switch (id) {
    case HEAD_SLOT_FITTED_NASAL_ANTERIOR:
        return s->hasCranialProfile ? Vec3_Lerp(s->cranialRadii[0],s->cranialRadii[1],.23f) :
            Vec3_Lerp(s->faceTipRadii,s->faceMidRadii,.23f);
    case HEAD_SLOT_FITTED_NASAL_POSTERIOR:
        return s->hasCranialProfile ? s->cranialRadii[1] : s->faceMidRadii;
    case HEAD_SLOT_FITTED_PROBOSCIS:
        return s->hasCranialProfile ? Vec3_Lerp(s->cranialRadii[0],s->cranialRadii[1],.25f) :
            Vec3_Lerp(s->faceTipRadii,s->faceMidRadii,.25f);
    case HEAD_SLOT_FITTED_MAXILLARY_LEFT:
    case HEAD_SLOT_FITTED_MAXILLARY_RIGHT:
        return Vec3_Lerp(s->faceTipRadii,s->maxillaryRadii,.25f);
    default:
        return s->craniumRadii;
    }
}

static float FacialSupportField(const HeadSurfaceRecipe *s, const SDFSweepStation stations[6],
    bool sweep, Vector3 point)
{
    if (sweep) return SDF_EllipticalSweepZ(point,stations,6);
    float tip = SDF_Ellipsoid(Vec3_Sub(point,s->faceTip),s->faceTipRadii);
    float mid = SDF_Ellipsoid(Vec3_Sub(point,s->faceMid),s->faceMidRadii);
    return fminf(tip,mid);
}

static void FitFacialSlot(AttachmentSlot *site, const HeadSurfaceRecipe *s, Vector3 origin,
    const SDFSweepStation stations[6], bool sweep)
{
    Vector3 support = HeadModule_FacialSupport(s,site->id);
    Vector3 interior = s->faceTip, outward = {0,0,1};
    float span = fmaxf(s->faceTip.z-s->faceRoot.z,s->faceTipRadii.z);
    float distance = span;
    if (site->id >= HEAD_SLOT_FITTED_NASAL_ANTERIOR) {
        Vector3 tip = sweep ? s->cranialCenters[0] : s->faceTip;
        Vector3 mid = sweep ? s->cranialCenters[1] : s->faceMid;
        interior = site->id == HEAD_SLOT_FITTED_NASAL_ANTERIOR ? Vec3_Lerp(tip,mid,.23f) : mid;
        outward = Vec3_Create(0,1,0);
        distance = support.y*1.7f;
    } else {
        float sign = site->id == HEAD_SLOT_FITTED_MAXILLARY_LEFT ? 1 : -1;
        interior = Vec3_Lerp(s->faceTip,s->faceMid,.32f);
        interior.x = sign*s->faceTipRadii.x*.74f;
        interior.y -= s->faceTipRadii.y*.48f;
        outward = Vec3_Normalize(Vec3_Create(sign*.28f,-.18f,1));
    }

    if (FacialSupportField(s,stations,sweep,interior) >= 0) return;
    Vector3 exterior = Vec3_Add(interior,Vec3_Scale(outward,distance));
    if (FacialSupportField(s,stations,sweep,exterior) <= 0) return;
    for (unsigned i=0; i<10; ++i) {
        Vector3 midpoint = Vec3_Lerp(interior,exterior,.5f);
        if (FacialSupportField(s,stations,sweep,midpoint) <= 0) interior=midpoint;
        else exterior=midpoint;
    }
    if (site->id >= HEAD_SLOT_FITTED_NASAL_ANTERIOR) {
        float h = support.y*.025f;
        Vector3 dy = Vec3_Create(0,h,0), dz = Vec3_Create(0,0,h);
        float vertical = FacialSupportField(s,stations,sweep,Vec3_Add(interior,dy)) -
                         FacialSupportField(s,stations,sweep,Vec3_Sub(interior,dy));
        float depth = FacialSupportField(s,stations,sweep,Vec3_Add(interior,dz)) -
                      FacialSupportField(s,stations,sweep,Vec3_Sub(interior,dz));
        if (vertical > 1e-6f) {
            site->up = Vec3_Normalize(Vec3_Create(0,1,Math_Clamp(depth/vertical*.48f,-.32f,.32f)));
            site->forward = Vec3_Normalize(Vec3_Cross(site->side,site->up));
        }
    } else {
        /* La sección inicial del barrido queda parcialmente dentro del
         * hocico. El premáxilar recibe los incisivos a ambos lados del eje. */
        float penetration = support.y*.36f;
        interior = Vec3_Sub(interior,Vec3_Scale(outward,penetration));
    }
    site->position = Vec3_Add(origin,interior);
    site->hostRadii = support;
}

bool HeadModule_ResolveProfileWithSites(const HeadPhenotype* phenotype,
                        const HeadEnvelopePhenotype* envelope,
                        const HeadHybridProfile* hybrid,
                        float development,
                        uint32_t module,
                        const AttachmentSlot* slot,
                        AnatomyGraph* graph,
                        AttachmentSlotSet* slots,
                        unsigned requestedSites) {
    if (!phenotype || !slot || !graph || !Anatomy_MakeId(module, HEAD_NODE_MAIN)) return false;

    float s = slot->scale;
    float d = development;
    float t = d * d * (3.0f - 2.0f * d);

    /* Resolver una sola envolvente y colocar la unión cervical en su anclaje. */
    Vector3 radii = Vec3_Create(
        Math_Lerp(slot->hostRadii.x * 1.5f, 1.025f * s, t),
        Math_Lerp(slot->hostRadii.y * 1.847825f, .60f * s, t),
        Math_Lerp(slot->hostRadii.x * 1.10f, 1.10f * s, t));
    if (hybrid && hybrid->active) radii = Vec3_Create(s,s,s);
    if (envelope) {
        radii.x *= envelope->scale * envelope->widthScale;
        radii.y *= envelope->scale * envelope->heightScale;
        radii.z *= envelope->scale * envelope->lengthScale;
    }
    HeadAnatomy anatomy;
    if (!HeadAnatomy_ResolveHybrid(phenotype,hybrid,0,radii,&anatomy)) return false;
    Vector3 c = Vec3_Sub(slot->position, anatomy.landmarks.neckAttachment);
    Vector3 skull = Vec3_Add(c, anatomy.surface.craniumCenter);
    float w = anatomy.surface.craniumRadii.x;
    float h = anatomy.surface.craniumRadii.y;
    float depth = anatomy.surface.craniumRadii.z;

    AnatomyId headId = Anatomy_MakeId(module, HEAD_NODE_MAIN);
    if (!Module_Node(graph, module, HEAD_NODE_MAIN, c, w, h, 3,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_HEAD, ANATOMY_SIDE_CENTER, d) ||
        !Module_Edge(graph, module, 1, headId, slot->hostNode, BODY_CONNECTION_AXIAL_LOFT, d)) {
        return false;
    }

    if (hybrid && hybrid->active) {
        Vector3 ratio = HeadHybridProfile_EnvelopeRatio(hybrid);
        radii = Vec3_Create(radii.x*ratio.x,radii.y*ratio.y,radii.z*ratio.z);
    }
    graph->nodes[graph->nodeCount - 1].envelopeRadii = radii;

    if (slots) {
        AttachmentSlot cranial = {
            .id = HEAD_SLOT_CRANIAL_DORSAL,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Scale(slot->up, h * .95f)),
            .forward = slot->forward,
            .up = slot->up,
            .side = slot->side,
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = cranial;

        Vector3 occUpL = Vec3_Normalize(Vec3_Create(0.12f, 0.65f, -0.75f));
        Vector3 occUpR = Vec3_Normalize(Vec3_Create(-0.12f, 0.65f, -0.75f));
        AttachmentSlot hornL = {
            .id = HEAD_SLOT_HORN_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.72f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.75f),
                                                          Vec3_Scale(slot->forward, -depth * 0.42f)))),
            .forward = slot->forward,
            .up = occUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, occUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornL;

        AttachmentSlot hornR = {
            .id = HEAD_SLOT_HORN_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.72f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.75f),
                                                          Vec3_Scale(slot->forward, -depth * 0.42f)))),
            .forward = slot->forward,
            .up = occUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, occUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornR;

        /* Ranuras occipitales específicas para coronas craneales (orientadas principalmente hacia atrás) */
        Vector3 crownOccUpL = Vec3_Normalize(Vec3_Create(0.12f, 0.28f, -0.94f));
        Vector3 crownOccUpR = Vec3_Normalize(Vec3_Create(-0.12f, 0.28f, -0.94f));
        AttachmentSlot crownOccL = {
            .id = HEAD_SLOT_OCCIPITAL_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.32f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.68f),
                                                          Vec3_Scale(slot->forward, -depth * 0.44f)))),
            .forward = slot->forward,
            .up = crownOccUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, crownOccUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = crownOccL;

        AttachmentSlot crownOccR = {
            .id = HEAD_SLOT_OCCIPITAL_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.32f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.68f),
                                                          Vec3_Scale(slot->forward, -depth * 0.44f)))),
            .forward = slot->forward,
            .up = crownOccUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, crownOccUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = crownOccR;

        Vector3 plUpL = Vec3_Normalize(Vec3_Create(0.48f, 0.48f, -0.73f));
        Vector3 plUpR = Vec3_Normalize(Vec3_Create(-0.48f, 0.48f, -0.73f));
        AttachmentSlot hornPLL = {
            .id = HEAD_SLOT_POSTEROLATERAL_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.68f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.58f),
                                                          Vec3_Scale(slot->forward, -depth * 0.35f)))),
            .forward = slot->forward,
            .up = plUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, plUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornPLL;

        AttachmentSlot hornPLR = {
            .id = HEAD_SLOT_POSTEROLATERAL_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.68f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.58f),
                                                          Vec3_Scale(slot->forward, -depth * 0.35f)))),
            .forward = slot->forward,
            .up = plUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, plUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornPLR;

        Vector3 tempUpL = Vec3_Normalize(Vec3_Create(0.88f, 0.22f, -0.42f));
        Vector3 tempUpR = Vec3_Normalize(Vec3_Create(-0.88f, 0.22f, -0.42f));
        AttachmentSlot hornTL = {
            .id = HEAD_SLOT_TEMPORAL_LEFT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, w * 0.95f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.25f),
                                                          Vec3_Scale(slot->forward, -depth * 0.18f)))),
            .forward = slot->forward,
            .up = tempUpL,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, tempUpL)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornTL;

        AttachmentSlot hornTR = {
            .id = HEAD_SLOT_TEMPORAL_RIGHT,
            .role = ATTACHMENT_CRANIAL,
            .hostModuleInstanceId = module,
            .hostNode = headId,
            .position = Vec3_Add(skull, Vec3_Add(Vec3_Scale(slot->side, -w * 0.95f),
                                                 Vec3_Add(Vec3_Scale(slot->up, h * 0.25f),
                                                          Vec3_Scale(slot->forward, -depth * 0.18f)))),
            .forward = slot->forward,
            .up = tempUpR,
            .side = Vec3_Normalize(Vec3_Cross(slot->forward, tempUpR)),
            .hostRadii = Vec3_Create(w, h, depth),
            .scale = s
        };
        if (slots->count < ATTACHMENT_MAX_SLOTS) slots->slots[slots->count++] = hornTR;
        if (phenotype->archetype == HEAD_ARCHETYPE_PROBOSCIDEAN) {
            if (slots->count+2 > ATTACHMENT_MAX_SLOTS) return false;
            AttachmentSlot facial=cranial;
            facial.id=HEAD_SLOT_MAXILLARY_LEFT;
            facial.position=Vec3_Add(c,Vec3_Add(anatomy.surface.leftMaxillaryCenter,
                Vec3_Create(w*.04f,-h*.06f,depth*.23f)));
            slots->slots[slots->count++]=facial;
            facial.id=HEAD_SLOT_MAXILLARY_RIGHT;
            facial.position=Vec3_Add(c,Vec3_Add(anatomy.surface.rightMaxillaryCenter,
                Vec3_Create(-w*.04f,-h*.06f,depth*.23f)));
            facial.side=Vec3_Scale(facial.side,-1);
            slots->slots[slots->count++]=facial;
        }
        if (slots->count+2 > ATTACHMENT_MAX_SLOTS) return false;
        const HeadSurfaceRecipe* surface=&anatomy.surface;
        const AttachmentSlotId nasalSlots[]={HEAD_SLOT_PROBOSCIS,HEAD_SLOT_FITTED_PROBOSCIS};
        for (unsigned i=0; i<2; ++i) {
            if (i ? !(requestedSites & 1u) : (!(requestedSites & HEAD_SITE_LEGACY_PROBOSCIS) &&
                phenotype->archetype!=HEAD_ARCHETYPE_PROBOSCIDEAN)) continue;
            if (slots->count>=ATTACHMENT_MAX_SLOTS) return false;
            AttachmentSlot nasal=cranial;
            nasal.id=nasalSlots[i];
            nasal.position=Vec3_Add(c,HeadModule_NasalRoot(surface));
            nasal.hostRadii=HeadModule_FacialSupport(surface,HEAD_SLOT_FITTED_PROBOSCIS);
            slots->slots[slots->count++]=nasal;
        }
        Vector3 tip=surface->faceTip, mid=surface->faceMid;
        tip.y+=surface->faceTipRadii.y;
        mid.y+=surface->faceMidRadii.y;
        if (surface->hasCranialProfile) {
            tip=surface->cranialCenters[0];
            mid=surface->cranialCenters[1];
            tip.y+=surface->cranialRadii[0].y;
            mid.y+=surface->cranialRadii[1].y;
        }
        for (unsigned i=0;i<2;++i) {
            AttachmentSlot nasal=cranial;
            nasal.id=i==0?HEAD_SLOT_NASAL_ANTERIOR:HEAD_SLOT_NASAL_POSTERIOR;
            Vector3 landmark=i==0?Vec3_Lerp(tip,mid,.17f):mid;
            nasal.position=Vec3_Add(c,landmark);
            slots->slots[slots->count++]=nasal;
        }
        /* Los apéndices del ápice comparten la misma pinna resuelta al SDF. */
        for(unsigned side=0;side<2;++side) {
            const HeadSurfaceRecipe* r=&anatomy.surface;
            Vector3 up=side?r->rightEarDirection:r->earDirection;
            Vector3 normal=side?r->rightEarNormal:r->leftEarNormal;
            Vector3 base=side?r->rightEarCenter:r->leftEarCenter;
            AttachmentSlot tip={.id=side?HEAD_SLOT_PINNA_RIGHT:HEAD_SLOT_PINNA_LEFT,
                .role=ATTACHMENT_CRANIAL,.hostModuleInstanceId=module,.hostNode=headId,
                .position=Vec3_Add(c,Vec3_Add(base,Vec3_Add(Vec3_Scale(up,r->earShape.y*.46f),
                    Vec3_Scale(normal,r->earLongitudinalCurve+r->earFold)))),
                .up=up,.forward=normal,.side=side?r->rightEarSide:r->leftEarSide,
                .scale=s,.hostRadii=r->earShape};
            if(slots->count>=ATTACHMENT_MAX_SLOTS)return false;
            slots->slots[slots->count++]=tip;
        }
        if (requestedSites && hybrid && hybrid->hasRegionalProfile) {
            SDFSweepStation stations[6];
            for (unsigned i=0; i<6; ++i) {
                stations[i] = (SDFSweepStation){.center=surface->cranialCenters[i],
                    .width=surface->cranialRadii[i].x,.height=surface->cranialRadii[i].y};
            }
            bool sweep = surface->sweptSkull && SDF_SweepResolveTangents(stations,6);
            for (AttachmentSlotId legacy=HEAD_SLOT_MAXILLARY_LEFT; legacy<=HEAD_SLOT_NASAL_POSTERIOR; ++legacy) {
                if (!(requestedSites & (1u << (legacy-HEAD_SLOT_PROBOSCIS)))) continue;
                const AttachmentSlot *original = AttachmentSlotSet_Find(slots,module,legacy);
                if (!original) continue;
                if (slots->count >= ATTACHMENT_MAX_SLOTS) return false;
                AttachmentSlot fitted = *original;
                fitted.id = legacy + (HEAD_SLOT_FITTED_PROBOSCIS-HEAD_SLOT_PROBOSCIS);
                FitFacialSlot(&fitted,surface,c,stations,sweep);
                slots->slots[slots->count++] = fitted;
            }
        }
    }

    return true;
}

bool HeadModule_ResolveProfile(const HeadPhenotype *phenotype, const HeadEnvelopePhenotype *envelope,
    const HeadHybridProfile *hybrid, float development, uint32_t module, const AttachmentSlot *slot,
    AnatomyGraph *graph, AttachmentSlotSet *slots)
{
    return HeadModule_ResolveProfileWithSites(phenotype,envelope,hybrid,development,module,slot,
        graph,slots,0);
}

bool HeadModule_Resolve(const HeadPhenotype *phenotype,const HeadEnvelopePhenotype *envelope,
    float development,uint32_t module,const AttachmentSlot *slot,AnatomyGraph *graph,AttachmentSlotSet *slots)
{
    return HeadModule_ResolveProfile(phenotype,envelope,NULL,development,module,slot,graph,slots);
}
