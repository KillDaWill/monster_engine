/**
 * @file MoleculeRegistry.c
 * @brief Implementación del registro versionado y congelable de moléculas.
 */
#include "MoleculeRegistry.h"
#include <ctype.h>
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

const char *SensoryChannel_Name(SensoryChannel channel) {
    static const char *names[SENSORY_SIGNATURE_CHANNELS] = {
        "moisture",
        "plant_aroma",
        "organic_aroma",
        "lipid_aroma",
        "sweetness",
        "decay_aroma"
    };
    return (channel >= 0 && (unsigned)channel < SENSORY_SIGNATURE_CHANNELS)
               ? names[channel] : "unknown";
}

bool SensorySignature_Validate(const SensorySignature *sig) {
    if (!sig) return false;
    for (unsigned i = 0; i < SENSORY_SIGNATURE_CHANNELS; ++i) {
        float v = sig->channels[i];
        if (!isfinite(v) || v < 0.0f || v > 1.0f) return false;
    }
    return true;
}

bool MoleculeRegistry_ValidateId(const char *id) {
    if (!id || !id[0]) return false;
    size_t len = strlen(id);
    if (len == 0 || len >= MOLECULE_ID_MAX) return false;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)id[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    if (!(id[0] >= 'a' && id[0] <= 'z')) return false;
    return true;
}

void MoleculeRegistry_Init(MoleculeRegistry *reg) {
    if (!reg) return;
    memset(reg, 0, sizeof(*reg));
    reg->schemaVersion = MOLECULE_SCHEMA_VERSION;
}

bool MoleculeRegistry_Register(MoleculeRegistry *reg, const MoleculeDef *def,
                               MoleculeHandle *outHandle, char *error, size_t errorSize) {
    if (!reg || !def) {
        setErr(error, errorSize, "Argumentos nulos al registrar molécula");
        return false;
    }
    if (reg->frozen) {
        setErr(error, errorSize, "El registro de moléculas está congelado");
        return false;
    }
    if (reg->count >= MOLECULE_MAX_COUNT) {
        setErr(error, errorSize, "Capacidad máxima del registro de moléculas alcanzada (%u)",
               MOLECULE_MAX_COUNT);
        return false;
    }
    if (!MoleculeRegistry_ValidateId(def->id)) {
        setErr(error, errorSize, "Identificador molecular inválido: '%s'", def->id);
        return false;
    }
    if (!SensorySignature_Validate(&def->sensory)) {
        setErr(error, errorSize, "Firma sensorial fuera de [0,1] o no finita en molécula '%s'",
               def->id);
        return false;
    }
    if (MoleculeRegistry_Find(reg, def->id) != MOLECULE_INVALID_HANDLE) {
        setErr(error, errorSize, "Identificador molecular duplicado: '%s'", def->id);
        return false;
    }

    MoleculeHandle handle = reg->count;
    MoleculeDef stored = *def;
    stored.handle = handle;
    stored.id[MOLECULE_ID_MAX - 1u] = '\0';
    stored.name[MOLECULE_NAME_MAX - 1u] = '\0';
    stored.description[MOLECULE_DESC_MAX - 1u] = '\0';
    if (!stored.name[0]) snprintf(stored.name, sizeof(stored.name), "%s", stored.id);
    for (unsigned i = 0; i < SENSORY_SIGNATURE_CHANNELS; ++i) {
        if (stored.sensory.channels[i] == 0.0f) stored.sensory.channels[i] = 0.0f;
    }
    stored.valid = true;

    reg->items[handle] = stored;
    ++reg->count;
    if (outHandle) *outHandle = handle;
    return true;
}

MoleculeHandle MoleculeRegistry_Find(const MoleculeRegistry *reg, const char *id) {
    if (!reg || !id || !id[0]) return MOLECULE_INVALID_HANDLE;
    for (uint16_t i = 0; i < reg->count; ++i) {
        if (reg->items[i].valid && strcmp(reg->items[i].id, id) == 0) {
            return i;
        }
    }
    return MOLECULE_INVALID_HANDLE;
}

const MoleculeDef *MoleculeRegistry_Get(const MoleculeRegistry *reg, MoleculeHandle handle) {
    if (!reg || handle >= reg->count || !reg->items[handle].valid) return NULL;
    return &reg->items[handle];
}

uint64_t MoleculeRegistry_ComputeFingerprint(const MoleculeRegistry *reg) {
    if (!reg) return 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    fnv1aBytes(&hash, &reg->schemaVersion, sizeof(reg->schemaVersion));
    fnv1aBytes(&hash, &reg->count, sizeof(reg->count));
    for (uint16_t i = 0; i < reg->count; ++i) {
        const MoleculeDef *m = &reg->items[i];
        fnv1aBytes(&hash, m->id, strlen(m->id) + 1u);
        for (unsigned c = 0; c < SENSORY_SIGNATURE_CHANNELS; ++c) {
            fnv1aFloat(&hash, m->sensory.channels[c]);
        }
    }
    return hash;
}

uint64_t MoleculeRegistry_Freeze(MoleculeRegistry *reg) {
    if (!reg) return 0u;
    reg->fingerprint = MoleculeRegistry_ComputeFingerprint(reg);
    reg->frozen = true;
    return reg->fingerprint;
}
