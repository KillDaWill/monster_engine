/**
 * @file WorldIO.c
 * @brief Serialización estricta, validación, presets y derivación determinista de semillas.
 */
#include "WorldIO.h"
#include "TerrainHeightfield.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void setErr(char *err, size_t errSize, const char *fmt, ...) {
    if (!err || errSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, errSize, fmt, args);
    va_end(args);
}

static uint64_t splitmix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

uint32_t WorldSeed_Derive(uint64_t masterSeed, uint64_t domainTag, uint64_t index) {
    uint64_t h = splitmix64(masterSeed ^ UINT64_C(0x243f6a8885a308d3));
    h = splitmix64(h ^ domainTag);
    h = splitmix64(h ^ index);
    uint32_t out = (uint32_t)(h ^ (h >> 32));
    return out ? out : 1u;
}

static void fnv1aBytes(uint64_t *h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) {
        *h ^= (uint64_t)p[i];
        *h *= UINT64_C(1099511628211);
    }
}

static void fnv1aQFloat(uint64_t *h, float v) {
    int64_t q = llround((double)v * 100000.0);
    fnv1aBytes(h, &q, sizeof(q));
}

WorldRecipe WorldRecipe_Default(void) {
    WorldRecipe r;
    memset(&r, 0, sizeof(r));
    r.version = WORLD_RECIPE_SCHEMA_VERSION;
    r.generatorVersion = WORLD_GENERATOR_VERSION_LEGACY;
    snprintf(r.id, sizeof(r.id), "river_valley");
    snprintf(r.name, sizeof(r.name), "River Valley");
    r.seed = 42u;
    r.size = 256.0f;
    r.flat = false;

    r.terrain.resolution = 257u;
    r.terrain.heightScale = 24.0f;
    r.terrain.baseFrequency = 0.011f;
    r.terrain.octaves = 5u;
    r.terrain.persistence = 0.50f;
    r.terrain.lacunarity = 2.0f;
    r.terrain.ridgeWeight = 0.22f;
    r.terrain.domainWarpStrength = 0.25f;
    r.terrain.valleyShaping = 0.35f;

    r.erosion.thermalIterations = 24u;
    r.erosion.talusAngleDegrees = 32.0f;
    r.erosion.thermalRate = 0.35f;

    r.hydrology.enabled = true;
    r.hydrology.outletPolicy = WORLD_OUTLET_OPEN_BOUNDARY;
    r.hydrology.riverDensity = 0.45f;
    r.hydrology.lakeDensity = 0.28f;
    r.hydrology.riverWidthScale = 1.0f;
    r.hydrology.riverDepthScale = 1.0f;
    snprintf(r.hydrology.waterSubstanceId, sizeof(r.hydrology.waterSubstanceId), "clean_water");
    r.hydrology.reservoirPolicy = WORLD_RESERVOIR_INFINITE_EXTERNAL;
    r.hydrology.finiteVolume = 500.0f;

    r.geology.rockDensity = 0.35f;
    r.geology.rockVariants = 16u;
    r.geology.minRockScale = 0.7f;
    r.geology.maxRockScale = 3.0f;

    r.vegetation.density = 0.70f;
    r.vegetation.fertilityBias = 0.05f;
    r.vegetation.moistureBias = 0.05f;

    r.recipeHash = WorldRecipe_Fingerprint(&r);
    r.valid = true;
    return r;
}

WorldRecipe WorldRecipe_Flat(float size) {
    WorldRecipe r = WorldRecipe_Default();
    snprintf(r.id, sizeof(r.id), "flat");
    snprintf(r.name, sizeof(r.name), "Flat Arena");
    r.size = (isfinite(size) && size >= 12.0f && size <= 1024.0f) ? size : 64.0f;
    r.flat = true;
    r.terrain.resolution = 17u;
    r.terrain.heightScale = 0.0f;
    r.erosion.thermalIterations = 0u;
    r.hydrology.enabled = false;
    r.hydrology.riverDensity = 0.0f;
    r.hydrology.lakeDensity = 0.0f;
    r.geology.rockDensity = 0.0f;
    r.recipeHash = WorldRecipe_Fingerprint(&r);
    r.valid = true;
    return r;
}

