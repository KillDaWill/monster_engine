#include "Creature.h"
#include "MonsterAger.h"
#include "MonsterVisualAsync.h"
#include "AnatomyDeformer.h"
#include "MorphSurfacePatcher.h"
#include "CreatureMorph.h"
#include "SDFSamplingPool.h"
#include "PrimitiveMesh.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static double GetTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static bool CloneMorphMesh(Mesh* destination,const Mesh* source) {
    if(!destination||!source||
       !Mesh_ReserveVertices(destination,source->vertexCount)||
       !Mesh_ReserveIndices(destination,source->indexCount))return false;
    memcpy(destination->vertices,source->vertices,
        source->vertexCount*sizeof(*source->vertices));
    memcpy(destination->indices,source->indices,
        source->indexCount*sizeof(*source->indices));
    destination->vertexCount=source->vertexCount;
    destination->indexCount=source->indexCount;
    destination->surfaceRecipe=source->surfaceRecipe;
    destination->hasSurface=source->hasSurface;
    Mesh_MarkGeometryChanged(destination);
    Mesh_MarkSurfaceChanged(destination);
    return true;
}

static bool TrackedCloneMorphMesh(MonsterVisualAsync* asyncMgr,Mesh* destination,const Mesh* source) {
    bool ok=CloneMorphMesh(destination,source);
    if(ok && asyncMgr && source) {
        asyncMgr->stats.fullMeshClones++;
        asyncMgr->stats.geometryBytesCopied+=source->vertexCount*sizeof(MeshVertex)+source->indexCount*sizeof(MeshIndex);
    }
    return ok;
}

static size_t CountOpposedTriangles(const Mesh* mesh) {
    size_t opposed=0;
    for(size_t t=0;t+2<mesh->indexCount;t+=3) {
        MeshIndex ia=mesh->indices[t],ib=mesh->indices[t+1],ic=mesh->indices[t+2];
        if(ia>=mesh->vertexCount||ib>=mesh->vertexCount||ic>=mesh->vertexCount)
            continue;
        const MeshVertex* a=&mesh->vertices[ia];
        const MeshVertex* b=&mesh->vertices[ib];
        const MeshVertex* c=&mesh->vertices[ic];
        Vector3 area=Vec3_Cross(Vec3_Sub(b->position,a->position),
            Vec3_Sub(c->position,a->position));
        Vector3 normals=Vec3_Add(a->normal,Vec3_Add(b->normal,c->normal));
        opposed+=Vec3_Dot(area,normals)<-1e-8f;
    }
    return opposed;
}

static bool MorphMeshMatchesSDF(const Mesh* mesh,const MonsterSDF* sdf,float tolerance) {
    if(!mesh||!sdf||!mesh->vertexCount||!mesh->vertices)return false;
    size_t stride=mesh->vertexCount/2048+1;
    for(size_t v=0;v<mesh->vertexCount;v+=stride) {
        float d=MonsterSDF_EvaluateDistance(sdf,mesh->vertices[v].position);
        if(!isfinite(d)||fabsf(d)>tolerance)return false;
    }
    return true;
}

static bool StableInterfaceValid(const Mesh* mesh,const Mesh* certified,
    const uint8_t* frozen,const MorphAnchoredVertex* anchored) {
    if(!mesh||!certified||!frozen||mesh->vertexCount!=certified->vertexCount||
       mesh->indexCount!=certified->indexCount)return false;
    for(size_t t=0;t+2<mesh->indexCount;t+=3) {
        MeshIndex a=mesh->indices[t],b=mesh->indices[t+1],c=mesh->indices[t+2];
        if(a>=mesh->vertexCount||b>=mesh->vertexCount||c>=mesh->vertexCount)
            return false;
        unsigned frozenCount=frozen[a]+frozen[b]+frozen[c];
        if(!frozenCount||frozenCount==3)continue;
        Vector3 pa=mesh->vertices[a].position,pb=mesh->vertices[b].position,
            pc=mesh->vertices[c].position;
        Vector3 qa=frozen[a]?anchored[a].position:certified->vertices[a].position,
            qb=frozen[b]?anchored[b].position:certified->vertices[b].position,
            qc=frozen[c]?anchored[c].position:certified->vertices[c].position;
        Vector3 ab=Vec3_Sub(pb,pa),ac=Vec3_Sub(pc,pa),bc=Vec3_Sub(pc,pb);
        Vector3 qab=Vec3_Sub(qb,qa),qac=Vec3_Sub(qc,qa),qbc=Vec3_Sub(qc,qb);
        float edge=fmaxf(Vec3_LengthSq(ab),fmaxf(Vec3_LengthSq(ac),
            Vec3_LengthSq(bc)));
        float referenceEdge=fmaxf(Vec3_LengthSq(qab),
            fmaxf(Vec3_LengthSq(qac),Vec3_LengthSq(qbc)));
        Vector3 areaVector=Vec3_Cross(ab,ac),referenceAreaVector=Vec3_Cross(qab,qac);
        float area=Vec3_Length(areaVector);
        float referenceArea=Vec3_Length(referenceAreaVector);
        if(!isfinite(edge)||!isfinite(area)||
           edge>referenceEdge*9.0f+.0004f||
           (referenceArea>1e-8f&&
            (area<referenceArea*.25f||
             Vec3_Dot(areaVector,referenceAreaVector)<=0)))return false;
    }
    return true;
}

static bool StableInterfaceValidFast(const Mesh* mesh,const Mesh* certified,
    const size_t* interfaceTriangles,size_t triangleCount,
    const uint8_t* frozen,const MorphAnchoredVertex* anchored) {
    if(!mesh||!certified||mesh->vertexCount!=certified->vertexCount||
       mesh->indexCount!=certified->indexCount)return false;
    if(!interfaceTriangles||triangleCount==0)return true;
    for(size_t i=0;i<triangleCount;++i) {
        size_t t=interfaceTriangles[i];
        if(t+2>=mesh->indexCount)return false;
        MeshIndex a=mesh->indices[t],b=mesh->indices[t+1],c=mesh->indices[t+2];
        if(a>=mesh->vertexCount||b>=mesh->vertexCount||c>=mesh->vertexCount)
            return false;
        Vector3 pa=mesh->vertices[a].position,pb=mesh->vertices[b].position,
            pc=mesh->vertices[c].position;
        Vector3 qa=frozen[a]?anchored[a].position:certified->vertices[a].position,
            qb=frozen[b]?anchored[b].position:certified->vertices[b].position,
            qc=frozen[c]?anchored[c].position:certified->vertices[c].position;
        Vector3 ab=Vec3_Sub(pb,pa),ac=Vec3_Sub(pc,pa),bc=Vec3_Sub(pc,pb);
        Vector3 qab=Vec3_Sub(qb,qa),qac=Vec3_Sub(qc,qa),qbc=Vec3_Sub(qc,qb);
        float edge=fmaxf(Vec3_LengthSq(ab),fmaxf(Vec3_LengthSq(ac),
            Vec3_LengthSq(bc)));
        float referenceEdge=fmaxf(Vec3_LengthSq(qab),
            fmaxf(Vec3_LengthSq(qac),Vec3_LengthSq(qbc)));
        Vector3 areaVector=Vec3_Cross(ab,ac),referenceAreaVector=Vec3_Cross(qab,qac);
        float area=Vec3_Length(areaVector);
        float referenceArea=Vec3_Length(referenceAreaVector);
        if(!isfinite(edge)||!isfinite(area)||
           edge>referenceEdge*9.0f+.0004f||
           (referenceArea>1e-8f&&
            (area<referenceArea*.25f||
             Vec3_Dot(areaVector,referenceAreaVector)<=0)))return false;
    }
    return true;
}

static bool BuildDisplayTopologyMask(MonsterVisualAsync* asyncMgr) {
    free(asyncMgr->displayTopologyMask);
    asyncMgr->displayTopologyMask=NULL;
    free(asyncMgr->displayInterfaceTriangles);
    asyncMgr->displayInterfaceTriangles=NULL;
    asyncMgr->displayInterfaceTriangleCount=0;
    asyncMgr->stats.topologyFrozenVertices=0;
    if(!asyncMgr->morphModuleCount)return true;
    const AnatomyDeformer* binding=asyncMgr->displayMorph;
    if(!binding||!AnatomyDeformer_IsBound(binding)||
       binding->vertexCount!=asyncMgr->displayMesh.vertexCount)return false;
    uint8_t* mask=calloc(binding->vertexCount,sizeof(*mask));
    if(!mask)return false;
    for(size_t v=0;v<binding->vertexCount;++v) {
        int region=(int)lrintf(asyncMgr->displayMesh.vertices[v].surface.region);
        /* La influencia de una inserción no convierte el torso en un miembro
         * topológico congelado. Esta máscara limita solo predicción, no dibujo. */
        if(region!=SURFACE_REGION_FORELIMB&&region!=SURFACE_REGION_HINDLIMB&&
           region!=SURFACE_REGION_DIGIT&&region!=SURFACE_REGION_TAIL&&
           region!=SURFACE_REGION_ORNAMENT)continue;
        const AnatomyDeformerBinding* b=&binding->bindings[v];
        for(int k=0;k<ANATOMY_MORPH_INFLUENCES&&!mask[v];++k) {
            if(b->weight[k]<.15f||b->connIndex[k]>=binding->refGraph.connectionCount)
                continue;
            uint32_t module=binding->refGraph.connections[b->connIndex[k]].moduleInstanceId;
            for(size_t m=0;m<asyncMgr->morphModuleCount;++m)
                if(module==asyncMgr->morphModules[m].moduleId) {
                    mask[v]=1;
                    asyncMgr->stats.topologyFrozenVertices++;
                    break;
                }
        }
    }
    asyncMgr->displayTopologyMask=mask;

    /* Precomputar lista compacta de triángulos que tocan la frontera de interfaz mixta */
    if(asyncMgr->displayMesh.indexCount>=3) {
        size_t fullTriangles=asyncMgr->displayMesh.indexCount/3;
        size_t* triList=malloc(fullTriangles*sizeof(size_t));
        if(triList) {
            size_t count=0;
            for(size_t t=0;t+2<asyncMgr->displayMesh.indexCount;t+=3) {
                MeshIndex a=asyncMgr->displayMesh.indices[t];
                MeshIndex b=asyncMgr->displayMesh.indices[t+1];
                MeshIndex c=asyncMgr->displayMesh.indices[t+2];
                if(a<asyncMgr->displayMesh.vertexCount &&
                   b<asyncMgr->displayMesh.vertexCount &&
                   c<asyncMgr->displayMesh.vertexCount) {
                    unsigned frozenCount=mask[a]+mask[b]+mask[c];
                    if(frozenCount==1 || frozenCount==2) {
                        triList[count++]=t;
                    }
                }
            }
            if(count>0) {
                size_t* shrunk=realloc(triList,count*sizeof(size_t));
                asyncMgr->displayInterfaceTriangles=shrunk?shrunk:triList;
                asyncMgr->displayInterfaceTriangleCount=count;
            } else {
                free(triList);
            }
        }
    }
    return true;
}

/* Preparar todos los estados antes de sustituir el ancla publicada. */
static bool CommitMorphPatch(Mesh* visible,Mesh* anchor,
    AnatomyDeformer** binding,HeadMorph** head,MorphSurfaceTracker* tracker,
    Mesh* patch,const MeshPatchTransfer* transfer,const Monster* target,
    SDFSamplingPool* pool,float age) {
    AnatomyDeformer* nextBinding=AnatomyDeformer_Create();
    HeadMorph* nextHead=HeadMorph_Create();
    MorphSurfaceTracker nextTracker={0};
    Mesh nextAnchor=Mesh_Create();
    bool ok=nextBinding&&nextHead&&
        AnatomyDeformer_Copy(nextBinding,*binding)&&
        AnatomyDeformer_Reanchor(nextBinding,patch,&target->anatomyGraph,
            transfer->oldVertexOfNew,NULL)&&
        HeadMorph_Copy(nextHead,*head);
    if(ok&&target->hasHead&&
       target->head.anatomy.attachmentBodyPartIndex<target->bodyPartCount)
        ok=HeadMorph_Bind(nextHead,patch,&target->head.anatomy,
            target->bodyParts[target->head.anatomy.attachmentBodyPartIndex].positionRender);
    if(ok)ok=MorphSurfaceTracker_Bind(&nextTracker,patch,age)&&
        CloneMorphMesh(&nextAnchor,patch);
    if(ok) {
        MorphSurfaceTracker_SetPool(&nextTracker,pool);
        MorphSurfaceTracker_AuditQuality(&nextTracker,patch);
        ok=nextTracker.stats.valid&&nextTracker.stats.watertight;
    }
    if(ok) {
        Mesh previous=*visible;*visible=*patch;*patch=Mesh_Create();Mesh_Free(&previous);
        previous=*anchor;*anchor=nextAnchor;nextAnchor=Mesh_Create();Mesh_Free(&previous);
        AnatomyDeformer* oldBinding=*binding;*binding=nextBinding;nextBinding=oldBinding;
        HeadMorph* oldHead=*head;*head=nextHead;nextHead=oldHead;
        MorphSurfaceTracker oldTracker=*tracker;*tracker=nextTracker;nextTracker=oldTracker;
    }
    Mesh_Free(&nextAnchor);
    MorphSurfaceTracker_Free(&nextTracker);
    AnatomyDeformer_Free(nextBinding);
    HeadMorph_Free(nextHead);
    return ok;
}

static uint64_t Fnv1a64Bytes(const void* data, size_t size, uint64_t hash) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static inline uint64_t HashFloat(float v, uint64_t hash) {
    return Fnv1a64Bytes(&v, sizeof(float), hash);
}

static inline uint64_t HashVector3(Vector3 v, uint64_t hash) {
    hash = HashFloat(v.x, hash);
    hash = HashFloat(v.y, hash);
    return HashFloat(v.z, hash);
}

static inline uint64_t HashBool(bool b, uint64_t hash) {
    unsigned char v = b ? 1 : 0;
    return Fnv1a64Bytes(&v, 1, hash);
}

static inline uint64_t HashColor(Color c, uint64_t hash) {
    hash = Fnv1a64Bytes(&c.r, 1, hash);
    hash = Fnv1a64Bytes(&c.g, 1, hash);
    hash = Fnv1a64Bytes(&c.b, 1, hash);
    return Fnv1a64Bytes(&c.a, 1, hash);
}

static inline uint64_t HashSizeT(size_t v, uint64_t hash) {
    return Fnv1a64Bytes(&v, sizeof(size_t), hash);
}

