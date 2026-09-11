#include "MonsterSDF.h"
#include "Monster.h"
#include "SDFPrimitives.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void MonsterSDF_ResolveVisualBounds(MonsterSDF* sdf,size_t index);

MonsterSDFConfig MonsterSDF_DefaultConfig(void) {
    return (MonsterSDFConfig){
        .bodySmoothness = 0.5f,
        .connectionSmoothness = 0.4f,
        .mouthSmoothness = 0.25f,
        .connectionRadiusFactor = 0.85f,
        .boundsPadding = 0.7f,
        .enableConnectorPruning = true
    };
}

static __thread bool tls_enableStats = false;
static __thread size_t tls_connectorCandidateCount = 0;
static __thread size_t tls_connectorExactCount = 0;
static __thread size_t tls_connectorPrunedCount = 0;

void MonsterSDF_EnableThreadStats(bool enable) {
    tls_enableStats = enable;
}

void MonsterSDF_ResetThreadStats(void) {
    tls_connectorCandidateCount = 0;
    tls_connectorExactCount = 0;
    tls_connectorPrunedCount = 0;
}

void MonsterSDF_GetThreadStats(size_t* outCandidate, size_t* outExact, size_t* outPruned) {
    if (outCandidate) *outCandidate = tls_connectorCandidateCount;
    if (outExact) *outExact = tls_connectorExactCount;
    if (outPruned) *outPruned = tls_connectorPrunedCount;
}

