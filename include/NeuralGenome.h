/**
 * @file NeuralGenome.h
 * @brief Genoma neuronal heredable multi-arquitectura (v1 fixed_rnn y v2 recurrent_candidate_policy).
 *
 * Independiente de la anatomía y del renderizado. El estado recurrente en ejecución vive
 * separado en @ref NeuralBrainRuntime y el estado digestivo vive separado en @ref MetabolicState.
 */
#ifndef NEURAL_GENOME_H
#define NEURAL_GENOME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEURAL_BRAIN_FORMAT_VERSION    1u
#define NEURAL_BRAIN_FORMAT_VERSION_V2 2u
#define NEURAL_SENSOR_SCHEMA_V1 "monster_sensor_v1"
#define NEURAL_ACTION_SCHEMA_V1 "monster_action_v1"
#define NEURAL_SENSOR_SCHEMA_V2 "monster_sensor_v2"
#define NEURAL_ACTION_SCHEMA_V2 "monster_action_v2"

/* Dimensiones v1 (fixed_rnn: 32 -> 12 -> 10, 670 parámetros) */
#define NEURAL_RNN_INPUTS  32u
#define NEURAL_RNN_HIDDEN  12u
#define NEURAL_RNN_OUTPUTS 10u

#define NEURAL_RNN_W_XH_COUNT (NEURAL_RNN_HIDDEN * NEURAL_RNN_INPUTS)  /* 384 */
#define NEURAL_RNN_W_HH_COUNT (NEURAL_RNN_HIDDEN * NEURAL_RNN_HIDDEN)  /* 144 */
#define NEURAL_RNN_W_HY_COUNT (NEURAL_RNN_OUTPUTS * NEURAL_RNN_HIDDEN) /* 120 */
#define NEURAL_RNN_B_H_COUNT  (NEURAL_RNN_HIDDEN)                      /*  12 */
#define NEURAL_RNN_B_Y_COUNT  (NEURAL_RNN_OUTPUTS)                     /*  10 */
#define NEURAL_RNN_PARAM_COUNT                                                  \
    (NEURAL_RNN_W_XH_COUNT + NEURAL_RNN_W_HH_COUNT + NEURAL_RNN_W_HY_COUNT +    \
     NEURAL_RNN_B_H_COUNT + NEURAL_RNN_B_Y_COUNT) /* 670 */

/* Dimensiones v2 (recurrent_candidate_policy: contexto 24->16 + global 3 + recurso 14+16->12->1 + criatura 11+16->10->4, 1416 parámetros) */
#define NEURAL_V2_CONTEXT_INPUTS          24u
#define NEURAL_V2_CONTEXT_HIDDEN          16u
#define NEURAL_V2_GLOBAL_ACTIONS          3u  /* IDLE, EXPLORE, REST */
#define NEURAL_V2_MAX_RESOURCE_CANDIDATES 8u
#define NEURAL_V2_RES_FEAT                14u
#define NEURAL_V2_RES_IN                  (NEURAL_V2_RES_FEAT + NEURAL_V2_CONTEXT_HIDDEN) /* 30 */
#define NEURAL_V2_RES_HIDDEN              12u
#define NEURAL_V2_MAX_CREATURE_CANDIDATES 6u
#define NEURAL_V2_CRE_FEAT                11u
#define NEURAL_V2_CRE_IN                  (NEURAL_V2_CRE_FEAT + NEURAL_V2_CONTEXT_HIDDEN) /* 27 */
#define NEURAL_V2_CRE_HIDDEN              10u
#define NEURAL_V2_CRE_ACTIONS             4u  /* ATTACK, FLEE, FOLLOW, MATE */

