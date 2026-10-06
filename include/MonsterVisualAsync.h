/**
 * @file MonsterVisualAsync.h
 * @brief Administrador de renderizado visual asíncrono para criaturas SDF.
 * Desacopla la reconstrucción del volumen de polígonos (Marching Cubes) del hilo de renderizado principal (60 FPS).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_VISUAL_ASYNC_H
#define MONSTER_VISUAL_ASYNC_H

#include "Monster.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "Mesh.h"
#include "MonsterVisual.h"
#include "SurfaceMapper.h"
#include "AnatomyDeformer.h"
#include "HeadMorph.h"
#include "MorphSurfaceTracker.h"
#include "MorphModuleState.h"
#include "MorphHandoff.h"
#include "MorphSpatialCache.h"
#include "MorphChunkManager.h"
#include "RenderInterfaces.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum MonsterVisualQualityTier
 * @brief Niveles adaptativos de resolución para la generación de malla.
 */
typedef enum MonsterVisualQualityTier {
    MONSTER_VISUAL_QUALITY_INTERACTIVE = 0, /**< Malla rápida de menor resolución durante movimiento constante */
    MONSTER_VISUAL_QUALITY_SETTLED = 1,      /**< Malla detallada de alta resolución cuando la postura se estabiliza */
    MONSTER_VISUAL_QUALITY_MORPH = 2        /**< Tier adaptado para transiciones continuas de envejecimiento */
} MonsterVisualQualityTier;

/**
 * @struct MonsterVisualAsyncConfig
 * @brief Configuración para el orquestador asíncrono de mallas SDF.
 */
typedef struct MonsterVisualAsyncConfig {
    SDFMesherConfig interactiveMesherConfig; /**< Configuración mesher para tier INTERACTIVE */
    SDFMesherConfig settledMesherConfig;     /**< Configuración mesher para tier SETTLED */
    SDFMesherConfig morphMesherConfig;       /**< Configuración mesher para tier MORPH */
    SDFMesherConfig interactiveHeadMesherConfig; /**< Calidad local cefálica durante interacción. */
    SDFMesherConfig settledHeadMesherConfig; /**< Calidad local cefálica asentada. */
    SDFMesherConfig morphHeadMesherConfig;   /**< Calidad local cefálica durante MORPH. */
    MonsterSDFConfig sdfConfig;              /**< Configuración del campo SDF del monstruo */
    bool enableLocalMorphPatches; /**< Reconstruye módulos topológicos en segundo plano. */
    bool enableChunkedRemeshing; /**< Habilita remallado local incremental por chunks volumétricos en el worker. */
    float chunkSize;             /**< Dimensión métrica del bloque volumétrico (ej. 0.45f). */
    float maxGeometryAgeLag; /**< Cancela snapshots obsoletos; cero desactiva el límite. */
    float settledDelaySec;                   /**< Tiempo en segundos sin cambios para escalar a SETTLED */
    unsigned workerDelayMs; /**< Inyección determinista de latencia para regresiones; cero normalmente. */
} MonsterVisualAsyncConfig;

/**
 * @struct MonsterVisualAsyncStats
 * @brief Diagnósticos de rendimiento y métricas de ejecución en segundo plano.
 */