MonsterSDF MonsterSDF_Create(void) {
    MonsterSDF sdf;
    memset(&sdf, 0, sizeof(MonsterSDF));
    sdf.config = MonsterSDF_DefaultConfig();
    sdf.bounds = AABB_Empty();
    sdf.bodyBounds = AABB_Empty();
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
    sdf->bodyBounds = AABB_Empty();
    sdf->hasPartitionedHead = false;
    sdf->appendageDevelopment=1.0f;
    sdf->axialStationCount=0;
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
    sdf->bodyBounds = AABB_Empty();
    sdf->hasPartitionedHead = false;
    sdf->appendageDevelopment=monster&&monster->hasLizardPhenotype?
        monster->lizardPhenotype.appendageDevelopment:1.0f;
    sdf->axialStationCount=0;
    sdf->bodyPartCount = 0; sdf->connectorCount = 0; sdf->mouthCount = 0;
    sdf->axialStationCount=0;
    if (!monster || (monster->bodyPartCount == 0 && !monster->hasAnatomyGraph)) {
        sdf->bounds = AABB_FromMinMax(Vec3_Create(-1.0f, -1.0f, -1.0f), Vec3_Create(1.0f, 1.0f, 1.0f));
        sdf->bodyBounds = sdf->bounds;
        return true;
    }
    /* 1. Partes heredadas. En el camino anatómico son anclas, no volúmenes visibles. */
    size_t anatomicalHost = monster->hasHead ? monster->head.anatomy.attachmentBodyPartIndex : (size_t)-1;
    sdf->bodyPartCount = 0;
    if (!monster->hasAnatomyGraph) {
        for (size_t i=0;i<monster->bodyPartCount;++i)
            if(i!=anatomicalHost) sdf->bodyPartCount++;
    }
    if (!MonsterSDF_EnsureCapacity((void**)&sdf->bodyParts, sizeof(MonsterSDFBodyPart), &sdf->bodyPartCapacity, sdf->bodyPartCount)) {
        MonsterSDF_Free(sdf); return false;
    }
    size_t compiledPart=0;
    for (size_t i = 0; i < monster->bodyPartCount && compiledPart<sdf->bodyPartCount; ++i) {
        if(i==anatomicalHost) continue;
        const BodyPart* part = &monster->bodyParts[i];
        size_t dst=compiledPart++;
        sdf->bodyParts[dst].center = part->positionRender;
        float rx = Math_Max(part->widthRender * 0.5f, 0.0001f);
        float ry = Math_Max(part->heightRender * 0.5f, 0.0001f);
        float rz = Math_Max(part->lengthRender * 0.5f, 0.0001f);
        sdf->bodyParts[dst].radii = Vec3_Create(rx, ry, rz);
        sdf->bodyParts[dst].invRadii = Vec3_Create(1.0f / rx, 1.0f / ry, 1.0f / rz);
        sdf->bodyParts[dst].invRadiiSquared = Vec3_Create(
            sdf->bodyParts[dst].invRadii.x * sdf->bodyParts[dst].invRadii.x,
            sdf->bodyParts[dst].invRadii.y * sdf->bodyParts[dst].invRadii.y,
            sdf->bodyParts[dst].invRadii.z * sdf->bodyParts[dst].invRadii.z);
        sdf->bodyParts[dst].minRadius = Math_Min(rx, Math_Min(ry, rz));
        sdf->bodyParts[dst].color = Monster_GetColorFromIndexStruct(monster, part->color);
        AABB_ExpandRadius(&sdf->bounds, sdf->bodyParts[dst].center, sdf->bodyParts[dst].radii);
    }
    /* 2. Conectores: grafo explícito o compatibilidad secuencial heredada. */
    if(monster->hasLizardPhenotype && monster->hasAnatomyGraph) {
        for(size_t i=0;i<monster->anatomyGraph.nodeCount;++i) {
            const AnatomyNode* n=&monster->anatomyGraph.nodes[i];
            if(n->role!=ANATOMY_ROLE_AXIAL || n->id==ANATOMY_ID_HEAD) continue;
            if(sdf->axialStationCount>=16) {MonsterSDF_Free(sdf);return false;}
            SDFSweepStation* st=&sdf->axialStations[sdf->axialStationCount++];
            *st=(SDFSweepStation){.center=n->center,.width=n->widthRadius,.height=n->heightRadius};
        }
        if(!SDF_SweepResolveTangents(sdf->axialStations,sdf->axialStationCount)){MonsterSDF_Free(sdf);return false;}
    }

    if (monster->hasAnatomyGraph) {
        sdf->connectorCount = monster->anatomyGraph.connectionCount;
        if (!MonsterSDF_EnsureCapacity((void**)&sdf->connectors,sizeof(MonsterSDFConnector),
                                       &sdf->connectorCapacity,sdf->connectorCount)) {
            MonsterSDF_Free(sdf); return false;
        }
        for (size_t i=0;i<sdf->connectorCount;++i) {
            const BodyConnection* edge=&monster->anatomyGraph.connections[i];
            const AnatomyNode* a=AnatomyGraph_FindNode(&monster->anatomyGraph,edge->fromId);
            const AnatomyNode* b=AnatomyGraph_FindNode(&monster->anatomyGraph,edge->toId);
            if(!a||!b){MonsterSDF_Free(sdf);return false;}
            MonsterSDFConnector* c=&sdf->connectors[i]; memset(c,0,sizeof(*c));
            c->a=a->center;c->b=b->center;c->ba=Vec3_Sub(b->center,a->center);
            float lengthSquared=Vec3_Dot(c->ba,c->ba);c->invBaLengthSquared=lengthSquared>1e-8f?1.0f/lengthSquared:0.0f;
            c->widthA=a->widthRadius;c->heightA=a->heightRadius;c->widthB=b->widthRadius;c->heightB=b->heightRadius;
            c->r1=Math_Min(c->widthA,c->heightA);c->r2=Math_Min(c->widthB,c->heightB);c->radiusDelta=c->r2-c->r1;
            c->fromId=edge->fromId;c->toId=edge->toId;c->kind=edge->kind;
            c->color=Color_Lerp(Monster_GetColorFromIndex(monster,a->colorIndex),Monster_GetColorFromIndex(monster,b->colorIndex),.5f);
            AABB_ExpandRadius(&sdf->bounds,c->a,Vec3_Create(c->widthA,c->heightA,c->widthA));
            AABB_ExpandRadius(&sdf->bounds,c->b,Vec3_Create(c->widthB,c->heightB,c->widthB));
        }
    } else if (monster->bodyPartCount > 1) {
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
            sdf->connectors[i].widthA=r1;sdf->connectors[i].heightA=r1;
            sdf->connectors[i].widthB=r2;sdf->connectors[i].heightB=r2;
            sdf->connectors[i].fromId=0;sdf->connectors[i].toId=0;
            sdf->connectors[i].kind=BODY_CONNECTION_LIMB_SEGMENT;
            Color c1 = Monster_GetColorFromIndexStruct(monster, p1->color);
            Color c2 = Monster_GetColorFromIndexStruct(monster, p2->color);
            sdf->connectors[i].color = Color_Lerp(c1, c2, 0.5f);
            AABB_ExpandRadius(&sdf->bounds, sdf->connectors[i].a, Vec3_Create(r1, r1, r1));
            AABB_ExpandRadius(&sdf->bounds, sdf->connectors[i].b, Vec3_Create(r2, r2, r2));
        }
    }
    for(size_t i=0;i<sdf->connectorCount;++i) {
        MonsterSDFConnector* c=&sdf->connectors[i];
        c->length=Vec3_Length(c->ba);
        c->forward=c->length>1e-6f?Vec3_Scale(c->ba,1/c->length):Vec3_Create(0,0,1);
        Vector3 reference=fabsf(c->forward.y)>.94f?Vec3_Create(1,0,0):Vec3_Create(0,1,0);
        c->side=Vec3_Normalize(Vec3_Cross(reference,c->forward));
        c->up=Vec3_Cross(c->forward,c->side);

        float maxR = Math_Max(Math_Max(c->widthA, c->heightA), Math_Max(c->widthB, c->heightB));
        float minR = Math_Min(Math_Min(c->widthA, c->heightA), Math_Min(c->widthB, c->heightB));
        float factor = c->kind == BODY_CONNECTION_AXIAL_LOFT ? 0.0f : c->kind == BODY_CONNECTION_LIMB_SEGMENT ? 0.18f : 0.08f;
        c->localSmoothness = Math_Min(config.connectionSmoothness, minR * factor);
        c->groupCount=0;
        c->maxRadius = maxR;
        c->distanceLowerBoundScale = Math_Min(
            Math_Min(c->widthA,c->heightA)/Math_Max(c->widthA,c->heightA),
            Math_Min(c->widthB,c->heightB)/Math_Max(c->widthB,c->heightB));
        Vector3 bmin = Vec3_Create(Math_Min(c->a.x, c->b.x) - maxR,
                                   Math_Min(c->a.y, c->b.y) - maxR,
                                   Math_Min(c->a.z, c->b.z) - maxR);
        Vector3 bmax = Vec3_Create(Math_Max(c->a.x, c->b.x) + maxR,
                                   Math_Max(c->a.y, c->b.y) + maxR,
                                   Math_Max(c->a.z, c->b.z) + maxR);
        c->bounds = AABB_FromMinMax(bmin, bmax);
    }
    /* Agrupar sólo tramos contiguos: la mezcla suave conserva su orden original. */
    if(monster->hasLizardPhenotype) {
        for(size_t i=0;i<sdf->connectorCount;) {
            AnatomyId id=sdf->connectors[i].toId;
            int limb=id>=ANATOMY_ID_DIGIT_BASE?(int)((id-ANATOMY_ID_DIGIT_BASE)/128u):
                id>=100&&id<=163?(int)((id-100)/20u):-1;
            if(limb<0||limb>=4){++i;continue;}
            MonsterSDFConnector* first=&sdf->connectors[i];
            first->groupBounds=first->bounds;first->groupLowerBoundScale=first->distanceLowerBoundScale;
            first->groupSmoothness=first->localSmoothness;size_t j=i+1;
            for(;j<sdf->connectorCount;++j) {
                MonsterSDFConnector* c=&sdf->connectors[j];id=c->toId;
                int next=id>=ANATOMY_ID_DIGIT_BASE?(int)((id-ANATOMY_ID_DIGIT_BASE)/128u):
                    id>=100&&id<=163?(int)((id-100)/20u):-1;
                if(next!=limb)break;
                AABB_ExpandPoint(&first->groupBounds,c->bounds.start);
                AABB_ExpandPoint(&first->groupBounds,c->bounds.end);
                first->groupLowerBoundScale=Math_Min(first->groupLowerBoundScale,c->distanceLowerBoundScale);
                first->groupSmoothness=Math_Max(first->groupSmoothness,c->localSmoothness);
            }
            first->groupCount=j-i;i=j;
        }
    }
    /* 3. Bocas */
    if (monster->mouthCount > 0) {
        sdf->mouthCount = monster->mouthCount;
        if (!MonsterSDF_EnsureCapacity((void**)&sdf->mouths, sizeof(MonsterSDFMouth), &sdf->mouthCapacity, sdf->mouthCount)) {
            MonsterSDF_Free(sdf); return false;
        }
        for (size_t m = 0; m < monster->mouthCount; ++m) {
            memset(&sdf->mouths[m],0,sizeof(sdf->mouths[m]));
            sdf->mouths[m].cephalicDevelopment=monster->hasLizardPhenotype?
                monster->lizardPhenotype.cephalicDevelopment:1.0f;
            HeadAnatomy resolvedHead={0};
            bool useAnatomicalHead = monster->hasHead && m == 0 &&
                monster->head.anatomy.attachmentBodyPartIndex < monster->bodyPartCount;
            Mouth normalized;
            if (useAnatomicalHead) {
                /* La anatomía resuelta es la autoridad. El anfitrión heredado sólo
                 * aporta la transformación mundial y nunca vuelve a definir la forma. */
                resolvedHead=monster->head.anatomy;
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
            sdf->mouths[m].visualJawPivot=mouth->jawPivot;
            sdf->mouths[m].visualJawAngle=Mouth_GetJawAngle(mouth)*.01745329252f;
            sdf->mouths[m].visualJawScale=monster->hasLizardPhenotype?monster->lizardPhenotype.cephalicDevelopment:1.0f;
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
            sdf->mouths[m].leftBrowCenterLocal = Vec3_Add(hostCenter, Vec3_Create(0.0f, hostRadii.y * 0.58f, hostRadii.z * 0.18f));
            sdf->mouths[m].rightBrowCenterLocal = sdf->mouths[m].leftBrowCenterLocal;
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
                sdf->mouths[m].faceMidLocal=HEAD_LOCAL(recipe->faceMid);
                sdf->mouths[m].faceTipLocal=HEAD_LOCAL(recipe->faceTip);
                sdf->mouths[m].faceRootRadii=recipe->faceRootRadii;
                sdf->mouths[m].faceMidRadii=recipe->faceMidRadii;
                sdf->mouths[m].faceTipRadii=recipe->faceTipRadii;
                sdf->mouths[m].cheekCenterLocal=HEAD_LOCAL(recipe->leftCheekCenter);
                sdf->mouths[m].cheekRadii=recipe->cheekRadii;
                sdf->mouths[m].leftTemporalCenterLocal=HEAD_LOCAL(recipe->leftTemporalCenter);
                sdf->mouths[m].rightTemporalCenterLocal=HEAD_LOCAL(recipe->rightTemporalCenter);
                sdf->mouths[m].temporalRadii=recipe->temporalRadii;
                sdf->mouths[m].leftMaxillaryCenterLocal=HEAD_LOCAL(recipe->leftMaxillaryCenter);
                sdf->mouths[m].rightMaxillaryCenterLocal=HEAD_LOCAL(recipe->rightMaxillaryCenter);
                sdf->mouths[m].maxillaryRadii=recipe->maxillaryRadii;
                sdf->mouths[m].leftBrowCenterLocal=HEAD_LOCAL(recipe->leftBrowCenter);
                sdf->mouths[m].rightBrowCenterLocal=HEAD_LOCAL(recipe->rightBrowCenter);
                sdf->mouths[m].browRadii=recipe->browRadii;
                sdf->mouths[m].leftOrbitCenterLocal=HEAD_LOCAL(recipe->leftOrbitCenter);
                sdf->mouths[m].rightOrbitCenterLocal=HEAD_LOCAL(recipe->rightOrbitCenter);
                sdf->mouths[m].orbitRadii=recipe->orbitRadii;
                sdf->mouths[m].leftOrbitRimCenterLocal=HEAD_LOCAL(recipe->leftOrbitRimCenter);
                sdf->mouths[m].rightOrbitRimCenterLocal=HEAD_LOCAL(recipe->rightOrbitRimCenter);
                sdf->mouths[m].orbitRimRadii=recipe->orbitRimRadii;
                sdf->mouths[m].leftOrbitNormal=recipe->leftOrbitNormal;
                sdf->mouths[m].rightOrbitNormal=recipe->rightOrbitNormal;
                sdf->mouths[m].orbitSocketDepth=recipe->orbitSocketDepth;
                sdf->mouths[m].noseCenterLocal=HEAD_LOCAL(recipe->noseCenter);
                sdf->mouths[m].noseRadii=recipe->noseRadii;
                sdf->mouths[m].leftNostrilCenterLocal=HEAD_LOCAL(recipe->leftNostrilCenter);
                sdf->mouths[m].rightNostrilCenterLocal=HEAD_LOCAL(recipe->rightNostrilCenter);
                sdf->mouths[m].nostrilRadii=recipe->nostrilRadii;
                sdf->mouths[m].leftTympanumCenterLocal=HEAD_LOCAL(recipe->leftTympanumCenter);
                sdf->mouths[m].rightTympanumCenterLocal=HEAD_LOCAL(recipe->rightTympanumCenter);
                sdf->mouths[m].tympanumRadii=recipe->tympanumRadii;
                sdf->mouths[m].tympanumDepth=recipe->tympanumDepth;
                sdf->mouths[m].leftEarCenterLocal=HEAD_LOCAL(recipe->leftEarCenter);
                sdf->mouths[m].rightEarCenterLocal=HEAD_LOCAL(recipe->rightEarCenter);
                sdf->mouths[m].earRadii=recipe->earRadii;
                sdf->mouths[m].headUnionSmoothness=recipe->unionSmoothness;
                sdf->mouths[m].headBodySmoothness=recipe->headBodySmoothness;
                sdf->mouths[m].neckCollarRootLocal=HEAD_LOCAL(resolvedHead.landmarks.neckAttachment);
                const AnatomyNode* neckNode=monster->hasAnatomyGraph?
                    AnatomyGraph_FindNode(&monster->anatomyGraph,ANATOMY_ID_NECK):NULL;
                sdf->mouths[m].neckCollarTipLocal=neckNode?
                    Transform3D_ApplyRotationBasis(sdf->mouths[m].inverseRotation,
                        Vec3_Sub(neckNode->center,mouthWorldPos)):
                    Vec3_Add(sdf->mouths[m].neckCollarRootLocal,Vec3_Create(0,-hostRadii.y*.08f,-hostRadii.z*.30f));
                float collarRootX=recipe->craniumRadii.x*.36f,collarRootY=recipe->craniumRadii.y*.50f;
                sdf->mouths[m].neckCollarRootRadii=Vec3_Create(collarRootX,collarRootY,Math_Min(collarRootX,collarRootY));
                if(neckNode) {
                    float collarTipX=neckNode->widthRadius*.58f,collarTipY=neckNode->heightRadius*.58f;
                    sdf->mouths[m].neckCollarTipRadii=Vec3_Create(collarTipX,collarTipY,Math_Min(collarTipX,collarTipY));
                } else {
                    float collarTipX=recipe->craniumRadii.x*.24f,collarTipY=recipe->craniumRadii.y*.34f;
                    sdf->mouths[m].neckCollarTipRadii=Vec3_Create(collarTipX,collarTipY,Math_Min(collarTipX,collarTipY));
                }
                sdf->mouths[m].hasNasalPad=recipe->hasNasalPad;
                sdf->mouths[m].hasEars=recipe->hasEars;
                sdf->mouths[m].hasTympana=recipe->hasTympana;
                sdf->mouths[m].faceRounding=recipe->faceRounding;
                sdf->hasPartitionedHead=true;
#undef HEAD_LOCAL
            }
            float front = hostCenter.z + hostRadii.z + Math_Max(0.02f, slitThickness * 0.5f);
            float rear = hostCenter.z - hostRadii.z * 0.55f;
            if(useAnatomicalHead){
                front=resolvedHead.surface.faceTip.z+resolvedHead.surface.faceTipRadii.z+0.04f;
                rear=resolvedHead.landmarks.leftJawHinge.z;
            }
            float maxRear = front - Math_Max(depth * 0.95f, slitThickness * 4.0f);
            if (rear > maxRear) rear = maxRear;
            halfDepth = Math_Max((front - rear) * 0.5f, slitThickness * 2.0f);
            sdf->mouths[m].entranceCenterLocal = Vec3_Create(0.0f, useAnatomicalHead?resolvedHead.landmarks.leftMouthCorner.y:0.0f, (front + rear) * 0.5f);
            float entranceHalfX = cutHalfWidth;
            if (useAnatomicalHead) {
                float maxLateral = Math_Max(resolvedHead.landmarks.leftMouthCorner.x, resolvedHead.landmarks.leftJawHinge.x);
                entranceHalfX = Math_Max(entranceHalfX, maxLateral + cutHalfHeight);
            }
            sdf->mouths[m].entranceHalfExtents = Vec3_Create(entranceHalfX, cutHalfHeight, halfDepth);
            sdf->mouths[m].cavityCenterLocal = useAnatomicalHead?resolvedHead.landmarks.oralCavityCenter:Vec3_Create(0.0f, 0.0f, rear + halfDepth * 0.32f);
            float maxCavityX = useAnatomicalHead ? resolvedHead.surface.faceMidRadii.x * 0.65f : width * 0.43f;
            float cavityX = Math_Max(Math_Min(width * 0.43f, maxCavityX), slitThickness * 2.0f);
            sdf->mouths[m].cavityRadii = Vec3_Create(cavityX, useAnatomicalHead?Math_Max(resolvedHead.surface.faceRootRadii.y*.34f,slitThickness*2.0f):Math_Max(maxOpening * 0.62f, width * 0.18f), Math_Max(depth * 0.48f, slitThickness * 2.0f));
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
            float effLen = useAnatomicalHead ? phenotypeLen : Math_Max(derivedLen, phenotypeLen * 0.98f);
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
            sdf->mouths[m].taperedMandible = mouth->shape == MOUTH_SHAPE_TAPERED_MANDIBLE;

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
                INCLUDE_ELLIPSOID(sdf->mouths[m].faceMidLocal,sdf->mouths[m].faceMidRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].faceTipLocal,sdf->mouths[m].faceTipRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].leftTemporalCenterLocal,sdf->mouths[m].temporalRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].rightTemporalCenterLocal,sdf->mouths[m].temporalRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].leftMaxillaryCenterLocal,sdf->mouths[m].maxillaryRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].rightMaxillaryCenterLocal,sdf->mouths[m].maxillaryRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].cheekCenterLocal,sdf->mouths[m].cheekRadii);
                Vector3 rightCheek=sdf->mouths[m].cheekCenterLocal;rightCheek.x*=-1.0f;
                INCLUDE_ELLIPSOID(rightCheek,sdf->mouths[m].cheekRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].leftBrowCenterLocal,sdf->mouths[m].browRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].rightBrowCenterLocal,sdf->mouths[m].browRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].leftOrbitRimCenterLocal,sdf->mouths[m].orbitRimRadii);
                INCLUDE_ELLIPSOID(sdf->mouths[m].rightOrbitRimCenterLocal,sdf->mouths[m].orbitRimRadii);
                if(sdf->mouths[m].hasEars) {
                    INCLUDE_ELLIPSOID(sdf->mouths[m].leftEarCenterLocal,sdf->mouths[m].earRadii);
                    INCLUDE_ELLIPSOID(sdf->mouths[m].rightEarCenterLocal,sdf->mouths[m].earRadii);
                }
#undef INCLUDE_ELLIPSOID

                AABB3D localHead=AABB_Empty();
