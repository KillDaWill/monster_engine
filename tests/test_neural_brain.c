#include "NeuralBrain.h"
#include "test_utils.h"
#define NEAR_EPS(a, b, eps) (fabsf((a) - (b)) < (eps))
#include <math.h>
#include <string.h>

static void test_known_network_inference_and_simultaneous_recurrence(void) {
    NeuralGenome g;
    NeuralGenome_Init(&g);

    /* Red controlada manualmente:
     * h0 = tanh(2.0 * x0 + 0.5 * h0_prev - 1.0 * h1_prev + 0.1)
     * h1 = tanh(-1.5 * x1 + 0.8 * h0_prev + 0.25 * h1_prev - 0.2)
     * y0 = 1.2 * h0 - 0.7 * h1 + 0.3
     * y1 = -0.5 * h0 + 2.0 * h1 - 0.1
     */
    g.w_xh[0 * NEURAL_RNN_INPUTS + 0] = 2.0f;
    g.w_hh[0 * NEURAL_RNN_HIDDEN + 0] = 0.5f;
    g.w_hh[0 * NEURAL_RNN_HIDDEN + 1] = -1.0f;
    g.b_h[0] = 0.1f;

    g.w_xh[1 * NEURAL_RNN_INPUTS + 1] = -1.5f;
    g.w_hh[1 * NEURAL_RNN_HIDDEN + 0] = 0.8f;
    g.w_hh[1 * NEURAL_RNN_HIDDEN + 1] = 0.25f;
    g.b_h[1] = -0.2f;

    g.w_hy[0 * NEURAL_RNN_HIDDEN + 0] = 1.2f;
    g.w_hy[0 * NEURAL_RNN_HIDDEN + 1] = -0.7f;
    g.b_y[0] = 0.3f;

    g.w_hy[1 * NEURAL_RNN_HIDDEN + 0] = -0.5f;
    g.w_hy[1 * NEURAL_RNN_HIDDEN + 1] = 2.0f;
    g.b_y[1] = -0.1f;
    NeuralGenome_UpdateFingerprint(&g);

    NeuralBrain brain;
    TEST_ASSERT(NeuralBrain_Init(&brain, &g), "inicialización de cerebro conocido");

    float x1[NEURAL_RNN_INPUTS] = {0};
    x1[0] = 0.4f;
    x1[1] = 0.6f;
    float y1[NEURAL_RNN_OUTPUTS] = {0};
    TEST_ASSERT(NeuralBrain_Step(&brain, x1, y1), "paso 1 de inferencia");

    float expH0_t1 = tanhf(2.0f * 0.4f + 0.1f);
    float expH1_t1 = tanhf(-1.5f * 0.6f - 0.2f);
    float expY0_t1 = 1.2f * expH0_t1 - 0.7f * expH1_t1 + 0.3f;
    float expY1_t1 = -0.5f * expH0_t1 + 2.0f * expH1_t1 - 0.1f;
    TEST_ASSERT(NEAR_EPS(brain.runtime.hidden[0], expH0_t1, 1e-5f), "h0 paso 1 exacto");
    TEST_ASSERT(NEAR_EPS(brain.runtime.hidden[1], expH1_t1, 1e-5f), "h1 paso 1 exacto");
    TEST_ASSERT(NEAR_EPS(y1[0], expY0_t1, 1e-5f), "y0 paso 1 exacto");
    TEST_ASSERT(NEAR_EPS(y1[1], expY1_t1, 1e-5f), "y1 paso 1 exacto");

    /* Paso 2: verifica actualización simultánea (h1 lee expH0_t1, NO expH0_t2). */
    float x2[NEURAL_RNN_INPUTS] = {0};
    x2[0] = -0.2f;
    x2[1] = 0.1f;
    float y2[NEURAL_RNN_OUTPUTS] = {0};
    TEST_ASSERT(NeuralBrain_Step(&brain, x2, y2), "paso 2 de inferencia");

    float expH0_t2 = tanhf(2.0f * (-0.2f) + 0.5f * expH0_t1 - 1.0f * expH1_t1 + 0.1f);
    float expH1_t2 = tanhf(-1.5f * 0.1f + 0.8f * expH0_t1 + 0.25f * expH1_t1 - 0.2f);
    float expY0_t2 = 1.2f * expH0_t2 - 0.7f * expH1_t2 + 0.3f;
    float expY1_t2 = -0.5f * expH0_t2 + 2.0f * expH1_t2 - 0.1f;
    TEST_ASSERT(NEAR_EPS(brain.runtime.hidden[0], expH0_t2, 1e-5f), "h0 paso 2 simultáneo");
    TEST_ASSERT(NEAR_EPS(brain.runtime.hidden[1], expH1_t2, 1e-5f), "h1 paso 2 simultáneo");
    TEST_ASSERT(NEAR_EPS(y2[0], expY0_t2, 1e-5f), "y0 paso 2 simultáneo");
    TEST_ASSERT(NEAR_EPS(y2[1], expY1_t2, 1e-5f), "y1 paso 2 simultáneo");
    TEST_ASSERT(brain.runtime.stepCount == 2u, "contador de pasos recurrentes");

    NeuralBrain_Destroy(&brain);
    NeuralGenome_Destroy(&g);
}

