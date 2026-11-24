/**
 * @file NeuralGenome.c
 * @brief Implementación del genoma neuronal recurrente, recombinación y mutación determinista.
 */
#include "NeuralGenome.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NEURAL_TWO_PI 6.28318530717958647692f

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float canonicalFloat(float v) {
    return v == 0.0f ? 0.0f : v;
}

static void fnv1aBytes(uint64_t *hash, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; ++i) {
        *hash ^= (uint64_t)p[i];
        *hash *= UINT64_C(1099511628211);
    }
}

static void fnv1aFloat(uint64_t *hash, float v) {
    float c = canonicalFloat(v);
    uint32_t bits = 0;
    memcpy(&bits, &c, sizeof(bits));
    fnv1aBytes(hash, &bits, sizeof(bits));
}

static void fnv1aU32(uint64_t *hash, uint32_t v) {
    fnv1aBytes(hash, &v, sizeof(v));
}

NeuralMutationConfig NeuralMutationConfig_Default(void) {
    return (NeuralMutationConfig){
        .mutationRate = 0.05f,
        .mutationSigma = 0.15f,
        .replaceRate = 0.005f,
        .replaceScale = 1.0f,
        .weightLimit = NEURAL_GENOME_DEFAULT_WEIGHT_LIMIT,
        .crossoverMode = NEURAL_CROSSOVER_NEURON_WISE
    };
}

bool NeuralMutationConfig_Validate(const NeuralMutationConfig *config) {
    if (!config) return false;
    if (!isfinite(config->mutationRate) || config->mutationRate < 0.0f || config->mutationRate > 1.0f)
        return false;
    if (!isfinite(config->mutationSigma) || config->mutationSigma < 0.0f || config->mutationSigma > 10.0f)
        return false;
    if (!isfinite(config->replaceRate) || config->replaceRate < 0.0f || config->replaceRate > 1.0f)
        return false;
    if (!isfinite(config->replaceScale) || config->replaceScale < 0.0f ||
        config->replaceScale > NEURAL_GENOME_MAX_WEIGHT_LIMIT)
        return false;
    if (!isfinite(config->weightLimit) || config->weightLimit <= 0.0f ||
        config->weightLimit > NEURAL_GENOME_MAX_WEIGHT_LIMIT)
        return false;
    if (config->crossoverMode < NEURAL_CROSSOVER_NEURON_WISE ||
        config->crossoverMode > NEURAL_CROSSOVER_MUTATION_ONLY)
        return false;
    return true;
}

const char *NeuralArchitecture_Name(NeuralArchitectureType arch) {
    switch (arch) {
    case NEURAL_ARCH_FIXED_RNN:
        return "fixed_rnn";
    default:
        return "unknown";
    }
}

bool NeuralArchitecture_FromName(const char *name, NeuralArchitectureType *out) {
    if (!name || !out) return false;
    if (strcmp(name, "fixed_rnn") == 0) {
        *out = NEURAL_ARCH_FIXED_RNN;
        return true;
    }
    return false;
}

const char *NeuralCrossoverMode_Name(NeuralCrossoverMode mode) {
    switch (mode) {
    case NEURAL_CROSSOVER_NEURON_WISE:
        return "neuron_wise";
    case NEURAL_CROSSOVER_UNIFORM:
        return "uniform";
    case NEURAL_CROSSOVER_MUTATION_ONLY:
        return "mutation_only";
    default:
        return "unknown";
    }
}