typedef struct MonsterVisualAsyncStats {
    uint64_t requestedFingerprint, workingFingerprint, displayedFingerprint; /**< Identidades de snapshots. */
    float workingScale, displayedScale; /**< Escala semántica de generación y presentación. */
    float targetAge, presentedMorphAge, displayedGeometryAge; /**< Edades independientes; NAN si no proceden de Ager. */
    float certifiedAge; /**< Edad de la última malla visible validada, antes de la predicción. */
    float visualAge; /**< Edad visual efectivamente proyectada/mostrada en pantalla. */
    float certifiedGeometryAge; /**< Edad de la última malla geométrica certificada por el worker. */
    float predictedGeometryAge; /**< Edad geométrica proyectada por el deformador/rastreador. */
    float topologyCertifiedAge; /**< Edad certificada a nivel de topología / blueprint. */
    float frameCPUms; /**< Tiempo CPU total estimado por fotograma en el pipeline visual. */
    float presentationCPUms; /**< Tiempo CPU consumido exclusivamente por el hilo de presentación. */
    float workerCPUms; /**< Tiempo CPU consumido por el hilo trabajador en la última tarea. */
    size_t sdfEvaluations; /**< Evaluaciones SDF acumuladas en la última actualización. */
    size_t dirtyRegionCount; /**< Conteo de regiones espaciales sucias detectadas. */
    size_t dirtyBrickCount; /**< Conteo de ladrillos espaciales marcados como sucios. */
    size_t processedBrickCount; /**< Ladrillos consumidos por snapshots construidos. */
    size_t activeDirtyBrickCount; /**< Ladrillos pendientes tras la última construcción. */
    size_t remeshedVertexCount; /**< Vértices reconstruidos mediante remallado local. */
    size_t remeshedTriangleCount; /**< Triángulos reconstruidos mediante remallado local. */
    size_t chunkCount; /**< Chunks volumétricos registrados en el gestor. */
    size_t dirtyChunkCount; /**< Chunks volumétricos actualmente marcados como sucios. */
    size_t residualChunkCount; /**< Chunks obsoletos detectados por distancia al SDF actual. */
    size_t chunkCertificationFailureCount; /**< Ensambles rechazados por separarse del SDF actual. */
    size_t surfaceChunkCount; /**< Chunks volumétricos con geometría superficial activa. */
    size_t fallbackRegionCount, fallbackModuleCount; /**< Regiones y módulos con propiedad SDF local. */
    float fallbackCoverageVolume; /**< Volumen total de las cajas locales activas. */
    uint64_t fallbackFrames; /**< Fotogramas con alguna región de respaldo. */
    size_t meshChunksRendered, meshChunksMasked, sdfFallbackRegionsRendered; /**< Diagnóstico de propiedad visible. */
    size_t meshTrianglesMasked; /**< Triángulos de módulos obsoletos omitidos en la vista. */
    size_t geometryBytesCopied; /**< Bytes de datos geométricos copiados en memoria de sistema. */
    size_t geometryBytesUploadedGPU; /**< Bytes de datos geométricos subidos a buffers GPU. */
    size_t fullMeshClones; /**< Conteo de duplicaciones de malla completa ejecutadas. */
    size_t fullMeshValidations; /**< Conteo de validaciones globales de malla ejecutadas. */
    uint64_t staleResultsDiscarded; /**< Resultados del worker descartados por desfase o generación obsoleta. */
    size_t topologyFallbackCount; /**< Conteo de activaciones del fallback ante incompatibilidad topológica. */
    float topologyFallbackDuration; /**< Duración temporal acumulada en segundos bajo fallback topológico. */
    size_t furRootsRebuiltCount; /**< Raíces de pelaje reconstruidas en el ciclo. */
    size_t surfaceRemapVertexCount; /**< Vértices cuyo mapeo superficial requirió reevaluación. */
    size_t topologyModuleCount; /**< Módulos con cambio de blueprint entre extremos. */
    uint64_t topologyFreezeCount,invalidDynamicMeshCount,obsoleteTopologyResultCount;
    size_t topologyFrozenVertices; /**< Vértices que permanecen en la edad certificada. */
    size_t bodyOpposedTriangles,bodyDegenerateTriangles,bodyBoundaryEdges;
    size_t bodyDisconnectedComponents;
    float bodyValidationMs;
    float workingGeometryAge, pendingGeometryAge; /**< Snapshot en curso y último pendiente. */
    float geometryLag; /**< Diferencia absoluta objetivo-geometría. */
    float presentedScale; /**< Escala continua realmente deformada en pantalla. */
    uint64_t requestCount;                  /**< Total de solicitudes enviadas al hilo worker */
    uint64_t completedBuildCount;           /**< Reconstrucciones completadas exitosamente */
    uint64_t cancelledBuildCount;           /**< Reconstrucciones canceladas por quedar obsoletas */
    uint64_t staleBuildDiscardedCount; /**< Construcciones terminadas fuera del límite de edad. */
    uint64_t coalescedCount;                /**< Solicitudes intermedias descartadas por coalescencia */
    uint64_t workerRetargetCount; /**< Solicitudes recientes adoptadas entre fases de corrección. */
    float lastBuildDurationMs;              /**< Duración de la última reconstrucción en milisegundos */
    float sdfBuildMs,bodyMeshMs,headMeshMs; /**< Etapas CPU del último snapshot. */
    float surfaceMappingMs,eyeBuildMs,mouthBuildMs;
    float morphBindingMs,morphDeformMs,headBindingMs;
    float readyPublicationMs,displayPublicationMs;
    MorphSurfaceTrackerStats dynamicSurface; /**< Corrección SDF del último snapshot dinámico. */
    float presentationMs; /**< Tiempo de actualización de la malla visible. */
    size_t dynamicFullFallbackCount; /**< Reconstrucciones completas tras fallo del rastreador. */
    size_t localPatchAttempts,localPatchPublications;
    float lastLocalPatchMs;
    uint32_t lastLocalPatchModule;
    SurfaceMapperStats surfaceMapper;       /**< Candidatos anatómicos realmente evaluados. */
    bool isWorkerBusy;                     /**< Indica si el worker está construyendo una malla activamente */
    MonsterVisualQualityTier activeQualityTier; /**< Tier de calidad usado en la malla mostrada */
    SDFMesherStats bodyMesher;              /**< Rejilla efectiva del cuerpo grueso. */
    SDFMesherStats headMesher;              /**< Rejilla efectiva de la cabeza local. */
} MonsterVisualAsyncStats;

