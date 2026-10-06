/** @file MorphHandoff.h
 * @brief Interfaz geométrica continua para cesión entre malla explícita y SDF local.
 *
 * Proporciona el plano de articulación proximal, radio seccional, banda de
 * transición suave y planos de recorte continuo para evitar saltos y desconexiones
 * en miembros durante la metamorfosis asíncrona.
 */
#ifndef MORPH_HANDOFF_H
#define MORPH_HANDOFF_H

#include "Vector.h"
#include "AABB.h"
#include "Anatomy.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/** Capacidad máxima de interfaces simultáneas activas (hombros, caderas, cola). */
#define MORPH_HANDOFF_MAX_INTERFACES 16
#define MORPH_HANDOFF_MAX_SEGMENTS 128
#define MORPH_HANDOFF_GPU_SEGMENTS 1024

/** @brief Soporte anatómico finito; nunca se deriva de triángulos ni de AABB. */
typedef struct MorphHandoffSegment { Vector3 a,b; float radius; } MorphHandoffSegment;

/** @struct MorphHandoffInterface
 * @brief Interfaz geométrica continua de cesión en la inserción proximal de un miembro.
 */
typedef struct MorphHandoffInterface {
    uint32_t moduleId;         /**< Identificador del módulo anatómico. */
    uint32_t chunkId;          /**< Chunk espacial asociado. */
    Vector3 origin;            /**< Centro de la articulación proximal (origen del plano). */
    Vector3 normal;            /**< Normal unitaria saliente a lo largo del eje del miembro. */
    float radius;              /**< Radio transversal de la sección en la articulación. */
    float hardClipOffset;      /**< Desplazamiento longitudinal respecto al origen. */
    float blendWidth;          /**< Semiancho espacial de la banda de transición continua. */
    float transitionFactor;    /**< Factor de avance temporal (0.0 = malla pura, 1.0 = SDF puro). */
    MorphHandoffSegment segments[MORPH_HANDOFF_MAX_SEGMENTS];
    size_t segmentCount;
    bool retireOnly;           /**< Retira una referencia ausente sin generar superficie SDF. */
    bool active;               /**< Indica si esta interfaz se encuentra activa en el fotograma. */
} MorphHandoffInterface;

/** @struct MorphHandoffSet
 * @brief Conjunto de interfaces de cesión activas en un fotograma de visualización.
 */
typedef struct MorphHandoffSet {
    MorphHandoffInterface interfaces[MORPH_HANDOFF_MAX_INTERFACES];
    size_t count;
} MorphHandoffSet;

/** @brief Inicializa un conjunto de interfaces vacío.
 * @param set Puntero al conjunto a reiniciar.
 */
void MorphHandoffSet_Init(MorphHandoffSet* set);

/** @brief Construye las interfaces de cesión a partir del grafo anatómico.
 *
 * Localiza las articulaciones proximales (raíz escapular, coxal y base caudal)
 * de los módulos indicados y calcula los planos y radios continuos correspondientes.
 *
 * @param graph Grafo anatómico de referencia con estaciones y conexiones resueltas.
 * @param fallbackModuleIds Arreglo de identificadores de módulos en fallback.
 * @param fallbackModuleCount Cantidad de módulos en fallback.
 * @param set Estructura receptora de las interfaces construidas.
 * @return Cantidad de interfaces válidas registradas en el conjunto.
 */
size_t MorphHandoff_BuildFromGraph(const AnatomyGraph* graph,
    const uint32_t* fallbackModuleIds, size_t fallbackModuleCount,
    MorphHandoffSet* set);

/** @brief Evalúa la distancia longitudinal con signo respecto a una interfaz.
 *
 * Retorna valores positivos hacia el dominio distal (SDF) y negativos hacia
 * el dominio proximal o corporal (malla).
 *
 * @param handoff Interfaz de cesión evaluada.
 * @param point Punto mundial tridimensional a consultar.
 * @return Distancia con signo en metros.
 */
float MorphHandoff_EvaluateDistance(const MorphHandoffInterface* handoff, Vector3 point);

/** @brief Obtiene la ecuación del plano de corte GPU [nx, ny, nz, d].
 *
 * La ecuación cumple n.x*p.x + n.y*p.y + n.z*p.z + d >= 0 para la porción que se
 * desea conservar en la malla corporal, incorporando el solapamiento del semiancho suave.
 *
 * @param handoff Interfaz de cesión activa.
 * @param outPlane Arreglo de 4 floats donde se almacenan [nx, ny, nz, d].
 */
void MorphHandoff_GetMeshClipPlane(const MorphHandoffInterface* handoff, float outPlane[4]);

/** @brief Evalúa el factor continuo de mezcla espacial para un punto (0 = malla, 1 = SDF).
 * @param set Conjunto de interfaces activas.
 * @param point Punto mundial a consultar.
 * @return Factor de mezcla normalizado en [0.0, 1.0].
 */
float MorphHandoff_EvaluateBlend(const MorphHandoffSet* set, Vector3 point);

/** @brief Añade el soporte de una referencia transportada como el anclaje congelado.
 * @return false si la capacidad no permite conservar todo el soporte.
 */
bool MorphHandoff_AddReference(MorphHandoffInterface*,const AnatomyGraph*,const AnatomyGraph*);
/** @brief Distancia de propiedad acotada al soporte anatómico. */
float MorphHandoff_EvaluateDomainDistance(const MorphHandoffInterface*,Vector3);
/** @brief Caja de trabajo conservadora del soporte más banda y tolerancia. */
AABB3D MorphHandoff_WorkBounds(const MorphHandoffInterface*);
#endif
