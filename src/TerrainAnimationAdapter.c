/**
 * @file TerrainAnimationAdapter.c
 * @brief Implementación del adaptador local <-> mundial para muestreo de patas sobre terreno 3D.
 */
#include "TerrainAnimationAdapter.h"
#include <math.h>
#include <string.h>

static bool adapterSampleGround(World *self, Vector3 localQuery, SurfaceHit *outHit) {
    if (!self || !outHit) return false;
    const TerrainAnimationAdapter *a = (const TerrainAnimationAdapter *)self;
    if (!a->staticWorld || a->staticWorld->isFlat) {
        outHit->position = Vec3_Create(localQuery.x, 0.0f, localQuery.z);
        outHit->normal = Vec3_Create(0.0f, 1.0f, 0.0f);
        return true;
    }

    float sc = fabsf(a->scale) > 1e-6f ? a->scale : 1.0f;
    Vector3 worldQuery = World_LocalToWorld(localQuery, a->modelCenter, sc,
                                            a->bodyOrientation, a->worldOrigin);
    SurfaceHit worldHit;
    if (!World_SampleSurface(a->staticWorld, worldQuery, &worldHit)) {
        /* En mundo procedural nunca fabricar un suelo plano falso ante fallo fuera de dominio. */
        return false;
    }

    outHit->position = World_WorldToLocal(worldHit.position, a->modelCenter, sc,
                                          a->bodyOrientation, a->worldOrigin);
    Vector3 localN = World_WorldDirToLocal(worldHit.normal, a->bodyOrientation);
    float lenSq = Vec3_LengthSq(localN);
    outHit->normal = (lenSq > 1e-8f) ? Vec3_Scale(localN, 1.0f / sqrtf(lenSq))
                                     : Vec3_Create(0.0f, 1.0f, 0.0f);
    return true;
}

void TerrainAnimationAdapter_Init(TerrainAnimationAdapter *adapter,
                                  const WorldStaticData *staticWorld,
                                  Vector3 worldOrigin,
                                  Quaternion bodyOrientation,
                                  Vector3 modelCenter,
                                  float scale) {
    if (!adapter) return;
    memset(adapter, 0, sizeof(*adapter));
    adapter->worldInterface.getWalkingHeight = NULL;
    adapter->worldInterface.sampleGround = adapterSampleGround;
    adapter->staticWorld = staticWorld;
    adapter->worldOrigin = worldOrigin;
    float qLenSq = bodyOrientation.x * bodyOrientation.x + bodyOrientation.y * bodyOrientation.y +
                   bodyOrientation.z * bodyOrientation.z + bodyOrientation.w * bodyOrientation.w;
    adapter->bodyOrientation = (qLenSq > 0.5f) ? Quat_Normalize(bodyOrientation) : Quat_Identity();
    adapter->modelCenter = modelCenter;
    adapter->scale = (isfinite(scale) && scale > 1e-5f) ? scale : 1.0f;
}
