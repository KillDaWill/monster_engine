/**
 * @file NeuralBrain.h
 * @brief Inferencia recurrente y estado temporal separado del genoma heredable.
 *
 * No posee anatomía, memoria perceptiva ni recursos gráficos. No realiza
 * asignaciones dinámicas durante la inferencia normal.
 */
#ifndef NEURAL_BRAIN_H
#define NEURAL_BRAIN_H

#include "NeuralGenome.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Estado temporal no heredable de una instancia neuronal en ejecución. */
typedef struct NeuralBrainRuntime {
    float inputs[NEURAL_RNN_INPUTS];
    float prevHidden[NEURAL_RNN_HIDDEN];
    float hidden[NEURAL_RNN_HIDDEN];
    float outputs[NEURAL_RNN_OUTPUTS];
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
 * @brief Evalúa un paso recurrente simultáneo sin asignaciones dinámicas.
 *
 * Ecuaciones:
 *   h_t = tanh(W_xh * x_t + W_hh * h_{t-1} + b_h)
 *   y_t = W_hy * h_t + b_y
 *
 * Todas las neuronas ocultas leen la instantánea previa h_{t-1} antes de publicar h_t.
 */
bool NeuralBrain_Evaluate(const NeuralGenome *genome, NeuralBrainRuntime *runtime,
                          const float *inputs, unsigned inputCount,
                          float *outputsOut, unsigned outputCount);

/** @brief Atajo sobre una instancia @ref NeuralBrain completa. */
bool NeuralBrain_Step(NeuralBrain *brain, const float *inputs, float *outputsOut);

#ifdef __cplusplus
}
#endif

#endif /* NEURAL_BRAIN_H */
