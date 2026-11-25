/**
 * @file TerrainHeightfield.h
 * @brief Campo de alturas acotado con triangulación autoritativa compartida entre física y render.
 *
 * Convención de coordenadas y triángulos:
 * - Y es arriba, XZ es el plano horizontal centrado en (0, 0), dominio [-halfSize, +halfSize].
 * - Cada celda (ix, iz) con esquinas v00=(ix,iz), v10=(ix+1,iz), v01=(ix,iz+1), v11=(ix+1,iz+1)
 *   y coordenadas fraccionales u, v en [0, 1] se divide exactamente por la diagonal u = v (v00-v11):
 *   * Si u >= v: triángulo T0 = (v00, v11, v10), h = h00 + u*(h10-h00) + v*(h11-h10).
 *   * Si u <  v: triángulo T1 = (v00, v01, v11), h = h00 + v*(h01-h00) + u*(h11-h01).
 * - Ambos triángulos tienen orientación CCW vistos desde +Y (normal física hacia +Y).
 * - Las consultas fuera de dominio o con entradas no finitas devuelven false explícitamente.
 */
#ifndef MONSTER_TERRAIN_HEIGHTFIELD_H
#define MONSTER_TERRAIN_HEIGHTFIELD_H

#include "Vector.h"
#include "WorldInterface.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TERRAIN_MIN_RESOLUTION 5u
#define TERRAIN_MAX_RESOLUTION 1025u
#define TERRAIN_COARSE_BLOCK_SIZE 8u

typedef struct TerrainHeightfield {
    uint32_t resolution;     /**< Número de muestras por eje (impar, ej. 129, 257, 513, 1025). */
    float halfSize;          /**< Semiancho del dominio cuadrado [-halfSize, +halfSize]. */
    float originX;           /**< -halfSize */
    float originZ;           /**< -halfSize */
    float span;              /**< 2.0f * halfSize */
    float spacing;           /**< span / (resolution - 1) */
    float invSpacing;        /**< 1.0f / spacing */
    float minHeight;         /**< Altura mínima real del campo. */
    float maxHeight;         /**< Altura máxima real del campo. */
    bool isFlat;             /**< true si todas las alturas son exactamente 0. */

    float *heights;          /**< resolution * resolution alturas en metros/u.m. */
    Vector3 *smoothNormals;  /**< resolution * resolution normales suaves para sombreado. */
    float *slopeTan;         /**< resolution * resolution tangente de pendiente local |grad h|. */

    /* Pirámide gruesa de bloques 8x8 celdas para poda rápida de rayos y barridos. */
    uint32_t blockDim;
    float *blockMinY;        /**< blockDim * blockDim mínimos por bloque. */
    float *blockMaxY;        /**< blockDim * blockDim máximos por bloque. */
    size_t memoryBytes;
} TerrainHeightfield;

/** @brief Reserva buffers contiguos inicializados a altura cero (valida límites y desbordamiento). */
bool TerrainHeightfield_Init(TerrainHeightfield *tf, uint32_t resolution, float size);

/** @brief Libera todos los buffers propiedad del campo de alturas. */
void TerrainHeightfield_Free(TerrainHeightfield *tf);

/** @brief Recalcula normales suaves, pendientes, cotas globales y bloques gruesos min/max. */
bool TerrainHeightfield_RebuildDerived(TerrainHeightfield *tf);

/** @brief Verifica si (x, z) es finito y está dentro del dominio [-halfSize, +halfSize]. */
bool Terrain_InBounds(const TerrainHeightfield *tf, float x, float z);

/** @brief Muestrea altura y normal física exacta del triángulo autoritativo en (x, z). */
bool Terrain_SampleGround(const TerrainHeightfield *tf, float x, float z, SurfaceHit *out);

/** @brief Muestrea altura física exacta sin calcular normal completa. */
bool Terrain_SampleHeight(const TerrainHeightfield *tf, float x, float z, float *outHeight);

/** @brief Muestrea normal suave interpolada por baricéntricas del mismo triángulo. */
bool Terrain_SampleSmoothNormal(const TerrainHeightfield *tf, float x, float z, Vector3 *outNormal);

/** @brief Devuelve el ángulo de pendiente física en radianes [0, pi/2] en (x, z). */
bool Terrain_SampleSlopeAngle(const TerrainHeightfield *tf, float x, float z, float *outRadians);

/** @brief Calcula cotas min/max de altura de un rectángulo horizontal [minX,maxX] x [minZ,maxZ]. */
bool Terrain_QueryRegionBounds(const TerrainHeightfield *tf,
                               float minX, float minZ, float maxX, float maxZ,
                               float *outMinY, float *outMaxY);

/**
 * @brief Intersección exacta de segmento 3D [a, b] contra los triángulos autoritativos del terreno.
 *
 * Utiliza recorrido DDA 2D con poda jerárquica por bloques 8x8 y prueba Möller-Trumbore exacta.
 * @param endpointTolerance Margen paramétrico/vertical en extremos para permitir contacto legítimo
 *        sobre la superficie sin permitir atravesar el terreno.
 * @return true si el segmento intersecta el terreno estrictamente entre los extremos tolerados.
 */
bool Terrain_RaycastSegment(const TerrainHeightfield *tf, Vector3 a, Vector3 b,
                            float endpointTolerance, float *outT, Vector3 *outHit);

/** @brief Calcula huella canónica cuantizada FNV-1a de geometría y metadatos del terreno. */
uint64_t TerrainHeightfield_Fingerprint(const TerrainHeightfield *tf);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_TERRAIN_HEIGHTFIELD_H */
