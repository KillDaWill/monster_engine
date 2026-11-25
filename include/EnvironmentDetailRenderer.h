#ifndef MONSTER_ENVIRONMENT_DETAIL_RENDERER_H
#define MONSTER_ENVIRONMENT_DETAIL_RENDERER_H

#include "EcosystemVisual.h"
#include "RenderInterfaces.h"
#include "WorldStaticData.h"

/** @brief Presentación ambiental. Las huellas y la decoración no modifican el mundo. */
typedef struct EnvironmentDetailRenderer { void *impl; } EnvironmentDetailRenderer;
bool EnvironmentDetailRenderer_Init(EnvironmentDetailRenderer *details);
void EnvironmentDetailRenderer_Destroy(EnvironmentDetailRenderer *details);
void EnvironmentDetailRenderer_SyncWorld(EnvironmentDetailRenderer *details);
size_t EnvironmentDetailRenderer_VramBytes(const EnvironmentDetailRenderer *details);

void EnvironmentDetailRenderer_QueueDecoration(EnvironmentDetailRenderer *details,
    const WorldStaticData *world, Renderer3D *renderer, const ICamera *camera, int tier);

/** @brief Actualiza un mapa R8 de oclusión de proximidad proyectado sobre el terreno. */
unsigned EnvironmentDetailRenderer_Contacts(EnvironmentDetailRenderer *details,
    const WorldStaticData *world, const ICamera *camera, const EcosystemVisualDrawPacket *packets,
    size_t count, bool shadowed, Vector3 *mapping);

#endif
