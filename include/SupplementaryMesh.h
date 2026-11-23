/**
 * @file SupplementaryMesh.h
 * @brief Generador genérico de geometría suplementaria ligada a anatomía y deformación esquelética.
 *
 * Produce mallas opacas (plumas, ventosas, nervaduras, detalles de aletas, branquias)
 * y semitransparentes (alas de insecto, tentáculos filamentosos, brazos orales rizados)
 * derivadas de parámetros fenotípicos y del grafo anatómico resuelto, vinculadas al
 * mismo deformador esquelético O(V) sin reconstruir el SDF por fotograma.
 */
#ifndef MONSTER_SUPPLEMENTARY_MESH_H
#define MONSTER_SUPPLEMENTARY_MESH_H

#include "AnatomyDeformer.h"
#include "Mesh.h"
#include "Monster.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Construye las mallas suplementarias opaca y semitransparente en pose de reposo.
 * @param monster Criatura resuelta con fenotipo y grafo anatómico.
 * @param opaqueOut Malla de detalle opaco (limpia si no aplica).
 * @param translucentOut Malla de detalle semitransparente/doble cara (limpia si no aplica).
 * @return true si la generación fue válida y sin errores de memoria.
 */
bool SupplementaryMesh_Build(const Monster* monster, Mesh* opaqueOut, Mesh* translucentOut);

/**
 * @brief Vincula mallas suplementarias ya generadas al esqueleto para deformación O(V).
 * @param monster Criatura en pose de reposo.
 * @param skeleton Esqueleto compilado del rig.
 * @param opaqueMesh Malla opaca generada por SupplementaryMesh_Build (puede estar vacía).
 * @param opaqueDeformer Deformador destino para la malla opaca (puede ser NULL).
 * @param translucentMesh Malla translúcida generada por SupplementaryMesh_Build (puede estar vacía).
 * @param translucentDeformer Deformador destino para la malla translúcida (puede ser NULL).
 * @return true si todas las vinculaciones activas tuvieron éxito.
 */
bool SupplementaryMesh_BindDeformers(const Monster* monster,
                                     const Skeleton* skeleton,
                                     const Mesh* opaqueMesh,
                                     AnatomyDeformer* opaqueDeformer,
                                     const Mesh* translucentMesh,
                                     AnatomyDeformer* translucentDeformer);

#ifdef __cplusplus
}
#endif

#endif
