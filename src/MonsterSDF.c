#include "MonsterSDF.h"
#include "Monster.h"
#include "SDFPrimitives.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

MonsterSDFConfig MonsterSDF_DefaultConfig(void) {
    return (MonsterSDFConfig){
        .bodySmoothness = 0.5f,
        .connectionSmoothness = 0.4f,
        .mouthSmoothness = 0.25f,
        .connectionRadiusFactor = 0.85f,
        .boundsPadding = 0.7f
    };
}

MonsterSDF MonsterSDF_Create(void) {
    MonsterSDF sdf;
    memset(&sdf, 0, sizeof(MonsterSDF));
    sdf.config = MonsterSDF_DefaultConfig();
    sdf.bounds = AABB_Empty();
    return sdf;
}

void MonsterSDF_Free(MonsterSDF* sdf) {
    if (!sdf) return;
    if (sdf->bodyParts) { free(sdf->bodyParts); sdf->bodyParts = NULL; }
    if (sdf->connectors) { free(sdf->connectors); sdf->connectors = NULL; }
    if (sdf->mouths) { free(sdf->mouths); sdf->mouths = NULL; }
    sdf->bodyPartCount = 0; sdf->bodyPartCapacity = 0;
    sdf->connectorCount = 0; sdf->connectorCapacity = 0;
    sdf->mouthCount = 0; sdf->mouthCapacity = 0;
    sdf->bounds = AABB_Empty();
}

static bool MonsterSDF_EnsureCapacity(void** buffer, size_t elementSize, size_t* capacity, size_t needed) {
    if (needed <= *capacity) return true;
    size_t newCapacity = 0;
    if (!Math_GrowCapacity(*capacity, needed, elementSize, &newCapacity)) return false;
    void* grown = realloc(*buffer, newCapacity * elementSize);
    if (!grown) return false;
    *buffer = grown; *capacity = newCapacity;
    return true;
}

static Vector3 MonsterSDF_LocalToWorld(const MonsterSDFMouth* mouth, Vector3 localPoint) {
    RotationBasis3D basis = mouth->inverseRotation;
    return Vec3_Add(mouth->center, Vec3_Create(
        basis.row0.x * localPoint.x + basis.row1.x * localPoint.y + basis.row2.x * localPoint.z,
        basis.row0.y * localPoint.x + basis.row1.y * localPoint.y + basis.row2.y * localPoint.z,
        basis.row0.z * localPoint.x + basis.row1.z * localPoint.y + basis.row2.z * localPoint.z
    ));
}

/* Helpers anatómicos compartidos */
static void MonsterSDF_GetBasinParams(const MonsterSDFMouth* mouth, Vector3* outCenter, Vector3* outRadii, float* outK) {
    float h = mouth->seamScale;
    if (h < 1e-4f) h = Math_Max(mouth->hingeRadius, Math_Max(mouth->throatRadius, mouth->entranceHalfExtents.y * 2.0f));
    float rx = mouth->jawRadii.x;
    float ry = mouth->jawRadii.y;
    float rz = mouth->jawRadii.z;
    float basinHalfY = h * 0.28f;
    float jawTop = mouth->jawCenterLocal.y + ry;
    float basinTop = jawTop + h * 0.30f;
    float cy = basinTop - basinHalfY;
    Vector3 center = Vec3_Create(mouth->jawCenterLocal.x, cy, mouth->jawCenterLocal.z + rz * 0.05f);
    Vector3 radii = Vec3_Create(rx * 0.60f, basinHalfY, rz * 0.68f);
    float minWallX = h * 0.22f;
    if (rx - radii.x < minWallX) radii.x = Math_Max(rx - minWallX, rx * 0.45f);
    float minWallZ = h * 0.18f;
    if (rz - radii.z < minWallZ) radii.z = Math_Max(rz - minWallZ, rz * 0.50f);
    float k = h * 0.18f;
    if (k < 0.010f) k = 0.010f;
    if (k > h * 0.28f) k = h * 0.28f;
    if (outCenter) *outCenter = center;
    if (outRadii) *outRadii = radii;
    if (outK) *outK = k;
}

