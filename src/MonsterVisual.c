#include "MonsterVisual.h"
#include <stdlib.h>
#include <string.h>

MonsterVisual MonsterVisual_Create(SDFMesherConfig mesherConfig) {
    MonsterVisual visual;
    memset(&visual, 0, sizeof(MonsterVisual));
    visual.sdf = MonsterSDF_Create();
    visual.mesher = SDFMesher_Create(mesherConfig);
    visual.mesh = Mesh_Create();
    visual.isDirty = true;
    visual.updateTimer = 0.0f;
    return visual;
}

void MonsterVisual_Free(MonsterVisual* visual) {
    if (!visual) return;
    MonsterSDF_Free(&visual->sdf);
    Mesh_Free(&visual->mesh);
    visual->isDirty = false;
    visual->updateTimer = 0.0f;
}

void MonsterVisual_MarkDirty(MonsterVisual* visual) {
    if (visual) visual->isDirty = true;
}

bool MonsterVisual_RebuildNow(
    MonsterVisual* visual,
    const Monster* monster,
    MonsterSDFConfig sdfConfig
) {
    if (!visual || !monster) return false;

    if (!MonsterSDF_Build(&visual->sdf, monster, sdfConfig)) {
        return false;
    }

    SDFField field = MonsterSDF_GetField(&visual->sdf);
    bool ok = SDFMesher_GenerateMesh(&visual->mesher, &field, &visual->mesh);

    if (ok) {
        visual->isDirty = false;
        visual->updateTimer = 0.0f;
    }

    return ok;
}

bool MonsterVisual_Update(
    MonsterVisual* visual,
    const Monster* monster,
    float deltaTime,
    float rebuildInterval,
    MonsterSDFConfig sdfConfig
) {
    if (!visual || !monster) return false;

    visual->updateTimer += deltaTime;

    bool timeTriggered = (rebuildInterval > 0.0f) && (visual->updateTimer >= rebuildInterval);

    if (visual->isDirty || timeTriggered || visual->mesh.vertexCount == 0) {
        return MonsterVisual_RebuildNow(visual, monster, sdfConfig);
    }

    return false;
}

const Mesh* MonsterVisual_GetMesh(const MonsterVisual* visual) {
    return visual ? &visual->mesh : NULL;
}
