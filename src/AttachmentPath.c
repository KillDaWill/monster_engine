/**
 * @file AttachmentPath.c
 * @brief Implementación de rutas de anclaje anatómico y distribución de arreglos de ornamentos.
 * @author Monster Engine Team
 * @date 2026
 */

#include "AttachmentPath.h"
#include "AxialBody.h"
#include "AxialBodySprawlingTetrapod.h"
#include "MathUtils.h"
#include <math.h>
#include <string.h>

AttachmentPath AttachmentPath_FromAxialDorsal(const AnatomyGraph* graph, uint32_t axialModuleId) {
    AttachmentPath path;
    memset(&path, 0, sizeof(path));

    const uint16_t localNodes[] = {
        AXIAL_NODE_NECK,
        AXIAL_NODE_PECTORAL,
        AXIAL_NODE_THORAX_ANTERIOR,
        AXIAL_NODE_THORAX_POSTERIOR,
        AXIAL_NODE_ABDOMEN,
        AXIAL_NODE_PELVIS
    };
    size_t targetCount = sizeof(localNodes) / sizeof(localNodes[0]);

    for (size_t i = 0; i < targetCount; ++i) {
        const AnatomyNode* node = graph ? AnatomyGraph_FindModuleNode(graph, axialModuleId, localNodes[i]) : NULL;
        AttachmentPathPoint pt;
        memset(&pt, 0, sizeof(pt));

        if (node) {
            pt.node = node->id;
            pt.position = Vec3_Add(node->center, Vec3_Create(0.0f, node->heightRadius, 0.0f));
            pt.normal = Vec3_Create(0.0f, 1.0f, 0.0f);
            pt.hostRadius = (node->widthRadius + node->heightRadius) * 0.5f;
        } else {
            return (AttachmentPath){0};
        }
        path.points[path.count++] = pt;
    }

    /* Calcular tangentes y longitudes acumuladas */
    float accumulated = 0.0f;
    path.points[0].t = 0.0f;
    for (size_t i = 0; i < path.count; ++i) {
        if (i + 1 < path.count) {
            Vector3 delta = Vec3_Sub(path.points[i + 1].position, path.points[i].position);
            float dist = Vec3_Length(delta);
            accumulated += dist;
            path.points[i].tangent = dist > 1e-4f ? Vec3_Scale(delta, 1.0f / dist) : Vec3_Create(0, 0, -1);
        } else if (i > 0) {
            path.points[i].tangent = path.points[i - 1].tangent;
        } else {
            path.points[i].tangent = Vec3_Create(0, 0, -1);
        }
    }
    path.totalLength = accumulated > 1e-4f ? accumulated : 1.0f;

    float currentDist = 0.0f;
    for (size_t i = 1; i < path.count; ++i) {
        float segDist = Vec3_Distance(path.points[i].position, path.points[i - 1].position);
        currentDist += segDist;
        path.points[i].t = Math_Clamp01(currentDist / path.totalLength);
    }
    path.points[path.count - 1].t = 1.0f;

    return path;
}

AttachmentPath AttachmentPath_FromAxialLateral(const AnatomyGraph* graph, uint32_t axialModuleId, bool leftSide) {
    AttachmentPath path;
    memset(&path, 0, sizeof(path));

    const uint16_t localNodes[] = {
        AXIAL_NODE_PECTORAL,
        AXIAL_NODE_THORAX_ANTERIOR,
        AXIAL_NODE_THORAX_POSTERIOR,
        AXIAL_NODE_ABDOMEN,
        AXIAL_NODE_PELVIS
    };
    size_t targetCount = sizeof(localNodes) / sizeof(localNodes[0]);
    float side = leftSide ? 1.0f : -1.0f;

    for (size_t i = 0; i < targetCount; ++i) {
        const AnatomyNode* node = graph ? AnatomyGraph_FindModuleNode(graph, axialModuleId, localNodes[i]) : NULL;
        AttachmentPathPoint pt;
        memset(&pt, 0, sizeof(pt));

        if (node) {
            pt.node = node->id;
            pt.position = Vec3_Add(node->center, Vec3_Create(side * node->widthRadius, 0.0f, 0.0f));
            pt.normal = Vec3_Create(side, 0.0f, 0.0f);
            pt.hostRadius = node->widthRadius;
        } else {
            return (AttachmentPath){0};
        }
        path.points[path.count++] = pt;
    }

    float accumulated = 0.0f;
    path.points[0].t = 0.0f;
    for (size_t i = 0; i < path.count; ++i) {
        if (i + 1 < path.count) {
            Vector3 delta = Vec3_Sub(path.points[i + 1].position, path.points[i].position);
            float dist = Vec3_Length(delta);
            accumulated += dist;
            path.points[i].tangent = dist > 1e-4f ? Vec3_Scale(delta, 1.0f / dist) : Vec3_Create(0, 0, -1);
        } else if (i > 0) {
            path.points[i].tangent = path.points[i - 1].tangent;
        } else {
            path.points[i].tangent = Vec3_Create(0, 0, -1);
        }
    }
    path.totalLength = accumulated > 1e-4f ? accumulated : 1.0f;

    float currentDist = 0.0f;
    for (size_t i = 1; i < path.count; ++i) {
        float segDist = Vec3_Distance(path.points[i].position, path.points[i - 1].position);
        currentDist += segDist;
        path.points[i].t = Math_Clamp01(currentDist / path.totalLength);
    }
    path.points[path.count - 1].t = 1.0f;

    return path;
}