#define EXPAND_HEAD(centerValue,radiiValue) AABB_ExpandRadius(&localHead,(centerValue),(radiiValue))
                EXPAND_HEAD(sdf->mouths[m].craniumCenterLocal,sdf->mouths[m].craniumRadii);
                EXPAND_HEAD(sdf->mouths[m].faceRootLocal,sdf->mouths[m].faceRootRadii);
                EXPAND_HEAD(sdf->mouths[m].faceMidLocal,sdf->mouths[m].faceMidRadii);
                EXPAND_HEAD(sdf->mouths[m].faceTipLocal,sdf->mouths[m].faceTipRadii);
                EXPAND_HEAD(sdf->mouths[m].leftTemporalCenterLocal,sdf->mouths[m].temporalRadii);
                EXPAND_HEAD(sdf->mouths[m].rightTemporalCenterLocal,sdf->mouths[m].temporalRadii);
                EXPAND_HEAD(sdf->mouths[m].leftMaxillaryCenterLocal,sdf->mouths[m].maxillaryRadii);
                EXPAND_HEAD(sdf->mouths[m].rightMaxillaryCenterLocal,sdf->mouths[m].maxillaryRadii);
                EXPAND_HEAD(sdf->mouths[m].cheekCenterLocal,sdf->mouths[m].cheekRadii);
                EXPAND_HEAD(rightCheek,sdf->mouths[m].cheekRadii);
                EXPAND_HEAD(sdf->mouths[m].leftBrowCenterLocal,sdf->mouths[m].browRadii);
                EXPAND_HEAD(sdf->mouths[m].rightBrowCenterLocal,sdf->mouths[m].browRadii);
                EXPAND_HEAD(sdf->mouths[m].leftOrbitRimCenterLocal,sdf->mouths[m].orbitRimRadii);
                EXPAND_HEAD(sdf->mouths[m].rightOrbitRimCenterLocal,sdf->mouths[m].orbitRimRadii);
                float collarRootBound=Math_Max(sdf->mouths[m].neckCollarRootRadii.x,sdf->mouths[m].neckCollarRootRadii.y);
                float collarTipBound=Math_Max(sdf->mouths[m].neckCollarTipRadii.x,sdf->mouths[m].neckCollarTipRadii.y);
                EXPAND_HEAD(sdf->mouths[m].neckCollarRootLocal,Vec3_Create(collarRootBound,collarRootBound,collarRootBound));
                EXPAND_HEAD(sdf->mouths[m].neckCollarTipLocal,Vec3_Create(collarTipBound,collarTipBound,collarTipBound));
                if(sdf->mouths[m].hasNasalPad)EXPAND_HEAD(sdf->mouths[m].noseCenterLocal,sdf->mouths[m].noseRadii);
                if(sdf->mouths[m].hasEars){EXPAND_HEAD(sdf->mouths[m].leftEarCenterLocal,sdf->mouths[m].earRadii);EXPAND_HEAD(sdf->mouths[m].rightEarCenterLocal,sdf->mouths[m].earRadii);}
#undef EXPAND_HEAD
                AABB_Pad(&localHead,Math_Max(sdf->mouths[m].headUnionSmoothness*.8f,.025f));
                Vector3 lc=Vec3_Scale(Vec3_Add(localHead.start,localHead.end),.5f);
                Vector3 le=Vec3_Scale(Vec3_Sub(localHead.end,localHead.start),.5f);
                Vector3 wc=MonsterSDF_LocalToWorld(&sdf->mouths[m],lc);
                float hxw=fabsf(invRot.row0.x)*le.x+fabsf(invRot.row1.x)*le.y+fabsf(invRot.row2.x)*le.z;
                float hyw=fabsf(invRot.row0.y)*le.x+fabsf(invRot.row1.y)*le.y+fabsf(invRot.row2.y)*le.z;
                float hzw=fabsf(invRot.row0.z)*le.x+fabsf(invRot.row1.z)*le.y+fabsf(invRot.row2.z)*le.z;
                sdf->mouths[m].headBounds=AABB_FromMinMax(Vec3_Sub(wc,Vec3_Create(hxw,hyw,hzw)),Vec3_Add(wc,Vec3_Create(hxw,hyw,hzw)));
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
    for(size_t i=0;i<sdf->mouthCount;++i) {
        MonsterSDFMouth* m=&sdf->mouths[i];
        m->sweptSkull=monster->hasHead && monster->head.phenotype.archetype==HEAD_ARCHETYPE_LIZARD;
        if(!m->sweptSkull)continue;
        Vector3 c=m->craniumCenterLocal,r=m->craniumRadii;
        m->headStations[0]=(SDFSweepStation){.center=m->faceTipLocal,.width=m->faceTipRadii.x,.height=m->faceTipRadii.y};
        m->headStations[1]=(SDFSweepStation){.center=m->faceMidLocal,.width=m->faceMidRadii.x,.height=m->faceMidRadii.y};
        m->headStations[2]=(SDFSweepStation){.center=m->faceRootLocal,.width=m->faceRootRadii.x,.height=m->faceRootRadii.y};
        m->headStations[3]=(SDFSweepStation){.center=Vec3_Add(c,Vec3_Create(0,0,r.z*.05f)),.width=r.x*.98f,.height=r.y*.92f};
        m->headStations[4]=(SDFSweepStation){.center=Vec3_Add(c,Vec3_Create(0,0,-r.z*.45f)),.width=r.x*.88f,.height=r.y*.88f};
        m->headStations[5]=(SDFSweepStation){.center=Vec3_Add(c,Vec3_Create(0,-r.y*.08f,-r.z*.85f)),.width=r.x*.50f,.height=r.y*.62f};
        float safeBottom = m->entranceCenterLocal.y;
        for(int st=0; st<4; ++st) {
            float topY = m->headStations[st].center.y + m->headStations[st].height;
            if(topY > safeBottom + 0.01f) {
                m->headStations[st].height = (topY - safeBottom) * 0.5f;
                m->headStations[st].center.y = safeBottom + m->headStations[st].height;
            }
        }
        if(!SDF_SweepResolveTangents(m->headStations,6)){MonsterSDF_Free(sdf);return false;}
    }
    for(size_t i=0;i<sdf->mouthCount;++i) {
        MonsterSDFMouth* m=&sdf->mouths[i];
        float r=Vec3_Distance(m->visualJawPivot,m->jawCenterLocal)+Vec3_Length(m->jawRadii)*2.f;
        for(int corner=0;corner<8;++corner) {
            Vector3 p=Vec3_Create((corner&1)?m->seamBounds.end.x:m->seamBounds.start.x,
                (corner&2)?m->seamBounds.end.y:m->seamBounds.start.y,
                (corner&4)?m->seamBounds.end.z:m->seamBounds.start.z);
            r=Math_Max(r,Vec3_Distance(p,m->visualJawPivot));
        }
        m->visualJawBoundRadius=(r+0.25f)*Math_Max(m->visualJawScale,0.20f)+0.10f;
        MonsterSDF_ResolveVisualBounds(sdf,i);
    }
    float pad = config.boundsPadding + config.bodySmoothness;
    AABB_Pad(&sdf->bounds, pad);
    if(!sdf->hasPartitionedHead) {
        sdf->bodyBounds=sdf->bounds;
    } else {
        sdf->bodyBounds=AABB_Empty();
        for(size_t i=0;i<sdf->bodyPartCount;++i)
            AABB_ExpandRadius(&sdf->bodyBounds,sdf->bodyParts[i].center,sdf->bodyParts[i].radii);
        for(size_t i=0;i<sdf->connectorCount;++i) {
            const MonsterSDFConnector* c=&sdf->connectors[i];
            if(c->fromId==ANATOMY_ID_HEAD&&c->toId==ANATOMY_ID_NECK)continue;
            AABB_ExpandRadius(&sdf->bodyBounds,c->a,Vec3_Create(c->widthA,c->heightA,c->widthA));
            AABB_ExpandRadius(&sdf->bodyBounds,c->b,Vec3_Create(c->widthB,c->heightB,c->widthB));
        }
        if(AABB_Size(sdf->bodyBounds).x<=0.0f)sdf->bodyBounds=sdf->bounds;
        else AABB_Pad(&sdf->bodyBounds,pad);
    }
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
    if(conn->length<1e-6f) return SDF_Ellipsoid(Vec3_Sub(point,conn->a),
        Vec3_Create(conn->widthA,conn->heightA,conn->widthA));
    Vector3 p=Vec3_Sub(point,conn->a);
    float along=Vec3_Dot(p,conn->forward),t=Math_Clamp01(along/conn->length);
    float w=Math_Max(Math_Lerp(conn->widthA,conn->widthB,t),.0001f);
    float h=Math_Max(Math_Lerp(conn->heightA,conn->heightB,t),.0001f),r=Math_Min(w,h);
    float x=Vec3_Dot(p,conn->side)/w,y=Vec3_Dot(p,conn->up)/h;
    float z=(along<0?along:along>conn->length?along-conn->length:0)/r;
    return (sqrtf(x*x+y*y+z*z)-1)*r;
}

