/**
 * @file WorldHydrology.h
 * @brief Hidrología estática basada en Priority-Flood: drenaje acíclico, lagos en cuencas,
 *        ríos excavados, superficies de agua físicas y sitios accesibles de orilla.
 */
#ifndef MONSTER_WORLD_HYDROLOGY_H
#define MONSTER_WORLD_HYDROLOGY_H

#include "TerrainHeightfield.h"
#include "WorldIO.h"
#include "AABB.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_MAX_WATER_BODIES 64u
#define WORLD_MAX_SHORE_SITES 512u
#define WORLD_WATER_DEPTH_EPSILON 0.01f
#define WORLD_WATER_PATCH_VERTICES 5u

typedef enum WaterBodyType {
    WATER_BODY_NONE  = 0,
    WATER_BODY_LAKE  = 1,
    WATER_BODY_RIVER = 2
} WaterBodyType;

typedef struct WaterSample {
    bool wet;                 /**< true sólo si el punto tiene agua física real (depth > 0). */
    uint16_t waterBodyId;     /**< ID estable del cuerpo de agua (1..N), 0 si seco. */
    WaterBodyType type;       /**< Lago o río. */
    float surfaceY;           /**< Elevación real de la lámina de agua en (x, z). */
    float groundY;            /**< Elevación real del lecho en (x, z). */
    float depth;              /**< max(0, surfaceY - groundY). */
    Vector3 flow;             /**< Vector horizontal de flujo (XZ), magnitud proporcional a caudal. */
} WaterSample;

typedef struct WorldWaterBody {
    uint16_t id;              /**< Identificador estático estable (1-based). */
    WaterBodyType type;
    char substanceId[48];
    WorldReservoirPolicy reservoirPolicy;
    float initialVolume;      /**< Volumen/masa inicial si FINITE_FIXED_VOLUME. */
    float surfaceY;           /**< Nivel horizontal de referencia (exacto en lagos, medio en ríos). */
    float minSurfaceY;
    float maxSurfaceY;
    float maxDepth;
    uint32_t wetCellCount;
    AABB3D bounds;
    Vector3 centroid;
} WorldWaterBody;

typedef struct WorldShoreSite {
    uint32_t siteId;          /**< Identificador estable de la estación de orilla (1-based). */
    uint16_t waterBodyId;     /**< Cuerpo de agua propietario (para libro molecular compartido). */
    Vector3 wetContactPoint;  /**< Punto real sobre la lámina húmeda alcanzable por la boca. */
    Vector3 bankApproachPoint;/**< Punto validado en tierra/vado seguro para posicionar el cuerpo. */
    Vector3 bankNormal;       /**< Normal horizontal saliente desde el agua hacia tierra firme. */
    float waterSurfaceY;      /**< Elevación exacta del agua en el punto de contacto. */
    float bankSlopeRadians;   /**< Pendiente física del terreno en bankApproachPoint. */
    float maxSafeFootprint;   /**< Radio corporal máximo validado en esta estación. */
    bool accessible;
} WorldShoreSite;

typedef struct WorldHydrology {
    uint32_t resolution;
    bool enabled;

    /* Campos por muestra del terreno (resolution * resolution). */
    float *waterSurfaceY;     /**< Cota de lámina donde hay dominio; v1 sólo en muestras húmedas. */
    uint16_t *surfaceBodyId;  /**< Dominio v2, incluye cotas firmadas en muestras secas de ribera. */
    float *waterDepth;        /**< Profundidad física >= 0 (0 si seco). */
    uint16_t *waterBodyId;    /**< 0 = seco, 1..bodyCount = cuerpo propietario. */
    float *flowX;             /**< Componente X del flujo superficial. */
    float *flowZ;             /**< Componente Z del flujo superficial. */
    float *drainageAccum;     /**< Acumulación de drenaje normalizada/área de cuenca. */

    WorldWaterBody *bodies;
    uint32_t bodyCount;
    uint32_t lakeCount;
    uint32_t riverCount;

    WorldShoreSite *shoreSites;
    uint32_t shoreSiteCount;

    /* Estadísticas diagnósticas */
    uint32_t totalWetCells;
    float wetAreaFraction;
    float shorelineLength;   /**< Longitud XZ del contorno v2, incluidos límites de dominio. */
    uint32_t riverSegmentCount, riverJunctionCount;
    Vector3 majorJunction;    /**< Confluencia v2 de mayor acumulación, si existe. */
    bool drainageAcyclic;
    size_t memoryBytes;
} WorldHydrology;