/* Hash por campos: la anatomía resuelta puede cambiar sin editar el fenotipo. */
static uint64_t HashHeadSurface(const HeadSurfaceRecipe* surface,uint64_t hash) {
    hash=HashVector3(surface->craniumCenter,hash);
    hash=HashVector3(surface->craniumRadii,hash);
    hash=HashVector3(surface->neckCollarRoot,hash);
    hash=HashFloat(surface->neckCollarPenetration,hash);
    hash=HashVector3(surface->neckCollarRadii,hash);
    hash=HashVector3(surface->cranialPostorbitalCenter,hash);
    hash=HashVector3(surface->cranialTemporalCenter,hash);
    hash=HashVector3(surface->cranialOccipitalCenter,hash);
    hash=HashVector3(surface->faceRoot,hash);
    hash=HashVector3(surface->faceMid,hash);
    hash=HashVector3(surface->faceTip,hash);
    hash=HashVector3(surface->faceRootRadii,hash);
    hash=HashVector3(surface->faceMidRadii,hash);
    hash=HashVector3(surface->faceTipRadii,hash);
    hash=HashVector3(surface->leftCheekCenter,hash);
    hash=HashVector3(surface->rightCheekCenter,hash);
    hash=HashVector3(surface->cheekRadii,hash);
    hash=HashVector3(surface->leftTemporalCenter,hash);
    hash=HashVector3(surface->rightTemporalCenter,hash);
    hash=HashVector3(surface->temporalRadii,hash);
    hash=HashVector3(surface->leftMaxillaryCenter,hash);
    hash=HashVector3(surface->rightMaxillaryCenter,hash);
    hash=HashVector3(surface->maxillaryRadii,hash);
    hash=HashVector3(surface->leftBrowCenter,hash);
    hash=HashVector3(surface->rightBrowCenter,hash);
    hash=HashVector3(surface->browRadii,hash);
    hash=HashVector3(surface->leftOrbitCenter,hash);
    hash=HashVector3(surface->rightOrbitCenter,hash);
    hash=HashVector3(surface->orbitRadii,hash);
    hash=HashVector3(surface->leftOrbitRimCenter,hash);
    hash=HashVector3(surface->rightOrbitRimCenter,hash);
    hash=HashVector3(surface->orbitRimRadii,hash);
    hash=HashVector3(surface->leftOrbitNormal,hash);
    hash=HashVector3(surface->rightOrbitNormal,hash);
    hash=HashFloat(surface->orbitSocketDepth,hash);
    hash=HashFloat(surface->eyelidCoverage,hash);
    hash=HashVector3(surface->noseCenter,hash);
    hash=HashVector3(surface->noseRadii,hash);
    hash=HashVector3(surface->leftNostrilCenter,hash);
    hash=HashVector3(surface->rightNostrilCenter,hash);
    hash=HashVector3(surface->nostrilRadii,hash);
    hash=HashVector3(surface->leftTympanumCenter,hash);
    hash=HashVector3(surface->rightTympanumCenter,hash);
    hash=HashVector3(surface->tympanumRadii,hash);
    hash=HashFloat(surface->tympanumDepth,hash);
    hash=HashVector3(surface->leftEarCenter,hash);
    hash=HashVector3(surface->rightEarCenter,hash);
    hash=HashVector3(surface->earRadii,hash);
    hash=HashVector3(surface->earShape,hash);
    hash=HashVector3(surface->earDirection,hash);
    hash=HashVector3(surface->rightEarDirection,hash);
    hash=HashVector3(surface->leftEarSide,hash);
    hash=HashVector3(surface->rightEarSide,hash);
    hash=HashVector3(surface->leftEarNormal,hash);
    hash=HashVector3(surface->rightEarNormal,hash);
    hash=HashFloat(surface->earConcavity,hash);
    hash=HashFloat(surface->earTipFraction,hash);
    hash=HashFloat(surface->earLongitudinalCurve,hash);
    hash=HashFloat(surface->earRootRoll,hash);
    hash=HashFloat(surface->earTipRoundness,hash);
    hash=HashFloat(surface->earFold,hash);
    hash=HashFloat(surface->earRootFlare,hash);
    hash=HashFloat(surface->earMarginBow,hash);
    hash=HashFloat(surface->earMarginAsymmetry,hash);
    hash=HashVector3(surface->cranialPostorbitalRadii,hash);
    hash=HashVector3(surface->cranialTemporalRadii,hash);
    hash=HashVector3(surface->cranialOccipitalRadii,hash);
    hash=HashFloat(surface->unionSmoothness,hash);
    hash=HashFloat(surface->headBodySmoothness,hash);
    hash=HashBool(surface->hasNasalPad,hash);
    hash=HashBool(surface->hasEars,hash);
    hash=HashBool(surface->sweptSkull,hash);
    hash=HashBool(surface->isBeak,hash);
    hash=HashFloat(surface->tympanumDevelopment,hash);
    hash=HashBool(surface->hasTympana,hash);
    hash=HashFloat(surface->faceRounding,hash);
    return hash;
}

static uint64_t ComputeMonsterFingerprint(const Monster* monster, MonsterSDFConfig sdfConfig, MonsterVisualQualityTier tier) {
    uint64_t hash = 0xcbf29ce484222325ULL;

    hash = HashSizeT((size_t)tier, hash);

    /* Configuración SDF */
    hash = HashFloat(sdfConfig.bodySmoothness, hash);
    hash = HashFloat(sdfConfig.connectionSmoothness, hash);
    hash = HashFloat(sdfConfig.mouthSmoothness, hash);
    hash = HashFloat(sdfConfig.connectionRadiusFactor, hash);
    hash = HashFloat(sdfConfig.boundsPadding, hash);
    hash = HashBool(sdfConfig.enableConnectorPruning, hash);

    if (!monster) return hash;
    hash=HashSizeT(monster->recipeId,hash);
    hash=HashBool(monster->hasResolvedMorph,hash);
    if(monster->hasResolvedMorph && monster->hasHead)hash=HashHeadSurface(&monster->head.anatomy.surface,hash);
    hash = HashBool(monster->hasHead,hash);
    if(monster->hasHead) hash=Fnv1a64Bytes(&monster->head.phenotype,sizeof(monster->head.phenotype),hash);
    hash=HashBool(monster->hasAnatomyGraph,hash);
    if(monster->hasAnatomyGraph) {
        uint64_t anatomy=AnatomyGraph_Fingerprint(&monster->anatomyGraph);
        hash=Fnv1a64Bytes(&anatomy,sizeof(anatomy),hash);
    }

    /* Partes del cuerpo */
    hash = HashSizeT(monster->bodyPartCount, hash);
    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        const BodyPart* part = &monster->bodyParts[i];
        hash = HashVector3(part->positionRender, hash);
        hash = HashFloat(part->widthRender, hash);
        hash = HashFloat(part->heightRender, hash);
        hash = HashFloat(part->lengthRender, hash);
        hash = HashSizeT(part->color.index, hash);
    }

    /* Bocas */
    hash = HashSizeT(monster->mouthCount, hash);
    for (size_t i = 0; i < monster->mouthCount; ++i) {
        const Mouth* mouth = &monster->mouths[i];
        hash = HashSizeT(mouth->bodyPartIndex, hash);
        hash = HashSizeT((size_t)mouth->shape,hash);
        hash = HashVector3(mouth->offset, hash);
        hash = HashVector3(mouth->rotation, hash);
        hash = HashVector3(mouth->scale, hash);
        hash = HashFloat(mouth->slitThickness, hash);
        hash = HashFloat(mouth->slitSoftness, hash);
        hash = HashFloat(mouth->cornerRadius, hash);
        hash = HashVector3(mouth->jawPivot, hash);
        hash = HashFloat(mouth->jawLength, hash);
        hash = HashFloat(mouth->jawWidth, hash);
        hash = HashFloat(mouth->jawThickness, hash);
        hash = HashFloat(mouth->jawRearMass, hash);
        hash = HashFloat(mouth->jawMuscle, hash);
        hash = HashFloat(mouth->maxJawAngle, hash);
        hash = HashFloat(mouth->hingeRadius, hash);
        hash = HashFloat(mouth->throatRadius, hash);
        hash = HashVector3(mouth->cranium, hash);
        hash = HashVector3(mouth->snout, hash);
        hash = HashVector3(mouth->cheeks, hash);
        hash = HashVector3(mouth->brows, hash);
        hash = HashColor(mouth->insideColor, hash);
    }

    /* Ojos */
    hash = HashSizeT(monster->eyeCount, hash);
    for (size_t i = 0; i < monster->eyeCount; ++i) {
        const Eye* eye = &monster->eyes[i];
        hash = HashSizeT(eye->bodyPartIndex, hash);
        hash = HashVector3(eye->offset, hash);
        hash = HashVector3(eye->rotation, hash);
        hash = HashVector3(eye->forward, hash);
        hash = HashVector3(eye->scale, hash);
        hash = HashFloat(eye->irisScale, hash);
        hash = HashFloat(eye->pupilScale, hash);
        hash = HashFloat(eye->pupilAspect, hash);
        hash = HashSizeT(eye->pupilShape, hash);
        hash = HashColor(eye->scleraColor, hash);
        hash = HashColor(eye->irisColor, hash);
        hash = HashColor(eye->pupilColor, hash);
    }

    /* Paleta de colores */
    hash = HashSizeT(monster->colorPalette.count, hash);
    for (size_t i = 0; i < monster->colorPalette.count; ++i) {
        hash = HashColor(monster->colorPalette.colors[i], hash);
    }

    return hash;
}

MonsterVisualAsyncConfig MonsterVisualAsync_DefaultConfig(void) {
    MonsterVisualAsyncConfig cfg;
    memset(&cfg, 0, sizeof(MonsterVisualAsyncConfig));

    cfg.interactiveMesherConfig = SDFMesher_DefaultConfig();
    cfg.interactiveMesherConfig.voxelSize = 0.12f;
    cfg.interactiveMesherConfig.maxCells = 250000;

    cfg.settledMesherConfig = SDFMesher_DefaultConfig();
    cfg.settledMesherConfig.voxelSize = 0.08f;
    cfg.settledMesherConfig.maxCells = 500000;

    cfg.morphMesherConfig = SDFMesher_DefaultConfig();
    cfg.morphMesherConfig.voxelSize = 0.14f;
    cfg.morphMesherConfig.maxCells = 260000;

    cfg.interactiveHeadMesherConfig = SDFMesher_DefaultConfig();
    cfg.interactiveHeadMesherConfig.voxelSize = 0.024f;
    cfg.interactiveHeadMesherConfig.maxCells = 900000;
    cfg.interactiveHeadMesherConfig.maxResolution = 256;

    cfg.settledHeadMesherConfig = SDFMesher_DefaultConfig();
    cfg.settledHeadMesherConfig.voxelSize = 0.015f;
    cfg.settledHeadMesherConfig.maxCells = 2200000;
    cfg.settledHeadMesherConfig.maxResolution = 384;

    cfg.morphHeadMesherConfig = SDFMesher_DefaultConfig();
    cfg.morphHeadMesherConfig.voxelSize = 0.035f;
    cfg.morphHeadMesherConfig.maxCells = 300000;
    cfg.morphHeadMesherConfig.maxResolution = 256;

    cfg.sdfConfig = MonsterSDF_DefaultConfig();
    cfg.enableChunkedRemeshing = true;
    cfg.chunkSize = 0.45f;
    cfg.settledDelaySec = 0.15f;
    cfg.maxGeometryAgeLag = .10f;
    return cfg;
}

SDFMesherConfig MonsterVisualAsync_ResolveBodyConfig(const MonsterVisualAsyncConfig* c,
    MonsterVisualQualityTier tier, bool hasUnifiedAnatomy) {
    SDFMesherConfig body=tier==MONSTER_VISUAL_QUALITY_SETTLED?c->settledMesherConfig:
        tier==MONSTER_VISUAL_QUALITY_MORPH?c->morphMesherConfig:c->interactiveMesherConfig;
    SDFMesherConfig head=tier==MONSTER_VISUAL_QUALITY_SETTLED?c->settledHeadMesherConfig:
        tier==MONSTER_VISUAL_QUALITY_MORPH?c->morphHeadMesherConfig:c->interactiveHeadMesherConfig;
    if(hasUnifiedAnatomy) {
        body.adaptiveDetail=true;
        body.maxCells+=head.maxCells/2;
        if(body.maxResolution<head.maxResolution)body.maxResolution=head.maxResolution;
    }
    return body;
}

static void FreeEyeArray(MonsterVisualEyeAsync* eyes, size_t count) {
    if (!eyes) return;
    for (size_t i = 0; i < count; ++i) {
        Mesh_Free(&eyes[i].globe);
    }
    free(eyes);
}

static void FreeMouthArray(MonsterVisualMouth* mouths, size_t count) {
    if (!mouths) return;
    for (size_t i = 0; i < count; ++i) {
        MonsterVisualMouth_Free(&mouths[i]);
    }
    free(mouths);
}

static bool WorkerShouldCancel(void* context) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)context;
    if (!asyncMgr) return true;
    /* En movimiento sostenido se termina el snapshot en curso para evitar
     * bloqueos de cancelación (livelock); fuera de movimiento continuo se
     * cancela ante una nueva solicitud pendiente para atenderla de inmediato. */
    pthread_mutex_lock(&asyncMgr->lock);
    bool cancel = asyncMgr->shouldQuit ||
        (asyncMgr->displayGeneration != 0 && !asyncMgr->continuousMotion &&
         asyncMgr->hasPendingRequest);
    pthread_mutex_unlock(&asyncMgr->lock);
    return cancel;
}

/* La corrección de una fase terminada sigue siendo útil, pero el resto del
 * trabajo debe perseguir la última edad solicitada del mismo morph. */
static bool WorkerAdoptLatestMorphRequest(MonsterVisualAsync* asyncMgr,
    uint64_t endpointGeneration,Monster* workMonster,uint64_t* fingerprint) {
    bool adopted=false;
    pthread_mutex_lock(&asyncMgr->lock);
    if(asyncMgr->hasPendingRequest && asyncMgr->hasMorphEndpoints &&
       asyncMgr->morphEndpointGeneration==endpointGeneration &&
       asyncMgr->pendingTier==MONSTER_VISUAL_QUALITY_MORPH &&
       asyncMgr->pendingSnapshot.hasResolvedMorph &&
       asyncMgr->pendingSnapshot.hasGrowthAge) {
        Monster_CopyInto(workMonster,&asyncMgr->pendingSnapshot);
        *fingerprint=asyncMgr->pendingFingerprint;
        asyncMgr->hasPendingRequest=false;
        asyncMgr->stats.workingFingerprint=*fingerprint;
        asyncMgr->stats.workingGeometryAge=workMonster->growthAge;
        asyncMgr->stats.workingScale=workMonster->hasCreaturePhenotype?
            workMonster->phenotype.axial.totalScale:0;
        asyncMgr->stats.coalescedCount++;
        asyncMgr->stats.workerRetargetCount++;
        adopted=true;
    }
    pthread_mutex_unlock(&asyncMgr->lock);
    return adopted;
}

static bool WorkerMorphRequestIsNear(MonsterVisualAsync* asyncMgr,
    uint64_t endpointGeneration,float age) {
    pthread_mutex_lock(&asyncMgr->lock);
    bool near=asyncMgr->morphEndpointGeneration==endpointGeneration &&
        (!asyncMgr->hasPendingRequest ||
         (asyncMgr->pendingTier==MONSTER_VISUAL_QUALITY_MORPH &&
          asyncMgr->pendingSnapshot.hasGrowthAge &&
          fabsf(asyncMgr->pendingSnapshot.growthAge-age)<=.075f));
    pthread_mutex_unlock(&asyncMgr->lock);
    return near;
}

