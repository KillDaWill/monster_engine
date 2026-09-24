#include "Attachment.h"

const AttachmentSlot* AttachmentSlotSet_Find(const AttachmentSlotSet* slots, uint32_t hostModuleInstanceId, AttachmentSlotId slotId) {
    if (!slots || !slotId) return NULL;
    for (size_t i = 0; i < slots->count; ++i) {
        if (slots->slots[i].hostModuleInstanceId == hostModuleInstanceId && slots->slots[i].id == slotId) {
            return &slots->slots[i];
        }
    }
    return NULL;
}

const AttachmentSlot* AttachmentSlotSet_FindRef(const AttachmentSlotSet* slots, AttachmentRef ref) {
    return AttachmentSlotSet_Find(slots, ref.hostModuleInstanceId, ref.slotId);
}

const AttachmentSlot* AttachmentSlotSet_FindByRole(const AttachmentSlotSet* slots, AttachmentRole role) {
    if (!slots || role == ATTACHMENT_NONE) return NULL;
    for (size_t i = 0; i < slots->count; ++i) {
        if (slots->slots[i].role == role) {
            return &slots->slots[i];
        }
    }
    return NULL;
}

AttachmentRef AttachmentSlotSet_FindRefByRole(const AttachmentSlotSet* slots, AttachmentRole role) {
    const AttachmentSlot* slot = AttachmentSlotSet_FindByRole(slots, role);
    if (!slot) return (AttachmentRef){0, 0};
    return (AttachmentRef){slot->hostModuleInstanceId, slot->id};
}
