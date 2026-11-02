/**
 * @file CreatureRecipe.c
 * @brief Implementación de validación y evaluación ontogenética de recetas.
 * @author Monster Engine Team
 * @date 2026
 */

#include "CreatureRecipe.h"
#include "MathUtils.h"
#include "AxialBody.h"
#include "Limb.h"
#include <math.h>

static bool AddDependency(const BodyPlan* plan, size_t child, uint32_t host, uint64_t* mask) {
    for (size_t i = 0; i < plan->moduleCount; ++i) {
        if (plan->modules[i].instanceId == host) {
            if (i == child) return false;
            *mask |= UINT64_C(1) << i;
            return true;
        }
    }
    return false;
}

static unsigned ResolutionPriority(CreatureModuleKind kind) {
    /* Precedencia histórica de las recetas incorporadas, sin significado de especie. */
    switch (kind) {
    case CREATURE_MODULE_AXIAL: return 0;
    case CREATURE_MODULE_HEAD: return 1;
    case CREATURE_MODULE_TAIL: return 2;
    case CREATURE_MODULE_LIMB: return 3;
    case CREATURE_MODULE_ORNAMENT: return 4;
    default: return 5;
    }
}

bool CreatureRecipe_ResolutionOrder(const CreatureRecipe* r, size_t order[CREATURE_MAX_MODULES]) {
    if (!r || !order || !r->bodyPlan.moduleCount || r->bodyPlan.moduleCount > CREATURE_MAX_MODULES) return false;
    _Static_assert(CREATURE_MAX_MODULES <= 64, "La máscara de dependencias admite hasta 64 módulos");
    const BodyPlan* plan = &r->bodyPlan;
    uint64_t dependencies[CREATURE_MAX_MODULES] = {0}, resolved = 0;
    for (size_t i = 0; i < plan->moduleCount; ++i) {
        const CreatureModuleInstance* m = &plan->modules[i];
        if (!Anatomy_MakeId(m->instanceId, 1)) return false;
        for (size_t j = 0; j < i; ++j)
            if (plan->modules[j].instanceId == m->instanceId) return false;
        if (m->kind == CREATURE_MODULE_AXIAL) {
            /* El contrato axial actual es una raíz, no un módulo anclado. */
            if (m->pathAttached || m->membranePrevious || m->attachment.hostModuleInstanceId || m->attachment.slotId) return false;
            continue;
        }
        if (m->pathAttached) {
            if (!(m->pathNodeA & 0xffffu) || !(m->pathNodeB & 0xffffu) || m->pathNodeA == m->pathNodeB ||
                !isfinite(m->pathU) || m->pathU < 0 || m->pathU > 1 || !isfinite(m->lateralOffset) ||
                !isfinite(m->pathNormal.x) || !isfinite(m->pathNormal.y) || !isfinite(m->pathNormal.z) ||
                !isfinite(Vec3_Length(m->pathNormal)) || Vec3_Length(m->pathNormal) < 1e-6f ||
                !AddDependency(plan, i, m->pathNodeA >> 16, &dependencies[i]) ||
                !AddDependency(plan, i, m->pathNodeB >> 16, &dependencies[i])) return false;
        } else if (!m->attachment.slotId ||
                   !AddDependency(plan, i, m->attachment.hostModuleInstanceId, &dependencies[i])) return false;
        if (m->membranePrevious) {
            if (m->kind != CREATURE_MODULE_ORNAMENT ||
                !AddDependency(plan, i, m->membranePrevious, &dependencies[i])) return false;
            for (size_t j = 0; j < plan->moduleCount; ++j)
                if (plan->modules[j].instanceId == m->membranePrevious &&
                    plan->modules[j].kind != CREATURE_MODULE_ORNAMENT) return false;
        }
    }
    for (size_t n = 0; n < plan->moduleCount; ++n) {
        size_t next = plan->moduleCount;
        for (size_t i = 0; i < plan->moduleCount; ++i) {
            if ((resolved & (UINT64_C(1) << i)) || (dependencies[i] & ~resolved)) continue;
            if (next == plan->moduleCount ||
                ResolutionPriority(plan->modules[i].kind) < ResolutionPriority(plan->modules[next].kind) ||
                (ResolutionPriority(plan->modules[i].kind) == ResolutionPriority(plan->modules[next].kind) &&
                 plan->modules[i].instanceId < plan->modules[next].instanceId)) next = i;
        }
        if (next == plan->moduleCount) return false;
        order[n] = next;
        resolved |= UINT64_C(1) << next;
    }
    return true;
}

const CreaturePhenotype* CreatureRecipe_GetStage(const CreatureRecipe* r, CreatureStage s) {
    if (!r) return NULL;
    switch (s) {
    case CREATURE_STAGE_SEED: return &r->seed;
    case CREATURE_STAGE_LARVA: return &r->larva;
    case CREATURE_STAGE_JUVENILE: return &r->juvenile;
    case CREATURE_STAGE_ADULT: return &r->adult;
    }
    return NULL;
}

