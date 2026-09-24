/**
 * @file Attachment.h
 * @brief Anclajes locales publicados por módulos anfitriones con identidades estables.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_ATTACHMENT_H
#define CREATURE_ATTACHMENT_H

#include "Vector.h"
#include "Anatomy.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identificador local de ranura de anclaje dentro de un módulo anfitrión. */
typedef uint16_t AttachmentSlotId;

/** Semántica funcional o regional del punto de anclaje. */
typedef enum AttachmentRole {
    ATTACHMENT_NONE = 0,
    ATTACHMENT_CERVICAL,
    ATTACHMENT_PECTORAL_LEFT,
    ATTACHMENT_PECTORAL_RIGHT,
    ATTACHMENT_PELVIC_LEFT,
    ATTACHMENT_PELVIC_RIGHT,
    ATTACHMENT_CAUDAL,
    ATTACHMENT_DORSAL,
    ATTACHMENT_VENTRAL,
    ATTACHMENT_CRANIAL
} AttachmentRole;

/** Referencia explícita a una ranura de un módulo anfitrión específico. */
typedef struct AttachmentRef {
    uint32_t hostModuleInstanceId;
    AttachmentSlotId slotId;
} AttachmentRef;

/** Constructor en línea de referencia de anclaje. */
static inline AttachmentRef AttachmentRef_Create(uint32_t hostModuleInstanceId, AttachmentSlotId slotId) {
    AttachmentRef ref = {hostModuleInstanceId, slotId};
    return ref;
}

/**
 * @struct AttachmentSlot
 * @brief Ranura de anclaje publicada por un módulo anfitrión.
 */
typedef struct AttachmentSlot {
    AttachmentSlotId id;             /**< ID estable de la ranura en el módulo anfitrión. */
    AttachmentRole role;             /**< Rol o significado semántico regional. */
    uint32_t hostModuleInstanceId;   /**< Módulo propietario de la ranura. */
    AnatomyId hostNode;              /**< Estación anatómica anfitriona. */
    Vector3 position;                /**< Posición de anclaje en el espacio del modelo. */
    Vector3 forward;                 /**< Eje de crecimiento longitudinal. */
    Vector3 up;                      /**< Eje de orientación vertical/dorsal. */
    Vector3 side;                    /**< Eje lateral/sagital. */
    Vector3 hostRadii;               /**< Radios de sección de la estación anfitriona. */
    float scale;                     /**< Factor de escala heredado del anfitrión. */
} AttachmentSlot;

#define ATTACHMENT_MAX_SLOTS 64

/**
 * @struct AttachmentSlotSet
 * @brief Colección de ranuras publicadas durante la resolución de módulos.
 */
typedef struct AttachmentSlotSet {
    AttachmentSlot slots[ATTACHMENT_MAX_SLOTS];
    size_t count;
} AttachmentSlotSet;

/** Busca una ranura por módulo anfitrión e ID local de slot. */
const AttachmentSlot* AttachmentSlotSet_Find(const AttachmentSlotSet* slots, uint32_t hostModuleInstanceId, AttachmentSlotId slotId);

/** Busca una ranura a partir de una referencia estructurada. */
const AttachmentSlot* AttachmentSlotSet_FindRef(const AttachmentSlotSet* slots, AttachmentRef ref);

/** Busca la primera ranura que cumpla con el rol semántico dado. */
const AttachmentSlot* AttachmentSlotSet_FindByRole(const AttachmentSlotSet* slots, AttachmentRole role);

/** Obtiene la referencia explícita correspondiente al primer slot con el rol solicitado. */
AttachmentRef AttachmentSlotSet_FindRefByRole(const AttachmentSlotSet* slots, AttachmentRole role);

#ifdef __cplusplus
}
#endif

#endif
