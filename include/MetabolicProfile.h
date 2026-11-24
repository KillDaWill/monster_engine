/**
 * @file MetabolicProfile.h
 * @brief Perfiles metabólicos guiados por datos y compilados a tablas O(1) por molécula.
 *
 * Define capacidades digestivas de un cuerpo (tasa de ingestión, capacidad estomacal,
 * reglas de procesamiento por molécula, eficiencia, rendimientos normalizados, costes,
 * eliminación de desechos y detoxificación) sin condicionales codificados por especie.
 *
 * Unidades explícitas:
 * - Masa digestiva / ingerida / desecho: unidades físicas de masa (u_m).
 * - Energía, hidratación, reserva de nutrientes, salud, malestar: reservas normalizadas [0, 1].
 * - Toxicidad: carga tóxica acumulada normalizada (>= 0).
 * - Rendimientos (energyYield, hydrationYield, nutrientYield): reserva normalizada por u_m procesada.
 */
#ifndef METABOLIC_PROFILE_H
#define METABOLIC_PROFILE_H

#include "MoleculeRegistry.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METABOLIC_PROFILE_SCHEMA_VERSION 1u
#define METABOLIC_PROFILE_MAX_COUNT 64u
#define METABOLIC_PROFILE_MAX_RULES 64u
#define METABOLIC_RULE_MAX_OUTPUTS 4u
#define METABOLIC_PROFILE_ID_MAX 48u
#define METABOLIC_PROFILE_NAME_MAX 64u
#define METABOLIC_PROFILE_INVALID_HANDLE UINT16_MAX

/** @brief Manejador compacto de un perfil metabólico compilado. */
typedef uint16_t MetabolicProfileHandle;

/** @brief Producto molecular explícito de una transformación metabólica (conserva masa). */
typedef struct MetabolicRuleOutput {
    MoleculeHandle molecule;
    float massRatio; /**< Fracción de masa de entrada transformada en esta molécula [0, 1]. */
} MetabolicRuleOutput;

/** @brief Regla compilada de procesamiento para un componente molecular dado. */
typedef struct MetabolicProcessingRule {
    MoleculeHandle molecule;
    float rate;                 /**< Masa máxima procesada por segundo simulado (u_m / s). */
    float efficiency;           /**< Fracción eficazmente procesada [0, 1]; el resto pasa a rechazo. */
    float energyYield;          /**< Energía normalizada obtenida por u_m procesada eficazmente. */
    float hydrationYield;       /**< Hidratación normalizada obtenida por u_m procesada eficazmente. */
    float nutrientYield;        /**< Nutrientes normalizados obtenidos por u_m procesada eficazmente. */
    float digestiveEnergyCost;  /**< Coste energético normalizado por u_m intentada de procesar. */
    float toxicityYield;        /**< Toxicidad generada por u_m procesada eficazmente. */
    float discomfortYield;      /**< Malestar digestivo por u_m procesada o rechazada. */
    float wasteFraction;        /**< Fracción de masa procesada eficazmente que deviene residuo [0, 1]. */
    MetabolicRuleOutput outputs[METABOLIC_RULE_MAX_OUTPUTS];
    uint8_t outputCount;
} MetabolicProcessingRule;

