#include "test_utils.h"
#include "MarchingCubes.h"
#include "MarchingCubesTables.h"

/* ============================================================
 * Validación estructural + geométrica de la tabla de triángulos.
 * Garantía permanente: para cada caso, el conjunto de aristas
 * referenciadas por la fila == conjunto de aristas cortadas
 * (máscara geométrica). Regresión directa de la corrupción
 * detectada: 200/256 filas referenciaban aristas inactivas.
 * ============================================================ */

static void test_all_rows_structure(void) {
    for (int cubeIndex = 0; cubeIndex < 256; ++cubeIndex) {
        const int* row = MarchingCubes_GetTriangleRow(cubeIndex);
        TEST_ASSERT(row != NULL, "GetTriangleRow NULL para caso válido");

        /* Terminador dentro de los límites y sin datos tras él */
        int term = -1;
        for (int i = 0; i < MARCHING_CUBES_MAX_ROW_ENTRIES; ++i) {
            if (row[i] == -1) {
                term = i;
                break;
            }
        }
        TEST_ASSERT(term >= 0, "Fila sin terminador -1");

        /* Entradas previas al terminador: aristas válidas y conteo múltiplo de 3 */
        int nonTerm = 0;
        for (int i = 0; i < term; ++i) {
            TEST_ASSERT(row[i] >= 0 && row[i] < MARCHING_CUBES_EDGE_COUNT,
                        "Entrada de fila fuera de rango [0,12)");
            ++nonTerm;
        }
        TEST_ASSERT(nonTerm % 3 == 0, "Conteo de aristas no múltiplo de 3");

        for (int i = term + 1; i < MARCHING_CUBES_MAX_ROW_ENTRIES; ++i) {
            TEST_ASSERT(row[i] == -1, "Datos residuales tras el terminador");
        }

        /* Coincidencia con GetTriangleCount */
        int expectedTriangles = nonTerm / 3;
        TEST_ASSERT(MarchingCubes_GetTriangleCount(cubeIndex) == expectedTriangles,
                    "GetTriangleCount inconsistente con la fila");
        TEST_ASSERT(expectedTriangles <= MARCHING_CUBES_MAX_TRIANGLES,
                    "Demasiados triángulos para un caso");
    }
    printf("[PASS] test_all_rows_structure\n");
}

static void test_used_edges_equal_active_edges(void) {
    for (int cubeIndex = 0; cubeIndex < 256; ++cubeIndex) {
        uint16_t mask = MarchingCubes_GetEdgeMask(cubeIndex);
        const int* row = MarchingCubes_GetTriangleRow(cubeIndex);

        /* Máscara geométrica independiente (regla de la arista: extremos dentro/fuera) */
        uint16_t geometricMask = 0;
        for (int e = 0; e < MARCHING_CUBES_EDGE_COUNT; ++e) {
            int c1 = MARCHING_CUBES_EDGE_ENDPOINTS[e][0];
            int c2 = MARCHING_CUBES_EDGE_ENDPOINTS[e][1];
            int inside1 = (cubeIndex >> c1) & 1;
            int inside2 = (cubeIndex >> c2) & 1;
            if (inside1 != inside2) {
                geometricMask |= (uint16_t)(1u << e);
            }
        }
        TEST_ASSERT(mask == geometricMask, "GetEdgeMask difiere de la máscara geométrica");

        /* Aristas usadas ⊆ aristas activas */
        for (int i = 0; row[i] != -1; ++i) {
            int e = row[i];
            TEST_ASSERT((mask & (1u << e)) != 0,
                        "Fila referencia arista no cortada para el caso");
        }

        /* Aristas activas ⊆ aristas usadas (cada arista cortada aparece en la fila) */
        uint16_t usedMask = 0;
        for (int i = 0; row[i] != -1; ++i) {
            usedMask |= (uint16_t)(1u << row[i]);
        }
        TEST_ASSERT(usedMask == mask,
                    "Conjunto de aristas usadas != conjunto de aristas activas");
    }
    printf("[PASS] test_used_edges_equal_active_edges\n");
}

static void test_case_32_explicit(void) {
    const int* row = MarchingCubes_GetTriangleRow(32);
    TEST_ASSERT(row[0] == 9 && row[1] == 5 && row[2] == 4 && row[3] == -1,
                "Caso 32 (esquina 5 activa) debe ser {9,5,4}");

    TEST_ASSERT(MarchingCubes_GetTriangleCount(32) == 1, "Caso 32 debe tener 1 triángulo");
    TEST_ASSERT(MarchingCubes_GetEdgeMask(32) == (uint16_t)((1u << 9) | (1u << 5) | (1u << 4)),
                "Máscara del caso 32 debe ser aristas {4,5,9}");
    printf("[PASS] test_case_32_explicit\n");
}

static void test_edge_endpoints(void) {
    /* Pares esperados según la convención del motor */
    static const int expected[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    for (int e = 0; e < MARCHING_CUBES_EDGE_COUNT; ++e) {
        int a = -1, b = -1;
        TEST_ASSERT(MarchingCubes_GetEdgeEndpoints(e, &a, &b), "GetEdgeEndpoints false para arista válida");
        TEST_ASSERT(a == expected[e][0] && b == expected[e][1], "Extremos de arista incorrectos");
        TEST_ASSERT(a >= 0 && a < 8 && b >= 0 && b < 8, "Esquina fuera de rango");
    }

    int a = -1, b = -1;
    TEST_ASSERT(!MarchingCubes_GetEdgeEndpoints(-1, &a, &b), "GetEdgeEndpoints acepta arista negativa");
    TEST_ASSERT(!MarchingCubes_GetEdgeEndpoints(12, &a, &b), "GetEdgeEndpoints acepta arista 12");
    TEST_ASSERT(!MarchingCubes_GetEdgeEndpoints(5, NULL, &b), "GetEdgeEndpoints acepta salidas NULL");
    printf("[PASS] test_edge_endpoints\n");
}

static void test_invalid_cube_index(void) {
    TEST_ASSERT(MarchingCubes_GetTriangleRow(-1) == NULL, "GetTriangleRow acepta -1");
    TEST_ASSERT(MarchingCubes_GetTriangleRow(256) == NULL, "GetTriangleRow acepta 256");
    TEST_ASSERT(MarchingCubes_GetTriangleCount(-1) == -1, "GetTriangleCount no devuelve -1");
    TEST_ASSERT(MarchingCubes_GetTriangleCount(256) == -1, "GetTriangleCount no devuelve -1");
    TEST_ASSERT(MarchingCubes_GetEdgeMask(-1) == 0, "GetEdgeMask no devuelve 0");
    TEST_ASSERT(MarchingCubes_GetEdgeMask(256) == 0, "GetEdgeMask no devuelve 0");
    printf("[PASS] test_invalid_cube_index\n");
}

void run_marching_cubes_tests(void) {
    test_all_rows_structure();
    test_used_edges_equal_active_edges();
    test_case_32_explicit();
    test_edge_endpoints();
    test_invalid_cube_index();
}
