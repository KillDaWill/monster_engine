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

#define ANATOMY_MORPH_INFLUENCES 4

/**
 * @struct AnatomyDeformerBinding
 * @brief Vinculación de un vértice a conexiones anatómicas con pesos de soporte continuo.
 */
typedef struct AnatomyDeformerBinding {
    uint16_t connIndex[ANATOMY_MORPH_INFLUENCES];  /**< Índices de conexiones en el AnatomyGraph. */
    float weight[ANATOMY_MORPH_INFLUENCES];        /**< Pesos normalizados de influencia (suma = 1.0). */
    float projT[ANATOMY_MORPH_INFLUENCES];         /**< Proyección paramétrica t en el segmento [0, 1]. */
    Vector3 localOffset[ANATOMY_MORPH_INFLUENCES]; /**< Desplazamiento local relativo a la proyección de referencia. */
    float refRadius[ANATOMY_MORPH_INFLUENCES];     /**< Radio medio de la sección en la pose de referencia. */
} AnatomyDeformerBinding;

/**
 * @struct AnatomySkinBinding
 * @brief Influencias articulares derivadas de las conexiones anatómicas.
 */
typedef struct AnatomySkinBinding { int joints[2*ANATOMY_MORPH_INFLUENCES]; float weights[2*ANATOMY_MORPH_INFLUENCES]; } AnatomySkinBinding;

/** @brief Matrices afines de skinning compartidas por CPU y GPU, en reposo anatómico. */
typedef struct AnatomyPosePalette {
    float rows[SKELETON_MAX_JOINTS][3][4];
    size_t jointCount;
} AnatomyPosePalette;

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

/** @brief Compila matrices de la pose actual sin asignaciones.
 * @param deformer Vinculación. @param skeleton Esqueleto. @param pose Pose actual.
 * @param palette Salida. @return false si no corresponde al reposo vinculado. */
bool AnatomyDeformer_BuildPosePalette(const AnatomyDeformer* deformer,const Skeleton* skeleton,
    const SkeletonPose* pose,AnatomyPosePalette* palette);
/** @brief Paleta de pose con secciones de crecimiento; reutiliza geometría y pesos adultos. */
bool AnatomyDeformer_BuildMorphPosePalette(const AnatomyDeformer*,const Skeleton*,const SkeletonPose*,
    const AnatomyGraph* target,AnatomyPosePalette*);
/** @brief Reemplaza una matriz articular por la escala local de un landmark acotado. */
bool AnatomyDeformer_ScaleJointPalette(const AnatomyDeformer*,const SkeletonPose*,int,Vector3,AnatomyPosePalette*);
/** @brief Evalúa un vértice con las mismas influencias y normalización del skinning.
 * @param deformer Vinculación. @param palette Matrices actuales. @param vertex Índice.
 * @param position Posición. @param normal Normal. @return false ante una influencia inválida. */
bool AnatomyDeformer_EvaluatePoseVertex(const AnatomyDeformer* deformer,const AnatomyPosePalette* palette,
    size_t vertex,Vector3* position,Vector3* normal);

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
 * @brief Copia los buffers y vinculaciones de un deformador a otro.
 * @param dst Deformador destino.
 * @param src Deformador origen.
 * @return true si la copia fue exitosa.
 */
bool AnatomyDeformer_Copy(AnatomyDeformer* dst, const AnatomyDeformer* src);

/**
 * @brief Vincula una malla base al grafo anatómico de referencia.
 * @param morph Puntero al deformador.
 * @param baseMesh Malla poligonal de referencia.
 * @param refGraph Grafo anatómico en la misma pose que baseMesh.
 * @return true si la vinculación fue exitosa.
 */
bool AnatomyDeformer_Bind(AnatomyDeformer* morph, const Mesh* baseMesh, const AnatomyGraph* refGraph);

/**
 * @brief Reancla una malla cosida conservando influencias de vértices intactos.
 *
 * Los índices UINT32_MAX, o superiores al recuento anterior, se vinculan
 * mediante una búsqueda anatómica completa. Para los demás se transportan
 * conexiones y pesos semánticos y se recalcula únicamente su marco local.
 * Requiere grafos topológicamente compatibles; el fallo deja el deformador
 * anterior intacto. El mapa tiene una entrada por vértice de `mesh`.
 */
bool AnatomyDeformer_Reanchor(AnatomyDeformer* morph,const Mesh* mesh,
    const AnatomyGraph* newGraph,const uint32_t* oldVertexOfNew,
    size_t* newlyBoundVertices);

/**
 * @brief Deforma una malla en tiempo real según un nuevo grafo anatómico.
 * @param morph Deformador con malla previamente vinculada.
 * @param newGraph Grafo anatómico destino resuelto a la edad actual.
 * @param targetMesh Malla cuyos vértices serán actualizados in situ.
 * @return true si la deformación fue exitosa.
 */
bool AnatomyDeformer_Deform(const AnatomyDeformer* morph, const AnatomyGraph* newGraph, Mesh* targetMesh);
/** @brief Deforma sólo vértices no congelados; conserva la interfaz certificada. */
bool AnatomyDeformer_DeformMasked(const AnatomyDeformer* morph,
    const AnatomyGraph* newGraph,Mesh* targetMesh,const uint8_t* frozenVertices);

/** @brief Ancla cinemáticamente vértices congelados al marco local de su articulación proximal.
 * Permite que extremidades en transición topológica acompañen el movimiento del animal
 * sin desgarrar la malla ni quedar flotando en coordenadas absolutas obsoletas.
 * @param morph Deformador vinculado con el grafo de referencia certificado.
 * @param newGraph Grafo anatómico actual de la criatura.
 * @param certifiedMesh Malla del snapshot certificado de referencia.
 * @param targetMesh Malla objetivo donde se actualizarán los vértices congelados.
 * @param frozenMask Máscara por vértice (distinto de cero = congelado).
 * @return true si el anclaje fue exitoso.
 */
bool AnatomyDeformer_AnchorFrozenVertices(const AnatomyDeformer* morph,
    const AnatomyGraph* newGraph,const Mesh* certifiedMesh,
    Mesh* targetMesh,const uint8_t* frozenMask);

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
/** @brief Fija el cráneo a su articulación; sólo el collar posterior mezcla piel.
 * @param deformer Vinculación esquelética actual. @param skeleton Esqueleto vinculado.
 * @param headJoint Articulación cefálica. @param attachment Plano occipital en reposo.
 * @param axis Normal hacia el cráneo. @param collar Longitud de transición posterior.
 * @param sdf Campo de reposo opcional: conserva propiedad cefálica al variar proporciones.
 * @return false si la vinculación o el perfil no son válidos. */
struct MonsterSDF;
bool AnatomyDeformer_BindCranium(AnatomyDeformer* deformer,const Skeleton* skeleton,
    int headJoint,Vector3 attachment,Vector3 axis,float collar,const struct MonsterSDF* sdf);
/** @brief Peso rígido craneal; uno delante del occipucio, cero detrás del collar.
 * @param position Muestra de reposo. @param attachment Plano occipital.
 * @param axis Normal unitaria hacia el cráneo. @param collar Anchura posterior.
 * @return Peso suave entre cero y uno. */
float AnatomyDeformer_CranialWeight(Vector3 position,Vector3 attachment,Vector3 axis,float collar);
/** @brief Deforma desde bases inmutables usando rotaciones completas (incluido twist). */
bool AnatomyDeformer_DeformPose(const AnatomyDeformer* deformer,const Skeleton* skeleton,
    const SkeletonPose* pose,Mesh* mesh);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_ANATOMY_DEFORMER_H */
