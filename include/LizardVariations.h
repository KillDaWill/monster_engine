/**
 * @file LizardVariations.h
 * @brief Colección de las diez variantes morfológicas y cromáticas de lagartos.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef LIZARD_VARIATIONS_H
#define LIZARD_VARIATIONS_H

#include "CreatureVariation.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIZARD_VARIATIONS_COUNT 10

/**
 * @enum LizardVariantId
 * @brief Identificadores de las diez variantes de lagartos de la demo.
 */
typedef enum LizardVariantId {
    LIZARD_VARIANT_EMERALD_CLIMBER = 0,
    LIZARD_VARIANT_DESERT_HORNED,
    LIZARD_VARIANT_BLUE_RUNNER,
    LIZARD_VARIANT_FAT_TAILED_GECKO,
    LIZARD_VARIANT_VOLCANIC_SPINY,
    LIZARD_VARIANT_SAND_BURROWER,
    LIZARD_VARIANT_STONE_ARMORED,
    LIZARD_VARIANT_SAILBACK,
    LIZARD_VARIANT_JEWEL_CHAMELEON,
    LIZARD_VARIANT_AQUATIC_FIN_TAIL
} LizardVariantId;

CreatureVariation LizardVariations_EmeraldClimber(uint32_t seed);
CreatureVariation LizardVariations_DesertHorned(uint32_t seed);
CreatureVariation LizardVariations_BlueRunner(uint32_t seed);
CreatureVariation LizardVariations_FatTailedGecko(uint32_t seed);
CreatureVariation LizardVariations_VolcanicSpiny(uint32_t seed);
CreatureVariation LizardVariations_SandBurrower(uint32_t seed);
CreatureVariation LizardVariations_StoneArmored(uint32_t seed);
CreatureVariation LizardVariations_Sailback(uint32_t seed);
CreatureVariation LizardVariations_JewelChameleon(uint32_t seed);
CreatureVariation LizardVariations_AquaticFinTail(uint32_t seed);

/**
 * @brief Obtiene la cantidad total de variantes de lagartos disponibles.
 * @return 10.
 */
size_t LizardVariations_GetCount(void);

/**
 * @brief Genera la variación correspondiente al índice dado [0 - 9] con la semilla provista.
 * @param index Índice de variante (0 a 9).
 * @param seed Semilla para la generación determinista.
 * @return Estructura CreatureVariation configurada.
 */
CreatureVariation LizardVariations_Get(size_t index, uint32_t seed);

/**
 * @brief Retorna el nombre canónico de la variante para visualización.
 * @param index Índice de la variante.
 * @return Cadena con el nombre de la variante.
 */
const char* LizardVariations_GetName(size_t index);

#ifdef __cplusplus
}
#endif

#endif /* LIZARD_VARIATIONS_H */