static void* WorkerThreadRoutine(void* arg) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)arg;

    MonsterSDF workerSdf = MonsterSDF_Create();
    SDFMesher workerMesher = SDFMesher_Create(asyncMgr->config.interactiveMesherConfig);
    SDFMesher workerHeadMesher = SDFMesher_Create(asyncMgr->config.interactiveHeadMesherConfig);
    SDFMesherConfig jawCfg = SDFMesher_DefaultConfig(); jawCfg.voxelSize = 0.04f; jawCfg.maxCells = 100000; jawCfg.useAutoBounds = true;
    SDFMesherConfig seamCfg = SDFMesher_DefaultConfig(); seamCfg.voxelSize = 0.03f; seamCfg.maxCells = 120000; seamCfg.useAutoBounds = true;
    SDFMesher workerJawMesher = SDFMesher_Create(jawCfg);
    SDFMesher workerSeamMesher = SDFMesher_Create(seamCfg);

    SDFMesher_SetSamplingPool(&workerMesher, asyncMgr->samplingPool);
    SDFMesher_SetSamplingPool(&workerHeadMesher, asyncMgr->samplingPool);
    SDFMesher_SetSamplingPool(&workerJawMesher, asyncMgr->samplingPool);
    SDFMesher_SetSamplingPool(&workerSeamMesher, asyncMgr->samplingPool);

    Monster workMonster = Monster_Create();
    Mesh workBodyMesh = Mesh_Create();
    Mesh workHeadMesh = Mesh_Create();
    AnatomyDeformer* workerMorph = AnatomyDeformer_Create();
    HeadMorph* workerHeadMorph = HeadMorph_Create();
    Monster canonicalMonster=Monster_Create();
    CreaturePhenotype canonicalPhenotype={0};
    bool canonicalReady=false;
    CreatureRecipe canonicalRecipe={0};
    Mesh canonicalMesh = Mesh_Create();
    AnatomyDeformer* canonicalMorph = AnatomyDeformer_Create();
    HeadMorph* canonicalHeadMorph = HeadMorph_Create();
    bool canonicalMorphReady = false;
    Monster morphStart=Monster_Create(),morphEnd=Monster_Create();
    Monster morphAnchor=Monster_Create(),morphStage=Monster_Create();
    MonsterSDF anchorSdf=MonsterSDF_Create();
    Mesh anchorMesh=Mesh_Create();
    AnatomyDeformer* anchorMorph=AnatomyDeformer_Create();
    HeadMorph* anchorHead=HeadMorph_Create();
    MorphSurfaceTracker dynamicTracker={0};
    uint64_t anchorGeneration=0;
    bool anchorReady=false;
    size_t anchorComponentCount=0;
    CreatureMorphTopologyEvent topologyEvents[ANATOMY_MAX_NODES];
    size_t topologyEventCount=0;
    float workerChunkSize = asyncMgr->config.chunkSize > 0.05f ? asyncMgr->config.chunkSize : 0.45f;
    MorphChunkManager* workerChunkMgr = MorphChunkManager_Create(workerChunkSize, 0.08f);
    MorphSpatialCache* workerSpatialCache = MorphSpatialCache_Create(workerChunkSize * 0.5f);
    Monster prevWorkMonster = Monster_Create();
    bool hasPrevMonster = false;

    while (1) {
        pthread_mutex_lock(&asyncMgr->lock);
        while (!asyncMgr->hasPendingRequest && !asyncMgr->shouldQuit) {
            pthread_cond_wait(&asyncMgr->cond, &asyncMgr->lock);
        }

        if (asyncMgr->shouldQuit) {
            pthread_mutex_unlock(&asyncMgr->lock);
            break;
        }

        Monster_CopyInto(&workMonster, &asyncMgr->pendingSnapshot);
        bool hasMorphEndpoints=asyncMgr->hasMorphEndpoints;
        uint64_t endpointGeneration=asyncMgr->morphEndpointGeneration;
        if(hasMorphEndpoints && endpointGeneration!=anchorGeneration) {
            Monster_CopyInto(&morphStart,&asyncMgr->morphStartSnapshot);
            Monster_CopyInto(&morphEnd,&asyncMgr->morphEndSnapshot);
            anchorReady=false;
            anchorComponentCount=0;
            topologyEventCount=CreatureMorph_CollectTopologyEvents(&morphStart,
                &morphEnd,topologyEvents,ANATOMY_MAX_NODES);
            if(topologyEventCount>ANATOMY_MAX_NODES)
                topologyEventCount=0;
        }

        uint64_t workFingerprint = asyncMgr->pendingFingerprint;
        MonsterVisualQualityTier workTier = asyncMgr->pendingTier;
        unsigned workerDelayMs=asyncMgr->config.workerDelayMs;
        asyncMgr->hasPendingRequest = false;
        asyncMgr->stats.isWorkerBusy = true;
        asyncMgr->stats.workingGeometryAge=workMonster.hasGrowthAge?workMonster.growthAge:NAN;
        asyncMgr->stats.workingFingerprint=workFingerprint;
        asyncMgr->stats.workingScale=workMonster.hasCreaturePhenotype?workMonster.phenotype.axial.totalScale:0;

        pthread_mutex_unlock(&asyncMgr->lock);
        if(workerDelayMs)usleep((useconds_t)workerDelayMs*1000u);

        /* Referencia propiedad del mismo blueprint, incluso para recetas externas. */
        if(workMonster.hasCreaturePhenotype && !workMonster.hasResolvedMorph) {
            const CreatureRecipe* recipe=&workMonster.recipe;
            bool changed=!canonicalReady||canonicalRecipe.id!=recipe->id||
                memcmp(&canonicalRecipe.adult,&recipe->adult,sizeof(recipe->adult))!=0||
                !CreaturePhenotype_MorphCompatible(&canonicalPhenotype,&recipe->adult)||
                !AnatomyGraph_TopologyCompatible(&canonicalMonster.anatomyGraph,&workMonster.anatomyGraph);
            if(changed) {
                canonicalRecipe=*recipe; canonicalPhenotype=recipe->adult;
                canonicalReady=Creature_ResolveAppearance(&canonicalMonster,recipe,&canonicalPhenotype)&&
                    AnatomyGraph_TopologyCompatible(&canonicalMonster.anatomyGraph,&workMonster.anatomyGraph);
                canonicalMorphReady=false; Mesh_Clear(&canonicalMesh);
            }
        } else { canonicalReady=false; canonicalMorphReady=false; }
        /* --- TRABAJO PESADO FUERA DEL MUTEX --- */
        double tStart = GetTimeMs();
        float sdfBuildMs=0,bodyMeshMs=0,headMeshMs=0,mappingMs=0,eyeMs=0,mouthMs=0,morphBindingMs=0,headBindingMs=0;
        SurfaceMapperStats mappingStats={0},headMappingStats={0};

        SDFMesherConfig activeMesherCfg;
        SDFMesherConfig activeHeadCfg;
        if (workTier == MONSTER_VISUAL_QUALITY_SETTLED) {
            activeMesherCfg = asyncMgr->config.settledMesherConfig;
            activeHeadCfg = asyncMgr->config.settledHeadMesherConfig;
        } else if (workTier == MONSTER_VISUAL_QUALITY_MORPH) {
            activeMesherCfg = asyncMgr->config.morphMesherConfig;
            activeHeadCfg = asyncMgr->config.morphHeadMesherConfig;
        } else {
            activeMesherCfg = asyncMgr->config.interactiveMesherConfig;
            activeHeadCfg = asyncMgr->config.interactiveHeadMesherConfig;
        }

        if(workMonster.hasHead && !workMonster.hasCreaturePhenotype) {
            float recommended=HeadAnatomy_RecommendedVoxelSize(&workMonster.head.anatomy);
            float tierTarget=workTier==MONSTER_VISUAL_QUALITY_SETTLED?recommended:recommended*1.75f;
            if(activeHeadCfg.voxelSize<=0.0001f||activeHeadCfg.voxelSize>tierTarget)
                activeHeadCfg.voxelSize=tierTarget;
        }

        activeMesherCfg=MonsterVisualAsync_ResolveBodyConfig(&asyncMgr->config,workTier,
            workMonster.hasCreaturePhenotype);
        activeMesherCfg.shouldCancel = WorkerShouldCancel;
        activeMesherCfg.cancelContext = asyncMgr;
        activeHeadCfg.shouldCancel = WorkerShouldCancel;
        activeHeadCfg.cancelContext = asyncMgr;
        workerMesher.config = activeMesherCfg;
        workerHeadMesher.config = activeHeadCfg;

        double stageStart=GetTimeMs();
        bool buildOk = MonsterSDF_Build(&workerSdf, &workMonster, asyncMgr->config.sdfConfig);
        sdfBuildMs=(float)(GetTimeMs()-stageStart);
        bool meshOk = false;
        bool chunkAttempted=false;
        size_t processedBricks=0,activeDirtyBricks=0;
        bool headOk = true;
        bool usedDynamicMorph=false;
        bool invalidDynamicMesh=false;
        MorphSurfaceTrackerStats dynamicStats={0};
        size_t localPatchAttempts=0,localPatchPublications=0;
        float lastLocalPatchMs=0;
        uint32_t lastLocalPatchModule=UINT32_MAX;

        if(buildOk && hasMorphEndpoints && topologyEventCount==0 &&
           workTier==MONSTER_VISUAL_QUALITY_MORPH &&
           workMonster.hasResolvedMorph && workMonster.hasGrowthAge) {
            bool warmingReference=!anchorReady;
            if(!anchorReady) {
                double anchorStart=GetTimeMs();
                MonsterAger_Interpolate(&morphStart,&morphEnd,.25f,&morphAnchor);
                if(morphAnchor.hasAnatomyGraph &&
                   MonsterSDF_Build(&anchorSdf,&morphAnchor,asyncMgr->config.sdfConfig)) {
                    SDFField anchorField=MonsterSDF_GetField(&anchorSdf);
                    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
                    size_t count=MonsterSDF_GetDetailRegions(&anchorSdf,1.6f,regions,
                        MONSTER_SDF_DETAIL_REGION_CAPACITY);
                    for(size_t i=0;i<count;++i)
                        if(regions[i].targetVoxelSize<.004f)regions[i].targetVoxelSize=.004f;
                    Mesh_Clear(&anchorMesh);
                    anchorReady=SDFMesher_GenerateMeshDetailed(&workerMesher,&anchorField,
                        regions,count,&anchorMesh);
                    if(anchorReady) {
                        stageStart=GetTimeMs();
                        anchorReady=AnatomyDeformer_Bind(anchorMorph,&anchorMesh,
                            &morphAnchor.anatomyGraph);
                        morphBindingMs=(float)(GetTimeMs()-stageStart);
                    }
                    if(anchorReady) {
                        stageStart=GetTimeMs();
                        SurfaceMapper_MapMeshWithStats(&anchorMesh,&morphAnchor.anatomyGraph,
                            &morphAnchor.surfaceMapping,&mappingStats);
                        mappingMs=(float)(GetTimeMs()-stageStart);
                        stageStart=GetTimeMs();
                        if(morphAnchor.hasHead && morphAnchor.head.anatomy.attachmentBodyPartIndex<morphAnchor.bodyPartCount)
                            anchorReady=HeadMorph_Bind(anchorHead,&anchorMesh,&morphAnchor.head.anatomy,
                                morphAnchor.bodyParts[morphAnchor.head.anatomy.attachmentBodyPartIndex].positionRender);
                        headBindingMs=(float)(GetTimeMs()-stageStart);
                    }
                    if(anchorReady) {
                        anchorReady=MorphSurfaceTracker_Bind(&dynamicTracker,&anchorMesh,.25f);
                        MorphSurfaceTracker_SetPool(&dynamicTracker,asyncMgr->samplingPool);
                    }
                    if(anchorReady) {
                        size_t largest=0,second=0;
                        anchorReady=Mesh_ComponentStatistics(&anchorMesh,
                            &anchorComponentCount,&largest,&second);
                    }
                }
                if(anchorReady)anchorGeneration=endpointGeneration;
                bodyMeshMs=(float)(GetTimeMs()-anchorStart);
            }
            if(anchorReady) {
                WorkerAdoptLatestMorphRequest(asyncMgr,endpointGeneration,
                    &workMonster,&workFingerprint);
                double correctionStart=GetTimeMs();
                Mesh_Clear(&workBodyMesh);
                meshOk=Mesh_ReserveVertices(&workBodyMesh,anchorMesh.vertexCount)&&
                    Mesh_ReserveIndices(&workBodyMesh,anchorMesh.indexCount);
                if(meshOk) {
                    memcpy(workBodyMesh.vertices,anchorMesh.vertices,
                        anchorMesh.vertexCount*sizeof(*anchorMesh.vertices));
                    memcpy(workBodyMesh.indices,anchorMesh.indices,
                        anchorMesh.indexCount*sizeof(*anchorMesh.indices));
                    workBodyMesh.vertexCount=anchorMesh.vertexCount;
                    workBodyMesh.indexCount=anchorMesh.indexCount;
                    workBodyMesh.hasSurface=morphAnchor.hasSurface;
                    Mesh_MarkSurfaceChanged(&workBodyMesh);
                }
                float cursor=dynamicTracker.correctedAge,target=workMonster.growthAge;
                bool corrected=false;
                while(meshOk && fabsf(target-cursor)>1e-5f) {
                    float delta=target-cursor;
                    float next=warmingReference && fabsf(delta)>.05001f?
                        cursor+copysignf(.05f,delta):target;
                    const Monster* stepMonster=&workMonster;
                    if(next!=target) {
                        MonsterAger_Interpolate(&morphStart,&morphEnd,next,&morphStage);
                        stepMonster=&morphStage;
                    }
                    meshOk=MonsterSDF_Build(&workerSdf,stepMonster,asyncMgr->config.sdfConfig)&&
                        MorphSurfaceTracker_Predict(&dynamicTracker,anchorMorph,anchorHead,
                            stepMonster,&workBodyMesh);
                    if(meshOk) {
                        SDFField targetField=MonsterSDF_GetField(&workerSdf);
                        meshOk=MorphSurfaceTracker_Correct(&dynamicTracker,&targetField,
                            &workBodyMesh,next);
                    }
                    if(meshOk) {
                        /* Cercanía al SDF no certifica conectividad. El mesh
                         * heredado se rechaza antes de entrar en ready. */
                        size_t allowed=workBodyMesh.indexCount/3000;
                        if(allowed<64)allowed=64;
                        meshOk=dynamicTracker.stats.valid &&
                            dynamicTracker.stats.watertight &&
                            dynamicTracker.stats.opposedTriangles<=allowed;
                        if(!meshOk)invalidDynamicMesh=true;
                    }
                    if(meshOk && asyncMgr->config.enableLocalMorphPatches &&
                       topologyEventCount && localPatchPublications==0) {
                        size_t problems=0;
                        uint32_t module=MorphSurfacePatcher_SelectModule(&workBodyMesh,
                            anchorMorph,&dynamicTracker,topologyEvents,
                            topologyEventCount,&problems);
                        if(module!=UINT32_MAX &&
                           WorkerMorphRequestIsNear(asyncMgr,endpointGeneration,next)) {
                            double patchStart=GetTimeMs();
                            Mesh candidate=Mesh_Create();
                            MeshPatchTransfer transfer={0};
                            MorphSurfacePatchStats patchStats={0};
                            localPatchAttempts++;
                            bool accepted=MorphSurfacePatcher_TryModule(&workBodyMesh,
                                anchorMorph,stepMonster,&workerSdf,&workerMesher,
                                module,&candidate,&transfer,&patchStats);
                            if(accepted)
                                accepted=CommitMorphPatch(&workBodyMesh,&anchorMesh,
                                    &anchorMorph,&anchorHead,&dynamicTracker,
                                    &candidate,&transfer,stepMonster,
                                    asyncMgr->samplingPool,next);
                            if(accepted) {
                                localPatchPublications++;
                                lastLocalPatchModule=module;
                                size_t largest=0,second=0;
                                Mesh_ComponentStatistics(&anchorMesh,
                                    &anchorComponentCount,&largest,&second);
                            }
                            lastLocalPatchMs=(float)(GetTimeMs()-patchStart);
                            MeshPatchTransfer_Free(&transfer);
                            Mesh_Free(&candidate);
                        }
                    }
                    cursor=next;
                    corrected=true;
                }
                if(meshOk && !corrected) {
                    meshOk=MorphSurfaceTracker_Predict(&dynamicTracker,anchorMorph,anchorHead,
                        &workMonster,&workBodyMesh);
                }
                if(meshOk) {
                    /* El último Correct puede haber usado un estado anterior
                     * a la petición adoptada: recompilar el campo publicado. */
                    meshOk=MonsterSDF_Build(&workerSdf,&workMonster,
                        asyncMgr->config.sdfConfig);
                }
                if(meshOk) {
                    workBodyMesh.surfaceRecipe=SurfaceRecipe_CompileScaled(&workMonster.surface,
                        workMonster.surfaceMapping.unitScale);
                    workBodyMesh.hasSurface=workMonster.hasSurface;
                    Mesh_Clear(&workHeadMesh);
                    meshOk=AnatomyDeformer_Copy(workerMorph,anchorMorph)&&
                        HeadMorph_Copy(workerHeadMorph,anchorHead);
                    dynamicStats=dynamicTracker.stats;
                    usedDynamicMorph=meshOk;
                }
                bodyMeshMs+=(float)(GetTimeMs()-correctionStart);
            }
        }

        if (buildOk && !meshOk && workTier == MONSTER_VISUAL_QUALITY_MORPH && workMonster.hasCreaturePhenotype && canonicalReady) {
            if (!canonicalMorphReady) {
                stageStart = GetTimeMs();
                Monster canonicalAdult = Monster_Create();
                CreaturePhenotype canonicalAdultPheno = canonicalPhenotype;
                canonicalAdultPheno.surface = workMonster.surface;
                CreaturePhenotype_Normalize(&canonicalAdultPheno);
                Creature_ResolveAppearance(&canonicalAdult, &canonicalRecipe, &canonicalAdultPheno);

                MonsterSDF canonicalSdf = MonsterSDF_Create();
                MonsterSDF_Build(&canonicalSdf, &canonicalAdult, asyncMgr->config.sdfConfig);
                SDFField field = MonsterSDF_GetField(&canonicalSdf);
                SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
                size_t rCount = MonsterSDF_GetDetailRegions(&canonicalSdf, 1.6f, regions, MONSTER_SDF_DETAIL_REGION_CAPACITY);
                for (size_t r = 0; r < rCount; ++r) {
                    if (regions[r].targetVoxelSize < 0.004f) regions[r].targetVoxelSize = 0.004f;
                }
                Mesh_Clear(&canonicalMesh);
                bool cMeshOk = SDFMesher_GenerateMeshDetailed(&workerMesher, &field, regions, rCount, &canonicalMesh);
                sdfBuildMs += (float)(GetTimeMs() - stageStart);
                if (cMeshOk) {
                    stageStart = GetTimeMs();
                    AnatomyDeformer_Bind(canonicalMorph, &canonicalMesh, &canonicalAdult.anatomyGraph);
                    morphBindingMs = (float)(GetTimeMs() - stageStart);
                    stageStart = GetTimeMs();
                    SurfaceMapper_MapMeshWithStats(&canonicalMesh, &canonicalAdult.anatomyGraph, &canonicalAdult.surfaceMapping, &mappingStats);
                    mappingMs = (float)(GetTimeMs() - stageStart);
                    if (canonicalAdult.hasHead && canonicalAdult.head.anatomy.attachmentBodyPartIndex < canonicalAdult.bodyPartCount) {
                        HeadMorph_Bind(canonicalHeadMorph, &canonicalMesh, &canonicalAdult.head.anatomy,
                                       canonicalAdult.bodyParts[canonicalAdult.head.anatomy.attachmentBodyPartIndex].positionRender);
                        HeadMorph_MapSurfaceDomain(canonicalHeadMorph, &canonicalMesh, &canonicalAdult.head.anatomy,
                                             canonicalAdult.bodyParts[canonicalAdult.head.anatomy.attachmentBodyPartIndex].positionRender,canonicalAdult.surfaceMapping.origin,canonicalAdult.surfaceMapping.unitScale);
                    }
                    canonicalMorphReady = true;
                }
                MonsterSDF_Free(&canonicalSdf);
                Monster_Free(&canonicalAdult);
            }

            if (canonicalMorphReady) {
                stageStart = GetTimeMs();
                Mesh_Clear(&workBodyMesh);
                Mesh_ReserveVertices(&workBodyMesh, canonicalMesh.vertexCount);
                memcpy(workBodyMesh.vertices, canonicalMesh.vertices, canonicalMesh.vertexCount * sizeof(MeshVertex));
                workBodyMesh.vertexCount = canonicalMesh.vertexCount;
                Mesh_ReserveIndices(&workBodyMesh, canonicalMesh.indexCount);
                memcpy(workBodyMesh.indices, canonicalMesh.indices, canonicalMesh.indexCount * sizeof(MeshIndex));
                workBodyMesh.indexCount = canonicalMesh.indexCount;
                workBodyMesh.surfaceRecipe = SurfaceRecipe_CompileScaled(&workMonster.surface,workMonster.surfaceMapping.unitScale);
                workBodyMesh.hasSurface = workMonster.hasSurface;

                bool defOk = AnatomyDeformer_Deform(canonicalMorph, &workMonster.anatomyGraph, &workBodyMesh);
                if (defOk && workMonster.hasHead && workMonster.head.anatomy.attachmentBodyPartIndex < workMonster.bodyPartCount) {
                    HeadMorph_Deform(canonicalHeadMorph, &workBodyMesh, &workMonster.head.anatomy,
                                     workMonster.bodyParts[workMonster.head.anatomy.attachmentBodyPartIndex].positionRender);
                }
                bodyMeshMs = (float)(GetTimeMs() - stageStart);
                Mesh_Clear(&workHeadMesh);

                AnatomyDeformer_Copy(workerMorph, canonicalMorph);
                HeadMorph_Copy(workerHeadMorph, canonicalHeadMorph);
                meshOk = defOk;
                headOk = true;
            }
        }
        if(buildOk&&meshOk&&workTier==MONSTER_VISUAL_QUALITY_MORPH&&
           !MorphMeshMatchesSDF(&workBodyMesh,&workerSdf,
               fmaxf(.08f,activeMesherCfg.voxelSize*2.5f))) {
            meshOk=false;
            usedDynamicMorph=false;
            asyncMgr->stats.invalidDynamicMeshCount++;
        }
        if (buildOk && !meshOk) {
            MonsterSDFBodyField bodyContext;
            SDFField field = MonsterSDF_GetBodyField(&workerSdf,&bodyContext);
            Mesh_Clear(&workBodyMesh);
            SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
            float sampleRate = (workTier == MONSTER_VISUAL_QUALITY_SETTLED) ? 6.0f :
                               ((workTier == MONSTER_VISUAL_QUALITY_MORPH) ? 1.6f : 3.5f);
            size_t regionCount=MonsterSDF_GetDetailRegions(&workerSdf,
                sampleRate,
                regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
            if (workTier == MONSTER_VISUAL_QUALITY_MORPH) {
                for (size_t r = 0; r < regionCount; ++r) {
                    if (regions[r].targetVoxelSize < 0.004f) {
                        regions[r].targetVoxelSize = 0.004f;
                    }
                }
            }
            if(workerSdf.connectorCount>0) field=MonsterSDF_GetField(&workerSdf);
            stageStart=GetTimeMs();
            bool useChunked = asyncMgr->config.enableChunkedRemeshing &&
                              (asyncMgr->morphMode || workTier == MONSTER_VISUAL_QUALITY_MORPH) &&
                              activeMesherCfg.voxelSize > 0.001f;
            if (useChunked) {
                chunkAttempted=true;
                if (!workerChunkMgr || fabsf(workerChunkMgr->voxelSize - activeMesherCfg.voxelSize) > 1e-4f) {
                    if (workerChunkMgr) MorphChunkManager_Destroy(workerChunkMgr);
                    workerChunkMgr = MorphChunkManager_Create(workerChunkSize, activeMesherCfg.voxelSize);
                    if (workerSpatialCache) MorphSpatialCache_Destroy(workerSpatialCache);
                    workerSpatialCache = MorphSpatialCache_Create(workerChunkSize * 0.5f);
                    hasPrevMonster = false;
                }

                if (!hasPrevMonster) {
                    AABB3D bounds = MonsterSDF_GetBounds(&workerSdf);
                    MorphChunkManager_MarkDirtyAABB(workerChunkMgr, bounds, MORPH_DIRT_TOPOLOGY);
                    if (workerSpatialCache) MorphSpatialCache_MarkDirtyAABB(workerSpatialCache, bounds, MORPH_DIRT_TOPOLOGY);
                } else {
                    bool anyMoved = false;
                    for (size_t ni = 0; ni < workMonster.anatomyGraph.nodeCount; ++ni) {
                        const AnatomyNode* curr = &workMonster.anatomyGraph.nodes[ni];
                        const AnatomyNode* prev = AnatomyGraph_FindNode(&prevWorkMonster.anatomyGraph, curr->id);
                        if (!prev || Vec3_Distance(prev->center, curr->center) > 1e-4f ||
                            fabsf(prev->widthRadius - curr->widthRadius) > 1e-4f ||
                            fabsf(prev->heightRadius - curr->heightRadius) > 1e-4f) {
                            if (workerSpatialCache) {
                                float rx = curr->envelopeRadii.x > 0.01f ? curr->envelopeRadii.x : (curr->widthRadius > 0.01f ? curr->widthRadius : 0.05f);
                                float ry = curr->envelopeRadii.y > 0.01f ? curr->envelopeRadii.y : (curr->heightRadius > 0.01f ? curr->heightRadius : 0.05f);
                                float rz = curr->envelopeRadii.z > 0.01f ? curr->envelopeRadii.z : 0.05f;
                                Vector3 rCurr = Vec3_Create(rx, ry, rz);
                                AABB3D boxCurr = AABB_FromMinMax(Vec3_Sub(curr->center, rCurr), Vec3_Add(curr->center, rCurr));
                                AABB3D boxPrev = boxCurr;
                                if (prev) {
                                    float prx = prev->envelopeRadii.x > 0.01f ? prev->envelopeRadii.x : (prev->widthRadius > 0.01f ? prev->widthRadius : 0.05f);
                                    float pry = prev->envelopeRadii.y > 0.01f ? prev->envelopeRadii.y : (prev->heightRadius > 0.01f ? prev->heightRadius : 0.05f);
                                    float prz = prev->envelopeRadii.z > 0.01f ? prev->envelopeRadii.z : 0.05f;
                                    Vector3 rPrev = Vec3_Create(prx, pry, prz);
                                    boxPrev = AABB_FromMinMax(Vec3_Sub(prev->center, rPrev), Vec3_Add(prev->center, rPrev));
                                }
                                MorphSpatialCache_MarkSweptModuleDirty(workerSpatialCache, boxPrev, boxCurr, 0.10f, false);
                            }
                            anyMoved = true;
                        }
                    }
                    if (anyMoved && workerSpatialCache) {
                        MorphChunkManager_SynchronizeWithSpatialCache(workerChunkMgr, workerSpatialCache);
                    }
                    if (MorphChunkManager_GetDirtyChunkCount(workerChunkMgr) == 0 &&
                        MorphChunkManager_GetSurfaceChunkCount(workerChunkMgr) == 0) {
                        AABB3D bounds = MonsterSDF_GetBounds(&workerSdf);
                        MorphChunkManager_MarkDirtyAABB(workerChunkMgr, bounds, MORPH_DIRT_TOPOLOGY);
                    }
                }

                size_t residualDirty=MorphChunkManager_MarkResidualDirty(
                    workerChunkMgr,&field,workerChunkMgr->voxelSize*2.5f);
                size_t dirtyBefore = MorphChunkManager_GetDirtyChunkCount(workerChunkMgr);
                size_t dirtyBricksBefore = workerSpatialCache ?
                    MorphSpatialCache_GetDirtyBrickCount(workerSpatialCache) : 0;
                MorphChunkManager_UpdateDirtyParallel(
                    workerChunkMgr, activeMesherCfg, asyncMgr->samplingPool, &field,
                    endpointGeneration + workFingerprint
                );
                bool assembleOk = MorphChunkManager_AssembleMesh(workerChunkMgr, &workBodyMesh, true);
                bool chunkCertified=assembleOk&&workBodyMesh.vertexCount>0&&
                    MorphMeshMatchesSDF(&workBodyMesh,&workerSdf,
                        fmaxf(.08f,activeMesherCfg.voxelSize*2.5f));
                if(chunkCertified) {
                    MeshValidationResult chunkValidation=Mesh_Validate(&workBodyMesh);
                    chunkCertified=chunkValidation.valid&&chunkValidation.watertight;
                }
                if (chunkCertified) {
                    meshOk = true;
                } else {
                    if(assembleOk&&workBodyMesh.vertexCount>0)
                        asyncMgr->stats.chunkCertificationFailureCount++;
                    workerMesher.config.adaptiveDetail=false;
                    meshOk = workerSdf.connectorCount>0 ?
                        SDFMesher_GenerateMeshDetailed(&workerMesher,&field,regions,regionCount,&workBodyMesh):
                        SDFMesher_GenerateMesh(&workerMesher, &field, &workBodyMesh);
                }
                asyncMgr->stats.dirtyRegionCount += dirtyBefore;
                asyncMgr->stats.residualChunkCount += residualDirty;
                asyncMgr->stats.dirtyBrickCount += dirtyBricksBefore;
                asyncMgr->stats.remeshedVertexCount += MorphChunkManager_GetTotalVertexCount(workerChunkMgr);
                asyncMgr->stats.remeshedTriangleCount += MorphChunkManager_GetTotalTriangleCount(workerChunkMgr);
                asyncMgr->stats.chunkCount = MorphChunkManager_GetTotalChunkCount(workerChunkMgr);
                asyncMgr->stats.dirtyChunkCount = MorphChunkManager_GetDirtyChunkCount(workerChunkMgr);
                asyncMgr->stats.surfaceChunkCount = MorphChunkManager_GetSurfaceChunkCount(workerChunkMgr);
            } else {
                meshOk = workerSdf.connectorCount>0 ?
                    SDFMesher_GenerateMeshDetailed(&workerMesher,&field,regions,regionCount,&workBodyMesh):
                    SDFMesher_GenerateMesh(&workerMesher, &field, &workBodyMesh);
            }
            bodyMeshMs=(float)(GetTimeMs()-stageStart);
            if(meshOk&&workTier==MONSTER_VISUAL_QUALITY_MORPH&&
               workMonster.hasResolvedMorph&&workerSdf.connectorCount>0&&
               !MorphMeshMatchesSDF(&workBodyMesh,&workerSdf,
                   fmaxf(.08f,activeMesherCfg.voxelSize*2.5f))) {
                meshOk=false;
                asyncMgr->stats.invalidDynamicMeshCount++;
            }
            Mesh_Clear(&workHeadMesh);
            if(meshOk&&workerSdf.hasPartitionedHead&&workerSdf.connectorCount==0) {
                MonsterSDFHeadField headContext;
                SDFField headField=MonsterSDF_GetHeadField(&workerSdf,0,&headContext);
                stageStart=GetTimeMs();
                headOk=SDFMesher_GenerateMesh(&workerHeadMesher,&headField,&workHeadMesh)&&
                       Mesh_KeepLargestComponent(&workHeadMesh);
                headMeshMs=(float)(GetTimeMs()-stageStart);
            }

            if(meshOk && headOk) {
                bool bound=false;
                if(workMonster.hasAnatomyGraph && workerMorph) {
                    stageStart=GetTimeMs();
                    bound=AnatomyDeformer_Bind(workerMorph,&workBodyMesh,&workMonster.anatomyGraph);
                    morphBindingMs=(float)(GetTimeMs()-stageStart);
                }
                stageStart=GetTimeMs();
                /* La especie aporta el dominio adulto de referencia; el mapeador y
                 * el deformador comparten exactamente las mismas influencias. */
                if(!bound || !canonicalReady || !workMonster.hasCreaturePhenotype ||
                   !SurfaceMapper_MapBoundMesh(&workBodyMesh,workerMorph,
                    &canonicalMonster.anatomyGraph,&canonicalMonster.surfaceMapping,&mappingStats))
                    SurfaceMapper_MapMeshWithStats(&workBodyMesh,&workMonster.anatomyGraph,&workMonster.surfaceMapping,&mappingStats);
                SurfaceMapper_MapMeshWithStats(&workHeadMesh,&workMonster.anatomyGraph,&workMonster.surfaceMapping,&headMappingStats);
                mappingMs=(float)(GetTimeMs()-stageStart);
                mappingStats.verticesProcessed+=headMappingStats.verticesProcessed;
                mappingStats.candidateTests+=headMappingStats.candidateTests;
                mappingStats.bruteForceTests+=headMappingStats.bruteForceTests;
                mappingStats.durationMs+=headMappingStats.durationMs;
                mappingStats.averageCandidatesPerVertex=mappingStats.verticesProcessed?
                    (float)mappingStats.candidateTests/(float)mappingStats.verticesProcessed:0;
                workBodyMesh.surfaceRecipe=workHeadMesh.surfaceRecipe=SurfaceRecipe_CompileScaled(&workMonster.surface,workMonster.surfaceMapping.unitScale);
                workBodyMesh.hasSurface=workHeadMesh.hasSurface=workMonster.hasSurface;
                stageStart=GetTimeMs();
                if(workMonster.hasHead && workMonster.head.anatomy.attachmentBodyPartIndex<workMonster.bodyPartCount)
                    HeadMorph_Bind(workerHeadMorph,&workBodyMesh,&workMonster.head.anatomy,
                        workMonster.bodyParts[workMonster.head.anatomy.attachmentBodyPartIndex].positionRender);
                if(canonicalReady && workMonster.hasCreaturePhenotype)
                    HeadMorph_MapSurfaceDomain(workerHeadMorph,&workBodyMesh,&canonicalMonster.head.anatomy,
                        canonicalMonster.bodyParts[canonicalMonster.head.anatomy.attachmentBodyPartIndex].positionRender,canonicalMonster.surfaceMapping.origin,canonicalMonster.surfaceMapping.unitScale);
                headBindingMs=(float)(GetTimeMs()-stageStart);
            }
        }
        size_t bodyOpposed=0,bodyDegenerate=0,bodyBoundary=0,bodyComponents=0;
        float bodyValidationMs=0;
        if(meshOk && headOk && hasMorphEndpoints &&
           workTier==MONSTER_VISUAL_QUALITY_MORPH) {
            double validationStart=GetTimeMs();
            MeshValidationResult validation=Mesh_Validate(&workBodyMesh);
            asyncMgr->stats.fullMeshValidations++;
            size_t largest=0,second=0;
            bool componentsOk=Mesh_ComponentStatistics(&workBodyMesh,
                &bodyComponents,&largest,&second);
            bodyOpposed=CountOpposedTriangles(&workBodyMesh);
            asyncMgr->stats.fullMeshValidations++;
            bodyDegenerate=validation.degenerateTriangleCount+
                validation.zeroAreaTriangleCount;
            bodyBoundary=validation.boundaryEdgeCount;
            size_t allowed=workBodyMesh.indexCount/3000;
            if(allowed<64)allowed=64;
            meshOk=validation.valid&&validation.watertight&&
                componentsOk&&
                (!usedDynamicMorph||!anchorComponentCount||
                 bodyComponents<=anchorComponentCount)&&
                bodyOpposed<=allowed;
            bodyValidationMs=(float)(GetTimeMs()-validationStart);
        }
        /* Generar mallas primitivas de ojos */
        MonsterVisualEyeAsync* workEyes = NULL;
        size_t workEyeCount = workMonster.eyeCount;
        bool eyeOk = true;

        if (meshOk && headOk && workEyeCount > 0) {
            stageStart=GetTimeMs();
            workEyes = (MonsterVisualEyeAsync*)calloc(workEyeCount, sizeof(MonsterVisualEyeAsync));
            if (workEyes) {
                for (size_t i = 0; i < workEyeCount; ++i) {
                    workEyes[i].globe = Mesh_Create();
                }
                if (!MonsterVisual_UpdateEyes(workEyes, workEyeCount, &workMonster)) {
                    eyeOk = false;
                }
            } else {
                eyeOk = false;
            }
            eyeMs=(float)(GetTimeMs()-stageStart);
        }

        MonsterVisualMouth* workMouths = NULL;
        size_t workMouthCount = workMonster.mouthCount;
        bool mouthOk = true;

        if (meshOk && headOk && workMouthCount > 0) {
            stageStart=GetTimeMs();
            workMouths = (MonsterVisualMouth*)calloc(workMouthCount, sizeof(MonsterVisualMouth));
            if (!workMouths) {
                mouthOk = false;
            } else {
                for (size_t i = 0; i < workMouthCount; ++i) {
                    if (!MonsterVisual_BuildMouthMeshesFromSDFWithMeshers(&workMouths[i], &workMonster.mouths[i], &workMonster, &workerSdf, i, &workerJawMesher, &workerSeamMesher)) {
                        mouthOk = false;
                        break;
                    }
                }
            }
            mouthMs=(float)(GetTimeMs()-stageStart);
        }

        float workScale=workMonster.hasCreaturePhenotype?workMonster.phenotype.axial.totalScale:0;
        if(chunkAttempted&&buildOk&&meshOk&&headOk&&eyeOk&&mouthOk) {
            if(workerSpatialCache) {
                processedBricks=MorphSpatialCache_UpdateDirty(workerSpatialCache,&workerSdf,
                    workMonster.hasGrowthAge?workMonster.growthAge:NAN);
                activeDirtyBricks=MorphSpatialCache_GetDirtyBrickCount(workerSpatialCache);
            }
            Monster_CopyInto(&prevWorkMonster,&workMonster);
            hasPrevMonster=true;
        }
        double tEnd = GetTimeMs();
        float durationMs = (float)(tEnd - tStart);

        /* --- DEPOSITAR RESULTADO EN READY BUFFER DENTRO DEL MUTEX --- */
        double publicationStart=GetTimeMs();
        pthread_mutex_lock(&asyncMgr->lock);
        asyncMgr->stats.processedBrickCount+=processedBricks;
        asyncMgr->stats.activeDirtyBrickCount=activeDirtyBricks;

        bool obsoleteTopology=hasMorphEndpoints &&
            endpointGeneration!=asyncMgr->morphEndpointGeneration;
        if(invalidDynamicMesh)asyncMgr->stats.invalidDynamicMeshCount++;
        bool stale=asyncMgr->displayGeneration!=0 && asyncMgr->hasPendingRequest &&
            !asyncMgr->continuousMotion &&
            asyncMgr->config.maxGeometryAgeLag>0 &&
            fabsf(asyncMgr->stats.targetAge-asyncMgr->stats.workingGeometryAge)>asyncMgr->config.maxGeometryAgeLag;
        if (buildOk && meshOk && headOk && eyeOk && mouthOk && !stale && !obsoleteTopology && !asyncMgr->shouldQuit &&
            !(asyncMgr->displayFingerprint==asyncMgr->stats.requestedFingerprint && workFingerprint!=asyncMgr->stats.requestedFingerprint)) {
            /* Liberar readyEyes anterior */
            FreeEyeArray(asyncMgr->readyEyes, asyncMgr->readyEyeCount);

            /* Intercambiar mallas */
            Mesh tmp = asyncMgr->readyMesh;
            asyncMgr->readyMesh = workBodyMesh;
            workBodyMesh = tmp;
            Mesh tmpHead=asyncMgr->readyHeadMesh;
            asyncMgr->readyHeadMesh=workHeadMesh;
            workHeadMesh=tmpHead;

            AnatomyDeformer* tmpMorph = asyncMgr->readyMorph;
            asyncMgr->readyMorph = workerMorph;
            workerMorph = tmpMorph;
            HeadMorph* tmpHeadMorph=asyncMgr->readyHeadMorph;
            asyncMgr->readyHeadMorph=workerHeadMorph;workerHeadMorph=tmpHeadMorph;
            if (workerChunkMgr && asyncMgr->readyChunkManager) {
                MorphChunkManager_Copy(asyncMgr->readyChunkManager, workerChunkMgr, false);
            }

            asyncMgr->readyEyes = workEyes;
            asyncMgr->readyEyeCount = workEyeCount;
            asyncMgr->readyEyeCapacity = workEyeCount;
            asyncMgr->readyGeneration++;
            asyncMgr->readyEndpointGeneration=hasMorphEndpoints?endpointGeneration:0;
            asyncMgr->readyFingerprint = workFingerprint;
            asyncMgr->readyScale=workScale;
            asyncMgr->readyAppendageDevelopment=workMonster.hasCreaturePhenotype?
                workMonster.phenotype.development.appendages:1;
            asyncMgr->readyTier = workTier;
            asyncMgr->readyDynamicMorph=usedDynamicMorph;
            if(usedDynamicMorph) {
                if(asyncMgr->readyTracker.vertexCount!=anchorMesh.vertexCount||
                   asyncMgr->readyTracker.triangleCount!=anchorMesh.indexCount/3||
                   fabsf(asyncMgr->readyTracker.referenceAge-
                       dynamicTracker.referenceAge)>1e-5f)
                    MorphSurfaceTracker_Bind(&asyncMgr->readyTracker,&anchorMesh,
                        dynamicTracker.referenceAge);
                if(!MorphSurfaceTracker_CopyCorrections(&asyncMgr->readyTracker,&dynamicTracker))
                    asyncMgr->readyDynamicMorph=false;
            }
            FreeMouthArray(asyncMgr->readyMouths, asyncMgr->readyMouthCount);
            asyncMgr->readyMouths = workMouths;
            asyncMgr->readyMouthCount = workMouthCount;
            asyncMgr->readyMouthCapacity = workMouthCount;
            asyncMgr->hasReadyMesh = true;

            asyncMgr->stats.completedBuildCount++;
            asyncMgr->stats.lastBuildDurationMs = durationMs;
            asyncMgr->stats.sdfBuildMs=sdfBuildMs;asyncMgr->stats.bodyMeshMs=bodyMeshMs;
            asyncMgr->stats.headMeshMs=headMeshMs;asyncMgr->stats.surfaceMappingMs=mappingMs;
            asyncMgr->stats.eyeBuildMs=eyeMs;asyncMgr->stats.mouthBuildMs=mouthMs;
            asyncMgr->stats.morphBindingMs=morphBindingMs;asyncMgr->stats.headBindingMs=headBindingMs;
            asyncMgr->stats.surfaceMapper=mappingStats;
            asyncMgr->stats.dynamicSurface=usedDynamicMorph?dynamicStats:(MorphSurfaceTrackerStats){0};
            asyncMgr->stats.bodyOpposedTriangles=bodyOpposed;
            asyncMgr->stats.bodyDegenerateTriangles=bodyDegenerate;
            asyncMgr->stats.bodyBoundaryEdges=bodyBoundary;
            asyncMgr->stats.bodyDisconnectedComponents=bodyComponents;
            asyncMgr->stats.bodyValidationMs=bodyValidationMs;
            asyncMgr->stats.localPatchAttempts+=localPatchAttempts;
            asyncMgr->stats.localPatchPublications+=localPatchPublications;
            asyncMgr->stats.lastLocalPatchMs=lastLocalPatchMs;
            asyncMgr->stats.lastLocalPatchModule=lastLocalPatchModule;
            if(hasMorphEndpoints && workTier==MONSTER_VISUAL_QUALITY_MORPH && !usedDynamicMorph)
                asyncMgr->stats.dynamicFullFallbackCount++;
            asyncMgr->stats.activeQualityTier = workTier;
            asyncMgr->stats.bodyMesher=*SDFMesher_GetLastStats(&workerMesher);
            if(workerSdf.hasPartitionedHead&&workerSdf.connectorCount==0)asyncMgr->stats.headMesher=*SDFMesher_GetLastStats(&workerHeadMesher);
            else memset(&asyncMgr->stats.headMesher,0,sizeof(asyncMgr->stats.headMesher));
            asyncMgr->stats.readyPublicationMs=(float)(GetTimeMs()-publicationStart);
            asyncMgr->readyStats=asyncMgr->stats;
        } else {
            if (workEyes) FreeEyeArray(workEyes, workEyeCount);
            if (workMouths) FreeMouthArray(workMouths, workMouthCount);
            if (!meshOk) asyncMgr->stats.cancelledBuildCount++;
            else if(stale)asyncMgr->stats.staleBuildDiscardedCount++;
            else if(obsoleteTopology)asyncMgr->stats.obsoleteTopologyResultCount++;
        }

        asyncMgr->stats.isWorkerBusy = false;
        pthread_mutex_unlock(&asyncMgr->lock);
    }

    AnatomyDeformer_Free(workerMorph);
    HeadMorph_Free(workerHeadMorph);
    Monster_Free(&canonicalMonster);
    Mesh_Free(&canonicalMesh);
    MorphSurfaceTracker_Free(&dynamicTracker);
    HeadMorph_Free(anchorHead);
    AnatomyDeformer_Free(anchorMorph);
    Mesh_Free(&anchorMesh);
    MonsterSDF_Free(&anchorSdf);
    Monster_Free(&morphStage);Monster_Free(&morphAnchor);
    Monster_Free(&morphStart);Monster_Free(&morphEnd);
    AnatomyDeformer_Free(canonicalMorph);
    HeadMorph_Free(canonicalHeadMorph);
    Mesh_Free(&workBodyMesh);
    Mesh_Free(&workHeadMesh);
    SDFMesher_Free(&workerMesher);
    SDFMesher_Free(&workerHeadMesher);
    SDFMesher_Free(&workerJawMesher);
    SDFMesher_Free(&workerSeamMesher);
    MonsterSDF_Free(&workerSdf);
    Monster_Free(&workMonster);
    if (workerChunkMgr) MorphChunkManager_Destroy(workerChunkMgr);
    if (workerSpatialCache) MorphSpatialCache_Destroy(workerSpatialCache);
    Monster_Free(&prevWorkMonster);

    return NULL;
}

