/** @file MorphSurfaceTracker.c
 * @brief Predicción persistente y proyección local contra el campo exacto.
 */
#include "MorphSurfaceTracker.h"
#include "MonsterSDF.h"
#include "AABB.h"
#include "MathUtils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct MorphSurfaceBrick {
    uint64_t key;
    AABB3D bounds;
    float projectionRadius;
    size_t count,offset,cursor,candidateCount;
    const MonsterSDF* sdf;
    bool qualityProblem;
    uint16_t candidates[ANATOMY_MAX_CONNECTIONS];
} MorphSurfaceBrick;

static double Tracker_NowMs(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec*1000.0+(double)t.tv_nsec/1000000.0;
}
static float Tracker_QuickSelect(float* arr, size_t n, size_t k) {
    if (n == 0) return 0.0f;
    if (k >= n) k = n - 1;
    ptrdiff_t left = 0, right = (ptrdiff_t)n - 1;
    while (left < right) {
        ptrdiff_t mid = left + (right - left) / 2;
        float a = arr[left], b = arr[mid], c = arr[right];
        float pivot = ((a <= b && b <= c) || (c <= b && b <= a)) ? b :
                      ((b <= a && a <= c) || (c <= a && a <= b)) ? a : c;
        ptrdiff_t i = left, j = right;
        while (i <= j) {
            while (arr[i] < pivot) i++;
            while (arr[j] > pivot) j--;
            if (i <= j) {
                float tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
                i++; j--;
            }
        }
        if ((ptrdiff_t)k <= j) {
            right = j;
        } else if ((ptrdiff_t)k >= i) {
            left = i;
        } else {
            break;
        }
    }
    return arr[k];
}
void MorphSurfaceTracker_Free(MorphSurfaceTracker* t) {
    if(!t)return;
    free(t->corrections);free(t->normalCorrections);free(t->rawPositions);
    free(t->rawNormals);free(t->edgeLength);free(t->residuals);
    free(t->triangleOffsets);free(t->incidentTriangles);free(t->normalDirty);
    free(t->problemTriangles);free(t->referenceThinTriangles);
    free(t->bricks);free(t->brickTable);free(t->vertexOrder);free(t->vertexBrick);
    memset(t,0,sizeof(*t));
}
void MorphSurfaceTracker_SetPool(MorphSurfaceTracker* t,SDFSamplingPool* pool) {
    if(t)t->pool=pool;
}
bool MorphSurfaceTracker_Bind(MorphSurfaceTracker* t,const Mesh* mesh,float age) {
    if(!t||!mesh||!mesh->vertexCount||!mesh->vertices||!mesh->indices||
       mesh->indexCount>UINT32_MAX)return false;
    MorphSurfaceTracker_Free(t);
    size_t n=mesh->vertexCount;
    t->corrections=calloc(n,sizeof(*t->corrections));
    t->normalCorrections=calloc(n,sizeof(*t->normalCorrections));
    t->rawPositions=malloc(n*sizeof(*t->rawPositions));
    t->rawNormals=malloc(n*sizeof(*t->rawNormals));
    t->edgeLength=calloc(n,sizeof(*t->edgeLength));
    t->residuals=malloc(n*sizeof(*t->residuals));
    t->triangleOffsets=calloc(n+1,sizeof(*t->triangleOffsets));
    t->incidentTriangles=malloc(mesh->indexCount*sizeof(*t->incidentTriangles));
    t->normalDirty=calloc(n,sizeof(*t->normalDirty));
    t->problemTriangles=calloc(mesh->indexCount/3,sizeof(*t->problemTriangles));
    t->referenceThinTriangles=calloc(mesh->indexCount/3,sizeof(*t->referenceThinTriangles));
    t->vertexOrder=malloc(n*sizeof(*t->vertexOrder));
    t->vertexBrick=malloc(n*sizeof(*t->vertexBrick));
    size_t tableSize=1;while(tableSize<n*2)tableSize*=2;
    t->brickTable=calloc(tableSize,sizeof(*t->brickTable));
    t->brickTableCapacity=tableSize;
    uint32_t* counts=calloc(n,sizeof(*counts));
    if(!t->corrections||!t->normalCorrections||!t->rawPositions||!t->rawNormals||
       !t->edgeLength||!t->residuals||
       !t->triangleOffsets||!t->incidentTriangles||!t->normalDirty||
       !t->problemTriangles||!t->referenceThinTriangles||
       !t->vertexOrder||!t->vertexBrick||!t->brickTable||!counts) {
        free(counts);MorphSurfaceTracker_Free(t);return false;
    }
    for(size_t i=0;i+2<mesh->indexCount;i+=3)for(int edge=0;edge<3;++edge) {
        MeshIndex a=mesh->indices[i+edge],b=mesh->indices[i+(edge+1)%3];
        if(a>=n||b>=n)continue;
        float length=Vec3_Distance(mesh->vertices[a].position,mesh->vertices[b].position);
        t->edgeLength[a]+=length;t->edgeLength[b]+=length;
        counts[a]++;counts[b]++;
    }
    for(size_t i=0;i<n;++i)t->edgeLength[i]=counts[i]?
        t->edgeLength[i]/counts[i]:.01f;
    memset(counts,0,n*sizeof(*counts));
    for(size_t i=0;i+2<mesh->indexCount;i+=3)
        for(int corner=0;corner<3;++corner)
            if(mesh->indices[i+corner]<n)counts[mesh->indices[i+corner]]++;
    for(size_t i=0;i<n;++i)t->triangleOffsets[i+1]=t->triangleOffsets[i]+counts[i];
    for(size_t i=0;i+2<mesh->indexCount;i+=3)
        for(int corner=0;corner<3;++corner) {
            MeshIndex vertex=mesh->indices[i+corner];
            if(vertex<n)t->incidentTriangles[t->triangleOffsets[vertex]+--counts[vertex]]=(uint32_t)i;
        }
    free(counts);
    for(size_t i=0;i<n;++i) {
        t->rawNormals[i]=mesh->vertices[i].normal;
    }
    for(size_t triangle=0;triangle+2<mesh->indexCount;triangle+=3) {
        Vector3 a=mesh->vertices[mesh->indices[triangle]].position;
        Vector3 b=mesh->vertices[mesh->indices[triangle+1]].position;
        Vector3 c=mesh->vertices[mesh->indices[triangle+2]].position;
        Vector3 ab=Vec3_Sub(b,a),bc=Vec3_Sub(c,b),ca=Vec3_Sub(a,c);
        float longest=fmaxf(Vec3_Dot(ab,ab),fmaxf(Vec3_Dot(bc,bc),Vec3_Dot(ca,ca)));
        Vector3 area=Vec3_Cross(ab,Vec3_Sub(c,a));
        t->referenceThinTriangles[triangle/3]=longest>1e-12f &&
            Vec3_Length(area)/longest<.02f;
    }
    t->vertexCount=n;t->triangleCount=mesh->indexCount/3;
    t->referenceAge=age;t->correctedAge=age;
    return true;
}
bool MorphSurfaceTracker_CopyCorrections(MorphSurfaceTracker* dst,const MorphSurfaceTracker* src) {
    if(!dst||!src||dst->vertexCount!=src->vertexCount||
       dst->triangleCount!=src->triangleCount||!dst->problemTriangles||
       !src->problemTriangles||!dst->corrections||
       !dst->normalCorrections||!src->corrections||!src->normalCorrections)return false;
    memcpy(dst->corrections,src->corrections,src->vertexCount*sizeof(*src->corrections));
    memcpy(dst->normalCorrections,src->normalCorrections,
        src->vertexCount*sizeof(*src->normalCorrections));
    memcpy(dst->problemTriangles,src->problemTriangles,
        src->triangleCount*sizeof(*src->problemTriangles));
    dst->correctedAge=src->correctedAge;
    dst->stats=src->stats;
    return true;
}
bool MorphSurfaceTracker_PredictMasked(MorphSurfaceTracker* t,const AnatomyDeformer* anatomy,
    const HeadMorph* head,const Monster* target,Mesh* mesh,
    const uint8_t* frozenVertices) {
    if(!t||!anatomy||!target||!mesh||mesh->vertexCount!=t->vertexCount)return false;
    double begin=Tracker_NowMs();
    if(!AnatomyDeformer_DeformMasked(anatomy,&target->anatomyGraph,mesh,
        frozenVertices))return false;
    if(head && target->hasHead && target->head.anatomy.attachmentBodyPartIndex<target->bodyPartCount)
        if(!HeadMorph_Deform(head,mesh,&target->head.anatomy,
            target->bodyParts[target->head.anatomy.attachmentBodyPartIndex].positionRender))return false;
    float correctionScale=1.0f;
    if(target->hasGrowthAge && isfinite(target->growthAge) &&
       isfinite(t->referenceAge) && isfinite(t->correctedAge)) {
        float before=t->correctedAge-t->referenceAge;
        float after=target->growthAge-t->referenceAge;
        if(before*after<=0.0f || fabsf(after)<1e-5f)
            correctionScale=0.0f;
        else if(fabsf(after)<fabsf(before))
            correctionScale=Math_Clamp01(fabsf(after)/fabsf(before));
    }
    for(size_t i=0;i<t->vertexCount;++i) {
        if(frozenVertices&&frozenVertices[i])continue;
        t->rawPositions[i]=mesh->vertices[i].position;
        t->rawNormals[i]=mesh->vertices[i].normal;
        mesh->vertices[i].position=Vec3_Add(mesh->vertices[i].position,
            Vec3_Scale(t->corrections[i],correctionScale));
        Vector3 corrected=Vec3_Add(mesh->vertices[i].normal,
            Vec3_Scale(t->normalCorrections[i],correctionScale));
        if(Vec3_Dot(corrected,corrected)>1e-10f)
            mesh->vertices[i].normal=Vec3_Normalize(corrected);
    }
    Mesh_MarkGeometryChanged(mesh);
    t->stats.deformationMs=(float)(Tracker_NowMs()-begin);
    return true;
}