float NeuralGenome_RandomUnit(uint32_t *randomState) {
    uint32_t x = (randomState && *randomState) ? *randomState : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (randomState) *randomState = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

float NeuralGenome_RandomGaussian(uint32_t *randomState) {
    float u1 = NeuralGenome_RandomUnit(randomState);
    float u2 = NeuralGenome_RandomUnit(randomState);
    if (u1 < 1e-7f) u1 = 1e-7f;
    float radius = sqrtf(-2.0f * logf(u1));
    float theta = NEURAL_TWO_PI * u2;
    float z = radius * cosf(theta);
    return isfinite(z) ? z : 0.0f;
}

void NeuralGenome_Init(NeuralGenome *genome) {
    if (!genome) return;
    memset(genome, 0, sizeof(*genome));
    genome->formatVersion = NEURAL_BRAIN_FORMAT_VERSION;
    genome->architecture = NEURAL_ARCH_FIXED_RNN;
    snprintf(genome->sensorSchema, sizeof(genome->sensorSchema), "%s", NEURAL_SENSOR_SCHEMA_V1);
    snprintf(genome->actionSchema, sizeof(genome->actionSchema), "%s", NEURAL_ACTION_SCHEMA_V1);
    genome->inputCount = NEURAL_RNN_INPUTS;
    genome->hiddenCount = NEURAL_RNN_HIDDEN;
    genome->outputCount = NEURAL_RNN_OUTPUTS;
    snprintf(genome->metadata.label, sizeof(genome->metadata.label), "zero_rnn");
    snprintf(genome->metadata.origin, sizeof(genome->metadata.origin), "zero");
    genome->valid = true;
    NeuralGenome_UpdateFingerprint(genome);
    genome->metadata.founderFingerprint = genome->metadata.fingerprint;
}

void NeuralGenome_Destroy(NeuralGenome *genome) {
    if (!genome) return;
    memset(genome, 0, sizeof(*genome));
}

static bool validateFloatArray(const float *arr, size_t count, float limit) {
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(arr[i]) || fabsf(arr[i]) > limit) return false;
    }
    return true;
}

bool NeuralGenome_Validate(const NeuralGenome *genome) {
    if (!genome || !genome->valid) return false;
    if (genome->formatVersion != NEURAL_BRAIN_FORMAT_VERSION) return false;
    if (genome->architecture != NEURAL_ARCH_FIXED_RNN) return false;
    if (strcmp(genome->sensorSchema, NEURAL_SENSOR_SCHEMA_V1) != 0) return false;
    if (strcmp(genome->actionSchema, NEURAL_ACTION_SCHEMA_V1) != 0) return false;
    if (genome->inputCount != NEURAL_RNN_INPUTS ||
        genome->hiddenCount != NEURAL_RNN_HIDDEN ||
        genome->outputCount != NEURAL_RNN_OUTPUTS)
        return false;
    if (!validateFloatArray(genome->w_xh, NEURAL_RNN_W_XH_COUNT, NEURAL_GENOME_MAX_WEIGHT_LIMIT))
        return false;
    if (!validateFloatArray(genome->w_hh, NEURAL_RNN_W_HH_COUNT, NEURAL_GENOME_MAX_WEIGHT_LIMIT))
        return false;
    if (!validateFloatArray(genome->w_hy, NEURAL_RNN_W_HY_COUNT, NEURAL_GENOME_MAX_WEIGHT_LIMIT))
        return false;
    if (!validateFloatArray(genome->b_h, NEURAL_RNN_B_H_COUNT, NEURAL_GENOME_MAX_WEIGHT_LIMIT))
        return false;
    if (!validateFloatArray(genome->b_y, NEURAL_RNN_B_Y_COUNT, NEURAL_GENOME_MAX_WEIGHT_LIMIT))
        return false;
    return true;
}

bool NeuralGenome_Clone(NeuralGenome *dst, const NeuralGenome *src) {
    if (!dst || !NeuralGenome_Validate(src)) return false;
    if (dst != src) {
        memcpy(dst, src, sizeof(*dst));
    }
    NeuralGenome_UpdateFingerprint(dst);
    return true;
}

uint64_t NeuralGenome_Fingerprint(const NeuralGenome *genome) {
    if (!NeuralGenome_Validate(genome)) return 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    fnv1aU32(&hash, genome->formatVersion);
    fnv1aU32(&hash, (uint32_t)genome->architecture);
    fnv1aBytes(&hash, genome->sensorSchema, strlen(genome->sensorSchema));
    fnv1aBytes(&hash, genome->actionSchema, strlen(genome->actionSchema));
    fnv1aU32(&hash, genome->inputCount);
    fnv1aU32(&hash, genome->hiddenCount);
    fnv1aU32(&hash, genome->outputCount);
    for (size_t i = 0; i < NEURAL_RNN_W_XH_COUNT; ++i) fnv1aFloat(&hash, genome->w_xh[i]);
    for (size_t i = 0; i < NEURAL_RNN_W_HH_COUNT; ++i) fnv1aFloat(&hash, genome->w_hh[i]);
    for (size_t i = 0; i < NEURAL_RNN_W_HY_COUNT; ++i) fnv1aFloat(&hash, genome->w_hy[i]);
    for (size_t i = 0; i < NEURAL_RNN_B_H_COUNT; ++i) fnv1aFloat(&hash, genome->b_h[i]);
    for (size_t i = 0; i < NEURAL_RNN_B_Y_COUNT; ++i) fnv1aFloat(&hash, genome->b_y[i]);
    return hash;
}

