/**
 * @file MarchingCubesTables.h
 * @brief Tablas de búsqueda estándar de Marching Cubes (edgeTable y triTable).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MARCHING_CUBES_TABLES_H
#define MONSTER_MARCHING_CUBES_TABLES_H

#ifdef __cplusplus
extern "C" {
#endif

extern const int MARCHING_CUBES_EDGE_TABLE[256];
extern const int MARCHING_CUBES_TRI_TABLE[256][18];

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MARCHING_CUBES_TABLES_H
