/**
 * @file NeuralBrain.h
 * @brief Inferencia recurrente (v1 fixed_rnn y v2 recurrent_candidate_policy) con estado temporal separado.
 *
 * No posee anatomía, memoria perceptiva, estado digestivo ni recursos gráficos.
 * No realiza asignaciones dinámicas durante la inferencia normal.
 */
#ifndef NEURAL_BRAIN_H
#define NEURAL_BRAIN_H

#include "NeuralGenome.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Estado temporal en ejecución para la arquitectura v2 (`recurrent_candidate_policy`). */
typedef struct NeuralBrainV2Runtime {
    float contextInputs[NEURAL_V2_CONTEXT_INPUTS];
    float prevContext[NEURAL_V2_CONTEXT_HIDDEN];
    float context[NEURAL_V2_CONTEXT_HIDDEN];
    float globalScores[NEURAL_V2_GLOBAL_ACTIONS];
    uint32_t resourceCount;
    float resourceScores[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    uint32_t creatureCount;
    float creatureScores[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS];
} NeuralBrainV2Runtime;

/** @brief Estado temporal no heredable de una instancia neuronal en ejecución. */
typedef struct NeuralBrainRuntime {
    union {
        struct {
            float inputs[NEURAL_RNN_INPUTS];
            float prevHidden[NEURAL_RNN_HIDDEN];
            float hidden[NEURAL_RNN_HIDDEN];
            float outputs[NEURAL_RNN_OUTPUTS];
        };
        NeuralBrainV2Runtime v2;
    };
    NeuralArchitectureType architecture;
    uint64_t stepCount;
    bool valid;
} NeuralBrainRuntime;

/** @brief Contenedor propietario de un genoma neuronal y su estado temporal separado. */
typedef struct NeuralBrain {
    NeuralGenome genome;
    NeuralBrainRuntime runtime;
} NeuralBrain;

/** @brief Inicializa un cerebro clonando el genoma validado y poniendo a cero su estado temporal. */
bool NeuralBrain_Init(NeuralBrain *brain, const NeuralGenome *genome);

/** @brief Reinicia activaciones recurrentes y lecturas sin modificar el genoma heredable. */
void NeuralBrain_ResetRuntime(NeuralBrainRuntime *runtime);

/** @brief Libera de forma explícita los recursos del cerebro y limpia su memoria. */
void NeuralBrain_Destroy(NeuralBrain *brain);

/**
 * @brief Evalúa un paso recurrente simultáneo v1 (`fixed_rnn`) sin asignaciones dinámicas.
 */
bool NeuralBrain_Evaluate(const NeuralGenome *genome, NeuralBrainRuntime *runtime,
                          const float *inputs, unsigned inputCount,
                          float *outputsOut, unsigned outputCount);

/**
 * @brief Evalúa un paso recurrente simultáneo v2 (`recurrent_candidate_policy`) con
 *        evaluadores compartidos equivariantes a permutaciones.
 *
 * No evalúa ranuras de candidatos ausentes (`resourceCount`..MAX-1 ni `creatureCount`..MAX-1).
 */
bool NeuralBrain_EvaluateV2(const NeuralGenome *genome, NeuralBrainRuntime *runtime,
                            const float contextInputs[NEURAL_V2_CONTEXT_INPUTS],
                            const float resourceFeatures[][NEURAL_V2_RES_FEAT],
                            unsigned resourceCount,
                            const float creatureFeatures[][NEURAL_V2_CRE_FEAT],
                            unsigned creatureCount,
                            float outGlobalScores[NEURAL_V2_GLOBAL_ACTIONS],
                            float outResourceScores[NEURAL_V2_MAX_RESOURCE_CANDIDATES],
                            float outCreatureScores[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS]);

/** @brief Atajo sobre una instancia @ref NeuralBrain v1 completa. */
bool NeuralBrain_Step(NeuralBrain *brain, const float *inputs, float *outputsOut);

#ifdef __cplusplus
}
#endif

#endif /* NEURAL_BRAIN_H */