uint64_t NeuralGenome_UpdateFingerprint(NeuralGenome *genome) {
    if (!genome) return 0u;
    uint64_t fp = NeuralGenome_Fingerprint(genome);
    genome->metadata.fingerprint = fp;
    return fp;
}

float NeuralGenome_Distance(const NeuralGenome *a, const NeuralGenome *b) {
    if (!NeuralGenome_Validate(a) || !NeuralGenome_Validate(b)) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < NEURAL_RNN_W_XH_COUNT; ++i) {
        double d = (double)a->w_xh[i] - (double)b->w_xh[i];
        sumSq += d * d;
    }
    for (size_t i = 0; i < NEURAL_RNN_W_HH_COUNT; ++i) {
        double d = (double)a->w_hh[i] - (double)b->w_hh[i];
        sumSq += d * d;
    }
    for (size_t i = 0; i < NEURAL_RNN_W_HY_COUNT; ++i) {
        double d = (double)a->w_hy[i] - (double)b->w_hy[i];
        sumSq += d * d;
    }
    for (size_t i = 0; i < NEURAL_RNN_B_H_COUNT; ++i) {
        double d = (double)a->b_h[i] - (double)b->b_h[i];
        sumSq += d * d;
    }
    for (size_t i = 0; i < NEURAL_RNN_B_Y_COUNT; ++i) {
        double d = (double)a->b_y[i] - (double)b->b_y[i];
        sumSq += d * d;
    }
    return (float)sqrt(sumSq / (double)NEURAL_RNN_PARAM_COUNT);
}

bool NeuralGenome_InitRandom(NeuralGenome *genome, uint32_t *randomState, float scale,
                             float weightLimit, uint64_t lineageId) {
    if (!genome || !randomState) return false;
    if (!isfinite(scale) || scale <= 0.0f || scale > NEURAL_GENOME_MAX_WEIGHT_LIMIT)
        scale = 0.6f;
    if (!isfinite(weightLimit) || weightLimit <= 0.0f || weightLimit > NEURAL_GENOME_MAX_WEIGHT_LIMIT)
        weightLimit = NEURAL_GENOME_DEFAULT_WEIGHT_LIMIT;

    NeuralGenome_Init(genome);
    float sXh = scale * sqrtf(2.0f / (float)(NEURAL_RNN_INPUTS + NEURAL_RNN_HIDDEN));
    float sHh = scale * sqrtf(1.0f / (float)NEURAL_RNN_HIDDEN);
    float sHy = scale * sqrtf(2.0f / (float)(NEURAL_RNN_HIDDEN + NEURAL_RNN_OUTPUTS));

    for (size_t i = 0; i < NEURAL_RNN_W_XH_COUNT; ++i)
        genome->w_xh[i] = clampf(NeuralGenome_RandomGaussian(randomState) * sXh, -weightLimit, weightLimit);
    for (size_t i = 0; i < NEURAL_RNN_W_HH_COUNT; ++i)
        genome->w_hh[i] = clampf(NeuralGenome_RandomGaussian(randomState) * sHh, -weightLimit, weightLimit);
    for (size_t i = 0; i < NEURAL_RNN_W_HY_COUNT; ++i)
        genome->w_hy[i] = clampf(NeuralGenome_RandomGaussian(randomState) * sHy, -weightLimit, weightLimit);
    for (size_t i = 0; i < NEURAL_RNN_B_H_COUNT; ++i)
        genome->b_h[i] = clampf((NeuralGenome_RandomUnit(randomState) * 2.0f - 1.0f) * 0.25f, -weightLimit, weightLimit);
    for (size_t i = 0; i < NEURAL_RNN_B_Y_COUNT; ++i)
        genome->b_y[i] = clampf((NeuralGenome_RandomUnit(randomState) * 2.0f - 1.0f) * 0.25f, -weightLimit, weightLimit);

    snprintf(genome->metadata.label, sizeof(genome->metadata.label), "random_rnn");
    snprintf(genome->metadata.origin, sizeof(genome->metadata.origin), "random");
    genome->metadata.lineageId = lineageId ? lineageId : 1u;
    genome->metadata.generation = 0u;
    genome->valid = true;
    NeuralGenome_UpdateFingerprint(genome);
    genome->metadata.founderFingerprint = genome->metadata.fingerprint;
    return NeuralGenome_Validate(genome);
}

