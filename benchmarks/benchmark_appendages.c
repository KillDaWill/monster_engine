#include "MonsterVisualAsync.h"
#include "MonsterAger.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>

/** @file benchmark_appendages.c
 * @brief Diagnóstico estático de siete edades, tiers reales y A/B de poda/descarte.
 */
static double ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.+t.tv_nsec/1e6;}
static size_t uncull(const void* ctx,AABB3D* boxes,size_t cap){if(!cap)return 0;boxes[0]=MonsterSDF_GetBounds(ctx);return 1;}
int main(int argc,char** argv){
 int modes=argc>1&&strcmp(argv[1],"--ab")==0?3:1;
 unsigned failures=0;
 float ages[]={0,.1,.25,.5,.75,.9,1};
 puts("age,tier,prune,cull,nodes,inside,visible,ms,cells,refined,min,max,triangles,candidates,exact,pruned,ratio");
 for(int a=0;a<7;a++)for(int tier=0;tier<2;tier++)for(int mode=0;mode<modes;mode++){
 Monster m=Monster_Create(); LizardPhenotype p=LizardPhenotype_Interpolate(NULL,NULL,ages[a]);Lizard_BuildMonster(&m,&p);
 MonsterSDF sdf=MonsterSDF_Create();MonsterSDFConfig sc=MonsterSDF_DefaultConfig();sc.enableConnectorPruning=mode!=1;MonsterSDF_Build(&sdf,&m,sc);
 MonsterVisualAsyncConfig ac=MonsterVisualAsync_DefaultConfig();
 SDFMesherConfig cfg=MonsterVisualAsync_ResolveBodyConfig(&ac,
     tier?MONSTER_VISUAL_QUALITY_SETTLED:MONSTER_VISUAL_QUALITY_MORPH,true);
 cfg.samplingThreadCount=1;
 SDFField field=MonsterSDF_GetField(&sdf);if(mode==2)field.getComponentBounds=uncull;
 SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];size_t n=MonsterSDF_GetDetailRegions(&sdf,tier?6:3.5,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
 SDFMesher mesher=SDFMesher_Create(cfg);Mesh mesh=Mesh_Create();double t=ms();
 if(!SDFMesher_GenerateMeshDetailed(&mesher,&field,regions,n,&mesh))return 1;
 t=ms()-t;
 int inside=0,visible=0;for(size_t i=0;i<m.anatomyGraph.nodeCount;i++){
 const AnatomyNode* node=&m.anatomyGraph.nodes[i];if(node->role!=ANATOMY_ROLE_DIGIT)continue;
 int outgoing=0;for(size_t j=0;j<m.anatomyGraph.connectionCount;j++)outgoing+=m.anatomyGraph.connections[j].fromId==node->id;if(outgoing)continue;
 inside+=MonsterSDF_EvaluateDistance(&sdf,node->center)<0;float near=1e9;
 for(size_t j=0;j<mesh.vertexCount;j++)near=fminf(near,Vec3_Distance(node->center,mesh.vertices[j].position));
 visible+=near<node->widthRadius*1.8f;
 }
 failures+=inside!=20||visible!=20;
 SDFMesherStats s=mesher.lastStats;
 printf("%.2f,%s,%d,%d,%zu,%d,%d,%.2f,%zu,%zu,%.6f,%.6f,%zu,%zu,%zu,%zu,%.3f\n",ages[a],tier?"settled":"morph",mode!=1,mode!=2,m.anatomyGraph.nodeCount,inside,visible,t,s.cellCount,s.refinedCellCount,s.minimumVoxelSize,s.effectiveVoxelSize,s.generatedTriangleCount,s.connectorCandidateCount,s.connectorExactEvaluationCount,s.connectorPrunedCount,s.detailSpacingRatio);fflush(stdout);
 Mesh_Free(&mesh);SDFMesher_Free(&mesher);MonsterSDF_Free(&sdf);Monster_Free(&m);
 }return failures?2:0;}
