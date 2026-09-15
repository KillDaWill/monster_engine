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
 * @brief Mallas primitivas de globo, iris y pupila que representan un ojo.
 */
typedef struct MonsterVisualEye {
    Mesh sclera; /**< Malla UV-esfera de la esclerótica (base blanca) */
    Mesh iris;   /**< Disco elipsoidal coloreado orientado hacia fuera. */
    Mesh pupil;  /**< Pupila oscura sobre el iris. */
} MonsterVisualEye;

/**
 * @struct MonsterVisualMouth
 * @brief Componentes cerrados de mandíbula y tejido blando de bisagra.
 */
typedef struct MonsterVisualMouth {
    Mesh jawBase;         /**< Malla base inmutable en coordenadas de la boca */
    Mesh jaw;             /**< Malla articulada expuesta al renderizador */
    Mesh hingeBase;       /**< Base inmutable del puente gular */
    Mesh hinge;           /**< Tejido blando alrededor del pivote */
    Vector3 pivot;
    Vector3 worldPosition;
    Vector3 rotation;
    Vector3 seamSkullLeft;
    Vector3 seamSkullRight;
    Vector3 seamJawLeftClosed;
    Vector3 seamJawRightClosed;
    Vector3 seamGular;
    Vector3 seamJawAnchor;
    float seamScale;
    Vector3 basePivot;        /**< Pivote de la mandíbula al momento del mallado base */
    float baseJawLength;      /**< Longitud mandibular de referencia */
    float baseJawWidth;       /**< Anchura mandibular de referencia */
    float baseJawThickness;   /**< Grosor mandibular de referencia */
    float baseScale;          /**< Escala ontogenética del monstruo al mallar */
} MonsterVisualMouth;

/**
 * @brief Actualiza las mallas de los ojos según los parámetros anatómicos del monstruo sin recalcular el SDF.
 * @param eyes Arreglo de mallas de ojos.
 * @param eyeCount Cantidad de ojos asignados en el arreglo.
 * @param monster Monstruo con los ojos actualizados.
 * @return true si la actualización fue exitosa.
 */
bool MonsterVisual_UpdateEyes(
    MonsterVisualEye* eyes,
    size_t eyeCount,
    const Monster* monster
);

/**
 * @brief Construye la mandíbula y la bisagra sin generar geometría de labios.
 * @param visualMouth Estructura de salida cuya propiedad se transfiere al llamador.
 * @param mouth Parámetros de la boca.
 * @param monster Snapshot del monstruo que contiene la parte de anclaje.
 * @return true si los componentes fueron construidos.
 */
bool MonsterVisual_BuildMouthMeshes(
    MonsterVisualMouth* visualMouth,
    const Mouth* mouth,
    const Monster* monster
);

/** Construye una boca usando explícitamente el índice de su snapshot SDF. */
bool MonsterVisual_BuildMouthMeshesFromSDF(
    MonsterVisualMouth* visualMouth,
    const Mouth* mouth,
    const Monster* monster,
    const MonsterSDF* sdf,
    size_t mouthIndex
);

/** Construye una boca reutilizando instancias preasignadas de SDFMesher para evitar churn en hebras worker. */
bool MonsterVisual_BuildMouthMeshesFromSDFWithMeshers(
    MonsterVisualMouth* visualMouth,
    const Mouth* mouth,
    const Monster* monster,
    const MonsterSDF* sdf,
    size_t mouthIndex,
    SDFMesher* jawMesher,
    SDFMesher* seamMesher
);

/** Actualiza únicamente las posiciones y normales de la mandíbula articulada. */
void MonsterVisual_UpdateMouthArticulation(MonsterVisualMouth* visualMouth, const Mouth* mouth, const Monster* monster);

/**
 * @brief Libera los componentes poseídos por una boca visual.
 */
void MonsterVisualMouth_Free(MonsterVisualMouth* visualMouth);

/**
 * @struct MonsterVisual
 * @brief Estructura que gestiona el ciclo de vida del buffer de malla SDF, ojos y bocas para un monstruo.
 */