bool WorldRecipe_Preset(const char *presetName, WorldRecipe *out) {
    if (!presetName || !out) return false;
    if (strcmp(presetName, "flat") == 0) {
        *out = WorldRecipe_Flat(64.0f);
        return true;
    }
    char legacyName[48];
    size_t len = strlen(presetName);
    bool continuous = len > 3u && len < sizeof(legacyName) &&
                      strcmp(presetName + len - 3u, "_v2") == 0;
    const char *requestedName = presetName;
    if (continuous) {
        memcpy(legacyName, presetName, len - 3u);
        legacyName[len - 3u] = '\0';
        presetName = legacyName;
    }
    WorldRecipe r = WorldRecipe_Default();
    if (strcmp(presetName, "river_valley") == 0) {
        r.terrain.resolution = 513u;
        r.size = 256.0f;
    } else if (strcmp(presetName, "mountain_lakes") == 0) {
        snprintf(r.id, sizeof(r.id), "mountain_lakes");
        snprintf(r.name, sizeof(r.name), "Mountain Lakes");
        r.seed = 108u;
        r.size = 256.0f;
        r.terrain.resolution = 513u;
        r.terrain.heightScale = 38.0f;
        r.terrain.baseFrequency = 0.013f;
        r.terrain.ridgeWeight = 0.42f;
        r.terrain.domainWarpStrength = 0.30f;
        r.terrain.valleyShaping = 0.45f;
        r.erosion.thermalIterations = 34u;
        r.erosion.talusAngleDegrees = 35.0f;
        r.hydrology.riverDensity = 0.38f;
        r.hydrology.lakeDensity = 0.45f;
        r.geology.rockDensity = 0.50f;
        r.vegetation.density = 0.55f;
    } else if (strcmp(presetName, "rocky_highlands") == 0) {
        snprintf(r.id, sizeof(r.id), "rocky_highlands");
        snprintf(r.name, sizeof(r.name), "Rocky Highlands");
        r.seed = 317u;
        r.size = 256.0f;
        r.terrain.resolution = 513u;
        r.terrain.heightScale = 34.0f;
        r.terrain.baseFrequency = 0.015f;
        r.terrain.ridgeWeight = 0.48f;
        r.terrain.domainWarpStrength = 0.35f;
        r.terrain.valleyShaping = 0.20f;
        r.erosion.thermalIterations = 28u;
        r.erosion.talusAngleDegrees = 38.0f;
        r.hydrology.riverDensity = 0.25f;
        r.hydrology.lakeDensity = 0.15f;
        r.geology.rockDensity = 0.68f;
        r.geology.maxRockScale = 3.8f;
        r.vegetation.density = 0.45f;
        r.vegetation.moistureBias = -0.10f;
    } else if (strcmp(presetName, "wetlands") == 0) {
        snprintf(r.id, sizeof(r.id), "wetlands");
        snprintf(r.name, sizeof(r.name), "Wetlands");
        r.seed = 707u;
        r.size = 192.0f;
        r.terrain.resolution = 385u;
        r.terrain.heightScale = 11.0f;
        r.terrain.baseFrequency = 0.014f;
        r.terrain.ridgeWeight = 0.08f;
        r.terrain.domainWarpStrength = 0.22f;
        r.terrain.valleyShaping = 0.50f;
        r.erosion.thermalIterations = 16u;
        r.erosion.talusAngleDegrees = 28.0f;
        r.hydrology.riverDensity = 0.60f;
        r.hydrology.lakeDensity = 0.55f;
        r.hydrology.riverWidthScale = 1.35f;
        r.geology.rockDensity = 0.18f;
        r.vegetation.density = 0.85f;
        r.vegetation.moistureBias = 0.20f;
        r.vegetation.fertilityBias = 0.15f;
    } else {
        return false;
    }
    if (continuous) {
        r.generatorVersion = WORLD_GENERATOR_VERSION;
        snprintf(r.id, sizeof(r.id), "%s", requestedName);
    }
    r.recipeHash = WorldRecipe_Fingerprint(&r);
    r.valid = true;
    *out = r;
    return true;
}

