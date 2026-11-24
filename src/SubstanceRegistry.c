/**
 * @file SubstanceRegistry.c
 * @brief Implementación de registro y compilación de sustancias moleculares.
 */
#include "SubstanceRegistry.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void setErr(char *err, size_t errSize, const char *fmt, ...) {
    if (!err || errSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, errSize, fmt, args);
    va_end(args);
}

static float clamp01(float v) {
    if (!isfinite(v) || v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static void fnv1aBytes(uint64_t *hash, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; ++i) {
        *hash ^= (uint64_t)p[i];
        *hash *= UINT64_C(1099511628211);
    }
}

static void fnv1aFloat(uint64_t *hash, float v) {
    float c = (v == 0.0f) ? 0.0f : v;
    uint32_t bits = 0u;
    memcpy(&bits, &c, sizeof(bits));
    fnv1aBytes(hash, &bits, sizeof(bits));
}

const char *SubstancePhase_Name(SubstancePhase phase) {
    switch (phase) {
    case SUBSTANCE_PHASE_SOLID:      return "solid";
    case SUBSTANCE_PHASE_LIQUID:     return "liquid";
    case SUBSTANCE_PHASE_SEMI_SOLID: return "semi_solid";
    default:                         return "unknown";
    }
}

bool SubstancePhase_FromName(const char *name, SubstancePhase *out) {
    if (!name || !out) return false;
    if (strcmp(name, "solid") == 0) { *out = SUBSTANCE_PHASE_SOLID; return true; }
    if (strcmp(name, "liquid") == 0) { *out = SUBSTANCE_PHASE_LIQUID; return true; }
    if (strcmp(name, "semi_solid") == 0 || strcmp(name, "semi-solid") == 0) {
        *out = SUBSTANCE_PHASE_SEMI_SOLID;
        return true;
    }
    return false;
}

const char *SubstancePresentationModel_Name(SubstancePresentationModel model) {
    switch (model) {
    case SUBSTANCE_MODEL_VEGETATION: return "vegetation";
    case SUBSTANCE_MODEL_WATER:      return "water";
    case SUBSTANCE_MODEL_CARCASS:    return "carcass";
    case SUBSTANCE_MODEL_FRUIT:      return "fruit";
    case SUBSTANCE_MODEL_GENERIC:    return "generic";
    default:                         return "generic";
    }
}

bool SubstancePresentationModel_FromName(const char *name, SubstancePresentationModel *out) {
    if (!name || !out) return false;
    if (strcmp(name, "vegetation") == 0) { *out = SUBSTANCE_MODEL_VEGETATION; return true; }
    if (strcmp(name, "water") == 0)      { *out = SUBSTANCE_MODEL_WATER; return true; }
    if (strcmp(name, "carcass") == 0)    { *out = SUBSTANCE_MODEL_CARCASS; return true; }
    if (strcmp(name, "fruit") == 0)      { *out = SUBSTANCE_MODEL_FRUIT; return true; }
    if (strcmp(name, "generic") == 0)    { *out = SUBSTANCE_MODEL_GENERIC; return true; }
    return false;
}

void SubstanceRegistry_Init(SubstanceRegistry *reg, const MoleculeRegistry *molecules) {
    if (!reg) return;
    memset(reg, 0, sizeof(*reg));
    reg->molecules = molecules;
}

bool SubstanceRegistry_ComputeSignature(const MoleculeRegistry *molecules,
                                        const SubstanceComponent *components,
                                        unsigned componentCount,
                                        const SensorySignature *optionalOffset,
                                        SensorySignature *outSignature) {
    if (!molecules || !components || !outSignature || componentCount == 0u) return false;
    SensorySignature acc;
    memset(&acc, 0, sizeof(acc));
    float totalWeight = 0.0f;
    for (unsigned i = 0; i < componentCount; ++i) {
        const MoleculeDef *mol = MoleculeRegistry_Get(molecules, components[i].molecule);
        if (!mol || !isfinite(components[i].fraction) || components[i].fraction < 0.0f) return false;
        float w = components[i].fraction;
        totalWeight += w;
        for (unsigned c = 0; c < SENSORY_SIGNATURE_CHANNELS; ++c) {
            acc.channels[c] += w * mol->sensory.channels[c];
        }
    }
    if (totalWeight > 1e-6f) {
        float inv = 1.0f / totalWeight;
        for (unsigned c = 0; c < SENSORY_SIGNATURE_CHANNELS; ++c) {
            acc.channels[c] *= inv;
        }
    }
    if (optionalOffset) {
        for (unsigned c = 0; c < SENSORY_SIGNATURE_CHANNELS; ++c) {
            acc.channels[c] += optionalOffset->channels[c];
        }
    }
    for (unsigned c = 0; c < SENSORY_SIGNATURE_CHANNELS; ++c) {
        acc.channels[c] = clamp01(acc.channels[c]);
    }
    *outSignature = acc;
    return true;
}

bool SubstanceRegistry_Register(SubstanceRegistry *reg, const SubstanceDef *def,
                                SubstanceHandle *outHandle, char *error, size_t errorSize) {
    if (!reg || !def || !reg->molecules) {
        setErr(error, errorSize, "Argumentos o registro de moléculas nulo al registrar sustancia");
        return false;
    }
    if (reg->frozen) {
        setErr(error, errorSize, "El registro de sustancias está congelado");
        return false;
    }
    if (reg->count >= SUBSTANCE_MAX_COUNT) {
        setErr(error, errorSize, "Capacidad máxima de sustancias alcanzada (%u)", SUBSTANCE_MAX_COUNT);
        return false;
    }
    if (def->schemaVersion != SUBSTANCE_SCHEMA_VERSION) {
        setErr(error, errorSize, "Versión de esquema de sustancia no soportada: %u", def->schemaVersion);
        return false;
    }
    if (!MoleculeRegistry_ValidateId(def->id)) {
        setErr(error, errorSize, "Identificador de sustancia inválido: '%s'", def->id);
        return false;
    }
    if (SubstanceRegistry_Find(reg, def->id) != SUBSTANCE_INVALID_HANDLE) {
        setErr(error, errorSize, "Identificador de sustancia duplicado: '%s'", def->id);
        return false;
    }
    if (def->componentCount == 0u || def->componentCount > SUBSTANCE_MAX_COMPONENTS) {
        setErr(error, errorSize, "Sustancia '%s' debe tener entre 1 y %u componentes",
               def->id, SUBSTANCE_MAX_COMPONENTS);
        return false;
    }
    if (!isfinite(def->hardness) || def->hardness < 0.0f || def->hardness > 1.0f) {
        setErr(error, errorSize, "Dureza física inválida en sustancia '%s'", def->id);
        return false;
    }
    for (unsigned c = 0; c < 3u; ++c) {
        if (!isfinite(def->color[c]) || def->color[c] < 0.0f || def->color[c] > 1.0f) {
            setErr(error, errorSize, "Color inválido en sustancia '%s'", def->id);
            return false;
        }
    }

    bool seenMol[MOLECULE_MAX_COUNT];
    memset(seenMol, 0, sizeof(seenMol));
    double fractionSum = 0.0;
    for (uint16_t i = 0; i < def->componentCount; ++i) {
        MoleculeHandle mh = def->components[i].molecule;
        float frac = def->components[i].fraction;
        if (!MoleculeRegistry_Get(reg->molecules, mh)) {
            setErr(error, errorSize, "Sustancia '%s' referencia molécula desconocida (handle %u)",
                   def->id, (unsigned)mh);
            return false;
        }
        if (seenMol[mh]) {
            setErr(error, errorSize, "Sustancia '%s' duplica componente molecular (handle %u)",
                   def->id, (unsigned)mh);
            return false;
        }
        if (!isfinite(frac) || frac < 0.0f || frac > 1.0f + SUBSTANCE_FRACTION_SUM_TOLERANCE) {
            setErr(error, errorSize, "Fracción molecular inválida (%g) en sustancia '%s'",
                   (double)frac, def->id);
            return false;
        }
        seenMol[mh] = true;
        fractionSum += (double)frac;
    }
    if (fabs(fractionSum - 1.0) > (double)SUBSTANCE_FRACTION_SUM_TOLERANCE) {
        setErr(error, errorSize,
               "Las fracciones de '%s' suman %.6f (se requiere 1.0 ± %.4f)",
               def->id, fractionSum, (double)SUBSTANCE_FRACTION_SUM_TOLERANCE);
        return false;
    }

    SubstanceDef stored = *def;
    stored.handle = reg->count;
    stored.id[SUBSTANCE_ID_MAX - 1u] = '\0';
    stored.name[SUBSTANCE_NAME_MAX - 1u] = '\0';
    if (!stored.name[0]) snprintf(stored.name, sizeof(stored.name), "%s", stored.id);

    /* Ordenar componentes por MoleculeHandle ascendente para huella y evaluación deterministas. */
    for (uint16_t i = 1; i < stored.componentCount; ++i) {
        SubstanceComponent key = stored.components[i];
        int j = (int)i - 1;
        while (j >= 0 && stored.components[j].molecule > key.molecule) {
            stored.components[j + 1] = stored.components[j];
            --j;
        }
        stored.components[j + 1] = key;
    }

    const SensorySignature *offset = stored.hasSensoryOverride ? &stored.sensoryOffset : NULL;
    if (!SubstanceRegistry_ComputeSignature(reg->molecules, stored.components,
                                            stored.componentCount, offset,
                                            &stored.cachedSignature)) {
        setErr(error, errorSize, "No se pudo calcular la firma sensorial de '%s'", stored.id);
        return false;
    }
    stored.valid = true;

    reg->items[stored.handle] = stored;
    ++reg->count;
    if (outHandle) *outHandle = stored.handle;
    return true;
}

SubstanceHandle SubstanceRegistry_Find(const SubstanceRegistry *reg, const char *id) {
    if (!reg || !id || !id[0]) return SUBSTANCE_INVALID_HANDLE;
    for (uint16_t i = 0; i < reg->count; ++i) {
        if (reg->items[i].valid && strcmp(reg->items[i].id, id) == 0) return i;
    }
    return SUBSTANCE_INVALID_HANDLE;
}

const SubstanceDef *SubstanceRegistry_Get(const SubstanceRegistry *reg, SubstanceHandle handle) {
    if (!reg || handle >= reg->count || !reg->items[handle].valid) return NULL;
    return &reg->items[handle];
}

uint64_t SubstanceRegistry_ComputeFingerprint(const SubstanceRegistry *reg) {
    if (!reg) return 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    fnv1aBytes(&hash, &reg->count, sizeof(reg->count));
    for (uint16_t i = 0; i < reg->count; ++i) {
        const SubstanceDef *s = &reg->items[i];
        fnv1aBytes(&hash, s->id, strlen(s->id) + 1u);
        uint32_t ph = (uint32_t)s->phase;
        fnv1aBytes(&hash, &ph, sizeof(ph));
        fnv1aFloat(&hash, s->hardness);
        fnv1aBytes(&hash, &s->componentCount, sizeof(s->componentCount));
        for (uint16_t c = 0; c < s->componentCount; ++c) {
            fnv1aBytes(&hash, &s->components[c].molecule, sizeof(s->components[c].molecule));
            fnv1aFloat(&hash, s->components[c].fraction);
        }
        for (unsigned k = 0; k < SENSORY_SIGNATURE_CHANNELS; ++k) {
            fnv1aFloat(&hash, s->cachedSignature.channels[k]);
        }
    }
    return hash;
}

uint64_t SubstanceRegistry_Freeze(SubstanceRegistry *reg) {
    if (!reg) return 0u;
    reg->fingerprint = SubstanceRegistry_ComputeFingerprint(reg);
    reg->frozen = true;
    return reg->fingerprint;
}
