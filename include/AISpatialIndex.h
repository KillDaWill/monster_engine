/**
 * @file AISpatialIndex.h
 * @brief Índice espacial 2D (XZ) uniforme con capas separadas y tabla hash O(1) de IDs.
 *
 * Diseñado para consultas `const` seguras desde múltiples hilos sin asignaciones dinámicas
 * por consulta. Cada objeto pertenece exactamente a la celda de su centro y las consultas
 * amplían conservadoramente su ventana por el radio máximo de cada capa, evitando
 * duplicados, deduplicación mutable y objetos perdidos en bordes de celda.
 */
#ifndef AI_SPATIAL_INDEX_H
#define AI_SPATIAL_INDEX_H

#include "Vector.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AIWorldObject;

typedef enum AISpatialLayer {
    AI_SPATIAL_LAYER_STATIC = 0,     /**< Obstáculos estáticos y estanques de agua. */
    AI_SPATIAL_LAYER_CREATURES = 1,  /**< Criaturas vivas móviles. */
    AI_SPATIAL_LAYER_RESOURCES = 2,  /**< Plantas y recursos alimenticios estáticos. */
    AI_SPATIAL_LAYER_CARCASSES = 3,  /**< Cadáveres temporales y carne dinámica. */
    AI_SPATIAL_LAYER_COUNT = 4
} AISpatialLayer;

#define AI_SPATIAL_MASK_STATIC    (1u << AI_SPATIAL_LAYER_STATIC)
#define AI_SPATIAL_MASK_CREATURES (1u << AI_SPATIAL_LAYER_CREATURES)
#define AI_SPATIAL_MASK_RESOURCES (1u << AI_SPATIAL_LAYER_RESOURCES)
#define AI_SPATIAL_MASK_CARCASSES (1u << AI_SPATIAL_LAYER_CARCASSES)
#define AI_SPATIAL_MASK_ALL       ((1u << AI_SPATIAL_LAYER_COUNT) - 1u)
#define AI_SPATIAL_MASK_DYNAMIC   (AI_SPATIAL_MASK_CREATURES | AI_SPATIAL_MASK_RESOURCES | AI_SPATIAL_MASK_CARCASSES)

typedef struct AISpatialNode {
    int prev;
    int next;
    int cellIndex;
    uint8_t layer;
    bool inGrid;
} AISpatialNode;

typedef struct AISpatialGridLayer {
    int *cellHeads;
    float maxRadius;
    unsigned count;
} AISpatialGridLayer;

typedef struct AISpatialIndex {
    unsigned capacity;
    float halfSize;
    float cellSize;
    float invCellSize;
    int gridDim;
    unsigned cellCount;

    AISpatialGridLayer layers[AI_SPATIAL_LAYER_COUNT];
    AISpatialNode *nodes;

    /* Tabla hash abierta con borrado backward-shift: O(1) sin lápidas. */
    uint32_t *hashKeys;
    uint32_t *hashSlots;
    uint32_t hashMask;
    uint32_t hashCapacity;

    /* Listas compactas para asignación O(1) e iteración densa. */
    uint32_t *freeSlots;
    unsigned freeCount;
    uint32_t *activeSlots;
    unsigned activeCount;
    uint32_t *slotToActive;
} AISpatialIndex;

/** @brief Crea e inicializa un índice espacial para la arena [-halfSize, halfSize]^2. */
AISpatialIndex *AISpatialIndex_Create(unsigned capacity, float halfSize, float cellSize);

/** @brief Libera toda la memoria del índice espacial. */
void AISpatialIndex_Destroy(AISpatialIndex *idx);

/** @brief Reconfigura el tamaño de celda reconstruyendo la pertenencia desde @p objects. */
bool AISpatialIndex_SetCellSize(AISpatialIndex *idx, float cellSize,
                                const struct AIWorldObject *objects);

/** @brief Clasifica un AIWorldObject en su capa espacial correspondiente. */
AISpatialLayer AISpatialIndex_LayerForObject(const struct AIWorldObject *obj);

/** @brief Reserva una ranura libre O(1); devuelve false si está lleno. */
bool AISpatialIndex_AllocSlot(AISpatialIndex *idx, uint32_t *outSlot);

/** @brief Registra un objeto activo en la tabla hash, lista activa y celda espacial. */
void AISpatialIndex_Insert(AISpatialIndex *idx, uint32_t slot, uint32_t id,
                           AISpatialLayer layer, Vector3 pos, float radius);

/** @brief Actualiza posición/radio de un objeto en O(1) (no-op si la celda no cambia). */
void AISpatialIndex_Update(AISpatialIndex *idx, uint32_t slot,
                           AISpatialLayer layer, Vector3 pos, float radius);

/** @brief Retira un objeto de la tabla hash, lista activa y celda espacial, devolviendo ranura. */
void AISpatialIndex_Remove(AISpatialIndex *idx, uint32_t slot, uint32_t id);

/** @brief Consulta O(1) de ranura por ID estable; devuelve -1 si no existe. */
int AISpatialIndex_Lookup(const AISpatialIndex *idx, uint32_t id);

/** @brief Consulta conservadora por radio en XZ (const, segura entre hilos, cero heap). */
unsigned AISpatialIndex_QueryRadius(const AISpatialIndex *idx,
                                    uint32_t layerMask, Vector3 center, float radius,
                                    uint32_t *outSlots, unsigned capacity, bool *overflowed);

/** @brief Consulta conservadora por AABB en XZ (const, segura entre hilos, cero heap). */
unsigned AISpatialIndex_QueryAABB(const AISpatialIndex *idx,
                                  uint32_t layerMask,
                                  float minX, float minZ, float maxX, float maxZ,
                                  uint32_t *outSlots, unsigned capacity, bool *overflowed);

/** @brief Consulta conservadora por segmento barrido en XZ (const, segura entre hilos). */
unsigned AISpatialIndex_QuerySegment(const AISpatialIndex *idx,
                                     uint32_t layerMask,
                                     Vector3 a, Vector3 b, float radius,
                                     uint32_t *outSlots, unsigned capacity, bool *overflowed);

/** @brief Ordena deterministamente las ranuras candidatas por ID ascendente del objeto. */
void AISpatialIndex_SortSlotsById(const struct AIWorldObject *objects,
                                  uint32_t *slots, unsigned count);

#ifdef __cplusplus
}
#endif

#endif /* AI_SPATIAL_INDEX_H */
