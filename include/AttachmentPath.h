/**
 * @file AttachmentPath.h
 * @brief Rutas continuas de anclaje anatómico y distribución de arreglos de ornamentos.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_ATTACHMENT_PATH_H
#define CREATURE_ATTACHMENT_PATH_H

#include "Attachment.h"
#include "Anatomy.h"
#include "CreatureRecipe.h"
#include "CreaturePhenotype.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATTACHMENT_PATH_MAX_POINTS 32

/**
 * @struct AttachmentPathPoint
 * @brief Estación discreta a lo largo de una trayectoria continua en la superficie anatómica.
 */
typedef struct AttachmentPathPoint {
    AnatomyId node;       /**< Estación anatómica anfitriona más cercana */
    Vector3 position;     /**< Posición superficial en el espacio del modelo */
    Vector3 normal;       /**< Vector normal exterior (ej. dorsal) */
    Vector3 tangent;      /**< Vector tangente longitudinal en la dirección de la ruta */
    float t;              /**< Coordenada normalizada a lo largo de la ruta [0.0 - 1.0] */
    float hostRadius;     /**< Radio representativo del anfitrión en este punto */
} AttachmentPathPoint;

/**
 * @struct AttachmentPath
 * @brief Trayectoria paramétrica que une una secuencia de estaciones anatómicas.
 */
typedef struct AttachmentPath {
    AttachmentPathPoint points[ATTACHMENT_PATH_MAX_POINTS];
    size_t count;
    float totalLength;
} AttachmentPath;

/**
 * @struct OrnamentArray
 * @brief Distribución repetida de ornamentos (espinas, crestas, placas) a lo largo de una ruta.
 */
typedef struct OrnamentArray {
    OrnamentPhenotype ornament; /**< Fenotipo base del ornamento individual */
    float pathStart;            /**< Punto de inicio en la ruta [0.0 - 1.0] */
    float pathEnd;              /**< Punto de fin en la ruta [0.0 - 1.0] */
    unsigned count;             /**< Cantidad de ornamentos en la hilera */
    float sizeStart;            /**< Escala relativa al inicio de la hilera */
    float sizePeak;             /**< Escala relativa en el ápice de la hilera */
    float sizeEnd;              /**< Escala relativa al final de la hilera */
    float lateralOffset;        /**< Desplazamiento lateral respecto a la línea central */
} OrnamentArray;

/** @brief Campo de hileras sobre una ruta anatómica, independiente de especie. */
typedef struct OrnamentField {
    OrnamentArray row;         /**< Perfil longitudinal y forma individual. */
    unsigned rows;             /**< Número de hileras alrededor del anfitrión. */
    float angularSpread;       /**< Apertura total en radianes, centrada en el dorso. */
    float stagger;             /**< Desfase alterno en fracciones de intervalo [0,1]. */
    float sizeJitter;          /**< Variación de tamaño determinista [0,1]. */
    uint32_t seed;             /**< Semilla local del campo. */
} OrnamentField;

/**
 * @brief Instancia un campo sobre cualquier ruta axial o caudal con referencias estables.
 * @param field Distribución y fenotipo de los ornamentos.
 * @param path Ruta del anfitrión.
 * @param hostModuleId Identidad del anfitrión.
 * @param recipe Receta destino.
 * @param phenotype Fenotipo destino.
 * @return false sin cambios si la configuración o capacidad son inválidas.
 */
bool OrnamentField_Instantiate(const OrnamentField* field, const AttachmentPath* path,
    uint32_t hostModuleId, CreatureRecipe* recipe, CreaturePhenotype* phenotype);

/**
 * @struct MembranePhenotype
 * @brief Estructura dérmica continua o vela dorsal sustentada a lo largo de una ruta.
 */
typedef struct MembranePhenotype {
    float height;       /**< Altura máxima de la vela en el ápice */
    float thickness;    /**< Grosor o radio transversal de la lámina */
    float startT;       /**< Posición normalizada de inicio [0.0 - 1.0] */
    float endT;         /**< Posición normalizada de terminación [0.0 - 1.0] */
    float peakT;        /**< Posición de máxima elevación */
    float transparency; /**< Opacidad / permeabilidad visual */
} MembranePhenotype;