AttachmentSlot AttachmentPath_Sample(
    const AttachmentPath* path,
    float t,
    AttachmentSlotId slotId,
    uint32_t hostModuleId) {
    AttachmentSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = slotId;
    slot.role = ATTACHMENT_DORSAL;
    slot.hostModuleInstanceId = hostModuleId;
    slot.scale = 1.0f;

    if (!path || path->count == 0) {
        slot.position = Vec3_Zero();
        slot.up = Vec3_Create(0, 1, 0);
        slot.forward = Vec3_Create(0, 0, -1);
        slot.side = Vec3_Create(1, 0, 0);
        return slot;
    }

    float clampedT = Math_Clamp01(t);
    size_t idx = 0;
    while (idx + 1 < path->count && path->points[idx + 1].t < clampedT) {
        ++idx;
    }

    if (idx + 1 >= path->count) {
        const AttachmentPathPoint* pt = &path->points[path->count - 1];
        slot.hostNode = pt->node;
        slot.position = pt->position;
        slot.up = pt->normal;
        slot.forward = pt->tangent;
        slot.side = Vec3_Normalize(Vec3_Cross(slot.forward, slot.up));
        slot.hostRadii = Vec3_Create(pt->hostRadius, pt->hostRadius, pt->hostRadius);
        return slot;
    }

    const AttachmentPathPoint* p0 = &path->points[idx];
    const AttachmentPathPoint* p1 = &path->points[idx + 1];
    float segSpan = p1->t - p0->t;
    float u = (segSpan > 1e-5f) ? (clampedT - p0->t) / segSpan : 0.0f;

    slot.hostNode = (u < 0.5f) ? p0->node : p1->node;
    slot.position = Vec3_Lerp(p0->position, p1->position, u);
    slot.up = Vec3_Normalize(Vec3_Lerp(p0->normal, p1->normal, u));
    slot.forward = Vec3_Normalize(Vec3_Lerp(p0->tangent, p1->tangent, u));
    slot.side = Vec3_Normalize(Vec3_Cross(slot.forward, slot.up));
    float r = Math_Lerp(p0->hostRadius, p1->hostRadius, u);
    slot.hostRadii = Vec3_Create(r, r, r);

    return slot;
}

bool AttachmentPath_PublishSlots(
    const AttachmentPath* path,
    unsigned count,
    AttachmentSlotId baseSlotId,
    uint32_t hostModuleId,
    AttachmentSlotSet* slots) {
    if (!path || count == 0 || !slots) return false;
    if (slots->count + count > ATTACHMENT_MAX_SLOTS) return false;

    for (unsigned i = 0; i < count; ++i) {
        float t = (count > 1) ? (float)i / (float)(count - 1) : 0.5f;
        AttachmentSlot s = AttachmentPath_Sample(path, t, (AttachmentSlotId)(baseSlotId + i), hostModuleId);
        slots->slots[slots->count++] = s;
    }
    return true;
}

static bool PathReference(CreatureModuleInstance* mod, const AttachmentPath* path, float t, float lateral) {
    if (!path || path->count < 2 || !isfinite(t) || t < 0 || t > 1) return false;
    size_t i = 0;
    while (i + 2 < path->count && path->points[i+1].t < t) ++i;
    const AttachmentPathPoint *a = &path->points[i], *b = &path->points[i+1];
    if (!a->node || !b->node || b->t <= a->t) return false;
    mod->pathAttached = true;
    mod->pathNodeA = a->node; mod->pathNodeB = b->node;
    mod->pathU = (t - a->t) / (b->t - a->t);
    mod->pathNormal = Vec3_Normalize(Vec3_Lerp(a->normal, b->normal, mod->pathU));
    mod->lateralOffset = lateral;
    return true;
}

