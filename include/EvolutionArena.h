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

/** @brief Presets escalables de arena (preservando la arena pequeña clásica). */
typedef enum EvolutionArenaPreset {
    EVOLUTION_PRESET_SMALL = 0,   /**< 22 criaturas, arena 40x40, cap 128, 36 plantas, 2 estanques. */
    EVOLUTION_PRESET_MEDIUM = 1,  /**< 100 criaturas, arena 90x90, cap 256, 160 plantas, 8 estanques. */
    EVOLUTION_PRESET_LARGE = 2,   /**< 500 criaturas, arena 200x200, cap 1024, 800 plantas, 40 estanques. */
    EVOLUTION_PRESET_STRESS = 3   /**< 1000 criaturas, arena 280x280, cap 2048, 1600 plantas, 80 estanques. */
} EvolutionArenaPreset;

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
    unsigned workers;                 /**< Hilos de simulación (0=auto, 1=serial, 2..16). */
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
    const char *presetName;           /**< "small" | "medium" | "large" | "stress" | NULL. */
    EcosystemMetabolismMode metabolismMode; /**< LEGACY o MOLECULAR. */
    const char *metabolismDir;        /**< Directorio de moléculas/sustancias/perfiles (defecto "metabolism"). */
    bool includeMixedSubstances;      /**< Añade fruta, carne, plantas tóxicas y agua contaminada en modo molecular. */
} EvolutionArenaConfig;

/** @brief Nombre textual del modo de fundadores. */
const char *EvolutionFounderMode_Name(EvolutionFounderMode mode);

/** @brief Parsea "random", "seeded" o "imported". */
bool EvolutionFounderMode_FromName(const char *name, EvolutionFounderMode *out);

/** @brief Nombre textual del preset de arena. */
const char *EvolutionArenaPreset_Name(EvolutionArenaPreset preset);

/** @brief Aplica uno de los presets ("small", "medium", "large", "stress"). */
bool EvolutionArenaConfig_ApplyPreset(EvolutionArenaConfig *cfg, const char *name);

/** @brief Configuración por defecto de Evolution Arena (compatibilidad v1). */
EvolutionArenaConfig EvolutionArenaConfig_Default(void);

/** @brief Configuración por defecto en modo molecular con cerebros genéricos v2. */
EvolutionArenaConfig EvolutionArenaConfig_DefaultMolecular(void);

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
