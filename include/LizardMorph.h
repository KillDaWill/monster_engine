/**
 * @file LizardMorph.h
 * @brief Deformador morfológico en tiempo real para lagartos procedurales.
 * Permite interpolar y deformar una malla continua O(V) a 60 FPS vinculando los
 * vértices a las estaciones del esqueleto anatómico (AnatomyGraph).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_LIZARD_MORPH_H
#define MONSTER_LIZARD_MORPH_H

#include "Mesh.h"
#include "Anatomy.h"
#include "Vector.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct LizardMorphBinding
 * @brief Vinculación de un vértice a dos conexiones anatómicas dominantes.
 */
typedef struct LizardMorphBinding {
    uint16_t connIndex[2];  /**< Índices de conexiones en el AnatomyGraph. */
    float weight[2];        /**< Pesos normalizados de influencia (suma = 1.0). */
    float projT[2];         /**< Proyección paramétrica t en el segmento [0, 1]. */
    Vector3 localOffset[2]; /**< Desplazamiento local relativo a la proyección de referencia. */
    float refRadius[2];     /**< Radio medio de la sección en la pose de referencia. */
} LizardMorphBinding;

/**
 * @struct LizardMorph
 * @brief Estructura del deformador morfológico en tiempo real.
 */
typedef struct LizardMorph {
    AnatomyGraph refGraph;           /**< Grafo anatómico en la pose de referencia. */
    LizardMorphBinding* bindings;    /**< Arreglo dinámico de vinculaciones por vértice. */
    size_t vertexCount;              /**< Cantidad de vértices vinculados. */
    size_t vertexCapacity;           /**< Capacidad reservada de vértices. */
    Vector3* basePositions;          /**< Posiciones originales en la pose base. */
    Vector3* baseNormals;            /**< Normales originales en la pose base. */
    bool isBound;                    /**< Indica si la malla base está vinculada. */
} LizardMorph;

/**
 * @brief Crea una nueva instancia de LizardMorph.
 * @return Puntero a LizardMorph o NULL en caso de error.
 */
LizardMorph* LizardMorph_Create(void);

/**
 * @brief Libera los recursos de una instancia de LizardMorph.
 * @param morph Puntero a la instancia.
 */
void LizardMorph_Free(LizardMorph* morph);

/**
 * @brief Vincula una malla base al grafo anatómico de referencia.
 * @param morph Puntero al deformador.
 * @param baseMesh Malla poligonal de referencia.
 * @param refGraph Grafo anatómico en la misma pose que baseMesh.
 * @return true si la vinculación fue exitosa.
 */
bool LizardMorph_Bind(LizardMorph* morph, const Mesh* baseMesh, const AnatomyGraph* refGraph);

/**
 * @brief Deforma una malla en tiempo real según un nuevo grafo anatómico.
 * @param morph Deformador con malla previamente vinculada.
 * @param newGraph Grafo anatómico destino resuelto a la edad actual.
 * @param targetMesh Malla cuyos vértices serán actualizados in situ.
 * @return true si la deformación fue exitosa.
 */
bool LizardMorph_Deform(const LizardMorph* morph, const AnatomyGraph* newGraph, Mesh* targetMesh);

/**
 * @brief Consulta si el deformador tiene una vinculación activa válida.
 * @param morph Puntero al deformador.
 * @return true si está vinculado y listo para deformar.
 */
bool LizardMorph_IsBound(const LizardMorph* morph);

/**
 * @brief Retorna la cantidad de vértices vinculados en el deformador.
 * @param morph Puntero al deformador.
 * @return Número de vértices vinculados.
 */
size_t LizardMorph_GetVertexCount(const LizardMorph* morph);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_LIZARD_MORPH_H */
