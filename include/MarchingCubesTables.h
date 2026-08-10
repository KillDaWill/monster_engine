/**
 * @file MarchingCubesTables.h
 * @brief Tablas de búsqueda de Marching Cubes: tabla de triángulos canónica y topología de aristas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MARCHING_CUBES_TABLES_H
#define MONSTER_MARCHING_CUBES_TABLES_H

#ifdef __cplusplus
extern "C" {
#endif

/** Número de esquinas de una celda cúbica */
#define MARCHING_CUBES_CORNER_COUNT 8

/** Número de aristas de una celda cúbica */
#define MARCHING_CUBES_EDGE_COUNT 12

/** Número máximo de entradas por fila de la tabla de triángulos (15 + terminador -1) */
#define MARCHING_CUBES_MAX_ROW_ENTRIES 16

/** Número máximo de triángulos por configuración de celda */
#define MARCHING_CUBES_MAX_TRIANGLES 5

/**
 * @brief Tabla de triángulos canónica de Marching Cubes (256 casos).
 *
 * Cada fila contiene tripletas de aristas que forman triángulos, terminadas en -1.
 * Garantizado: toda arista referenciada está cortada para el caso (coincide con
 * MarchingCubes_GetEdgeMask).
 */
extern const int MARCHING_CUBES_TRI_TABLE[256][MARCHING_CUBES_MAX_ROW_ENTRIES];

/**
 * @brief Topología de aristas: esquinas conectadas por cada una de las 12 aristas.
 *
 * Convención de esquinas: 0=(0,0,0), 1=(1,0,0), 2=(1,0,1), 3=(0,0,1),
 * 4=(0,1,0), 5=(1,1,0), 6=(1,1,1), 7=(0,1,1).
 * Aristas: 0..3 anillo inferior, 4..7 anillo superior, 8..11 verticales (0->4, 1->5, 2->6, 3->7).
 */
extern const int MARCHING_CUBES_EDGE_ENDPOINTS[MARCHING_CUBES_EDGE_COUNT][2];

/**
 * @brief Coordenadas enteras locales (0 o 1) de las 8 esquinas de una celda cúbica.
 */
extern const int MARCHING_CUBES_CORNER_OFFSETS[8][3];

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MARCHING_CUBES_TABLES_H