/**
 * @brief Extrae la ruta dorsal principal de un módulo axial a partir de sus estaciones.
 * @param graph Grafo anatómico resuelto.
 * @param axialModuleId ID de instancia del módulo axial (habitualmente 1).
 * @return Ruta dorsal parametrizada.
 */
/** @brief Ruta dorsal de las estaciones de un módulo caudal resuelto. */
AttachmentPath AttachmentPath_FromTail(const AnatomyGraph* graph, uint32_t module);

AttachmentPath AttachmentPath_FromAxialDorsal(const AnatomyGraph* graph, uint32_t axialModuleId);
AttachmentPath AttachmentPath_FromAxialLateral(const AnatomyGraph* graph, uint32_t axialModuleId, bool leftSide);

/**
 * @brief Muestrea una ranura de anclaje orientada en cualquier punto t de la ruta.
 * @param path Ruta paramétrica.
 * @param t Coordenada normalizada [0.0 - 1.0].
 * @param slotId Identificador local asignado a la ranura.
 * @param hostModuleId Módulo propietario.
 * @return Ranura orientada y lista para anclaje.
 */
AttachmentSlot AttachmentPath_Sample(
    const AttachmentPath* path,
    float t,
    AttachmentSlotId slotId,
    uint32_t hostModuleId);

/**
 * @brief Publica ranuras distribuidas en un conjunto a lo largo de una ruta.
 * @param path Ruta fuente.
 * @param count Cantidad de ranuras equidistantes a publicar.
 * @param baseSlotId ID de slot a partir del cual numerar.
 * @param hostModuleId Módulo anfitrión propietario.
 * @param slots Conjunto receptor de ranuras.
 * @return true si todas las ranuras cupieron en el conjunto.
 */
bool AttachmentPath_PublishSlots(
    const AttachmentPath* path,
    unsigned count,
    AttachmentSlotId baseSlotId,
    uint32_t hostModuleId,
    AttachmentSlotSet* slots);

/**
 * @brief Añade una hilera de ornamentos como módulos independientes a una receta y fenotipo.
 * @param array Configuración de la hilera.
 * @param path Ruta sobre la que distribuir los ornamentos.
 * @param baseModuleId ID inicial de instancia para los módulos de ornamento.
 * @param baseSlotId ID de slot correspondiente en el módulo anfitrión.
 * @param hostModuleId Módulo anfitrión al que anclar los ornamentos.
 * @param recipe Receta destino (se añaden instancias a bodyPlan).
 * @param phenotype Fenotipo destino (se añaden ornamentos al arreglo).
 * @return true si se instanciaron todos los ornamentos.
 */
bool OrnamentArray_Instantiate(
    const OrnamentArray* array,
    const AttachmentPath* path,
    uint32_t baseModuleId,
    AttachmentSlotId baseSlotId,
    uint32_t hostModuleId,
    CreatureRecipe* recipe,
    CreaturePhenotype* phenotype);

/**
 * @brief Instancia una vela dorsal o membrana continua sustentada por espinas neurales conectadas.
 * @param membrane Parámetros de la membrana/vela.
 * @param path Ruta dorsal de soporte.
 * @param baseModuleId ID inicial para los módulos de soporte.
 * @param baseSlotId ID de slot base en el módulo anfitrión.
 * @param hostModuleId Módulo anfitrión al que anclar la vela.
 * @param stationCount Cantidad de estaciones de sustentación.
 * @param recipe Receta destino.
 * @param phenotype Fenotipo destino.
 * @return true si la vela se integró correctamente.
 */
bool Membrane_Instantiate(
    const MembranePhenotype* membrane,
    const AttachmentPath* path,
    uint32_t baseModuleId,
    AttachmentSlotId baseSlotId,
    uint32_t hostModuleId,
    unsigned stationCount,
    CreatureRecipe* recipe,
    CreaturePhenotype* phenotype);

#ifdef __cplusplus
}
#endif

#endif /* CREATURE_ATTACHMENT_PATH_H */
