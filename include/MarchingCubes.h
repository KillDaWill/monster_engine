/**
 * @file MarchingCubes.h
 * @brief Algoritmo Marching Cubes para polygonización de campos SDF a mallas poligonales 3D.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MARCHING_CUBES_H
#define MONSTER_MARCHING_CUBES_H

#include "Mesh.h"
#include "SDFSampling.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct MarchingCubesCell
 * @brief Representación de una celda cúbica con sus 8 vértices y muestras SDF.
 */
typedef struct MarchingCubesCell {
    Vector3 corners[8];   /**< Posiciones 3D de las 8 esquinas del cubo */
    SDFSample samples[8]; /**< Muestras SDF evaluadas en las 8 esquinas */
} MarchingCubesCell;

/**
 * @brief Poligoniza una única celda cúbica y añade los triángulos generados a la malla.
 * @param cell Celda con las 8 esquinas y muestras SDF.
 * @param isolevel Valor isosuperficie (normalmente 0.0f).
 * @param mesh Malla poligonal de destino.
 * @param evalFn Función opcional para estimar normales por gradiente (si es NULL, interpola de las esquinas).
 * @param context Contexto para evalFn.
 * @param normalEps Épsilon para el cálculo de normales.
 */
void MarchingCubes_PolygonizeCell(
    const MarchingCubesCell* cell,
    float isolevel,
    Mesh* mesh,
    SDFEvaluateFn evalFn,
    const void* context,
    float normalEps
);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MARCHING_CUBES_H
