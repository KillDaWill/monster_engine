/**
 * @file NeuralBrain.c
 * @brief Motor de inferencia RNN síncrono, acotado y libre de asignaciones dinámicas.
 */
#include "NeuralBrain.h"
#include <math.h>
#include <string.h>

bool NeuralBrain_Init(NeuralBrain *brain, const NeuralGenome *genome) {
    if (!brain || !NeuralGenome_Validate(genome)) return false;
    NeuralGenome copy;
    if (!NeuralGenome_Clone(&copy, genome)) return false;
    brain->genome = copy;
    NeuralBrain_ResetRuntime(&brain->runtime);
    return true;
}

void NeuralBrain_ResetRuntime(NeuralBrainRuntime *runtime) {
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
}

void NeuralBrain_Destroy(NeuralBrain *brain) {
    if (!brain) return;
    NeuralGenome_Destroy(&brain->genome);
    NeuralBrain_ResetRuntime(&brain->runtime);
}

bool NeuralBrain_Evaluate(const NeuralGenome *genome, NeuralBrainRuntime *runtime,
                          const float *inputs, unsigned inputCount,
                          float *outputsOut, unsigned outputCount) {
    if (!NeuralGenome_Validate(genome) || !runtime || !inputs) return false;
    if (inputCount != genome->inputCount || outputCount != genome->outputCount) return false;

    for (unsigned i = 0; i < inputCount; ++i) {
        if (!isfinite(inputs[i])) return false;
    }

    float prev[NEURAL_RNN_HIDDEN];
    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        float v = runtime->hidden[h];
        prev[h] = isfinite(v) ? v : 0.0f;
    }

    float nextHidden[NEURAL_RNN_HIDDEN];
    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        float acc = genome->b_h[h];
        const float *rowX = &genome->w_xh[h * NEURAL_RNN_INPUTS];
        const float *rowH = &genome->w_hh[h * NEURAL_RNN_HIDDEN];
        for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) {
            acc += rowX[i] * inputs[i];
        }
        for (unsigned k = 0; k < NEURAL_RNN_HIDDEN; ++k) {
            acc += rowH[k] * prev[k];
        }
        float activated = tanhf(acc);
        if (!isfinite(activated)) return false;
        nextHidden[h] = activated;
    }

    float nextOut[NEURAL_RNN_OUTPUTS];
    for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o) {
        float acc = genome->b_y[o];
        const float *rowY = &genome->w_hy[o * NEURAL_RNN_HIDDEN];
        for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
            acc += rowY[h] * nextHidden[h];
        }
        if (!isfinite(acc)) return false;
        nextOut[o] = acc;
    }

    memcpy(runtime->inputs, inputs, sizeof(runtime->inputs));
    memcpy(runtime->prevHidden, prev, sizeof(runtime->prevHidden));
    memcpy(runtime->hidden, nextHidden, sizeof(runtime->hidden));
    memcpy(runtime->outputs, nextOut, sizeof(runtime->outputs));
    ++runtime->stepCount;
    runtime->valid = true;

    if (outputsOut) {
        memcpy(outputsOut, nextOut, sizeof(nextOut));
    }
    return true;
}

bool NeuralBrain_Step(NeuralBrain *brain, const float *inputs, float *outputsOut) {
    if (!brain) return false;
    return NeuralBrain_Evaluate(&brain->genome, &brain->runtime, inputs,
                                brain->genome.inputCount, outputsOut, brain->genome.outputCount);
}
