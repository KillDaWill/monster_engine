/** @file Surface.h
 * @brief Fenotipo de cobertura independiente de morfología, pose y materiales SDF.
 */
#ifndef MONSTER_SURFACE_H
#define MONSTER_SURFACE_H
#include "Color.h"
#include "Vector.h"
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
    SURFACE_REGION_COUNT
} SurfaceRegion;
/** @brief Pigmentación macroscópica; semillas independientes de las escamas. */
typedef struct PigmentPhenotype {
    Color baseColor, secondaryColor, ventralColor;
    float dorsalDarkening, ventralLightening, patternStrength, patternScale, bands;
    uint32_t seed;
} PigmentPhenotype;
/** @brief Tamaño relativo al dominio corporal; densidad = 1 / tamaño, sin control redundante. */
typedef struct ScalePhenotype {
    float size, aspectRatio, roundness, irregularity;
    float relief, edgeDepth, edgeWidth, keelStrength, roughness, microColorVariation;
    uint32_t seed;
} ScalePhenotype;
typedef struct IntegumentPhenotype {
    IntegumentType type;
    float coverage;
    ScalePhenotype scales;
} IntegumentPhenotype;
/** @brief Modificadores de tamaño, forma y respuesta para una región semántica. */
typedef struct SurfaceRegionProfile {
    float size, aspect, relief, keel;
    float roughness, irregularity, roundness, reserved;
} SurfaceRegionProfile;
typedef struct SurfacePhenotype {
    PigmentPhenotype pigment;
    IntegumentPhenotype integument;
    SurfaceRegionProfile regions[SURFACE_REGION_COUNT];
} SurfacePhenotype;
/** @brief Coordenadas de reposo; nunca se recalculan al aplicar una pose. */
typedef struct SurfaceCoordinate {
    Vector3 position, normal;
    float region, secondaryRegion, blend, ventral;
} SurfaceCoordinate;
/** @brief Transporte explícito vec4: colores, pigmento, escala, respuesta y perfiles. */
#define SURFACE_RECIPE_ROWS (8 + 2 * SURFACE_REGION_COUNT)
typedef struct SurfaceRecipe {
    float data[SURFACE_RECIPE_ROWS][4];
    uint32_t pigmentSeed, scaleSeed;
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
#endif