MonsterVisualAsync* MonsterVisualAsync_Create(MonsterVisualAsyncConfig config) {
    MonsterVisualAsync* asyncMgr = (MonsterVisualAsync*)calloc(1, sizeof(MonsterVisualAsync));
    if (!asyncMgr) return NULL;

    asyncMgr->config = config;
    asyncMgr->displayMesh = Mesh_Create();
    asyncMgr->displayCertifiedMesh = Mesh_Create();
    asyncMgr->readyMesh = Mesh_Create();
    asyncMgr->displayHeadMesh = Mesh_Create();
    asyncMgr->readyHeadMesh = Mesh_Create();
    asyncMgr->pendingSnapshot = Monster_Create();
    asyncMgr->morphStartSnapshot=Monster_Create();
    asyncMgr->morphEndSnapshot=Monster_Create();
    asyncMgr->displayHeadMorph=HeadMorph_Create();asyncMgr->readyHeadMorph=HeadMorph_Create();
    asyncMgr->displayMorph = AnatomyDeformer_Create();
    asyncMgr->readyMorph = AnatomyDeformer_Create();
    float defaultChunkSize = config.chunkSize > 0.05f ? config.chunkSize : 0.45f;
    asyncMgr->displayChunkManager = MorphChunkManager_Create(defaultChunkSize, 0.08f);
    asyncMgr->readyChunkManager = MorphChunkManager_Create(defaultChunkSize, 0.08f);
    asyncMgr->samplingPool = SDFSamplingPool_Create(0);

    pthread_mutex_init(&asyncMgr->lock, NULL);
    pthread_cond_init(&asyncMgr->cond, NULL);

    asyncMgr->threadRunning = true;
    if (pthread_create(&asyncMgr->workerThread, NULL, WorkerThreadRoutine, asyncMgr) != 0) {
        fprintf(stderr, "MonsterVisualAsync: error al crear hilo worker\n");
        asyncMgr->threadRunning = false;
    }

    return asyncMgr;
}

