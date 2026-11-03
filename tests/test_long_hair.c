/** @file test_long_hair.c
 * @brief Identidad, marcos de reposo, curvas y compatibilidad de pelo largo. */
#include "LongHair.h"
#include "Surface.h"
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "MonsterVisual.h"
#include "MonsterAnimation.h"
#include "MonsterAger.h"
#include "MonsterVisualAsync.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static AnatomyGraph Graph(void){AnatomyGraph g={0};for(unsigned i=0;i<3;++i){AnatomyNode n={.id=Anatomy_MakeId(1,i+1),.moduleInstanceId=1,.localNodeId=i+1,
    .center={0,0,(float)i},.widthRadius=.4f,.heightRadius=.3f,.envelopeRadii={.4f,.3f,.5f},.development=1,.region=ANATOMY_REGION_NECK};
    TEST_ASSERT(AnatomyGraph_AddNode(&g,n),"estación de pelo");}return g;}
void run_long_hair_tests(void){
    AnatomyGraph graph=Graph();LongHairGroup g=LongHairGroup_Default();g.domain.hostModuleId=1;g.domain.anchorCount=3;
    for(unsigned i=0;i<3;++i)g.domain.anchors[i]=Anatomy_MakeId(1,i+1);
    LongHairSet a={0},b={0};TEST_ASSERT(LongHairSet_Build(&a,&g,1,&graph,1,NULL,NULL),"guías construidas");
    TEST_ASSERT(LongHairSet_Build(&b,&g,1,&graph,1,NULL,NULL),"regeneración sin malla");TEST_ASSERT(a.count==g.guideCount,"cantidad acotada");
    for(size_t i=0;i<a.count;++i){TEST_ASSERT(a.guides[i].root.stableRootId==b.guides[i].root.stableRootId,"identidad reproducible");
        TEST_ASSERT(a.guides[i].root.moduleId==1,"propiedad anatómica");
        TEST_ASSERT(Vec3_Distance(a.guides[i].points[0],a.guides[i].root.restPosition)<1e-6f,"raíz anclada");
        for(unsigned j=1;j<LONG_HAIR_POINTS;++j)TEST_ASSERT(fabsf(Vec3_Distance(a.guides[i].points[j],a.guides[i].points[j-1])-a.guides[i].length/(LONG_HAIR_POINTS-1))<1e-5f,"segmentos de longitud fija");
        for(unsigned j=0;j<100;++j){Vector3 p=LongHairGuide_Evaluate(&a.guides[i],j/99.f);TEST_ASSERT(isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z),"curva finita");}
    }
    /* Cambiar orden o triangulación no participa en la identidad ni el muestreo. */
    AnatomyNode swap=graph.nodes[0];graph.nodes[0]=graph.nodes[2];graph.nodes[2]=swap;
    TEST_ASSERT(LongHairSet_Build(&b,&g,1,&graph,1,NULL,NULL),"orden independiente");
    for(size_t i=0;i<a.count;++i)TEST_ASSERT(Vec3_Distance(a.guides[i].points[0],b.guides[i].points[0])<1e-6f,"raíz estable tras reordenar");
    LongHairGroup semantic=g;semantic.domain.kind=LONG_HAIR_REGION;semantic.domain.region=ANATOMY_REGION_NECK;
    TEST_ASSERT(LongHairSet_Build(&b,&semantic,1,&graph,1,NULL,NULL),"dominio semántico");
    for(size_t i=0;i<a.count;++i)TEST_ASSERT(Vec3_Distance(a.guides[i].points[0],b.guides[i].points[0])<1e-6f,"selección semántica ordenada por identidad");
    LongHairGroup patch=g;patch.domain.kind=LONG_HAIR_PATCH;patch.domain.anchorCount=1;
    TEST_ASSERT(LongHairSet_Build(&b,&patch,1,&graph,1,NULL,NULL)&&b.count==patch.guideCount,"parche anatómico sin índices de triángulos");
    Skeleton skeleton={.jointCount=3};for(unsigned i=0;i<3;++i)skeleton.joints[i]=(SkeletonJoint){.id=Anatomy_MakeId(1,i+1),.anatomyId=Anatomy_MakeId(1,i+1),.parentIndex=(int)i-1,.restPosition={0,0,i?1:0},.restRotation={0,0,0,1}};
    SkeletonPose pose;TEST_ASSERT(SkeletonPose_Init(&pose,&skeleton),"pose de raíces");pose.joints[0].localPosition.x=.4f;
    TEST_ASSERT(SkeletonPose_UpdateWorldTransforms(&pose,&skeleton)&&LongHairSet_Pose(&a,&skeleton,&pose),"transporte por pose");
    for(size_t i=0;i<a.count;++i)TEST_ASSERT(fabsf(a.guides[i].points[0].x-a.guides[i].restPoints[0].x-.4f)<1e-5f,"sin raíz suspendida");
    float (*packed)[4]=calloc(a.count*LONG_HAIR_GPU_ROWS,sizeof(*packed));TEST_ASSERT(packed,"buffer de prueba");
    TEST_ASSERT(LongHairSet_Pack(&a,packed,a.count*LONG_HAIR_GPU_ROWS)==a.count*LONG_HAIR_GPU_ROWS,"transporte acotado");
    for(size_t i=0;i<a.count*LONG_HAIR_GPU_ROWS;++i)for(unsigned j=0;j<4;++j)TEST_ASSERT(isfinite(packed[i][j]),"GPU sin NaN");
    free(packed);
    g.domain.anchors[1]=99999;TEST_ASSERT(LongHairSet_Build(&b,&g,1,&graph,1,NULL,NULL)&&b.count==0,"anfitrión retirado sin raíz huérfana");
    LongHairSet_Free(&a);LongHairSet_Free(&b);
    SurfacePhenotype x=SurfacePhenotype_Default(),y=x;x.longHairCount=1;x.longHair[0]=LongHairGroup_Default();
    for(unsigned i=0;i<=20;++i){SurfacePhenotype p=SurfacePhenotype_Interpolate(&x,&y,i/20.f);TEST_ASSERT(p.longHairCount<=1,"retirada segura en morph");}
    y=x;y.longHair[0].guideCount=24;SurfacePhenotype middle=SurfacePhenotype_Interpolate(&x,&y,.5f);
    TEST_ASSERT(middle.longHairCount==1&&middle.longHair[0].density==0,"cambio discreto sin salto opaco");
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));TEST_ASSERT(doc&&m,"memoria de integración");char error[256];
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,"recipes/caballo.creature.json",error,sizeof(error)),"receta v1 anterior");
    CreaturePhenotype phenotype=doc->recipe.adult;phenotype.surface.longHairCount=1;g=LongHairGroup_Default();g.domain.hostModuleId=1;
    g.domain.anchorCount=2;g.domain.anchors[0]=Anatomy_MakeId(1,1);g.domain.anchors[1]=Anatomy_MakeId(1,2);phenotype.surface.longHair[0]=g;
    doc->recipe.adult=phenotype;FILE* file=tmpfile();TEST_ASSERT(file&&CreatureRecipeIO_WriteJSON(file,&doc->recipe),"JSON con guías");
    long bytes=ftell(file);TEST_ASSERT(bytes>0,"JSON escrito");rewind(file);char* json=calloc((size_t)bytes+1,1);TEST_ASSERT(json,"JSON de prueba");
    TEST_ASSERT(fread(json,1,(size_t)bytes,file)==(size_t)bytes,"lectura completa");fclose(file);
    CreatureRecipeDocument* restored=calloc(1,sizeof(*restored));TEST_ASSERT(restored,"documento de prueba");
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(restored,json,error,sizeof(error)),"roundtrip v1 con dominio anatómico");
    TEST_ASSERT(LongHairGroups_Fingerprint(restored->recipe.adult.surface.longHair,1)==LongHairGroups_Fingerprint(phenotype.surface.longHair,1),"controles sin pérdida");
    free(restored);free(json);
    *m=Monster_Create();TEST_ASSERT(Creature_BuildMonster(m,&doc->recipe,&phenotype),"receta a anatomía con pelo");
    TEST_ASSERT(MonsterVisual_UpdateLongHair(&a,m)&&a.count==g.guideCount,"pipeline anatómico real");
    for(size_t i=0;i<a.count;++i)TEST_ASSERT(a.guides[i].visible,"raíces proyectadas sobre cuello real");
    uint64_t generation=a.generation;TEST_ASSERT(MonsterVisual_UpdateLongHair(&a,m)&&a.generation==generation,"pose sin regenerar raíces");
    for(unsigned frame=0;frame<20;++frame){MonsterAnimation_Update(m,.016f);TEST_ASSERT(MonsterVisual_UpdateLongHair(&a,m),"animación sin remallado");}
    Monster* young=malloc(sizeof(*young));MonsterAger* ager=malloc(sizeof(*ager));TEST_ASSERT(young&&ager,"crecimiento de guías");
    CreaturePhenotype juvenile=doc->recipe.juvenile;juvenile.surface.longHairCount=1;juvenile.surface.longHair[0]=g;
    *young=Monster_Create();TEST_ASSERT(Creature_BuildMonster(young,&doc->recipe,&juvenile),"etapa joven con guías");
    *ager=MonsterAger_Create(young,m,0);MonsterAger_SetGeometrySnapshotEnabled(ager,false);
    uint32_t stable=a.guides[0].root.stableRootId;
    for(unsigned cycle=0;cycle<3;++cycle)for(unsigned step=0;step<=10;++step){float t=(cycle&1)?1-step/10.f:step/10.f;
        MonsterAger_SetPerc(ager,t);const Monster* current=MonsterAger_GetResultConst(ager);
        TEST_ASSERT(MonsterVisual_UpdateLongHair(&a,current)&&a.count==g.guideCount,"morph y crecimiento repetidos");
        TEST_ASSERT(a.guides[0].root.stableRootId==stable,"identidad después de regeneración anatómica");
        for(size_t i=0;i<a.count;++i)TEST_ASSERT(isfinite(a.guides[i].points[9].x)&&a.guides[i].visible,"crecimiento finito y anclado");}
    MonsterVisualAsyncConfig config=MonsterVisualAsync_DefaultConfig();config.workerDelayMs=80;config.interactiveMesherConfig.voxelSize=.2f;
    MonsterVisualAsync* async=MonsterVisualAsync_Create(config);TEST_ASSERT(async,"runtime asíncrono");
    MonsterVisualAsync_Update(async,m,.016f);m->surface.longHair[0].rootColor.r=133;
    MonsterVisualAsync_Update(async,m,.016f);TEST_ASSERT(async->longHair.groups[0].rootColor.r==133,"apariencia actual, no snapshot worker");
    MonsterVisualAsync_Free(async);
    MonsterAger_Free(ager);free(ager);Monster_Free(young);free(young);
    LongHairSet_Free(&a);Monster_Free(m);free(m);free(doc);
}