/** @brief Propiedad visible de una región durante la transición. */
typedef enum MorphRepresentationMode {
    MORPH_REP_CERTIFIED_MESH,
    MORPH_REP_PREDICTED_MESH,
    MORPH_REP_LOCAL_SDF,
    MORPH_REP_ABSENT /**< El módulo desapareció; se retira su malla antigua. */
} MorphRepresentationMode;

/** @brief Posición y normal certificadas transportadas a la pose visible. */
typedef struct MorphAnchoredVertex {
    Vector3 position,normal;
} MorphAnchoredVertex;

/** @brief Campo anatómico dueño de una región de respaldo. */
typedef enum MorphFallbackKind {
    MORPH_FALLBACK_MODULE,
    MORPH_FALLBACK_AXIAL,
    MORPH_FALLBACK_HEAD
} MorphFallbackKind;

/** @brief Región de respaldo independiente asociada a un módulo anatómico. */
typedef struct MorphFallbackRegion {
    uint32_t moduleId;
    MorphFallbackKind kind;
    AABB3D bounds;
    uint64_t morphGeneration;
    float targetAge;
    MorphRepresentationMode representation;
    MorphHandoffInterface handoff;
    MorphHandoffState handoffState;
    float certifiedAge,visualAge;
} MorphFallbackRegion;

#define MORPH_MAX_FALLBACK_REGIONS 32

/** @brief Mismo tipo ocular que la ruta síncrona; transferencia entre buffers. */
typedef MonsterVisualEye MonsterVisualEyeAsync;

/**
 * @struct MonsterVisualAsync
 * @brief Gestor asíncrono desacoplado con arquitectura de triple buffer y coalescencia de solicitudes.
 */
