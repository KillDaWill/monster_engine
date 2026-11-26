/**
 * @file EnvironmentRenderer.h
 * @brief Capa compartida de presentación ambiental para Evolution Arena y Brain Evolution Lab.
 *
 * Unifica el renderizado del terreno chunked, agua natural, rocas instanciadas,
 * overlays conformes al relieve, minimapa topográfico y picking de rayos sobre el relieve.
 * En modo plano (WORLD_MODE_FLAT o world==NULL) mantiene compatibilidad visual exacta.
 */
#ifndef MONSTER_ENVIRONMENT_RENDERER_H
#define MONSTER_ENVIRONMENT_RENDERER_H

#include "TerrainRenderer.h"
#include "WaterRenderer.h"
#include "RockVisualCache.h"
#include "WorldStaticData.h"
#include "RenderInterfaces.h"
#include "Mesh.h"
#include "SceneLighting.h"
#include "EcosystemVisual.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EnvironmentPassStats {
    double cpuMeanMs, gpuMeanMs, gpuP95Ms;
    size_t gpuSamples;
} EnvironmentPassStats;

typedef struct EnvironmentRenderStats {
    bool isProcedural;
    TerrainRenderStats terrain;
    WaterRenderStats water;
    RockRenderStats rocks;
    uint64_t totalTriangles;
    uint64_t totalDrawCalls;
    size_t totalVramBytes, irradianceVramBytes;
    size_t materialVramBytes;
    double lastUploadMs;
    EnvironmentPassStats terrainPass, waterPass, rockPass, shadowPass, contactPass, objectPass, decorationPass;
    size_t shadowVramBytes, shadowDraws, shadowTriangles, shadowCacheMisses;
    size_t objectDraws, objectTriangles, decorationDraws, decorationTriangles, compositionVramBytes;
} EnvironmentRenderStats;

typedef struct EnvironmentRenderer {
    void *impl;
} EnvironmentRenderer;

bool EnvironmentRenderer_Init(EnvironmentRenderer *er);
void EnvironmentRenderer_Destroy(EnvironmentRenderer *er);

/** @brief Sincroniza recursos GPU con un WorldStaticData inmutable (solo reconstruye si cambia época/huella). */
bool EnvironmentRenderer_SyncWorld(EnvironmentRenderer *er, const WorldStaticData *world);

/** @brief Devuelve true si el mundo actualmente vinculado es procedural (no plano). */
bool EnvironmentRenderer_IsProcedural(const EnvironmentRenderer *er);

/** @brief Devuelve el puntero retenido al WorldStaticData actual (o NULL en modo plano). */
const WorldStaticData *EnvironmentRenderer_GetWorld(const EnvironmentRenderer *er);

void EnvironmentRenderer_SetDebugMode(EnvironmentRenderer *er, TerrainDebugMode mode);
TerrainDebugMode EnvironmentRenderer_GetDebugMode(const EnvironmentRenderer *er);
/** @brief Fuerza la malla física completa para diagnóstico. */
void EnvironmentRenderer_ForceTerrainLod0(EnvironmentRenderer *er, bool enabled);
void EnvironmentRenderer_SetQuality(EnvironmentRenderer *er, float terrainLodScale,
                                    int waterQualityTier, bool wireframe);
void EnvironmentRenderer_SetOverlays(EnvironmentRenderer *er, bool showShoreSites,
                                     bool showRockCollision, bool showTerrainGrid);

/** @brief Activa consultas de GPU diferidas para cada pasada de entorno. */
void EnvironmentRenderer_SetProfiling(EnvironmentRenderer *er, bool enabled);

/** @brief Configura sombras independientemente del LOD y los materiales. */
void EnvironmentRenderer_SetShadows(EnvironmentRenderer *er, SceneShadowQuality quality);
bool EnvironmentRenderer_SetSun(EnvironmentRenderer *er, float azimuth, float elevation);
void EnvironmentRenderer_SetPeriodicMaterials(EnvironmentRenderer *er, bool periodic);
/** @brief Selecciona el relleno global o local y el diagnóstico de materiales de criatura. */
void EnvironmentRenderer_SetCreatureLighting(EnvironmentRenderer *er, bool globalFill, unsigned debug);
void EnvironmentRenderer_SetDetails(EnvironmentRenderer *er, bool contacts, bool decoration);
void EnvironmentRenderer_QueueDecoration(EnvironmentRenderer *er, Renderer3D *renderer, const ICamera *camera);

/** @brief Ejecuta la lista de dibujo en profundidad y color. Mantiene iluminación hasta EndScene. */
bool EnvironmentRenderer_RenderScene(EnvironmentRenderer *er, Renderer3D *renderer,
                                    const ICamera *camera, bool drawRecorded,
                                    const EcosystemVisualDrawPacket *packets, size_t count);
/** @brief Iluminación natural de la lista grabada de una arena plana. */
bool EnvironmentRenderer_RenderFlatScene(EnvironmentRenderer *er, Renderer3D *renderer,
                                         const ICamera *camera, float halfSize);
void EnvironmentRenderer_EndScene(EnvironmentRenderer *er);
/** @brief Exporta el depth map utilizado en el último frame para diagnóstico. */
bool EnvironmentRenderer_SaveShadowDepth(const EnvironmentRenderer *er, const char *path);

/** @brief Dibuja geometría opaca del entorno (terreno + rocas en procedural; suelo plano + rejilla en plano). */
bool EnvironmentRenderer_RenderOpaque(EnvironmentRenderer *er, Renderer3D *renderer,
                                      const ICamera *camera,
                                      float flatHalfSize, const Mesh *flatGroundMesh,
                                      float maxSlopeNavRadians, float maxWadeNavDepth);

/** @brief Dibuja superficie de agua natural (en modo plano es no-op). */
bool EnvironmentRenderer_RenderWater(EnvironmentRenderer *er, const ICamera *camera, float timeSeconds);

/** @brief Dibuja overlays de diagnóstico conformes al relieve (orillas, proxies de roca, rejilla). */
void EnvironmentRenderer_RenderOverlays(EnvironmentRenderer *er, const ICamera *camera);

/** @brief Dibuja el fondo topográfico/hidrológico del minimapa en coordenadas de pantalla 2D (ortho). */
void EnvironmentRenderer_DrawMinimapBackground(const EnvironmentRenderer *er, float x, float y, float size);

/** @brief Intersecta terreno o, al pedir un cuerpo, la primera superficie visible de agua v2. */
bool EnvironmentRenderer_PickGroundRay(const EnvironmentRenderer *er, Vector3 rayOrigin, Vector3 rayDir,
                                       float maxDist, Vector3 *outHitPos, uint16_t *outWaterBodyId);

/** @brief Muestrea la altura del suelo (o 0.0 en modo plano) en (x, z). */
float EnvironmentRenderer_SampleGroundHeight(const EnvironmentRenderer *er, float x, float z);

/** @brief Dibuja un anillo de línea conforme a la altura real del terreno en (cx, cz). */
void EnvironmentRenderer_DrawTerrainCircle(const EnvironmentRenderer *er, float cx, float cz,
                                           float radius, float liftY, int segments);

/** @brief Dibuja una polilínea conforme al relieve (rutas A*, objetivos). */
void EnvironmentRenderer_DrawTerrainPath(const EnvironmentRenderer *er, const Vector3 *pts,
                                         uint32_t count, float liftY);

EnvironmentRenderStats EnvironmentRenderer_GetStats(const EnvironmentRenderer *er);
EnvironmentRenderStats EnvironmentRenderer_GetProfile(const EnvironmentRenderer *er);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_ENVIRONMENT_RENDERER_H */