bool OrnamentArray_Instantiate(const OrnamentArray* array, const AttachmentPath* path,
    uint32_t baseModuleId, AttachmentSlotId baseSlotId, uint32_t hostModuleId,
    CreatureRecipe* recipe, CreaturePhenotype* phenotype) {
    if (!array || !recipe || !phenotype || !path || path->count < 2 || !array->count ||
        array->pathStart < 0 || array->pathEnd > 1 || array->pathEnd < array->pathStart ||
        recipe->bodyPlan.moduleCount + array->count > CREATURE_MAX_MODULES ||
        phenotype->ornamentCount + array->count > CREATURE_MAX_ORNAMENTS) return false;
    /* El ID sugerido no autoriza colisiones; el asignador es la autoridad. */
    (void)baseModuleId;
    size_t oldModules = recipe->bodyPlan.moduleCount, oldOrnaments = phenotype->ornamentCount;
    for (unsigned i = 0; i < array->count; ++i) {
        float u = array->count > 1 ? (float)i / (array->count - 1) : .5f;
        float size = u < .5f ? Math_Lerp(array->sizeStart, array->sizePeak, u*2) :
            Math_Lerp(array->sizePeak, array->sizeEnd, (u-.5f)*2);
        CreatureModuleInstance mod = {0};
        mod.instanceId = CreatureRecipe_NextFreeModuleId(recipe);
        mod.kind = CREATURE_MODULE_ORNAMENT;
        mod.attachment = AttachmentRef_Create(hostModuleId, baseSlotId);
        mod.phenotypeIndex = (unsigned)phenotype->ornamentCount;
        if (!mod.instanceId || !PathReference(&mod, path,
            Math_Lerp(array->pathStart, array->pathEnd, u), array->lateralOffset)) {
            recipe->bodyPlan.moduleCount = oldModules; phenotype->ornamentCount = oldOrnaments;
            return false;
        }
        OrnamentPhenotype orn = array->ornament;
        orn.length *= size; orn.baseRadius *= size; orn.tipRadius *= size; orn.curvature *= size;
        phenotype->ornaments[phenotype->ornamentCount++] = orn;
        recipe->bodyPlan.modules[recipe->bodyPlan.moduleCount++] = mod;
    }
    return true;
}

bool OrnamentField_Instantiate(const OrnamentField* field, const AttachmentPath* path,
    uint32_t hostModuleId, CreatureRecipe* recipe, CreaturePhenotype* phenotype) {
    if (!field || !path || !recipe || !phenotype || !field->rows ||
        !field->row.count || field->rows > CREATURE_MAX_ORNAMENTS / field->row.count ||
        !isfinite(field->angularSpread) || field->angularSpread < 0 || field->angularSpread > 6.2831853f ||
        !isfinite(field->stagger) || field->stagger < 0 || field->stagger > 1 ||
        !isfinite(field->sizeJitter) || field->sizeJitter < 0 || field->sizeJitter > 1 ||
        !isfinite(field->row.pathStart) || !isfinite(field->row.pathEnd) ||
        !isfinite(field->row.sizeStart) || !isfinite(field->row.sizePeak) || !isfinite(field->row.sizeEnd) ||
        field->row.sizeStart < 0 || field->row.sizePeak < 0 || field->row.sizeEnd < 0)
        return false;
    const OrnamentPhenotype* o = &field->row.ornament;
    if (o->archetype > ORNAMENT_ARCHETYPE_CREST ||
        !isfinite(o->length) || o->length <= 0 ||
        !isfinite(o->baseRadius) || o->baseRadius <= 0 ||
        !isfinite(o->tipRadius) || o->tipRadius <= 0 ||
        !isfinite(o->curvature) || !isfinite(o->development) ||
        o->development < 0 || o->development > 1 ||
        !isfinite(field->row.lateralOffset)) return false;
    size_t count = field->rows * field->row.count;
    if (recipe->bodyPlan.moduleCount + count > CREATURE_MAX_MODULES ||
        phenotype->ornamentCount + count > CREATURE_MAX_ORNAMENTS) return false;
    CreatureRecipe candidateRecipe = *recipe;
    CreaturePhenotype candidatePhenotype = *phenotype;
    uint32_t state = field->seed;
    for (unsigned row = 0; row < field->rows; ++row) {
        OrnamentArray array = field->row;
        float step = (array.pathEnd - array.pathStart) / (float)array.count;
        array.pathEnd -= step * field->stagger;
        if (row & 1u) {
            array.pathStart += step * field->stagger;
            array.pathEnd += step * field->stagger;
        }
        size_t first = candidateRecipe.bodyPlan.moduleCount;
        if (!OrnamentArray_Instantiate(&array, path, 0, 1, hostModuleId,
            &candidateRecipe, &candidatePhenotype)) return false;
        float angle = field->rows > 1 ? field->angularSpread *
            ((float)row / (float)(field->rows - 1) - .5f) : 0;
        for (size_t i = first; i < candidateRecipe.bodyPlan.moduleCount; ++i) {
            CreatureModuleInstance* module = &candidateRecipe.bodyPlan.modules[i];
            module->pathNormal = Vec3_Create(sinf(angle), cosf(angle), 0);
            state = state * 1664525u + 1013904223u;
            float random = (float)(state & 0xffffffu) / 16777216.0f;
            float size = 1 + field->sizeJitter * (2 * random - 1);
            OrnamentPhenotype* ornament = &candidatePhenotype.ornaments[module->phenotypeIndex];
            ornament->length *= size;
            ornament->baseRadius *= size;
            ornament->tipRadius *= size;
            ornament->curvature *= size;
        }
    }
    if (!CreatureRecipe_Validate(&candidateRecipe, &candidatePhenotype)) return false;
    *recipe = candidateRecipe;
    *phenotype = candidatePhenotype;
    return true;
}