bool MonsterSDF_Build(MonsterSDF* sdf, const Monster* monster, MonsterSDFConfig config) {
    if (!sdf) return false;
    sdf->config = config;
    sdf->bounds = AABB_Empty();
    sdf->bodyPartCount = 0; sdf->connectorCount = 0; sdf->mouthCount = 0;
    if (!monster || monster->bodyPartCount == 0) {
        sdf->bounds = AABB_FromMinMax(Vec3_Create(-1.0f, -1.0f, -1.0f), Vec3_Create(1.0f, 1.0f, 1.0f));
        return true;
    }
    /* 1. Partes */
    sdf->bodyPartCount = monster->bodyPartCount;
    if (!MonsterSDF_EnsureCapacity((void**)&sdf->bodyParts, sizeof(MonsterSDFBodyPart), &sdf->bodyPartCapacity, sdf->bodyPartCount)) {
        MonsterSDF_Free(sdf); return false;
    }
    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* part = &monster->bodyParts[i];
        sdf->bodyParts[i].center = part->positionRender;
        float rx = Math_Max(part->widthRender * 0.5f, 0.0001f);
        float ry = Math_Max(part->heightRender * 0.5f, 0.0001f);
        float rz = Math_Max(part->lengthRender * 0.5f, 0.0001f);
        sdf->bodyParts[i].radii = Vec3_Create(rx, ry, rz);
        sdf->bodyParts[i].invRadii = Vec3_Create(1.0f / rx, 1.0f / ry, 1.0f / rz);
        sdf->bodyParts[i].invRadiiSquared = Vec3_Create(
            sdf->bodyParts[i].invRadii.x * sdf->bodyParts[i].invRadii.x,
            sdf->bodyParts[i].invRadii.y * sdf->bodyParts[i].invRadii.y,
            sdf->bodyParts[i].invRadii.z * sdf->bodyParts[i].invRadii.z);
        sdf->bodyParts[i].minRadius = Math_Min(rx, Math_Min(ry, rz));
        sdf->bodyParts[i].color = Monster_GetColorFromIndexStruct(monster, part->color);
        AABB_ExpandRadius(&sdf->bounds, sdf->bodyParts[i].center, sdf->bodyParts[i].radii);
    }
    /* 2. Conectores */
    if (monster->bodyPartCount > 1) {
        sdf->connectorCount = monster->bodyPartCount - 1;
        if (!MonsterSDF_EnsureCapacity((void**)&sdf->connectors, sizeof(MonsterSDFConnector), &sdf->connectorCapacity, sdf->connectorCount)) {
            MonsterSDF_Free(sdf); return false;
        }
        for (size_t i = 0; i < monster->bodyPartCount - 1; ++i) {
            const BodyPart* p1 = &monster->bodyParts[i];
            const BodyPart* p2 = &monster->bodyParts[i + 1];
            float r1 = sqrtf(Math_Max(p1->widthRender * 0.5f * p1->heightRender * 0.5f, 0.0001f)) * config.connectionRadiusFactor;
            float r2 = sqrtf(Math_Max(p2->widthRender * 0.5f * p2->heightRender * 0.5f, 0.0001f)) * config.connectionRadiusFactor;
            sdf->connectors[i].a = p1->positionRender;
            sdf->connectors[i].b = p2->positionRender;
            sdf->connectors[i].ba = Vec3_Sub(p2->positionRender, p1->positionRender);
            float baLenSq = Vec3_Dot(sdf->connectors[i].ba, sdf->connectors[i].ba);
            sdf->connectors[i].invBaLengthSquared = (baLenSq > 1e-8f) ? (1.0f / baLenSq) : 0.0f;
            sdf->connectors[i].r1 = r1; sdf->connectors[i].r2 = r2;
            sdf->connectors[i].radiusDelta = r2 - r1;
            Color c1 = Monster_GetColorFromIndexStruct(monster, p1->color);
            Color c2 = Monster_GetColorFromIndexStruct(monster, p2->color);
            sdf->connectors[i].color = Color_Lerp(c1, c2, 0.5f);
            AABB_ExpandRadius(&sdf->bounds, sdf->connectors[i].a, Vec3_Create(r1, r1, r1));
            AABB_ExpandRadius(&sdf->bounds, sdf->connectors[i].b, Vec3_Create(r2, r2, r2));
        }
    }
    /* 3. Bocas */
    if (monster->mouthCount > 0) {
        sdf->mouthCount = monster->mouthCount;
        if (!MonsterSDF_EnsureCapacity((void**)&sdf->mouths, sizeof(MonsterSDFMouth), &sdf->mouthCapacity, sdf->mouthCount)) {
            MonsterSDF_Free(sdf); return false;
        }
        for (size_t m = 0; m < monster->mouthCount; ++m) {
            HeadAnatomy resolvedHead;
            bool useAnatomicalHead = monster->hasHead && m == 0 &&
                monster->head.anatomy.attachmentBodyPartIndex < monster->bodyPartCount;
            Mouth normalized;
            if (useAnatomicalHead) {
                size_t attachment=monster->head.anatomy.attachmentBodyPartIndex;
                const BodyPart* headHost=&monster->bodyParts[attachment];
                Vector3 headHostRadii=Vec3_Create(headHost->widthRender*.5f,headHost->heightRender*.5f,headHost->lengthRender*.5f);
                if (!HeadAnatomy_Resolve(&monster->head.phenotype,attachment,headHostRadii,&resolvedHead)) return false;
                normalized=resolvedHead.oralSystem;
                normalized.openFactor=monster->mouths[m].openFactor;
                normalized.insideColor=monster->mouths[m].insideColor;
            } else normalized = monster->mouths[m];
            Mouth_Normalize(&normalized);
            const Mouth* mouth = &normalized;
            Vector3 partPos = Vec3_Zero();
            if (mouth->bodyPartIndex < monster->bodyPartCount) partPos = monster->bodyParts[mouth->bodyPartIndex].positionRender;
            Vector3 mouthWorldPos = Vec3_Add(partPos, mouth->offset);
            sdf->mouths[m].center = mouthWorldPos;
            sdf->mouths[m].inverseRotation = Transform3D_BuildInverseRotationBasis(mouth->rotation);
            float width = Math_Max(mouth->scale.x, 0.0001f);
            float maxOpening = Math_Max(mouth->scale.y, 0.0001f);
            float depth = Math_Max(mouth->scale.z, 0.0001f);
            float slitThickness = mouth->slitThickness;
            float cutHalfWidth = Math_Max(width * 0.5f - slitThickness * 0.35f, width * 0.08f);
            float cutHalfHeight = Math_Max(Math_Max(slitThickness * 0.9f, mouth->cornerRadius * 0.9f), 0.004f);
            cutHalfWidth = Math_Max(cutHalfWidth, mouth->cornerRadius);
            float halfDepth = Math_Max(depth * 0.62f, slitThickness * 2.0f);
            sdf->mouths[m].muzzleCenterLocal = Vec3_Create(0.0f, 0.0f, -depth * 0.12f);
            sdf->mouths[m].muzzleHalfExtents = Vec3_Create(width * 0.5f + slitThickness * 1.5f, maxOpening * 0.5f + slitThickness * 1.5f, halfDepth);
            sdf->mouths[m].muzzleSmoothness = Math_Max(config.mouthSmoothness, slitThickness);
            sdf->mouths[m].skinColor = (mouth->bodyPartIndex < monster->bodyPartCount) ? Monster_GetColorFromIndexStruct(monster, monster->bodyParts[mouth->bodyPartIndex].color) : COLOR_WHITE;
            Vector3 hostCenter = Transform3D_ApplyRotationBasis(sdf->mouths[m].inverseRotation, Vec3_Sub(partPos, mouthWorldPos));
            Vector3 hostRadii = Vec3_Create(width, maxOpening, depth);
            if (mouth->bodyPartIndex < monster->bodyPartCount) {
                const BodyPart* host = &monster->bodyParts[mouth->bodyPartIndex];
                hostRadii = Vec3_Create(host->widthRender * 0.5f, host->heightRender * 0.5f, host->lengthRender * 0.5f);
            }
            sdf->mouths[m].hostCenterLocal = hostCenter;
            sdf->mouths[m].hostRadii = hostRadii;
            sdf->mouths[m].craniumCenterLocal = hostCenter;
            sdf->mouths[m].craniumRadii = Vec3_Create(hostRadii.x * mouth->cranium.x, hostRadii.y * mouth->cranium.y, hostRadii.z * mouth->cranium.z);
            sdf->mouths[m].snoutCenterLocal = Vec3_Add(hostCenter, Vec3_Create(0.0f, -hostRadii.y * 0.04f, hostRadii.z * 0.42f));
            sdf->mouths[m].snoutRadii = Vec3_Create(hostRadii.x * mouth->snout.x, hostRadii.y * mouth->snout.y, hostRadii.z * 0.62f * mouth->snout.z);
            sdf->mouths[m].cheekCenterLocal = Vec3_Add(hostCenter, Vec3_Create(hostRadii.x * 0.42f, -hostRadii.y * 0.04f, hostRadii.z * 0.05f));
            sdf->mouths[m].cheekRadii = Vec3_Create(hostRadii.x * 0.62f * mouth->cheeks.x, hostRadii.y * 0.72f * mouth->cheeks.y, hostRadii.z * 0.68f * mouth->cheeks.z);
            sdf->mouths[m].browCenterLocal = Vec3_Add(hostCenter, Vec3_Create(0.0f, hostRadii.y * 0.58f, hostRadii.z * 0.18f));
            sdf->mouths[m].browRadii = Vec3_Create(hostRadii.x * mouth->brows.x, hostRadii.y * 0.28f * mouth->brows.y, hostRadii.z * 0.34f * mouth->brows.z);
            sdf->mouths[m].anatomicalHead = useAnatomicalHead;
            sdf->mouths[m].hasNasalPad = false;
            sdf->mouths[m].hasEars = false;
            if (useAnatomicalHead) {
                const HeadSurfaceRecipe* recipe=&resolvedHead.surface;
                Vector3 origin=mouth->offset;
#define HEAD_LOCAL(value) Vec3_Sub((value),origin)
                sdf->mouths[m].craniumCenterLocal=HEAD_LOCAL(recipe->craniumCenter);
                sdf->mouths[m].craniumRadii=recipe->craniumRadii;
                sdf->mouths[m].faceRootLocal=HEAD_LOCAL(recipe->faceRoot);
                sdf->mouths[m].faceTipLocal=HEAD_LOCAL(recipe->faceTip);
                sdf->mouths[m].faceRootRadii=recipe->faceRootRadii;
                sdf->mouths[m].faceTipRadii=recipe->faceTipRadii;
                sdf->mouths[m].cheekCenterLocal=HEAD_LOCAL(recipe->leftCheekCenter);
                sdf->mouths[m].cheekRadii=recipe->cheekRadii;
                sdf->mouths[m].browCenterLocal=HEAD_LOCAL(recipe->browCenter);
                sdf->mouths[m].browRadii=recipe->browRadii;
                sdf->mouths[m].leftOrbitCenterLocal=HEAD_LOCAL(recipe->leftOrbitCenter);
                sdf->mouths[m].rightOrbitCenterLocal=HEAD_LOCAL(recipe->rightOrbitCenter);
                sdf->mouths[m].orbitRadii=recipe->orbitRadii;
                sdf->mouths[m].leftOrbitRimCenterLocal=HEAD_LOCAL(recipe->leftOrbitRimCenter);
                sdf->mouths[m].rightOrbitRimCenterLocal=HEAD_LOCAL(recipe->rightOrbitRimCenter);
                sdf->mouths[m].orbitRimRadii=recipe->orbitRimRadii;
                sdf->mouths[m].noseCenterLocal=HEAD_LOCAL(recipe->noseCenter);
                sdf->mouths[m].noseRadii=recipe->noseRadii;
                sdf->mouths[m].leftNostrilCenterLocal=HEAD_LOCAL(recipe->leftNostrilCenter);
                sdf->mouths[m].rightNostrilCenterLocal=HEAD_LOCAL(recipe->rightNostrilCenter);
                sdf->mouths[m].nostrilRadii=recipe->nostrilRadii;
                sdf->mouths[m].leftEarCenterLocal=HEAD_LOCAL(recipe->leftEarCenter);
                sdf->mouths[m].rightEarCenterLocal=HEAD_LOCAL(recipe->rightEarCenter);
                sdf->mouths[m].earRadii=recipe->earRadii;
                sdf->mouths[m].headUnionSmoothness=recipe->unionSmoothness;
                sdf->mouths[m].hasNasalPad=recipe->hasNasalPad;
                sdf->mouths[m].hasEars=recipe->hasEars;
#undef HEAD_LOCAL
            }
            float front = hostCenter.z + hostRadii.z + Math_Max(0.02f, slitThickness * 0.5f);
            float rear = hostCenter.z - hostRadii.z * 0.55f;
            float maxRear = front - Math_Max(depth * 0.95f, slitThickness * 4.0f);
            if (rear > maxRear) rear = maxRear;
            halfDepth = Math_Max((front - rear) * 0.5f, slitThickness * 2.0f);
            sdf->mouths[m].entranceCenterLocal = Vec3_Create(0.0f, 0.0f, (front + rear) * 0.5f);
            sdf->mouths[m].entranceHalfExtents = Vec3_Create(cutHalfWidth, cutHalfHeight, halfDepth);
            sdf->mouths[m].cavityCenterLocal = Vec3_Create(0.0f, 0.0f, rear + halfDepth * 0.32f);
            sdf->mouths[m].cavityRadii = Vec3_Create(Math_Max(width * 0.43f, slitThickness * 2.0f), Math_Max(maxOpening * 0.62f, width * 0.18f), Math_Max(depth * 0.48f, slitThickness * 2.0f));
            sdf->mouths[m].insideColor = mouth->insideColor;
            sdf->mouths[m].entranceToCavitySmoothness = Math_Min(maxOpening, depth) * 0.15f;
            sdf->mouths[m].rimBevel = Math_Clamp(slitThickness * (0.15f + mouth->slitSoftness * 0.2f), 0.004f, 0.06f);

            /* --- Geometría mandibular: única finalización antes de anclas/bounds (sin overrides posteriores) --- */
            float slitHx = sdf->mouths[m].entranceHalfExtents.x;
            float slitHy = sdf->mouths[m].entranceHalfExtents.y;
            float h = Math_Max(mouth->hingeRadius, Math_Max(mouth->throatRadius, slitHy * 2.0f));
            if (h < 1e-4f) h = slitHy * 2.0f + 0.02f;
            sdf->mouths[m].seamScale = h;
            float inset = Math_Min(slitHx * 0.12f, h * 0.32f);
            float jawTopY = sdf->mouths[m].entranceCenterLocal.y - slitHy;
            float ry = Math_Max(mouth->jawThickness * 0.5f, Math_Max(h * 0.55f, slitHy * 1.70f));
            ry = Math_Max(ry, mouth->throatRadius * 0.60f);
            // ry scale-relative, sin absolutos
            float rx = Math_Max(mouth->jawWidth * 0.5f, slitHx - inset + h * 0.12f);
            rx = Math_Max(rx, width * 0.34f);
            float maxRx = slitHx * 1.22f + h * 0.18f;
            if (rx > maxRx) rx = maxRx;
            float rearZ = mouth->jawPivot.z;
            float frontZ0 = sdf->mouths[m].entranceCenterLocal.z + sdf->mouths[m].entranceHalfExtents.z * 0.88f;
            float muzzleFront = sdf->mouths[m].muzzleCenterLocal.z + sdf->mouths[m].muzzleHalfExtents.z;
            frontZ0 = Math_Max(frontZ0, muzzleFront + h * 0.18f);
            float derivedLen = frontZ0 - rearZ;
            if (derivedLen < h * 0.6f) derivedLen = h * 0.6f;
            float phenotypeLen = mouth->jawLength;
            float effLen = Math_Max(derivedLen, phenotypeLen * 0.98f);
            float rz = effLen * 0.5f;
            if (rz < h * 0.45f) rz = h * 0.45f;
            float cx = 0.0f;
            float cy = jawTopY - ry;
            float cz = rearZ + effLen * 0.5f;
            sdf->mouths[m].jawCenterLocal = Vec3_Create(cx, cy, cz);
            sdf->mouths[m].jawRadii = Vec3_Create(rx, ry, rz);
            sdf->mouths[m].hingeCenterLocal = mouth->jawPivot;
            sdf->mouths[m].hingeRadius = mouth->hingeRadius;
            sdf->mouths[m].throatRadius = mouth->throatRadius;
            sdf->mouths[m].jawRearMass = mouth->jawRearMass;
            sdf->mouths[m].jawMuscle = mouth->jawMuscle;
            sdf->mouths[m].lowerBeak = mouth->shape == MOUTH_SHAPE_LOWER_BEAK;

            /* --- Anclas posteriores compactas --- */
            float jawHalfW = rx; float jawHalfL = rz;
            Vector3 pivot = mouth->jawPivot;
            // Laterales proporcionales a rx y h
            Vector3 skullL = Vec3_Add(pivot, Vec3_Create(jawHalfW * 0.30f, h * 0.08f, -h * 0.06f));
            Vector3 skullR = Vec3_Add(pivot, Vec3_Create(-jawHalfW * 0.30f, h * 0.08f, -h * 0.06f));
            Vector3 jawL = Vec3_Add(pivot, Vec3_Create(jawHalfW * 0.38f, -h * 0.42f, jawHalfL * 0.42f));
            Vector3 jawR = Vec3_Add(pivot, Vec3_Create(-jawHalfW * 0.38f, -h * 0.42f, jawHalfL * 0.42f));
            Vector3 gular = Vec3_Add(pivot, Vec3_Create(0.0f, -mouth->throatRadius * 0.50f, jawHalfL * 0.18f));
            Vector3 jawAnchor = Vec3_Add(pivot, Vec3_Create(0.0f, -h * 0.18f, jawHalfL * 0.28f));
            sdf->mouths[m].seamSkullLeftLocal = skullL;
            sdf->mouths[m].seamSkullRightLocal = skullR;
            sdf->mouths[m].seamJawLeftClosedLocal = jawL;
            sdf->mouths[m].seamJawRightClosedLocal = jawR;
            sdf->mouths[m].seamGularLocal = gular;
            sdf->mouths[m].seamJawAnchorLocal = jawAnchor;

            /* --- Bounds de seam: sólo primitivas finales, margen scale-relative --- */
            {
                Vector3 sMin = skullL; Vector3 sMax = skullL;
                Vector3 pts[6] = {skullR, jawL, jawR, gular, jawAnchor, pivot};
                for (int pi = 0; pi < 6; ++pi) {
                    if (pts[pi].x < sMin.x) sMin.x = pts[pi].x;
                    if (pts[pi].y < sMin.y) sMin.y = pts[pi].y;
                    if (pts[pi].z < sMin.z) sMin.z = pts[pi].z;
                    if (pts[pi].x > sMax.x) sMax.x = pts[pi].x;
                    if (pts[pi].y > sMax.y) sMax.y = pts[pi].y;
                    if (pts[pi].z > sMax.z) sMax.z = pts[pi].z;
                }
                // expansión por radios de cápsulas/pads scale-relative
                float rHinge = h * 0.34f;
                float rGular = h * 0.30f;
                float rMax = Math_Max(rHinge, rGular) + h * 0.28f;
                // gular pad elipsoide
                Vector3 gHalf = Vec3_Create(h * 0.62f, h * 0.55f, h * 0.60f);
                Vector3 gMin = Vec3_Sub(gular, gHalf);
                Vector3 gMax = Vec3_Add(gular, gHalf);
                if (gMin.x < sMin.x) sMin.x = gMin.x;
                if (gMin.y < sMin.y) sMin.y = gMin.y;
                if (gMin.z < sMin.z) sMin.z = gMin.z;
                if (gMax.x > sMax.x) sMax.x = gMax.x;
                if (gMax.y > sMax.y) sMax.y = gMax.y;
                if (gMax.z > sMax.z) sMax.z = gMax.z;
                sMin = Vec3_Sub(sMin, Vec3_Create(rMax, rMax, rMax));
                sMax = Vec3_Add(sMax, Vec3_Create(rMax, rMax, rMax));
                sdf->mouths[m].seamBounds = AABB_FromMinMax(sMin, sMax);
            }

            RotationBasis3D invRot = sdf->mouths[m].inverseRotation;
            float rxInf = Math_Max(width * 0.8f, fabsf(sdf->mouths[m].muzzleCenterLocal.x) + sdf->mouths[m].muzzleHalfExtents.x);
            float ryInf = Math_Max(maxOpening * 0.8f, fabsf(sdf->mouths[m].muzzleCenterLocal.y) + sdf->mouths[m].muzzleHalfExtents.y);
            float rzInf = Math_Max(depth * 1.2f, fabsf(sdf->mouths[m].muzzleCenterLocal.z) + sdf->mouths[m].muzzleHalfExtents.z);
            rxInf = Math_Max(rxInf, fabsf(sdf->mouths[m].cavityCenterLocal.x) + sdf->mouths[m].cavityRadii.x);
            ryInf = Math_Max(ryInf, fabsf(sdf->mouths[m].cavityCenterLocal.y) + sdf->mouths[m].cavityRadii.y);
            rzInf = Math_Max(rzInf, fabsf(sdf->mouths[m].cavityCenterLocal.z) + sdf->mouths[m].cavityRadii.z);
            rxInf = Math_Max(rxInf, fabsf(sdf->mouths[m].seamBounds.end.x));
            ryInf = Math_Max(ryInf, fabsf(sdf->mouths[m].seamBounds.end.y));
            rzInf = Math_Max(rzInf, fabsf(sdf->mouths[m].seamBounds.end.z));
            // incluir jaw
            rxInf = Math_Max(rxInf, fabsf(sdf->mouths[m].jawCenterLocal.x) + sdf->mouths[m].jawRadii.x + h*0.35f);
            ryInf = Math_Max(ryInf, fabsf(sdf->mouths[m].jawCenterLocal.y) + sdf->mouths[m].jawRadii.y + h*0.35f);
            rzInf = Math_Max(rzInf, fabsf(sdf->mouths[m].jawCenterLocal.z) + sdf->mouths[m].jawRadii.z + h*0.35f);
            if (useAnatomicalHead) {
#define INCLUDE_ELLIPSOID(centerValue,radiiValue) do { \
    rxInf=Math_Max(rxInf,fabsf((centerValue).x)+(radiiValue).x); \
    ryInf=Math_Max(ryInf,fabsf((centerValue).y)+(radiiValue).y); \
    rzInf=Math_Max(rzInf,fabsf((centerValue).z)+(radiiValue).z); \
} while(0)
                INCLUDE_ELLIPSOID(sdf->mouths[m].craniumCenterLocal,sdf->mouths[m].craniumRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].faceRootLocal,sdf->mouths[m].faceRootRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].faceTipLocal,sdf->mouths[m].faceTipRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].leftOrbitRimCenterLocal,sdf->mouths[m].orbitRimRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].rightOrbitRimCenterLocal,sdf->mouths[m].orbitRimRadii);
                if(sdf->mouths[m].hasEars) {
                    INCLUDE_ELLIPSOID(sdf->mouths[m].leftEarCenterLocal,sdf->mouths[m].earRadii);
                    INCLUDE_ELLIPSOID(sdf->mouths[m].rightEarCenterLocal,sdf->mouths[m].earRadii);
                }