typedef struct MonsterVisual {
    MonsterSDF sdf;         /**< Snapshot SDF activo de la geometría */
    MonsterSDF stagingSdf;  /**< Snapshot SDF de trabajo (reutilizable) */
    SDFMesher mesher;       /**< Orquestador de poligonización */
    SDFMesher headMesher;   /**< Orquestador local de alta resolución cefálica. */
    Mesh mesh;              /**< Malla 3D poligonizada activa (cuerpo) */
    Mesh stagingMesh;       /**< Malla 3D de trabajo (reutilizable) */
    Mesh headMesh;          /**< Malla local anatómica de cabeza activa. */
    Mesh stagingHeadMesh;   /**< Malla cefálica transaccional de trabajo. */
    MonsterVisualEye* eyes; /**< Arreglo dinámico de mallas de ojos */
    size_t eyeCount;        /**< Cantidad actual de ojos */
    size_t eyeCapacity;     /**< Capacidad reservada de ojos */
    MonsterVisualMouth* mouths; /**< Arreglo dinámico de mallas de bocas */
    size_t mouthCount;          /**< Cantidad actual de bocas */
    size_t mouthCapacity;       /**< Capacidad reservada de bocas */
    uint64_t geometryFingerprint;    /**< FNV-1a-64 de la geometría del cuerpo */
    uint64_t mouthVisualFingerprint; /**< FNV-1a-64 de la representación visual de boca */
    uint64_t rebuildGeneration;      /**< Contador incremental de reconstrucciones del cuerpo */
    uint64_t mouthVisualGeneration;  /**< Contador incremental de reconstrucciones de bocas */
    bool hasFingerprint; /**< true si geometryFingerprint ya fue calculated */
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
 * @brief Marca el objeto visual como sucio/desactualizado para forzar la reconstrucción.
 */
void MonsterVisual_MarkDirty(MonsterVisual* visual);

/**
 * @brief Obtiene el contador de generaciones de reconstrucción exitosas del cuerpo SDF.
 */
uint64_t MonsterVisual_GetGeneration(const MonsterVisual* visual);

/** @brief Comprueba que la generación publicada pertenece a esta morfología sin reconstruir. */
bool MonsterVisual_MatchesGeometry(const MonsterVisual* visual,const Monster* monster);

/**
 * @brief Obtiene el contador de generaciones de reconstrucción de bocas visuales.
 */
uint64_t MonsterVisual_GetMouthVisualGeneration(const MonsterVisual* visual);

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
 * @brief Actualiza el temporizador y reconstruye la malla periódicamente si es necesario,
 * respetando el tiempo mínimo entre reconstrucciones (minRebuildInterval) cuando la
 * geometría o estado sucio lo requieran.
 * @param visual Puntero al coordinador visual.
 * @param monster Instancia de Monster de la que extraer la geometría.
 * @param deltaTime Delta de tiempo en segundos.
 * @param minRebuildInterval Tiempo mínimo entre reconstrucciones automáticas en segundos (si <= 0, reconstrucción inmediata).
 * @param sdfConfig Parámetros de mezcla SDF.
 * @return true si la malla se regeneró en este frame.
 */
bool MonsterVisual_Update(
    MonsterVisual* visual,
    const Monster* monster,
    float deltaTime,
    float minRebuildInterval,
    MonsterSDFConfig sdfConfig
);

/**
 * @brief Retorna un puntero de solo lectura a la malla 3D generada (cuerpo).
 */
const Mesh* MonsterVisual_GetMesh(const MonsterVisual* visual);

/** @brief Retorna la malla cefálica local de alta resolución. */
const Mesh* MonsterVisual_GetHeadMesh(const MonsterVisual* visual);

/**
 * @brief Retorna la cantidad de ojos sintetizados.
 */
size_t MonsterVisual_GetEyeCount(const MonsterVisual* visual);

/**
 * @brief Retorna la malla de esclerótica del ojo en el índice dado (NULL si no existe).
 */
const Mesh* MonsterVisual_GetEyeSclera(const MonsterVisual* visual, size_t index);

/** @brief Retorna la malla de iris del ojo indicado. */
const Mesh* MonsterVisual_GetEyeIris(const MonsterVisual* visual, size_t index);

/**
 * @brief Retorna la malla de pupila del ojo en el índice dado (NULL si no existe).
 */
const Mesh* MonsterVisual_GetEyePupil(const MonsterVisual* visual, size_t index);

/**
 * @brief Retorna la cantidad de bocas sintetizadas.
 */
size_t MonsterVisual_GetMouthCount(const MonsterVisual* visual);

/**
 * @brief Retorna la malla del labio superior de la boca en el índice dado (NULL si no existe).
 */
/** @brief Retorna la malla articulada de mandíbula. */
const Mesh* MonsterVisual_GetJaw(const MonsterVisual* visual, size_t index);

/** @brief Retorna la malla de tejido blando de bisagra. */
const Mesh* MonsterVisual_GetHinge(const MonsterVisual* visual, size_t index);

/**
 * @brief Envía la malla superior, mandíbula, bisagra y ojos al renderizador.
 * @return true si se envió al menos la malla del cuerpo correctamente.
 */
/** @brief Actualiza sólo recetas; no marca geometría sucia ni toca coordenadas. */
void MonsterVisual_SetSurface(MonsterVisual* visual,const SurfacePhenotype* surface);

bool MonsterVisual_Render(const MonsterVisual* visual, Renderer3D* renderer);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VISUAL_H