#define NEURAL_V2_CTX_WXH_COUNT (NEURAL_V2_CONTEXT_HIDDEN * NEURAL_V2_CONTEXT_INPUTS) /* 384 */
#define NEURAL_V2_CTX_WHH_COUNT (NEURAL_V2_CONTEXT_HIDDEN * NEURAL_V2_CONTEXT_HIDDEN) /* 256 */
#define NEURAL_V2_CTX_BH_COUNT  (NEURAL_V2_CONTEXT_HIDDEN)                            /*  16 */
#define NEURAL_V2_GLB_W_COUNT   (NEURAL_V2_GLOBAL_ACTIONS * NEURAL_V2_CONTEXT_HIDDEN) /*  48 */
#define NEURAL_V2_GLB_B_COUNT   (NEURAL_V2_GLOBAL_ACTIONS)                            /*   3 */
#define NEURAL_V2_RES_W1_COUNT  (NEURAL_V2_RES_HIDDEN * NEURAL_V2_RES_IN)             /* 360 */
#define NEURAL_V2_RES_B1_COUNT  (NEURAL_V2_RES_HIDDEN)                                /*  12 */
#define NEURAL_V2_RES_W2_COUNT  (NEURAL_V2_RES_HIDDEN)                                /*  12 */
#define NEURAL_V2_RES_B2_COUNT  (1u)                                                  /*   1 */
#define NEURAL_V2_CRE_W1_COUNT  (NEURAL_V2_CRE_HIDDEN * NEURAL_V2_CRE_IN)             /* 270 */
#define NEURAL_V2_CRE_B1_COUNT  (NEURAL_V2_CRE_HIDDEN)                                /*  10 */
#define NEURAL_V2_CRE_W2_COUNT  (NEURAL_V2_CRE_ACTIONS * NEURAL_V2_CRE_HIDDEN)        /*  40 */
#define NEURAL_V2_CRE_B2_COUNT  (NEURAL_V2_CRE_ACTIONS)                               /*   4 */

#define NEURAL_V2_PARAM_COUNT                                                         \
    (NEURAL_V2_CTX_WXH_COUNT + NEURAL_V2_CTX_WHH_COUNT + NEURAL_V2_CTX_BH_COUNT +     \
     NEURAL_V2_GLB_W_COUNT + NEURAL_V2_GLB_B_COUNT +                                  \
     NEURAL_V2_RES_W1_COUNT + NEURAL_V2_RES_B1_COUNT +                                \
     NEURAL_V2_RES_W2_COUNT + NEURAL_V2_RES_B2_COUNT +                                \
     NEURAL_V2_CRE_W1_COUNT + NEURAL_V2_CRE_B1_COUNT +                                \
     NEURAL_V2_CRE_W2_COUNT + NEURAL_V2_CRE_B2_COUNT) /* 1416 */

#define NEURAL_MAX_PARAM_COUNT NEURAL_V2_PARAM_COUNT

#define NEURAL_GENOME_DEFAULT_WEIGHT_LIMIT 5.0f
#define NEURAL_GENOME_MAX_WEIGHT_LIMIT     10.0f

/** @brief Arquitecturas soportadas con despacho explícito y serialización versionada. */
typedef enum NeuralArchitectureType {
    NEURAL_ARCH_NONE = 0,
    NEURAL_ARCH_FIXED_RNN = 1,
    NEURAL_ARCH_CANDIDATE_POLICY = 2
} NeuralArchitectureType;

/** @brief Estrategia de recombinación biparental para redes neuronales. */
typedef enum NeuralCrossoverMode {
    NEURAL_CROSSOVER_NEURON_WISE = 0,   /**< Cruce por neurona (conserva circuitos recurrentes). */
    NEURAL_CROSSOVER_UNIFORM = 1,       /**< Cruce independiente por parámetro. */
    NEURAL_CROSSOVER_MUTATION_ONLY = 2  /**< Copia parentA sin cruce; sólo mutación. */
} NeuralCrossoverMode;

/** @brief Parámetros configurables de mutación y recombinación neuronal. */
typedef struct NeuralMutationConfig {
    float mutationRate;      /**< Probabilidad por parámetro de perturbación Gaussiana (defecto 0.05). */
    float mutationSigma;     /**< Desviación estándar Gaussiana (defecto 0.15). */
    float replaceRate;       /**< Probabilidad por parámetro de reemplazo aleatorio (defecto 0.005). */
    float replaceScale;      /**< Semiamplitud de reemplazo uniforme [-scale, +scale] (defecto 1.0). */
    float weightLimit;       /**< Cota absoluta de pesos y sesgos (defecto 5.0). */
    NeuralCrossoverMode crossoverMode; /**< Modo de recombinación (defecto NEURON_WISE). */
} NeuralMutationConfig;

