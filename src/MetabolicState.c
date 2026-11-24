/**
 * @file MetabolicState.c
 * @brief Implementación dispersa y sin asignaciones dinámicas del estado digestivo.
 */
#include "MetabolicState.h"
#include <math.h>
#include <string.h>

static float clamp01(float v) {
    if (!isfinite(v) || v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void MetabolicState_Init(MetabolicState *state, float digestiveCapacity, float initialNutrients) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->initialized = true;
    state->digestiveCapacity = (isfinite(digestiveCapacity) && digestiveCapacity > 0.0f)
                                   ? digestiveCapacity : 4.0f;
    state->nutrientReserve = clamp01(initialNutrients);
    state->lastIngestedSubstance = SUBSTANCE_INVALID_HANDLE;
}

void MetabolicState_Reset(MetabolicState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->lastIngestedSubstance = SUBSTANCE_INVALID_HANDLE;
}

float MetabolicState_RemainingCapacity(const MetabolicState *state) {
    if (!state || !state->initialized || state->digestiveCapacity <= 0.0f) return 0.0f;
    float occupied = state->digestiveMass + state->wasteMass;
    float rem = state->digestiveCapacity - occupied;
    return rem > 0.0f ? rem : 0.0f;
}

float MetabolicState_Fullness(const MetabolicState *state) {
    if (!state || !state->initialized || state->digestiveCapacity <= 0.0f) return 0.0f;
    float occupied = state->digestiveMass + state->wasteMass;
    return clamp01(occupied / state->digestiveCapacity);
}

bool MetabolicState_AddMolecule(MetabolicState *state, MoleculeHandle molecule, float mass) {
    if (!state || !state->initialized || molecule == MOLECULE_INVALID_HANDLE ||
        !isfinite(mass) || mass <= 0.0f) {
        return false;
    }
    for (uint16_t i = 0; i < state->activeCount; ++i) {
        if (state->entries[i].molecule == molecule) {
            state->entries[i].mass += mass;
            state->digestiveMass += mass;
            return true;
        }
    }
    if (state->activeCount >= METABOLIC_STATE_MAX_ACTIVE) {
        /* Fallback seguro: si se satura el límite disperso, pasar a masa residual sin perder conservación. */
        state->wasteMass += mass;
        return false;
    }
    state->entries[state->activeCount++] = (MetabolicDigestiveEntry){
        .molecule = molecule,
        .mass = mass
    };
    state->digestiveMass += mass;
    return true;
}

float MetabolicState_GetMoleculeMass(const MetabolicState *state, MoleculeHandle molecule) {
    if (!state || !state->initialized || molecule == MOLECULE_INVALID_HANDLE) return 0.0f;
    for (uint16_t i = 0; i < state->activeCount; ++i) {
        if (state->entries[i].molecule == molecule) return state->entries[i].mass;
    }
    return 0.0f;
}

void MetabolicState_Compact(MetabolicState *state) {
    if (!state || !state->initialized) return;
    float sum = 0.0f;
    uint16_t writeIdx = 0u;
    for (uint16_t i = 0; i < state->activeCount; ++i) {
        float m = state->entries[i].mass;
        if (isfinite(m) && m > 1e-7f) {
            state->entries[writeIdx++] = state->entries[i];
            sum += m;
        }
    }
    state->activeCount = writeIdx;
    state->digestiveMass = sum;
    if (!isfinite(state->wasteMass) || state->wasteMass < 1e-7f) state->wasteMass = 0.0f;
}
