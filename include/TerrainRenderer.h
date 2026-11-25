/**
 * @file TerrainRenderer.h
 * @brief Renderizador chunked OpenGL con LOD, costura sin grietas, culling de frustum,
 *        splatting de 4 materiales procedurales y modos de diagnóstico del terreno.
 */
#ifndef MONSTER_TERRAIN_RENDERER_H
#define MONSTER_TERRAIN_RENDERER_H

#include "WorldStaticData.h"
#include "RenderInterfaces.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TerrainDebugMode {
    TERRAIN_DEBUG_NATURAL     = 0,
    TERRAIN_DEBUG_ELEVATION   = 1,
    TERRAIN_DEBUG_SLOPE       = 2,
    TERRAIN_DEBUG_HYDROLOGY   = 3,
    TERRAIN_DEBUG_WATER_DEPTH = 4,
    TERRAIN_DEBUG_BIOME       = 5,
    TERRAIN_DEBUG_NAVIGATION  = 6,
    TERRAIN_DEBUG_COLLISION   = 7,
    TERRAIN_DEBUG_MODE_COUNT  = 8
} TerrainDebugMode;

typedef struct TerrainRenderStats {
    uint32_t totalChunks;
    uint32_t renderedChunks;
    uint32_t culledChunks;
    uint32_t lodCounts[4];
    uint64_t renderedTriangles;
    uint64_t drawCalls;
    uint64_t gpuUploads;
    size_t vramBytes;
} TerrainRenderStats;

typedef struct TerrainRenderer {
    void *impl;
} TerrainRenderer;

/** @brief Nombre textual legible del modo de depuración del terreno. */
const char *TerrainDebugMode_Name(TerrainDebugMode mode);

/** @brief Inicializa estado del renderizador de terreno (requiere contexto OpenGL activo al compilar/subir). */
bool TerrainRenderer_Init(TerrainRenderer *tr);

/** @brief Libera shaders, VAOs, VBOs y EBOs GPU. */
void TerrainRenderer_Destroy(TerrainRenderer *tr);

/** @brief Sincroniza/construye chunks GPU una única vez por huella/época de WorldStaticData. */
bool TerrainRenderer_SyncWorld(TerrainRenderer *tr, const WorldStaticData *world);

/** @brief Selecciona modo de depuración visual sin reconstruir mallas. */
void TerrainRenderer_SetDebugMode(TerrainRenderer *tr, TerrainDebugMode mode);
TerrainDebugMode TerrainRenderer_GetDebugMode(const TerrainRenderer *tr);

/** @brief Ajusta factor de calidad/distancia LOD (0.6 = bajo, 1.0 = medio, 1.35 = alto). */
void TerrainRenderer_SetQuality(TerrainRenderer *tr, float lodQualityScale, bool wireframe);

/** @brief Dibuja chunks visibles del terreno con LOD sin grietas y restaura estado OpenGL. */
bool TerrainRenderer_Render(TerrainRenderer *tr, const ICamera *camera,
                            float maxSlopeNavRadians, float maxWadeNavDepth);

/** @brief Métricas del último frame y memoria GPU estimada. */
TerrainRenderStats TerrainRenderer_GetStats(const TerrainRenderer *tr);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_TERRAIN_RENDERER_H */
