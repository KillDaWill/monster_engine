/**
 * @file NeuralBrain.c
 * @brief Motor de inferencia RNN síncrono (v1 y v2), acotado y libre de asignaciones dinámicas.
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
    brain->runtime.architecture = copy.architecture;
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
    if (!genome || !genome->valid || !runtime || !inputs) return false;
    if (genome->formatVersion != NEURAL_BRAIN_FORMAT_VERSION ||
        genome->architecture != NEURAL_ARCH_FIXED_RNN ||
        inputCount != NEURAL_RNN_INPUTS || outputCount != NEURAL_RNN_OUTPUTS ||
        genome->inputCount != NEURAL_RNN_INPUTS ||
        genome->hiddenCount != NEURAL_RNN_HIDDEN ||
        genome->outputCount != NEURAL_RNN_OUTPUTS)
        return false;

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
    runtime->architecture = NEURAL_ARCH_FIXED_RNN;
    ++runtime->stepCount;
    runtime->valid = true;

    if (outputsOut) {
        memcpy(outputsOut, nextOut, sizeof(nextOut));
    }
    return true;
}

bool NeuralBrain_EvaluateV2(const NeuralGenome *genome, NeuralBrainRuntime *runtime,
                            const float contextInputs[NEURAL_V2_CONTEXT_INPUTS],
                            const float resourceFeatures[][NEURAL_V2_RES_FEAT],
                            unsigned resourceCount,
                            const float creatureFeatures[][NEURAL_V2_CRE_FEAT],
                            unsigned creatureCount,
                            float outGlobalScores[NEURAL_V2_GLOBAL_ACTIONS],
                            float outResourceScores[NEURAL_V2_MAX_RESOURCE_CANDIDATES],
                            float outCreatureScores[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS]) {
    if (!genome || !genome->valid || !runtime || !contextInputs) return false;
    if (genome->formatVersion != NEURAL_BRAIN_FORMAT_VERSION_V2 ||
        genome->architecture != NEURAL_ARCH_CANDIDATE_POLICY)
        return false;
    if (resourceCount > NEURAL_V2_MAX_RESOURCE_CANDIDATES ||
        creatureCount > NEURAL_V2_MAX_CREATURE_CANDIDATES)
        return false;
    if (resourceCount > 0u && !resourceFeatures) return false;
    if (creatureCount > 0u && !creatureFeatures) return false;

    for (unsigned i = 0; i < NEURAL_V2_CONTEXT_INPUTS; ++i) {
        if (!isfinite(contextInputs[i])) return false;
    }

    const NeuralCandidatePolicyParams *p = &genome->v2;

    /* 1) Actualización simultánea de la red recurrente de contexto (24 -> 16). */
    float prevCtx[NEURAL_V2_CONTEXT_HIDDEN];
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        float v = runtime->v2.context[h];
        prevCtx[h] = isfinite(v) ? v : 0.0f;
    }

    float nextCtx[NEURAL_V2_CONTEXT_HIDDEN];
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        float acc = p->ctx_b_h[h];
        const float *rowX = &p->ctx_w_xh[h * NEURAL_V2_CONTEXT_INPUTS];
        const float *rowH = &p->ctx_w_hh[h * NEURAL_V2_CONTEXT_HIDDEN];
        for (unsigned i = 0; i < NEURAL_V2_CONTEXT_INPUTS; ++i) {
            acc += rowX[i] * contextInputs[i];
        }
        for (unsigned k = 0; k < NEURAL_V2_CONTEXT_HIDDEN; ++k) {
            acc += rowH[k] * prevCtx[k];
        }
        float act = tanhf(acc);
        if (!isfinite(act)) return false;
        nextCtx[h] = act;
    }

    /* 2) Cabeza global de acciones (IDLE, EXPLORE, REST). */
    float glb[NEURAL_V2_GLOBAL_ACTIONS];
    for (unsigned g = 0; g < NEURAL_V2_GLOBAL_ACTIONS; ++g) {
        float acc = p->global_b[g];
        const float *rowG = &p->global_w[g * NEURAL_V2_CONTEXT_HIDDEN];
        for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
            acc += rowG[h] * nextCtx[h];
        }
        if (!isfinite(acc)) return false;
        glb[g] = acc;
    }

    /* 3) Evaluador compartido de recursos (solo candidatos presentes; equivariante a permutaciones). */
    /* Precomputar la contribución constante del contexto h_t para cada neurona oculta del evaluador! */
    float resCtxBias[NEURAL_V2_RES_HIDDEN];
    if (resourceCount > 0u) {
        for (unsigned h = 0; h < NEURAL_V2_RES_HIDDEN; ++h) {
            float acc = p->res_b1[h];
            const float *rowC = &p->res_w1[h * NEURAL_V2_RES_IN + NEURAL_V2_RES_FEAT];
            for (unsigned k = 0; k < NEURAL_V2_CONTEXT_HIDDEN; ++k) {
                acc += rowC[k] * nextCtx[k];
            }
            resCtxBias[h] = acc;
        }
    }

    float resScores[NEURAL_V2_MAX_RESOURCE_CANDIDATES];
    memset(resScores, 0, sizeof(resScores));
    for (unsigned r = 0; r < resourceCount; ++r) {
        const float *feat = resourceFeatures[r];
        for (unsigned f = 0; f < NEURAL_V2_RES_FEAT; ++f) {
            if (!isfinite(feat[f])) return false;
        }
        float out = p->res_b2[0];
        for (unsigned h = 0; h < NEURAL_V2_RES_HIDDEN; ++h) {
            float acc = resCtxBias[h];
            const float *rowF = &p->res_w1[h * NEURAL_V2_RES_IN];
            for (unsigned f = 0; f < NEURAL_V2_RES_FEAT; ++f) {
                acc += rowF[f] * feat[f];
            }
            out += p->res_w2[h] * tanhf(acc);
        }
        if (!isfinite(out)) return false;
        resScores[r] = out;
    }

    /* 4) Evaluador compartido de criaturas (solo candidatos presentes; equivariante a permutaciones). */
    float creCtxBias[NEURAL_V2_CRE_HIDDEN];
    if (creatureCount > 0u) {
        for (unsigned h = 0; h < NEURAL_V2_CRE_HIDDEN; ++h) {
            float acc = p->cre_b1[h];
            const float *rowC = &p->cre_w1[h * NEURAL_V2_CRE_IN + NEURAL_V2_CRE_FEAT];
            for (unsigned k = 0; k < NEURAL_V2_CONTEXT_HIDDEN; ++k) {
                acc += rowC[k] * nextCtx[k];
            }
            creCtxBias[h] = acc;
        }
    }

    float creScores[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS];
    memset(creScores, 0, sizeof(creScores));
    for (unsigned c = 0; c < creatureCount; ++c) {
        const float *feat = creatureFeatures[c];
        for (unsigned f = 0; f < NEURAL_V2_CRE_FEAT; ++f) {
            if (!isfinite(feat[f])) return false;
        }
        float hid[NEURAL_V2_CRE_HIDDEN];
        for (unsigned h = 0; h < NEURAL_V2_CRE_HIDDEN; ++h) {
            float acc = creCtxBias[h];
            const float *rowF = &p->cre_w1[h * NEURAL_V2_CRE_IN];
            for (unsigned f = 0; f < NEURAL_V2_CRE_FEAT; ++f) {
                acc += rowF[f] * feat[f];
            }
            hid[h] = tanhf(acc);
        }
        for (unsigned a = 0; a < NEURAL_V2_CRE_ACTIONS; ++a) {
            float out = p->cre_b2[a];
            const float *rowA = &p->cre_w2[a * NEURAL_V2_CRE_HIDDEN];
            for (unsigned h = 0; h < NEURAL_V2_CRE_HIDDEN; ++h) {
                out += rowA[h] * hid[h];
            }
            if (!isfinite(out)) return false;
            creScores[c][a] = out;
        }
    }

    /* Publicar instantánea temporal para inspección y siguiente paso recurrente. */
    memcpy(runtime->v2.contextInputs, contextInputs, sizeof(runtime->v2.contextInputs));
    memcpy(runtime->v2.prevContext, prevCtx, sizeof(prevCtx));
    memcpy(runtime->v2.context, nextCtx, sizeof(nextCtx));
    memcpy(runtime->v2.globalScores, glb, sizeof(glb));
    runtime->v2.resourceCount = resourceCount;
    memcpy(runtime->v2.resourceScores, resScores, sizeof(resScores));
    runtime->v2.creatureCount = creatureCount;
    memcpy(runtime->v2.creatureScores, creScores, sizeof(creScores));
    runtime->architecture = NEURAL_ARCH_CANDIDATE_POLICY;
    ++runtime->stepCount;
    runtime->valid = true;

    if (outGlobalScores) memcpy(outGlobalScores, glb, sizeof(glb));
    if (outResourceScores) memcpy(outResourceScores, resScores, sizeof(resScores));
    if (outCreatureScores) memcpy(outCreatureScores, creScores, sizeof(creScores));
    return true;
}

bool NeuralBrain_Step(NeuralBrain *brain, const float *inputs, float *outputsOut) {
    if (!brain) return false;
    return NeuralBrain_Evaluate(&brain->genome, &brain->runtime, inputs,
                                brain->genome.inputCount, outputsOut, brain->genome.outputCount);
}
