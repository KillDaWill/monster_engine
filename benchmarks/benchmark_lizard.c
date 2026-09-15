/** @file benchmark_lizard.c
 * @brief Benchmark reproducible del preset compartido, tiers reales y detalle anatómico local.
 */
#include "MonsterVisualAsync.h"
#include "MonsterAger.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
static double now_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6;}
int main(int argc,char**argv) {
    int repetitions=argc>1?atoi(argv[1]):3;if(repetitions<1)repetitions=1;
    Monster a=Monster_Create(),b=Monster_Create();LizardPhenotype pa=LizardPreset_Juvenile(),pb=LizardPreset_Adult();
    if(!Lizard_BuildMonster(&a,&pa)||!Lizard_BuildMonster(&b,&pb))return 1;
    Monster_SetHeadOpenFactor(&a,.1f);Monster_SetHeadOpenFactor(&b,.1f);
    MonsterAger ag=MonsterAger_Create(&a,&b,0);
    puts("age,tier,iteration,total_ms,sdf_ms,body_mesh_ms,head_mesh_ms,surface_map_ms,mouth_ms,map_candidates_per_vertex,map_tests,map_bruteforce,cells,active,refined,distance_samples,attribute_samples,triangles,min_step,max_step,detail_ratio,budget_adjusted,detail_degraded,boundary_edges,nostril_vertices,orbit_vertices,connector_candidates,connector_exact,connector_pruned");
    const float ages[]={0,.10f,.25f,.50f,.75f,.90f,1};
    for(unsigned i=0;i<7;++i) {
        MonsterAger_SetPerc(&ag,ages[i]);
        for(int rep=0;rep<repetitions;++rep) {
            MonsterVisualAsync* v=MonsterVisualAsync_Create(MonsterVisualAsync_DefaultConfig());if(!v)return 1;
            for(int q=0;q<3;++q) {
                MonsterVisualAsync_SetMorphMode(v,q==1);
                MonsterVisualAsync_Update(v,&ag.result,q==2?1:0);MonsterVisualAsync_Flush(v);
                MonsterVisualAsyncStats async=MonsterVisualAsync_GetStats(v);SDFMesherStats s=async.bodyMesher;
                const Mesh* mesh=MonsterVisualAsync_GetDisplayMesh(v);MeshValidationResult validation=Mesh_Validate(mesh);
                size_t nose=0,orbit=0;for(size_t j=0;j<mesh->vertexCount;++j){nose+=mesh->vertices[j].material==SDF_MATERIAL_NOSTRIL;orbit+=mesh->vertices[j].material==SDF_MATERIAL_EYE_SOCKET;}
                printf("%.2f,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.4f,%d,%d,%zu,%zu,%zu,%zu,%zu,%zu\n",
                    ages[i],q==2?"settled":q==1?"morph":"interactive",rep,async.lastBuildDurationMs,
                    async.sdfBuildMs,async.bodyMeshMs,async.headMeshMs,async.surfaceMappingMs,async.mouthBuildMs,
                    async.surfaceMapper.averageCandidatesPerVertex,async.surfaceMapper.candidateTests,async.surfaceMapper.bruteForceTests,
                    s.cellCount,s.activeCellCount,s.refinedCellCount,
                    s.distanceEvaluationCount,s.fullSampleEvaluationCount,s.generatedTriangleCount,s.minimumVoxelSize,s.effectiveVoxelSize,
                    s.detailSpacingRatio,s.cellBudgetAdjusted,s.detailBudgetAdjusted,validation.boundaryEdgeCount,nose,orbit,s.connectorCandidateCount,s.connectorExactEvaluationCount,s.connectorPrunedCount);
                fflush(stdout);if(!validation.valid||!validation.watertight||nose==0||orbit==0)return 2;
            }
            MonsterVisualAsync_Free(v);
        }
    }
    MonsterSDF sdf=MonsterSDF_Create();if(!MonsterSDF_Build(&sdf,&b,MonsterSDF_DefaultConfig()))return 1;
    MonsterSDFHeadField context;SDFField field=MonsterSDF_GetHeadField(&sdf,0,&context);
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];size_t count=MonsterSDF_GetDetailRegions(&sdf,6,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
    SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.voxelSize=.06f;cfg.maxResolution=384;cfg.maxCells=800000;
    SDFMesher mesher=SDFMesher_Create(cfg);Mesh mesh=Mesh_Create();double start=now_ms();
    if(!SDFMesher_GenerateMeshDetailed(&mesher,&field,regions,count,&mesh))return 1;
    fprintf(stderr,"Cabeza local: %.2f ms, %zu celdas, %zu muestras, %zu triángulos; scratch %.2f MiB\n",
        now_ms()-start,mesher.lastStats.cellCount,mesher.lastStats.distanceEvaluationCount,mesh.indexCount/3,
        (mesher.cornerVertexCapacity*sizeof(MeshIndex)+mesher.gridDistanceCapacity*sizeof(float)+mesher.gridGradientCapacity*sizeof(Vector3)+mesher.gradientStampCapacity*sizeof(uint32_t)+
         (mesher.xEdgeCapacity+mesher.yEdgeCapacity+mesher.zEdgeCapacity)*sizeof(MeshIndex))/1048576.0);
    double oldError=0,newError=0;start=now_ms();
    for(size_t i=0;i<mesh.vertexCount;++i) {
        Vector3 p=mesh.vertices[i].position;float d=field.evaluateDistance(field.context,p);
        Vector3 normal=SDF_EstimateNormal(field.evaluate,field.context,p,.002f);
        Vector3 projected=Vec3_Sub(p,Vec3_Scale(normal,d));
        oldError+=fabsf(d);newError+=fabsf(field.evaluateDistance(field.context,projected));
    }
    fprintf(stderr,"Ensayo de proyección (sin modificar la malla): %.2f ms, residuo medio %.6f -> %.6f\n",
        now_ms()-start,oldError/mesh.vertexCount,newError/mesh.vertexCount);
    Mesh_Free(&mesh);SDFMesher_Free(&mesher);MonsterSDF_Free(&sdf);MonsterAger_Free(&ag);Monster_Free(&a);Monster_Free(&b);
    return 0;
}