uint64_t WorldRecipe_Fingerprint(const WorldRecipe *r) {
    if (!r) return 0u;
    uint64_t h = UINT64_C(14695981039346656037);
    fnv1aBytes(&h, &r->version, sizeof(r->version));
    fnv1aBytes(&h, &r->generatorVersion, sizeof(r->generatorVersion));
    fnv1aBytes(&h, r->id, strlen(r->id) + 1u);
    fnv1aBytes(&h, &r->seed, sizeof(r->seed));
    fnv1aQFloat(&h, r->size);
    uint8_t flatByte = r->flat ? 1u : 0u;
    fnv1aBytes(&h, &flatByte, 1u);

    fnv1aBytes(&h, &r->terrain.resolution, sizeof(r->terrain.resolution));
    fnv1aQFloat(&h, r->terrain.heightScale);
    fnv1aQFloat(&h, r->terrain.baseFrequency);
    fnv1aBytes(&h, &r->terrain.octaves, sizeof(r->terrain.octaves));
    fnv1aQFloat(&h, r->terrain.persistence);
    fnv1aQFloat(&h, r->terrain.lacunarity);
    fnv1aQFloat(&h, r->terrain.ridgeWeight);
    fnv1aQFloat(&h, r->terrain.domainWarpStrength);
    fnv1aQFloat(&h, r->terrain.valleyShaping);

    fnv1aBytes(&h, &r->erosion.thermalIterations, sizeof(r->erosion.thermalIterations));
    fnv1aQFloat(&h, r->erosion.talusAngleDegrees);
    fnv1aQFloat(&h, r->erosion.thermalRate);

    uint8_t hydroEn = r->hydrology.enabled ? 1u : 0u;
    uint32_t outlet = (uint32_t)r->hydrology.outletPolicy;
    uint32_t resPol = (uint32_t)r->hydrology.reservoirPolicy;
    fnv1aBytes(&h, &hydroEn, 1u);
    fnv1aBytes(&h, &outlet, sizeof(outlet));
    fnv1aQFloat(&h, r->hydrology.riverDensity);
    fnv1aQFloat(&h, r->hydrology.lakeDensity);
    fnv1aQFloat(&h, r->hydrology.riverWidthScale);
    fnv1aQFloat(&h, r->hydrology.riverDepthScale);
    fnv1aBytes(&h, r->hydrology.waterSubstanceId, strlen(r->hydrology.waterSubstanceId) + 1u);
    fnv1aBytes(&h, &resPol, sizeof(resPol));
    fnv1aQFloat(&h, r->hydrology.finiteVolume);

    fnv1aQFloat(&h, r->geology.rockDensity);
    fnv1aBytes(&h, &r->geology.rockVariants, sizeof(r->geology.rockVariants));
    fnv1aQFloat(&h, r->geology.minRockScale);
    fnv1aQFloat(&h, r->geology.maxRockScale);

    fnv1aQFloat(&h, r->vegetation.density);
    fnv1aQFloat(&h, r->vegetation.fertilityBias);
    fnv1aQFloat(&h, r->vegetation.moistureBias);
    return h;
}