static inline bool MonsterSDF_PruneBox(const AABB3D* bounds, float lowerScale, Vector3 point, float cutoff) {
    /* La distancia fuera de la caja multiplicada por min(r)/max(r) es una
     * cota inferior del campo elíptico. Un factor fijo no protege palmas planas. */
    float safeCutoff = cutoff / Math_Max(lowerScale,0.0001f);
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    if (point.x < bounds->start.x) dx = bounds->start.x - point.x;
    else if (point.x > bounds->end.x) dx = point.x - bounds->end.x;

    if (point.y < bounds->start.y) dy = bounds->start.y - point.y;
    else if (point.y > bounds->end.y) dy = point.y - bounds->end.y;

    if (point.z < bounds->start.z) dz = bounds->start.z - point.z;
    else if (point.z > bounds->end.z) dz = point.z - bounds->end.z;

    /* Fuera de la caja, la cota es no negativa: tampoco puede mejorar una
     * unión cuyo umbral ya sea negativo. Evita evaluar dedos lejanos dentro del torso. */
    if(cutoff<=0.0f)return dx>0.0f || dy>0.0f || dz>0.0f;
    return (dx * dx + dy * dy + dz * dz) >= (safeCutoff * safeCutoff);
}
static inline bool MonsterSDF_ShouldPruneConnector(const MonsterSDFConnector* c, Vector3 point, float cutoff) {
    return MonsterSDF_PruneBox(&c->bounds,c->distanceLowerBoundScale,point,cutoff);
}
static inline bool MonsterSDF_ShouldPruneGroup(const MonsterSDFConnector* c,Vector3 point,float distance) {
    return c->groupCount>1 && MonsterSDF_PruneBox(&c->groupBounds,c->groupLowerBoundScale,
        point,distance+c->groupSmoothness);
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
static float MonsterSDF_EvalShallowCutter(Vector3 p,Vector3 center,Vector3 radii,
                                           Vector3 normal,float depth) {
    Vector3 rel=Vec3_Sub(p,center);
    float ellipsoid=SDF_Ellipsoid(rel,radii);
    float rearPlane=-Vec3_Dot(rel,normal)-Math_Max(depth,.001f);
    return Math_Max(ellipsoid,rearPlane);
}
static float MonsterSDF_EvalOrbitCavities(const MonsterSDFMouth* mouth,Vector3 p) {
    if(!mouth->anatomicalHead)return 1e6f;
    /* El cutter se adelanta hacia el exterior para que la órbita sea una
     * cavidad abierta. Centrarlo en el landmark podía cerrar una piel fina
     * delante del globo y producir islotes al polygonizarla. */
    Vector3 leftCenter=Vec3_Add(mouth->leftOrbitCenterLocal,Vec3_Scale(mouth->leftOrbitNormal,mouth->orbitRadii.x*.18f));
    Vector3 rightCenter=Vec3_Add(mouth->rightOrbitCenterLocal,Vec3_Scale(mouth->rightOrbitNormal,mouth->orbitRadii.x*.18f));
    float left=MonsterSDF_EvalShallowCutter(p,leftCenter,mouth->orbitRadii,mouth->leftOrbitNormal,mouth->orbitSocketDepth);
    float right=MonsterSDF_EvalShallowCutter(p,rightCenter,mouth->orbitRadii,mouth->rightOrbitNormal,mouth->orbitSocketDepth);
    return Math_Min(left,right);
}
static float MonsterSDF_EvalNostrilCavities(const MonsterSDFMouth* mouth,Vector3 p) {
    if(!mouth->anatomicalHead)return 1e6f;
    /* Las narinas son depresiones abiertas, no elipsoides cerrados que puedan
     * aparecer como islas internas al cambiar la resolución de Marching Cubes. */
    Vector3 leftNormal=Vec3_Normalize(Vec3_Create(1.0f,.78f,.12f));
    Vector3 rightNormal=leftNormal;rightNormal.x*=-1.0f;
    float depth=Math_Min(mouth->nostrilRadii.x,mouth->nostrilRadii.y)*.58f;
    float left=MonsterSDF_EvalShallowCutter(p,mouth->leftNostrilCenterLocal,mouth->nostrilRadii,leftNormal,depth);
    float right=MonsterSDF_EvalShallowCutter(p,mouth->rightNostrilCenterLocal,mouth->nostrilRadii,rightNormal,depth);
    return Math_Min(left,right);
}
static float MonsterSDF_EvalTympanumCavities(const MonsterSDFMouth* mouth,Vector3 p) {
    if(!mouth->anatomicalHead||!mouth->hasTympana)return 1e6f;
    float left=MonsterSDF_EvalShallowCutter(p,mouth->leftTympanumCenterLocal,mouth->tympanumRadii,Vec3_Create(1,0,0),mouth->tympanumDepth);
    float right=MonsterSDF_EvalShallowCutter(p,mouth->rightTympanumCenterLocal,mouth->tympanumRadii,Vec3_Create(-1,0,0),mouth->tympanumDepth);
    return Math_Min(left,right);
}
static float MonsterSDF_EvalJawBase(const MonsterSDFMouth* mouth, Vector3 localP) {
    if(mouth->lowerBeak) {
        Vector3 root=Vec3_Add(mouth->hingeCenterLocal,Vec3_Create(0,-mouth->jawRadii.y*.12f,mouth->jawRadii.z*.08f));
        Vector3 tip=Vec3_Create(mouth->jawCenterLocal.x,mouth->jawCenterLocal.y,mouth->jawCenterLocal.z+mouth->jawRadii.z);
        Vector3 rootRadii=Vec3_Create(mouth->jawRadii.x,mouth->jawRadii.y,mouth->jawRadii.x*.42f);
        Vector3 tipRadii=Vec3_Create(mouth->jawRadii.x*.20f,mouth->jawRadii.y*.28f,mouth->jawRadii.x*.18f);
        return SDF_TaperedEllipticalCapsuleApprox(localP,root,tip,rootRadii,tipRadii);
    }
    if(mouth->taperedMandible) {
        float rx=mouth->jawRadii.x,ry=mouth->jawRadii.y,rz=mouth->jawRadii.z;
        Vector3 leftRear=Vec3_Add(mouth->hingeCenterLocal,Vec3_Create(rx*.76f,-ry*.18f,0));
        Vector3 rightRear=leftRear;rightRear.x*=-1;
        Vector3 leftTip=Vec3_Create(rx*.08f,mouth->jawCenterLocal.y+ry*.64f,mouth->jawCenterLocal.z+rz*.96f);
        Vector3 rightTip=leftTip;rightTip.x*=-1;
        float rearW=Math_Max(mouth->jawRearMass*.62f,rx*.15f),tipW=Math_Max(rearW*.58f,rx*.055f);
        float left=SDF_TaperedEllipticalSegmentApprox(localP,leftRear,leftTip,rearW,ry*.72f,tipW,ry*.34f);
        float right=SDF_TaperedEllipticalSegmentApprox(localP,rightRear,rightTip,rearW,ry*.72f,tipW,ry*.34f);
        float bridge=SDF_TaperedEllipticalSegmentApprox(localP,leftTip,rightTip,tipW,ry*.46f,tipW,ry*.46f);
        Vector3 rearRadii=Vec3_Create(rearW*1.25f,ry*.82f,rearW*1.10f);
        float rearLeft=SDF_Ellipsoid(Vec3_Sub(localP,leftRear),rearRadii);
        float rearRight=SDF_Ellipsoid(Vec3_Sub(localP,rightRear),rearRadii);
        float k=Math_Max(mouth->seamScale*.10f,.006f);
        float result=SDF_SmoothUnion(left,right,k);result=SDF_SmoothUnion(result,bridge,k);
        result=SDF_SmoothUnion(result,rearLeft,k);result=SDF_SmoothUnion(result,rearRight,k);
        /* El suelo mandibular une las ramas: la mandíbula es tejido con volumen,
         * no dos varillas aisladas cuando la cámara observa el interior oral. */
        Vector3 floorRear=Vec3_Create(0,leftRear.y-ry*.18f,leftRear.z);
        Vector3 floorTip=Vec3_Create(0,leftTip.y-ry*.10f,leftTip.z);
        float floor=SDF_TaperedEllipticalSegmentApprox(localP,floorRear,floorTip,
            rx*.70f,ry*.40f,tipW,ry*.32f);
        return SDF_SmoothUnion(result,floor,k);
    }
    /* Composición heredada para criaturas no anatómicas. */
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
static float MonsterSDF_EvalTongueDistance(const MonsterSDFMouth* mouth, Vector3 localP) {
    if(!mouth->taperedMandible) return 1e6f;
    float tDev = Math_Clamp01((mouth->cephalicDevelopment - 0.25f) / 0.50f);
    if(tDev <= 0.001f) return 1e6f;
    float rx=mouth->jawRadii.x, ry=mouth->jawRadii.y, rz=mouth->jawRadii.z;
    Vector3 leftRear=Vec3_Add(mouth->hingeCenterLocal,Vec3_Create(rx*.76f,-ry*.18f,0));
    Vector3 leftTip=Vec3_Create(rx*.08f,mouth->jawCenterLocal.y+ry*.64f,mouth->jawCenterLocal.z+rz*.96f);
    Vector3 floorRear=Vec3_Create(0,leftRear.y-ry*.18f,leftRear.z);
    Vector3 floorTip=Vec3_Create(0,leftTip.y-ry*.10f,leftTip.z);
    float spanZ=floorTip.z-floorRear.z;
    Vector3 tongueRoot=Vec3_Create(0,floorRear.y+ry*(0.44f*tDev),floorRear.z+spanZ*.18f);
    Vector3 tongueTip=Vec3_Create(0,floorTip.y+ry*(0.36f*tDev),floorRear.z+spanZ*.74f);
    float rootW=rx*.32f*tDev, rootH=ry*.20f*tDev;
    float tipW=rx*.14f*tDev, tipH=ry*.10f*tDev;
    return SDF_TaperedEllipticalSegmentApprox(localP,tongueRoot,tongueTip,rootW,rootH,tipW,tipH);
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
    if(mouth->taperedMandible) {
        /* En el lagarto la comisura es un tejido posterior compacto. Las
         * correas cruzadas del modelo heredado atravesaban visualmente toda
         * la boca abierta y parecían fragmentos de geometría rota. */
        Vector3 gHalf=Vec3_Create(h*1.58f,h*.62f,h*.88f);
        float gular=SDF_Ellipsoid(Vec3_Sub(localP,mouth->seamGularLocal),gHalf);
        float pivotPad=SDF_Sphere(Vec3_Sub(localP,mouth->hingeCenterLocal),h*.38f);
        float res=SDF_SmoothUnion(left,right,k);
        res=SDF_SmoothUnion(res,gular,k);
        return SDF_SmoothUnion(res,pivotPad,k);
    }
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

static float MonsterSDF_EvalRostrumDistance(const MonsterSDFMouth* mouth,Vector3 localP) {
    if(!mouth->anatomicalHead)return MonsterSDF_EvalMuzzleDistance(mouth,localP);
    return SDF_ThreeSectionEllipticalLoftApprox(localP,
        mouth->faceRootLocal,mouth->faceMidLocal,mouth->faceTipLocal,
        mouth->faceRootRadii,mouth->faceMidRadii,mouth->faceTipRadii,
        mouth->faceRounding);
}

static float MonsterSDF_EvalPeriorbitalDistance(const MonsterSDFMouth* mouth,Vector3 localP) {
    float k=Math_Max(mouth->headUnionSmoothness*.24f,.002f);
    float d=SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftOrbitRimCenterLocal),mouth->orbitRimRadii);
    d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightOrbitRimCenterLocal),mouth->orbitRimRadii),k);
    d=SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->leftBrowCenterLocal),mouth->browRadii),k);
    return SDF_SmoothUnion(d,SDF_Ellipsoid(Vec3_Sub(localP,mouth->rightBrowCenterLocal),mouth->browRadii),k);
}

static float MonsterSDF_EvalNeckCollarDistance(const MonsterSDFMouth* mouth,Vector3 localP) {
    return SDF_TaperedEllipticalSegmentApprox(localP,
        mouth->neckCollarRootLocal,mouth->neckCollarTipLocal,
        mouth->neckCollarRootRadii.x,mouth->neckCollarRootRadii.y,
        mouth->neckCollarTipRadii.x,mouth->neckCollarTipRadii.y);
}

static float MonsterSDF_EvalUpperHeadDistance(const MonsterSDFMouth* mouth, Vector3 localP) {
    if (mouth->anatomicalHead) {
        float dev = mouth->cephalicDevelopment;
        float k = Math_Max(mouth->headUnionSmoothness, .005f);
        float cranium = SDF_Ellipsoid(Vec3_Sub(localP, mouth->craniumCenterLocal), mouth->craniumRadii);
        float d = cranium;
        if (dev > 0.02f) {
            float face = MonsterSDF_EvalRostrumDistance(mouth, localP);
            float skull = mouth->sweptSkull ? SDF_EllipticalSweepZ(localP, mouth->headStations, 6) : SDF_SmoothUnion(d, face, k);
            d = Math_Lerp(cranium, skull, Math_Clamp01(dev * 1.35f));
            float featK = k * dev;
            if (featK > 0.001f) {
                d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->leftTemporalCenterLocal), mouth->temporalRadii), featK * .38f);
                d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->rightTemporalCenterLocal), mouth->temporalRadii), featK * .38f);
                d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->leftMaxillaryCenterLocal), mouth->maxillaryRadii), featK * .34f);
                d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->rightMaxillaryCenterLocal), mouth->maxillaryRadii), featK * .34f);
                d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->cheekCenterLocal), mouth->cheekRadii), featK * .42f);
                Vector3 rightCheek = mouth->cheekCenterLocal; rightCheek.x *= -1.0f;
                d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, rightCheek), mouth->cheekRadii), featK * .42f);
                d = SDF_SmoothUnion(d, MonsterSDF_EvalPeriorbitalDistance(mouth, localP), featK * .85f);
            }
        }
        d = SDF_SmoothUnion(d, MonsterSDF_EvalNeckCollarDistance(mouth, localP), mouth->headBodySmoothness);
        if (mouth->hasNasalPad) d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->noseCenterLocal), mouth->noseRadii), k * .35f * dev);
        if (mouth->hasEars) {
            d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->leftEarCenterLocal), mouth->earRadii), k * .28f * dev);
            d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->rightEarCenterLocal), mouth->earRadii), k * .28f * dev);
        }
        return d;
    }
    float d = SDF_Ellipsoid(Vec3_Sub(localP, mouth->craniumCenterLocal), mouth->craniumRadii);
    d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->snoutCenterLocal), mouth->snoutRadii), 0.08f);
    d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->cheekCenterLocal), mouth->cheekRadii), 0.06f);
    d = SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, Vec3_Create(-mouth->cheekCenterLocal.x, mouth->cheekCenterLocal.y, mouth->cheekCenterLocal.z)), mouth->cheekRadii), 0.06f);
    return SDF_SmoothUnion(d, SDF_Ellipsoid(Vec3_Sub(localP, mouth->leftBrowCenterLocal), mouth->browRadii), 0.05f);
}

