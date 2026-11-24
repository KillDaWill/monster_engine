#include "NeuralGenome.h"
#include "NeuralBrain.h"
#include "test_utils.h"
#define NEAR_EPS(a, b, eps) (fabsf((a) - (b)) < (eps))
#include <math.h>
#include <string.h>

static void test_deterministic_seeds_and_bounds(void) {
    uint32_t s1 = 12345u, s2 = 12345u;
    NeuralGenome a, b;
    TEST_ASSERT(NeuralGenome_InitRandom(&a, &s1, 0.9f, 5.0f, 10u), "init random A");
    TEST_ASSERT(NeuralGenome_InitRandom(&b, &s2, 0.9f, 5.0f, 10u), "init random B");
    TEST_ASSERT(a.metadata.fingerprint == b.metadata.fingerprint, "semillas idénticas producen misma huella");
    TEST_ASSERT(NEAR_EPS(NeuralGenome_Distance(&a, &b), 0.0f, 1e-7f), "distancia cero con misma semilla");

    NeuralMutationConfig cfg = NeuralMutationConfig_Default();
    uint32_t m1 = 777u, m2 = 777u;
    TEST_ASSERT(NeuralGenome_Mutate(&a, cfg, &m1), "mutación A");
    TEST_ASSERT(NeuralGenome_Mutate(&b, cfg, &m2), "mutación B");
    TEST_ASSERT(a.metadata.fingerprint == b.metadata.fingerprint, "mutaciones con misma semilla coinciden");
    TEST_ASSERT(NeuralGenome_Validate(&a), "genoma mutado válido");

    /* Mutación fuerte sigue respetando cota absoluta. */
    cfg.mutationRate = 1.0f;
    cfg.mutationSigma = 8.0f;
    cfg.weightLimit = 3.5f;
    TEST_ASSERT(NeuralGenome_Mutate(&a, cfg, &m1), "mutación extrema acotada");
    for (size_t i = 0; i < NEURAL_RNN_W_XH_COUNT; ++i)
        TEST_ASSERT(isfinite(a.w_xh[i]) && fabsf(a.w_xh[i]) <= 3.5f + 1e-6f, "w_xh en límites");
    for (size_t i = 0; i < NEURAL_RNN_W_HH_COUNT; ++i)
        TEST_ASSERT(isfinite(a.w_hh[i]) && fabsf(a.w_hh[i]) <= 3.5f + 1e-6f, "w_hh en límites");
    for (size_t i = 0; i < NEURAL_RNN_W_HY_COUNT; ++i)
        TEST_ASSERT(isfinite(a.w_hy[i]) && fabsf(a.w_hy[i]) <= 3.5f + 1e-6f, "w_hy en límites");

    NeuralGenome_Destroy(&a);
    NeuralGenome_Destroy(&b);
}

