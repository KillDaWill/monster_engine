/**
 * @file WaterRenderer.h
 * @brief Renderizador GPU estático de cuerpos de agua naturales (lagos y ríos)
 *        construido directamente desde la geometría húmeda física de WorldHydrology.
 */
#ifndef MONSTER_WATER_RENDERER_H
#define MONSTER_WATER_RENDERER_H

#include "WorldStaticData.h"
#include "RenderInterfaces.h"
#include "TerrainMaterials.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WaterRenderStats {
    uint64_t triangles;
    uint64_t drawCalls;
    size_t vramBytes;
} WaterRenderStats;

typedef struct WaterRenderer {
    void *impl;
} WaterRenderer;

bool WaterRenderer_Init(WaterRenderer *wr);
void WaterRenderer_Destroy(WaterRenderer *wr);
bool WaterRenderer_SyncWorld(WaterRenderer *wr, const WorldStaticData *world);
void WaterRenderer_SetQuality(WaterRenderer *wr, int qualityTier);
bool WaterRenderer_Render(WaterRenderer *wr, const ICamera *camera,
                          const TerrainMaterials *materials, float timeSeconds, bool showContour);
WaterRenderStats WaterRenderer_GetStats(const WaterRenderer *wr);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WATER_RENDERER_H */