/** @brief Metadatos de trazabilidad genealógica y entrenamiento; nunca restringen asignación a otro cuerpo. */
typedef struct NeuralGenomeMetadata {
    char label[64];
    char origin[32]; /**< "random", "seeded_baseline", "imported", "inherited", "cloned", "evolved". */
    uint64_t lineageId;          /**< Identificador estable de linaje ancestral. */
    uint32_t generation;         /**< Generación biológica/genealógica del cerebro. */
    uint32_t parentIds[2];       /**< IDs de progenitores si fue heredado. */
    uint64_t fingerprint;        /**< Hash FNV-1a canónico de topología y parámetros. */
    uint64_t founderFingerprint; /**< Huella del ancestro fundador para medir deriva. */
    /* Metadatos opcionales de evaluación del laboratorio (separados de generación biológica). */
    char trainedSpecies[48];
    char trainedEnvironment[64];
    uint32_t trainerGeneration;
    float validationFitness;
    float validationSurvival;
} NeuralGenomeMetadata;

/** @brief Parámetros heredables de la arquitectura v2 (`recurrent_candidate_policy`). */
typedef struct NeuralCandidatePolicyParams {
    float ctx_w_xh[NEURAL_V2_CTX_WXH_COUNT];
    float ctx_w_hh[NEURAL_V2_CTX_WHH_COUNT];
    float ctx_b_h[NEURAL_V2_CTX_BH_COUNT];

    float global_w[NEURAL_V2_GLB_W_COUNT];
    float global_b[NEURAL_V2_GLB_B_COUNT];

    float res_w1[NEURAL_V2_RES_W1_COUNT];
    float res_b1[NEURAL_V2_RES_B1_COUNT];
    float res_w2[NEURAL_V2_RES_W2_COUNT];
    float res_b2[NEURAL_V2_RES_B2_COUNT];

    float cre_w1[NEURAL_V2_CRE_W1_COUNT];
    float cre_b1[NEURAL_V2_CRE_B1_COUNT];
    float cre_w2[NEURAL_V2_CRE_W2_COUNT];
    float cre_b2[NEURAL_V2_CRE_B2_COUNT];
} NeuralCandidatePolicyParams;

/**
 * @brief Genoma neuronal autocontenido con almacenamiento etiquetado por arquitectura.
 *
 * Usa unión etiquetada por @ref architecture para evitar duplicar arreglos de todas las
 * arquitecturas simultáneamente y mantener copias seguras sin asignaciones dinámicas.
 */
typedef struct NeuralGenome {
    uint32_t formatVersion;
    NeuralArchitectureType architecture;
    char sensorSchema[32];
    char actionSchema[32];
    uint32_t inputCount;
    uint32_t hiddenCount;
    uint32_t outputCount;
    union {
        struct {
            float w_xh[NEURAL_RNN_W_XH_COUNT];
            float w_hh[NEURAL_RNN_W_HH_COUNT];
            float w_hy[NEURAL_RNN_W_HY_COUNT];
            float b_h[NEURAL_RNN_B_H_COUNT];
            float b_y[NEURAL_RNN_B_Y_COUNT];
        };
        NeuralCandidatePolicyParams v2;
    };
    NeuralGenomeMetadata metadata;
    bool valid;
} NeuralGenome;

/** @brief Configuración experimental por defecto de mutación neuronal. */
NeuralMutationConfig NeuralMutationConfig_Default(void);

/** @brief Valida probabilidades, desviaciones y límites de mutación. */
bool NeuralMutationConfig_Validate(const NeuralMutationConfig *config);

/** @brief Nombre canónico de la arquitectura ("fixed_rnn" | "recurrent_candidate_policy"). */
const char *NeuralArchitecture_Name(NeuralArchitectureType arch);