SDFSample MonsterSDF_Evaluate(const MonsterSDF* sdf, Vector3 point) {
    if (!sdf || (sdf->bodyPartCount == 0 && sdf->connectorCount == 0 && sdf->mouthCount == 0)) return SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_UNKNOWN);
    SDFSample accumulated = SDFSample_Create(1e6f, COLOR_WHITE, SDF_MATERIAL_SKIN);
    bool hasInitialSample = sdf->axialStationCount>1;
    if(hasInitialSample) accumulated=SDFSample_Create(SDF_EllipticalSweepZ(point,sdf->axialStations,sdf->axialStationCount),(sdf->connectorCount?sdf->connectors[0].color:COLOR_WHITE),SDF_MATERIAL_SKIN);
    for (size_t i = 0; i < sdf->bodyPartCount; ++i) {
        const MonsterSDFBodyPart* part = &sdf->bodyParts[i];
        float dist = MonsterSDF_EvalBodyPartDistance(part, point);
        SDFSample partSample = SDFSample_Create(dist, part->color, SDF_MATERIAL_SKIN);
        if (!hasInitialSample) { accumulated = partSample; hasInitialSample = true; }
        else accumulated = SDFSample_SmoothUnion(accumulated, partSample, sdf->config.bodySmoothness);
    }
    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        const MonsterSDFConnector* conn = &sdf->connectors[i];
        if(sdf->axialStationCount>1 && conn->kind==BODY_CONNECTION_AXIAL_LOFT)continue;
        if(sdf->config.enableConnectorPruning && hasInitialSample && MonsterSDF_ShouldPruneGroup(conn,point,accumulated.distance)) {
            if(tls_enableStats){tls_connectorCandidateCount+=conn->groupCount;tls_connectorPrunedCount+=conn->groupCount;}
            i+=conn->groupCount-1;continue;
        }
        if (tls_enableStats) tls_connectorCandidateCount++;
        float localSmoothness = conn->localSmoothness;
        if (sdf->config.enableConnectorPruning && hasInitialSample &&
            MonsterSDF_ShouldPruneConnector(conn, point, accumulated.distance + localSmoothness)) {
            if (tls_enableStats) tls_connectorPrunedCount++;
            continue;
        }
        if (tls_enableStats) tls_connectorExactCount++;
        float dist = MonsterSDF_EvalConnectorDistance(conn, point);
        SDFSample connSample = SDFSample_Create(dist, conn->color, SDF_MATERIAL_SKIN);
        if (!hasInitialSample) { accumulated = connSample; hasInitialSample = true; }
        else accumulated = SDFSample_SmoothUnion(accumulated, connSample, localSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        Vector3 localP = Transform3D_ApplyRotationBasis(mouth->inverseRotation, Vec3_Sub(point, mouth->center));
        if (!mouth->anatomicalHead) {
            float muzzleDist = MonsterSDF_EvalMuzzleDistance(mouth, localP);
            SDFSample muzzleSample = SDFSample_Create(muzzleDist, mouth->skinColor, SDF_MATERIAL_SKIN);
            accumulated = SDFSample_SmoothUnion(accumulated, muzzleSample, mouth->muzzleSmoothness);
        }
        float upperHeadDist = MonsterSDF_EvalUpperHeadDistance(mouth, localP);
        accumulated = SDFSample_SmoothUnion(accumulated, SDFSample_Create(upperHeadDist, mouth->skinColor, SDF_MATERIAL_SKIN), mouth->anatomicalHead?mouth->headBodySmoothness:mouth->muzzleSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        Vector3 localP; float cutterDist = MonsterSDF_EvalMouthDistance(mouth, point, &localP);
        float devMouth = Math_Clamp01((mouth->cephalicDevelopment - 0.05f) / 0.25f);
        cutterDist += (1.0f - devMouth) * Math_Max(mouth->hostRadii.x, Math_Max(mouth->hostRadii.y, mouth->hostRadii.z)) * 2.0f;
        SDFSample cutterSample = SDFSample_Create(cutterDist, mouth->insideColor, SDF_MATERIAL_MOUTH);
        accumulated = SDFSample_Subtract(accumulated, cutterSample, mouth->rimBevel);
        if(mouth->anatomicalHead) {
            accumulated=SDFSample_Subtract(accumulated,SDFSample_Create(
                MonsterSDF_EvalOrbitCavities(mouth,localP),Color_FromRGB(24,28,19),SDF_MATERIAL_EYE_SOCKET),mouth->headUnionSmoothness*.06f);
            accumulated=SDFSample_Subtract(accumulated,SDFSample_Create(
                MonsterSDF_EvalNostrilCavities(mouth,localP),Color_FromRGB(18,20,14),SDF_MATERIAL_NOSTRIL),mouth->headUnionSmoothness*.04f);
            if(mouth->hasTympana) accumulated=SDFSample_Subtract(accumulated,SDFSample_Create(
                MonsterSDF_EvalTympanumCavities(mouth,localP),Color_FromRGB(25,24,16),SDF_MATERIAL_TYMPANUM),mouth->headUnionSmoothness*.04f);
        }
    }
    return accumulated;
}

float MonsterSDF_EvaluateDistance(const MonsterSDF* sdf, Vector3 point) {
    if (!sdf) return 1e6f;
    float accumulated = sdf->axialStationCount>1?SDF_EllipticalSweepZ(point,sdf->axialStations,sdf->axialStationCount):1e6f;
    bool hasInitial = sdf->axialStationCount>1;
    for (size_t i = 0; i < sdf->bodyPartCount; ++i) {
        const MonsterSDFBodyPart* part = &sdf->bodyParts[i];
        float dist = MonsterSDF_EvalBodyPartDistance(part, point);
        if (!hasInitial) { accumulated = dist; hasInitial = true; }
        else accumulated = SDF_SmoothUnion(accumulated, dist, sdf->config.bodySmoothness);
    }
    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        if(sdf->axialStationCount>1 && sdf->connectors[i].kind==BODY_CONNECTION_AXIAL_LOFT)continue;
        float dist = MonsterSDF_EvalConnectorDistance(&sdf->connectors[i], point);
        const MonsterSDFConnector* conn=&sdf->connectors[i];
        float scale=Math_Min(Math_Min(conn->widthA,conn->heightA),Math_Min(conn->widthB,conn->heightB));
        float factor=conn->kind==BODY_CONNECTION_AXIAL_LOFT?0.0f:conn->kind==BODY_CONNECTION_LIMB_SEGMENT?.18f:.08f;
        float localSmoothness=Math_Min(sdf->config.connectionSmoothness,scale*factor);
        if (!hasInitial) { accumulated = dist; hasInitial = true; }
        else accumulated = SDF_SmoothUnion(accumulated, dist, localSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        Vector3 localP = Transform3D_ApplyRotationBasis(mouth->inverseRotation, Vec3_Sub(point, mouth->center));
        if (!mouth->anatomicalHead) {
            float muzzleDist = MonsterSDF_EvalMuzzleDistance(mouth, localP);
            accumulated = SDF_SmoothUnion(accumulated, muzzleDist, mouth->muzzleSmoothness);
        }
        accumulated = SDF_SmoothUnion(accumulated, MonsterSDF_EvalUpperHeadDistance(mouth, localP), mouth->anatomicalHead?mouth->headBodySmoothness:mouth->muzzleSmoothness);
    }
    for (size_t m = 0; m < sdf->mouthCount; ++m) {
        const MonsterSDFMouth* mouth = &sdf->mouths[m];
        float cutterDist = MonsterSDF_EvalMouthDistance(mouth, point, NULL);
        float devMouth = Math_Clamp01((mouth->cephalicDevelopment - 0.05f) / 0.25f);
        cutterDist += (1.0f - devMouth) * Math_Max(mouth->hostRadii.x, Math_Max(mouth->hostRadii.y, mouth->hostRadii.z)) * 2.0f;
        accumulated = SDF_SmoothSubtract(accumulated, cutterDist, mouth->rimBevel);
        if(mouth->anatomicalHead) {
            Vector3 localP=Transform3D_ApplyRotationBasis(mouth->inverseRotation,Vec3_Sub(point,mouth->center));
            accumulated=SDF_SmoothSubtract(accumulated,MonsterSDF_EvalOrbitCavities(mouth,localP),mouth->headUnionSmoothness*.06f);
            accumulated=SDF_SmoothSubtract(accumulated,MonsterSDF_EvalNostrilCavities(mouth,localP),mouth->headUnionSmoothness*.04f);
            if(mouth->hasTympana) accumulated=SDF_SmoothSubtract(accumulated,MonsterSDF_EvalTympanumCavities(mouth,localP),mouth->headUnionSmoothness*.04f);
        }
    }
    return accumulated;
}

static SDFSample MonsterSDF_EvaluateBodyPartition(const MonsterSDF* sdf,Vector3 point) {
    if(!sdf)return SDFSample_Create(1e6f,COLOR_WHITE,SDF_MATERIAL_UNKNOWN);
    if(!sdf->hasPartitionedHead)return MonsterSDF_Evaluate(sdf,point);
    SDFSample accumulated=SDFSample_Create(1e6f,COLOR_WHITE,SDF_MATERIAL_SKIN);bool has=sdf->axialStationCount>1;
    if(has) accumulated=SDFSample_Create(SDF_EllipticalSweepZ(point,sdf->axialStations,sdf->axialStationCount),(sdf->connectorCount?sdf->connectors[0].color:COLOR_WHITE),SDF_MATERIAL_SKIN);
    for(size_t i=0;i<sdf->bodyPartCount;++i) {
        const MonsterSDFBodyPart* part=&sdf->bodyParts[i];
        SDFSample sample=SDFSample_Create(MonsterSDF_EvalBodyPartDistance(part,point),part->color,SDF_MATERIAL_SKIN);
        if(!has){accumulated=sample;has=true;}else accumulated=SDFSample_SmoothUnion(accumulated,sample,sdf->config.bodySmoothness);
    }
    for(size_t i=0;i<sdf->connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf->connectors[i];
        if(sdf->axialStationCount>1 && c->kind==BODY_CONNECTION_AXIAL_LOFT)continue;
        if(c->fromId==ANATOMY_ID_HEAD&&c->toId==ANATOMY_ID_NECK)continue;
        if(sdf->config.enableConnectorPruning && has && MonsterSDF_ShouldPruneGroup(c,point,accumulated.distance)) {
            if(tls_enableStats){tls_connectorCandidateCount+=c->groupCount;tls_connectorPrunedCount+=c->groupCount;}
            i+=c->groupCount-1;continue;
        }
        if (tls_enableStats) tls_connectorCandidateCount++;
        float smooth = c->localSmoothness;
        if (sdf->config.enableConnectorPruning && has &&
            MonsterSDF_ShouldPruneConnector(c, point, accumulated.distance + smooth)) {
            if (tls_enableStats) tls_connectorPrunedCount++;
            continue;
        }
        if (tls_enableStats) tls_connectorExactCount++;
        SDFSample sample=SDFSample_Create(MonsterSDF_EvalConnectorDistance(c,point),c->color,SDF_MATERIAL_SKIN);
        if(!has){accumulated=sample;has=true;}else accumulated=SDFSample_SmoothUnion(accumulated,sample,smooth);
    }
    return accumulated;
}

static float MonsterSDF_EvaluateBodyPartitionDistance(const MonsterSDF* sdf,Vector3 point) {
    if(!sdf)return 1e6f;
    if(!sdf->hasPartitionedHead)return MonsterSDF_EvaluateDistance(sdf,point);
    float accumulated=sdf->axialStationCount>1?SDF_EllipticalSweepZ(point,sdf->axialStations,sdf->axialStationCount):1e6f;
    bool has=sdf->axialStationCount>1;
    for(size_t i=0;i<sdf->bodyPartCount;++i) {
        float d=MonsterSDF_EvalBodyPartDistance(&sdf->bodyParts[i],point);
        if(!has){accumulated=d;has=true;}else accumulated=SDF_SmoothUnion(accumulated,d,sdf->config.bodySmoothness);
    }
    for(size_t i=0;i<sdf->connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf->connectors[i];
        if(sdf->axialStationCount>1 && c->kind==BODY_CONNECTION_AXIAL_LOFT)continue;
        if(c->fromId==ANATOMY_ID_HEAD&&c->toId==ANATOMY_ID_NECK)continue;
        if(sdf->config.enableConnectorPruning && has && MonsterSDF_ShouldPruneGroup(c,point,accumulated)) {
            if(tls_enableStats){tls_connectorCandidateCount+=c->groupCount;tls_connectorPrunedCount+=c->groupCount;}
            i+=c->groupCount-1;continue;
        }
        if (tls_enableStats) tls_connectorCandidateCount++;
        float smooth = c->localSmoothness;
        if (sdf->config.enableConnectorPruning && has &&
            MonsterSDF_ShouldPruneConnector(c, point, accumulated + smooth)) {
            if (tls_enableStats) tls_connectorPrunedCount++;
            continue;
        }
        if (tls_enableStats) tls_connectorExactCount++;
        float d=MonsterSDF_EvalConnectorDistance(c,point);
        if(!has){accumulated=d;has=true;}else accumulated=SDF_SmoothUnion(accumulated,d,smooth);
    }
    return accumulated;
}

static SDFSample MonsterSDF_EvaluateHeadAtWorld(const MonsterSDFMouth* mouth,Vector3 point) {
    Vector3 local=Transform3D_ApplyRotationBasis(mouth->inverseRotation,Vec3_Sub(point,mouth->center));
    SDFSample sample=SDFSample_Create(MonsterSDF_EvalUpperHeadDistance(mouth,local),mouth->skinColor,SDF_MATERIAL_SKIN);
    sample=SDFSample_Subtract(sample,SDFSample_Create(MonsterSDF_EvalMouthDistance(mouth,point,NULL),mouth->insideColor,SDF_MATERIAL_MOUTH),mouth->rimBevel);
    sample=SDFSample_Subtract(sample,SDFSample_Create(MonsterSDF_EvalOrbitCavities(mouth,local),Color_FromRGB(24,28,19),SDF_MATERIAL_EYE_SOCKET),0.0f);
    sample=SDFSample_Subtract(sample,SDFSample_Create(MonsterSDF_EvalNostrilCavities(mouth,local),Color_FromRGB(18,20,14),SDF_MATERIAL_NOSTRIL),mouth->headUnionSmoothness*.04f);
    if(mouth->hasTympana)sample=SDFSample_Subtract(sample,SDFSample_Create(MonsterSDF_EvalTympanumCavities(mouth,local),Color_FromRGB(25,24,16),SDF_MATERIAL_TYMPANUM),mouth->headUnionSmoothness*.04f);
    return sample;
}