bool NeuralGenome_InitSeededBaseline(NeuralGenome *genome, uint64_t lineageId) {
    if (!genome) return false;
    NeuralGenome_Init(genome);

    /* Índices del esquema monster_sensor_v1:
     * 0:energy, 1:hydration, 2:fatigue, 3:health, 4:age, 5:mateReady,
     * 6:speed, 7:canAttack, 8:waterCap, 9:fail, 10:intake, 11:damage
     * 12..15: plant [conf, dist, sin, cos]
     * 16..19: meat  [conf, dist, sin, cos]
     * 20..23: water [conf, dist, sin, cos]
     * 24..27: conspecific [conf, dist, sin, cos]
     * 28..31: other species [conf, dist, sin, cos]
     *
     * Salidas monster_action_v1:
     * 0:IDLE, 1:EXPLORE, 2:REST, 3:HYDRATE, 4:FEED_PLANT,
     * 5:FEED_MEAT, 6:HUNT, 7:FLEE, 8:MATE, 9:FOLLOW
     */
    #define WXH(h, i, v) genome->w_xh[(h) * NEURAL_RNN_INPUTS + (i)] = (v)
    #define WHH(h, k, v) genome->w_hh[(h) * NEURAL_RNN_HIDDEN + (k)] = (v)
    #define WHY(o, h, v) genome->w_hy[(o) * NEURAL_RNN_HIDDEN + (h)] = (v)

    /* h0: Impulso de hidratación (baja hidratación + capacidad hídrica + agua percibida). */
    WXH(0, 1, -3.4f);
    WXH(0, 8,  0.9f);
    WXH(0, 20, 0.7f);
    WXH(0, 21, -0.35f);
    WXH(0, 9, -0.4f);
    WHH(0, 0,  0.30f);
    genome->b_h[0] = 0.55f;

    /* h1: Impulso general de búsqueda de alimento vegetal (baja energía, sin capacidad de ataque). */
    WXH(1, 0, -3.2f);
    WXH(1, 7, -2.2f);
    WXH(1, 12, 1.0f);
    WXH(1, 13, -0.35f);
    WXH(1, 9, -0.55f);
    WHH(1, 1,  0.25f);
    genome->b_h[1] = 0.90f;

    /* h2: Impulso de consumo de carne/carroña (baja energía + capacidad de ataque + carne visible). */
    WXH(2, 0, -2.8f);
    WXH(2, 7,  1.8f);
    WXH(2, 16, 2.2f);
    WXH(2, 17, -0.45f);
    WXH(2, 9, -0.45f);
    WHH(2, 2,  0.35f);
    genome->b_h[2] = -0.35f;

    /* h3: Impulso de caza (baja energía + capacidad de ataque + otra especie visible, inhibido si ya hay carne). */
    WXH(3, 0, -2.6f);
    WXH(3, 2, -0.8f);
    WXH(3, 7,  2.4f);
    WXH(3, 16, -1.6f);
    WXH(3, 28, 1.8f);
    WXH(3, 29, -0.7f);
    WXH(3, 9, -0.5f);
    WHH(3, 3,  0.40f);
    genome->b_h[3] = -0.85f;

    /* h4: Impulso de huida (otra especie cercana o daño reciente, más fuerte sin capacidad de ataque). */
    WXH(4, 7, -1.8f);
    WXH(4, 11, 2.8f);
    WXH(4, 28, 2.0f);
    WXH(4, 29, -3.2f);
    WHH(4, 4,  0.40f);
    genome->b_h[4] = -0.65f;

    /* h5: Impulso de descanso (fatiga alta). */
    WXH(5, 2,  3.2f);
    WXH(5, 0,  0.4f);
    WXH(5, 1,  0.4f);
    WXH(5, 11, -1.2f);
    WHH(5, 5,  0.35f);
    genome->b_h[5] = -2.20f;

    /* h6: Impulso reproductivo (madurez/disposición + conspecífico percibido + buenas reservas). */
    WXH(6, 5,  2.4f);
    WXH(6, 24, 1.6f);
    WXH(6, 25, -0.5f);
    WXH(6, 0,  1.2f);
    WXH(6, 1,  1.5f);
    WXH(6, 11, -1.5f);
    WHH(6, 6,  0.25f);
    genome->b_h[6] = -3.35f;

    /* h7: Impulso de exploración cuando faltan recursos visibles o tras fallo reciente. */
    WXH(7, 12, -0.5f);
    WXH(7, 16, -0.4f);
    WXH(7, 20, -0.5f);
    WXH(7, 9,   1.3f);
    WHH(7, 7,   0.25f);
    genome->b_h[7] = 0.55f;

    /* h8: Seguimiento social suave si está bien nutrido y ve conspecífico. */
    WXH(8, 24, 1.3f);
    WXH(8, 0,  0.6f);
    WXH(8, 1,  0.6f);
    WXH(8, 5, -0.9f);
    genome->b_h[8] = -1.35f;

    /* h9: Memoria recurrente de ingesta confirmada (sostiene brevemente la alimentación). */
    WXH(9, 10, 2.2f);
    WXH(9, 0, -0.8f);
    WXH(9, 1, -0.8f);
    WHH(9, 9,  0.45f);
    genome->b_h[9] = -0.30f;

    /* h10: Memoria recurrente de fallo reciente (rompe bucles improductivos). */
    WXH(10, 9,  2.4f);
    WHH(10, 10, 0.45f);
    genome->b_h[10] = -0.50f;

    /* h11: Urgencia de búsqueda para cazador sin presa ni carne a la vista. */
    WXH(11, 0, -2.0f);
    WXH(11, 7,  1.8f);
    WXH(11, 16, -1.2f);
    WXH(11, 28, -1.2f);
    genome->b_h[11] = -0.25f;

    /* Capa de salida: combinación lineal de estados ocultos. */
    /* 0: IDLE (bajo por defecto) */
    WHY(0, 5, 0.25f);
    genome->b_y[0] = -0.80f;

    /* 1: EXPLORE */
    WHY(1, 7,  0.95f);
    WHY(1, 10, 0.85f);
    WHY(1, 11, 1.35f);
    genome->b_y[1] = 0.35f;

    /* 2: REST */
    WHY(2, 5,  2.15f);
    WHY(2, 4, -0.90f);
    genome->b_y[2] = -0.10f;

    /* 3: HYDRATE */
    WHY(3, 0,  2.95f);
    WHY(3, 9,  0.35f);
    WHY(3, 4, -0.65f);
    genome->b_y[3] = 0.25f;

    /* 4: FEED_PLANT */
    WHY(4, 1,  2.75f);
    WHY(4, 9,  0.35f);
    WHY(4, 4, -0.80f);
    genome->b_y[4] = 0.10f;

    /* 5: FEED_MEAT */
    WHY(5, 2,  2.85f);
    WHY(5, 9,  0.40f);
    genome->b_y[5] = -0.05f;

    /* 6: HUNT */
    WHY(6, 3,  2.45f);
    WHY(6, 2, -0.90f);
    genome->b_y[6] = -0.15f;

    /* 7: FLEE */
    WHY(7, 4,  2.85f);
    genome->b_y[7] = -0.35f;

    /* 8: MATE */
    WHY(8, 6,  1.85f);
    WHY(8, 4, -1.20f);
    genome->b_y[8] = -0.15f;

    /* 9: FOLLOW */
    WHY(9, 8,  0.80f);
    genome->b_y[9] = -0.35f;

    #undef WXH
    #undef WHH
    #undef WHY

    snprintf(genome->metadata.label, sizeof(genome->metadata.label), "seeded_baseline_v1");
    snprintf(genome->metadata.origin, sizeof(genome->metadata.origin), "seeded_baseline");
    genome->metadata.lineageId = lineageId ? lineageId : 1u;
    genome->metadata.generation = 0u;
    genome->valid = true;
    NeuralGenome_UpdateFingerprint(genome);
    genome->metadata.founderFingerprint = genome->metadata.fingerprint;
    return NeuralGenome_Validate(genome);
}

