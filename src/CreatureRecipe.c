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

        /* Comprobar que el anfitrión del anclaje exista si no es axial */
        if (m->kind != CREATURE_MODULE_AXIAL) {
            bool hostFound = false;
            for (size_t j = 0; j < r->bodyPlan.moduleCount; ++j) {
                if (r->bodyPlan.modules[j].instanceId == m->attachment.hostModuleInstanceId) {
                    hostFound = true;
                    break;
                }
            }
            if (!hostFound || !m->attachment.slotId) return false;
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