static float MonsterSDF_EvaluateHeadDistanceAtWorld(const MonsterSDFMouth* mouth,Vector3 point) {
    Vector3 local=Transform3D_ApplyRotationBasis(mouth->inverseRotation,Vec3_Sub(point,mouth->center));
    float d=MonsterSDF_EvalUpperHeadDistance(mouth,local);
    d=SDF_SmoothSubtract(d,MonsterSDF_EvalMouthDistance(mouth,point,NULL),mouth->rimBevel);
    d=SDF_SmoothSubtract(d,MonsterSDF_EvalOrbitCavities(mouth,local),0.0f);
    d=SDF_SmoothSubtract(d,MonsterSDF_EvalNostrilCavities(mouth,local),mouth->headUnionSmoothness*.04f);
    if(mouth->hasTympana)d=SDF_SmoothSubtract(d,MonsterSDF_EvalTympanumCavities(mouth,local),mouth->headUnionSmoothness*.04f);
    return d;
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
            case MONSTER_HEAD_DEBUG_SNOUT:
            case MONSTER_HEAD_DEBUG_ROSTRUM: distance = MonsterSDF_EvalRostrumDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_UPPER_HEAD: distance = MonsterSDF_EvalUpperHeadDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_JAW: distance = MonsterSDF_EvalJawCarvedDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_BRIDGES: distance = MonsterSDF_EvalSeamDistance(mouth, local); material = SDF_MATERIAL_SKIN; color = mouth->skinColor; break;
            case MONSTER_HEAD_DEBUG_CAVITY: distance = SDF_Ellipsoid(Vec3_Sub(local, mouth->cavityCenterLocal), mouth->cavityRadii); break;
            case MONSTER_HEAD_DEBUG_SLIT: distance = SDF_RoundedSlotExtruded(Vec3_Sub(local, mouth->entranceCenterLocal), mouth->entranceHalfExtents.x, mouth->entranceHalfExtents.y, mouth->entranceHalfExtents.z); break;
            case MONSTER_HEAD_DEBUG_ORBIT_CAVITIES: distance=MonsterSDF_EvalOrbitCavities(mouth,local);material=SDF_MATERIAL_EYE_SOCKET;color=Color_FromRGB(24,28,19);break;
            case MONSTER_HEAD_DEBUG_PERIORBITAL: distance=MonsterSDF_EvalPeriorbitalDistance(mouth,local);material=SDF_MATERIAL_SKIN;color=mouth->skinColor;break;
            case MONSTER_HEAD_DEBUG_NOSTRILS: distance=MonsterSDF_EvalNostrilCavities(mouth,local);material=SDF_MATERIAL_NOSTRIL;color=Color_FromRGB(18,20,14);break;
            case MONSTER_HEAD_DEBUG_LOCAL_HEAD: return MonsterSDF_EvaluateHeadAtWorld(mouth,point);
            default: break;
        }
        if (distance < result.distance) result = SDFSample_Create(distance, color, material);
    }
    return result;
}