typedef struct MonsterVisualAsync {
    MonsterVisualAsyncConfig config;

    /* Front Buffer / Mallas expuestas al hilo principal de renderizado */
    Mesh displayMesh;
    Mesh displayCertifiedMesh; /**< Copia inmutable de la última malla publicada. */
    MorphAnchoredVertex* displayAnchoredFrozen;
    size_t displayAnchoredCapacity;
    uint8_t* displayTopologyMask; /**< Vértices de módulos que no admiten extrapolación. */
    size_t* displayInterfaceTriangles; /**< Índices base de triángulos en la frontera de interfaz. */
    size_t displayInterfaceTriangleCount; /**< Cantidad de triángulos de interfaz precalculados. */
    bool displayPredicted;
    Mesh displayHeadMesh;
    MonsterVisualEyeAsync* displayEyes;
    size_t displayEyeCount;
    size_t displayEyeCapacity;
    uint64_t displayGeneration;
    uint64_t displayEndpointGeneration; /**< Extremos que certificaron el front buffer. */
    uint64_t displayFingerprint;
    MonsterVisualMouth* displayMouths;
    size_t displayMouthCount;
    size_t displayMouthCapacity;

    /* Sincronización y worker thread */
    pthread_t workerThread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool threadRunning;
    bool shouldQuit;

    /* Back Buffer / Snapshot pendiente solicitada (latest-request-wins) */
    bool hasPendingRequest;
    Monster pendingSnapshot;
    Monster morphStartSnapshot,morphEndSnapshot; /**< Extremos semánticos para la referencia unión. */
    bool hasMorphEndpoints;
    uint64_t morphEndpointGeneration;
    MorphModuleState* morphModules; /**< Política de certificación por identidad semántica. */
    size_t morphModuleCount;
    uint64_t pendingFingerprint;
    MonsterVisualQualityTier pendingTier;

    /* Ready Buffer / Mallas listas generadas por el worker */
    bool hasReadyMesh;
    Mesh readyMesh;
    Mesh readyHeadMesh;
    MonsterVisualEyeAsync* readyEyes;
    size_t readyEyeCount;
    size_t readyEyeCapacity;
    uint64_t readyGeneration;
    uint64_t readyEndpointGeneration;
    uint64_t readyFingerprint;
    float readyScale; /**< Escala del snapshot publicado. */
    float readyAppendageDevelopment; /**< Fase topológica del snapshot listo. */
    float displayAppendageDevelopment; /**< Fase topológica de la malla visible. */
    MonsterVisualQualityTier readyTier;
    MonsterVisualMouth* readyMouths;
    size_t readyMouthCount;
    size_t readyMouthCapacity;

    /* Control de temporización de movimiento para cambio de tier */
    uint64_t lastObservedFingerprint;
    uint64_t lastPresentedFingerprint; /**< Evita redeformar/recrear anexos sin cambios. */
    float timeSinceLastMotionSec;
    bool continuousMotion; /**< Evita asentamientos durante una interacción sostenida. */
    bool morphMode;        /**< Utiliza el tier MORPH en lugar de INTERACTIVE durante movimiento continuo */
    HeadMorph* displayHeadMorph; /**< Jaula cefálica publicada. */
    HeadMorph* readyHeadMorph; /**< Jaula cefálica lista. */
    MorphSurfaceTracker displayTracker,readyTracker; /**< Correcciones por vértice publicadas. */
    bool displayDynamicMorph,readyDynamicMorph;
    AnatomyDeformer* displayMorph; /**< Deformador morfológico en tiempo real para display */
    AnatomyDeformer* readyMorph;   /**< Deformador morfológico listo para transferir */
    struct SDFSamplingPool* samplingPool; /**< Pool persistente de hilos compartido con los meshers */
    MorphChunkManager* displayChunkManager; /**< Chunks volumétricos para renderizado/inspección. */
    MorphChunkManager* readyChunkManager;   /**< Chunks volumétricos listos generados por el worker. */

    /* Métricas */
    MonsterVisualAsyncStats readyStats, displayStats; /**< Métricas transferidas con las mallas. */
    MonsterVisualAsyncStats stats;
} MonsterVisualAsync;