bool WorldRecipe_Validate(const WorldRecipe *r, char *error, size_t errorSize) {
    if (!r) { setErr(error, errorSize, "Receta de mundo nula"); return false; }
    if (r->version != WORLD_RECIPE_SCHEMA_VERSION) {
        setErr(error, errorSize, "Versión de receta de mundo no soportada: %u", r->version);
        return false;
    }
    if (r->generatorVersion != WORLD_GENERATOR_VERSION_LEGACY &&
        r->generatorVersion != WORLD_GENERATOR_VERSION) {
        setErr(error, errorSize, "Versión de generador de mundo no soportada: %u", r->generatorVersion);
        return false;
    }
    if (!r->id[0]) { setErr(error, errorSize, "Falta identificador 'id' en receta de mundo"); return false; }
    if (!isfinite(r->size) || r->size < 12.0f || r->size > 1024.0f) {
        setErr(error, errorSize, "Tamaño de mundo fuera de rango [12, 1024]: %g", r->size);
        return false;
    }
    if (r->terrain.resolution < TERRAIN_MIN_RESOLUTION || r->terrain.resolution > TERRAIN_MAX_RESOLUTION ||
        (r->terrain.resolution & 1u) == 0u) {
        setErr(error, errorSize, "Resolución de terreno debe ser impar en [%u, %u]: %u",
               TERRAIN_MIN_RESOLUTION, TERRAIN_MAX_RESOLUTION, r->terrain.resolution);
        return false;
    }
    uint64_t samples = (uint64_t)r->terrain.resolution * (uint64_t)r->terrain.resolution;
    uint64_t estimatedBytes = samples * 64u; /* altura + normales + hidrología + biomas */
    if (estimatedBytes > WORLD_MAX_MEMORY_BUDGET_BYTES) {
        setErr(error, errorSize, "Presupuesto de memoria del terreno excedido (%llu bytes)",
               (unsigned long long)estimatedBytes);
        return false;
    }
    if (!isfinite(r->terrain.heightScale) || r->terrain.heightScale < 0.0f || r->terrain.heightScale > 200.0f) {
        setErr(error, errorSize, "height_scale fuera de rango [0, 200]"); return false;
    }
    if (!isfinite(r->terrain.baseFrequency) || r->terrain.baseFrequency <= 0.0f || r->terrain.baseFrequency > 1.0f) {
        setErr(error, errorSize, "base_frequency fuera de rango (0, 1]"); return false;
    }
    if (r->terrain.octaves < 1u || r->terrain.octaves > 8u) {
        setErr(error, errorSize, "octaves fuera de rango [1, 8]"); return false;
    }
    if (!isfinite(r->terrain.persistence) || r->terrain.persistence < 0.1f || r->terrain.persistence > 0.9f ||
        !isfinite(r->terrain.lacunarity) || r->terrain.lacunarity < 1.2f || r->terrain.lacunarity > 4.0f ||
        !isfinite(r->terrain.ridgeWeight) || r->terrain.ridgeWeight < 0.0f || r->terrain.ridgeWeight > 1.0f ||
        !isfinite(r->terrain.domainWarpStrength) || r->terrain.domainWarpStrength < 0.0f || r->terrain.domainWarpStrength > 2.0f ||
        !isfinite(r->terrain.valleyShaping) || r->terrain.valleyShaping < 0.0f || r->terrain.valleyShaping > 1.0f) {
        setErr(error, errorSize, "Parámetros de terreno fuera de rango válido"); return false;
    }
    if (r->erosion.thermalIterations > 120u ||
        !isfinite(r->erosion.talusAngleDegrees) || r->erosion.talusAngleDegrees < 10.0f || r->erosion.talusAngleDegrees > 70.0f ||
        !isfinite(r->erosion.thermalRate) || r->erosion.thermalRate <= 0.0f || r->erosion.thermalRate > 0.5f) {
        setErr(error, errorSize, "Parámetros de erosión térmica fuera de rango válido"); return false;
    }
    if (!isfinite(r->hydrology.riverDensity) || r->hydrology.riverDensity < 0.0f || r->hydrology.riverDensity > 1.0f ||
        !isfinite(r->hydrology.lakeDensity) || r->hydrology.lakeDensity < 0.0f || r->hydrology.lakeDensity > 1.0f ||
        !isfinite(r->hydrology.riverWidthScale) || r->hydrology.riverWidthScale < 0.25f || r->hydrology.riverWidthScale > 4.0f ||
        !isfinite(r->hydrology.riverDepthScale) || r->hydrology.riverDepthScale < 0.25f || r->hydrology.riverDepthScale > 3.0f ||
        !isfinite(r->hydrology.finiteVolume) || r->hydrology.finiteVolume < 0.0f) {
        setErr(error, errorSize, "Parámetros de hidrología fuera de rango válido"); return false;
    }
    if (r->hydrology.enabled && !r->hydrology.waterSubstanceId[0]) {
        setErr(error, errorSize, "Se requiere water_substance cuando hydrology.enabled=true"); return false;
    }
    if (!isfinite(r->geology.rockDensity) || r->geology.rockDensity < 0.0f || r->geology.rockDensity > 1.0f ||
        r->geology.rockVariants < 1u || r->geology.rockVariants > 32u ||
        !isfinite(r->geology.minRockScale) || r->geology.minRockScale < 0.2f ||
        !isfinite(r->geology.maxRockScale) || r->geology.maxRockScale < r->geology.minRockScale ||
        r->geology.maxRockScale > 12.0f) {
        setErr(error, errorSize, "Parámetros de geología fuera de rango válido"); return false;
    }
    if (!isfinite(r->vegetation.density) || r->vegetation.density < 0.0f || r->vegetation.density > 1.0f ||
        !isfinite(r->vegetation.fertilityBias) || r->vegetation.fertilityBias < -0.5f || r->vegetation.fertilityBias > 0.5f ||
        !isfinite(r->vegetation.moistureBias) || r->vegetation.moistureBias < -0.5f || r->vegetation.moistureBias > 0.5f) {
        setErr(error, errorSize, "Parámetros de vegetación fuera de rango válido"); return false;
    }
    return true;
}

/* ======================== Parser JSON estricto ======================== */

typedef struct WJson {
    const char *p, *end;
    char *error;
    size_t errorSize;
} WJson;

static void wjSpace(WJson *r) { while (r->p < r->end && isspace((unsigned char)*r->p)) ++r->p; }
static bool wjTake(WJson *r, char c) { wjSpace(r); if (r->p >= r->end || *r->p != c) return false; ++r->p; return true; }

