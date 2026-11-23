#include "OffspringRepair.h"
#include "Limb.h"
#include "AxialBody.h"
#include "HeadModule.h"
#include "FlexibleAppendage.h"
#include "MathUtils.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "Ornament.h"
#include <math.h>
#include <stdlib.h>

void OffspringRepair_Integrate(CreatureRecipe *recipe)
{
    CreaturePhenotype *stages[] = {&recipe->seed,&recipe->larva,&recipe->juvenile,&recipe->adult};
    for (size_t i=0; i<recipe->bodyPlan.moduleCount; ++i) {
        CreatureModuleInstance *module = &recipe->bodyPlan.modules[i];
        if (module->pathAttached) continue;
        if (module->kind == CREATURE_MODULE_TAIL) {
            for (unsigned age=0; age<4; ++age) {
                CreaturePhenotype *p = stages[age];
                TailPhenotype *tail = &p->tails[module->phenotypeIndex];
                float slenderness = tail->baseWidth/p->axial.pelvicWidth;
                float transition = .18f*Math_Clamp01(1-slenderness/.18f)*tail->development;
                tail->rootMatchStrength = fmaxf(tail->rootMatchStrength,transition);
            }
            continue;
        }
        const CreatureModuleInstance *host = NULL;
        for (size_t j=0; j<recipe->bodyPlan.moduleCount; ++j)
            if (recipe->bodyPlan.modules[j].instanceId == module->attachment.hostModuleInstanceId)
                host = &recipe->bodyPlan.modules[j];
        if (!host || host->kind != CREATURE_MODULE_HEAD || !recipe->adult.headHybrid.hasRegionalProfile)
            continue;
        AttachmentSlotId slot = module->attachment.slotId;
        if (slot >= HEAD_SLOT_FITTED_PROBOSCIS && slot <= HEAD_SLOT_FITTED_NASAL_POSTERIOR)
            slot -= HEAD_SLOT_FITTED_PROBOSCIS-HEAD_SLOT_PROBOSCIS;
        bool nasal = slot == HEAD_SLOT_NASAL_ANTERIOR || slot == HEAD_SLOT_NASAL_POSTERIOR;
        bool maxillary = slot == HEAD_SLOT_MAXILLARY_LEFT || slot == HEAD_SLOT_MAXILLARY_RIGHT;
        bool proboscis = module->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE && slot == HEAD_SLOT_PROBOSCIS;
        bool ornament = module->kind == CREATURE_MODULE_ORNAMENT &&
            ((nasal && recipe->adult.ornaments[module->phenotypeIndex].archetype == ORNAMENT_ARCHETYPE_HORN) ||
             (maxillary && recipe->adult.ornaments[module->phenotypeIndex].archetype == ORNAMENT_ARCHETYPE_TUSK));
        if (!proboscis && !ornament) continue;
        AttachmentSlotId fitted = slot + (HEAD_SLOT_FITTED_PROBOSCIS-HEAD_SLOT_PROBOSCIS);
        module->attachment.slotId = fitted;
        for (unsigned age=0; age<4; ++age) {
            CreaturePhenotype *p = stages[age];
            HeadAnatomy resolved;
            Vector3 envelope = {p->headEnvelope.widthScale,p->headEnvelope.heightScale,
                                p->headEnvelope.lengthScale};
            if (!HeadAnatomy_ResolveHybrid(&p->head,&p->headHybrid,0,
                    Vec3_Scale(envelope,p->headEnvelope.scale),&resolved)) continue;
            Vector3 local = HeadModule_FacialSupport(&resolved.surface,fitted);
            float section = sqrtf(local.x*local.y);
            if (proboscis) {
                FlexibleAppendagePhenotype *f = &p->flexibleAppendages[module->phenotypeIndex];
                float root = fminf(local.x*.76f,local.y*.86f/f->aspect);
                f->baseRadius = Math_Clamp(f->baseRadius,root*.78f,root);
                f->tipRadius = fminf(f->tipRadius,f->baseRadius*.51f);
                float minimum=FlexibleAppendage_MinNasalLength(f,local.y);
                f->length = Math_Clamp(f->length,minimum,
                    fmaxf(minimum,section*4.4f+resolved.surface.craniumRadii.y*.65f));
                f->initialPitch = Math_Clamp(f->initialPitch,.55f,1.18f);
            } else {
                OrnamentPhenotype *o = &p->ornaments[module->phenotypeIndex];
                /* El radio de apoyo y la longitud comparten el mismo sitio,
                 * con límites distintos para la raíz y la palanca. */
                float limit = nasal ? fminf(local.x*.61f,local.y*.78f) : section*.36f;
                float radiusScale = fminf(1,limit/o->baseRadius);
                o->baseRadius *= radiusScale;
                o->tipRadius *= radiusScale;
                o->length = fminf(o->length,nasal ? section*2.35f : section*3.05f);
                if (maxillary) {
                    o->growthDirection.x = fmaxf(o->growthDirection.x,.24f);
                    o->lateralSweep = fmaxf(o->lateralSweep,.22f);
                }
            }
        }
    }
}