/** @brief Resuelve arquitectura a partir de su nombre textual. */
bool NeuralArchitecture_FromName(const char *name, NeuralArchitectureType *out);

/** @brief Nombre diagnóstico del modo de recombinación. */
const char *NeuralCrossoverMode_Name(NeuralCrossoverMode mode);

/** @brief Número total de parámetros entrenables del genoma (670 en v1, 1416 en v2). */
uint32_t NeuralGenome_ParamCount(const NeuralGenome *genome);

/** @brief Comprueba si dos genomas válidos comparten arquitectura y esquemas compatibles. */
bool NeuralGenome_Compatible(const NeuralGenome *a, const NeuralGenome *b);

/** @brief PRNG determinista xorshift32 en [0, 1) sin estado global. */
float NeuralGenome_RandomUnit(uint32_t *randomState);

/** @brief Muestra Gaussiana estándar N(0, 1) determinista mediante Box-Muller acotado. */
float NeuralGenome_RandomGaussian(uint32_t *randomState);

/** @brief Inicializa un genoma v1 RNN válido con parámetros a cero. */
void NeuralGenome_Init(NeuralGenome *genome);

/** @brief Inicializa un genoma v2 `recurrent_candidate_policy` válido con parámetros a cero. */
void NeuralGenome_InitV2(NeuralGenome *genome);

/** @brief Limpia de forma segura todos los recursos propiedad del genoma. */
void NeuralGenome_Destroy(NeuralGenome *genome);

/** @brief Inicializa un genoma v1 aleatorio acotado determinista (escala Xavier/Glorot). */
bool NeuralGenome_InitRandom(NeuralGenome *genome, uint32_t *randomState, float scale,
                             float weightLimit, uint64_t lineageId);

/** @brief Inicializa un genoma v2 `recurrent_candidate_policy` aleatorio acotado determinista. */
bool NeuralGenome_InitRandomV2(NeuralGenome *genome, uint32_t *randomState, float scale,
                               float weightLimit, uint64_t lineageId);

/** @brief Inicializa el genoma base genérico v1 puramente mediante pesos de la RNN. */
bool NeuralGenome_InitSeededBaseline(NeuralGenome *genome, uint64_t lineageId);

/** @brief Valida arquitectura, esquemas, dimensiones, finitud y cotas de parámetros. */
bool NeuralGenome_Validate(const NeuralGenome *genome);

/** @brief Copia profunda independiente; no modifica el origen ni comparte memoria mutable. */
bool NeuralGenome_Clone(NeuralGenome *dst, const NeuralGenome *src);

/** @brief Calcula la huella canónica FNV-1a de 64 bits de topología y parámetros. */
uint64_t NeuralGenome_Fingerprint(const NeuralGenome *genome);

/** @brief Actualiza genome->metadata.fingerprint si el genoma es válido. */
uint64_t NeuralGenome_UpdateFingerprint(NeuralGenome *genome);

/** @brief Distancia RMS L2 entre los parámetros de dos genomas de arquitectura compatible. */
float NeuralGenome_Distance(const NeuralGenome *a, const NeuralGenome *b);

/** @brief Copia el vector plano de parámetros de un genoma válido a @p out (para métricas O(N*P)). */
uint32_t NeuralGenome_CopyFlatParams(const NeuralGenome *genome, float *out, uint32_t capacity);

/** @brief Aplica mutación acotada in-place sobre un genoma válido (v1 o v2). */
bool NeuralGenome_Mutate(NeuralGenome *genome, NeuralMutationConfig config, uint32_t *randomState);

/**
 * @brief Genera un genoma hijo independiente por recombinación y mutación.
 *
 * Rechaza el cruce entre arquitecturas incompatibles (devuelve false sin alterar @p child).
 */
bool NeuralGenome_Inherit(NeuralGenome *child, const NeuralGenome *parentA,
                          const NeuralGenome *parentB, NeuralMutationConfig config,
                          uint32_t *randomState);

#ifdef __cplusplus
}
#endif

#endif /* NEURAL_GENOME_H */
