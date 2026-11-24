#include "NeuralBrain.h"
#include "NeuralBrainIO.h"
#include "test_utils.h"
#define NEAR_EPS(a, b, eps) (fabsf((a) - (b)) < (eps))
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_roundtrip_memory_and_file(void) {
    uint32_t seed = 9001u;
    NeuralGenome original;
    TEST_ASSERT(NeuralGenome_InitRandom(&original, &seed, 0.9f, 5.0f, 42u), "genoma original");
    original.metadata.generation = 7u;
    original.metadata.parentIds[0] = 15u;
    original.metadata.parentIds[1] = 28u;
    snprintf(original.metadata.label, sizeof(original.metadata.label), "test_roundtrip");

    const char *path = "/tmp/monster_test_brain_roundtrip.brain.json";
    remove(path);
    char err[256] = {0};
    TEST_ASSERT(NeuralBrain_SaveFile(&original, path, false, err, sizeof(err)), "guardar archivo nuevo");
    TEST_ASSERT(!NeuralBrain_SaveFile(&original, path, false, err, sizeof(err)), "bloquea sobrescritura sin permiso");
    TEST_ASSERT(NeuralBrain_SaveFile(&original, path, true, err, sizeof(err)), "sobrescritura explícita permitida");

    NeuralGenome loaded;
    memset(&loaded, 0, sizeof(loaded));
    TEST_ASSERT(NeuralBrain_LoadFile(path, &loaded, err, sizeof(err)), "cargar archivo guardado");
    remove(path);

    TEST_ASSERT(loaded.metadata.fingerprint == original.metadata.fingerprint, "huella idéntica tras round-trip");
    TEST_ASSERT(NEAR_EPS(NeuralGenome_Distance(&original, &loaded), 0.0f, 1e-7f), "pesos sin pérdida float32");
    TEST_ASSERT(loaded.metadata.lineageId == 42u && loaded.metadata.generation == 7u, "metadatos preservados");
    TEST_ASSERT(loaded.metadata.parentIds[0] == 15u && loaded.metadata.parentIds[1] == 28u, "padres preservados");

    /* Verificar inferencia multipaso idéntica tras round-trip. */
    NeuralBrain bA, bB;
    TEST_ASSERT(NeuralBrain_Init(&bA, &original) && NeuralBrain_Init(&bB, &loaded), "init cerebros round-trip");
    float x[NEURAL_RNN_INPUTS];
    for (unsigned step = 0; step < 5; ++step) {
        for (unsigned i = 0; i < NEURAL_RNN_INPUTS; ++i) x[i] = sinf((float)(step * 7 + i) * 0.3f);
        float yA[NEURAL_RNN_OUTPUTS], yB[NEURAL_RNN_OUTPUTS];
        TEST_ASSERT(NeuralBrain_Step(&bA, x, yA) && NeuralBrain_Step(&bB, x, yB), "paso inferencia");
        for (unsigned o = 0; o < NEURAL_RNN_OUTPUTS; ++o)
            TEST_ASSERT(NEAR_EPS(yA[o], yB[o], 1e-6f), "salida recurrente idéntica");
    }

    NeuralBrain_Destroy(&bA);
    NeuralBrain_Destroy(&bB);
    NeuralGenome_Destroy(&original);
    NeuralGenome_Destroy(&loaded);
}

static void test_reject_invalid_json_without_corrupting_destination(void) {
    NeuralGenome sentinel;
    TEST_ASSERT(NeuralGenome_InitSeededBaseline(&sentinel, 77u), "sentinel válido");
    uint64_t sentinelFp = sentinel.metadata.fingerprint;

    char err[256] = {0};
    const char *badCases[] = {
        "",
        "{malformed",
        "{\"version\": 99, \"architecture\": \"fixed_rnn\"}",
        "{\"version\": 1, \"architecture\": \"unknown_arch\", \"sensor_schema\": \"monster_sensor_v1\", \"action_schema\": \"monster_action_v1\", \"topology\": {\"inputs\":32,\"hidden\":12,\"outputs\":10}, \"weights\":{\"w_xh\":[],\"w_hh\":[],\"w_hy\":[]}, \"biases\":{\"b_h\":[],\"b_y\":[]}, \"metadata\":{}}",
        "{\"version\": 1, \"architecture\": \"fixed_rnn\", \"sensor_schema\": \"bad_sensor\", \"action_schema\": \"monster_action_v1\", \"topology\": {\"inputs\":32,\"hidden\":12,\"outputs\":10}, \"weights\":{\"w_xh\":[],\"w_hh\":[],\"w_hy\":[]}, \"biases\":{\"b_h\":[],\"b_y\":[]}, \"metadata\":{}}",
        "{\"version\": 1, \"architecture\": \"fixed_rnn\", \"sensor_schema\": \"monster_sensor_v1\", \"action_schema\": \"monster_action_v1\", \"topology\": {\"inputs\":16,\"hidden\":12,\"outputs\":10}, \"weights\":{\"w_xh\":[],\"w_hh\":[],\"w_hy\":[]}, \"biases\":{\"b_h\":[],\"b_y\":[]}, \"metadata\":{}}"
    };
    for (size_t i = 0; i < sizeof(badCases) / sizeof(badCases[0]); ++i) {
        TEST_ASSERT(!NeuralBrain_DeserializeJSON(badCases[i], strlen(badCases[i]), &sentinel, err, sizeof(err)),
                    "JSON inválido rechazado");
        TEST_ASSERT(sentinel.metadata.fingerprint == sentinelFp, "fallo deja destino intacto");
    }

    /* Serializar válido y corromper un peso fuera de límites (> 10.0) y NaN. */
    size_t needed = 0;
    TEST_ASSERT(NeuralBrain_SerializeJSON(&sentinel, NULL, 0, &needed), "medir JSON válido");
    char *buf = malloc(needed + 64u);
    TEST_ASSERT(buf != NULL, "reserva buf");
    TEST_ASSERT(NeuralBrain_SerializeJSON(&sentinel, buf, needed + 64u, &needed), "serializar válido");

    /* Sustituir primer número en b_y por 999.0 (fuera de cota). */
    char *pos = strstr(buf, "\"b_y\": [");
    TEST_ASSERT(pos != NULL, "localiza b_y");
    char *badBuf = malloc(needed + 128u);
    size_t prefix = (size_t)(pos - buf) + 8u;
    memcpy(badBuf, buf, prefix);
    snprintf(badBuf + prefix, needed + 128u - prefix, "999.0%s", strchr(pos + 8u, ','));
    TEST_ASSERT(!NeuralBrain_DeserializeJSON(badBuf, strlen(badBuf), &sentinel, err, sizeof(err)),
                "rechaza peso fuera de límites");
    TEST_ASSERT(sentinel.metadata.fingerprint == sentinelFp, "sentinel intacto tras rango inválido");

    /* Rechazo de tamaño excesivo. */
    TEST_ASSERT(!NeuralBrain_DeserializeJSON(buf, NEURAL_BRAIN_MAX_FILE_BYTES + 1u, &sentinel, err, sizeof(err)),
                "rechaza archivo excesivamente grande");

    free(buf);
    free(badBuf);
    NeuralGenome_Destroy(&sentinel);
}

void run_neural_brain_io_tests(void) {
    test_roundtrip_memory_and_file();
    test_reject_invalid_json_without_corrupting_destination();
    printf("[PASS] neural_brain_io: round-trip exacto, protección de sobrescritura y rechazo estricto\n");
}
