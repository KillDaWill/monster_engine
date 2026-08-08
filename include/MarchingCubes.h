/**
 * @file MarchingCubes.h
 * @brief Interprete de topología Marching Cubes: máscara de aristas y filas de triángulos canónicas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MARCHING_CUBES_H
#define MONSTER_MARCHING_CUBES_H

#include "MarchingCubesTables.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Deriva la máscara de aristas cortadas para una configuración de celda.
 *
 * La máscara se calcula geométricamente a partir de los bits de esquina
 * (bit i = 1 si la esquina i está dentro del isosuperficie) y de la topología
 * de aristas MARCHING_CUBES_EDGE_ENDPOINTS. No se almacena ninguna tabla de
 * aristas: la máscara siempre coincide con las aristas realmente cortadas.
 * @param cubeIndex Índice de configuración en [0, 255].
 * @return Máscara de 12 bits (bit e = 1 si la arista e está cortada), o 0 si cubeIndex es inválido.
 */
uint16_t MarchingCubes_GetEdgeMask(int cubeIndex);

/**
 * @brief Obtiene la fila canónica de triángulos para una configuración de celda.
 * @param cubeIndex Índice de configuración en [0, 255].
 * @return Puntero a la fila (tripletas de aristas terminadas en -1), o NULL si cubeIndex es inválido.
 */
const int* MarchingCubes_GetTriangleRow(int cubeIndex);

/**
 * @brief Cuenta el número de triángulos de una configuración de celda.
 * @param cubeIndex Índice de configuración en [0, 255].
 * @return Número de triángulos en [0, 5], o -1 si cubeIndex es inválido.
 */
int MarchingCubes_GetTriangleCount(int cubeIndex);

/**
 * @brief Obtiene las dos esquinas conectadas por una arista.
 * @param edgeIndex Índice de arista en [0, 11].
 * @param outCornerA Salida: primera esquina.
 * @param outCornerB Salida: segunda esquina.
 * @return true si edgeIndex es válido, false en caso contrario (sin tocar las salidas).
 */
bool MarchingCubes_GetEdgeEndpoints(int edgeIndex, int* outCornerA, int* outCornerB);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MARCHING_CUBES_H
