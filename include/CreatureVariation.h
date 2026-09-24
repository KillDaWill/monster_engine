/**
 * @file CreatureVariation.h
 * @brief Capa reutilizable de variación morfológica y rasgos fenotípicos.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_VARIATION_H
#define CREATURE_VARIATION_H

#include "CreatureRecipe.h"
#include "AttachmentPath.h"
#include "CreaturePhenotype.h"
#include "Color.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CREATURE_MAX_TRAITS 32

/**
 * @enum CreatureTraitType
 * @brief Vocabulario semántico de rasgos morfológicos independientes de especie.
 */
typedef enum CreatureTraitType {
    /* Rasgos axiales y proporciones corporales */
    CREATURE_TRAIT_BODY_SLENDER = 0,
    CREATURE_TRAIT_BODY_ROBUST,
    CREATURE_TRAIT_BODY_COMPACT,
    CREATURE_TRAIT_BODY_ELONGATED,
    CREATURE_TRAIT_BODY_FLATTENED,
    CREATURE_TRAIT_BODY_DEEP,
    CREATURE_TRAIT_BODY_BROAD_LOW,

    /* Rasgos cefálicos y mandibulares */
    CREATURE_TRAIT_HEAD_SMALL,
    CREATURE_TRAIT_HEAD_LARGE,
    CREATURE_TRAIT_HEAD_LONG_SNOUT,
    CREATURE_TRAIT_HEAD_SHORT_SNOUT,
    CREATURE_TRAIT_HEAD_HEAVY_JAW,
    CREATURE_TRAIT_HEAD_WEDGE,
    CREATURE_TRAIT_HEAD_CASQUE,
    CREATURE_TRAIT_HEAD_BROAD_TRIANGULAR,
    CREATURE_TRAIT_HEAD_FORM,
    CREATURE_TRAIT_EYE_LAYOUT,

    /* Rasgos oculares */
    CREATURE_TRAIT_EYES_SMALL,
    CREATURE_TRAIT_EYES_LARGE,
    CREATURE_TRAIT_EYES_PROTRUDING,
    CREATURE_TRAIT_PUPIL_ROUND,
    CREATURE_TRAIT_PUPIL_VERTICAL,
    CREATURE_TRAIT_PUPIL_HORIZONTAL,
    CREATURE_TRAIT_PUPIL_DIAMOND,

    /* Rasgos de extremidades y autopodios */
    CREATURE_TRAIT_LIMBS_LONG,
    CREATURE_TRAIT_LIMBS_SHORT,
    CREATURE_TRAIT_LIMBS_ROBUST,
    CREATURE_TRAIT_LIMBS_ARBOREAL,
    CREATURE_TRAIT_LIMBS_CURSORIAL,
    CREATURE_TRAIT_LIMBS_DIGGING,
    CREATURE_TRAIT_FEET_LARGE,
    CREATURE_TRAIT_LIMBS_GRACILE,

    /* Arquetipos y morfología caudal */
    CREATURE_TRAIT_TAIL_TAPERED,
    CREATURE_TRAIT_TAIL_WHIPPED,
    CREATURE_TRAIT_TAIL_HEAVY,
    CREATURE_TRAIT_TAIL_PREHENSILE,
    CREATURE_TRAIT_TAIL_FINNED,

    /* Tegumento y microestructura superficial */
    CREATURE_TRAIT_FINE_SCALES,
    CREATURE_TRAIT_HEAVY_SCALES,
    CREATURE_TRAIT_PLATES,

    /* Ornamentos y estructuras dérmicas distribuidas */
    CREATURE_TRAIT_HORNS,
    CREATURE_TRAIT_DORSAL_SPINES,
    CREATURE_TRAIT_DORSAL_SAIL,
    CREATURE_TRAIT_CRANIAL_CROWN,
    CREATURE_TRAIT_LATERAL_SPINES,

    /* Coloración y capas de pigmento */
    CREATURE_TRAIT_COLOR_PALETTE,
    CREATURE_TRAIT_PATTERN_LAYER,

    CREATURE_TRAIT_ORNAMENT_FIELD,
    CREATURE_TRAIT_HEAD_DEEP,

    CREATURE_TRAIT_HEAD_STREAMLINED, /**< Cabeza baja con volumen y rostro afinado. */
    CREATURE_TRAIT_FEET_NARROW, /**< Autopodios estrechos con dedos finos. */

    CREATURE_TRAIT_NECK_CONTINUOUS, /**< Cuello de sección continua con la cintura anterior. */

    CREATURE_TRAIT_LIMBS_RETRACTED, /**< Rodillas y tobillos retrasados, con pies orientados hacia fuera. */

    CREATURE_TRAIT_EYE_APERTURE, /**< Compresión vertical orbital; paramA en [0,1]. */
    CREATURE_TRAIT_EYE_PALETTE, /**< Color del iris y de la esclerótica. */
    CREATURE_TRAIT_TAIL_CURVED, /**< Curvatura distal conservando longitud de arco. */
    CREATURE_TRAIT_COUNT
} CreatureTraitType;