static float MonsterSDF_EvalJawCarvedDistance(const MonsterSDFMouth* mouth, Vector3 point) {
    float body = MonsterSDF_EvalJawBase(mouth, point);
    if(mouth->taperedMandible) {
        float tongue = MonsterSDF_EvalTongueDistance(mouth, point);
        float k = Math_Max(mouth->seamScale * .08f, .005f);
        return SDF_SmoothUnion(body, tongue, k);
    }
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
    if(mouth->taperedMandible) {
        float tongue = MonsterSDF_EvalTongueDistance(mouth, point);
        float k = Math_Max(mouth->seamScale * .08f, .005f);
        SDFSample bodySample = SDFSample_Create(body, mouth->skinColor, SDF_MATERIAL_SKIN);
        SDFSample tongueSample = SDFSample_Create(tongue, mouth->insideColor, SDF_MATERIAL_LIP);
        return SDFSample_SmoothUnion(bodySample, tongueSample, k);
    }
    Vector3 basinCenter, basinRadii; float k;
    MonsterSDF_GetBasinParams(mouth, &basinCenter, &basinRadii, &k);
    float oral = SDF_Ellipsoid(Vec3_Sub(point, basinCenter), basinRadii);
    return SDFSample_Subtract(SDFSample_Create(body, mouth->skinColor, SDF_MATERIAL_SKIN), SDFSample_Create(oral, mouth->insideColor, SDF_MATERIAL_MOUTH), k);
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
    if(mouth->taperedMandible) {
        float rearW=Math_Max(mouth->jawRearMass*.62f,r.x*.15f);
        pad=Math_Max(pad,rearW*1.35f);
    }
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
size_t MonsterSDF_GetComponentBounds(const MonsterSDF* sdf, AABB3D* outBoxes, size_t capacity) {
    if (!sdf || !outBoxes || capacity == 0) return 0;
    size_t count = 0;
    float smoothMargin = sdf->config.bodySmoothness + sdf->config.connectionSmoothness + 0.05f;

    /* 1. Barridos axiales estación a estación */
    if (sdf->axialStationCount > 1) {
        for (int i = 0; i < sdf->axialStationCount - 1 && count < capacity; ++i) {
            const SDFSweepStation* s0 = &sdf->axialStations[i];
            const SDFSweepStation* s1 = &sdf->axialStations[i + 1];
            float maxR0 = Math_Max(s0->width, s0->height);
            float maxR1 = Math_Max(s1->width, s1->height);
            float r = Math_Max(maxR0, maxR1) + smoothMargin;

            Vector3 minP = Vec3_Create(Math_Min(s0->center.x, s1->center.x),
                                       Math_Min(s0->center.y, s1->center.y),
                                       Math_Min(s0->center.z, s1->center.z));
            Vector3 maxP = Vec3_Create(Math_Max(s0->center.x, s1->center.x),
                                       Math_Max(s0->center.y, s1->center.y),
                                       Math_Max(s0->center.z, s1->center.z));
            AABB3D box = AABB_FromMinMax(Vec3_Sub(minP, Vec3_Create(r, r, r)),
                                         Vec3_Add(maxP, Vec3_Create(r, r, r)));
            outBoxes[count++] = box;
        }
    }

    /* 2. Conectores de extremidades / dedos */
    for (size_t i = 0; i < sdf->connectorCount && count < capacity; ++i) {
        AABB3D box = sdf->connectors[i].bounds;
        AABB_Pad(&box, sdf->connectors[i].localSmoothness + 0.02f);
        outBoxes[count++] = box;
    }

    /* 3. Partes de cuerpo discretas */
    for (size_t i = 0; i < sdf->bodyPartCount && count < capacity; ++i) {
        const MonsterSDFBodyPart* bp = &sdf->bodyParts[i];
        float r = bp->radii.x;
        if (bp->radii.y > r) r = bp->radii.y;
        if (bp->radii.z > r) r = bp->radii.z;
        r += smoothMargin;
        AABB3D box = AABB_FromMinMax(Vec3_Sub(bp->center, Vec3_Create(r, r, r)),
                                     Vec3_Add(bp->center, Vec3_Create(r, r, r)));
        outBoxes[count++] = box;
    }

    /* 4. Bocas y cabezas */
    for (size_t i = 0; i < sdf->mouthCount && count < capacity; ++i) {
        const MonsterSDFMouth* m = &sdf->mouths[i];
        if (AABB_Size(m->headBounds).x > 0.001f) {
            AABB3D box = m->headBounds;
            AABB_Pad(&box, 0.05f);
            outBoxes[count++] = box;
        }
        if (AABB_Size(m->influenceBounds).x > 0.001f && count < capacity) {
            AABB3D box = m->influenceBounds;
            AABB_Pad(&box, 0.05f);
            outBoxes[count++] = box;
        }
    }

    return count;
}

static size_t MonsterSDF_GetJawFieldComponentBoundsWrapper(const void* context, AABB3D* outBoxes, size_t capacity) {
    const MonsterSDFJawField* field = (const MonsterSDFJawField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount || capacity == 0) return 0;
    outBoxes[0] = MonsterSDF_GetJawBoundsWrapper(context);
    AABB_Pad(&outBoxes[0], 0.03f);
    return 1;
}

SDFField MonsterSDF_GetJawField(const MonsterSDF* sdf, size_t mouthIndex, MonsterSDFJawField* context) {
    if (!context) return (SDFField){0};
    context->owner=sdf; context->mouthIndex=mouthIndex;
    return (SDFField){.evaluate=MonsterSDF_EvaluateJawWrapper,.evaluateDistance=MonsterSDF_EvaluateJawDistanceWrapper,.getBounds=MonsterSDF_GetJawBoundsWrapper,.getComponentBounds=MonsterSDF_GetJawFieldComponentBoundsWrapper,.context=context};
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
static size_t MonsterSDF_GetSeamFieldComponentBoundsWrapper(const void* context, AABB3D* outBoxes, size_t capacity) {
    const MonsterSDFSeamField* field = (const MonsterSDFSeamField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount || capacity == 0) return 0;
    outBoxes[0] = field->owner->mouths[field->mouthIndex].seamBounds;
    AABB_Pad(&outBoxes[0], 0.03f);
    return 1;
}
SDFField MonsterSDF_GetSeamField(const MonsterSDF* sdf, size_t mouthIndex, MonsterSDFSeamField* context) {
    if (!context) return (SDFField){0};
    context->owner=sdf; context->mouthIndex=mouthIndex;
    return (SDFField){.evaluate=MonsterSDF_EvaluateSeamWrapper,.evaluateDistance=MonsterSDF_EvaluateSeamDistanceWrapper,.getBounds=MonsterSDF_GetSeamBoundsWrapper,.getComponentBounds=MonsterSDF_GetSeamFieldComponentBoundsWrapper,.context=context};
}

static SDFSample MonsterSDF_EvaluateBodyWrapper(const void* context,Vector3 point) {
    const MonsterSDFBodyField* field=(const MonsterSDFBodyField*)context;
    return field?MonsterSDF_EvaluateBodyPartition(field->owner,point):SDFSample_Create(1e6f,COLOR_WHITE,SDF_MATERIAL_UNKNOWN);
}
static float MonsterSDF_EvaluateBodyDistanceWrapper(const void* context,Vector3 point) {
    const MonsterSDFBodyField* field=(const MonsterSDFBodyField*)context;
    return field?MonsterSDF_EvaluateBodyPartitionDistance(field->owner,point):1e6f;
}
static AABB3D MonsterSDF_GetBodyFieldBoundsWrapper(const void* context) {
    const MonsterSDFBodyField* field=(const MonsterSDFBodyField*)context;
    return field&&field->owner?field->owner->bodyBounds:AABB_Empty();
}
static size_t MonsterSDF_GetBodyFieldComponentBoundsWrapper(const void* context, AABB3D* outBoxes, size_t capacity) {
    const MonsterSDFBodyField* field = (const MonsterSDFBodyField*)context;
    if (!field || !field->owner) return 0;
    return MonsterSDF_GetComponentBounds(field->owner, outBoxes, capacity);
}
SDFField MonsterSDF_GetBodyField(const MonsterSDF* sdf,MonsterSDFBodyField* context) {
    if(!context)return (SDFField){0};
    context->owner=sdf;
    return (SDFField){.evaluate=MonsterSDF_EvaluateBodyWrapper,.evaluateDistance=MonsterSDF_EvaluateBodyDistanceWrapper,.getBounds=MonsterSDF_GetBodyFieldBoundsWrapper,.getComponentBounds=MonsterSDF_GetBodyFieldComponentBoundsWrapper,.context=context};
}

static SDFSample MonsterSDF_EvaluateHeadWrapper(const void* context,Vector3 point) {
    const MonsterSDFHeadField* field=(const MonsterSDFHeadField*)context;
    if(!field||!field->owner||field->mouthIndex>=field->owner->mouthCount)return SDFSample_Create(1e6f,COLOR_WHITE,SDF_MATERIAL_UNKNOWN);
    const MonsterSDFMouth* mouth=&field->owner->mouths[field->mouthIndex];
    if(!mouth->anatomicalHead)return SDFSample_Create(1e6f,COLOR_WHITE,SDF_MATERIAL_UNKNOWN);
    return MonsterSDF_EvaluateHeadAtWorld(mouth,point);
}
static float MonsterSDF_EvaluateHeadDistanceWrapper(const void* context,Vector3 point) {
    const MonsterSDFHeadField* field=(const MonsterSDFHeadField*)context;
    if(!field||!field->owner||field->mouthIndex>=field->owner->mouthCount)return 1e6f;
    const MonsterSDFMouth* mouth=&field->owner->mouths[field->mouthIndex];
    return mouth->anatomicalHead?MonsterSDF_EvaluateHeadDistanceAtWorld(mouth,point):1e6f;
}
static AABB3D MonsterSDF_GetHeadBoundsWrapper(const void* context) {
    const MonsterSDFHeadField* field=(const MonsterSDFHeadField*)context;
    if(!field||!field->owner||field->mouthIndex>=field->owner->mouthCount)return AABB_Empty();
    return field->owner->mouths[field->mouthIndex].headBounds;
}
static size_t MonsterSDF_GetHeadFieldComponentBoundsWrapper(const void* context, AABB3D* outBoxes, size_t capacity) {
    const MonsterSDFHeadField* field = (const MonsterSDFHeadField*)context;
    if (!field || !field->owner || field->mouthIndex >= field->owner->mouthCount || capacity == 0) return 0;
    outBoxes[0] = field->owner->mouths[field->mouthIndex].headBounds;
    AABB_Pad(&outBoxes[0], 0.05f);
    return 1;
}
SDFField MonsterSDF_GetHeadField(const MonsterSDF* sdf,size_t mouthIndex,MonsterSDFHeadField* context) {
    if(!context)return (SDFField){0};
    context->owner=sdf;context->mouthIndex=mouthIndex;
    return (SDFField){.evaluate=MonsterSDF_EvaluateHeadWrapper,.evaluateDistance=MonsterSDF_EvaluateHeadDistanceWrapper,.getBounds=MonsterSDF_GetHeadBoundsWrapper,.getComponentBounds=MonsterSDF_GetHeadFieldComponentBoundsWrapper,.context=context};
}
float MonsterSDF_EvaluateDistanceWrapper(const void* context, Vector3 point) { return MonsterSDF_EvaluateDistance((const MonsterSDF*)context, point); }
AABB3D MonsterSDF_GetBounds(const MonsterSDF* sdf) { if (!sdf) return AABB_Empty(); return sdf->bounds; }
AABB3D MonsterSDF_GetBoundsWrapper(const void* context) { return MonsterSDF_GetBounds((const MonsterSDF*)context); }
/* Intervalos de las mismas recetas elípticas que evalúa el campo. Se conserva
 * la dependencia desconocida ensanchando el intervalo, nunca mediante un
 * supuesto sobre la derivada de una distancia aproximada. */
typedef struct MonsterSDFRange { double lo, hi; } MonsterSDFRange;
static MonsterSDFRange MonsterSDF_Range(double a,double b) {
    return (MonsterSDFRange){fmin(a,b),fmax(a,b)};
}
static bool MonsterSDF_BoxesOverlap(AABB3D a,AABB3D b) {
    return a.start.x<=b.end.x && a.end.x>=b.start.x &&
        a.start.y<=b.end.y && a.end.y>=b.start.y &&
        a.start.z<=b.end.z && a.end.z>=b.start.z;
}
static MonsterSDFRange MonsterSDF_ProjectRange(Vector3 center,Vector3 half,Vector3 basis) {
    double mid=(double)center.x*basis.x+(double)center.y*basis.y+(double)center.z*basis.z;
    double radius=(double)half.x*fabs(basis.x)+(double)half.y*fabs(basis.y)+(double)half.z*fabs(basis.z);
    return (MonsterSDFRange){mid-radius,mid+radius};
}
static MonsterSDFRange MonsterSDF_RangeSubtract(MonsterSDFRange a,MonsterSDFRange b) {
    return (MonsterSDFRange){a.lo-b.hi,a.hi-b.lo};
}
static MonsterSDFRange MonsterSDF_RangeSquareRatio(MonsterSDFRange a,MonsterSDFRange radius) {
    double lower=a.lo<=0 && a.hi>=0?0:fmin(fabs(a.lo),fabs(a.hi));
    double upper=fmax(fabs(a.lo),fabs(a.hi));
    lower/=radius.hi;upper/=radius.lo;
    return (MonsterSDFRange){lower*lower,upper*upper};
}
static MonsterSDFRange MonsterSDF_EllipticRange(MonsterSDFRange x,MonsterSDFRange y,
    MonsterSDFRange z,MonsterSDFRange w,MonsterSDFRange h) {
    w.lo=fmax(w.lo,.0001);w.hi=fmax(w.hi,.0001);
    h.lo=fmax(h.lo,.0001);h.hi=fmax(h.hi,.0001);
    MonsterSDFRange r={fmin(w.lo,h.lo),fmin(w.hi,h.hi)};
    MonsterSDFRange xx=MonsterSDF_RangeSquareRatio(x,w), yy=MonsterSDF_RangeSquareRatio(y,h),
        zz=MonsterSDF_RangeSquareRatio(z,r);
    double lo=sqrt(xx.lo+yy.lo+zz.lo)-1,hi=sqrt(xx.hi+yy.hi+zz.hi)-1;
    return (MonsterSDFRange){fmin(lo*r.lo,lo*r.hi),fmax(hi*r.lo,hi*r.hi)};
}
static double MonsterSDF_ClampDouble(double v,double lo,double hi) {
    return fmax(lo,fmin(hi,v));
}
static MonsterSDFRange MonsterSDF_ConnectorRange(const MonsterSDFConnector* connector,
    Vector3 center,Vector3 half) {
    Vector3 relative=Vec3_Sub(center,connector->a);
    MonsterSDFRange along=MonsterSDF_ProjectRange(relative,half,connector->forward);
    double ta=MonsterSDF_ClampDouble(along.lo/connector->length,0,1);
    double tb=MonsterSDF_ClampDouble(along.hi/connector->length,0,1);
    MonsterSDFRange w=MonsterSDF_Range(connector->widthA+(connector->widthB-connector->widthA)*ta,
        connector->widthA+(connector->widthB-connector->widthA)*tb);
    MonsterSDFRange h=MonsterSDF_Range(connector->heightA+(connector->heightB-connector->heightA)*ta,
        connector->heightA+(connector->heightB-connector->heightA)*tb);
    MonsterSDFRange z={along.lo<0?along.lo:along.lo>connector->length?along.lo-connector->length:0,
        along.hi<0?along.hi:along.hi>connector->length?along.hi-connector->length:0};
    return MonsterSDF_EllipticRange(MonsterSDF_ProjectRange(relative,half,connector->side),
        MonsterSDF_ProjectRange(relative,half,connector->up),z,w,h);
}
/* Los cuatro controles de Bézier del tramo restringido contienen la cúbica,
 * incluso si una receta futura usa tangentes no monótonas. */
static MonsterSDFRange MonsterSDF_HermiteRange(double a,double b,double da,double db,
    double span,double lo,double hi) {
    double aa=2*a-2*b+span*(da+db),bb=-3*a+3*b-span*(2*da+db),cc=span*da;
    double p0=((aa*lo+bb)*lo+cc)*lo+a,p3=((aa*hi+bb)*hi+cc)*hi+a;
    double p1=p0+(hi-lo)*(3*aa*lo*lo+2*bb*lo+cc)/3;
    double p2=p3-(hi-lo)*(3*aa*hi*hi+2*bb*hi+cc)/3;
    return (MonsterSDFRange){fmin(fmin(p0,p1),fmin(p2,p3)),fmax(fmax(p0,p1),fmax(p2,p3))};
}
static MonsterSDFRange MonsterSDF_SweepRange(const MonsterSDF* sdf,AABB3D box) {
    MonsterSDFRange result={INFINITY,-INFINITY};
    const SDFSweepStation* stations=sdf->axialStations;
    int count=sdf->axialStationCount;
    for(int i=0;i<count-1;++i) {
        const SDFSweepStation *a=&stations[i],*b=&stations[i+1];
        double lower=i==count-2?box.start.z:fmax(box.start.z,b->center.z);
        double upper=i==0?box.end.z:fmin(box.end.z,a->center.z);
        if(lower>upper)continue;
        double span=b->center.z-a->center.z;
        double ta=MonsterSDF_ClampDouble((upper-a->center.z)/span,0,1);
        double tb=MonsterSDF_ClampDouble((lower-a->center.z)/span,0,1);
        MonsterSDFRange w=MonsterSDF_HermiteRange(a->width,b->width,a->widthSlope,b->widthSlope,span,ta,tb);
        MonsterSDFRange h=MonsterSDF_HermiteRange(a->height,b->height,a->heightSlope,b->heightSlope,span,ta,tb);
        MonsterSDFRange cy=MonsterSDF_HermiteRange(a->center.y,b->center.y,a->centerSlope,b->centerSlope,span,ta,tb);
        MonsterSDFRange cx=MonsterSDF_Range(a->center.x+(b->center.x-a->center.x)*ta,
            a->center.x+(b->center.x-a->center.x)*tb);
        double first=stations[0].center.z,last=stations[count-1].center.z;
        MonsterSDFRange z={lower>first?lower-first:lower<last?lower-last:0,
            upper>first?upper-first:upper<last?upper-last:0};
        MonsterSDFRange range=MonsterSDF_EllipticRange(
            MonsterSDF_RangeSubtract((MonsterSDFRange){box.start.x,box.end.x},cx),
            MonsterSDF_RangeSubtract((MonsterSDFRange){box.start.y,box.end.y},cy),z,w,h);
        result.lo=fmin(result.lo,range.lo);result.hi=fmax(result.hi,range.hi);
    }
    return result;
}
static double MonsterSDF_RangeSmoothMin(double a,double b,double k) {
    if(k<=.0001 || fabs(a-b)>=k)return fmin(a,b);
    double h=MonsterSDF_ClampDouble(.5+.5*(b-a)/k,0,1);
    return b+(a-b)*h-k*h*(1-h);
}
static bool MonsterSDF_GetCellRangeWrapper(const void* context,AABB3D box,float* minimum,float* maximum) {
    const MonsterSDF* sdf=(const MonsterSDF*)context;
    if(!sdf || sdf->bodyPartCount || sdf->axialStationCount<2)return false;
    for(size_t i=0;i<sdf->mouthCount;++i)
        if(MonsterSDF_BoxesOverlap(box,sdf->mouths[i].influenceBounds) ||
           MonsterSDF_BoxesOverlap(box,sdf->mouths[i].headBounds))return false;
    Vector3 center=Vec3_Scale(Vec3_Add(box.start,box.end),.5f),half=Vec3_Scale(AABB_Size(box),.5f);
    MonsterSDFRange accumulated=MonsterSDF_SweepRange(sdf,box);
    for(size_t i=0;i<sdf->connectorCount;++i) {
        const MonsterSDFConnector* connector=&sdf->connectors[i];
        if(connector->kind==BODY_CONNECTION_AXIAL_LOFT)continue;
        if(connector->groupCount>1) {
            const AABB3D* group=&connector->groupBounds;
            double gx=fmax(0,fmax(group->start.x-box.end.x,box.start.x-group->end.x));
            double gy=fmax(0,fmax(group->start.y-box.end.y,box.start.y-group->end.y));
            double gz=fmax(0,fmax(group->start.z-box.end.z,box.start.z-group->end.z));
            double cutoff=accumulated.hi+connector->groupSmoothness;
            if((gx>0 || gy>0 || gz>0) &&
               (cutoff<0 || (gx*gx+gy*gy+gz*gz)*connector->groupLowerBoundScale*
                connector->groupLowerBoundScale>cutoff*cutoff)) {
                i+=connector->groupCount-1;continue;
            }
        }
        if(connector->length<1e-6f)return false;
        double dx=fmax(0,fmax(connector->bounds.start.x-box.end.x,box.start.x-connector->bounds.end.x));
        double dy=fmax(0,fmax(connector->bounds.start.y-box.end.y,box.start.y-connector->bounds.end.y));
        double dz=fmax(0,fmax(connector->bounds.start.z-box.end.z,box.start.z-connector->bounds.end.z));
        MonsterSDFRange range;
        if(dx>0 || dy>0 || dz>0) {
            range=(MonsterSDFRange){sqrt(dx*dx+dy*dy+dz*dz)*connector->distanceLowerBoundScale,INFINITY};
            if(range.lo>accumulated.hi+connector->localSmoothness)continue;
        } else range=MonsterSDF_ConnectorRange(connector,center,half);
        accumulated.lo=MonsterSDF_RangeSmoothMin(accumulated.lo,range.lo,connector->localSmoothness);
        accumulated.hi=MonsterSDF_RangeSmoothMin(accumulated.hi,range.hi,connector->localSmoothness);
    }
    /* Redondeo exterior frente a las operaciones float de la evaluación escalar. */
    double margin=1e-5*fmax(1,fmax(fabs(accumulated.lo),fabs(accumulated.hi)));
    *minimum=(float)(accumulated.lo-margin);*maximum=(float)(accumulated.hi+margin);
    return isfinite(*minimum) && isfinite(*maximum);
}

static size_t MonsterSDF_GetFieldComponentBoundsWrapper(const void* context, AABB3D* outBoxes, size_t capacity) {
    return MonsterSDF_GetComponentBounds((const MonsterSDF*)context, outBoxes, capacity);
}
SDFField MonsterSDF_GetField(const MonsterSDF* sdf) {
    return (SDFField){ .evaluate = MonsterSDF_EvaluateWrapper, .evaluateDistance = MonsterSDF_EvaluateDistanceWrapper, .getBounds = MonsterSDF_GetBoundsWrapper, .getComponentBounds = MonsterSDF_GetFieldComponentBoundsWrapper, .context = (const void*)sdf, .getCellRange = MonsterSDF_GetCellRangeWrapper };
}

static SDFDetailRegion MonsterSDF_Detail(const MonsterSDFMouth* mouth,
    Vector3 center,Vector3 radii,float voxel) {
    RotationBasis3D r=mouth->inverseRotation;
    RotationBasis3D world={Vec3_Create(r.row0.x,r.row1.x,r.row2.x),
        Vec3_Create(r.row0.y,r.row1.y,r.row2.y),Vec3_Create(r.row0.z,r.row1.z,r.row2.z)};
    Vector3 c=Vec3_Add(mouth->center,Transform3D_ApplyRotationBasis(world,center));
    Vector3 e=Vec3_Create(fabsf(world.row0.x)*radii.x+fabsf(world.row0.y)*radii.y+fabsf(world.row0.z)*radii.z,
        fabsf(world.row1.x)*radii.x+fabsf(world.row1.y)*radii.y+fabsf(world.row1.z)*radii.z,
        fabsf(world.row2.x)*radii.x+fabsf(world.row2.y)*radii.y+fabsf(world.row2.z)*radii.z);
    return (SDFDetailRegion){.bounds={Vec3_Sub(c,e),Vec3_Add(c,e)},.targetVoxelSize=voxel};
}

/* Una caja por autopodio evita multiplicar regiones por cada falange. Los
 * radios compilados incluyen el ahusamiento real del ungual más pequeño. */
static void MonsterSDF_AppendageDetails(const MonsterSDF* sdf, float samples,
    SDFDetailRegion* regions, size_t capacity, size_t* count) {
    if(sdf->appendageDevelopment<.20f)return;
    const AnatomyId wrists[4] = { ANATOMY_ID_FORE_LEFT_WRIST, ANATOMY_ID_FORE_RIGHT_WRIST,
        ANATOMY_ID_HIND_LEFT_ANKLE, ANATOMY_ID_HIND_RIGHT_ANKLE };
    const AnatomyId palms[4] = { ANATOMY_ID_FORE_LEFT_HAND, ANATOMY_ID_FORE_RIGHT_HAND,
        ANATOMY_ID_HIND_LEFT_FOOT, ANATOMY_ID_HIND_RIGHT_FOOT };
    for (int limb = 0; limb < 4; ++limb) {
        AABB3D autopod = AABB_Empty(), distal = AABB_Empty();
        float digitDiameter = INFINITY, distalDiameter = INFINITY;
        for (size_t i = 0; i < sdf->connectorCount; ++i) {
            const MonsterSDFConnector* c = &sdf->connectors[i];
            bool digit = c->kind == BODY_CONNECTION_DIGIT_SEGMENT &&
                (c->fromId >= ANATOMY_ID_DIGIT_BASE &&
                 c->toId >= Anatomy_DigitId((unsigned)limb, 0, 0) &&
                 c->toId <= Anatomy_DigitId((unsigned)limb, 4, 6));
            bool lower = c->kind == BODY_CONNECTION_LIMB_SEGMENT &&
                (c->toId == wrists[limb] || c->toId == palms[limb]);
            if (!digit && !lower) continue;
            float diameter = 2 * Math_Min(Math_Min(c->widthA, c->heightA),
                                           Math_Min(c->widthB, c->heightB));
            AABB3D* box = digit ? &autopod : &distal;
            AABB_ExpandPoint(box, c->bounds.start);
            AABB_ExpandPoint(box, c->bounds.end);
            if (digit) digitDiameter = Math_Min(digitDiameter, diameter);
            else distalDiameter = Math_Min(distalDiameter, diameter);
        }
        if (isfinite(digitDiameter) && *count < capacity) {
            float target = digitDiameter / samples;
            AABB_Pad(&autopod, target * 2);
            regions[(*count)++] = (SDFDetailRegion){autopod, target};
        }
        if (isfinite(distalDiameter) && *count < capacity) {
            float target = distalDiameter / samples;
            AABB_Pad(&distal, target * 2);
            regions[(*count)++] = (SDFDetailRegion){distal, target};
        }
    }
}

size_t MonsterSDF_GetDetailRegions(const MonsterSDF* sdf,float samples,
    SDFDetailRegion* regions,size_t capacity) {
    if(!sdf || !regions || !isfinite(samples) || samples<1) return 0;
    size_t n=0;
    for(size_t i=0;i<sdf->mouthCount && n+7<=capacity;++i) {
        const MonsterSDFMouth* m=&sdf->mouths[i];
        if(!m->anatomicalHead||m->cephalicDevelopment<.20f)continue;
        float base=m->craniumRadii.y*.20f;
        regions[n++]=(SDFDetailRegion){m->headBounds,base};
        float nasal=2*Math_Min(m->nostrilRadii.x,Math_Min(m->nostrilRadii.y,m->nostrilRadii.z))/samples;
        Vector3 radius=Vec3_Scale(m->nostrilRadii,1.8f);
        regions[n++]=MonsterSDF_Detail(m,m->leftNostrilCenterLocal,radius,nasal);
        regions[n++]=MonsterSDF_Detail(m,m->rightNostrilCenterLocal,radius,nasal);
        float orbital=2*Math_Min(m->orbitRadii.y,m->orbitRadii.z)/samples;
        radius=Vec3_Scale(m->orbitRadii,1.3f);
        regions[n++]=MonsterSDF_Detail(m,m->leftOrbitCenterLocal,radius,orbital);
        regions[n++]=MonsterSDF_Detail(m,m->rightOrbitCenterLocal,radius,orbital);
        float tym=2*Math_Min(m->tympanumRadii.y,m->tympanumRadii.z)/samples;
        radius=Vec3_Scale(m->tympanumRadii,1.5f);
        regions[n++]=MonsterSDF_Detail(m,m->leftTympanumCenterLocal,radius,tym);
        regions[n++]=MonsterSDF_Detail(m,m->rightTympanumCenterLocal,radius,tym);
    }
    MonsterSDF_AppendageDetails(sdf, samples, regions, capacity, &n);
    return n;
}

/* Inversa de la articulación continua usada por TransformJaw. El desarrollo
 * escala alrededor del pivote; la costura usa el mismo peso de anclajes. */
static float MonsterSDF_EvalPosedMouthDistance(const MonsterSDFMouth* mouth,Vector3 localP) {
    float scale=mouth->visualJawScale;
    if(scale<.0001f)return 1e6f;
    Vector3 pivot=mouth->visualJawPivot,rel=Vec3_Sub(localP,pivot);
    float outside=Vec3_Length(rel)-mouth->visualJawBoundRadius;
    if(outside>0)return Math_Max(outside,.01f);
    float angle=mouth->visualJawAngle,c=cosf(angle),s=sinf(angle);
    Vector3 rest=Vec3_Add(pivot,Vec3_Scale(Vec3_Create(rel.x,c*rel.y+s*rel.z,-s*rel.y+c*rel.z),1.0f/scale));
    float jaw=MonsterSDF_EvalJawCarvedDistance(mouth,rest)*scale;
    rest=Vec3_Add(pivot,Vec3_Scale(rel,1.0f/scale));
    float h=Math_Max(mouth->seamScale,.0001f);
    for(int iteration=0;iteration<6;++iteration) {
        float skull=Math_Min(Vec3_Distance(rest,mouth->seamSkullLeftLocal),Vec3_Distance(rest,mouth->seamSkullRightLocal));
        skull=Math_Min(skull,Vec3_Distance(rest,mouth->seamGularLocal));
        skull=Math_Min(skull,Vec3_Distance(rest,pivot));
        float lower=Math_Min(Vec3_Distance(rest,mouth->seamJawLeftClosedLocal),Vec3_Distance(rest,mouth->seamJawRightClosedLocal));
        lower=Math_Min(lower,Vec3_Distance(rest,mouth->seamJawAnchorLocal));
        float w=Math_Clamp01(skull/(skull+lower+h*1e-6f));
        float a=angle*w*w*(3.0f-2.0f*w);c=cosf(a);s=sinf(a);
        rest=Vec3_Add(pivot,Vec3_Scale(Vec3_Create(rel.x,c*rel.y+s*rel.z,-s*rel.y+c*rel.z),1.0f/scale));
    }
    return Math_Min(jaw,MonsterSDF_EvalSeamDistance(mouth,rest)*scale);
}

float MonsterSDF_EvaluateVisualDistance(const MonsterSDF* sdf,Vector3 point) {
    float distance=MonsterSDF_EvaluateDistance(sdf,point);
    if(!sdf)return distance;
    for(size_t i=0;i<sdf->mouthCount;++i) {
        const MonsterSDFMouth* mouth=&sdf->mouths[i];
        Vector3 local=Transform3D_ApplyRotationBasis(mouth->inverseRotation,Vec3_Sub(point,mouth->center));
        distance=Math_Min(distance,MonsterSDF_EvalPosedMouthDistance(mouth,local));
    }
    return distance;
}

static void MonsterSDF_ResolveVisualBounds(MonsterSDF* sdf,size_t index) {
    MonsterSDFMouth* m=&sdf->mouths[index];
    MonsterSDFJawField context={sdf,index};
    AABB3D boxes[2]={MonsterSDF_GetJawBoundsWrapper(&context),m->seamBounds};
    m->visualBounds=AABB_Empty();float maxRadius=0;
    for(int part=0;part<2;++part)for(int corner=0;corner<8;++corner) {
        AABB3D b=boxes[part];
        Vector3 p=Vec3_Create((corner&1)?b.end.x:b.start.x,(corner&2)?b.end.y:b.start.y,(corner&4)?b.end.z:b.start.z);
        Vector3 rel=Vec3_Scale(Vec3_Sub(p,m->visualJawPivot),m->visualJawScale);
        maxRadius=Math_Max(maxRadius,Vec3_Length(rel));
        for(int step=0;step<=8;++step) {
            float angle=m->visualJawAngle*(float)step/8.f,c=cosf(angle),s=sinf(angle);
            Vector3 q=Vec3_Add(m->visualJawPivot,Vec3_Create(rel.x,c*rel.y-s*rel.z,s*rel.y+c*rel.z));
            Vector3 world=Vec3_Add(m->center,Vec3_Add(Vec3_Scale(m->inverseRotation.row0,q.x),
                Vec3_Add(Vec3_Scale(m->inverseRotation.row1,q.y),Vec3_Scale(m->inverseRotation.row2,q.z))));
            AABB_ExpandPoint(&m->visualBounds,world);
        }
    }
    /* Cota por longitud de arco entre las muestras angulares consecutivas más margen de isosuperficie. */
    AABB_Pad(&m->visualBounds,maxRadius*fabsf(m->visualJawAngle)/16.f+0.15f);
}