static bool wjString(WJson *r, char *out, size_t cap) {
    if (!wjTake(r, '"')) { setErr(r->error, r->errorSize, "Se esperaba cadena JSON"); return false; }
    size_t n = 0;
    while (r->p < r->end && *r->p != '"') {
        unsigned char c = (unsigned char)*r->p++;
        if (c == '\\' && r->p < r->end) c = (unsigned char)*r->p++;
        if (n + 1u >= cap) { setErr(r->error, r->errorSize, "Cadena JSON demasiado larga"); return false; }
        out[n++] = (char)c;
    }
    if (r->p >= r->end || *r->p != '"') { setErr(r->error, r->errorSize, "Cadena JSON sin cierre"); return false; }
    ++r->p;
    out[n] = '\0';
    return true;
}

static bool wjBool(WJson *r, bool *out) {
    wjSpace(r);
    if ((size_t)(r->end - r->p) >= 4u && memcmp(r->p, "true", 4) == 0) { r->p += 4; *out = true; return true; }
    if ((size_t)(r->end - r->p) >= 5u && memcmp(r->p, "false", 5) == 0) { r->p += 5; *out = false; return true; }
    setErr(r->error, r->errorSize, "Se esperaba booleano JSON");
    return false;
}

static bool wjNumber(WJson *r, double *out) {
    wjSpace(r);
    const char *start = r->p;
    if (r->p < r->end && *r->p == '-') ++r->p;
    while (r->p < r->end && (isdigit((unsigned char)*r->p) || *r->p == '.' ||
                             *r->p == 'e' || *r->p == 'E' || *r->p == '+' || *r->p == '-')) {
        if ((*r->p == '+' || *r->p == '-') && r->p > start && r->p[-1] != 'e' && r->p[-1] != 'E') break;
        ++r->p;
    }
    size_t len = (size_t)(r->p - start);
    if (len == 0u || len >= 64u) { setErr(r->error, r->errorSize, "Número JSON inválido"); return false; }
    char tmp[64];
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *ep = NULL;
    errno = 0;
    double v = strtod(tmp, &ep);
    if (errno == ERANGE || ep != tmp + len || !isfinite(v)) {
        setErr(r->error, r->errorSize, "Número JSON no finito o fuera de rango");
        return false;
    }
    *out = v;
    return true;
}

