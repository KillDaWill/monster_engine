/**
 * @file EvolutionArena.h
 * @brief Configuración y constructor autoritativo de Evolution Arena (visual y headless).
 *
 * Comparte exactamente el mismo núcleo de simulación, recetas anatómicas (Brumacerno y Velkar),
 * límites físicos, obstáculos, recursos y evolución neuronal entre ambos ejecutables.
 */
#ifndef EVOLUTION_ARENA_H
#define EVOLUTION_ARENA_H

#include "EcosystemWorld.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Modo de inicialización de cerebros fundadores. */
typedef enum EvolutionFounderMode {
    EVOLUTION_FOUNDERS_RANDOM = 0,   /**< Redes independientes aleatorias acotadas. */
    EVOLUTION_FOUNDERS_SEEDED = 1,   /**< Genoma base genérico diversificado por mutación. */
    EVOLUTION_FOUNDERS_IMPORTED = 2  /**< Fundadores cargados desde archivos *.brain.json. */
} EvolutionFounderMode;

/** @brief Parámetros reproducibles de un experimento de Evolution Arena. */
typedef struct EvolutionArenaConfig {
    uint32_t seed;
    float arenaSize;                  /**< Ancho y profundidad física de la arena (defecto 40.0). */
    unsigned brumacernos;             /**< Población inicial de Brumacerno (defecto 16). */
    unsigned velkares;                /**< Población inicial de Velkar (defecto 6). */
    unsigned plants;                  /**< Plantas regenerables iniciales (defecto 36). */
    unsigned ponds;                   /**< Estanques de agua accesibles (defecto 2). */
    unsigned obstacles;               /**< Obstáculos cilíndricos estáticos (defecto 5). */
    unsigned capacity;                /**< Capacidad máxima de población (defecto 128). */
    double step;                      /**< Paso fijo de simulación (1/60 s). */
    double day;                       /**< Duración de un día ecológico (120 s). */
    double days;                      /**< Días a simular en modo headless (defecto 30). */
    CreatureControllerType controller;/**< CREATURE_CONTROLLER_NEURAL o UTILITY. */
    EvolutionFounderMode founderMode; /**< random | seeded | imported. */
    NeuralMutationConfig mutation;    /**< Parámetros de herencia y mutación neuronal. */
    NeuralControllerConfig neural;    /**< Cadencia (5 Hz), compromiso y ruido opcional. */
    float founderDiversityRate;       /**< Probabilidad de diversificación inicial en modo seeded. */
    float founderDiversitySigma;      /**< Desviación de diversificación inicial en modo seeded. */
    bool disablePhysicalMutation;     /**< Congela mutación física para aislar evolución neural. */
    const char *founderBrainPath;     /**< Archivo *.brain.json común opcional. */
    const char *brumacernoBrainPath;  /**< Archivo *.brain.json específico para Brumacerno. */
    const char *velkarBrainPath;      /**< Archivo *.brain.json específico para Velkar. */
    const char *legacyScenario;       /**< NULL para Evolution Arena; "A".."E"/"reference" compat. */
} EvolutionArenaConfig;

/** @brief Nombre textual del modo de fundadores. */
const char *EvolutionFounderMode_Name(EvolutionFounderMode mode);

/** @brief Parsea "random", "seeded" o "imported". */
bool EvolutionFounderMode_FromName(const char *name, EvolutionFounderMode *out);

/** @brief Configuración por defecto de Evolution Arena (40x40, 16 Brumacernos, 6 Velkares, 5 Hz). */
EvolutionArenaConfig EvolutionArenaConfig_Default(void);

/** @brief Aplica un escenario ecológico clásico (A..E o reference) para validación de regresión. */
bool EvolutionArenaConfig_ApplyLegacyScenario(EvolutionArenaConfig *cfg, const char *name);

/** @brief Valida configuración antes de construir el mundo. */
bool EvolutionArenaConfig_Validate(const EvolutionArenaConfig *cfg);

/**
 * @brief Construye la arena completa compartida por demo_evolution_arena y headless.
 *
 * Resuelve los IDs reales de Brumacerno y Velkar mediante el registro de recetas.
 * Ante error libera el mundo y escribe un diagnóstico legible en @p error.
 */
bool EvolutionArena_Create(EcosystemWorld *world, const EvolutionArenaConfig *cfg,
                           char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_ARENA_H */