/** @brief Perfil metabólico compilado de un cuerpo de criatura. */
typedef struct MetabolicProfile {
    MetabolicProfileHandle handle;
    uint32_t schemaVersion;
    char id[METABOLIC_PROFILE_ID_MAX];
    char name[METABOLIC_PROFILE_NAME_MAX];

    float digestiveCapacity;              /**< Capacidad máxima estomacal (u_m). */
    float maximumIngestionRate;           /**< Tasa física máxima de ingestión (u_m / s). */
    float waterAbsorptionRate;            /**< Multiplicador/tasa base de absorción hídrica (>= 0). */
    float wasteEliminationRate;           /**< Tasa de eliminación de residuos (u_m / s). */
    float unprocessablePassageRate;       /**< Tránsito de moléculas sin regla hacia residuo (u_m / s). */
    float unprocessableDiscomfortFactor;  /**< Malestar por u_m de material no procesable. */
    float toxinSensitivity;               /**< Multiplicador de sensibilidad tóxica (>= 0). */
    float detoxificationRate;             /**< Tasa de depuración tóxica (tox / s). */
    float detoxificationEnergyCost;       /**< Coste energético normalizado por unidad detoxificada. */
    float toxicityThreshold;              /**< Umbral de toxicidad sobre el cual aparece daño a salud. */
    float toxicityDamageRate;             /**< Daño a salud / s por unidad de exceso tóxico. */
    float discomfortDecayRate;            /**< Disipación del malestar digestivo / s. */
    float nutrientMaintenanceRate;        /**< Consumo basal de reserva de nutrientes / s. */
    float nutrientRecoveryBonus;          /**< Recuperación adicional de salud con nutrientes adecuados. */
    float minimumReproductionNutrients;   /**< Reserva mínima de nutrientes requerida para reproducirse. */

    MetabolicProcessingRule rules[METABOLIC_PROFILE_MAX_RULES];
    uint16_t ruleCount;
    /** @brief Tabla compilada O(1): índice en rules[] por MoleculeHandle, o -1 si no existe regla. */
    int16_t ruleIndexByMolecule[MOLECULE_MAX_COUNT];
    uint64_t fingerprint;
    bool valid;
} MetabolicProfile;

/** @brief Registro congelable de perfiles metabólicos compilados. */
typedef struct MetabolicProfileRegistry {
    const MoleculeRegistry *molecules;
    MetabolicProfile items[METABOLIC_PROFILE_MAX_COUNT];
    uint16_t count;
    bool frozen;
    uint64_t fingerprint;
} MetabolicProfileRegistry;

/** @brief Inicializa valores sensatos por defecto para construir un perfil. */
void MetabolicProfile_InitDefault(MetabolicProfile *profile);

/** @brief Valida reglas, conservación de masa de transformaciones y compila tabla O(1). */
bool MetabolicProfile_Compile(MetabolicProfile *profile, const MoleculeRegistry *molecules,
                              char *error, size_t errorSize);

/** @brief Inicializa un registro vacío de perfiles metabólicos. */
void MetabolicProfileRegistry_Init(MetabolicProfileRegistry *reg, const MoleculeRegistry *molecules);

/** @brief Compila, valida y registra un perfil metabólico. */
bool MetabolicProfileRegistry_Register(MetabolicProfileRegistry *reg, const MetabolicProfile *profile,
                                       MetabolicProfileHandle *outHandle, char *error, size_t errorSize);

/** @brief Congela el registro de perfiles y fija su huella FNV-1a. */
uint64_t MetabolicProfileRegistry_Freeze(MetabolicProfileRegistry *reg);

/** @brief Resuelve un ID de perfil a su manejador compacto; METABOLIC_PROFILE_INVALID_HANDLE si falta. */
MetabolicProfileHandle MetabolicProfileRegistry_Find(const MetabolicProfileRegistry *reg, const char *id);

/** @brief Consulta O(1) del perfil compilado por manejador; NULL si es inválido. */
const MetabolicProfile *MetabolicProfileRegistry_Get(const MetabolicProfileRegistry *reg,
                                                     MetabolicProfileHandle handle);

/** @brief Huella canónica FNV-1a de un perfil individual. */
uint64_t MetabolicProfile_ComputeFingerprint(const MetabolicProfile *profile);

/** @brief Huella canónica FNV-1a del registro de perfiles. */
uint64_t MetabolicProfileRegistry_ComputeFingerprint(const MetabolicProfileRegistry *reg);

#ifdef __cplusplus
}
#endif

#endif /* METABOLIC_PROFILE_H */
