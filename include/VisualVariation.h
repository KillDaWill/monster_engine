/**
 * @file VisualVariation.h
 * @brief Perfil visual determinista y cubetas morfológicas sin dependencia gráfica.
 */
#ifndef VISUAL_VARIATION_H
#define VISUAL_VARIATION_H

#include "CreatureGenome.h"
#include "CreatureRecipe.h"
#include "CreaturePhenotype.h"
#include "CreatureVariation.h"
#include "Surface.h"
#include "Color.h"
#include <stdint.h>
#include <stdbool.h>

#define VISUAL_MORPHOLOGY_BUCKETS 3
#define VISUAL_TIER_COUNT 3

/** @brief Nivel de detalle visual solicitado por el consumidor. */
typedef enum VisualTier {
    VISUAL_TIER_FAR = 0,
    VISUAL_TIER_MID = 1,
    VISUAL_TIER_NEAR = 2
} VisualTier;

/** @brief Atributos visuales estables derivados de un genoma individual. */
typedef struct CreatureVisualProfile {
    uint32_t bucket;      /**< 0..VISUAL_MORPHOLOGY_BUCKETS-1 */
    float renderScale;    /**< multiplicador de tamaño derivado de genome.size */
    float gaitCadence;    /**< multiplicador de frecuencia de zancada (genome.speed) */
    float postureLean;    /**< [-1,1] inclinación postural (genome.efficiency) */
    float headScanRate;   /**< multiplicador de escaneo cefálico (genome.perception) */
    uint32_t pigmentSeed; /**< semilla estable de superficies */
    Color primary;        /**< color base de capa */
    Color secondary;      /**< color secundario/patrón */
} CreatureVisualProfile;

/**
 * @brief Deriva un perfil visual puro y reproducible.
 * @param genome Genoma fuente; NULL selecciona el genoma neutro.
 * @param entitySeed Semilla estable de la identidad individual.
 * @return Perfil visual completamente inicializado.
 */
CreatureVisualProfile VisualVariation_Profile(const CreatureGenome* genome, uint32_t entitySeed);

/**
 * @brief Construye la variación semántica de una cubeta morfológica.
 * @param bucket Cubeta 0 robusta, 1 esbelta o 2 alta/alargada.
 * @param pigmentSeed Semilla estable para la aplicación de rasgos.
 * @return Variación sin estado global y con rasgos deterministas.
 */
CreatureVariation VisualVariation_BucketVariation(uint32_t bucket, uint32_t pigmentSeed);

/**
 * @brief Aplica una cubeta filtrando previamente los rasgos incompatibles.
 * @param baseRecipe Receta corporal inmutable.
 * @param basePhenotype Fenotipo de etapa inmutable.
 * @param bucket Cubeta morfológica solicitada.
 * @param pigmentSeed Semilla de la variación.
 * @param outRecipe Receta resultante.
 * @param outPhenotype Fenotipo resultante.
 * @return true si la variación fue aplicada y validada.
 */
bool VisualVariation_ApplyBucket(const CreatureRecipe* baseRecipe, const CreaturePhenotype* basePhenotype,
                                 uint32_t bucket, uint32_t pigmentSeed,
                                 CreatureRecipe* outRecipe, CreaturePhenotype* outPhenotype);

/**
 * @brief Genera la superficie adecuada a la familia de receta y al perfil.
 * @param recipe Receta cuya identidad selecciona el preset.
 * @param phenotype Fenotipo asociado; se exige para mantener la pareja válida.
 * @param profile Perfil visual individual.
 * @param maturity Madurez normalizada, limitada internamente a [0,1].
 * @param outSurface Superficie resultante.
 * @return true si la receta y todos los argumentos son válidos.
 */
bool VisualVariation_Surface(const CreatureRecipe* recipe, const CreaturePhenotype* phenotype,
                             const CreatureVisualProfile* profile, float maturity,
                             SurfacePhenotype* outSurface);

/**
 * @brief Calcula una clave FNV-1a estable para receta, etapa, cubeta y nivel.
 * @param recipeId Identidad persistente de receta.
 * @param stage Etapa ontogenética.
 * @param bucket Cubeta morfológica.
 * @param tier Nivel visual.
 * @return Clave de caché de 64 bits.
 */
uint64_t VisualVariation_CacheKey(uint32_t recipeId, uint32_t stage, uint32_t bucket, uint32_t tier);

#endif