/**
 * @brief Retorna una configuración asíncrona por defecto balanceada.
 */
MonsterVisualAsyncConfig MonsterVisualAsync_DefaultConfig(void);

/** @brief Resuelve la configuración corporal efectiva compartida por worker y regresiones.
 * @param config Configuración de las tres calidades.
 * @param tier Calidad solicitada.
 * @param lizard Si el cuerpo incorpora detalle anatómico cefálico.
 * @return Configuración con el presupuesto de la superficie unificada.
 */
SDFMesherConfig MonsterVisualAsync_ResolveBodyConfig(const MonsterVisualAsyncConfig* config,
    MonsterVisualQualityTier tier, bool hasUnifiedAnatomy);


/**
 * @brief Inicializa el gestor asíncrono y crea el hilo worker de reconstrucción.
 * @return Puntero asignado en heap a MonsterVisualAsync o NULL si falla.
 */
MonsterVisualAsync* MonsterVisualAsync_Create(MonsterVisualAsyncConfig config);

/**
 * @brief Detiene de forma segura el hilo worker y libera todas las mallas y memorias poseídas.
 */
void MonsterVisualAsync_Free(MonsterVisualAsync* asyncMgr);

/**
 * @brief Actualización llamada por el hilo principal (60 FPS, NUNCA bloquea).
 * Intercambia mallas listas y encola snapshots del estado del monstruo.
 * @param asyncMgr Gestor asíncrono.
 * @param monster Puntero al monstruo activo.
 * @param deltaTime Tiempo transcurrido en segundos desde el último frame.
 * @return true si la malla mostrada (displayMesh) fue actualizada en este fotograma.
 */
bool MonsterVisualAsync_Update(MonsterVisualAsync* asyncMgr, const Monster* monster, float deltaTime);

/** @brief Actualiza geometría cuantizada y apariencia continua por canales separados. */
bool MonsterVisualAsync_UpdateWithAppearance(MonsterVisualAsync* asyncMgr,
    const Monster* geometryMonster,const Monster* appearanceMonster,float deltaTime);

/** @brief Indica si el backend debe dibujar el SDF exacto al exceder el desfase permitido. */
bool MonsterVisualAsync_NeedsExactFallback(const MonsterVisualAsync* asyncMgr,
    float visualAge,float maximumFrozenAgeLag);

/** @brief Obtiene la región AABB acotada que requiere fallback de SDF cuando hay módulos desfasados.
 * @param asyncMgr Administrador asíncrono.
 * @param visualAge Edad visual del fotograma actual.
 * @param maximumFrozenAgeLag Desfase temporal tolerado antes de activar fallback.
 * @param outBounds Puntero donde se almacenará el AABB delimitador de la región afectada.
 * @return true si al menos un módulo requiere fallback y se delimitó la región.
 */
bool MonsterVisualAsync_GetFallbackBounds(const MonsterVisualAsync* asyncMgr,
    float visualAge,float maximumFrozenAgeLag,AABB3D* outBounds);

/** @brief Enumera módulos con respaldo exacto y sus cajas independientes.
 * @param asyncMgr Gestor visual.
 * @param currentField Campo compilado para la edad visual actual.
 * @param visualAge Edad visual.
 * @param maximumFrozenAgeLag Desfase permitido.
 * @param output Arreglo receptor.
 * @param capacity Capacidad del arreglo.
 * @return Número de regiones escritas.
 */
/** @brief Actualiza las rampas de representación una vez por fotograma. */
void MonsterVisualAsync_UpdateHandoffs(MonsterVisualAsync*,float,float,float);
size_t MonsterVisualAsync_GetFallbackRegions(const MonsterVisualAsync* asyncMgr,
    const MonsterSDF* currentField,const AnatomyGraph* currentGraph,
    float visualAge,float maximumFrozenAgeLag,
    MorphFallbackRegion* output,size_t capacity);

