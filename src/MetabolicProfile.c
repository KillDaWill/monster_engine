/**
 * @file MetabolicProfile.c
 * @brief Compilación, validación y registro de perfiles metabólicos por datos.
 */
#include "MetabolicProfile.h"
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

void MetabolicProfile_InitDefault(MetabolicProfile *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->handle = METABOLIC_PROFILE_INVALID_HANDLE;
    p->schemaVersion = METABOLIC_PROFILE_SCHEMA_VERSION;
    p->digestiveCapacity = 4.0f;
    p->maximumIngestionRate = 0.40f;
    p->waterAbsorptionRate = 1.0f;
    p->wasteEliminationRate = 0.25f;
    p->unprocessablePassageRate = 0.12f;
    p->unprocessableDiscomfortFactor = 0.20f;
    p->toxinSensitivity = 1.0f;
    p->detoxificationRate = 0.015f;
    p->detoxificationEnergyCost = 0.08f;
    p->toxicityThreshold = 0.20f;
    p->toxicityDamageRate = 0.045f;
    p->discomfortDecayRate = 0.05f;
    p->nutrientMaintenanceRate = 0.0008f;
    p->nutrientRecoveryBonus = 0.0025f;
    p->minimumReproductionNutrients = 0.20f;
    for (unsigned i = 0; i < MOLECULE_MAX_COUNT; ++i) {
        p->ruleIndexByMolecule[i] = -1;
    }
}

static bool validNonNeg(float v) { return isfinite(v) && v >= 0.0f; }
static bool validUnit(float v) { return isfinite(v) && v >= 0.0f && v <= 1.0f; }

