/**
 * @file AnatomyDeformer.h
 * @brief Deformador genérico de morfología y pose para mallas procedurales.
 * Permite interpolar y deformar una malla continua O(V) vinculando los
 * vértices a las estaciones del esqueleto anatómico (AnatomyGraph).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_ANATOMY_DEFORMER_H
#define MONSTER_ANATOMY_DEFORMER_H

#include "Mesh.h"
#include "Anatomy.h"
#include "Vector.h"
#include "Skeleton.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct AnatomyDeformerBinding
 * @brief Vinculación de un vértice a dos conexiones anatómicas dominantes.
 */
typedef struct AnatomyDeformerBinding {
    uint16_t connIndex[2];  /**< Índices de conexiones en el AnatomyGraph. */
    float weight[2];        /**< Pesos normalizados de influencia (suma = 1.0). */
    float projT[2];         /**< Proyección paramétrica t en el segmento [0, 1]. */
    Vector3 localOffset[2]; /**< Desplazamiento local relativo a la proyección de referencia. */
    float refRadius[2];     /**< Radio medio de la sección en la pose de referencia. */
} AnatomyDeformerBinding;

/**
 * @struct AnatomySkinBinding
 * @brief Hasta cuatro influencias articulares, derivadas de dos conexiones.
 */
typedef struct AnatomySkinBinding { int joints[4]; float weights[4]; } AnatomySkinBinding;

/** @brief Bases inmutables y buffers persistentes de deformación. */
typedef struct AnatomyDeformer {
    AnatomyGraph refGraph;           /**< Grafo anatómico en la pose de referencia. */
    AnatomyDeformerBinding* bindings;    /**< Arreglo dinámico de vinculaciones por vértice. */
    size_t vertexCount;              /**< Cantidad de vértices vinculados. */
    size_t vertexCapacity;           /**< Capacidad reservada de vértices. */
    Vector3* basePositions;          /**< Posiciones originales en la pose base. */
    Vector3* baseNormals;            /**< Normales originales en la pose base. */
    AnatomySkinBinding* skinBindings;
    SkeletonPose bindPose;
    bool poseBound;
    bool isBound;                    /**< Indica si la malla base está vinculada. */
} AnatomyDeformer;

/**
 * @brief Crea una nueva instancia de AnatomyDeformer.
 * @return Puntero a AnatomyDeformer o NULL en caso de error.
 */
AnatomyDeformer* AnatomyDeformer_Create(void);

/**
 * @brief Libera los recursos de una instancia de AnatomyDeformer.
 * @param morph Puntero a la instancia.
 */
void AnatomyDeformer_Free(AnatomyDeformer* morph);

/**
 * @brief Vincula una malla base al grafo anatómico de referencia.
 * @param morph Puntero al deformador.
 * @param baseMesh Malla poligonal de referencia.
 * @param refGraph Grafo anatómico en la misma pose que baseMesh.
 * @return true si la vinculación fue exitosa.
 */
bool AnatomyDeformer_Bind(AnatomyDeformer* morph, const Mesh* baseMesh, const AnatomyGraph* refGraph);

/**
 * @brief Deforma una malla en tiempo real según un nuevo grafo anatómico.
 * @param morph Deformador con malla previamente vinculada.
 * @param newGraph Grafo anatómico destino resuelto a la edad actual.
 * @param targetMesh Malla cuyos vértices serán actualizados in situ.
 * @return true si la deformación fue exitosa.
 */
bool AnatomyDeformer_Deform(const AnatomyDeformer* morph, const AnatomyGraph* newGraph, Mesh* targetMesh);

/**
 * @brief Consulta si el deformador tiene una vinculación activa válida.
 * @param morph Puntero al deformador.
 * @return true si está vinculado y listo para deformar.
 */
bool AnatomyDeformer_IsBound(const AnatomyDeformer* morph);

/**
 * @brief Retorna la cantidad de vértices vinculados en el deformador.
 * @param morph Puntero al deformador.
 * @return Número de vértices vinculados.
 */
size_t AnatomyDeformer_GetVertexCount(const AnatomyDeformer* morph);

/** @brief Vincula malla y transformados de reposo; reserva solo durante bind. */
bool AnatomyDeformer_BindSkeleton(AnatomyDeformer* deformer,const Mesh* mesh,
    const AnatomyGraph* rest,const Skeleton* skeleton);
/** @brief Deforma desde bases inmutables usando rotaciones completas (incluido twist). */
bool AnatomyDeformer_DeformPose(const AnatomyDeformer* deformer,const Skeleton* skeleton,
    const SkeletonPose* pose,Mesh* mesh);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_ANATOMY_DEFORMER_H */
