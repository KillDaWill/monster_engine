#include "Attachment.h"
#include <math.h>

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

bool AttachmentSlotSet_PublishRadialRing(
    AttachmentSlotSet* slots,
    uint32_t hostModule,
    AnatomyId hostNode,
    Vector3 center,
    Vector3 axis,
    Vector3 radialRef,
    float radius,
    unsigned count,
    float angleStart,
    float angleSpan,
    AttachmentSlotId baseSlotId,
    AttachmentRole role,
    Vector3 hostRadii,
    float scale) {
    if (!slots || !count || !baseSlotId || slots->count + count > ATTACHMENT_MAX_SLOTS) return false;
    float axisLen = Vec3_Length(axis);
    if (axisLen < 1e-6f) return false;
    Vector3 up = Vec3_Scale(axis, 1.0f / axisLen);
    Vector3 ref = Vec3_Sub(radialRef, Vec3_Scale(up, Vec3_Dot(radialRef, up)));
    if (Vec3_LengthSq(ref) < 1e-6f) {
        Vector3 fallback = fabsf(up.z) < 0.9f ? Vec3_Create(0, 0, 1) : Vec3_Create(1, 0, 0);
        ref = Vec3_Sub(fallback, Vec3_Scale(up, Vec3_Dot(fallback, up)));
    }
    Vector3 e0 = Vec3_Normalize(ref);
    Vector3 e1 = Vec3_Normalize(Vec3_Cross(up, e0));
    bool fullCircle = fabsf(fabsf(angleSpan) - 6.28318530718f) < 1e-3f;
    for (unsigned i = 0; i < count; ++i) {
        float frac = (fullCircle || count == 1)
            ? ((float)i / (float)count)
            : ((float)i / (float)(count - 1));
        float angle = angleStart + angleSpan * frac;
        float c = cosf(angle), s = sinf(angle);
        Vector3 radial = Vec3_Normalize(Vec3_Add(Vec3_Scale(e0, c), Vec3_Scale(e1, s)));
        Vector3 tangent = Vec3_Normalize(Vec3_Cross(up, radial));
        AttachmentSlot slot = {
            .id = (AttachmentSlotId)(baseSlotId + i),
            .role = role,
            .hostModuleInstanceId = hostModule,
            .hostNode = hostNode,
            .position = Vec3_Add(center, Vec3_Scale(radial, radius)),
            .forward = radial,
            .up = up,
            .side = tangent,
            .hostRadii = hostRadii,
            .scale = scale
        };
        slots->slots[slots->count++] = slot;
    }
    return true;
}
