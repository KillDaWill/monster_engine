/** @file Surface.h
 * @brief Fenotipo de cobertura independiente de morfología, pose y materiales SDF.
 */
#ifndef MONSTER_SURFACE_H
#define MONSTER_SURFACE_H
#include "Color.h"
#include "Vector.h"
#include "PigmentPattern.h"
#include <stdint.h>

typedef enum IntegumentType {
    INTEGUMENT_SMOOTH_SKIN, INTEGUMENT_SCALES,
    INTEGUMENT_FUR, INTEGUMENT_FEATHERS, INTEGUMENT_PLATES
} IntegumentType;
typedef enum SurfaceRegion {
    SURFACE_REGION_UNKNOWN, SURFACE_REGION_HEAD, SURFACE_REGION_NECK,
    SURFACE_REGION_DORSAL_TRUNK, SURFACE_REGION_VENTRAL_TRUNK,
    SURFACE_REGION_FORELIMB, SURFACE_REGION_HINDLIMB,
    SURFACE_REGION_TAIL, SURFACE_REGION_DIGIT, SURFACE_REGION_ORAL,
    SURFACE_REGION_ORNAMENT,
    SURFACE_REGION_COUNT
} SurfaceRegion;
/** @brief Pigmentación macroscópica; semillas independientes de las escamas. */
typedef struct PigmentPhenotype {
    Color baseColor, secondaryColor, ventralColor;
    float dorsalDarkening, ventralLightening, patternStrength, patternScale, bands;
    uint32_t seed;
    PigmentLayer layers[SURFACE_MAX_PIGMENT_LAYERS];
    size_t layerCount;
} PigmentPhenotype;
/** @brief Tamaño relativo al dominio corporal; densidad = 1 / tamaño, sin control redundante. */
typedef struct ScalePhenotype {
    float size, aspectRatio, roundness, irregularity;
    float relief, edgeDepth, edgeWidth, keelStrength, roughness, microColorVariation;
    uint32_t seed;
} ScalePhenotype;
/** @brief Fenotipo de pelaje mamífero procedural; independiente del remallado SDF. */
typedef struct FurPhenotype {
    float length;           /**< Longitud base proporcional a la escala de la criatura. */
    float density;          /**< Densidad global de fibras aparentes. */
    float thickness;        /**< Grosor aparente de fibra. */
    float lay;              /**< Inclinación a lo largo del flujo [0=erecto, 1=acostado]. */
    float stiffness;        /**< Rigidez del pelo primario ante flexión. */
    float clumping;         /**< Agrupamiento local de fibras adyacentes. */
    float irregularity;     /**< Variación determinista de longitud y orientación. */
    float undercoat;        /**< Densidad / cantidad de subpelo fino y corto. */
    float undercoatLength;  /**< Longitud del subpelo relativa a la capa primaria (0.4-0.7). */
    float guardDensity;     /**< Densidad de pelos de cobertura (largos y esparcidos). */
    float roughness;        /**< Rugosidad óptica del pelaje. */
    float sheen;            /**< Brillo tangencial suave. */
    float rootDarkening;    /**< Factor de oscurecimiento en la raíz [0, 1]. */
    float tipLightening;    /**< Factor de aclarado en la punta [0, 1]. */
    uint32_t seed;          /**< Semilla determinista persistente del pelaje. */
} FurPhenotype;
typedef struct IntegumentPhenotype {
    IntegumentType type;
    float coverage;
    ScalePhenotype scales;
    PlatePhenotype plates;
    FurPhenotype fur;
} IntegumentPhenotype;
/** @brief Modificadores de tamaño, forma y respuesta para una región semántica. */
typedef struct SurfaceRegionProfile {
    float size, aspect, relief, keel;
    float roughness, irregularity, roundness, reserved;
} SurfaceRegionProfile;
/** @brief Modificadores biológicos regionales del pelaje, sin controles de renderer. */
typedef struct FurRegionProfile {
    float lengthMultiplier,densityMultiplier,thicknessMultiplier,layMultiplier;
    float undercoatMultiplier,guardMultiplier,clumpMultiplier,variationMultiplier;
} FurRegionProfile;
typedef struct SurfacePhenotype {
    PigmentPhenotype pigment;
    IntegumentPhenotype integument;
    SurfaceRegionProfile regions[SURFACE_REGION_COUNT];
    FurRegionProfile furRegions[SURFACE_REGION_COUNT];
} SurfacePhenotype;
/** @brief Coordenadas de reposo; nunca se recalculan al aplicar una pose. */
typedef struct SurfaceCoordinate {
    Vector3 position, normal;
    Vector3 flowDirection;      /**< Vector tangencial unitario de crecimiento en reposo. */
    float region, secondaryRegion, blend, ventral;
    float integumentMask;       /**< Cobertura tisular: 1=piel cubierta, 0=sin pelo (trufa, boca, etc.). */
} SurfaceCoordinate;
/** @brief Transporte explícito vec4: colores, pigmento, escamas, pelaje, perfiles y capas. */
#define SURFACE_RECIPE_GLOBAL_ROWS  8
#define SURFACE_RECIPE_FUR_ROWS     4
#define SURFACE_RECIPE_REGION_ROWS  (2 * SURFACE_REGION_COUNT)
#define SURFACE_RECIPE_PIGMENT_ROWS (3 * SURFACE_MAX_PIGMENT_LAYERS)
#define SURFACE_RECIPE_ROWS (SURFACE_RECIPE_GLOBAL_ROWS + SURFACE_RECIPE_FUR_ROWS + SURFACE_RECIPE_REGION_ROWS + SURFACE_RECIPE_PIGMENT_ROWS)
typedef struct SurfaceRecipe {
    float data[SURFACE_RECIPE_ROWS][4];
    uint32_t pigmentSeed, scaleSeed, furSeed;
} SurfaceRecipe;
/** @brief Devuelve piel lisa neutra y perfiles regionales identidad.
 * @return Fenotipo inicial válido. */
SurfacePhenotype SurfacePhenotype_Default(void);
/** @brief Limita valores e intercambia NaN/Inf por valores seguros.
 * @param surface Fenotipo a normalizar; NULL no hace nada. */
void SurfacePhenotype_Normalize(SurfacePhenotype* surface);
/** @brief Interpola controles; conserva semillas del origen durante la transición.
 * @param a Origen; NULL selecciona valores por defecto.
 * @param b Destino; NULL selecciona valores por defecto.
 * @param t Madurez normalizada.
 * @return Fenotipo válido interpolado. */
SurfacePhenotype SurfacePhenotype_Interpolate(const SurfacePhenotype* a,const SurfacePhenotype* b,float t);
/** @brief Compila exclusivamente apariencia, sin tocar SDF, mallas ni anatomía.
 * @param surface Fenotipo fuente; NULL selecciona piel neutra.
 * @return Receta empaquetada y normalizada. */
SurfaceRecipe SurfaceRecipe_Compile(const SurfacePhenotype* surface);
/** @brief Compila apariencia con escala física saneada. @param surface Fenotipo. @param unitScale Escala del dominio. @return Receta. */
SurfaceRecipe SurfaceRecipe_CompileScaled(const SurfacePhenotype* surface,float unitScale);
#endif