bool Membrane_Instantiate(const MembranePhenotype* membrane, const AttachmentPath* path,
    uint32_t baseModuleId, AttachmentSlotId baseSlotId, uint32_t hostModuleId,
    unsigned stationCount, CreatureRecipe* recipe, CreaturePhenotype* phenotype) {
    if (!membrane || !recipe || !phenotype || stationCount < 2 ||
        membrane->peakT <= 0 || membrane->peakT >= 1 || membrane->height <= 0 || membrane->thickness <= 0)
        return false;
    OrnamentArray array = { .ornament = { .archetype = ORNAMENT_ARCHETYPE_MEMBRANE,
        .length = membrane->height, .baseRadius = membrane->thickness,
        .tipRadius = membrane->thickness, .development = 1 },
        .count = stationCount, .pathStart = membrane->startT, .pathEnd = membrane->endT,
        .sizeStart = 1, .sizePeak = 1, .sizeEnd = 1 };
    size_t first = recipe->bodyPlan.moduleCount;
    if (!OrnamentArray_Instantiate(&array, path, baseModuleId, baseSlotId, hostModuleId, recipe, phenotype)) return false;
    for (unsigned i = 0; i < stationCount; ++i) {
        float u = (float)i / (stationCount-1);
        float v = u < membrane->peakT ? u / membrane->peakT : (1-u) / (1-membrane->peakT);
        CreatureModuleInstance* mod = &recipe->bodyPlan.modules[first+i];
        phenotype->ornaments[mod->phenotypeIndex].length *= .08f + .92f * sinf(v*1.570796327f);
        if (i) mod->membranePrevious = recipe->bodyPlan.modules[first+i-1].instanceId;
    }
    return true;
}

AttachmentPath AttachmentPath_FromTail(const AnatomyGraph* graph, uint32_t module) {
    AttachmentPath path = {0};
    if (!graph) return path;
    for (size_t i = 0; i < graph->nodeCount && path.count < ATTACHMENT_PATH_MAX_POINTS; ++i) {
        const AnatomyNode* n = &graph->nodes[i];
        if (n->moduleInstanceId != module || n->region != ANATOMY_REGION_TAIL) continue;
        AttachmentPathPoint* pt = &path.points[path.count++];
        pt->node = n->id;
        pt->position = Vec3_Add(n->center, Vec3_Create(0,n->heightRadius,0));
        pt->normal = Vec3_Create(0,1,0);
        pt->hostRadius = (n->widthRadius+n->heightRadius)*.5f;
        if (path.count > 1) path.totalLength += Vec3_Distance(pt->position,path.points[path.count-2].position);
        pt->t = path.totalLength;
    }
    if (path.count < 2 || path.totalLength <= 0) return (AttachmentPath){0};
    for (size_t i = 0; i < path.count; ++i) {
        path.points[i].t /= path.totalLength;
        path.points[i].tangent = i+1 < path.count ? Vec3_Normalize(Vec3_Sub(path.points[i+1].position,path.points[i].position)) : path.points[i-1].tangent;
    }
    return path;
}
