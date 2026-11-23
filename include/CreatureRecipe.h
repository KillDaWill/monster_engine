/**
 * @file CreatureRecipe.h
 * @brief Composición modular desacoplada y etapas de desarrollo para criaturas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_RECIPE_H
#define CREATURE_RECIPE_H

#include "CreaturePhenotype.h"
#include "Attachment.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t CreatureRecipeId;

typedef enum CreatureModuleKind {
    CREATURE_MODULE_AXIAL = 0,
    CREATURE_MODULE_HEAD,
    CREATURE_MODULE_LIMB,
    CREATURE_MODULE_TAIL,
    CREATURE_MODULE_ORNAMENT,
    CREATURE_MODULE_FLEXIBLE_APPENDAGE,
    CREATURE_MODULE_FIN /**< Aleta hidrodinámica genérica (dorsal, anal, pectoral, pélvica o serie de pínulas). */
} CreatureModuleKind;

/** Referencia estable a una estación anatómica local de un módulo. */
typedef struct AnatomyRef {
    uint32_t moduleInstanceId;
    uint16_t localNodeId;
} AnatomyRef;

/** Instancia modular en el plan corporal de una criatura. */
typedef struct CreatureModuleInstance {
    uint32_t instanceId;
    CreatureModuleKind kind;
    AttachmentRef attachment;
    unsigned phenotypeIndex;
    bool pathAttached; /**< Anclaje interpolado resuelto después del anfitrión. */
    AnatomyId pathNodeA, pathNodeB;
    float pathU, lateralOffset;
    Vector3 pathNormal;
    uint32_t membranePrevious; /**< Estación anterior de una lámina continua. */
} CreatureModuleInstance;

#define CREATURE_MAX_MODULES 64

/**
 * @struct BodyPlan
 * @brief Plan corporal que define la raíz y los módulos que componen la criatura.
 */
typedef struct BodyPlan {
    AnatomyRef root;
    CreatureModuleInstance modules[CREATURE_MAX_MODULES];
    size_t moduleCount;
} BodyPlan;

typedef enum CreatureGait {
    CREATURE_GAIT_NONE = 0,
    CREATURE_GAIT_SPRAWLING_QUADRUPED,
    CREATURE_GAIT_UPRIGHT_QUADRUPED, /**< Paso cuadrúpedo digitígrado genérico. */
    CREATURE_GAIT_FELINE_STALK,     /**< Merodeo felino: ciclo largo, zancada corta y cuerpo agachado. */
    CREATURE_GAIT_BOVINE_WALK,      /**< Transferencia lenta de peso para ungulados robustos. */
    CREATURE_GAIT_COLUMNAR_WALK,    /**< Paso lento con elevación y oscilación reducidas. */
    CREATURE_GAIT_BIPEDAL_HOP,      /**< Salto bípedo sincronizado con fase de impulso, vuelo parabólico y contrapeso caudal. */
    CREATURE_GAIT_AVIAN_FLIGHT,     /**< Aleteo aviar con empuje escapular, plegado distal y postura posada estable. */
    CREATURE_GAIT_RAY_SWIM,         /**< Onda propagada sinusoidal a lo largo de aletas pectorales batoideas. */
    CREATURE_GAIT_OCTOPUS_CRAWL,    /**< Movimiento coordinado e independiente de brazos hidrostáticos y manto. */
    CREATURE_GAIT_INSECT_FLIGHT,    /**< Batido independiente de alas anteriores y posteriores con patas recogidas. */
    CREATURE_GAIT_MEDUSA_PULSE,     /**< Contracción y relajación rítmica de la campana con retardo en tentáculos. */
    CREATURE_GAIT_THUNNIFORM_SWIM,  /**< Nado túnido: tronco anterior estable, propulsión caudal lateral de alta frecuencia. */
    CREATURE_GAIT_CARANGIFORM_SHARK_SWIM, /**< Nado de escualo: ondulación lateral axial y cola heterocerca. */
    CREATURE_GAIT_SEAHORSE_SWIM,    /**< Nado erguido de signátido: aleta dorsal ondulante, pectorales rápidas y cola estable. */
    CREATURE_GAIT_TURTLE_SWIM,      /**< Nado de tortuga marina: batido coordinado de aletas anteriores con caparazón rígido. */
    CREATURE_GAIT_CETACEAN_SWIM     /**< Nado cetáceo: ondulación axial dorsoventral y aleta caudal horizontal. */
} CreatureGait;

/**
 * @struct CreatureRecipe
 * @brief Blueprint inmutable/reutilizable que define una criatura por sus datos ontogenéticos.
 */
typedef struct CreatureRecipe {
    CreatureRecipeId id;
    const char* name;
    BodyPlan bodyPlan;
    CreaturePhenotype seed, larva, juvenile, adult;
    CreatureGait gait;
    uint32_t offspringSeed;
    unsigned offspringVersion;
} CreatureRecipe;

typedef enum CreatureStage {
    CREATURE_STAGE_SEED = 0,
    CREATURE_STAGE_LARVA,
    CREATURE_STAGE_JUVENILE,
    CREATURE_STAGE_ADULT
} CreatureStage;

/** @brief Primer ID libre determinista; cero si se agota el espacio. */
uint32_t CreatureRecipe_NextFreeModuleId(const CreatureRecipe* recipe);

/** @param recipe Receta fuente. @param stage Etapa ontogenética. @return Fenotipo o NULL. */
const CreaturePhenotype* CreatureRecipe_GetStage(const CreatureRecipe* recipe, CreatureStage stage);

/** @brief Evalúa cuatro etapas equiespaciadas. @return Fenotipo a edad normalizada. */
CreaturePhenotype CreatureRecipe_EvaluateDevelopment(const CreatureRecipe* recipe, float age);

/** @brief Valida identidades, índices, ranuras de anclaje y módulos admitidos. @return Validez estructural. */
bool CreatureRecipe_Validate(const CreatureRecipe* recipe, const CreaturePhenotype* phenotype);

/** @brief Orden topológico estable: anfitriones, extremos de ruta y soporte de membrana.
 * Desempata por familia e identidad, sin depender del orden del documento.
 * No modifica la receta ni asigna memoria dinámica.
 * @param recipe Receta fuente. @param order Índices destino (CREATURE_MAX_MODULES).
 * @return false ante ciclos, identidades repetidas o dependencias ausentes. */
bool CreatureRecipe_ResolutionOrder(const CreatureRecipe* recipe, size_t order[CREATURE_MAX_MODULES]);

#ifdef __cplusplus
}
#endif

#endif