void MonsterVisualAsync_Free(MonsterVisualAsync* asyncMgr) {
    if (!asyncMgr) return;

    if (asyncMgr->threadRunning) {
        pthread_mutex_lock(&asyncMgr->lock);
        asyncMgr->shouldQuit = true;
        pthread_cond_signal(&asyncMgr->cond);
        pthread_mutex_unlock(&asyncMgr->lock);

        pthread_join(asyncMgr->workerThread, NULL);
        asyncMgr->threadRunning = false;
    }

    pthread_mutex_lock(&asyncMgr->lock);

    Monster_Free(&asyncMgr->pendingSnapshot);
    Monster_Free(&asyncMgr->morphStartSnapshot);
    Monster_Free(&asyncMgr->morphEndSnapshot);
    MorphSurfaceTracker_Free(&asyncMgr->displayTracker);
    MorphSurfaceTracker_Free(&asyncMgr->readyTracker);

    Mesh_Free(&asyncMgr->displayMesh);
    Mesh_Free(&asyncMgr->displayCertifiedMesh);
    free(asyncMgr->displayAnchoredFrozen);
    free(asyncMgr->displayTopologyMask);
    free(asyncMgr->displayInterfaceTriangles);
    free(asyncMgr->morphModules);
    Mesh_Free(&asyncMgr->displayHeadMesh);
    FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);
    FreeMouthArray(asyncMgr->displayMouths, asyncMgr->displayMouthCount);

    Mesh_Free(&asyncMgr->readyMesh);
    Mesh_Free(&asyncMgr->readyHeadMesh);
    FreeEyeArray(asyncMgr->readyEyes, asyncMgr->readyEyeCount);
    FreeMouthArray(asyncMgr->readyMouths, asyncMgr->readyMouthCount);

    HeadMorph_Free(asyncMgr->displayHeadMorph);HeadMorph_Free(asyncMgr->readyHeadMorph);
    AnatomyDeformer_Free(asyncMgr->displayMorph);
    AnatomyDeformer_Free(asyncMgr->readyMorph);

    if (asyncMgr->displayChunkManager) {
        MorphChunkManager_Destroy(asyncMgr->displayChunkManager);
        asyncMgr->displayChunkManager = NULL;
    }
    if (asyncMgr->readyChunkManager) {
        MorphChunkManager_Destroy(asyncMgr->readyChunkManager);
        asyncMgr->readyChunkManager = NULL;
    }

    pthread_mutex_unlock(&asyncMgr->lock);

    if (asyncMgr->samplingPool) {
        SDFSamplingPool_Free(asyncMgr->samplingPool);
        asyncMgr->samplingPool = NULL;
    }

    pthread_mutex_destroy(&asyncMgr->lock);
    pthread_cond_destroy(&asyncMgr->cond);

    free(asyncMgr);
}