static void mutateArray(float *arr, size_t count, NeuralMutationConfig cfg, uint32_t *rng) {
    for (size_t i = 0; i < count; ++i) {
        float v = arr[i];
        if (cfg.replaceRate > 0.0f && NeuralGenome_RandomUnit(rng) < cfg.replaceRate) {
            v = (NeuralGenome_RandomUnit(rng) * 2.0f - 1.0f) * cfg.replaceScale;
        } else if (cfg.mutationRate > 0.0f && cfg.mutationSigma > 0.0f &&
                   NeuralGenome_RandomUnit(rng) < cfg.mutationRate) {
            v += NeuralGenome_RandomGaussian(rng) * cfg.mutationSigma;
        }
        arr[i] = clampf(canonicalFloat(v), -cfg.weightLimit, cfg.weightLimit);
    }
}

bool NeuralGenome_Mutate(NeuralGenome *genome, NeuralMutationConfig config, uint32_t *randomState) {
    if (!NeuralGenome_Validate(genome) || !NeuralMutationConfig_Validate(&config) || !randomState)
        return false;
    mutateArray(genome->w_xh, NEURAL_RNN_W_XH_COUNT, config, randomState);
    mutateArray(genome->w_hh, NEURAL_RNN_W_HH_COUNT, config, randomState);
    mutateArray(genome->w_hy, NEURAL_RNN_W_HY_COUNT, config, randomState);
    mutateArray(genome->b_h, NEURAL_RNN_B_H_COUNT, config, randomState);
    mutateArray(genome->b_y, NEURAL_RNN_B_Y_COUNT, config, randomState);
    NeuralGenome_UpdateFingerprint(genome);
    return NeuralGenome_Validate(genome);
}