static void test_recurrent_memory_and_runtime_reset(void) {
    NeuralGenome g;
    NeuralGenome_Init(&g);
    g.w_xh[0 * NEURAL_RNN_INPUTS + 0] = 1.5f;
    g.w_hh[0 * NEURAL_RNN_HIDDEN + 0] = 0.9f;
    g.w_hy[3 * NEURAL_RNN_HIDDEN + 0] = 2.0f;
    NeuralGenome_UpdateFingerprint(&g);

    NeuralBrain brain;
    TEST_ASSERT(NeuralBrain_Init(&brain, &g), "init cerebro memoria recurrente");

    float pulse[NEURAL_RNN_INPUTS] = {0};
    pulse[0] = 1.0f;
    float zero[NEURAL_RNN_INPUTS] = {0};
    float outAfterPulse[NEURAL_RNN_OUTPUTS] = {0};
    float outAfterReset[NEURAL_RNN_OUTPUTS] = {0};

    TEST_ASSERT(NeuralBrain_Step(&brain, pulse, outAfterPulse), "pulso inicial");
    TEST_ASSERT(NeuralBrain_Step(&brain, zero, outAfterPulse), "entrada cero con memoria previa");
    TEST_ASSERT(outAfterPulse[3] > 0.5f, "entradas previas influyen en salidas posteriores");

    uint64_t fpBefore = brain.genome.metadata.fingerprint;
    NeuralBrain_ResetRuntime(&brain.runtime);
    TEST_ASSERT(brain.runtime.stepCount == 0u, "reset pone stepCount a cero");
    TEST_ASSERT(brain.genome.metadata.fingerprint == fpBefore, "reset no altera el genoma");

    TEST_ASSERT(NeuralBrain_Step(&brain, zero, outAfterReset), "entrada cero tras reset");
    TEST_ASSERT(NEAR_EPS(outAfterReset[3], 0.0f, 1e-6f), "reset elimina historial recurrente");

    /* Entradas NaN o Inf deben ser rechazadas sin corromper el estado previo. */
    float bad[NEURAL_RNN_INPUTS] = {0};
    bad[4] = NAN;
    TEST_ASSERT(!NeuralBrain_Step(&brain, bad, outAfterReset), "rechaza NaN");
    bad[4] = INFINITY;
    TEST_ASSERT(!NeuralBrain_Step(&brain, bad, outAfterReset), "rechaza Inf");
    TEST_ASSERT(brain.runtime.stepCount == 1u, "rechazo preserva stepCount");

    NeuralBrain_Destroy(&brain);
    NeuralGenome_Destroy(&g);
}

void run_neural_brain_tests(void) {
    test_known_network_inference_and_simultaneous_recurrence();
    test_recurrent_memory_and_runtime_reset();
    printf("[PASS] neural_brain: inferencia exacta, actualización simultánea, memoria recurrente y reset\n");
}