bool MorphSurfaceTracker_Predict(MorphSurfaceTracker* t,const AnatomyDeformer* anatomy,
    const HeadMorph* head,const Monster* target,Mesh* mesh) {
    return MorphSurfaceTracker_PredictMasked(t,anatomy,head,target,mesh,NULL);
}
static float Tracker_Distance(const SDFField* field,Vector3 p,MorphSurfaceTrackerStats* stats) {
    stats->distanceEvaluations++;
    return field->evaluateDistance?field->evaluateDistance(field->context,p):
        field->evaluate(field->context,p).distance;
}
static Vector3 Tracker_Gradient(const SDFField* field,Vector3 p,float h,MorphSurfaceTrackerStats* stats) {
    Vector3 x=Vec3_Create(h,0,0),y=Vec3_Create(0,h,0),z=Vec3_Create(0,0,h);
    stats->gradientEvaluations++;
    return Vec3_Create(
        (Tracker_Distance(field,Vec3_Add(p,x),stats)-Tracker_Distance(field,Vec3_Sub(p,x),stats))/(2*h),
        (Tracker_Distance(field,Vec3_Add(p,y),stats)-Tracker_Distance(field,Vec3_Sub(p,y),stats))/(2*h),
        (Tracker_Distance(field,Vec3_Add(p,z),stats)-Tracker_Distance(field,Vec3_Sub(p,z),stats))/(2*h));
}
static uint64_t Tracker_BrickKey(Vector3 p) {
    const float inverse=.25f;
    int x=(int)floorf(p.x/inverse)+2048;
    int y=(int)floorf(p.y/inverse)+2048;
    int z=(int)floorf(p.z/inverse)+2048;
    x=x<0?0:x>4095?4095:x;
    y=y<0?0:y>4095?4095:y;
    z=z<0?0:z>4095?4095:z;
    return (uint64_t)(unsigned)x|((uint64_t)(unsigned)y<<12)|((uint64_t)(unsigned)z<<24);
}
static uint64_t Tracker_Hash(uint64_t key) {
    key^=key>>30;key*=UINT64_C(0xbf58476d1ce4e5b9);
    key^=key>>27;key*=UINT64_C(0x94d049bb133111eb);
    return key^(key>>31);
}
static bool Tracker_BuildBricks(MorphSurfaceTracker* t,const Mesh* mesh,
    const MonsterSDF* sdf,MorphSurfaceTrackerStats* stats) {
    double start=Tracker_NowMs();
    memset(t->brickTable,0,t->brickTableCapacity*sizeof(*t->brickTable));
    t->brickCount=0;
    for(size_t i=0;i<t->vertexCount;++i) {
        Vector3 p=mesh->vertices[i].position;
        uint64_t key=Tracker_BrickKey(p);
        size_t slot=Tracker_Hash(key)&(t->brickTableCapacity-1);
        while(t->brickTable[slot] && t->bricks[t->brickTable[slot]-1].key!=key)
            slot=(slot+1)&(t->brickTableCapacity-1);
        size_t brickIndex;
        if(!t->brickTable[slot]) {
            if(t->brickCount==t->brickCapacity) {
                size_t next=t->brickCapacity?t->brickCapacity*2:256;
                MorphSurfaceBrick* grown=realloc(t->bricks,next*sizeof(*t->bricks));
                if(!grown)return false;
                t->bricks=grown;t->brickCapacity=next;
            }
            brickIndex=t->brickCount++;
            t->brickTable[slot]=brickIndex+1;
            t->bricks[brickIndex]=(MorphSurfaceBrick){.key=key,.bounds=AABB_Empty(),.sdf=sdf};
        } else brickIndex=t->brickTable[slot]-1;
        MorphSurfaceBrick* brick=&t->bricks[brickIndex];
        AABB_ExpandPoint(&brick->bounds,p);
        brick->projectionRadius=fmaxf(brick->projectionRadius,
            2*fmaxf(.035f,t->edgeLength[i]*2.5f)+.020f);
        brick->count++;
        t->vertexBrick[i]=(uint32_t)brickIndex;
    }
    size_t offset=0;
    for(size_t i=0;i<t->brickCount;++i) {
        MorphSurfaceBrick* brick=&t->bricks[i];
        brick->offset=brick->cursor=offset;offset+=brick->count;
        AABB_Pad(&brick->bounds,brick->projectionRadius);
    }
    for(size_t i=0;i<t->vertexCount;++i) {
        MorphSurfaceBrick* brick=&t->bricks[t->vertexBrick[i]];
        t->vertexOrder[brick->cursor++]=(uint32_t)i;
    }
    stats->brickUpdateMs=(float)(Tracker_NowMs()-start);
    start=Tracker_NowMs();
    if(sdf)for(size_t i=0;i<t->brickCount;++i) {
        MorphSurfaceBrick* brick=&t->bricks[i];
        brick->candidateCount=MonsterSDF_CompileConnectorCandidates(sdf,brick->bounds,
            brick->candidates,ANATOMY_MAX_CONNECTIONS);
    }
    stats->candidateCompileMs=(float)(Tracker_NowMs()-start);
    stats->processedBricks=t->brickCount;
    return true;
}
static float Tracker_BrickDistance(const void* context,Vector3 point) {
    const MorphSurfaceBrick* brick=context;
    return MonsterSDF_EvaluateDistanceCandidates(brick->sdf,point,
        brick->candidates,brick->candidateCount);
}
typedef struct TrackerCorrectionJob {
    MorphSurfaceTracker* tracker;
    const SDFField* field;
    Mesh* mesh;
    const MonsterSDF* sdf;
    MorphSurfaceTrackerStats partial[SDF_SAMPLING_POOL_MAX_THREADS];
    double sum[SDF_SAMPLING_POOL_MAX_THREADS];
    bool failed[SDF_SAMPLING_POOL_MAX_THREADS];
} TrackerCorrectionJob;