bool WorldRecipe_DeserializeJSON(const char *json, size_t length, WorldRecipe *out,
                                 char *error, size_t errorSize) {
    if (!json || length == 0 || !out) { setErr(error, errorSize, "Entrada JSON nula"); return false; }
    WorldRecipe cfg = WorldRecipe_Default();
    cfg.id[0] = '\0';
    cfg.name[0] = '\0';

    WJson r = {.p = json, .end = json + length, .error = error, .errorSize = errorSize};
    if (!wjTake(&r, '{')) { setErr(error, errorSize, "Se esperaba '{' en *.world.json"); return false; }

    bool seenVersion = false, seenId = false;
    wjSpace(&r);
    if (r.p < r.end && *r.p != '}') {
        for (;;) {
            char key[64];
            if (!wjString(&r, key, sizeof(key)) || !wjTake(&r, ':')) return false;
            if (strcmp(key, "version") == 0) {
                double v = 0; if (!wjNumber(&r, &v) || v < 0.0 || floor(v) != v) return false;
                cfg.version = (uint32_t)v; seenVersion = true;
            } else if (strcmp(key, "generator_version") == 0) {
                double v = 0; if (!wjNumber(&r, &v) || v < 0.0 || floor(v) != v) return false;
                cfg.generatorVersion = (uint32_t)v;
            } else if (strcmp(key, "id") == 0) {
                if (!wjString(&r, cfg.id, sizeof(cfg.id))) return false;
                seenId = true;
            } else if (strcmp(key, "name") == 0) {
                if (!wjString(&r, cfg.name, sizeof(cfg.name))) return false;
            } else if (strcmp(key, "seed") == 0) {
                double v = 0; if (!wjNumber(&r, &v) || v < 0.0 || v > 4294967295.0) return false;
                cfg.seed = (uint32_t)v;
            } else if (strcmp(key, "size") == 0) {
                double v = 0; if (!wjNumber(&r, &v)) return false;
                cfg.size = (float)v;
            } else if (strcmp(key, "flat") == 0) {
                if (!wjBool(&r, &cfg.flat)) return false;
            } else if (strcmp(key, "terrain") == 0) {
                if (!wjTake(&r, '{')) { setErr(error, errorSize, "Se esperaba objeto 'terrain'"); return false; }
                wjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char tk[64]; double v = 0;
                        if (!wjString(&r, tk, sizeof(tk)) || !wjTake(&r, ':') || !wjNumber(&r, &v)) return false;
                        if (strcmp(tk, "resolution") == 0) cfg.terrain.resolution = (uint32_t)v;
                        else if (strcmp(tk, "height_scale") == 0) cfg.terrain.heightScale = (float)v;
                        else if (strcmp(tk, "base_frequency") == 0) cfg.terrain.baseFrequency = (float)v;
                        else if (strcmp(tk, "octaves") == 0) cfg.terrain.octaves = (uint32_t)v;
                        else if (strcmp(tk, "persistence") == 0) cfg.terrain.persistence = (float)v;
                        else if (strcmp(tk, "lacunarity") == 0) cfg.terrain.lacunarity = (float)v;
                        else if (strcmp(tk, "ridge_weight") == 0) cfg.terrain.ridgeWeight = (float)v;
                        else if (strcmp(tk, "domain_warp_strength") == 0) cfg.terrain.domainWarpStrength = (float)v;
                        else if (strcmp(tk, "valley_shaping") == 0) cfg.terrain.valleyShaping = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en terrain: '%s'", tk); return false; }
                        if (!wjTake(&r, ',')) break;
                    }
                }
                if (!wjTake(&r, '}')) return false;
            } else if (strcmp(key, "erosion") == 0) {
                if (!wjTake(&r, '{')) { setErr(error, errorSize, "Se esperaba objeto 'erosion'"); return false; }
                wjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char ek[64]; double v = 0;
                        if (!wjString(&r, ek, sizeof(ek)) || !wjTake(&r, ':') || !wjNumber(&r, &v)) return false;
                        if (strcmp(ek, "thermal_iterations") == 0) cfg.erosion.thermalIterations = (uint32_t)v;
                        else if (strcmp(ek, "talus_angle_degrees") == 0) cfg.erosion.talusAngleDegrees = (float)v;
                        else if (strcmp(ek, "thermal_rate") == 0) cfg.erosion.thermalRate = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en erosion: '%s'", ek); return false; }
                        if (!wjTake(&r, ',')) break;
                    }
                }
                if (!wjTake(&r, '}')) return false;
            } else if (strcmp(key, "hydrology") == 0) {
                if (!wjTake(&r, '{')) { setErr(error, errorSize, "Se esperaba objeto 'hydrology'"); return false; }
                wjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char hk[64]; char sv[64]; double v = 0;
                        if (!wjString(&r, hk, sizeof(hk)) || !wjTake(&r, ':')) return false;
                        if (strcmp(hk, "enabled") == 0) { if (!wjBool(&r, &cfg.hydrology.enabled)) return false; }
                        else if (strcmp(hk, "outlet_policy") == 0) {
                            if (!wjString(&r, sv, sizeof(sv))) return false;
                            if (strcmp(sv, "open_boundary") == 0) cfg.hydrology.outletPolicy = WORLD_OUTLET_OPEN_BOUNDARY;
                            else { setErr(error, errorSize, "outlet_policy no soportada: '%s'", sv); return false; }
                        } else if (strcmp(hk, "river_density") == 0) {
                            if (!wjNumber(&r, &v)) return false;
                            cfg.hydrology.riverDensity = (float)v;
                        } else if (strcmp(hk, "lake_density") == 0) {
                            if (!wjNumber(&r, &v)) return false;
                            cfg.hydrology.lakeDensity = (float)v;
                        } else if (strcmp(hk, "river_width_scale") == 0) {
                            if (!wjNumber(&r, &v)) return false;
                            cfg.hydrology.riverWidthScale = (float)v;
                        } else if (strcmp(hk, "river_depth_scale") == 0) {
                            if (!wjNumber(&r, &v)) return false;
                            cfg.hydrology.riverDepthScale = (float)v;
                        } else if (strcmp(hk, "water_substance") == 0) {
                            if (!wjString(&r, cfg.hydrology.waterSubstanceId, sizeof(cfg.hydrology.waterSubstanceId))) return false;
                        } else if (strcmp(hk, "reservoir_policy") == 0) {
                            if (!wjString(&r, sv, sizeof(sv))) return false;
                            if (strcmp(sv, "infinite_external") == 0) cfg.hydrology.reservoirPolicy = WORLD_RESERVOIR_INFINITE_EXTERNAL;
                            else if (strcmp(sv, "finite_fixed_volume") == 0) cfg.hydrology.reservoirPolicy = WORLD_RESERVOIR_FINITE_FIXED_VOLUME;
                            else { setErr(error, errorSize, "reservoir_policy no soportada: '%s'", sv); return false; }
                        } else if (strcmp(hk, "finite_volume") == 0) {
                            if (!wjNumber(&r, &v)) return false;
                            cfg.hydrology.finiteVolume = (float)v;
                        } else {
                            setErr(error, errorSize, "Clave desconocida en hydrology: '%s'", hk);
                            return false;
                        }
                        if (!wjTake(&r, ',')) break;
                    }
                }
                if (!wjTake(&r, '}')) return false;
            } else if (strcmp(key, "geology") == 0) {
                if (!wjTake(&r, '{')) { setErr(error, errorSize, "Se esperaba objeto 'geology'"); return false; }
                wjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char gk[64]; double v = 0;
                        if (!wjString(&r, gk, sizeof(gk)) || !wjTake(&r, ':') || !wjNumber(&r, &v)) return false;
                        if (strcmp(gk, "rock_density") == 0) cfg.geology.rockDensity = (float)v;
                        else if (strcmp(gk, "rock_variants") == 0) cfg.geology.rockVariants = (uint32_t)v;
                        else if (strcmp(gk, "min_rock_scale") == 0) cfg.geology.minRockScale = (float)v;
                        else if (strcmp(gk, "max_rock_scale") == 0) cfg.geology.maxRockScale = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en geology: '%s'", gk); return false; }
                        if (!wjTake(&r, ',')) break;
                    }
                }
                if (!wjTake(&r, '}')) return false;
            } else if (strcmp(key, "vegetation") == 0) {
                if (!wjTake(&r, '{')) { setErr(error, errorSize, "Se esperaba objeto 'vegetation'"); return false; }
                wjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char vk[64]; double v = 0;
                        if (!wjString(&r, vk, sizeof(vk)) || !wjTake(&r, ':') || !wjNumber(&r, &v)) return false;
                        if (strcmp(vk, "density") == 0) cfg.vegetation.density = (float)v;
                        else if (strcmp(vk, "fertility_bias") == 0) cfg.vegetation.fertilityBias = (float)v;
                        else if (strcmp(vk, "moisture_bias") == 0) cfg.vegetation.moistureBias = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en vegetation: '%s'", vk); return false; }
                        if (!wjTake(&r, ',')) break;
                    }
                }
                if (!wjTake(&r, '}')) return false;
            } else {
                setErr(error, errorSize, "Clave desconocida en *.world.json: '%s'", key);
                return false;
            }
            if (!wjTake(&r, ',')) break;
        }
    }
    if (!wjTake(&r, '}')) { setErr(error, errorSize, "Falta '}' final en *.world.json"); return false; }
    wjSpace(&r);
    if (r.p != r.end) { setErr(error, errorSize, "Contenido inesperado tras '}' en *.world.json"); return false; }
    if (!seenVersion || !seenId) {
        setErr(error, errorSize, "Receta de mundo incompleta: requiere 'version' e 'id'");
        return false;
    }
    if (!cfg.name[0]) snprintf(cfg.name, sizeof(cfg.name), "%s", cfg.id);
    if (!WorldRecipe_Validate(&cfg, error, errorSize)) return false;
    cfg.recipeHash = WorldRecipe_Fingerprint(&cfg);
    cfg.valid = true;
    *out = cfg;
    return true;
}

