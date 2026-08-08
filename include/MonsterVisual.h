/**
 * @file MonsterVisual.h
 * @brief Coordinador desacoplado que sintetiza la representación visual (SDF -> Mesh) de un monstruo.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_VISUAL_H
#define MONSTER_VISUAL_H

#include "Monster.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "Mesh.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct MonsterVisual
 * @brief Estructura que gestiona el ciclo de vida del buffer de malla SDF para un monstruo.
 */
typedef struct MonsterVisual {
    MonsterSDF sdf;     /**< Snapshot SDF de la geometría */
    SDFMesher mesher;   /**< Orquestador de poligonización */
    Mesh mesh;          /**< Malla 3D poligonizada acumulada */
    bool isDirty;       /**< Flag que marca si la malla requiere reconstrucción */
    float updateTimer;  /**< Acumulativo de tiempo para reconstrucción periódica */
} MonsterVisual;

/**
 * @brief Crea e inicializa una instancia de MonsterVisual.
 */
MonsterVisual MonsterVisual_Create(SDFMesherConfig mesherConfig);

/**
 * @brief Libera los recursos asignados internamente por MonsterVisual.
 */
void MonsterVisual_Free(MonsterVisual* visual);

/**
 * @brief Marca el objeto visual como sujo/desactualizado para forzar la reconstrucción.
 */
void MonsterVisual_MarkDirty(MonsterVisual* visual);

/**
 * @brief Reconstruye inmediatamente la malla SDF del monstruo.
 * @return true si la malla fue reconstruida exitosamente.
 */
bool MonsterVisual_RebuildNow(
    MonsterVisual* visual,
    const Monster* monster,
    MonsterSDFConfig sdfConfig
);

/**
 * @brief Actualiza el temporizador y reconstruye la malla periódicamente o si está marcada como dirty.
 * @param visual Puntero al coordinador visual.
 * @param monster Instancia de Monster de la que extraer la geometría.
 * @param deltaTime Delta de tiempo en segundos.
 * @param rebuildInterval Intervalo mínimo de reconstrucción en segundos (si <= 0, solo cuando isDirty).
 * @param sdfConfig Parámetros de mezcla SDF.
 * @return true si la malla se regeneró en este frame.
 */
bool MonsterVisual_Update(
    MonsterVisual* visual,
    const Monster* monster,
    float deltaTime,
    float rebuildInterval,
    MonsterSDFConfig sdfConfig
);

/**
 * @brief Retorna un puntero de solo lectura a la malla 3D generada.
 */
const Mesh* MonsterVisual_GetMesh(const MonsterVisual* visual);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VISUAL_H