static void Tracker_CorrectRange(void* context,int start,int end,int threadIndex) {
    TrackerCorrectionJob* job=context;
    MorphSurfaceTracker* t=job->tracker;
    Mesh* mesh=job->mesh;
    MorphSurfaceTrackerStats* stats=&job->partial[threadIndex];
    if(job->sdf){MonsterSDF_EnableThreadStats(true);MonsterSDF_ResetThreadStats();}
    SDFField candidateField=*job->field;
    const SDFField* field=job->field;
    uint32_t lastBrick=UINT32_MAX;
    for(int vertexIndex=start;vertexIndex<end;++vertexIndex) {
        size_t i=t->vertexOrder?(size_t)t->vertexOrder[vertexIndex]:(size_t)vertexIndex;
        if(job->sdf && t->vertexBrick[i]!=lastBrick) {
            lastBrick=t->vertexBrick[i];
            candidateField.context=&t->bricks[lastBrick];
            candidateField.evaluateDistance=Tracker_BrickDistance;
            field=t->bricks[lastBrick].candidateCount==SIZE_MAX?job->field:&candidateField;
        }
        MeshVertex* vertex=&mesh->vertices[i];
        Vector3 p=vertex->position;
        Vector3 predicted=p;
        float edge=fmaxf(t->edgeLength[i],.001f);
        float tolerance=Math_Clamp(edge*.22f,.0015f,.010f);
        float h=Math_Clamp(edge*.25f,.001f,.008f);
        float f=Tracker_Distance(field,p,stats);
        if(!isfinite(f)){job->failed[threadIndex]=true;continue;}
        if(fabsf(f)>tolerance) {
            stats->projectedVertices++;
            for(int iteration=0;iteration<2 && fabsf(f)>tolerance;++iteration) {
                Vector3 n=Vec3_Normalize(vertex->normal);
                float stepLimit=fmaxf(.035f,edge*2.5f);
                float plus=Tracker_Distance(field,Vec3_Add(p,Vec3_Scale(n,h)),stats);
                float minus=Tracker_Distance(field,Vec3_Sub(p,Vec3_Scale(n,h)),stats);
                float derivative=(plus-minus)/(2*h);
                Vector3 candidate=p;float next=f;
                if(isfinite(derivative)&&fabsf(derivative)>.15f) {
                    float move=Math_Clamp(-f/derivative,-stepLimit,stepLimit);
                    candidate=Vec3_Add(p,Vec3_Scale(n,move));
                    next=Tracker_Distance(field,candidate,stats);
                }
                if(!isfinite(next)||fabsf(next)>=fabsf(f)*.85f) {
                    Vector3 g=Tracker_Gradient(field,p,h,stats);
                    float g2=Vec3_Dot(g,g);
                    if(g2>1e-5f && isfinite(g2)) {
                        Vector3 move=Vec3_Scale(g,-f/(g2+1e-6f));
                        float length=Vec3_Length(move);
                        if(length>stepLimit)move=Vec3_Scale(move,stepLimit/length);
                        candidate=Vec3_Add(p,move);
                        next=Tracker_Distance(field,candidate,stats);
                    }
                }
                if(!isfinite(next)||fabsf(next)>=fabsf(f)) {
                    stats->rejectedProjections++;break;
                }
                p=candidate;f=next;
            }
        }
        t->normalDirty[i]=Vec3_Distance(predicted,p)>fmaxf(edge*5.0f,.03f);
        vertex->position=p;
        t->corrections[i]=Vec3_Sub(p,t->rawPositions[i]);
        float error=fabsf(f);t->residuals[i]=error;job->sum[threadIndex]+=error;
        if(error>stats->maximumResidual) {
            stats->maximumResidual=error;
            stats->maximumResidualVertex=i;
        }
    }
    if(job->sdf) {
        MonsterSDF_GetThreadStats(&stats->connectorCandidates,&stats->connectorExact,
            &stats->connectorPruned);
        MonsterSDF_EnableThreadStats(false);
    }
}
void MorphSurfaceTracker_AuditQuality(MorphSurfaceTracker* t,const Mesh* mesh) {
    if(!t||!mesh||mesh->vertexCount!=t->vertexCount||
       mesh->indexCount/3!=t->triangleCount||!t->problemTriangles)return;
    double begin=Tracker_NowMs();
    t->stats.thinTriangles=0;t->stats.opposedTriangles=0;
    t->stats.qualityBricks=0;
    for(size_t brick=0;brick<t->brickCount;++brick)
        t->bricks[brick].qualityProblem=false;
    for(size_t triangle=0;triangle+2<mesh->indexCount;triangle+=3) {
        MeshIndex ia=mesh->indices[triangle],ib=mesh->indices[triangle+1],
            ic=mesh->indices[triangle+2];
        Vector3 a=mesh->vertices[ia].position,b=mesh->vertices[ib].position,
            c=mesh->vertices[ic].position;
        Vector3 ab=Vec3_Sub(b,a),bc=Vec3_Sub(c,b),ca=Vec3_Sub(a,c);
        Vector3 area=Vec3_Cross(ab,Vec3_Sub(c,a));
        float longest=fmaxf(Vec3_Dot(ab,ab),fmaxf(Vec3_Dot(bc,bc),Vec3_Dot(ca,ca)));
        bool thin=longest>1e-12f && Vec3_Length(area)/longest<.02f;
        Vector3 normal=Vec3_Add(mesh->vertices[ia].normal,
            Vec3_Add(mesh->vertices[ib].normal,mesh->vertices[ic].normal));
        bool opposed=Vec3_Dot(area,normal)<-1e-8f;
        t->stats.thinTriangles+=thin;
        t->stats.opposedTriangles+=opposed;
        bool bad=opposed||(thin&&!t->referenceThinTriangles[triangle/3]);
        t->problemTriangles[triangle/3]=(uint8_t)bad;
        if(bad&&t->brickCount) {
            t->bricks[t->vertexBrick[ia]].qualityProblem=true;
            t->bricks[t->vertexBrick[ib]].qualityProblem=true;
            t->bricks[t->vertexBrick[ic]].qualityProblem=true;
        }
    }
    for(size_t brick=0;brick<t->brickCount;++brick)
        t->stats.qualityBricks+=t->bricks[brick].qualityProblem;
    t->stats.qualityScanMs=(float)(Tracker_NowMs()-begin);
    MeshValidationResult validation=Mesh_Validate(mesh);
    t->stats.valid=validation.valid;t->stats.watertight=validation.watertight;
}
bool MorphSurfaceTracker_Correct(MorphSurfaceTracker* t,const SDFField* field,Mesh* mesh,float age) {
    if(!t||!field||!field->evaluate||!mesh||mesh->vertexCount!=t->vertexCount||
        t->vertexCount>INT32_MAX)return false;
    double begin=Tracker_NowMs();
    float deformationMs=t->stats.deformationMs;
    const MonsterSDF* sdf=field->evaluateDistance==MonsterSDF_EvaluateDistanceWrapper?
        (const MonsterSDF*)field->context:NULL;
    MorphSurfaceTrackerStats brickStats={0};
    if(!Tracker_BuildBricks(t,mesh,sdf,&brickStats)||t->brickCount>INT32_MAX)return false;
    TrackerCorrectionJob job={.tracker=t,.field=field,.mesh=mesh,.sdf=sdf};
    if(t->pool)SDFSamplingPool_ParallelFor(t->pool,(int)t->vertexCount,Tracker_CorrectRange,&job);
    else Tracker_CorrectRange(&job,0,(int)t->vertexCount,0);
    memset(&t->stats,0,sizeof(t->stats));t->stats.deformationMs=deformationMs;
    t->stats.brickUpdateMs=brickStats.brickUpdateMs;
    t->stats.candidateCompileMs=brickStats.candidateCompileMs;
    t->stats.processedBricks=brickStats.processedBricks;
    double sum=0;
    for(int i=0;i<SDF_SAMPLING_POOL_MAX_THREADS;++i) {
        if(job.failed[i])return false;
        sum+=job.sum[i];
        t->stats.distanceEvaluations+=job.partial[i].distanceEvaluations;
        t->stats.gradientEvaluations+=job.partial[i].gradientEvaluations;
        t->stats.projectedVertices+=job.partial[i].projectedVertices;
        t->stats.rejectedProjections+=job.partial[i].rejectedProjections;
        t->stats.connectorCandidates+=job.partial[i].connectorCandidates;
        t->stats.connectorExact+=job.partial[i].connectorExact;
        t->stats.connectorPruned+=job.partial[i].connectorPruned;
        if(job.partial[i].maximumResidual>t->stats.maximumResidual) {
            t->stats.maximumResidual=job.partial[i].maximumResidual;
            t->stats.maximumResidualVertex=job.partial[i].maximumResidualVertex;
        }
    }
    double normalStart=Tracker_NowMs();
    for(size_t i=0;i<t->vertexCount;++i)if(t->normalDirty[i]) {
        Vector3 sumNormal=Vec3_Create(0,0,0);
        for(size_t incident=t->triangleOffsets[i];incident<t->triangleOffsets[i+1];++incident) {
            size_t triangle=t->incidentTriangles[incident];
            Vector3 a=mesh->vertices[mesh->indices[triangle]].position;
            Vector3 b=mesh->vertices[mesh->indices[triangle+1]].position;
            Vector3 c=mesh->vertices[mesh->indices[triangle+2]].position;
            sumNormal=Vec3_Add(sumNormal,Vec3_Cross(Vec3_Sub(b,a),Vec3_Sub(c,a)));
        }
        float squared=Vec3_Dot(sumNormal,sumNormal);
        if(squared>1e-12f && isfinite(squared)) {
            if(Vec3_Dot(sumNormal,mesh->vertices[i].normal)<0)
                sumNormal=Vec3_Scale(sumNormal,-1);
            mesh->vertices[i].normal=Vec3_Scale(sumNormal,1.0f/sqrtf(squared));
            t->stats.correctedNormals++;
        }
    }
    t->stats.normalCorrectionMs=(float)(Tracker_NowMs()-normalStart);
    for(size_t i=0;i<t->vertexCount;++i)
        t->normalCorrections[i]=Vec3_Sub(mesh->vertices[i].normal,t->rawNormals[i]);
    MorphSurfaceTracker_AuditQuality(t,mesh);
    Mesh_MarkGeometryChanged(mesh);
    sum=0;t->stats.maximumResidual=0;
    for(size_t i=0;i<t->vertexCount;++i) {
        sum+=t->residuals[i];
        if(t->residuals[i]>t->stats.maximumResidual) {
            t->stats.maximumResidual=t->residuals[i];
            t->stats.maximumResidualVertex=i;
        }
    }
    t->stats.meanResidual=(float)(sum/t->vertexCount);
    t->stats.p95Residual=Tracker_QuickSelect(t->residuals,t->vertexCount,(size_t)(.95*t->vertexCount));
    t->stats.projectionMs=(float)(Tracker_NowMs()-begin);
    t->correctedAge=age;
    return true;
}
