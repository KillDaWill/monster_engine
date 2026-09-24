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
    CREATURE_MODULE_ORNAMENT
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
    CREATURE_GAIT_SPRAWLING_QUADRUPED
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

#ifdef __cplusplus
}
#endif

#endif