/** @brief Devuelve la malla íntegra: la cesión recorta fragmentos en GPU. */
const Mesh* MonsterVisualAsync_GetOwnedMesh(MonsterVisualAsync* asyncMgr,
    const MorphFallbackRegion* regions,size_t count,size_t* maskedTriangles);

/** @brief Registra propiedad visible observada al terminar un fotograma. */
void MonsterVisualAsync_RecordFallbackFrame(MonsterVisualAsync* asyncMgr,
    const MorphFallbackRegion* regions,size_t count,size_t sdfRendered,
    bool meshRendered,size_t maskedTriangles);

/** @brief Mantiene el tier interactivo durante una animación; libera settled al detenerse. */
void MonsterVisualAsync_SetContinuousMotion(MonsterVisualAsync* asyncMgr,bool active);

/** @brief Activa o desactiva el modo de transición morfológica continua (usa MORPH tier durante movimiento). */
void MonsterVisualAsync_SetMorphMode(MonsterVisualAsync* asyncMgr, bool active);

/** @brief Inyecta latencia antes del trabajo siguiente para probar el fallback asíncrono. */
void MonsterVisualAsync_SetWorkerDelay(MonsterVisualAsync* asyncMgr,unsigned milliseconds);

/** @brief Configura los extremos para una superficie unión persistente inter-especies. */
bool MonsterVisualAsync_SetMorphEndpoints(MonsterVisualAsync* asyncMgr,
    const Monster* start,const Monster* end);

/**
 * @brief Obtiene la malla del cuerpo lista para ser renderizada.
 */
const Mesh* MonsterVisualAsync_GetDisplayMesh(const MonsterVisualAsync* asyncMgr);

/** @brief Obtiene la malla local de cabeza de alta resolución. */
const Mesh* MonsterVisualAsync_GetDisplayHeadMesh(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Obtiene el número de ojos activos en la malla mostrada.
 */
size_t MonsterVisualAsync_GetDisplayEyeCount(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Obtiene la esclerótica del ojo en el índice especificado.
 */
const Mesh* MonsterVisualAsync_GetDisplayEyeSclera(const MonsterVisualAsync* asyncMgr, size_t index);

/** @brief Obtiene la malla visible del iris. */
const Mesh* MonsterVisualAsync_GetDisplayEyeIris(const MonsterVisualAsync* asyncMgr, size_t index);

/**
 * @brief Obtiene la pupila del ojo en el índice especificado.
 */
const Mesh* MonsterVisualAsync_GetDisplayEyePupil(const MonsterVisualAsync* asyncMgr, size_t index);

/**
 * @brief Obtiene el número de bocas activas en la malla mostrada.
 */
size_t MonsterVisualAsync_GetDisplayMouthCount(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Obtiene mandíbula (0) o bisagra (1) de la boca indicada.
 */
const Mesh* MonsterVisualAsync_GetDisplayMouthMesh(
    const MonsterVisualAsync* asyncMgr,
    size_t mouthIndex,
    size_t meshIndex
);

/**
 * @brief Renderiza todas las mallas visibles (cuerpo, bocas y ojos) utilizando la VTable de Renderer3D.
 */
bool MonsterVisualAsync_Render(const MonsterVisualAsync* asyncMgr, Renderer3D* renderer);

/**
 * @brief Retorna la generación actual de la malla en pantalla.
 */
uint64_t MonsterVisualAsync_GetDisplayGeneration(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Retorna un puntero de sólo lectura a las estadísticas del gestor asíncrono.
 */
MonsterVisualAsyncStats MonsterVisualAsync_GetStats(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Retorna el gestor de chunks volumétricos publicado en el front buffer si está disponible.
 */
const MorphChunkManager* MonsterVisualAsync_GetChunkManager(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Bloquea el hilo actual hasta que el worker procese las solicitudes pendientes.
 */
void MonsterVisualAsync_Flush(MonsterVisualAsync* asyncMgr);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VISUAL_ASYNC_H