bool NeuralGenome_Inherit(NeuralGenome *child, const NeuralGenome *parentA,
                          const NeuralGenome *parentB, NeuralMutationConfig config,
                          uint32_t *randomState) {
    if (!child || !NeuralGenome_Validate(parentA) || !NeuralGenome_Validate(parentB) ||
        !NeuralMutationConfig_Validate(&config) || !randomState)
        return false;

    NeuralGenome temp;
    NeuralGenome_Init(&temp);

    if (config.crossoverMode == NEURAL_CROSSOVER_MUTATION_ONLY) {
        memcpy(temp.w_xh, parentA->w_xh, sizeof(temp.w_xh));
        memcpy(temp.w_hh, parentA->w_hh, sizeof(temp.w_hh));
        memcpy(temp.w_hy, parentA->w_hy, sizeof(temp.w_hy));
        memcpy(temp.b_h, parentA->b_h, sizeof(temp.b_h));
        memcpy(temp.b_y, parentA->b_y, sizeof(temp.b_y));
    } else if (config.crossoverMode == NEURAL_CROSSOVER_UNIFORM) {
        for (size_t i = 0; i < NEURAL_RNN_W_XH_COUNT; ++i)
            temp.w_xh[i] = NeuralGenome_RandomUnit(randomState) < 0.5f ? parentA->w_xh[i] : parentB->w_xh[i];
        for (size_t i = 0; i < NEURAL_RNN_W_HH_COUNT; ++i)
            temp.w_hh[i] = NeuralGenome_RandomUnit(randomState) < 0.5f ? parentA->w_hh[i] : parentB->w_hh[i];
        for (size_t i = 0; i < NEURAL_RNN_W_HY_COUNT; ++i)
            temp.w_hy[i] = NeuralGenome_RandomUnit(randomState) < 0.5f ? parentA->w_hy[i] : parentB->w_hy[i];
        for (size_t i = 0; i < NEURAL_RNN_B_H_COUNT; ++i)
            temp.b_h[i] = NeuralGenome_RandomUnit(randomState) < 0.5f ? parentA->b_h[i] : parentB->b_h[i];
        for (size_t i = 0; i < NEURAL_RNN_B_Y_COUNT; ++i)
            temp.b_y[i] = NeuralGenome_RandomUnit(randomState) < 0.5f ? parentA->b_y[i] : parentB->b_y[i];
    } else {
        /* NEURAL_CROSSOVER_NEURON_WISE: conserva unidades ocultas y filas de salida coherentes. */
        for (uint32_t h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
            const NeuralGenome *src = NeuralGenome_RandomUnit(randomState) < 0.5f ? parentA : parentB;
            memcpy(&temp.w_xh[h * NEURAL_RNN_INPUTS], &src->w_xh[h * NEURAL_RNN_INPUTS],
                   NEURAL_RNN_INPUTS * sizeof(float));
            memcpy(&temp.w_hh[h * NEURAL_RNN_HIDDEN], &src->w_hh[h * NEURAL_RNN_HIDDEN],
                   NEURAL_RNN_HIDDEN * sizeof(float));
            temp.b_h[h] = src->b_h[h];
        }
        for (uint32_t o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
            const NeuralGenome *src = NeuralGenome_RandomUnit(randomState) < 0.5f ? parentA : parentB;
            memcpy(&temp.w_hy[o * NEURAL_RNN_HIDDEN], &src->w_hy[o * NEURAL_RNN_HIDDEN],
                   NEURAL_RNN_HIDDEN * sizeof(float));
            temp.b_y[o] = src->b_y[o];
        }
    }

    if (config.mutationRate > 0.0f || config.replaceRate > 0.0f) {
        mutateArray(temp.w_xh, NEURAL_RNN_W_XH_COUNT, config, randomState);
        mutateArray(temp.w_hh, NEURAL_RNN_W_HH_COUNT, config, randomState);
        mutateArray(temp.w_hy, NEURAL_RNN_W_HY_COUNT, config, randomState);
        mutateArray(temp.b_h, NEURAL_RNN_B_H_COUNT, config, randomState);
        mutateArray(temp.b_y, NEURAL_RNN_B_Y_COUNT, config, randomState);
    } else {
        /* Incluso sin mutación, respetar la cota absoluta configurada. */
        for (size_t i = 0; i < NEURAL_RNN_W_XH_COUNT; ++i)
            temp.w_xh[i] = clampf(canonicalFloat(temp.w_xh[i]), -config.weightLimit, config.weightLimit);
        for (size_t i = 0; i < NEURAL_RNN_W_HH_COUNT; ++i)
            temp.w_hh[i] = clampf(canonicalFloat(temp.w_hh[i]), -config.weightLimit, config.weightLimit);
        for (size_t i = 0; i < NEURAL_RNN_W_HY_COUNT; ++i)
            temp.w_hy[i] = clampf(canonicalFloat(temp.w_hy[i]), -config.weightLimit, config.weightLimit);
        for (size_t i = 0; i < NEURAL_RNN_B_H_COUNT; ++i)
            temp.b_h[i] = clampf(canonicalFloat(temp.b_h[i]), -config.weightLimit, config.weightLimit);
        for (size_t i = 0; i < NEURAL_RNN_B_Y_COUNT; ++i)
            temp.b_y[i] = clampf(canonicalFloat(temp.b_y[i]), -config.weightLimit, config.weightLimit);
    }

    snprintf(temp.metadata.label, sizeof(temp.metadata.label), "evolved_rnn");
    snprintf(temp.metadata.origin, sizeof(temp.metadata.origin), "inherited");
    temp.metadata.lineageId = parentA->metadata.lineageId ? parentA->metadata.lineageId : 1u;
    temp.metadata.founderFingerprint = parentA->metadata.founderFingerprint;
    uint32_t maxParentGen = parentA->metadata.generation > parentB->metadata.generation
                                ? parentA->metadata.generation
                                : parentB->metadata.generation;
    temp.metadata.generation = maxParentGen + 1u;
    temp.valid = true;
    NeuralGenome_UpdateFingerprint(&temp);
    if (!NeuralGenome_Validate(&temp)) return false;

    *child = temp;
    return true;
}