void MonsterVisualAsync_SetContinuousMotion(MonsterVisualAsync* asyncMgr,bool active) {
    if(!asyncMgr)return;
    pthread_mutex_lock(&asyncMgr->lock);
    if(asyncMgr->continuousMotion!=active)asyncMgr->timeSinceLastMotionSec=0;
    asyncMgr->continuousMotion=active;
    pthread_mutex_unlock(&asyncMgr->lock);
}

void MonsterVisualAsync_SetMorphMode(MonsterVisualAsync* asyncMgr, bool active) {
    if(!asyncMgr)return;
    asyncMgr->morphMode = active;
}

void MonsterVisualAsync_SetWorkerDelay(MonsterVisualAsync* asyncMgr,unsigned milliseconds) {
    if(!asyncMgr)return;
    pthread_mutex_lock(&asyncMgr->lock);
    asyncMgr->config.workerDelayMs=milliseconds;
    pthread_mutex_unlock(&asyncMgr->lock);
}

bool MonsterVisualAsync_SetMorphEndpoints(MonsterVisualAsync* asyncMgr,
    const Monster* start,const Monster* end) {
    if(!asyncMgr||!start||!end||!start->hasAnatomyGraph||!end->hasAnatomyGraph)return false;
    CreatureMorphTopologyEvent events[ANATOMY_MAX_NODES];
    size_t eventCount=CreatureMorph_CollectTopologyEvents(start,end,
        events,ANATOMY_MAX_NODES);
    if(eventCount==SIZE_MAX||eventCount>ANATOMY_MAX_NODES)return false;
    MorphModuleState* modules=eventCount?
        calloc(eventCount,sizeof(*modules)):NULL;
    if(eventCount&&!modules)return false;
    pthread_mutex_lock(&asyncMgr->lock);
    Monster_CopyInto(&asyncMgr->morphStartSnapshot,start);
    Monster_CopyInto(&asyncMgr->morphEndSnapshot,end);
    asyncMgr->hasMorphEndpoints=true;
    asyncMgr->morphEndpointGeneration++;
    for(size_t i=0;i<eventCount;++i)
        MorphModuleState_Init(&modules[i],events[i].moduleInstanceId,
            asyncMgr->morphEndpointGeneration,true);
    free(asyncMgr->morphModules);
    asyncMgr->morphModules=modules;
    asyncMgr->morphModuleCount=eventCount;
    asyncMgr->stats.topologyModuleCount=eventCount;
    /* Un ready de otros extremos nunca puede adquirir estas versiones. */
    if(asyncMgr->hasReadyMesh) {
        asyncMgr->hasReadyMesh=false;
        asyncMgr->stats.obsoleteTopologyResultCount++;
    }
    pthread_mutex_unlock(&asyncMgr->lock);
    return true;
}

bool MonsterVisualAsync_UpdateWithAppearance(MonsterVisualAsync* asyncMgr,
    const Monster* monster,const Monster* appearance,float deltaTime) {
    if (!asyncMgr || !monster || !appearance) return false;
    double presentationStart=GetTimeMs();

    /* 1. Detectar movimiento y actualizar temporizador de tier de calidad */
    uint64_t baseGeomHash = ComputeMonsterFingerprint(monster, asyncMgr->config.sdfConfig, MONSTER_VISUAL_QUALITY_INTERACTIVE);
    if (baseGeomHash != asyncMgr->lastObservedFingerprint) {
        asyncMgr->lastObservedFingerprint = baseGeomHash;
        asyncMgr->timeSinceLastMotionSec = 0.0f;
    } else {
        asyncMgr->timeSinceLastMotionSec += deltaTime;
    }

    MonsterVisualQualityTier activeMotionTier = asyncMgr->morphMode ?
        MONSTER_VISUAL_QUALITY_MORPH : MONSTER_VISUAL_QUALITY_INTERACTIVE;
    MonsterVisualQualityTier targetTier = (asyncMgr->continuousMotion || asyncMgr->timeSinceLastMotionSec < asyncMgr->config.settledDelaySec) ?
        activeMotionTier : MONSTER_VISUAL_QUALITY_SETTLED;

    uint64_t targetFingerprint = ComputeMonsterFingerprint(monster, asyncMgr->config.sdfConfig, targetTier);
    uint64_t presentationFingerprint=ComputeMonsterFingerprint(appearance,asyncMgr->config.sdfConfig,
        MONSTER_VISUAL_QUALITY_INTERACTIVE);

    bool updatedDisplay = false;

    pthread_mutex_lock(&asyncMgr->lock);

    asyncMgr->stats.targetAge=appearance->hasGrowthAge?appearance->growthAge:NAN;
    for(size_t i=0;i<asyncMgr->morphModuleCount;++i)
        MorphModuleState_Request(&asyncMgr->morphModules[i],
            asyncMgr->stats.targetAge);

    bool readyAccepted=asyncMgr->hasReadyMesh &&
        (!asyncMgr->hasMorphEndpoints ||
         asyncMgr->readyEndpointGeneration==asyncMgr->morphEndpointGeneration);
    if(readyAccepted)for(size_t i=0;i<asyncMgr->morphModuleCount;++i) {
        MorphModuleState candidate=asyncMgr->morphModules[i];
        MorphPatchToken token=MorphModuleState_Token(&candidate,
            asyncMgr->readyStats.workingGeometryAge);
        if(!MorphModuleState_Accept(&candidate,token)) {
            readyAccepted=false;
            break;
        }
    }
    if(asyncMgr->hasReadyMesh&&!readyAccepted) {
        asyncMgr->hasReadyMesh=false;
        asyncMgr->stats.obsoleteTopologyResultCount++;
    }

    /* 2. Si hay malla lista generada por el worker, swap hacia el front buffer (Display) */
    if (readyAccepted) {
        double publicationStart=GetTimeMs();
        FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);

        Mesh tmp = asyncMgr->displayMesh;
        asyncMgr->displayMesh = asyncMgr->readyMesh;
        asyncMgr->readyMesh = tmp;
        Mesh tmpHead=asyncMgr->displayHeadMesh;
        asyncMgr->displayHeadMesh=asyncMgr->readyHeadMesh;
        asyncMgr->readyHeadMesh=tmpHead;

        AnatomyDeformer* tmpMorph = asyncMgr->displayMorph;
        asyncMgr->displayMorph = asyncMgr->readyMorph;
        asyncMgr->readyMorph = tmpMorph;
        HeadMorph* tmpHeadMorph=asyncMgr->displayHeadMorph;
        asyncMgr->displayHeadMorph=asyncMgr->readyHeadMorph;asyncMgr->readyHeadMorph=tmpHeadMorph;
        MorphSurfaceTracker tmpTracker=asyncMgr->displayTracker;
        asyncMgr->displayTracker=asyncMgr->readyTracker;asyncMgr->readyTracker=tmpTracker;
        asyncMgr->displayDynamicMorph=asyncMgr->readyDynamicMorph;
        MorphChunkManager* tmpChunkMgr = asyncMgr->displayChunkManager;
        asyncMgr->displayChunkManager = asyncMgr->readyChunkManager;
        asyncMgr->readyChunkManager = tmpChunkMgr;

        asyncMgr->displayEyes = asyncMgr->readyEyes;
        asyncMgr->displayEyeCount = asyncMgr->readyEyeCount;
        asyncMgr->displayEyeCapacity = asyncMgr->readyEyeCapacity;
        asyncMgr->readyEyes = NULL;
        asyncMgr->readyEyeCount = 0;
        asyncMgr->readyEyeCapacity = 0;

        FreeMouthArray(asyncMgr->displayMouths, asyncMgr->displayMouthCount);
        asyncMgr->displayMouths = asyncMgr->readyMouths;
        asyncMgr->displayMouthCount = asyncMgr->readyMouthCount;
        asyncMgr->displayMouthCapacity = asyncMgr->readyMouthCapacity;
        asyncMgr->readyMouths = NULL;
        asyncMgr->readyMouthCount = 0;
        asyncMgr->readyMouthCapacity = 0;

        asyncMgr->displayGeneration = asyncMgr->readyGeneration;
        asyncMgr->displayEndpointGeneration=asyncMgr->readyEndpointGeneration;
        asyncMgr->displayPredicted=false;
        bool certifiedCopy=TrackedCloneMorphMesh(asyncMgr,&asyncMgr->displayCertifiedMesh,
            &asyncMgr->displayMesh);
        asyncMgr->displayFingerprint = asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedFingerprint=asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedGeometryAge=asyncMgr->readyStats.workingGeometryAge;
        asyncMgr->stats.certifiedAge=certifiedCopy?
            asyncMgr->stats.displayedGeometryAge:NAN;
        if(certifiedCopy)for(size_t i=0;i<asyncMgr->morphModuleCount;++i) {
            MorphPatchToken token=MorphModuleState_Token(&asyncMgr->morphModules[i],
                asyncMgr->stats.displayedGeometryAge);
            MorphModuleState_Accept(&asyncMgr->morphModules[i],token);
        }
        if(!BuildDisplayTopologyMask(asyncMgr))
            asyncMgr->stats.topologyFrozenVertices=asyncMgr->displayMesh.vertexCount;
        asyncMgr->stats.presentedMorphAge=asyncMgr->stats.displayedGeometryAge;
        asyncMgr->stats.displayedScale=asyncMgr->readyScale;
        asyncMgr->stats.presentedScale=asyncMgr->readyScale;
        asyncMgr->displayAppendageDevelopment=asyncMgr->readyAppendageDevelopment;
        asyncMgr->displayStats=asyncMgr->readyStats;
        asyncMgr->hasReadyMesh = false;
        asyncMgr->stats.displayPublicationMs=(float)(GetTimeMs()-publicationStart);
        updatedDisplay = true;
    }

    /* 3. Comprobar si se requiere solicitar una nueva reconstrucción */
    bool isPendingMatch = asyncMgr->hasPendingRequest && (asyncMgr->pendingFingerprint == targetFingerprint);
    bool isDisplayMatch = (asyncMgr->displayFingerprint == targetFingerprint);
    if(asyncMgr->displayGeneration>0) {
        SurfaceRecipe recipe=SurfaceRecipe_CompileScaled(&appearance->surface,appearance->surfaceMapping.unitScale);
        asyncMgr->displayMesh.surfaceRecipe=asyncMgr->displayHeadMesh.surfaceRecipe=recipe;
        asyncMgr->displayMesh.hasSurface=asyncMgr->displayHeadMesh.hasSurface=appearance->hasSurface;
        for(size_t i=0;i<asyncMgr->displayMouthCount;++i) {
            asyncMgr->displayMouths[i].jaw.surfaceRecipe=asyncMgr->displayMouths[i].hinge.surfaceRecipe=recipe;
            asyncMgr->displayMouths[i].jaw.hasSurface=asyncMgr->displayMouths[i].hinge.hasSurface=appearance->hasSurface;
        }
    }

    bool isWorkingMatch=asyncMgr->stats.isWorkerBusy && asyncMgr->stats.workingFingerprint==targetFingerprint;
    asyncMgr->stats.requestedFingerprint=targetFingerprint;
    if((isWorkingMatch || isDisplayMatch) && asyncMgr->hasPendingRequest && !isPendingMatch) {
        asyncMgr->hasPendingRequest=false;
        asyncMgr->stats.coalescedCount++;
    }
    if (!isDisplayMatch && !isPendingMatch && !isWorkingMatch) {
        if (asyncMgr->hasPendingRequest) {
            asyncMgr->stats.coalescedCount++;
        }

        asyncMgr->stats.pendingGeometryAge=monster->hasGrowthAge?monster->growthAge:NAN;
        Monster_CopyInto(&asyncMgr->pendingSnapshot, monster);
        asyncMgr->pendingFingerprint = targetFingerprint;
        asyncMgr->pendingTier = targetTier;
        asyncMgr->hasPendingRequest = true;
        asyncMgr->stats.requestCount++;

        pthread_cond_signal(&asyncMgr->cond);
    }

    bool presentationChanged=updatedDisplay || presentationFingerprint!=asyncMgr->lastPresentedFingerprint;
    asyncMgr->lastPresentedFingerprint=presentationFingerprint;
    bool allowLiveArticulation = isDisplayMatch && presentationChanged;

    pthread_mutex_unlock(&asyncMgr->lock);

    /* La anatomía y la jaula cefálica deforman el snapshot publicado;
     * las nuevas ramas anatómicas aparecen mediante keyframes del worker. */

    bool separatedChannels=appearance!=monster;
    bool presentedMorph=false;
    bool topologyPredictionSafe=true;
    for(size_t i=0;i<asyncMgr->morphModuleCount;++i)
        if(!MorphModuleState_CanPredict(&asyncMgr->morphModules[i],
            appearance->hasGrowthAge?appearance->growthAge:NAN)) {
            topologyPredictionSafe=false;
            break;
        }
    bool handoffOwnsTopology=asyncMgr->morphModuleCount>0;
    for(size_t i=0;i<asyncMgr->morphModuleCount;++i)
        handoffOwnsTopology &= asyncMgr->morphModules[i].handoffInitialized;
    bool maskedPrediction=!handoffOwnsTopology&&!topologyPredictionSafe&&
        asyncMgr->displayEndpointGeneration==asyncMgr->morphEndpointGeneration&&
        asyncMgr->displayTopologyMask&&asyncMgr->stats.topologyFrozenVertices>0&&
        asyncMgr->displayCertifiedMesh.vertexCount==asyncMgr->displayMesh.vertexCount;
    if(maskedPrediction&&asyncMgr->displayAnchoredCapacity<asyncMgr->displayMesh.vertexCount) {
        MorphAnchoredVertex* grown=realloc(asyncMgr->displayAnchoredFrozen,
            asyncMgr->displayMesh.vertexCount*sizeof(*grown));
        if(grown) {
            asyncMgr->displayAnchoredFrozen=grown;
            asyncMgr->displayAnchoredCapacity=asyncMgr->displayMesh.vertexCount;
        } else maskedPrediction=false;
    }
    if(maskedPrediction) {
        /* Anclaje cinemático de vértices congelados al marco local de su articulación */
        if(!AnatomyDeformer_AnchorFrozenVertices(asyncMgr->displayMorph,
            &appearance->anatomyGraph,&asyncMgr->displayCertifiedMesh,
            &asyncMgr->displayMesh,asyncMgr->displayTopologyMask)) {
            for(size_t v=0;v<asyncMgr->displayMesh.vertexCount;++v)
                if(asyncMgr->displayTopologyMask[v])
                    asyncMgr->displayMesh.vertices[v]=
                        asyncMgr->displayCertifiedMesh.vertices[v];
        }
        for(size_t v=0;v<asyncMgr->displayMesh.vertexCount;++v)
            if(asyncMgr->displayTopologyMask[v]) {
                asyncMgr->displayAnchoredFrozen[v].position=asyncMgr->displayMesh.vertices[v].position;
                asyncMgr->displayAnchoredFrozen[v].normal=asyncMgr->displayMesh.vertices[v].normal;
            }
    } else if(!handoffOwnsTopology&&!topologyPredictionSafe && asyncMgr->displayPredicted &&
       asyncMgr->displayCertifiedMesh.vertexCount==asyncMgr->displayMesh.vertexCount) {
        for(size_t v=0;v<asyncMgr->displayMesh.vertexCount;++v) {
            asyncMgr->displayMesh.vertices[v].position=asyncMgr->displayCertifiedMesh.vertices[v].position;
            asyncMgr->displayMesh.vertices[v].normal=asyncMgr->displayCertifiedMesh.vertices[v].normal;
        }
        Mesh_MarkGeometryChanged(&asyncMgr->displayMesh);
        asyncMgr->displayPredicted=false;
    }
    if(!topologyPredictionSafe && separatedChannels && presentationChanged) {
        asyncMgr->stats.topologyFreezeCount++;
        asyncMgr->stats.topologyFallbackCount++;
        asyncMgr->stats.topologyFallbackDuration+=deltaTime;
    }
    if(separatedChannels && presentationChanged && asyncMgr->morphMode && asyncMgr->displayGeneration>0 && appearance->hasAnatomyGraph &&
       (topologyPredictionSafe||maskedPrediction||handoffOwnsTopology) &&
       asyncMgr->displayMorph && AnatomyDeformer_IsBound(asyncMgr->displayMorph) &&
       AnatomyDeformer_GetVertexCount(asyncMgr->displayMorph)==asyncMgr->displayMesh.vertexCount) {
        double deformStart=GetTimeMs();
        if(asyncMgr->displayDynamicMorph)
            presentedMorph=MorphSurfaceTracker_PredictMasked(&asyncMgr->displayTracker,
                asyncMgr->displayMorph,asyncMgr->displayHeadMorph,appearance,
                &asyncMgr->displayMesh,maskedPrediction?asyncMgr->displayTopologyMask:NULL);
        else {
            presentedMorph=AnatomyDeformer_DeformMasked(asyncMgr->displayMorph,
                &appearance->anatomyGraph,&asyncMgr->displayMesh,
                maskedPrediction?asyncMgr->displayTopologyMask:NULL);
            if(presentedMorph && appearance->hasHead && appearance->head.anatomy.attachmentBodyPartIndex<appearance->bodyPartCount)
                HeadMorph_Deform(asyncMgr->displayHeadMorph,&asyncMgr->displayMesh,&appearance->head.anatomy,
                    appearance->bodyParts[appearance->head.anatomy.attachmentBodyPartIndex].positionRender);
        }
        if(presentedMorph&&maskedPrediction) {
            for(size_t v=0;v<asyncMgr->displayMesh.vertexCount;++v)
                if(asyncMgr->displayTopologyMask[v]) {
                    asyncMgr->displayMesh.vertices[v].position=asyncMgr->displayAnchoredFrozen[v].position;
                    asyncMgr->displayMesh.vertices[v].normal=asyncMgr->displayAnchoredFrozen[v].normal;
                }
            /* La interfaz visible nueva se recorta por posición en GPU. El borde
             * de la antigua máscara binaria ya no es una costura publicada y no
             * debe inmovilizar el torso cuando la raíz actual se mueve. */
            bool geometricOwnership=asyncMgr->morphModuleCount>0;
            for(size_t m=0;m<asyncMgr->morphModuleCount;++m)
                geometricOwnership &= asyncMgr->morphModules[m].handoffInitialized;
            bool interfaceSafe=geometricOwnership;
            for(int attempt=0;!interfaceSafe&&attempt<12;++attempt) {
                bool ok = (asyncMgr->displayInterfaceTriangles && asyncMgr->displayInterfaceTriangleCount>0) ?
                    StableInterfaceValidFast(&asyncMgr->displayMesh,
                        &asyncMgr->displayCertifiedMesh,
                        asyncMgr->displayInterfaceTriangles,
                        asyncMgr->displayInterfaceTriangleCount,
                        asyncMgr->displayTopologyMask,
                        asyncMgr->displayAnchoredFrozen) :
                    StableInterfaceValid(&asyncMgr->displayMesh,
                        &asyncMgr->displayCertifiedMesh,
                        asyncMgr->displayTopologyMask,
                        asyncMgr->displayAnchoredFrozen);
                if(ok) {
                    interfaceSafe=true;
                    break;
                }
                /* Acotar la deformación estable contra el borde certificado.
                 * La pata conserva exactamente sus vértices y su conectividad. */
                for(size_t v=0;v<asyncMgr->displayMesh.vertexCount;++v) {
                    if(asyncMgr->displayTopologyMask[v])continue;
                    MeshVertex* vertex=&asyncMgr->displayMesh.vertices[v];
                    const MeshVertex* base=&asyncMgr->displayCertifiedMesh.vertices[v];
                    vertex->position=Vec3_Lerp(base->position,vertex->position,.5f);
                    vertex->normal=Vec3_Normalize(Vec3_Add(base->normal,
                        vertex->normal));
                }
                Mesh_MarkGeometryChanged(&asyncMgr->displayMesh);
            }
            if(!interfaceSafe) {
                for(size_t v=0;v<asyncMgr->displayMesh.vertexCount;++v) {
                    if(asyncMgr->displayTopologyMask[v])continue;
                    asyncMgr->displayMesh.vertices[v].position=asyncMgr->displayCertifiedMesh.vertices[v].position;
                    asyncMgr->displayMesh.vertices[v].normal=asyncMgr->displayCertifiedMesh.vertices[v].normal;
                }
                Mesh_MarkGeometryChanged(&asyncMgr->displayMesh);
                presentedMorph=false;
                asyncMgr->displayPredicted=false;
            }
        }
        pthread_mutex_lock(&asyncMgr->lock);
        asyncMgr->stats.morphDeformMs=(float)(GetTimeMs()-deformStart);
        if(presentedMorph)asyncMgr->stats.presentedMorphAge=appearance->hasGrowthAge?appearance->growthAge:NAN;
        if(presentedMorph)asyncMgr->displayPredicted=true;
        if(presentedMorph)asyncMgr->stats.presentedScale=appearance->hasCreaturePhenotype?appearance->phenotype.axial.totalScale:0;
        pthread_mutex_unlock(&asyncMgr->lock);
    }
    allowLiveArticulation=allowLiveArticulation||presentedMorph;

    /* Ojos: actualización continua de posición y escala morfológica en tiempo real */
    if (allowLiveArticulation && appearance->eyeCount > 0) {
        if (asyncMgr->displayEyeCount < appearance->eyeCount) {
            FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);
            asyncMgr->displayEyes = (MonsterVisualEyeAsync*)calloc(appearance->eyeCount, sizeof(MonsterVisualEyeAsync));
            if (asyncMgr->displayEyes) {
                for (size_t i = 0; i < appearance->eyeCount; ++i) {
                    asyncMgr->displayEyes[i].globe = Mesh_Create();
                }
                asyncMgr->displayEyeCount = appearance->eyeCount;
                asyncMgr->displayEyeCapacity = appearance->eyeCount;
            }
        }
        if (asyncMgr->displayEyes && asyncMgr->displayEyeCount >= appearance->eyeCount) {
            MonsterVisual_UpdateEyes(asyncMgr->displayEyes, asyncMgr->displayEyeCount, appearance);
        }
    }

    /* Mandíbula: actualización continua de articulación y escalado morfológico */
    if (allowLiveArticulation) {
        for (size_t i = 0; i < asyncMgr->displayMouthCount && i < appearance->mouthCount; ++i) {
            MonsterVisual_UpdateMouthArticulation(&asyncMgr->displayMouths[i], &appearance->mouths[i], appearance);
        }
    }

    pthread_mutex_lock(&asyncMgr->lock);
    float presMs=(float)(GetTimeMs()-presentationStart);
    asyncMgr->stats.presentationMs=presMs;
    asyncMgr->stats.presentationCPUms=presMs;
    asyncMgr->stats.frameCPUms=presMs;
    asyncMgr->stats.visualAge=asyncMgr->stats.presentedMorphAge;
    asyncMgr->stats.certifiedGeometryAge=asyncMgr->stats.certifiedAge;
    asyncMgr->stats.predictedGeometryAge=isfinite(asyncMgr->stats.presentedMorphAge)?
        asyncMgr->stats.presentedMorphAge:asyncMgr->stats.certifiedAge;
    asyncMgr->stats.topologyCertifiedAge=asyncMgr->stats.certifiedAge;
    asyncMgr->stats.workerCPUms=asyncMgr->stats.lastBuildDurationMs;
    asyncMgr->stats.staleResultsDiscarded=asyncMgr->stats.staleBuildDiscardedCount;
    asyncMgr->stats.sdfEvaluations=asyncMgr->stats.dynamicSurface.distanceEvaluations;
    pthread_mutex_unlock(&asyncMgr->lock);
    return updatedDisplay;
}