bool MetabolicProfile_Compile(MetabolicProfile *p, const MoleculeRegistry *molecules,
                              char *error, size_t errorSize) {
    if (!p || !molecules) {
        setErr(error, errorSize, "Perfil o registro de moléculas nulo");
        return false;
    }
    if (p->schemaVersion != METABOLIC_PROFILE_SCHEMA_VERSION) {
        setErr(error, errorSize, "Versión de perfil metabólico no soportada: %u", p->schemaVersion);
        return false;
    }
    if (!MoleculeRegistry_ValidateId(p->id)) {
        setErr(error, errorSize, "Identificador de perfil metabólico inválido: '%s'", p->id);
        return false;
    }
    if (!isfinite(p->digestiveCapacity) || p->digestiveCapacity <= 0.0f ||
        !isfinite(p->maximumIngestionRate) || p->maximumIngestionRate <= 0.0f ||
        !validNonNeg(p->waterAbsorptionRate) ||
        !validNonNeg(p->wasteEliminationRate) ||
        !validNonNeg(p->unprocessablePassageRate) ||
        !validNonNeg(p->unprocessableDiscomfortFactor) ||
        !validNonNeg(p->toxinSensitivity) ||
        !validNonNeg(p->detoxificationRate) ||
        !validNonNeg(p->detoxificationEnergyCost) ||
        !validNonNeg(p->toxicityThreshold) ||
        !validNonNeg(p->toxicityDamageRate) ||
        !validNonNeg(p->discomfortDecayRate) ||
        !validNonNeg(p->nutrientMaintenanceRate) ||
        !validNonNeg(p->nutrientRecoveryBonus) ||
        !validUnit(p->minimumReproductionNutrients)) {
        setErr(error, errorSize, "Parámetros escalares inválidos en perfil '%s'", p->id);
        return false;
    }
    if (p->ruleCount > METABOLIC_PROFILE_MAX_RULES) {
        setErr(error, errorSize, "Demasiadas reglas en perfil '%s'", p->id);
        return false;
    }

    for (unsigned i = 0; i < MOLECULE_MAX_COUNT; ++i) {
        p->ruleIndexByMolecule[i] = -1;
    }

    for (uint16_t i = 0; i < p->ruleCount; ++i) {
        const MetabolicProcessingRule *r = &p->rules[i];
        if (!MoleculeRegistry_Get(molecules, r->molecule)) {
            setErr(error, errorSize, "Regla %u de '%s' referencia molécula desconocida (%u)",
                   (unsigned)i, p->id, (unsigned)r->molecule);
            return false;
        }
        if (p->ruleIndexByMolecule[r->molecule] >= 0) {
            setErr(error, errorSize, "Regla duplicada para molécula %u en perfil '%s'",
                   (unsigned)r->molecule, p->id);
            return false;
        }
        if (!validNonNeg(r->rate) || !validUnit(r->efficiency) ||
            !validNonNeg(r->energyYield) || !validNonNeg(r->hydrationYield) ||
            !validNonNeg(r->nutrientYield) || !validNonNeg(r->digestiveEnergyCost) ||
            !validNonNeg(r->toxicityYield) || !validNonNeg(r->discomfortYield) ||
            !validUnit(r->wasteFraction) || r->outputCount > METABOLIC_RULE_MAX_OUTPUTS) {
            setErr(error, errorSize, "Parámetros inválidos en regla %u del perfil '%s'",
                   (unsigned)i, p->id);
            return false;
        }
        /* Validar conservación de masa para transformaciones moleculares explícitas. */
        double outMassSum = (double)r->wasteFraction;
        for (uint8_t o = 0; o < r->outputCount; ++o) {
            if (!MoleculeRegistry_Get(molecules, r->outputs[o].molecule)) {
                setErr(error, errorSize, "Producto desconocido en transformación de perfil '%s'",
                       p->id);
                return false;
            }
            if (!validUnit(r->outputs[o].massRatio)) {
                setErr(error, errorSize, "Razón de masa inválida en transformación de perfil '%s'",
                       p->id);
                return false;
            }
            outMassSum += (double)r->outputs[o].massRatio;
        }
        if (outMassSum > 1.0 + 1e-3) {
            setErr(error, errorSize,
                   "Violación de conservación de masa (%.4f > 1.0) en regla %u del perfil '%s'",
                   outMassSum, (unsigned)i, p->id);
            return false;
        }
        p->ruleIndexByMolecule[r->molecule] = (int16_t)i;
    }

    p->id[METABOLIC_PROFILE_ID_MAX - 1u] = '\0';
    p->name[METABOLIC_PROFILE_NAME_MAX - 1u] = '\0';
    if (!p->name[0]) snprintf(p->name, sizeof(p->name), "%s", p->id);
    p->valid = true;
    p->fingerprint = MetabolicProfile_ComputeFingerprint(p);
    return true;
}

uint64_t MetabolicProfile_ComputeFingerprint(const MetabolicProfile *p) {
    if (!p) return 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    fnv1aBytes(&hash, p->id, strlen(p->id) + 1u);
    fnv1aFloat(&hash, p->digestiveCapacity);
    fnv1aFloat(&hash, p->maximumIngestionRate);
    fnv1aFloat(&hash, p->waterAbsorptionRate);
    fnv1aFloat(&hash, p->wasteEliminationRate);
    fnv1aFloat(&hash, p->unprocessablePassageRate);
    fnv1aFloat(&hash, p->toxinSensitivity);
    fnv1aFloat(&hash, p->detoxificationRate);
    fnv1aFloat(&hash, p->toxicityThreshold);
    fnv1aFloat(&hash, p->toxicityDamageRate);
    fnv1aFloat(&hash, p->nutrientMaintenanceRate);
    fnv1aBytes(&hash, &p->ruleCount, sizeof(p->ruleCount));
    for (uint16_t i = 0; i < p->ruleCount; ++i) {
        const MetabolicProcessingRule *r = &p->rules[i];
        fnv1aBytes(&hash, &r->molecule, sizeof(r->molecule));
        fnv1aFloat(&hash, r->rate);
        fnv1aFloat(&hash, r->efficiency);
        fnv1aFloat(&hash, r->energyYield);
        fnv1aFloat(&hash, r->hydrationYield);
        fnv1aFloat(&hash, r->nutrientYield);
        fnv1aFloat(&hash, r->digestiveEnergyCost);
        fnv1aFloat(&hash, r->toxicityYield);
        fnv1aFloat(&hash, r->discomfortYield);
        fnv1aFloat(&hash, r->wasteFraction);
        fnv1aBytes(&hash, &r->outputCount, sizeof(r->outputCount));
        for (uint8_t o = 0; o < r->outputCount; ++o) {
            fnv1aBytes(&hash, &r->outputs[o].molecule, sizeof(r->outputs[o].molecule));
            fnv1aFloat(&hash, r->outputs[o].massRatio);
        }
    }
    return hash;
}

