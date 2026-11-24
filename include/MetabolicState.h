/**
 * @file MetabolicState.h
 * @brief Estado digestivo y metabólico acotado en ejecución por individuo.
 *
 * Almacena de forma dispersa únicamente los componentes moleculares presentes en el
 * tracto digestivo (sin asignaciones dinámicas ni escaneo completo cuando está vacío).
 * Cada criatura posee su propia instancia; nunca se serializa dentro de @ref NeuralGenome.
 */
#ifndef METABOLIC_STATE_H
#define METABOLIC_STATE_H

#include "MoleculeRegistry.h"
#include "SubstanceRegistry.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METABOLIC_STATE_MAX_ACTIVE 16u

/** @brief Cantidad de una molécula concreta actualmente en digestión. */
typedef struct MetabolicDigestiveEntry {
    MoleculeHandle molecule;
    float mass; /**< Masa física presente (u_m >= 0). */
} MetabolicDigestiveEntry;

/** @brief Estado metabólico individual independiente del cerebro neuronal. */
typedef struct MetabolicState {
    bool initialized;
    float digestiveMass;          /**< Suma de masa de moléculas activas en el tracto (u_m). */
    float digestiveCapacity;      /**< Capacidad máxima física del cuerpo (u_m). */
    float wasteMass;              /**< Residuo acumulado pendiente de eliminación (u_m). */

    MetabolicDigestiveEntry entries[METABOLIC_STATE_MAX_ACTIVE];
    uint16_t activeCount;

    float nutrientReserve;        /**< Reserva normalizada de micronutrientes/construcción [0, 1]. */
    float toxicity;               /**< Carga tóxica circulante (>= 0). */
    float digestiveDiscomfort;    /**< Malestar digestivo acumulado [0, 1]. */

    SubstanceHandle lastIngestedSubstance;
    float lastIngestedMass;
    double lastIngestionTime;

    /* Deltas acumulados del último paso / ventana para inspección y retroalimentación neural. */
    float recentIngestedMass;
    float recentEnergyAbsorbed;
    float recentHydrationAbsorbed;
    float recentNutrientsAbsorbed;
    float recentToxicityDelta;
    float recentMetabolicDamage;
    float recentDigestiveEnergyCost;
    float recentWasteEliminated;
    float recentRejectedMass;

    /* Acumuladores de vida del individuo (sin heap). */
    double totalIngestedMass;
    double totalProcessedMass;
    double totalAbsorbedMass;
    double totalRejectedMass;
    double totalWasteEliminated;
    double totalEnergyGenerated;
    double totalHydrationGenerated;
    double totalNutrientsAbsorbed;
    double totalDigestiveEnergySpent;
    double totalToxinExposure;
    double totalDetoxified;
    double totalMetabolicDamage;
} MetabolicState;

/** @brief Inicializa un estado digestivo limpio (recién nacido o reutilización de ranura). */
void MetabolicState_Init(MetabolicState *state, float digestiveCapacity, float initialNutrients);

/** @brief Reinicia por completo el estado digestivo sin afectar otras estructuras. */
void MetabolicState_Reset(MetabolicState *state);

/** @brief Capacidad libre restante del tracto digestivo (u_m >= 0). */
float MetabolicState_RemainingCapacity(const MetabolicState *state);

/** @brief Fracción de llenado digestivo [0, 1] (masa activa + residuo / capacidad). */
float MetabolicState_Fullness(const MetabolicState *state);

/** @brief Añade masa de una molécula al almacenamiento disperso (mantiene orden compacto). */
bool MetabolicState_AddMolecule(MetabolicState *state, MoleculeHandle molecule, float mass);

/** @brief Consulta masa presente de una molécula concreta; 0 si no está presente. */
float MetabolicState_GetMoleculeMass(const MetabolicState *state, MoleculeHandle molecule);

/** @brief Recalcula `digestiveMass` y purga entradas agotadas (<= 1e-7). */
void MetabolicState_Compact(MetabolicState *state);

#ifdef __cplusplus
}
#endif

#endif /* METABOLIC_STATE_H */