#undef INCLUDE_ELLIPSOID
            }
            float extX = fabsf(invRot.row0.x) * rxInf + fabsf(invRot.row1.x) * ryInf + fabsf(invRot.row2.x) * rzInf;
            float extY = fabsf(invRot.row0.y) * rxInf + fabsf(invRot.row1.y) * ryInf + fabsf(invRot.row2.y) * rzInf;
            float extZ = fabsf(invRot.row0.z) * rxInf + fabsf(invRot.row1.z) * ryInf + fabsf(invRot.row2.z) * rzInf;
            float mouthPad = h * 0.45f + 0.02f;
            extX += mouthPad; extY += mouthPad; extZ += mouthPad;
            sdf->mouths[m].influenceBounds = (AABB3D){ .start = Vec3_Sub(mouthWorldPos, Vec3_Create(extX, extY, extZ)), .end = Vec3_Add(mouthWorldPos, Vec3_Create(extX, extY, extZ)) };
            AABB_ExpandRadius(&sdf->bounds,mouthWorldPos,Vec3_Create(extX,extY,extZ));
            Vector3 muzzleCenter = MonsterSDF_LocalToWorld(&sdf->mouths[m], sdf->mouths[m].muzzleCenterLocal);
            float muzzleX = fabsf(invRot.row0.x) * sdf->mouths[m].muzzleHalfExtents.x + fabsf(invRot.row1.x) * sdf->mouths[m].muzzleHalfExtents.y + fabsf(invRot.row2.x) * sdf->mouths[m].muzzleHalfExtents.z + sdf->mouths[m].muzzleSmoothness;
            float muzzleY = fabsf(invRot.row0.y) * sdf->mouths[m].muzzleHalfExtents.x + fabsf(invRot.row1.y) * sdf->mouths[m].muzzleHalfExtents.y + fabsf(invRot.row2.y) * sdf->mouths[m].muzzleHalfExtents.z + sdf->mouths[m].muzzleSmoothness;
            float muzzleZ = fabsf(invRot.row0.z) * sdf->mouths[m].muzzleHalfExtents.x + fabsf(invRot.row1.z) * sdf->mouths[m].muzzleHalfExtents.y + fabsf(invRot.row2.z) * sdf->mouths[m].muzzleHalfExtents.z + sdf->mouths[m].muzzleSmoothness;
            AABB_ExpandRadius(&sdf->bounds, muzzleCenter, Vec3_Create(muzzleX, muzzleY, muzzleZ));
        }
    }
    float pad = config.boundsPadding + config.bodySmoothness;
    AABB_Pad(&sdf->bounds, pad);
    return true;
}

