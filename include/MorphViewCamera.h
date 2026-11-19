#ifndef MORPH_VIEW_CAMERA_H
#define MORPH_VIEW_CAMERA_H
#include "Monster.h"
#include "MonsterSDF.h"
#include "RenderInterfaces.h"

/** @brief Encuadra la anatomía usando la caja conservadora del campo si está disponible. */
void MorphViewCamera_Framing(const Monster *, const MonsterSDF *, Vector3 *center, float *radius,
                             Vector3 *head, Vector3 *tail);
/** @brief Aplica la vista y mantiene la cámara fuera del campo implícito. */
void MorphViewCamera_Setup(ICamera *, int viewMode, float yaw, float pitch, float zoom,
                           const MonsterSDF *, Vector3 center, float radius, Vector3 head, Vector3 tail);
#endif