bool MonsterVisualAsync_Update(MonsterVisualAsync* asyncMgr,const Monster* monster,float deltaTime) {
    return MonsterVisualAsync_UpdateWithAppearance(asyncMgr,monster,monster,deltaTime);
}

bool MonsterVisualAsync_NeedsExactFallback(const MonsterVisualAsync* asyncMgr,
    float visualAge,float maximumFrozenAgeLag) {
    if(!asyncMgr)return false;
    pthread_mutex_lock((pthread_mutex_t*)&asyncMgr->lock);
    bool required=false;
    for(size_t i=0;i<asyncMgr->morphModuleCount;++i)
        if(MorphModuleState_NeedsExactFallback(&asyncMgr->morphModules[i],
            visualAge,maximumFrozenAgeLag)||(asyncMgr->morphModules[i].handoffInitialized&&
            asyncMgr->morphModules[i].handoffTransition>0)) {
            required=true;break;
        }
    pthread_mutex_unlock((pthread_mutex_t*)&asyncMgr->lock);
    return required;
}

bool MonsterVisualAsync_GetFallbackBounds(const MonsterVisualAsync* asyncMgr,
    float visualAge,float maximumFrozenAgeLag,AABB3D* outBounds) {
    if(!asyncMgr||!outBounds)return false;
    pthread_mutex_lock((pthread_mutex_t*)&asyncMgr->lock);
    bool required=false;
    uint32_t fallbackModuleIds[ANATOMY_MAX_NODES];
    size_t fallbackModuleCount=0;
    for(size_t i=0;i<asyncMgr->morphModuleCount;++i) {
        if(MorphModuleState_NeedsExactFallback(&asyncMgr->morphModules[i],
            visualAge,maximumFrozenAgeLag)) {
            required=true;
            if(fallbackModuleCount<ANATOMY_MAX_NODES)
                fallbackModuleIds[fallbackModuleCount++]=asyncMgr->morphModules[i].moduleId;
        }
    }
    if(!required) {
        pthread_mutex_unlock((pthread_mutex_t*)&asyncMgr->lock);
        return false;
    }

    AABB3D bounds=AABB_Empty();
    bool foundPoints=false;

    /* 1. Buscar vértices de la malla vinculados a las conexiones de los módulos afectados */
    const AnatomyDeformer* binding=asyncMgr->displayMorph;
    if(binding&&AnatomyDeformer_IsBound(binding)&&
       binding->vertexCount==asyncMgr->displayMesh.vertexCount&&
       asyncMgr->displayMesh.vertices) {
        for(size_t v=0;v<binding->vertexCount;++v) {
            const AnatomyDeformerBinding* b=&binding->bindings[v];
            for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k) {
                if(b->weight[k]<.15f||b->connIndex[k]>=binding->refGraph.connectionCount)
                    continue;
                uint32_t module=binding->refGraph.connections[b->connIndex[k]].moduleInstanceId;
                for(size_t m=0;m<fallbackModuleCount;++m) {
                    if(module==fallbackModuleIds[m]) {
                        AABB_ExpandPoint(&bounds,asyncMgr->displayMesh.vertices[v].position);
                        foundPoints=true;
                        break;
                    }
                }
            }
        }
    }

    /* 2. Si no hubo puntos en la malla (o faltaba vinculación), buscar en las estaciones del grafo anatómico */
    if(!foundPoints&&asyncMgr->hasMorphEndpoints) {
        const AnatomyGraph* graphs[2]={
            &asyncMgr->morphStartSnapshot.anatomyGraph,
            &asyncMgr->morphEndSnapshot.anatomyGraph
        };
        for(int g=0;g<2;++g) {
            const AnatomyGraph* ag=graphs[g];
            for(size_t n=0;n<ag->nodeCount;++n) {
                const AnatomyNode* node=&ag->nodes[n];
                for(size_t m=0;m<fallbackModuleCount;++m) {
                    if(node->moduleInstanceId==fallbackModuleIds[m]) {
                        float r=fmaxf(node->widthRadius,node->heightRadius);
                        if(node->envelopeRadii.x>r)r=node->envelopeRadii.x;
                        if(node->envelopeRadii.y>r)r=node->envelopeRadii.y;
                        if(node->envelopeRadii.z>r)r=node->envelopeRadii.z;
                        if(r<0.05f)r=0.05f;
                        Vector3 rad=Vec3_Create(r,r,r);
                        AABB_ExpandPoint(&bounds,Vec3_Sub(node->center,rad));
                        AABB_ExpandPoint(&bounds,Vec3_Add(node->center,rad));
                        foundPoints=true;
                        break;
                    }
                }
            }
        }
    }

    if(!foundPoints) {
        for(size_t v=0;v<asyncMgr->displayMesh.vertexCount;++v) {
            AABB_ExpandPoint(&bounds,asyncMgr->displayMesh.vertices[v].position);
            foundPoints=true;
        }
    }

    pthread_mutex_unlock((pthread_mutex_t*)&asyncMgr->lock);

    if(!foundPoints)return false;

    AABB_Pad(&bounds,0.08f);
    *outBounds=bounds;
    return true;
}