static inline float MonsterSDF_EvalBodyPartDistance(const MonsterSDFBodyPart* part, Vector3 point) {
    Vector3 pLocal = Vec3_Sub(point, part->center);
    Vector3 scaledP = Vec3_Create(pLocal.x * part->invRadii.x, pLocal.y * part->invRadii.y, pLocal.z * part->invRadii.z);
    float k0 = Vec3_Length(scaledP);
    Vector3 scaledP2 = Vec3_Create(pLocal.x * part->invRadiiSquared.x, pLocal.y * part->invRadiiSquared.y, pLocal.z * part->invRadiiSquared.z);
    float k1 = Vec3_Length(scaledP2);
    if (k0 < 1e-6f || k1 < 1e-6f) return -part->minRadius;
    return k0 * (k0 - 1.0f) / k1;
}
static inline float MonsterSDF_EvalConnectorDistance(const MonsterSDFConnector* conn, Vector3 point) {
    Vector3 pa = Vec3_Sub(point, conn->a);
    if (conn->invBaLengthSquared <= 0.0f) return Vec3_Length(pa) - conn->r1;
    float h = Math_Clamp01(Vec3_Dot(pa, conn->ba) * conn->invBaLengthSquared);
    float radius = conn->r1 + conn->radiusDelta * h;
    Vector3 projection = Vec3_Sub(pa, Vec3_Scale(conn->ba, h));
    return Vec3_Length(projection) - radius;
}
static inline float MonsterSDF_EvalMouthDistance(const MonsterSDFMouth* mouth, Vector3 point, Vector3* outLocalP) {
    Vector3 translated = Vec3_Sub(point, mouth->center);
    Vector3 localP = Transform3D_ApplyRotationBasis(mouth->inverseRotation, translated);
    if (outLocalP) *outLocalP = localP;
    Vector3 pEntrance = Vec3_Sub(localP, mouth->entranceCenterLocal);
    float entranceDist = SDF_RoundedSlotExtruded(pEntrance, mouth->entranceHalfExtents.x, mouth->entranceHalfExtents.y, mouth->entranceHalfExtents.z);
    Vector3 pCavity = Vec3_Sub(localP, mouth->cavityCenterLocal);
    float cavityDist = SDF_Ellipsoid(pCavity, mouth->cavityRadii);
    return SDF_SmoothUnion(entranceDist, cavityDist, mouth->entranceToCavitySmoothness);
}
static float MonsterSDF_EvalMuzzleDistance(const MonsterSDFMouth* mouth, Vector3 localP) {
    return SDF_Ellipsoid(Vec3_Sub(localP, mouth->muzzleCenterLocal), mouth->muzzleHalfExtents);
}
static float MonsterSDF_EvalHeadCavities(const MonsterSDFMouth* mouth,Vector3 localP) {
    if(!mouth->anatomicalHead) return 1e6f;
    float d=SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftOrbitCenterLocal),mouth->orbitRadii);
    d=Math_Min(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightOrbitCenterLocal),mouth->orbitRadii));
    d=Math_Min(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftNostrilCenterLocal),mouth->nostrilRadii));
    return Math_Min(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightNostrilCenterLocal),mouth->nostrilRadii));
}
static float MonsterSDF_EvalJawBase(const MonsterSDFMouth* mouth, Vector3 localP) {
    if(mouth->lowerBeak) {
        Vector3 root=Vec3_Add(mouth->hingeCenterLocal,Vec3_Create(0,-mouth->jawRadii.y*.12f,mouth->jawRadii.z*.08f));
        Vector3 tip=Vec3_Create(mouth->jawCenterLocal.x,mouth->jawCenterLocal.y,mouth->jawCenterLocal.z+mouth->jawRadii.z);
        Vector3 rootRadii=Vec3_Create(mouth->jawRadii.x,mouth->jawRadii.y,mouth->jawRadii.x*.42f);
        Vector3 tipRadii=Vec3_Create(mouth->jawRadii.x*.20f,mouth->jawRadii.y*.28f,mouth->jawRadii.x*.18f);
        return SDF_TaperedEllipticalCapsuleApprox(localP,root,tip,rootRadii,tipRadii);
    }
    // Composición anatómica redondeada: elipsoide principal + masa posterior + músculo inferior
    Vector3 dMain = Vec3_Sub(localP, mouth->jawCenterLocal);
    Vector3 radiiMain = Vec3_Create(mouth->jawRadii.x, mouth->jawRadii.y, mouth->jawRadii.z * 0.96f);
    float main = SDF_Ellipsoid(dMain, radiiMain);
    Vector3 rearCenter = Vec3_Add(mouth->hingeCenterLocal, Vec3_Create(0.0f, -mouth->jawRadii.y * 0.14f, mouth->jawRadii.z * 0.22f));
    float rearR = mouth->jawRearMass;
    // elipsoide posterior scale-relative
    Vector3 rearRadii = Vec3_Create(rearR * 0.95f, rearR * 0.85f, rearR * 0.92f);
    float rear = SDF_Ellipsoid(Vec3_Sub(localP, rearCenter), rearRadii);
    Vector3 muscleCenter = Vec3_Add(mouth->jawCenterLocal, Vec3_Create(0.0f, -mouth->jawMuscle * 0.38f, -mouth->jawRadii.z * 0.10f));
    Vector3 muscleRadii = Vec3_Create(mouth->jawRadii.x * 0.84f + mouth->jawMuscle * 0.12f, mouth->jawMuscle * 0.52f, mouth->jawRadii.z * 0.42f);
    float muscle = SDF_Ellipsoid(Vec3_Sub(localP, muscleCenter), muscleRadii);
    float h = mouth->seamScale; if (h < 1e-4f) h = mouth->hingeRadius;
    float k = h * 0.18f; if (k < 0.012f) k = 0.012f; if (k > h * 0.32f) k = h * 0.32f;
    float result = SDF_SmoothUnion(main, rear, k);
    result = SDF_SmoothUnion(result, muscle, k);
    return result;
}
static float MonsterSDF_EvalSeamDistance(const MonsterSDFMouth* mouth, Vector3 localP) {
    float h = mouth->seamScale;
    if (h < 1e-4f) h = Math_Max(mouth->hingeRadius, Math_Max(mouth->throatRadius, mouth->entranceHalfExtents.y * 2.0f));
    float k = h * 0.26f; if (k < 0.012f) k = 0.012f; if (k > h*0.38f) k = h*0.38f;
    float rHinge = h * 0.26f;
    float rGular = h * 0.30f;
    float rPivotGular = h * 0.28f;
    float left = SDF_Capsule(localP, mouth->seamSkullLeftLocal, mouth->seamJawLeftClosedLocal, rHinge);
    float right = SDF_Capsule(localP, mouth->seamSkullRightLocal, mouth->seamJawRightClosedLocal, rHinge);
    float jlG = SDF_Capsule(localP, mouth->seamJawLeftClosedLocal, mouth->seamGularLocal, rGular);
    float jrG = SDF_Capsule(localP, mouth->seamJawRightClosedLocal, mouth->seamGularLocal, rGular);
    float midG = SDF_Capsule(localP, mouth->seamJawAnchorLocal, mouth->seamGularLocal, rGular);
    float pivotG = SDF_Capsule(localP, mouth->hingeCenterLocal, mouth->seamGularLocal, rPivotGular);
    Vector3 gHalf = Vec3_Create(h * 0.74f, h * 0.68f, h * 0.72f);
    float gular = SDF_Ellipsoid(Vec3_Sub(localP, mouth->seamGularLocal), gHalf);
    float pivotPad = SDF_Sphere(Vec3_Sub(localP, mouth->hingeCenterLocal), h * 0.30f);
    float res = SDF_SmoothUnion(left, right, k);
    res = SDF_SmoothUnion(res, jlG, k);
    res = SDF_SmoothUnion(res, jrG, k);
    res = SDF_SmoothUnion(res, midG, k);
    res = SDF_SmoothUnion(res, pivotG, k);
    res = SDF_SmoothUnion(res, gular, k);
    res = SDF_SmoothUnion(res, pivotPad, k);
    return res;
}
static float MonsterSDF_EvalUpperHeadDistance(const MonsterSDFMouth* mouth, Vector3 localP) {
    if (mouth->anatomicalHead) {
        float k=Math_Max(mouth->headUnionSmoothness,.005f);
        float d=SDF_Ellipsoid(Vec3_Sub(localP,mouth->craniumCenterLocal),mouth->craniumRadii);
        float face=SDF_TaperedEllipticalCapsuleApprox(localP,mouth->faceRootLocal,mouth->faceTipLocal,mouth->faceRootRadii,mouth->faceTipRadii);
        d=SDF_SmoothUnion(d,face,k);
        d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->cheekCenterLocal),mouth->cheekRadii),k*.65f);
        Vector3 rightCheek=mouth->cheekCenterLocal; rightCheek.x*=-1.0f;
        d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,rightCheek),mouth->cheekRadii),k*.65f);
        d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->browCenterLocal),mouth->browRadii),k*.55f);
        d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftOrbitRimCenterLocal),mouth->orbitRimRadii),k*.35f);
        d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightOrbitRimCenterLocal),mouth->orbitRimRadii),k*.35f);
        if(mouth->hasNasalPad) d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->noseCenterLocal),mouth->noseRadii),k*.35f);
        if(mouth->hasEars) {
            d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftEarCenterLocal),mouth->earRadii),k*.28f);
            d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightEarCenterLocal),mouth->earRadii),k*.28f);
        }
        float leftOrbit=SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftOrbitCenterLocal),mouth->orbitRadii);
        float rightOrbit=SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightOrbitCenterLocal),mouth->orbitRadii);
        d=SDF_SmoothSubtract(d,leftOrbit,k*.12f);
        d=SDF_SmoothSubtract(d,rightOrbit,k*.12f);
        float leftNostril=SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftNostrilCenterLocal),mouth->nostrilRadii);
        float rightNostril=SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightNostrilCenterLocal),mouth->nostrilRadii);
        d=SDF_SmoothSubtract(d,leftNostril,k*.08f);
        d=SDF_SmoothSubtract(d,rightNostril,k*.08f);
        return d;
    }
    float d = SDF_Ellipsoid(Vec3_Sub(localP, mouth->craniumCenterLocal), mouth->craniumRadii);
    d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->snoutCenterLocal), mouth->snoutRadii), 0.08f);
    d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->cheekCenterLocal), mouth->cheekRadii), 0.06f);
    d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, Vec3_Create(-mouth->cheekCenterLocal.x, mouth->cheekCenterLocal.y, mouth->cheekCenterLocal.z)), mouth->cheekRadii), 0.06f);
    return SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->browCenterLocal), mouth->browRadii), 0.05f);
}

