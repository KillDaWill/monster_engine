/**
 * @file NeuralGenome.h
 * @brief Genoma neuronal heredable, independiente de la anatomía y del renderizado.
 *
 * Define el genotipo de un controlador neuronal recurrente (topología fija 32->12->10
 * en este hito, con tipo de arquitectura extensible para futura neuroevolución NEAT).
 * El estado recurrente en ejecución vive separado en @ref NeuralBrainRuntime.
 */
#ifndef NEURAL_GENOME_H
#define NEURAL_GENOME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEURAL_BRAIN_FORMAT_VERSION 1u
#define NEURAL_SENSOR_SCHEMA_V1 "monster_sensor_v1"
#define NEURAL_ACTION_SCHEMA_V1 "monster_action_v1"

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

#define NEURAL_GENOME_DEFAULT_WEIGHT_LIMIT 5.0f
#define NEURAL_GENOME_MAX_WEIGHT_LIMIT     10.0f

/** @brief Arquitectura soportada; preparada para coexistir con NEAT recurrente futuro. */
typedef enum NeuralArchitectureType {
    NEURAL_ARCH_NONE = 0,
    NEURAL_ARCH_FIXED_RNN = 1
} NeuralArchitectureType;

/** @brief Estrategia de recombinación biparental para redes recurrentes de topología fija. */
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

/** @brief Metadatos de trazabilidad genealógica y formato; no participan en la inferencia. */
typedef struct NeuralGenomeMetadata {
    char label[64];
    char origin[32]; /**< "random", "seeded_baseline", "imported", "inherited", "cloned". */
    uint64_t lineageId;          /**< Identificador estable de linaje ancestral. */
    uint32_t generation;         /**< Generación genealógica del cerebro. */
    uint32_t parentIds[2];       /**< IDs de progenitores si fue heredado. */
    uint64_t fingerprint;        /**< Hash FNV-1a canónico de topología y parámetros. */
    uint64_t founderFingerprint; /**< Huella del ancestro fundador para medir deriva. */
} NeuralGenomeMetadata;

/**
 * @brief Genoma neuronal autocontenido y serializable.
 *
 * Disposición de matrices en memoria (row-major):
 * - w_xh[h * inputCount + i]: conexión desde entrada i hacia neurona oculta h.
 * - w_hh[h * hiddenCount + k]: conexión recurrente desde h_{t-1}[k] hacia h_t[h].
 * - w_hy[o * hiddenCount + h]: conexión desde neurona oculta h hacia salida o.
 */
typedef struct NeuralGenome {
    uint32_t formatVersion;
    NeuralArchitectureType architecture;
    char sensorSchema[32];
    char actionSchema[32];
    uint32_t inputCount;
    uint32_t hiddenCount;
    uint32_t outputCount;
    float w_xh[NEURAL_RNN_W_XH_COUNT];
    float w_hh[NEURAL_RNN_W_HH_COUNT];
    float w_hy[NEURAL_RNN_W_HY_COUNT];
    float b_h[NEURAL_RNN_B_H_COUNT];
    float b_y[NEURAL_RNN_B_Y_COUNT];
    NeuralGenomeMetadata metadata;
    bool valid;
} NeuralGenome;

/** @brief Configuración experimental por defecto de mutación neuronal. */
NeuralMutationConfig NeuralMutationConfig_Default(void);

/** @brief Valida probabilidades, desviaciones y límites de mutación. */
bool NeuralMutationConfig_Validate(const NeuralMutationConfig *config);

/** @brief Nombre canónico de la arquitectura ("fixed_rnn"). */
const char *NeuralArchitecture_Name(NeuralArchitectureType arch);

/** @brief Resuelve arquitectura a partir de su nombre textual. */
bool NeuralArchitecture_FromName(const char *name, NeuralArchitectureType *out);

/** @brief Nombre diagnóstico del modo de recombinación. */
const char *NeuralCrossoverMode_Name(NeuralCrossoverMode mode);

/** @brief PRNG determinista xorshift32 en [0, 1) sin estado global. */
float NeuralGenome_RandomUnit(uint32_t *randomState);

/** @brief Muestra Gaussiana estándar N(0, 1) determinista mediante Box-Muller acotado. */
float NeuralGenome_RandomGaussian(uint32_t *randomState);

/** @brief Inicializa un genoma RNN válido con parámetros a cero. */
void NeuralGenome_Init(NeuralGenome *genome);

/** @brief Limpia de forma segura todos los recursos propiedad del genoma. */
void NeuralGenome_Destroy(NeuralGenome *genome);

/** @brief Inicializa un genoma aleatorio acotado determinista (escala Xavier/Glorot). */
bool NeuralGenome_InitRandom(NeuralGenome *genome, uint32_t *randomState, float scale,
                             float weightLimit, uint64_t lineageId);

/**
 * @brief Inicializa el genoma base genérico puramente mediante pesos de la RNN.
 *
 * No consulta especies ni reglas ocultas: codifica respuestas básicas a señales
 * internas (energía, hidratación, fatiga, madurez, capacidades) y externas.
 */
bool NeuralGenome_InitSeededBaseline(NeuralGenome *genome, uint64_t lineageId);

/** @brief Valida arquitectura, esquemas, dimensiones, finitud y cotas de parámetros. */
bool NeuralGenome_Validate(const NeuralGenome *genome);

/** @brief Copia profunda independiente; no modifica el origen ni comparte memoria mutable. */
bool NeuralGenome_Clone(NeuralGenome *dst, const NeuralGenome *src);

/** @brief Calcula la huella canónica FNV-1a de 64 bits de topología y parámetros. */
uint64_t NeuralGenome_Fingerprint(const NeuralGenome *genome);

/** @brief Actualiza genome->metadata.fingerprint si el genoma es válido. */
uint64_t NeuralGenome_UpdateFingerprint(NeuralGenome *genome);

/** @brief Distancia RMS L2 entre los 670 parámetros de dos genomas compatibles. */
float NeuralGenome_Distance(const NeuralGenome *a, const NeuralGenome *b);

/** @brief Aplica mutación acotada in-place sobre un genoma válido. */
bool NeuralGenome_Mutate(NeuralGenome *genome, NeuralMutationConfig config, uint32_t *randomState);

/**
 * @brief Genera un genoma hijo independiente por recombinación y mutación.
 *
 * Nunca modifica parentA ni parentB. Con tasa de mutación cero y reemplazo cero
 * produce exactamente la recombinación determinista esperada.
 */
bool NeuralGenome_Inherit(NeuralGenome *child, const NeuralGenome *parentA,
                          const NeuralGenome *parentB, NeuralMutationConfig config,
                          uint32_t *randomState);

#ifdef __cplusplus
}
#endif

#endif /* NEURAL_GENOME_H */
