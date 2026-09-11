#include "MonsterVisual.h"
#include "PrimitiveMesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint64_t HashBytes(const void* data, size_t size, uint64_t hash) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 0x100000001b3ULL; }
    return hash;
}
static uint64_t HashMouth(const Monster* monster) {
    uint64_t h = 0xcbf29ce484222325ULL;
    if (!monster) return h;
    h = HashBytes(&monster->hasHead,sizeof(monster->hasHead),h);
    if(monster->hasHead) h=HashBytes(&monster->head.phenotype,sizeof(monster->head.phenotype),h);
    h=HashBytes(&monster->hasAnatomyGraph,sizeof(monster->hasAnatomyGraph),h);
    if(monster->hasAnatomyGraph) {uint64_t anatomy=AnatomyGraph_Fingerprint(&monster->anatomyGraph);h=HashBytes(&anatomy,sizeof(anatomy),h);}
    h = HashBytes(&monster->mouthCount, sizeof(size_t), h);
    for (size_t i = 0; i < monster->mouthCount; ++i) {
        const Mouth* m = &monster->mouths[i];
        h = HashBytes(&m->bodyPartIndex, sizeof(m->bodyPartIndex), h);
        h = HashBytes(&m->shape,sizeof(m->shape),h);
        h = HashBytes(&m->offset, sizeof(Vector3), h); h = HashBytes(&m->rotation, sizeof(Vector3), h);
        h = HashBytes(&m->scale, sizeof(Vector3), h); h = HashBytes(&m->insideColor, sizeof(Color), h);
        h = HashBytes(&m->slitThickness, sizeof(float), h); h = HashBytes(&m->slitSoftness, sizeof(float), h);
        h = HashBytes(&m->cornerRadius, sizeof(float), h); h = HashBytes(&m->jawPivot, sizeof(Vector3), h);
        h = HashBytes(&m->jawLength, sizeof(float), h); h = HashBytes(&m->jawWidth, sizeof(float), h);
        h = HashBytes(&m->jawThickness, sizeof(float), h); h = HashBytes(&m->jawRearMass, sizeof(float), h);
        h = HashBytes(&m->jawMuscle, sizeof(float), h); h = HashBytes(&m->maxJawAngle, sizeof(float), h);
        h = HashBytes(&m->hingeRadius, sizeof(float), h); h = HashBytes(&m->throatRadius, sizeof(float), h);
        h = HashBytes(&m->cranium, sizeof(Vector3), h); h = HashBytes(&m->snout, sizeof(Vector3), h);
        h = HashBytes(&m->cheeks, sizeof(Vector3), h); h = HashBytes(&m->brows, sizeof(Vector3), h);
        if (m->bodyPartIndex < monster->bodyPartCount) h = HashBytes(&monster->bodyParts[m->bodyPartIndex].positionRender, sizeof(Vector3), h);
    }
    return h;
}
static uint64_t HashBody(const MonsterVisual* visual, const Monster* monster, MonsterSDFConfig cfg) {
    uint64_t h = 0xcbf29ce484222325ULL;
    h = HashBytes(&cfg.bodySmoothness, sizeof(float), h);
    h = HashBytes(&cfg.connectionSmoothness, sizeof(float), h);
    h = HashBytes(&cfg.mouthSmoothness, sizeof(float), h);
    h = HashBytes(&cfg.connectionRadiusFactor, sizeof(float), h);
    h = HashBytes(&cfg.boundsPadding, sizeof(float), h);
    bool pruning = cfg.enableConnectorPruning ? true : false;
    h = HashBytes(&pruning, sizeof(bool), h);
    if (visual) h = HashBytes(&visual->mesher.config, sizeof(visual->mesher.config), h);
    if (!monster) return h;
    h=HashBytes(&monster->hasAnatomyGraph,sizeof(monster->hasAnatomyGraph),h);
    if(monster->hasAnatomyGraph) {uint64_t anatomy=AnatomyGraph_Fingerprint(&monster->anatomyGraph);h=HashBytes(&anatomy,sizeof(anatomy),h);}
    h=HashBytes(&monster->hasHead,sizeof(monster->hasHead),h);
    if(monster->hasHead) h=HashBytes(&monster->head.phenotype,sizeof(monster->head.phenotype),h);
    h = HashBytes(&monster->bodyPartCount, sizeof(size_t), h);
    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* p=&monster->bodyParts[i]; h=HashBytes(&p->positionRender,sizeof(Vector3),h);
        h=HashBytes(&p->widthRender,sizeof(float),h);h=HashBytes(&p->heightRender,sizeof(float),h);h=HashBytes(&p->lengthRender,sizeof(float),h);h=HashBytes(&p->color,sizeof(p->color),h);
    }
    h = HashBytes(&monster->mouthCount, sizeof(size_t), h);
    for (size_t i = 0; i < monster->mouthCount; ++i) {
        const Mouth* m = &monster->mouths[i];
        h = HashBytes(&m->bodyPartIndex, sizeof(size_t), h);
        h = HashBytes(&m->shape,sizeof(m->shape),h);
        h = HashBytes(&m->offset, sizeof(Vector3), h);
        h = HashBytes(&m->rotation, sizeof(Vector3), h);
        h = HashBytes(&m->scale, sizeof(Vector3), h);
        h = HashBytes(&m->insideColor, sizeof(Color), h);
        h = HashBytes(&m->slitThickness, sizeof(float), h); h = HashBytes(&m->slitSoftness, sizeof(float), h);
        h = HashBytes(&m->cornerRadius, sizeof(float), h); h = HashBytes(&m->jawPivot, sizeof(Vector3), h);
        h = HashBytes(&m->cranium, sizeof(Vector3), h); h = HashBytes(&m->snout, sizeof(Vector3), h);
        h = HashBytes(&m->cheeks, sizeof(Vector3), h); h = HashBytes(&m->brows, sizeof(Vector3), h);
    }
    h = HashBytes(&monster->eyeCount, sizeof(size_t), h);
    for (size_t i=0;i<monster->eyeCount;++i) h=HashBytes(&monster->eyes[i],sizeof(Eye),h);
    h = HashBytes(&monster->colorPalette.count, sizeof(size_t), h);
    h = HashBytes(monster->colorPalette.colors, monster->colorPalette.count*sizeof(Color), h);
    return h;
}
static void TransformJaw(MonsterVisualMouth* vm, const Mouth* mouth, const Monster* monster) {
    float angle = Mouth_GetJawAngle(mouth) * 0.01745329252f;
    float c = cosf(angle), s = sinf(angle);

    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
    if (monster && monster->hasLizardPhenotype && vm->baseScale > 1e-4f) {
        float factor = monster->lizardPhenotype.totalScale / vm->baseScale;
        factor *= monster->lizardPhenotype.cephalicDevelopment;
        scaleX = factor;
        scaleY = factor;
        scaleZ = factor;
    } else {
        if (vm->baseJawWidth > 1e-4f && mouth->jawWidth > 1e-4f) {
            scaleX = mouth->jawWidth / vm->baseJawWidth;
        }
        if (vm->baseJawThickness > 1e-4f && mouth->jawThickness > 1e-4f) {
            scaleY = mouth->jawThickness / vm->baseJawThickness;
        }
        if (vm->baseJawLength > 1e-4f && mouth->jawLength > 1e-4f) {
            scaleZ = mouth->jawLength / vm->baseJawLength;
        }
    }

    Vector3 pivotBase = (Vec3_LengthSq(vm->basePivot) > 0.0f || vm->baseJawLength > 0.0f) ? vm->basePivot : vm->pivot;

    for (size_t i = 0; i < vm->jawBase.vertexCount; ++i) {
        Vector3 pRel = Vec3_Sub(vm->jawBase.vertices[i].position, pivotBase);
        Vector3 n = vm->jawBase.vertices[i].normal;
        Vector3 pScaled = Vec3_Create(pRel.x * scaleX, pRel.y * scaleY, pRel.z * scaleZ);
        Vector3 rp = Vec3_Create(pScaled.x, c * pScaled.y - s * pScaled.z, s * pScaled.y + c * pScaled.z);
        Vector3 rn = Vec3_Create(n.x, c * n.y - s * n.z, s * n.y + c * n.z);
        vm->jaw.vertices[i] = vm->jawBase.vertices[i];
        vm->jaw.vertices[i].position = Vec3_Add(vm->worldPosition, Transform3D_RotateVector(vm->rotation, Vec3_Add(vm->pivot, rp)));
        Vector3 nr = Transform3D_RotateVector(vm->rotation, rn);
        float len = Vec3_Length(nr);
        if (!isfinite(len) || len < 1e-6f) nr = Transform3D_RotateVector(vm->rotation, n);
        else nr = Vec3_Scale(nr, 1.0f/len);
        vm->jaw.vertices[i].normal = nr;
    }
    for (size_t i = 0; i < vm->hingeBase.vertexCount; ++i) {
        Vector3 p = vm->hingeBase.vertices[i].position;
        Vector3 n = vm->hingeBase.vertices[i].normal;
        float h = vm->seamScale * scaleZ;
        if (h < 1e-4f) h = Math_Max(Vec3_Distance(vm->seamSkullLeft, vm->pivot) * scaleZ, 0.02f);
        Vector3 pRel = Vec3_Sub(p, pivotBase);
        Vector3 pRelScaled = Vec3_Create(pRel.x * scaleX, pRel.y * scaleY, pRel.z * scaleZ);
        Vector3 pScaled = Vec3_Add(vm->pivot, pRelScaled);

        Vector3 seamSkullLeft = Vec3_Add(vm->pivot, Vec3_Create(
            (vm->seamSkullLeft.x - pivotBase.x) * scaleX,
            (vm->seamSkullLeft.y - pivotBase.y) * scaleY,
            (vm->seamSkullLeft.z - pivotBase.z) * scaleZ));
        Vector3 seamSkullRight = Vec3_Add(vm->pivot, Vec3_Create(
            (vm->seamSkullRight.x - pivotBase.x) * scaleX,
            (vm->seamSkullRight.y - pivotBase.y) * scaleY,
            (vm->seamSkullRight.z - pivotBase.z) * scaleZ));
        Vector3 seamGular = Vec3_Add(vm->pivot, Vec3_Create(
            (vm->seamGular.x - pivotBase.x) * scaleX,
            (vm->seamGular.y - pivotBase.y) * scaleY,
            (vm->seamGular.z - pivotBase.z) * scaleZ));
        Vector3 seamJawLeftClosed = Vec3_Add(vm->pivot, Vec3_Create(
            (vm->seamJawLeftClosed.x - pivotBase.x) * scaleX,
            (vm->seamJawLeftClosed.y - pivotBase.y) * scaleY,
            (vm->seamJawLeftClosed.z - pivotBase.z) * scaleZ));
        Vector3 seamJawRightClosed = Vec3_Add(vm->pivot, Vec3_Create(
            (vm->seamJawRightClosed.x - pivotBase.x) * scaleX,
            (vm->seamJawRightClosed.y - pivotBase.y) * scaleY,
            (vm->seamJawRightClosed.z - pivotBase.z) * scaleZ));
        Vector3 seamJawAnchor = Vec3_Add(vm->pivot, Vec3_Create(
            (vm->seamJawAnchor.x - pivotBase.x) * scaleX,
            (vm->seamJawAnchor.y - pivotBase.y) * scaleY,
            (vm->seamJawAnchor.z - pivotBase.z) * scaleZ));

        float dSkull = Math_Min(Vec3_Distance(pScaled, seamSkullLeft), Vec3_Distance(pScaled, seamSkullRight));
        dSkull = Math_Min(dSkull, Vec3_Distance(pScaled, seamGular));
        dSkull = Math_Min(dSkull, Vec3_Distance(pScaled, vm->pivot));
        float dJaw = Math_Min(Vec3_Distance(pScaled, seamJawLeftClosed), Vec3_Distance(pScaled, seamJawRightClosed));
        dJaw = Math_Min(dJaw, Vec3_Distance(pScaled, seamJawAnchor));
        float dnSkull = dSkull / h;
        float dnJaw = dJaw / h;
        float denom = dnSkull + dnJaw + 1e-6f;
        float w = dnSkull / denom;
        w = Math_Clamp01(w);
        float ws = w * w * (3.0f - 2.0f * w);
        float wAngle = angle * ws;
        float cw = cosf(wAngle), sw = sinf(wAngle);
        Vector3 pr = Vec3_Create(pRelScaled.x, cw * pRelScaled.y - sw * pRelScaled.z, sw * pRelScaled.y + cw * pRelScaled.z);
        Vector3 newPos = Vec3_Add(vm->pivot, pr);
        Vector3 nRot = Vec3_Create(n.x, cw * n.y - sw * n.z, sw * n.y + cw * n.z);
        float nl = Vec3_Length(nRot);
        if (!isfinite(nl) || nl < 1e-6f) nRot = n;
        else nRot = Vec3_Scale(nRot, 1.0f/nl);
        vm->hinge.vertices[i] = vm->hingeBase.vertices[i];
        vm->hinge.vertices[i].position = Vec3_Add(vm->worldPosition, Transform3D_RotateVector(vm->rotation, newPos));
        vm->hinge.vertices[i].normal = Transform3D_RotateVector(vm->rotation, nRot);
        float nlen = Vec3_Length(vm->hinge.vertices[i].normal);
        if (!isfinite(nlen) || nlen < 1e-6f) vm->hinge.vertices[i].normal = Vec3_Create(0,1,0);
        else vm->hinge.vertices[i].normal = Vec3_Scale(vm->hinge.vertices[i].normal, 1.0f/nlen);
    }
}