SDFSample MonsterSDF_Evaluate(const MonsterSDF* sdf, Vector3 point) {
    if (!sdf || sdf->bodyPartCount == 0) return SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_UNKNOWN);
    SDFSample accumulated = SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_SKIN);
    bool hasInitialSample = false;
    for (size_t i = 0; i < sdf->bodyPartCount; ++i) {
        const MonsterSDFBodyPart* part = &sdf->bodyParts[i];
        float dist = MonsterSDF_EvalBodyPartDistance(part, point);
        SDFSample partSample = SDFSample_Create(dist, part->color, SDF_MATERIAL_SKIN);
        if (!hasInitialSample) { accumulated = partSample; hasInitialSample = true; }
        else accumulated = SDFSample_SmoothUnion(accumulated, partSample, sdf->config.bodySmoothness);
    }
    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        const MonsterSDFConnector* conn = &sdf->connectors[i];
        float dist = MonsterSDF_EvalConnectorDistance(conn, point);
        SDFSample connSample = SDFSample_Create(dist, conn->color, SDF_MATERIAL_SKIN);
        if (!hasInitialSample) { accumulated = connSample; hasInitialSample = true; }
        else accumulated = SDFSample_SmoothUnion(accumulated, connSample, sdf->config.connectionSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        if (!AABB_ContainsPoint(mouth->influenceBounds, point)) continue;
        Vector3 localP = Transform3D_ApplyRotationBasis(mouth->inverseRotation, Vec3_Sub(point, mouth->center));
        if (!mouth->anatomicalHead) {
            float muzzleDist = MonsterSDF_EvalMuzzleDistance(mouth, localP);
            SDFSample muzzleSample = SDFSample_Create(muzzleDist, mouth->skinColor, SDF_MATERIAL_SKIN);
            accumulated = SDFSample_SmoothUnion(accumulated, muzzleSample, mouth->muzzleSmoothness);
        }
        float upperHeadDist = MonsterSDF_EvalUpperHeadDistance(mouth, localP);
        accumulated = SDFSample_SmoothUnion(accumulated, SDFSample_Create(upperHeadDist, mouth->skinColor, SDF_MATERIAL_SKIN), mouth->muzzleSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        if (!AABB_ContainsPoint(mouth->influenceBounds, point)) continue;
        Vector3 localP; float cutterDist = MonsterSDF_EvalMouthDistance(mouth, point, &localP);
        SDFSample cutterSample = SDFSample_Create(cutterDist, mouth->insideColor, SDF_MATERIAL_MOUTH);
        accumulated = SDFSample_Subtract(accumulated, cutterSample, mouth->rimBevel);
        if(mouth->anatomicalHead) {
            float headCavities=MonsterSDF_EvalHeadCavities(mouth,localP);
            accumulated=SDFSample_Subtract(accumulated,SDFSample_Create(headCavities,mouth->insideColor,SDF_MATERIAL_MOUTH),mouth->headUnionSmoothness*.08f);
        }
    }
    return accumulated;
}