/**
 * @struct CreatureTrait
 * @brief Instancia parametrizada de un rasgo morfológico o superficial.
 */
typedef enum CreatureLimbTarget { CREATURE_LIMBS_ALL, CREATURE_LIMBS_FORE, CREATURE_LIMBS_HIND } CreatureLimbTarget;
typedef struct CreatureTrait {
    CreatureTraitType type; /**< Tipo de rasgo semántico */
    CreatureLimbTarget limbTarget; /**< Selección funcional de miembros. */
    float strength;         /**< Intensidad normalizada [0.0 - 1.0] */
    float paramA;           /**< Parámetro continuo auxiliar */
    float paramB;           /**< Parámetro continuo auxiliar */
    Color colorA;           /**< Color primario específico del rasgo */
    Color colorB;           /**< Color secundario específico del rasgo */
    OrnamentField field;   /**< Distribución para ORNAMENT_FIELD. */
    CreatureModuleKind hostKind; /**< Clase del anfitrión del campo: axial o cola. */
    unsigned hostIndex;    /**< Índice del anfitrión dentro de su clase. */
} CreatureTrait;

/**
 * @struct CreatureVariation
 * @brief Colección coherente de rasgos semánticos y semilla pseudoaleatoria.
 */
typedef struct CreatureVariation {
    const char* name;
    uint32_t seed;
    CreatureTrait traits[CREATURE_MAX_TRAITS];
    size_t traitCount;
} CreatureVariation;

/**
 * @struct CreatureVariant
 * @brief Resultado concreto de aplicar una variación sobre una receta y fenotipo base.
 */
typedef struct CreatureVariant {
    CreatureRecipe recipe;
    CreaturePhenotype phenotype;
} CreatureVariant;

/**
 * @brief Inicializa una variación vacía con nombre y semilla.
 * @param name Nombre identificativo de la variación.
 * @param seed Semilla para la generación determinista de dispersiones.
 * @return Estructura de variación inicializada.
 */
CreatureVariation CreatureVariation_Create(const char* name, uint32_t seed);

/**
 * @brief Añade un rasgo morfológico a la variación.
 * @param variation Puntero a la variación destino.
 * @param trait Rasgo a incorporar.
 * @return true si se añadió con éxito; false si se alcanzó el límite de rasgos.
 */
bool CreatureVariation_AddTrait(CreatureVariation* variation, CreatureTrait trait);

/**
 * @brief Aplica una variación de forma pura e inmutable sobre una receta y fenotipo base.
 *
 * La receta base y el fenotipo base nunca se mutan. La función evalúa la compatibilidad
 * de cada rasgo con el plan corporal, calcula transformaciones correlacionadas con rangos
 * deterministas según la semilla y genera la receta y fenotipo resultantes en @p out.
 *
 * @param baseRecipe Receta base inmutable.
 * @param basePhenotype Fenotipo base inmutable (ej. adulto de la receta).
 * @param variation Definición de rasgos y semilla a aplicar.
 * @param out Puntero a la estructura donde escribir la variante resultante.
 * @return true si la variación fue aplicada exitosamente y la anatomía es válida.
 */
bool CreatureVariation_Apply(
    const CreatureRecipe* baseRecipe,
    const CreaturePhenotype* basePhenotype,
    const CreatureVariation* variation,
    CreatureVariant* out);

/**
 * @brief Comprueba si un rasgo es compatible con un fenotipo y plan corporal dados.
 * @param trait Rasgo a evaluar.
 * @param recipe Receta a comprobar.
 * @param phenotype Fenotipo a comprobar.
 * @return true si el rasgo puede aplicarse de forma segura.
 */
bool CreatureVariation_IsTraitCompatible(
    const CreatureTrait* trait,
    const CreatureRecipe* recipe,
    const CreaturePhenotype* phenotype);

/**
 * @brief Calcula una huella digital determinista (FNV-1a 64-bit) de un fenotipo.
 * @param phenotype Fenotipo a procesar.
 * @return Valor hash representativo de todas las propiedades numéricas.
 */
uint64_t CreaturePhenotype_Fingerprint(const CreaturePhenotype* phenotype);

#ifdef __cplusplus
}
#endif

#endif /* CREATURE_VARIATION_H */