bool MonsterVisual_UpdateEyes(MonsterVisualEye* eyes, size_t eyeCount, const Monster* monster) {
    if (!monster) return false;
    if (monster->eyeCount == 0) return true;
    if (!eyes || eyeCount < monster->eyeCount) return false;
    for (size_t i = 0; i < monster->eyeCount; ++i) {
        const Eye* e = &monster->eyes[i];
        if (e->scale.x <= 1e-4f || e->scale.y <= 1e-4f || e->scale.z <= 1e-4f) {
            Mesh_Clear(&eyes[i].sclera);
            Mesh_Clear(&eyes[i].iris);
            Mesh_Clear(&eyes[i].pupil);
            continue;
        }
        Vector3 base = e->bodyPartIndex < monster->bodyPartCount ? monster->bodyParts[e->bodyPartIndex].positionRender : Vec3_Zero();
        Vector3 p = Vec3_Add(base, e->offset), r = e->scale, f = Vec3_Normalize(e->forward);
        if (Vec3_LengthSq(f) < 1e-6f) f = Transform3D_RotateVector(e->rotation, Vec3_Create(0, 0, 1));
        Transform3D st = Transform3D_Create(p, e->rotation, r);
        float irisRadius = Math_Max(Math_Min(r.x, r.y) * Math_Clamp(e->irisScale, .25f, .96f), .008f);
        float irisDepth = Math_Max(r.z * .075f, .004f);
        Transform3D it = Transform3D_Create(Vec3_Add(p, Vec3_Scale(f, r.z - irisDepth * .55f)), e->rotation, Vec3_Create(irisRadius, irisRadius, irisDepth));
        float pupilHeight = Math_Max(irisRadius * Math_Clamp(e->pupilScale, .08f, .90f), .004f);
        float pupilWidth = pupilHeight * Math_Clamp(e->pupilAspect, .12f, 1.0f), pupilDepth = Math_Max(irisDepth * .55f, .002f);
        Transform3D pt = Transform3D_Create(Vec3_Add(p, Vec3_Scale(f, r.z + pupilDepth * .15f)), e->rotation, Vec3_Create(pupilWidth, pupilHeight, pupilDepth));
        if (!PrimitiveMesh_GenerateEllipsoid(&eyes[i].sclera, st, 18, 14, e->scleraColor) ||
            !PrimitiveMesh_GenerateEllipsoid(&eyes[i].iris, it, 16, 10, e->irisColor) ||
            !PrimitiveMesh_GenerateEllipsoid(&eyes[i].pupil, pt, 12, 8, e->pupilColor)) {
            return false;
        }
    }
    return true;
}