/** @brief Plano local de un cuerpo sobre uno de los triángulos del campo de alturas. */
typedef struct WorldWaterPatch {
    uint16_t bodyId;
    uint32_t grid[3];
    float surface[3];
    float depth[3];
    float domain[3];
    float flowX[3], flowZ[3];
} WorldWaterPatch;

typedef struct WorldWaterVertex {
    Vector3 position;
    float depth;
    float flowX, flowZ;
    uint8_t boundary; /**< Bits compartidos por aristas de contorno: 1 = dominio, 2 = profundidad. */
} WorldWaterVertex;

/** @brief Enumera los planos candidatos v2 del triángulo en orden CCW desde +Y. */
unsigned WorldHydrology_GetWaterPatches(const WorldHydrology *hydro, const TerrainHeightfield *tf,
                                        uint32_t x, uint32_t z, unsigned triangle,
                                        WorldWaterPatch out[3]);
/** @brief Recorta el triángulo exactamente contra dominio >= 0 y profundidad > epsilon. */
unsigned WorldHydrology_ClipWaterPatch(const TerrainHeightfield *tf, const WorldWaterPatch *patch,
                                       WorldWaterVertex out[WORLD_WATER_PATCH_VERTICES]);

/** @brief Construye la hidrología completa sobre @p tf (modifica alturas al excavar cauces y recalcula derivadas). */
bool WorldHydrology_Generate(WorldHydrology *hydro, TerrainHeightfield *tf,
                             const WorldRecipe *recipe, uint32_t hydroSeed);

/** @brief Observador opcional para datos temporales de generación.
 * Cada campo es prestado durante la llamada y contiene resolution² muestras.
 * No se retiene almacenamiento diagnóstico en WorldStaticData. */
typedef void (*WorldHydrologyTraceFn)(void *context, const char *name, const void *samples,
                                      size_t count, size_t sampleBytes);

/** @brief Generación idéntica a WorldHydrology_Generate con observador síncrono. */
bool WorldHydrology_GenerateTraced(WorldHydrology *hydro, TerrainHeightfield *tf,
                                  const WorldRecipe *recipe, uint32_t hydroSeed,
                                  WorldHydrologyTraceFn trace, void *context);

/** @brief Libera todos los buffers propiedad de la hidrología. */
void WorldHydrology_Free(WorldHydrology *hydro);

/** @brief Muestrea agua física en (x, z); garantiza wet=false en suelo seco o fuera de dominio. */
bool WorldHydrology_SampleWater(const WorldHydrology *hydro, const TerrainHeightfield *tf,
                                float x, float z, WaterSample *out);

/** @brief Primer impacto de un segmento con una superficie de agua canónica v2. */
bool WorldHydrology_RaycastWater(const WorldHydrology *hydro, const TerrainHeightfield *tf,
                                 Vector3 origin, Vector3 end, Vector3 *hit, uint16_t *bodyId);

/** @brief Busca un cuerpo de agua por su ID estable (1-based). */
const WorldWaterBody *WorldHydrology_GetBody(const WorldHydrology *hydro, uint16_t waterBodyId);

/** @brief Busca el sitio de orilla más cercano a @p pos dentro de @p maxRadius (opcionalmente filtrado por cuerpo). */
const WorldShoreSite *WorldHydrology_FindNearestShore(const WorldHydrology *hydro, Vector3 pos,
                                                      float maxRadius, uint16_t filterBodyId);

/** @brief Extrae/valida sitios de orilla accesibles teniendo en cuenta obstáculos rocosos opcionales. */
bool WorldHydrology_RebuildShoreSites(WorldHydrology *hydro, const TerrainHeightfield *tf,
                                      const WorldRecipe *recipe,
                                      bool (*extraBlockedFn)(const void *ctx, Vector3 pos, float radius),
                                      const void *extraBlockedCtx);

/** @brief Huella canónica cuantizada de cuerpos de agua, profundidades y orillas. */
uint64_t WorldHydrology_Fingerprint(const WorldHydrology *hydro);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_HYDROLOGY_H */