size_t MonsterVisualAsync_GetFallbackRegions(const MonsterVisualAsync* asyncMgr,
    const MonsterSDF* currentField,const AnatomyGraph* currentGraph,
    float visualAge,float maximumFrozenAgeLag,
    MorphFallbackRegion* output,size_t capacity) {
    if(!asyncMgr||!currentField||!output||!capacity)return 0;
    pthread_mutex_lock((pthread_mutex_t*)&asyncMgr->lock);
    size_t count=0;
    const AnatomyDeformer* binding=asyncMgr->displayMorph;
    bool bound=binding&&AnatomyDeformer_IsBound(binding)&&
        binding->vertexCount==asyncMgr->displayMesh.vertexCount&&
        asyncMgr->displayMesh.vertices;
    for(size_t m=0;m<asyncMgr->morphModuleCount&&count<capacity;++m) {
        const MorphModuleState* state=&asyncMgr->morphModules[m];
        if(state->handoffTransition<=0&&
           !MorphModuleState_NeedsExactFallback(state,visualAge,maximumFrozenAgeLag))continue;
        AABB3D box=AABB_Empty();
        bool found=false;
        for(size_t c=0;c<currentField->connectorCount;++c) {
            const MonsterSDFConnector* connector=&currentField->connectors[c];
            if(connector->moduleInstanceId!=state->moduleId)continue;
            AABB_ExpandPoint(&box,connector->bounds.start);
            AABB_ExpandPoint(&box,connector->bounds.end);
            found=true;
        }
        bool currentFieldPresent=found;
        if(!found&&bound)for(size_t v=0;v<binding->vertexCount;++v) {
            const AnatomyDeformerBinding* b=&binding->bindings[v];
            bool owned=false;
            for(int k=0;k<ANATOMY_MORPH_INFLUENCES;++k) {
                if(b->weight[k]<.15f||b->connIndex[k]>=binding->refGraph.connectionCount)continue;
                if(binding->refGraph.connections[b->connIndex[k]].moduleInstanceId==state->moduleId) {
                    owned=true;break;
                }
            }
            if(owned) {
                AABB_ExpandPoint(&box,asyncMgr->displayMesh.vertices[v].position);
                found=true;
            }
        }
        if(!found)continue;
        MorphHandoffSet interfaces;
        MorphHandoff_BuildFromGraph(currentGraph,&state->moduleId,1,&interfaces);
        if(!interfaces.count&&!currentFieldPresent&&bound)
            MorphHandoff_BuildFromGraph(&binding->refGraph,&state->moduleId,1,&interfaces);
        if(!interfaces.count||!interfaces.interfaces[0].active) {
            fprintf(stderr,"[Cesión] Interfaz ausente o soporte excedido: módulo %u.\n",state->moduleId);continue;
        }
        MorphHandoffInterface h=interfaces.interfaces[0];
        h.retireOnly=!currentFieldPresent;
        h.transitionFactor=h.retireOnly?1:(state->handoffInitialized?state->handoffTransition:1);
        if(bound&&(!state->handoffInitialized||h.retireOnly)&&!MorphHandoff_AddReference(&h,&binding->refGraph,currentGraph)) {
            fprintf(stderr,"[Cesión] Soporte de referencia excedido: módulo %u.\n",state->moduleId);continue;
        }
        AABB3D support=MorphHandoff_WorkBounds(&h);
        AABB_ExpandPoint(&box,support.start);AABB_ExpandPoint(&box,support.end);
        float collar=currentField->config.connectionSmoothness+
            currentField->config.bodySmoothness+4*.001f;
        AABB_Pad(&box,collar);
        output[count++]=(MorphFallbackRegion){
            .moduleId=state->moduleId,.kind=MORPH_FALLBACK_MODULE,.bounds=box,
            .morphGeneration=asyncMgr->displayEndpointGeneration,
            .targetAge=visualAge,.visualAge=visualAge,.certifiedAge=state->certifiedAge,
            .handoff=h,.handoffState=state->handoffState,
            .representation=currentFieldPresent?
                MORPH_REP_LOCAL_SDF:MORPH_REP_ABSENT
        };
    }
    /* Axial/cabeza conservan sus modos diagnósticos. Un residual aislado no
     * autoriza cambiar su representación; requieren un evento topológico. */
    pthread_mutex_unlock((pthread_mutex_t*)&asyncMgr->lock);
    return count;
}

const Mesh* MonsterVisualAsync_GetOwnedMesh(MonsterVisualAsync* asyncMgr,
    const MorphFallbackRegion* regions,size_t count,size_t* maskedTriangles) {
    if(maskedTriangles)*maskedTriangles=0;
    if(!asyncMgr)return NULL;
    const Mesh* source=&asyncMgr->displayMesh;
    (void)regions;(void)count;
    return source;
}

void MonsterVisualAsync_RecordFallbackFrame(MonsterVisualAsync* asyncMgr,
    const MorphFallbackRegion* regions,size_t count,size_t sdfRendered,
    bool meshRendered,size_t maskedTriangles) {
    if(!asyncMgr)return;
    float volume=0.0f;
    for(size_t i=0;regions&&i<count;++i) {
        Vector3 size=AABB_Size(regions[i].bounds);
        volume+=size.x*size.y*size.z;
    }
    pthread_mutex_lock(&asyncMgr->lock);
    asyncMgr->stats.fallbackRegionCount=count;
    asyncMgr->stats.fallbackModuleCount=count;
    asyncMgr->stats.fallbackCoverageVolume=volume;
    asyncMgr->stats.meshChunksRendered=meshRendered?1:0;
    asyncMgr->stats.meshChunksMasked=meshRendered&&count?1:0;
    asyncMgr->stats.sdfFallbackRegionsRendered=sdfRendered;
    asyncMgr->stats.meshTrianglesMasked=maskedTriangles;
    if(count)++asyncMgr->stats.fallbackFrames;
    pthread_mutex_unlock(&asyncMgr->lock);
}

const Mesh* MonsterVisualAsync_GetDisplayMesh(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? &asyncMgr->displayMesh : NULL;
}

const Mesh* MonsterVisualAsync_GetDisplayHeadMesh(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? &asyncMgr->displayHeadMesh : NULL;
}

size_t MonsterVisualAsync_GetDisplayEyeCount(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? asyncMgr->displayEyeCount : 0;
}

const Mesh* MonsterVisualAsync_GetDisplayEyeSclera(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return &asyncMgr->displayEyes[index].globe;
}

const Mesh* MonsterVisualAsync_GetDisplayEyeIris(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return NULL;
}

const Mesh* MonsterVisualAsync_GetDisplayEyePupil(const MonsterVisualAsync* asyncMgr, size_t index) {
    if (!asyncMgr || index >= asyncMgr->displayEyeCount || !asyncMgr->displayEyes) return NULL;
    return NULL;
}

size_t MonsterVisualAsync_GetDisplayMouthCount(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? asyncMgr->displayMouthCount : 0;
}

const Mesh* MonsterVisualAsync_GetDisplayMouthMesh(
    const MonsterVisualAsync* asyncMgr,
    size_t mouthIndex,
    size_t meshIndex
) {
    if (!asyncMgr || mouthIndex >= asyncMgr->displayMouthCount || meshIndex >= 2) return NULL;
    const MonsterVisualMouth* mouth = &asyncMgr->displayMouths[mouthIndex];
    switch (meshIndex) {
        case 0: return &mouth->jaw;
        case 1: return &mouth->hinge;
        default: return NULL;
    }
}

bool MonsterVisualAsync_Render(const MonsterVisualAsync* asyncMgr, Renderer3D* renderer) {
    if (!asyncMgr || !renderer || !renderer->renderMesh) return false;

    if (asyncMgr->displayMesh.vertexCount > 0) {
        renderer->renderMesh(renderer, &asyncMgr->displayMesh);
    }
    if(asyncMgr->displayHeadMesh.vertexCount>0)renderer->renderMesh(renderer,&asyncMgr->displayHeadMesh);
    for (size_t m = 0; m < asyncMgr->displayMouthCount; ++m) {
        const MonsterVisualMouth* mouth = &asyncMgr->displayMouths[m];
        if (mouth->jaw.vertexCount > 0) renderer->renderMesh(renderer, &mouth->jaw);
        if (mouth->hinge.vertexCount > 0) renderer->renderMesh(renderer, &mouth->hinge);
    }
    for (size_t i = 0; i < asyncMgr->displayEyeCount; ++i) {
        if (asyncMgr->displayEyes[i].globe.vertexCount > 0) {
            if(renderer->renderEye)renderer->renderEye(renderer,&asyncMgr->displayEyes[i]);
            else renderer->renderMesh(renderer,&asyncMgr->displayEyes[i].globe);
        }
    }
    return true;
}

uint64_t MonsterVisualAsync_GetDisplayGeneration(const MonsterVisualAsync* asyncMgr) {
    return asyncMgr ? asyncMgr->displayGeneration : 0;
}

MonsterVisualAsyncStats MonsterVisualAsync_GetStats(const MonsterVisualAsync* asyncMgr) {
    MonsterVisualAsyncStats s;
    memset(&s, 0, sizeof(MonsterVisualAsyncStats));
    if (!asyncMgr) return s;

    pthread_mutex_lock((pthread_mutex_t*)&asyncMgr->lock);
    s = asyncMgr->stats;
    s.geometryLag=fabsf(s.targetAge-s.displayedGeometryAge);
    if(!s.isWorkerBusy)s.workingGeometryAge=NAN;
    if(!asyncMgr->hasPendingRequest)s.pendingGeometryAge=NAN;
    if(asyncMgr->displayGeneration>0) {
        s.lastBuildDurationMs=asyncMgr->displayStats.lastBuildDurationMs;
        s.sdfBuildMs=asyncMgr->displayStats.sdfBuildMs;s.bodyMeshMs=asyncMgr->displayStats.bodyMeshMs;
        s.headMeshMs=asyncMgr->displayStats.headMeshMs;s.surfaceMappingMs=asyncMgr->displayStats.surfaceMappingMs;
        s.eyeBuildMs=asyncMgr->displayStats.eyeBuildMs;s.mouthBuildMs=asyncMgr->displayStats.mouthBuildMs;
        s.morphBindingMs=asyncMgr->displayStats.morphBindingMs;s.headBindingMs=asyncMgr->displayStats.headBindingMs;
        s.readyPublicationMs=asyncMgr->displayStats.readyPublicationMs;
        s.dynamicSurface=asyncMgr->displayStats.dynamicSurface;
        s.bodyOpposedTriangles=asyncMgr->displayStats.bodyOpposedTriangles;
        s.bodyDegenerateTriangles=asyncMgr->displayStats.bodyDegenerateTriangles;
        s.bodyBoundaryEdges=asyncMgr->displayStats.bodyBoundaryEdges;
        s.bodyDisconnectedComponents=asyncMgr->displayStats.bodyDisconnectedComponents;
        s.bodyValidationMs=asyncMgr->displayStats.bodyValidationMs;
        s.surfaceMapper=asyncMgr->displayStats.surfaceMapper;
        s.activeQualityTier=asyncMgr->displayStats.activeQualityTier;
        s.bodyMesher=asyncMgr->displayStats.bodyMesher;
        s.headMesher=asyncMgr->displayStats.headMesher;
    }
    pthread_mutex_unlock((pthread_mutex_t*)&asyncMgr->lock);

    return s;
}

void MonsterVisualAsync_Flush(MonsterVisualAsync* asyncMgr) {
    if (!asyncMgr) return;

    while (1) {
        pthread_mutex_lock(&asyncMgr->lock);
        bool busy = asyncMgr->hasPendingRequest || asyncMgr->stats.isWorkerBusy;
        pthread_mutex_unlock(&asyncMgr->lock);

        if (!busy) break;
        usleep(1000); /* 1 ms */
    }

    /* Intercambiar si quedó algo en readyMesh */
    pthread_mutex_lock(&asyncMgr->lock);
    if(asyncMgr->hasReadyMesh && asyncMgr->hasMorphEndpoints &&
       asyncMgr->readyEndpointGeneration!=asyncMgr->morphEndpointGeneration) {
        asyncMgr->hasReadyMesh=false;
        asyncMgr->stats.obsoleteTopologyResultCount++;
    }
    if(asyncMgr->hasReadyMesh)for(size_t i=0;i<asyncMgr->morphModuleCount;++i) {
        MorphModuleState candidate=asyncMgr->morphModules[i];
        MorphPatchToken token=MorphModuleState_Token(&candidate,
            asyncMgr->readyStats.workingGeometryAge);
        if(!MorphModuleState_Accept(&candidate,token)) {
            asyncMgr->hasReadyMesh=false;
            asyncMgr->stats.obsoleteTopologyResultCount++;
            break;
        }
    }
    if (asyncMgr->hasReadyMesh) {
        double publicationStart=GetTimeMs();
        FreeEyeArray(asyncMgr->displayEyes, asyncMgr->displayEyeCount);

        Mesh tmp = asyncMgr->displayMesh;
        asyncMgr->displayMesh = asyncMgr->readyMesh;
        asyncMgr->readyMesh = tmp;
        Mesh tmpHead=asyncMgr->displayHeadMesh;
        asyncMgr->displayHeadMesh=asyncMgr->readyHeadMesh;
        asyncMgr->readyHeadMesh=tmpHead;

        AnatomyDeformer* tmpMorph = asyncMgr->displayMorph;
        asyncMgr->displayMorph = asyncMgr->readyMorph;
        asyncMgr->readyMorph = tmpMorph;
        HeadMorph* tmpHeadMorph=asyncMgr->displayHeadMorph;
        asyncMgr->displayHeadMorph=asyncMgr->readyHeadMorph;asyncMgr->readyHeadMorph=tmpHeadMorph;
        MorphSurfaceTracker tmpTracker=asyncMgr->displayTracker;
        asyncMgr->displayTracker=asyncMgr->readyTracker;asyncMgr->readyTracker=tmpTracker;
        asyncMgr->displayDynamicMorph=asyncMgr->readyDynamicMorph;
        MorphChunkManager* tmpChunkMgr = asyncMgr->displayChunkManager;
        asyncMgr->displayChunkManager = asyncMgr->readyChunkManager;
        asyncMgr->readyChunkManager = tmpChunkMgr;

        asyncMgr->displayEyes = asyncMgr->readyEyes;
        asyncMgr->displayEyeCount = asyncMgr->readyEyeCount;
        asyncMgr->displayEyeCapacity = asyncMgr->readyEyeCapacity;
        asyncMgr->readyEyes = NULL;
        asyncMgr->readyEyeCount = 0;
        asyncMgr->readyEyeCapacity = 0;

        FreeMouthArray(asyncMgr->displayMouths, asyncMgr->displayMouthCount);
        asyncMgr->displayMouths = asyncMgr->readyMouths;
        asyncMgr->displayMouthCount = asyncMgr->readyMouthCount;
        asyncMgr->displayMouthCapacity = asyncMgr->readyMouthCapacity;
        asyncMgr->readyMouths = NULL;
        asyncMgr->readyMouthCount = 0;
        asyncMgr->readyMouthCapacity = 0;

        asyncMgr->displayGeneration = asyncMgr->readyGeneration;
        asyncMgr->displayEndpointGeneration=asyncMgr->readyEndpointGeneration;
        asyncMgr->displayPredicted=false;
        bool certifiedCopy=TrackedCloneMorphMesh(asyncMgr,&asyncMgr->displayCertifiedMesh,
            &asyncMgr->displayMesh);
        asyncMgr->displayFingerprint = asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedFingerprint=asyncMgr->readyFingerprint;
        asyncMgr->stats.displayedGeometryAge=asyncMgr->readyStats.workingGeometryAge;
        asyncMgr->stats.certifiedAge=certifiedCopy?
            asyncMgr->stats.displayedGeometryAge:NAN;
        if(certifiedCopy)for(size_t i=0;i<asyncMgr->morphModuleCount;++i) {
            MorphPatchToken token=MorphModuleState_Token(&asyncMgr->morphModules[i],
                asyncMgr->stats.displayedGeometryAge);
            MorphModuleState_Accept(&asyncMgr->morphModules[i],token);
        }
        if(!BuildDisplayTopologyMask(asyncMgr))
            asyncMgr->stats.topologyFrozenVertices=asyncMgr->displayMesh.vertexCount;
        asyncMgr->stats.presentedMorphAge=asyncMgr->stats.displayedGeometryAge;
        asyncMgr->stats.displayedScale=asyncMgr->readyScale;
        asyncMgr->stats.presentedScale=asyncMgr->readyScale;
        asyncMgr->displayAppendageDevelopment=asyncMgr->readyAppendageDevelopment;
        asyncMgr->displayStats=asyncMgr->readyStats;
        asyncMgr->hasReadyMesh = false;
        asyncMgr->stats.displayPublicationMs=(float)(GetTimeMs()-publicationStart);
    }
    pthread_mutex_unlock(&asyncMgr->lock);
}

const MorphChunkManager* MonsterVisualAsync_GetChunkManager(const MonsterVisualAsync* asyncMgr) {
    if (!asyncMgr) return NULL;
    return asyncMgr->displayChunkManager;
}

void MonsterVisualAsync_UpdateHandoffs(MonsterVisualAsync* m,float age,float lag,float dt) {
    if(!m)return;
    pthread_mutex_lock(&m->lock);
    for(size_t i=0;i<m->morphModuleCount;++i)
        MorphModuleState_UpdateHandoff(&m->morphModules[i],age,lag,
            m->displayEndpointGeneration==m->morphEndpointGeneration,dt,.12f);
    pthread_mutex_unlock(&m->lock);
}
