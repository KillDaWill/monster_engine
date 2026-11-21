/**
 * @file Membrane.h
 * @brief Generador genérico de membranas interdigitales y patagios ligados al grafo anatómico.
 */
#ifndef MONSTER_MEMBRANE_H
#define MONSTER_MEMBRANE_H

#include "AnatomyDeformer.h"
#include "Mesh.h"
#include "Monster.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Construye la malla cerrada de membranas y patagios en pose de reposo.
 * @param monster Criatura con fenotipo y grafo anatómico resueltos.
 * @param outMesh Malla de salida (se limpia cuando no hay membranas activas).
 * @return true si se generó sin errores de memoria.
 */
bool Membrane_BuildMesh(const Monster* monster, Mesh* outMesh);

/**
 * @brief Construye la malla de membranas y los pesos articulares exactos de borde.
 * @param monster Criatura resuelta en reposo.
 * @param skeleton Esqueleto del rig.
 * @param outMesh Malla de salida.
 * @param deformer Deformador esquelético ya vinculado para animaciones O(V).
 * @return true si todo se construyó y vinculó correctamente (o no había membranas).
 */
bool Membrane_BuildAndBind(const Monster* monster, const Skeleton* skeleton,
                           Mesh* outMesh, AnatomyDeformer* deformer);

/**
 * @brief Vincula una malla de membranas ya generada al esqueleto sin sobreescribir sus colores.
 * @param monster Criatura resuelta en reposo.
 * @param skeleton Esqueleto del rig.
 * @param mesh Malla existente con la misma topología generada por Membrane_BuildMesh.
 * @param deformer Deformador esquelético destino.
 * @return true si se vinculó correctamente (o la malla está vacía).
 */
bool Membrane_BindDeformer(const Monster* monster, const Skeleton* skeleton,
                           const Mesh* mesh, AnatomyDeformer* deformer);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_MEMBRANE_H */