CreaturePhenotype CreatureRecipe_EvaluateDevelopment(const CreatureRecipe* r, float age) {
    if (!r) return (CreaturePhenotype){0};
    float x = (isfinite(age) ? Math_Clamp01(age) : 0.0f) * 3.0f;
    unsigned i = x >= 3.0f ? 2 : (unsigned)x;
    return CreaturePhenotype_Interpolate(
        CreatureRecipe_GetStage(r, (CreatureStage)i),
        CreatureRecipe_GetStage(r, (CreatureStage)(i + 1)),
        x - (float)i
    );
}

bool CreatureRecipe_Validate(const CreatureRecipe* r, const CreaturePhenotype* p) {
    if (!r || !p || !r->id || !r->bodyPlan.moduleCount || r->bodyPlan.moduleCount > CREATURE_MAX_MODULES ||
        p->limbCount > CREATURE_MAX_LIMBS || p->tailCount > CREATURE_MAX_TAILS || p->ornamentCount > CREATURE_MAX_ORNAMENTS) {
        return false;
    }

    size_t order[CREATURE_MAX_MODULES];
    if (!CreatureRecipe_ResolutionOrder(r, order)) return false;

    /* Validar que la referencia a la raíz del BodyPlan sea coherente */
    if (!r->bodyPlan.root.moduleInstanceId || !r->bodyPlan.root.localNodeId ||
        !Anatomy_MakeId(r->bodyPlan.root.moduleInstanceId, r->bodyPlan.root.localNodeId)) {
        return false;
    }
    bool rootFound = false;

    unsigned axial = 0, head = 0;
    bool limbs[CREATURE_MAX_LIMBS] = {0};
    bool tails[CREATURE_MAX_TAILS] = {0};
    bool ornaments[CREATURE_MAX_ORNAMENTS] = {0};

    for (size_t i = 0; i < r->bodyPlan.moduleCount; ++i) {
        const CreatureModuleInstance* m = &r->bodyPlan.modules[i];
        if (!m->instanceId || m->instanceId > 65534 || !Anatomy_MakeId(m->instanceId, 1)) return false;

        if (m->instanceId == r->bodyPlan.root.moduleInstanceId) {
            rootFound = true;
        }

        /* Comprobar unicidad de ID de instancia */
        for (size_t j = 0; j < i; ++j) {
            if (r->bodyPlan.modules[j].instanceId == m->instanceId) return false;
        }

        switch (m->kind) {
        case CREATURE_MODULE_AXIAL:
            if (!AxialBody_Supports(p->axial.archetype)) return false;
            ++axial;
            break;
        case CREATURE_MODULE_HEAD:
            if (!Head_Supports(p->head.archetype)) return false;
            ++head;
            break;
        case CREATURE_MODULE_LIMB:
            if (m->phenotypeIndex >= p->limbCount || limbs[m->phenotypeIndex]) return false;
            if (!Limb_Supports(p->limbs[m->phenotypeIndex].archetype)) return false;
            const LimbPhenotype* limb = &p->limbs[m->phenotypeIndex];
            if (limb->digitCount > CREATURE_MAX_DIGITS) return false;
            if (limb->archetype == LIMB_ARCHETYPE_UNGULATE &&
                ((limb->digitCount != 1 && limb->digitCount != 2) ||
                 (limb->digitCount == 1 && limb->dewclawSize > 0))) return false;
            limbs[m->phenotypeIndex] = true;
            break;
        case CREATURE_MODULE_TAIL:
            if (m->phenotypeIndex >= p->tailCount || tails[m->phenotypeIndex]) return false;
            if (p->tails[m->phenotypeIndex].archetype > TAIL_ARCHETYPE_CUSTOM) return false;
            tails[m->phenotypeIndex] = true;
            break;
        case CREATURE_MODULE_ORNAMENT:
            if (m->phenotypeIndex >= p->ornamentCount || ornaments[m->phenotypeIndex]) return false;
            if (p->ornaments[m->phenotypeIndex].archetype > ORNAMENT_ARCHETYPE_CUSTOM) return false;
            ornaments[m->phenotypeIndex] = true;
            break;
        default:
            return false;
        }
    }

    return rootFound && axial >= 1 && head <= 1;
}

uint32_t CreatureRecipe_NextFreeModuleId(const CreatureRecipe* recipe) {
    if (!recipe) return 0;
    for (uint32_t id = 1; id <= 65534; ++id) {
        bool used = false;
        for (size_t i = 0; i < recipe->bodyPlan.moduleCount; ++i)
            if (recipe->bodyPlan.modules[i].instanceId == id) used = true;
        if (!used) return id;
    }
    return 0;
}