bool WorldRecipe_LoadFile(const char *path, WorldRecipe *out, char *error, size_t errorSize) {
    if (!path || !path[0] || !out) { setErr(error, errorSize, "Ruta de receta de mundo vacía"); return false; }
    FILE *f = fopen(path, "rb");
    if (!f) { setErr(error, errorSize, "No se pudo abrir '%s': %s", path, strerror(errno)); return false; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz <= 0 || sz > 1024L * 1024L) {
        fclose(f);
        setErr(error, errorSize, "Tamaño inválido de archivo de mundo '%s'", path);
        return false;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return false; }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (nread != (size_t)sz) { free(buf); return false; }
    buf[sz] = '\0';
    bool ok = WorldRecipe_DeserializeJSON(buf, (size_t)sz, out, error, errorSize);
    free(buf);
    return ok;
}

bool WorldRecipe_SerializeJSON(const WorldRecipe *r, char *buffer, size_t bufferSize, size_t *outLength) {
    if (!r) return false;
    const char *outletStr = "open_boundary";
    const char *resPolStr = (r->hydrology.reservoirPolicy == WORLD_RESERVOIR_FINITE_FIXED_VOLUME)
                                ? "finite_fixed_volume" : "infinite_external";
    int n = snprintf(buffer, bufferSize,
        "{\n"
        "  \"version\": %u,\n"
        "  \"generator_version\": %u,\n"
        "  \"id\": \"%s\",\n"
        "  \"name\": \"%s\",\n"
        "  \"seed\": %u,\n"
        "  \"size\": %.3f,\n"
        "  \"flat\": %s,\n"
        "  \"terrain\": {\n"
        "    \"resolution\": %u,\n"
        "    \"height_scale\": %.4f,\n"
        "    \"base_frequency\": %.6f,\n"
        "    \"octaves\": %u,\n"
        "    \"persistence\": %.4f,\n"
        "    \"lacunarity\": %.4f,\n"
        "    \"ridge_weight\": %.4f,\n"
        "    \"domain_warp_strength\": %.4f,\n"
        "    \"valley_shaping\": %.4f\n"
        "  },\n"
        "  \"erosion\": {\n"
        "    \"thermal_iterations\": %u,\n"
        "    \"talus_angle_degrees\": %.3f,\n"
        "    \"thermal_rate\": %.4f\n"
        "  },\n"
        "  \"hydrology\": {\n"
        "    \"enabled\": %s,\n"
        "    \"outlet_policy\": \"%s\",\n"
        "    \"river_density\": %.4f,\n"
        "    \"lake_density\": %.4f,\n"
        "    \"river_width_scale\": %.4f,\n"
        "    \"river_depth_scale\": %.4f,\n"
        "    \"water_substance\": \"%s\",\n"
        "    \"reservoir_policy\": \"%s\",\n"
        "    \"finite_volume\": %.3f\n"
        "  },\n"
        "  \"geology\": {\n"
        "    \"rock_density\": %.4f,\n"
        "    \"rock_variants\": %u,\n"
        "    \"min_rock_scale\": %.4f,\n"
        "    \"max_rock_scale\": %.4f\n"
        "  },\n"
        "  \"vegetation\": {\n"
        "    \"density\": %.4f,\n"
        "    \"fertility_bias\": %.4f,\n"
        "    \"moisture_bias\": %.4f\n"
        "  }\n"
        "}\n",
        r->version, r->generatorVersion, r->id, r->name[0] ? r->name : r->id,
        r->seed, (double)r->size, r->flat ? "true" : "false",
        r->terrain.resolution, (double)r->terrain.heightScale, (double)r->terrain.baseFrequency,
        r->terrain.octaves, (double)r->terrain.persistence, (double)r->terrain.lacunarity,
        (double)r->terrain.ridgeWeight, (double)r->terrain.domainWarpStrength, (double)r->terrain.valleyShaping,
        r->erosion.thermalIterations, (double)r->erosion.talusAngleDegrees, (double)r->erosion.thermalRate,
        r->hydrology.enabled ? "true" : "false", outletStr,
        (double)r->hydrology.riverDensity, (double)r->hydrology.lakeDensity,
        (double)r->hydrology.riverWidthScale, (double)r->hydrology.riverDepthScale,
        r->hydrology.waterSubstanceId, resPolStr, (double)r->hydrology.finiteVolume,
        (double)r->geology.rockDensity, r->geology.rockVariants,
        (double)r->geology.minRockScale, (double)r->geology.maxRockScale,
        (double)r->vegetation.density, (double)r->vegetation.fertilityBias, (double)r->vegetation.moistureBias);
    if (n < 0) return false;
    if (outLength) *outLength = (size_t)n;
    return buffer == NULL || (size_t)n < bufferSize;
}

bool WorldRecipe_SaveFile(const WorldRecipe *recipe, const char *path, char *error, size_t errorSize) {
    if (!recipe || !path || !path[0]) { setErr(error, errorSize, "Argumentos inválidos al guardar receta"); return false; }
    if (!WorldRecipe_Validate(recipe, error, errorSize)) return false;
    size_t needed = 0;
    if (!WorldRecipe_SerializeJSON(recipe, NULL, 0, &needed)) return false;
    char *buf = malloc(needed + 1u);
    if (!buf) return false;
    if (!WorldRecipe_SerializeJSON(recipe, buf, needed + 1u, &needed)) { free(buf); return false; }

    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        free(buf);
        setErr(error, errorSize, "No se pudo crear archivo temporal '%s': %s", tmp, strerror(errno));
        return false;
    }
    bool ok = (fwrite(buf, 1, needed, f) == needed) && (fflush(f) == 0);
    if (fclose(f) != 0) ok = false;
    free(buf);
    if (!ok) { remove(tmp); setErr(error, errorSize, "Error de escritura en '%s'", tmp); return false; }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        setErr(error, errorSize, "Error al renombrar '%s' -> '%s': %s", tmp, path, strerror(errno));
        return false;
    }
    return true;
}
