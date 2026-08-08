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
#include "RenderInterfaces.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct MonsterVisualEye
 * @brief Par de mallas primitivas (esclerótica + pupila) que representan un ojo.
 */
typedef struct MonsterVisualEye {
    Mesh sclera; /**< Malla UV-esfera de la esclerótica (base blanca) */
    Mesh pupil;  /**< Malla UV-esfera de la pupila/iris (sobresale al frente) */
} MonsterVisualEye;

/**
 * @struct MonsterVisual
 * @brief Estructura que gestiona el ciclo de vida del buffer de malla SDF y ojos para un monstruo.
 */
typedef struct MonsterVisual {
    MonsterSDF sdf;     /**< Snapshot SDF de la geometría */
    SDFMesher mesher;   /**< Orquestador de poligonización */
    Mesh mesh;          /**< Malla 3D poligonizada acumulada (cuerpo) */
    MonsterVisualEye* eyes; /**< Arreglo dinámico de mallas de ojos */
    size_t eyeCount;    /**< Cantidad actual de ojos */
    size_t eyeCapacity; /**< Capacidad reservada de ojos */
    uint64_t geometryFingerprint; /**< FNV-1a-64 de la geometría que invalida la reconstrucción */
    bool hasFingerprint; /**< true si geometryFingerprint ya fue calculado */
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
 * @brief Reconstruye inmediatamente la malla SDF del monstruo y sus ojos.
 * @return true si la malla fue reconstruida exitosamente.
 */
bool MonsterVisual_RebuildNow(
    MonsterVisual* visual,
    const Monster* monster,
    MonsterSDFConfig sdfConfig
);

/**
 * @brief Actualiza el temporizador y reconstruye la malla periódicamente, si está
 * marcada como dirty o si la huella geométrica del monstruo cambió (fingerprint).
 * @param visual Puntero al coordinador visual.
 * @param monster Instancia de Monster de la que extraer la geometría.
 * @param deltaTime Delta de tiempo en segundos.
 * @param rebuildInterval Intervalo mínimo de reconstrucción en segundos (si <= 0, solo cuando isDirty o fingerprint).
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
 * @brief Retorna un puntero de solo lectura a la malla 3D generada (cuerpo).
 */
const Mesh* MonsterVisual_GetMesh(const MonsterVisual* visual);

/**
 * @brief Retorna la cantidad de ojos sintetizados.
 */
size_t MonsterVisual_GetEyeCount(const MonsterVisual* visual);

/**
 * @brief Retorna la malla de esclerótica del ojo en el índice dado (NULL si no existe).
 */
const Mesh* MonsterVisual_GetEyeSclera(const MonsterVisual* visual, size_t index);

/**
 * @brief Retorna la malla de pupila del ojo en el índice dado (NULL si no existe).
 */
const Mesh* MonsterVisual_GetEyePupil(const MonsterVisual* visual, size_t index);

/**
 * @brief Envía la malla del cuerpo y todas las mallas de ojos al renderizador agnóstico.
 * @return true si se envió al menos la malla del cuerpo correctamente.
 */
bool MonsterVisual_Render(const MonsterVisual* visual, MonsterRenderer* renderer);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VISUAL_H