float MonsterSDF_EvaluateDistance(const MonsterSDF* sdf, Vector3 point) {
    if (!sdf || sdf->bodyPartCount == 0) return 1e6f;
    float accumulated = 1e6f; bool hasInitial = false;
    for (size_t i = 0; i < sdf->bodyPartCount; ++i) {
        float dist = MonsterSDF_EvalBodyPartDistance(&sdf->bodyParts[i], point);
        if (!hasInitial) { accumulated = dist; hasInitial = true; }
        else accumulated = SDF_SmoothUnion(accumulated, dist, sdf->config.bodySmoothness);
    }
    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        float dist = MonsterSDF_EvalConnectorDistance(&sdf->connectors[i], point);
        if (!hasInitial) { accumulated = dist; hasInitial = true; }
        else accumulated = SDF_SmoothUnion(accumulated, dist, sdf->config.connectionSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        if (!AABB_ContainsPoint(mouth->influenceBounds, point)) continue;
        Vector3 localP = Transform3D_ApplyRotationBasis(mouth->inverseRotation, Vec3_Sub(point, mouth->center));
        if (!mouth->anatomicalHead) {
            float muzzleDist = MonsterSDF_EvalMuzzleDistance(mouth, localP);
            accumulated = SDF_SmoothUnion(accumulated, muzzleDist, mouth->muzzleSmoothness);
        }
        accumulated = SDF_SmoothUnion(accumulated, MonsterSDF_EvalUpperHeadDistance(mouth, localP), mouth->muzzleSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        if (!AABB_ContainsPoint(mouth->influenceBounds, point)) continue;
        float cutterDist = MonsterSDF_EvalMouthDistance(mouth, point, NULL);
        accumulated = SDF_SmoothSubtract(accumulated, cutterDist, mouth->rimBevel);
        if(mouth->anatomicalHead) {
            Vector3 localP=Transform3D_ApplyRotationBasis(mouth->inverseRotation,Vec3_Sub(point,mouth->center));
            accumulated=SDF_SmoothSubtract(accumulated,MonsterSDF_EvalHeadCavities(mouth,localP),mouth->headUnionSmoothness*.08f);
        }
    }
    return accumulated;
}

SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point) { return MonsterSDF_Evaluate((const MonsterSDF*)context, point); }
static float MonsterSDF_EvalJawCarvedDistance(const MonsterSDFMouth* mouth, Vector3 point);
static float MonsterSDF_EvalSeamDistance(const MonsterSDFMouth* mouth, Vector3 localP);
SDFSample MonsterSDF_EvaluateDebug(const MonsterSDF* sdf, Vector3 point, MonsterHeadDebugMode mode) {
    if (!sdf || sdf->mouthCount == 0 || mode == MONSTER_HEAD_DEBUG_FULL) return MonsterSDF_Evaluate(sdf, point);
    SDFSample result = SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_UNKNOWN);
    for (size_t i = 0; i < sdf->mouthCount; ++i) {
        const MonsterSDFMouth* mouth = &sdf->mouths[i];
        if (!AABB_ContainsPoint(mouth->influenceBounds, point)) continue;
        Vector3 local = Transform3D_ApplyRotationBasis(mouth->inverseRotation, Vec3_Sub(point, mouth->center));
        float distance = 1e6f; SDFMaterial material = SDF_MATERIAL_MOUTH; Color color = mouth->insideColor;
        switch (mode) {
            case MONSTER_HEAD_DEBUG_CRANIUM: distance = SDF_Ellipsoid(Vec3_Sub(local, mouth->craniumCenterLocal), mouth->craniumRadii); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_SNOUT: distance = MonsterSDF_EvalMuzzleDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_UPPER_HEAD: distance = MonsterSDF_EvalUpperHeadDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_JAW: distance = MonsterSDF_EvalJawCarvedDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_BRIDGES: distance = MonsterSDF_EvalSeamDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_CAVITY: distance = SDF_Ellipsoid(Vec3_Sub(local, mouth->cavityCenterLocal), mouth->cavityRadii); break;
            case MONSTER_HEAD_DEBUG_SLIT: distance = SDF_RoundedSlotExtruded(Vec3_Sub(local, mouth->entranceCenterLocal), mouth->entranceHalfExtents.x, mouth->entranceHalfExtents.y, mouth->entranceHalfExtents.z); break;
            default: break;
        }
        if (distance < result.distance) result = SDFSample_Create(distance, color, material);
    }
    return result;
}