static bool Finite(Vector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool Connected(const AnatomyGraph *g, AnatomyRef root)
{
    AnatomyGraphIndex index;
    if (!AnatomyGraphIndex_Build(&index,g)) return false;
    const AnatomyNode *node = AnatomyGraph_FindModuleNode(g,root.moduleInstanceId,root.localNodeId);
    if (!node) return false;
    bool reached[ANATOMY_MAX_NODES] = {0};
    reached[node-g->nodes] = true;
    size_t count = 1;
    for (bool changed=true; changed && count<g->nodeCount;) {
        changed = false;
        for (size_t i=0; i<g->connectionCount; ++i) {
            const BodyConnection *e = &g->connections[i];
            int x = AnatomyGraphIndex_Find(&index,e->fromId);
            int y = AnatomyGraphIndex_Find(&index,e->toId);
            if (x < 0 || y < 0) return false;
            if (reached[x] == reached[y]) continue;
            reached[x] = reached[y] = true;
            ++count;
            changed = true;
        }
    }
    return count == g->nodeCount;
}

static unsigned SurfaceAnchors(CreaturePhenotype *p, const AnatomyGraph *graph, bool repair)
{
    unsigned removed = 0;
    SurfacePhenotype *s = &p->surface;
    size_t count = 0;
    for (size_t i=0; i<s->longHairCount; ++i) {
        const LongHairDomain *d = &s->longHair[i].domain;
        bool valid = false;
        for (size_t j=0; j<graph->nodeCount; ++j)
            valid |= graph->nodes[j].moduleInstanceId == d->hostModuleId;
        for (size_t j=0; j<d->anchorCount; ++j)
            valid &= AnatomyGraph_FindNode(graph,d->anchors[j]) != NULL;
        if (valid && repair) s->longHair[count] = s->longHair[i];
        count += valid;
        removed += !valid;
    }
    if (repair) s->longHairCount = count;
    PigmentPhenotype *banks[] = {&s->pigment,&s->transition.from,&s->transition.to};
    unsigned bankCount = s->transition.active ? 3 : 1;
    for (unsigned bank=0; bank<bankCount; ++bank) {
        PigmentPhenotype *pigment = banks[bank];
        count = 0;
        for (size_t i=0; i<pigment->layerCount; ++i) {
            AnatomyId anchor = pigment->layers[i].domain.anchorNode;
            bool valid = !anchor || AnatomyGraph_FindNode(graph,anchor);
            if (valid && repair) pigment->layers[count] = pigment->layers[i];
            count += valid;
            removed += !valid;
        }
        if (repair) pigment->layerCount = count;
    }
    return removed;
}

static bool Clamp(float *value, float low, float high, bool repair)
{
    float v = Math_Clamp(*value,low,high);
    if (v == *value) return false;
    if (repair) *value = v;
    return true;
}

static float Sole(const AnatomyGraph *graph, uint32_t id)
{
    float sole = INFINITY;
    for (size_t i=0; i<graph->nodeCount; ++i) {
        const AnatomyNode *node = &graph->nodes[i];
        if (node->moduleInstanceId != id ||
            (node->localNodeId != LIMB_NODE_AUTOPOD && node->role != ANATOMY_ROLE_DIGIT)) continue;
        sole = fminf(sole,node->center.y-node->heightRadius);
    }
    return sole;
}

static float ReferenceFloor(const BodyPlan *plan, const CreaturePhenotype *ref, const AnatomyGraph *original)
{
    float sum = 0;
    unsigned count = 0;
    for (size_t i=0; i<plan->moduleCount; ++i) {
        const CreatureModuleInstance *m = &plan->modules[i];
        if (m->kind != CREATURE_MODULE_LIMB || m->phenotypeIndex >= ref->limbCount) continue;
        const LimbPhenotype *lp = &ref->limbs[m->phenotypeIndex];
        if (lp->role == LIMB_WING || lp->archetype == LIMB_ARCHETYPE_WING ||
            lp->archetype == LIMB_ARCHETYPE_INSECT_WING || lp->archetype == LIMB_ARCHETYPE_FLIPPER ||
            lp->archetype == LIMB_ARCHETYPE_ARTHROPOD_LEG) continue;
        float sole = Sole(original,m->instanceId);
        if (!isfinite(sole)) continue;
        sum += sole;
        ++count;
    }
    return count ? sum/count : 0;
}

static unsigned Proportions(CreaturePhenotype *p, const CreaturePhenotype *ref, const BodyPlan *plan,
    const AnatomyGraph *graph, const AnatomyGraph *original, float strength, bool repair)
{
    unsigned issues = 0;
    float trunk = p->axial.trunkLength;
    float body = p->axial.totalScale*p->axial.withersHeight;
    float originalBody = ref->axial.totalScale*ref->axial.withersHeight;
    bool upright = p->axial.archetype == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD;
    float referenceFloor = upright ? 0 : ReferenceFloor(plan,ref,original)*body/originalBody;
    bool weightBearing = false;
    bool hasMacropodHind = false;
    for (size_t i=0; i<p->limbCount; ++i) {
        weightBearing |= p->limbs[i].development*p->development.appendages >= .65f;
        if (p->limbs[i].role == LIMB_HIND && p->limbs[i].anatomyProfile == LIMB_PROFILE_MACROPOD)
            hasMacropodHind = true;
    }
    if (!p->headHybrid.active && p->head.archetype == ref->head.archetype) {
        float neckRatio = p->axial.neckWidth/ref->axial.neckWidth;
        float headScale = ref->headEnvelope.scale/sqrtf(neckRatio);
        issues += Clamp(&p->headEnvelope.scale,headScale*.70f,headScale*1.42f,repair);
    }
    for (size_t i=0; i<plan->moduleCount; ++i) {
        const CreatureModuleInstance *m = &plan->modules[i];
        unsigned at = m->phenotypeIndex;
        if (m->kind == CREATURE_MODULE_LIMB) {
            LimbPhenotype *limb = &p->limbs[at];
            const LimbPhenotype *base = &ref->limbs[at];
            if (limb->archetype == base->archetype && limb->anatomyProfile == base->anatomyProfile) {
                float scale = p->axial.withersHeight/ref->axial.withersHeight;
                float ratio = base->thickness/base->length;
                issues += Clamp(&limb->length,base->length*scale*.40f,base->length*scale*1.65f,repair);
                issues += Clamp(&limb->thickness,limb->length*ratio*.66f,limb->length*ratio*1.45f,repair);
                issues += Clamp(&limb->footScale,base->footScale*.70f,base->footScale*1.36f,repair);
            }
            /* La referencia conserva postura y fase. Se aproxima la suela
             * mediante estaciones de apoyo, con tolerancia al espesor SDF. */
            if (!m->pathAttached && limb->role != LIMB_WING &&
                limb->archetype != LIMB_ARCHETYPE_WING &&
                limb->archetype != LIMB_ARCHETYPE_INSECT_WING &&
                limb->archetype != LIMB_ARCHETYPE_FLIPPER &&
                limb->archetype != LIMB_ARCHETYPE_ARTHROPOD_LEG &&
                !(hasMacropodHind && limb->role == LIMB_FORE) &&
                limb->development*p->development.appendages >= .65f) {
                float now = Sole(graph,m->instanceId);
                const AnatomyNode *root = AnatomyGraph_FindModuleNode(graph,m->instanceId,LIMB_NODE_ROOT);
                if (!root || !isfinite(now)) continue;
                float target = referenceFloor;
                float tolerance = (upright ? .018f : .030f)*body;
                float reach = root->center.y-now;
                if (reach > .08f*body && fabsf(now-target) > tolerance) {
                    if (repair) limb->length *= 1 + strength*Math_Clamp((now-target)/reach,-.22f,.22f);
                    ++issues;
                }
            }
        } else if (m->kind == CREATURE_MODULE_TAIL) {
            TailPhenotype *tail = &p->tails[at];
            issues += Clamp(&tail->baseWidth,p->axial.pelvicWidth*.035f,p->axial.pelvicWidth*.67f,repair);
            issues += Clamp(&tail->baseHeight,p->axial.pelvicHeight*.035f,p->axial.pelvicHeight*.85f,repair);
            issues += Clamp(&tail->length,trunk*.025f,trunk*2.20f,repair);
        } else if (m->kind == CREATURE_MODULE_ORNAMENT) {
            OrnamentPhenotype *o = &p->ornaments[at];
            if (o->archetype == ORNAMENT_ARCHETYPE_HORN || o->archetype == ORNAMENT_ARCHETYPE_TUSK ||
                o->archetype == ORNAMENT_ARCHETYPE_OSSICONE) {
                issues += Clamp(&o->baseRadius,o->length*.025f,o->length*.70f,repair);
                if (p->axial.totalScale >= .45f && o->development >= .5f)
                    issues += Clamp(&o->length,
                        m->attachment.slotId >= HEAD_SLOT_FITTED_MAXILLARY_LEFT ? 0 : trunk*.025f,
                        trunk*1.3f,repair);
            }
        } else if (m->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE) {
            FlexibleAppendagePhenotype *f = &p->flexibleAppendages[at];
            bool nasal=HeadModule_IsNasalExtension(m->attachment.slotId);
            if (!nasal) issues += Clamp(&f->baseRadius,f->length*.023f,f->length*.40f,repair);
            issues += Clamp(&f->length,trunk*(nasal?.002f:.04f),trunk*1.6f,repair);
            if (weightBearing) {
                const AnatomyNode *root = AnatomyGraph_FindModuleNode(graph,m->instanceId,1);
                if (!root) continue;
                float bottom = INFINITY;
                for (size_t j=0; j<graph->nodeCount; ++j) {
                    const AnatomyNode *node = &graph->nodes[j];
                    if (node->moduleInstanceId == m->instanceId)
                        bottom = fminf(bottom,node->center.y-node->heightRadius);
                }
                if (bottom < referenceFloor+.02f*body) {
                    float reach = root->center.y-bottom;
                    float space = root->center.y-referenceFloor-.045f*body;
                    /* Mantiene la punta y su terminal como una unidad. La
                     * curva hacia delante conserva alcance sin hundirla. */
                    if (repair && reach > 0 && space > 0) {
                        f->restBend = Math_Clamp(f->restBend+.20f,-1.57f,1.57f);
                        f->length *= Math_Clamp(space/reach,.45f,.95f);
                    }
                    ++issues;
                }
            }
        }
    }
    return issues;
}

static bool RepairGeometry(CreaturePhenotype *p, const CreatureRecipe *recipe,
    const AnatomyGraph *g, unsigned errors)
{
    bool changed = false;
    if (errors & 4) {
        if (p->head.eyeExposure > .005f) { p->head.eyeExposure *= .42f; changed = true; }
        if (p->headHybrid.active && p->headHybrid.secondary.eyeExposure > .005f) {
            p->headHybrid.secondary.eyeExposure *= .42f;
            changed = true;
        }
    }
    if (errors & 8) {
        for (size_t i=0; i<p->tailCount; ++i)
            p->tails[i].rootMatchStrength = Math_Lerp(p->tails[i].rootMatchStrength,1,.5f);
        changed = p->tailCount > 0 || changed;
    }
    if (!(errors & 16) || p->axial.archetype != AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD) return changed;
    float lateral = p->axial.limbAttachmentLateral;
    float foreWidth = 1, hindWidth = 1;
    for (size_t i=0; i<g->connectionCount; ++i) {
        const BodyConnection *edge = &g->connections[i];
        const AnatomyNode *a = AnatomyGraph_FindNode(g,edge->fromId);
        const AnatomyNode *b = AnatomyGraph_FindNode(g,edge->toId);
        if (a->moduleInstanceId == b->moduleInstanceId || b->role != ANATOMY_ROLE_JOINT) continue;
        float distance = Vec3_Distance(a->center,b->center);
        if (distance <= a->widthRadius+b->widthRadius) continue;
        if (b->localNodeId == LIMB_NODE_MUSCLE_ANCHOR) {
            /* El ancla del músculo cursorial puede quedar por delante del
             * tórax estrecho. Amplía sólo la sección que la recibe. */
            float needed = (distance-b->widthRadius)/a->widthRadius;
            if (b->region == ANATOMY_REGION_FORELIMB) foreWidth = fmaxf(foreWidth,needed*1.03f);
            else hindWidth = fmaxf(hindWidth,needed*1.03f);
        } else if (b->localNodeId == LIMB_NODE_ROOT && fabsf(b->center.x)>a->widthRadius*.9f) {
            lateral = fminf(lateral,.90f);
        } else {
            for (size_t j=0; j<recipe->bodyPlan.moduleCount; ++j) {
                const CreatureModuleInstance *m = &recipe->bodyPlan.modules[j];
                if (m->kind != CREATURE_MODULE_LIMB || m->instanceId != b->moduleInstanceId) continue;
                LimbPhenotype *limb = &p->limbs[m->phenotypeIndex];
                if (limb->rootThicknessScale >= 1.4f) break;
                limb->rootThicknessScale *= 1.08f;
                changed = true;
                break;
            }
        }
    }
    if (foreWidth > 1 && foreWidth <= 1.35f) { p->axial.shoulderWidth *= foreWidth; changed = true; }
    if (hindWidth > 1 && hindWidth <= 1.35f) { p->axial.pelvicWidth *= hindWidth; changed = true; }
    if (lateral < p->axial.limbAttachmentLateral) { p->axial.limbAttachmentLateral = lateral; changed = true; }
    return changed;
}

static bool Bounds(const MonsterSDF *field)
{
    AABB3D b = MonsterSDF_GetBounds(field);
    return Finite(b.start) && Finite(b.end) && b.end.x>b.start.x &&
        b.end.y>b.start.y && b.end.z>b.start.z;
}

static bool SeparatedFeet(const AnatomyGraph *graph)
{
    for (size_t i=0; i<graph->nodeCount; ++i) {
        const AnatomyNode *a = &graph->nodes[i];
        if (a->role != ANATOMY_ROLE_JOINT || a->localNodeId != LIMB_NODE_AUTOPOD ||
            a->development < .65f) continue;
        for (size_t j=i+1; j<graph->nodeCount; ++j) {
            const AnatomyNode *b = &graph->nodes[j];
            if (b->role != ANATOMY_ROLE_JOINT || b->localNodeId != LIMB_NODE_AUTOPOD ||
                b->development < .65f || a->moduleInstanceId == b->moduleInstanceId) continue;
            if (Vec3_Distance(a->center,b->center) < .54f*(a->widthRadius+b->widthRadius)) return false;
        }
    }
    return true;
}

static bool SupportedSeams(const MonsterSDF *field, const AnatomyGraph *g, float scale)
{
    for (size_t i=0; i<g->connectionCount; ++i) {
        const BodyConnection *edge = &g->connections[i];
        const AnatomyNode *a = AnatomyGraph_FindNode(g,edge->fromId);
        const AnatomyNode *b = AnatomyGraph_FindNode(g,edge->toId);
        if (g->dormantConnections[i] || a->moduleInstanceId == b->moduleInstanceId ||
            !(b->region == ANATOMY_REGION_TAIL ||
              ((b->region == ANATOMY_REGION_FORELIMB || b->region == ANATOMY_REGION_HINDLIMB) &&
                b->localNodeId == LIMB_NODE_ROOT))) continue;
        for (unsigned j=1; j<=4; ++j) {
            float t = (float)j/5;
            float distance = MonsterSDF_Evaluate(field,Vec3_Lerp(a->center,b->center,t)).distance;
            if (!isfinite(distance) || distance > fmaxf(.002f*scale,b->widthRadius*.12f)) return false;
        }
    }
    return true;
}

static bool SupportedHead(MonsterSDF *field, const Monster *monster, bool adult)
{
    if (!monster->hasHead || !monster->head.hybrid.hasRegionalProfile) return true;
    const HeadAnatomy *head=&monster->head.anatomy;
    if (!field->mouthCount || !HeadAnatomy_ValidateProfile(head)) return false;
    const HeadSurfaceRecipe *s=&head->surface;
    MonsterSDFHeadField context;
    SDFField core=MonsterSDF_GetHeadField(field,0,&context);
    Vector3 origin=monster->bodyParts[head->attachmentBodyPartIndex].positionRender;
    if (monster->head.development>=.65f) {
        /* Se busca tejido justo detrás del globo, dentro de la copa orbital.
         * La envolvente craneal por sí sola no detecta un ojo suspendido. */
        for (size_t i=0; i<monster->eyeCount; ++i) {
            const Eye *eye=&monster->eyes[i];
            if (eye->scale.z<=0) continue;
            float radius=fmaxf(eye->scale.x,eye->scale.z);
            Vector3 seat=Vec3_Sub(eye->offset,Vec3_Scale(eye->forward,radius*.90f));
            float distance=core.evaluateDistance(core.context,Vec3_Add(origin,seat));
            if (!isfinite(distance) || distance>radius*.20f) return false;
            /* Una cavidad interna también da distancia positiva. La abertura
             * debe comunicar con el exterior en la dirección de la mirada. */
            unsigned samples=adult ? 5 : 1;
            for (unsigned sample=0; sample<samples; ++sample) {
                float depth=eye->scale.z*.90f+radius*sample*.55f;
                Vector3 front=Vec3_Add(eye->offset,Vec3_Scale(eye->forward,depth));
                float exposed=core.evaluateDistance(core.context,Vec3_Add(origin,front));
                if (!isfinite(exposed) || exposed<-radius*.10f) return false;
            }
        }
    }
    MonsterSDFMouth *mouth=&field->mouths[0];
    if (mouth->hasNasalPad) {
        mouth->hasNasalPad=false;
        float distance=core.evaluateDistance(core.context,Vec3_Add(origin,s->noseCenter));
        mouth->hasNasalPad=true;
        if (!isfinite(distance) || distance>fminf(s->noseRadii.x,s->noseRadii.y)*.60f) return false;
    }
    if (!mouth->hasEars) return true;
    mouth->hasEars=false;
    float left=core.evaluateDistance(core.context,Vec3_Add(origin,head->landmarks.leftEarBase));
    float right=core.evaluateDistance(core.context,Vec3_Add(origin,head->landmarks.rightEarBase));
    mouth->hasEars=true;
    /* La raíz debe tocar el cráneo aun al retirar ambos pabellones del campo. */
    float tolerance=fmaxf(s->earShape.z*.70f,s->craniumRadii.y*.04f);
    return isfinite(left) && isfinite(right) && fmaxf(left,right)<tolerance;
}

static bool FacialRoots(const CreatureRecipe *recipe, const Monster *monster,
    const MonsterSDF *field, float *penalty)
{
    const AnatomyGraph *graph = &monster->anatomyGraph;
    for (size_t i=0; i<recipe->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance *m = &recipe->bodyPlan.modules[i];
        AttachmentSlotId slot = m->attachment.slotId;
        if (m->pathAttached || slot < HEAD_SLOT_FITTED_PROBOSCIS ||
            slot > HEAD_SLOT_FITTED_NASAL_POSTERIOR) continue;
        bool flexible = m->kind == CREATURE_MODULE_FLEXIBLE_APPENDAGE;
        if (!flexible && (m->kind != CREATURE_MODULE_ORNAMENT ||
            monster->phenotype.ornaments[m->phenotypeIndex].development < .15f)) continue;
        const AnatomyNode *root = AnatomyGraph_FindModuleNode(graph,m->instanceId,1);
        if (!root) return false;
        float radius = fminf(root->widthRadius,root->heightRadius);
        float depth = MonsterSDF_EvaluateDebug(field,root->center,MONSTER_HEAD_DEBUG_UPPER_HEAD).distance;
        /* Una unión suave no puede suplir un hueco mayor que el propio
         * radio. La penalización distingue raíces apenas solapadas. */
        if (!isfinite(depth) || depth > radius*.66f) return false;
        *penalty = fmaxf(*penalty,Math_Clamp01(.20f+depth/radius));
    }
    return true;
}

static float ProfilePenalty(const Monster *m)
{
    float penalty = 0;
    if (m->hasHead && m->head.hybrid.hasRegionalProfile) {
        const HeadSurfaceRecipe *s = &m->head.anatomy.surface;
        for (unsigned i=1; i<5; ++i) {
            float left = s->mandibularRadii[i-1].x;
            float right = s->mandibularRadii[i+1].x;
            float width = s->mandibularRadii[i].x;
            float ratio = width/sqrtf(left*right);
            penalty = fmaxf(penalty,.35f*Math_Clamp01((fabsf(logf(ratio))-.28f)*1.5f));
        }
    }
    if (m->phenotype.axial.archetype == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD) {
        float low = INFINITY, high = -INFINITY;
        const BodyPlan *plan = &m->recipe.bodyPlan;
        for (size_t i=0; i<plan->moduleCount; ++i) {
            const CreatureModuleInstance *module = &plan->modules[i];
            if (module->kind != CREATURE_MODULE_LIMB || module->phenotypeIndex >= m->phenotype.limbCount) continue;
            const LimbPhenotype *lp = &m->phenotype.limbs[module->phenotypeIndex];
            if (lp->role == LIMB_WING || lp->archetype == LIMB_ARCHETYPE_WING ||
                lp->archetype == LIMB_ARCHETYPE_INSECT_WING || lp->archetype == LIMB_ARCHETYPE_FLIPPER ||
                lp->archetype == LIMB_ARCHETYPE_ARTHROPOD_LEG ||
                (m->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP && lp->role == LIMB_FORE)) continue;
            float sole = Sole(&m->anatomyGraph,module->instanceId);
            if (!isfinite(sole)) continue;
            low = fminf(low,sole);
            high = fmaxf(high,sole);
        }
        float span = (high-low)/m->phenotype.axial.totalScale;
        penalty = fmaxf(penalty,.35f*Math_Clamp01((span-.025f)*4));
    }
    return penalty;
}

bool OffspringRepair_Validate(CreatureRecipe *recipe, const CreatureRecipe *reference,
    const AnatomyGraph referenceStages[4], float strength, bool repair, OffspringReport *report, CreatureOffspringCancel cancel, void *context,
    char *error, size_t capacity)
{
    Monster *monster = calloc(1,sizeof(*monster));
    AnatomyGraph *initial = malloc(sizeof(*initial));
    if (!monster || !initial) {
        free(monster);
        free(initial);
        snprintf(error,capacity,"Cannot allocate anatomy validation");
        return false;
    }
    CreaturePhenotype *stages[] = {&recipe->seed,&recipe->larva,&recipe->juvenile,&recipe->adult};
    bool valid = false, corrected = false;
    float coherencePenalty = 0;
    for (unsigned i=0; i<7; ++i) {
        if (cancel && cancel(context)) { snprintf(error,capacity,"Generation cancelled"); goto done; }
        if (i==4 && corrected) goto done;
        CreaturePhenotype middle;
        CreaturePhenotype *p;
        if (i<4) {
            p = stages[i];
            if (i && p->axial.totalScale < stages[i-1]->axial.totalScale) {
                if (!repair) { snprintf(error,capacity,"Non-monotone size at stage %u",i); goto done; }
                p->axial.totalScale = stages[i-1]->axial.totalScale;
                corrected = true;
            }
            CreaturePhenotype_Normalize(p);
            if (i && !CreaturePhenotype_MorphCompatible(stages[0],p)) {
                snprintf(error,capacity,"Development changes structural traits at stage %u",i);
                goto done;
            }
        } else {
            middle = CreatureRecipe_EvaluateDevelopment(recipe,((float)i-3.5f)/3);
            p = &middle;
        }
        if (!Creature_BuildMonster(monster,recipe,p) || !Connected(&monster->anatomyGraph,recipe->bodyPlan.root)) {
            snprintf(error,capacity,"Unresolved or disconnected anatomy at sample %u",i);
            goto done;
        }
        if (!i) *initial = monster->anatomyGraph;
        else if (!AnatomyGraph_TopologyCompatible(initial,&monster->anatomyGraph)) {
            snprintf(error,capacity,"Anatomical identities change at age sample %u",i);
            goto done;
        }
        if (i<4) {
            const CreaturePhenotype *ref = CreatureRecipe_GetStage(reference,i);
            unsigned anchors = SurfaceAnchors(p,&monster->anatomyGraph,repair);
            unsigned issues = Proportions(p,ref,&recipe->bodyPlan,&monster->anatomyGraph,&referenceStages[i],strength,repair);
            if (anchors || issues) {
                snprintf(error,capacity,"Anatomical constraints at stage %u (%u proportions, %u anchors)",i,issues,anchors);
                if (!repair) goto done;
                report->omitted += anchors;
                report->removedAnchors += anchors;
                corrected = true;
                continue;
            }
        }
        unsigned errors = Creature_ValidateGeometry(monster);
        if (errors) {
            snprintf(error,capacity,"Geometry validation mask %u at sample %u",errors,i);
            if (!repair) goto done;
            if (i<4) {
                if (!RepairGeometry(p,recipe,&monster->anatomyGraph,errors)) goto done;
            } else {
                bool left = RepairGeometry(stages[i-4],recipe,&monster->anatomyGraph,errors);
                bool right = RepairGeometry(stages[i-3],recipe,&monster->anatomyGraph,errors);
                if (!left && !right) goto done;
            }
            corrected = true;
            continue;
        }
        if (!SeparatedFeet(&monster->anatomyGraph)) {
            snprintf(error,capacity,"Overlapping feet at age sample %u",i);
            goto done;
        }
        MonsterSDF field = MonsterSDF_Create();
        bool built = MonsterSDF_Build(&field,monster,MonsterSDF_DefaultConfig()) && Bounds(&field);
        bool seams = built && SupportedSeams(&field,&monster->anatomyGraph,p->axial.totalScale);
        bool head = built && SupportedHead(&field,monster,i==CREATURE_STAGE_ADULT);
        bool roots = built && FacialRoots(recipe,monster,&field,&coherencePenalty);
        MonsterSDF_Free(&field);
        if (!built || !seams || !head || !roots) {
            snprintf(error,capacity,"%s at age sample %u",
                !built ? "Invalid SDF bounds" : !head ? "Unsupported regional head attachment" :
                !roots ? "Unsupported facial appendage root" : "Gap at a limb or tail attachment",i);
            goto done;
        }
        if (i == CREATURE_STAGE_ADULT) {
            report->signature = Creature_MorphologicalSignature(monster);
            coherencePenalty = fmaxf(coherencePenalty,ProfilePenalty(monster));
        }
    }
    valid = !corrected;
    if (valid) report->anatomicalCoherence = 1-coherencePenalty;
done:
    report->repairs = corrected;
    Monster_Free(monster);
    free(monster);
    free(initial);
    return valid;
}