static bool GenerateEyes(const Monster* monster, MonsterVisualEye** output) {
    if (!monster || !output) return false;
    if (monster->eyeCount == 0) {
        *output = NULL;
        return true;
    }
    MonsterVisualEye* eyes = (MonsterVisualEye*)calloc(monster->eyeCount, sizeof(*eyes));
    if (!eyes) return false;
    for (size_t i = 0; i < monster->eyeCount; ++i) {
        eyes[i].sclera = Mesh_Create();
        eyes[i].iris = Mesh_Create();
        eyes[i].pupil = Mesh_Create();
    }
    if (!MonsterVisual_UpdateEyes(eyes, monster->eyeCount, monster)) {
        for (size_t j = 0; j < monster->eyeCount; ++j) {
            Mesh_Free(&eyes[j].sclera);
            Mesh_Free(&eyes[j].iris);
            Mesh_Free(&eyes[j].pupil);
        }
        free(eyes);
        *output = NULL;
        return false;
    }
    *output = eyes;
    return true;
}
static void FreeEyes(MonsterVisualEye* eyes,size_t count){if(!eyes)return;for(size_t i=0;i<count;++i){Mesh_Free(&eyes[i].sclera);Mesh_Free(&eyes[i].iris);Mesh_Free(&eyes[i].pupil);}free(eyes);}
void MonsterVisual_UpdateMouthArticulation(MonsterVisualMouth* vm, const Mouth* source, const Monster* monster) {
    if (!vm || !source || !monster) return;
    Mouth m = *source; Mouth_Normalize(&m);
    Vector3 part = m.bodyPartIndex < monster->bodyPartCount ? monster->bodyParts[m.bodyPartIndex].positionRender : Vec3_Zero();
    vm->worldPosition = Vec3_Add(part, m.offset); vm->rotation = m.rotation; vm->pivot = m.jawPivot;
    TransformJaw(vm, &m, monster);
}
static bool BuildMouthFromSdfWithMeshers(MonsterVisualMouth* vm, const Mouth* source, const Monster* monster, const MonsterSDF* sdf, size_t mouthIndex, SDFMesher* jawMesher, SDFMesher* seamMesher) {
    if (!vm || !source || !monster || !sdf || mouthIndex >= sdf->mouthCount) return false;
    Mouth m = *source; Mouth_Normalize(&m);
    memset(vm, 0, sizeof(*vm));
    vm->jawBase = Mesh_Create(); vm->jaw = Mesh_Create(); vm->hingeBase = Mesh_Create(); vm->hinge = Mesh_Create();
    vm->pivot = m.jawPivot;
    vm->rotation = m.rotation;
    Vector3 part = m.bodyPartIndex < monster->bodyPartCount ? monster->bodyParts[m.bodyPartIndex].positionRender : Vec3_Zero();
    vm->worldPosition = Vec3_Add(part, m.offset);
    const MonsterSDFMouth* sm = &sdf->mouths[mouthIndex];
    vm->seamSkullLeft = sm->seamSkullLeftLocal;
    vm->seamSkullRight = sm->seamSkullRightLocal;
    vm->seamJawLeftClosed = sm->seamJawLeftClosedLocal;
    vm->seamJawRightClosed = sm->seamJawRightClosedLocal;
    vm->seamGular = sm->seamGularLocal;
    vm->seamJawAnchor = sm->seamJawAnchorLocal;
    vm->seamScale = sm->seamScale;
    vm->basePivot = m.jawPivot;
    vm->baseJawLength = m.jawLength;
    vm->baseJawWidth = m.jawWidth;
    vm->baseJawThickness = m.jawThickness;
    vm->baseScale = monster->hasLizardPhenotype ? monster->lizardPhenotype.totalScale : 1.0f;

    MonsterSDFJawField jawContext;
    SDFField jawField=MonsterSDF_GetJawField(sdf,mouthIndex,&jawContext);
    SDFMesher localJaw;
    SDFMesher* activeJaw = jawMesher;
    if (!activeJaw) {
        SDFMesherConfig jawConfig=SDFMesher_DefaultConfig(); jawConfig.voxelSize=.04f; jawConfig.maxCells=100000; jawConfig.useAutoBounds=true;
        localJaw = SDFMesher_Create(jawConfig);
        activeJaw = &localJaw;
    }
    bool jawOk = SDFMesher_GenerateMesh(activeJaw, &jawField, &vm->jawBase);
    if (!jawMesher) SDFMesher_Free(&localJaw);
    if (!jawOk) return false;

    if (!Mesh_ReserveVertices(&vm->jaw, vm->jawBase.vertexCount) || !Mesh_ReserveIndices(&vm->jaw, vm->jawBase.indexCount)) return false;
    vm->jaw.vertexCount = vm->jawBase.vertexCount; vm->jaw.indexCount = vm->jawBase.indexCount;
    memcpy(vm->jaw.indices, vm->jawBase.indices, vm->jawBase.indexCount * sizeof(MeshIndex));

    MonsterSDFSeamField seamCtx;
    SDFField seamField=MonsterSDF_GetSeamField(sdf,mouthIndex,&seamCtx);
    SDFMesher localSeam;
    SDFMesher* activeSeam = seamMesher;
    if (!activeSeam) {
        SDFMesherConfig seamConfig=SDFMesher_DefaultConfig(); seamConfig.voxelSize=.03f; seamConfig.maxCells=120000; seamConfig.useAutoBounds=true;
        localSeam = SDFMesher_Create(seamConfig);
        activeSeam = &localSeam;
    }
    bool seamOk = SDFMesher_GenerateMesh(activeSeam, &seamField, &vm->hingeBase);
    if (!seamMesher) SDFMesher_Free(&localSeam);
    if (!seamOk) return false;

    if (!Mesh_ReserveVertices(&vm->hinge, vm->hingeBase.vertexCount) || !Mesh_ReserveIndices(&vm->hinge, vm->hingeBase.indexCount)) return false;
    vm->hinge.vertexCount = vm->hingeBase.vertexCount; vm->hinge.indexCount = vm->hingeBase.indexCount;
    memcpy(vm->hinge.indices, vm->hingeBase.indices, vm->hingeBase.indexCount * sizeof(MeshIndex));
    TransformJaw(vm, &m, monster);
    return true;
}
bool MonsterVisual_BuildMouthMeshesFromSDFWithMeshers(MonsterVisualMouth* vm, const Mouth* source, const Monster* monster, const MonsterSDF* sdf, size_t mouthIndex, SDFMesher* jawMesher, SDFMesher* seamMesher) {
    return BuildMouthFromSdfWithMeshers(vm, source, monster, sdf, mouthIndex, jawMesher, seamMesher);
}
bool MonsterVisual_BuildMouthMeshesFromSDF(MonsterVisualMouth* vm, const Mouth* source, const Monster* monster, const MonsterSDF* sdf, size_t mouthIndex) {
    return BuildMouthFromSdfWithMeshers(vm,source,monster,sdf,mouthIndex,NULL,NULL);
}
bool MonsterVisual_BuildMouthMeshes(MonsterVisualMouth* vm, const Mouth* source, const Monster* monster) {
    MonsterSDF sdf=MonsterSDF_Create(); if(!vm||!source||!monster||!MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig())){MonsterSDF_Free(&sdf);return false;}
    bool ok=BuildMouthFromSdfWithMeshers(vm,source,monster,&sdf,0,NULL,NULL); MonsterSDF_Free(&sdf); return ok;
}
void MonsterVisualMouth_Free(MonsterVisualMouth* vm) { if (!vm) return; Mesh_Free(&vm->jawBase); Mesh_Free(&vm->jaw); Mesh_Free(&vm->hingeBase); Mesh_Free(&vm->hinge); }
static bool BuildMouthArray(const Monster* monster, const MonsterSDF* sdf, MonsterVisualMouth** output) {
    MonsterVisualMouth* mouths = monster->mouthCount ? calloc(monster->mouthCount, sizeof(*mouths)) : NULL;
    if (monster->mouthCount && !mouths) return false;
    for (size_t i = 0; i < monster->mouthCount; ++i) if (!BuildMouthFromSdfWithMeshers(&mouths[i], &monster->mouths[i], monster, sdf, i, NULL, NULL)) { for (size_t j = 0; j <= i; ++j) MonsterVisualMouth_Free(&mouths[j]); free(mouths); return false; }
    *output=mouths; return true;
}
static bool RebuildMouthsOnly(MonsterVisual* visual, const Monster* monster) {
    MonsterSDF snapshot=MonsterSDF_Create(); MonsterVisualMouth* mouths=NULL;
    if(!MonsterSDF_Build(&snapshot,monster,visual->sdf.config)||!BuildMouthArray(monster,&snapshot,&mouths)){MonsterSDF_Free(&snapshot);return false;}
    for(size_t i=0;i<visual->mouthCount;++i)MonsterVisualMouth_Free(&visual->mouths[i]);
    free(visual->mouths);
    visual->mouths=mouths;visual->mouthCount=monster->mouthCount;visual->mouthCapacity=monster->mouthCount;visual->mouthVisualFingerprint=HashMouth(monster);visual->mouthVisualGeneration++;
    MonsterSDF_Free(&snapshot);return true;
}
MonsterVisual MonsterVisual_Create(SDFMesherConfig cfg) { MonsterVisual v; memset(&v, 0, sizeof(v)); v.sdf=MonsterSDF_Create(); v.stagingSdf=MonsterSDF_Create(); v.mesher=SDFMesher_Create(cfg); SDFMesherConfig hc=SDFMesher_DefaultConfig();hc.voxelSize=.018f;hc.maxCells=1800000;hc.maxResolution=320;v.headMesher=SDFMesher_Create(hc); v.mesh=Mesh_Create(); v.stagingMesh=Mesh_Create();v.headMesh=Mesh_Create();v.stagingHeadMesh=Mesh_Create(); v.isDirty=true; return v; }
void MonsterVisual_Free(MonsterVisual* v) { if (!v) return; MonsterSDF_Free(&v->sdf); MonsterSDF_Free(&v->stagingSdf); SDFMesher_Free(&v->mesher);SDFMesher_Free(&v->headMesher); Mesh_Free(&v->mesh); Mesh_Free(&v->stagingMesh);Mesh_Free(&v->headMesh);Mesh_Free(&v->stagingHeadMesh); for(size_t i=0;i<v->eyeCount;++i){Mesh_Free(&v->eyes[i].sclera);Mesh_Free(&v->eyes[i].iris);Mesh_Free(&v->eyes[i].pupil);} free(v->eyes); for(size_t i=0;i<v->mouthCount;++i)MonsterVisualMouth_Free(&v->mouths[i]); free(v->mouths); memset(v,0,sizeof(*v)); }
void MonsterVisual_MarkDirty(MonsterVisual* v) { if (v) v->isDirty=true; }
uint64_t MonsterVisual_GetGeneration(const MonsterVisual* v) { return v ? v->rebuildGeneration : 0; }
uint64_t MonsterVisual_GetMouthVisualGeneration(const MonsterVisual* v) { return v ? v->mouthVisualGeneration : 0; }
bool MonsterVisual_RebuildNow(MonsterVisual* v, const Monster* monster, MonsterSDFConfig cfg) {
    if (!v || !monster || !MonsterSDF_Build(&v->stagingSdf, monster, cfg)) return false;
    MonsterSDFBodyField bodyContext;SDFField field=MonsterSDF_GetBodyField(&v->stagingSdf,&bodyContext); Mesh_Clear(&v->stagingMesh);
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];size_t regionCount=MonsterSDF_GetDetailRegions(&v->stagingSdf,6,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
    if(v->stagingSdf.axialStationCount>1) {
        field=MonsterSDF_GetField(&v->stagingSdf);
        v->mesher.config.adaptiveDetail=true;
        if(!SDFMesher_GenerateMeshDetailed(&v->mesher,&field,regions,regionCount,&v->stagingMesh))return false;
    } else if (!SDFMesher_GenerateMesh(&v->mesher,&field,&v->stagingMesh)) return false;
    Mesh_Clear(&v->stagingHeadMesh);
    if(v->stagingSdf.hasPartitionedHead && v->stagingSdf.axialStationCount==0) {
        float recommended=monster->hasHead?HeadAnatomy_RecommendedVoxelSize(&monster->head.anatomy):.018f;
        v->headMesher.config.voxelSize=Math_Min(.018f,recommended*1.25f);
        MonsterSDFHeadField headContext;SDFField headField=MonsterSDF_GetHeadField(&v->stagingSdf,0,&headContext);
        if(!SDFMesher_GenerateMesh(&v->headMesher,&headField,&v->stagingHeadMesh)||
           !Mesh_KeepLargestComponent(&v->stagingHeadMesh))return false;
    }
    MonsterVisualEye* newEyes=NULL; MonsterVisualMouth* newMouths=NULL;
    if(!GenerateEyes(monster,&newEyes)||!BuildMouthArray(monster,&v->stagingSdf,&newMouths)){FreeEyes(newEyes,monster->eyeCount);return false;}
    MonsterVisualEye* oldEyes=v->eyes; size_t oldEyeCount=v->eyeCount;
    MonsterVisualMouth* oldMouths=v->mouths; size_t oldMouthCount=v->mouthCount;
    MonsterSDF tmpS=v->sdf;v->sdf=v->stagingSdf;v->stagingSdf=tmpS; Mesh tmp=v->mesh;v->mesh=v->stagingMesh;v->stagingMesh=tmp;Mesh tmpHead=v->headMesh;v->headMesh=v->stagingHeadMesh;v->stagingHeadMesh=tmpHead;
    v->eyes=newEyes;v->eyeCount=monster->eyeCount;v->eyeCapacity=v->eyeCount;
    v->mouths=newMouths;v->mouthCount=monster->mouthCount;v->mouthCapacity=v->mouthCount;
    FreeEyes(oldEyes,oldEyeCount);
    for(size_t i=0;i<oldMouthCount;++i)MonsterVisualMouth_Free(&oldMouths[i]);
    free(oldMouths);
    v->mouthVisualFingerprint=HashMouth(monster);v->mouthVisualGeneration++;
    v->geometryFingerprint=HashBody(v,monster,cfg); v->hasFingerprint=true; v->isDirty=false;
    v->updateTimer=0; v->rebuildGeneration++; return true;
}
bool MonsterVisual_Update(MonsterVisual* v,const Monster* m,float dt,float interval,MonsterSDFConfig cfg){if(!v||!m)return false;v->updateTimer+=dt;uint64_t body=HashBody(v,m,cfg);if(v->isDirty||v->mesh.vertexCount==0||body!=v->geometryFingerprint){if(interval>0&&v->mesh.vertexCount>0&&v->updateTimer<interval)return false;return MonsterVisual_RebuildNow(v,m,cfg);}if(HashMouth(m)!=v->mouthVisualFingerprint&&!RebuildMouthsOnly(v,m))return false;for(size_t i=0;i<v->mouthCount;++i)MonsterVisual_UpdateMouthArticulation(&v->mouths[i],&m->mouths[i],m);return false;}
const Mesh* MonsterVisual_GetMesh(const MonsterVisual* v){return v?&v->mesh:NULL;}const Mesh* MonsterVisual_GetHeadMesh(const MonsterVisual* v){return v?&v->headMesh:NULL;} size_t MonsterVisual_GetEyeCount(const MonsterVisual* v){return v?v->eyeCount:0;} const Mesh* MonsterVisual_GetEyeSclera(const MonsterVisual* v,size_t i){return v&&i<v->eyeCount?&v->eyes[i].sclera:NULL;} const Mesh* MonsterVisual_GetEyeIris(const MonsterVisual* v,size_t i){return v&&i<v->eyeCount?&v->eyes[i].iris:NULL;} const Mesh* MonsterVisual_GetEyePupil(const MonsterVisual* v,size_t i){return v&&i<v->eyeCount?&v->eyes[i].pupil:NULL;} size_t MonsterVisual_GetMouthCount(const MonsterVisual* v){return v?v->mouthCount:0;} const Mesh* MonsterVisual_GetJaw(const MonsterVisual* v,size_t i){return v&&i<v->mouthCount?&v->mouths[i].jaw:NULL;} const Mesh* MonsterVisual_GetHinge(const MonsterVisual* v,size_t i){return v&&i<v->mouthCount?&v->mouths[i].hinge:NULL;}
bool MonsterVisual_Render(const MonsterVisual* v,Renderer3D* r){if(!v||!r||!r->renderMesh)return false;r->renderMesh(r,&v->mesh);r->renderMesh(r,&v->headMesh);for(size_t i=0;i<v->mouthCount;++i){r->renderMesh(r,&v->mouths[i].jaw);r->renderMesh(r,&v->mouths[i].hinge);}for(size_t i=0;i<v->eyeCount;++i){r->renderMesh(r,&v->eyes[i].sclera);r->renderMesh(r,&v->eyes[i].iris);r->renderMesh(r,&v->eyes[i].pupil);}return true;}
