/**
 * @file RockVisualCache.h
 * @brief Caché visual GPU instanciada para rocas procedurales SDF generadas desde WorldGeology.
 *
 * Extrae mallas cerradas deterministas por variante a partir de WorldGeology_EvaluateVariantSDF,
 * las renderiza mediante instanciado GPU con culling de frustum y ofrece overlay de diagnóstico
 * para inspeccionar los proxies conservadores de colisión (RockCollisionLobe).
 */
#ifndef MONSTER_ROCK_VISUAL_CACHE_H
#define MONSTER_ROCK_VISUAL_CACHE_H

#include "WorldStaticData.h"
#include "RenderInterfaces.h"
#include "TerrainMaterials.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RockRenderStats {
    uint32_t variantCount;
    uint32_t totalInstances;
    uint32_t renderedInstances;
    uint32_t culledInstances;
    uint64_t renderedTriangles;
    uint64_t drawCalls;
    size_t vramBytes;
} RockRenderStats;

typedef struct RockVisualCache {
    void *impl;
} RockVisualCache;

/** @brief Inicializa el caché visual de rocas (requiere contexto GL al sincronizar/renderizar). */
bool RockVisualCache_Init(RockVisualCache *cache);

/** @brief Libera shaders, VAOs, VBOs y EBOs GPU. */
void RockVisualCache_Destroy(RockVisualCache *cache);

/** @brief Sincroniza catálogo de variantes e instancias desde WorldStaticData una sola vez por época. */
bool RockVisualCache_SyncWorld(RockVisualCache *cache, const WorldStaticData *world);

/** @brief Renderiza todas las rocas visibles mediante instanciado GPU por variante. */
bool RockVisualCache_Render(RockVisualCache *cache, const ICamera *camera,
                            const TerrainMaterials *materials, bool materialDetail);

/** @brief Dibuja wireframes de los proxies conservadores de colisión de rocas en coordenadas mundiales. */
void RockVisualCache_RenderCollisionDebug(const RockVisualCache *cache, const WorldStaticData *world,
                                          const ICamera *camera);

/** @brief Métricas del último frame y memoria VRAM estimada. */
RockRenderStats RockVisualCache_GetStats(const RockVisualCache *cache);

void RockVisualCache_RenderShadow(RockVisualCache *renderer, const ICamera *camera);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_ROCK_VISUAL_CACHE_H */