static float MonsterSDF_EvalJawCarvedDistance(const MonsterSDFMouth* mouth, Vector3 point) {
    float body = MonsterSDF_EvalJawBase(mouth, point);
    Vector3 basinCenter, basinRadii; float k;
    MonsterSDF_GetBasinParams(mouth, &basinCenter, &basinRadii, &k);
    float oral = SDF_Ellipsoid(Vec3_Sub(point, basinCenter), basinRadii);
    return SDF_SmoothSubtract(body, oral, k);
}
static SDFSample MonsterSDF_EvaluateJawWrapper(const void* context, Vector3 point) {
    const MonsterSDFJawField* field=(const MonsterSDFJawField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount) return SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_UNKNOWN);
    const MonsterSDFMouth* mouth=&field->owner->mouths[field->mouthIndex];
    float body = MonsterSDF_EvalJawBase(mouth, point);
    SDFSample sample = SDFSample_Create(body, mouth->skinColor, SDF_MATERIAL_SKIN);
    Vector3 basinCenter, basinRadii; float k;
    MonsterSDF_GetBasinParams(mouth, &basinCenter, &basinRadii, &k);
    float oral = SDF_Ellipsoid(Vec3_Sub(point, basinCenter), basinRadii);
    return SDFSample_Subtract(sample, SDFSample_Create(oral, mouth->insideColor, SDF_MATERIAL_MOUTH), k);
}
static float MonsterSDF_EvaluateJawDistanceWrapper(const void* context, Vector3 point) {
    const MonsterSDFJawField* field=(const MonsterSDFJawField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount) return 1e6f;
    return MonsterSDF_EvalJawCarvedDistance(&field->owner->mouths[field->mouthIndex], point);
}
static AABB3D MonsterSDF_GetJawBoundsWrapper(const void* context) {
    const MonsterSDFJawField* field=(const MonsterSDFJawField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount) return AABB_Empty();
    const MonsterSDFMouth* mouth=&field->owner->mouths[field->mouthIndex];
    Vector3 r=mouth->jawRadii;
    float h = mouth->seamScale; if (h<1e-4f) h=Math_Max(mouth->hingeRadius,Math_Max(mouth->throatRadius,mouth->entranceHalfExtents.y*2.0f));
    float pad = h*0.38f + Math_Max(mouth->jawRearMass, mouth->jawMuscle)*0.12f;
    Vector3 minJ = Vec3_Sub(Vec3_Sub(mouth->jawCenterLocal, r), Vec3_Create(pad, pad, pad));
    Vector3 maxJ = Vec3_Add(Vec3_Add(mouth->jawCenterLocal, r), Vec3_Create(pad, pad, pad));
    Vector3 bCenter, bRadii; float bk;
    MonsterSDF_GetBasinParams(mouth, &bCenter, &bRadii, &bk);
    float basinTop = bCenter.y + bRadii.y;
    if (basinTop > maxJ.y) maxJ.y = basinTop + h*0.08f;
    Vector3 rearC = Vec3_Add(mouth->hingeCenterLocal, Vec3_Create(0, -mouth->jawRadii.y*0.14f, mouth->jawRadii.z*0.22f));
    float rearPad = mouth->jawRearMass + h*0.12f;
    Vector3 rearR = Vec3_Create(rearC.x - rearPad, rearC.y - rearPad, rearC.z - rearPad);
    Vector3 rearM = Vec3_Create(rearC.x + rearPad, rearC.y + rearPad, rearC.z + rearPad);
    if (rearR.x < minJ.x) minJ.x = rearR.x;
    if (rearR.y < minJ.y) minJ.y = rearR.y;
    if (rearR.z < minJ.z) minJ.z = rearR.z;
    if (rearM.x > maxJ.x) maxJ.x = rearM.x;
    if (rearM.y > maxJ.y) maxJ.y = rearM.y;
    if (rearM.z > maxJ.z) maxJ.z = rearM.z;
    // sin cápsulas frontales eliminadas: solo primitivas reales
    return AABB_FromMinMax(minJ, maxJ);
}
SDFField MonsterSDF_GetJawField(const MonsterSDF* sdf, size_t mouthIndex, MonsterSDFJawField* context) {
    if (!context) return (SDFField){0};
    context->owner=sdf; context->mouthIndex=mouthIndex;
    return (SDFField){.evaluate=MonsterSDF_EvaluateJawWrapper,.evaluateDistance=MonsterSDF_EvaluateJawDistanceWrapper,.getBounds=MonsterSDF_GetJawBoundsWrapper,.context=context};
}
static SDFSample MonsterSDF_EvaluateSeamWrapper(const void* context, Vector3 point) {
    const MonsterSDFSeamField* field=(const MonsterSDFSeamField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount) return SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_UNKNOWN);
    const MonsterSDFMouth* mouth=&field->owner->mouths[field->mouthIndex];
    float d = MonsterSDF_EvalSeamDistance(mouth, point);
    return SDFSample_Create(d, mouth->skinColor, SDF_MATERIAL_SKIN);
}
static float MonsterSDF_EvaluateSeamDistanceWrapper(const void* context, Vector3 point) {
    const MonsterSDFSeamField* field=(const MonsterSDFSeamField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount) return 1e6f;
    return MonsterSDF_EvalSeamDistance(&field->owner->mouths[field->mouthIndex], point);
}
static AABB3D MonsterSDF_GetSeamBoundsWrapper(const void* context) {
    const MonsterSDFSeamField* field=(const MonsterSDFSeamField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount) return AABB_Empty();
    return field->owner->mouths[field->mouthIndex].seamBounds;
}
SDFField MonsterSDF_GetSeamField(const MonsterSDF* sdf, size_t mouthIndex, MonsterSDFSeamField* context) {
    if (!context) return (SDFField){0};
    context->owner=sdf; context->mouthIndex=mouthIndex;
    return (SDFField){.evaluate=MonsterSDF_EvaluateSeamWrapper,.evaluateDistance=MonsterSDF_EvaluateSeamDistanceWrapper,.getBounds=MonsterSDF_GetSeamBoundsWrapper,.context=context};
}
float MonsterSDF_EvaluateDistanceWrapper(const void* context, Vector3 point) { return MonsterSDF_EvaluateDistance((const MonsterSDF*)context, point); }
AABB3D MonsterSDF_GetBounds(const MonsterSDF* sdf) { if (!sdf) return AABB_Empty(); return sdf->bounds; }
AABB3D MonsterSDF_GetBoundsWrapper(const void* context) { return MonsterSDF_GetBounds((const MonsterSDF*)context); }
SDFField MonsterSDF_GetField(const MonsterSDF* sdf) {
    return (SDFField){ .evaluate = MonsterSDF_EvaluateWrapper, .evaluateDistance = MonsterSDF_EvaluateDistanceWrapper, .getBounds = MonsterSDF_GetBoundsWrapper, .context = (const void*)sdf };
}