void MetabolicProfileRegistry_Init(MetabolicProfileRegistry *reg, const MoleculeRegistry *molecules) {
    if (!reg) return;
    memset(reg, 0, sizeof(*reg));
    reg->molecules = molecules;
}

bool MetabolicProfileRegistry_Register(MetabolicProfileRegistry *reg, const MetabolicProfile *profile,
                                       MetabolicProfileHandle *outHandle, char *error, size_t errorSize) {
    if (!reg || !profile || !reg->molecules) {
        setErr(error, errorSize, "Argumentos nulos al registrar perfil metabólico");
        return false;
    }
    if (reg->frozen) {
        setErr(error, errorSize, "El registro de perfiles metabólicos está congelado");
        return false;
    }
    if (reg->count >= METABOLIC_PROFILE_MAX_COUNT) {
        setErr(error, errorSize, "Capacidad máxima de perfiles metabólicos alcanzada (%u)",
               METABOLIC_PROFILE_MAX_COUNT);
        return false;
    }
    if (MetabolicProfileRegistry_Find(reg, profile->id) != METABOLIC_PROFILE_INVALID_HANDLE) {
        setErr(error, errorSize, "Identificador de perfil metabólico duplicado: '%s'", profile->id);
        return false;
    }

    MetabolicProfile compiled = *profile;
    if (!MetabolicProfile_Compile(&compiled, reg->molecules, error, errorSize)) {
        return false;
    }
    compiled.handle = reg->count;
    reg->items[compiled.handle] = compiled;
    ++reg->count;
    if (outHandle) *outHandle = compiled.handle;
    return true;
}

MetabolicProfileHandle MetabolicProfileRegistry_Find(const MetabolicProfileRegistry *reg, const char *id) {
    if (!reg || !id || !id[0]) return METABOLIC_PROFILE_INVALID_HANDLE;
    for (uint16_t i = 0; i < reg->count; ++i) {
        if (reg->items[i].valid && strcmp(reg->items[i].id, id) == 0) return i;
    }
    return METABOLIC_PROFILE_INVALID_HANDLE;
}

const MetabolicProfile *MetabolicProfileRegistry_Get(const MetabolicProfileRegistry *reg,
                                                     MetabolicProfileHandle handle) {
    if (!reg || handle >= reg->count || !reg->items[handle].valid) return NULL;
    return &reg->items[handle];
}

uint64_t MetabolicProfileRegistry_ComputeFingerprint(const MetabolicProfileRegistry *reg) {
    if (!reg) return 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    fnv1aBytes(&hash, &reg->count, sizeof(reg->count));
    for (uint16_t i = 0; i < reg->count; ++i) {
        uint64_t fp = reg->items[i].fingerprint;
        fnv1aBytes(&hash, &fp, sizeof(fp));
    }
    return hash;
}

uint64_t MetabolicProfileRegistry_Freeze(MetabolicProfileRegistry *reg) {
    if (!reg) return 0u;
    reg->fingerprint = MetabolicProfileRegistry_ComputeFingerprint(reg);
    reg->frozen = true;
    return reg->fingerprint;
}
