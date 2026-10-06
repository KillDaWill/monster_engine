/** @file MeshPatch.h
 * @brief Sustitución transaccional de una región de malla por un parche abierto.
 */
#ifndef MONSTER_MESH_PATCH_H
#define MONSTER_MESH_PATCH_H

#include "Mesh.h"
#include "SDFSampling.h"
#include "AABB.h"

/** @brief Métricas de una sustitución local validada. */
typedef struct MeshPatchStats {
    size_t removedTriangles,insertedTriangles,seamTriangles;
    size_t boundaryLoops;
    size_t oldBoundaryLoops,newBoundaryLoops;
    size_t oldBoundaryEdges,newBoundaryEdges;
    int oldBoundaryError,newBoundaryError;
    Vector3 oldLoopCenters[8],newLoopCenters[8];
    float loopNormalDot[8];
    float maximumSeamDistance;
    float maximumSeamResidual;
    float oldBoundaryMaximumResidual,newBoundaryMaximumResidual;
    size_t seamOpposedTriangles,seamDistanceEvaluations;
    size_t oldSeamOpposedTriangles,newSeamOpposedTriangles;
    float firstOpposedOrientation,firstOpposedArea;
    Vector3 firstOpposedCenter;
} MeshPatchStats;

/** @brief Campo y tolerancia para comprobar la costura antes de publicarla. */
typedef struct MeshPatchValidation {
    const SDFField* field;
    float maximumResidual;
    bool hasBoundaryBlend;
    float boundaryBlend; /**< 0 conserva borde antiguo (por defecto); 1 el parche. */
    int loopPhaseShift; /**< Desplazamiento cíclico de las muestras del parche. */
    bool angularSeam; /**< Correspondencia por ángulo en secciones casi planas. */
} MeshPatchValidation;

/** @brief Procedencia de los vértices tras compactar una sustitución local.
 * UINT32_MAX identifica vértices nuevos; los demás índices apuntan a la
 * malla entregada a MeshPatch_ReplaceWithTransfer. */
typedef struct MeshPatchTransfer {
    uint32_t* oldVertexOfNew;
    size_t vertexCount;
} MeshPatchTransfer;

/** @brief Libera la tabla de procedencia de una sustitución. */
void MeshPatchTransfer_Free(MeshPatchTransfer* transfer);

/** @brief Cierra las esquinas de selección con varios sectores incidentes. */
bool MeshPatch_RegularizeSelection(const Mesh* mesh,uint8_t* removeTriangle,
    size_t* expandedTriangles);

/**
 * @brief Parte una malla cerrada por los seis planos de una caja, sin grietas.
 *
 * Cada arista cortada crea un único vértice compartido y se interpolan sus
 * atributos superficiales. Devuelve una máscara de caras dentro de la caja.
 * `cut` debe estar inicializada con Mesh_Create(). El llamador libera
 * `removeTriangle` con free(). No modifica la malla fuente.
 */
bool MeshPatch_CutBox(const Mesh* source,AABB3D box,Mesh* cut,
    uint8_t** removeTriangle,size_t* removedCount);

/** @brief Recorta una superficie local abierta a los mismos planos de caja.
 * Conserva únicamente las caras interiores y compacta sus vértices. */
bool MeshPatch_ExtractInsideBox(const Mesh* source,AABB3D box,Mesh* inside);

/**
 * @brief Sustituye triángulos marcados por una superficie abierta y cose sus bordes.
 *
 * Las dos fronteras deben contener el mismo número de lazos orientados. La
 * operación no modifica `mesh` si falla cualquier comprobación. El parche
 * puede tener otra triangulación y otra topología en su interior.
 *
 * @param mesh Malla cerrada que se modificará tras validar el resultado.
 * @param removeTriangle Máscara de un byte por triángulo de la malla original.
 * @param patch Malla local abierta con la orientación exterior correcta.
 * @param maximumGap Límite absoluto de separación entre las dos fronteras.
 * @param validation Campo y tolerancia opcionales para rechazar costuras erróneas.
 * @param stats Métricas opcionales de la sustitución.
 * @return true si la nueva malla permanece cerrada y válida.
 */
bool MeshPatch_Replace(Mesh* mesh,const uint8_t* removeTriangle,
    const Mesh* patch,float maximumGap,const MeshPatchValidation* validation,
    MeshPatchStats* stats);

/** @brief Variante que conserva la correspondencia de vértices intactos.
 * `transfer` debe estar inicializada a cero. Un fallo deja intactos la malla
 * y el contenido anterior de la tabla; un éxito sustituye la tabla anterior. */
bool MeshPatch_ReplaceWithTransfer(Mesh* mesh,const uint8_t* removeTriangle,
    const Mesh* patch,float maximumGap,const MeshPatchValidation* validation,
    MeshPatchStats* stats,MeshPatchTransfer* transfer);

#endif