static void test_zero_mutation_recombination_and_parent_immutability(void) {
    uint32_t rngA = 11u, rngB = 22u;
    NeuralGenome parentA, parentB;
    TEST_ASSERT(NeuralGenome_InitRandom(&parentA, &rngA, 0.8f, 5.0f, 1u), "parentA");
    TEST_ASSERT(NeuralGenome_InitRandom(&parentB, &rngB, 0.8f, 5.0f, 2u), "parentB");
    uint64_t fpA = parentA.metadata.fingerprint;
    uint64_t fpB = parentB.metadata.fingerprint;
    TEST_ASSERT(fpA != fpB, "padres distintos");

    /* Modo mutation-only con tasa 0 produce exactamente parentA. */
    NeuralMutationConfig noMut = NeuralMutationConfig_Default();
    noMut.mutationRate = 0.0f;
    noMut.replaceRate = 0.0f;
    noMut.crossoverMode = NEURAL_CROSSOVER_MUTATION_ONLY;
    uint32_t rngC = 99u;
    NeuralGenome childOnly;
    TEST_ASSERT(NeuralGenome_Inherit(&childOnly, &parentA, &parentB, noMut, &rngC), "herencia mutation-only cero");
    TEST_ASSERT(childOnly.metadata.fingerprint == fpA, "cero mutación sin cruce preserva exactamente parentA");

    /* Modo neuron-wise con tasa 0 toma filas completas de parentA o parentB. */
    noMut.crossoverMode = NEURAL_CROSSOVER_NEURON_WISE;
    NeuralGenome childNeuron;
    TEST_ASSERT(NeuralGenome_Inherit(&childNeuron, &parentA, &parentB, noMut, &rngC), "herencia neuron-wise cero");
    for (unsigned h = 0; h < NEURAL_RNN_HIDDEN; ++h) {
        bool matchesA = memcmp(&childNeuron.w_xh[h * NEURAL_RNN_INPUTS],
                               &parentA.w_xh[h * NEURAL_RNN_INPUTS],
                               NEURAL_RNN_INPUTS * sizeof(float)) == 0 &&
                        memcmp(&childNeuron.w_hh[h * NEURAL_RNN_HIDDEN],
                               &parentA.w_hh[h * NEURAL_RNN_HIDDEN],
                               NEURAL_RNN_HIDDEN * sizeof(float)) == 0 &&
                        childNeuron.b_h[h] == parentA.b_h[h];
        bool matchesB = memcmp(&childNeuron.w_xh[h * NEURAL_RNN_INPUTS],
                               &parentB.w_xh[h * NEURAL_RNN_INPUTS],
                               NEURAL_RNN_INPUTS * sizeof(float)) == 0 &&
                        memcmp(&childNeuron.w_hh[h * NEURAL_RNN_HIDDEN],
                               &parentB.w_hh[h * NEURAL_RNN_HIDDEN],
                               NEURAL_RNN_HIDDEN * sizeof(float)) == 0 &&
                        childNeuron.b_h[h] == parentB.b_h[h];
        TEST_ASSERT(matchesA || matchesB, "cruce por neurona conserva fila íntegra de un progenitor");
    }

    /* Con mutación habilitada cambia parámetros sin mutar nunca a los progenitores. */
    NeuralMutationConfig withMut = NeuralMutationConfig_Default();
    withMut.mutationRate = 0.25f;
    withMut.mutationSigma = 0.2f;
    NeuralGenome childMut;
    TEST_ASSERT(NeuralGenome_Inherit(&childMut, &parentA, &parentB, withMut, &rngC), "herencia con mutación");
    TEST_ASSERT(NeuralGenome_Fingerprint(&parentA) == fpA, "parentA inmutable");
    TEST_ASSERT(NeuralGenome_Fingerprint(&parentB) == fpB, "parentB inmutable");
    TEST_ASSERT(childMut.metadata.fingerprint != fpA && childMut.metadata.fingerprint != fpB, "hijo mutado distinto");
    TEST_ASSERT(childMut.metadata.generation == 1u, "generación incrementada");

    NeuralGenome_Destroy(&parentA);
    NeuralGenome_Destroy(&parentB);
    NeuralGenome_Destroy(&childOnly);
    NeuralGenome_Destroy(&childNeuron);
    NeuralGenome_Destroy(&childMut);
}

static void test_seeded_baseline_pure_rnn_behavior(void) {
    NeuralGenome base;
    TEST_ASSERT(NeuralGenome_InitSeededBaseline(&base, 5u), "init seeded baseline");
    TEST_ASSERT(NeuralGenome_Validate(&base), "baseline válido");

    NeuralBrain brain;
    TEST_ASSERT(NeuralBrain_Init(&brain, &base), "cerebro desde baseline");

    /* Caso 1: herbívoro hambriento viendo planta -> prefiere FEED_PLANT (índice 4). */
    float xPlant[NEURAL_RNN_INPUTS] = {0};
    xPlant[0] = 0.15f; /* baja energía */
    xPlant[1] = 0.85f; /* bien hidratado */
    xPlant[3] = 1.0f;  /* sano */
    xPlant[7] = 0.0f;  /* sin ataque físico */
    xPlant[8] = 1.0f;  /* puede beber */
    xPlant[12] = 1.0f; /* planta visible */
    xPlant[13] = 0.2f; /* cercana */
    float y[NEURAL_RNN_OUTPUTS] = {0};
    TEST_ASSERT(NeuralBrain_Step(&brain, xPlant, y), "inferencia baseline planta");
    unsigned best = 0;
    for (unsigned o = 1; o < NEURAL_RNN_OUTPUTS; ++o) if (y[o] > y[best]) best = o;
    TEST_ASSERT(best == 4u, "baseline hambriento no-atacante con planta prefiere FEED_PLANT");

    /* Caso 2: criatura sedienta viendo agua -> prefiere HYDRATE (índice 3). */
    NeuralBrain_ResetRuntime(&brain.runtime);
    float xWater[NEURAL_RNN_INPUTS] = {0};
    xWater[0] = 0.85f;
    xWater[1] = 0.12f; /* muy sediento */
    xWater[3] = 1.0f;
    xWater[8] = 1.0f;
    xWater[20] = 1.0f; /* agua visible */
    xWater[21] = 0.2f;
    TEST_ASSERT(NeuralBrain_Step(&brain, xWater, y), "inferencia baseline agua");
    best = 0;
    for (unsigned o = 1; o < NEURAL_RNN_OUTPUTS; ++o) if (y[o] > y[best]) best = o;
    TEST_ASSERT(best == 3u, "baseline sediento prefiere HYDRATE");

    NeuralBrain_Destroy(&brain);
    NeuralGenome_Destroy(&base);
}

void run_neural_genome_tests(void) {
    test_deterministic_seeds_and_bounds();
    test_zero_mutation_recombination_and_parent_immutability();
    test_seeded_baseline_pure_rnn_behavior();
    printf("[PASS] neural_genome: determinismo, recombinación, mutación acotada y baseline RNN\n");
}
