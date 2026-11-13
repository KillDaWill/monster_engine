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
static float FloorDistance(const void* ctx,Vector3 p){return p.y-*(const float*)ctx;}
static float NeckDistance(const void* ctx,Vector3 p){(void)ctx;return sqrtf(p.x*p.x+p.y*p.y)-.3f;}
static void FollicleTests(void){
    AnatomyGraph graph=Graph();LongHairGroup g=LongHairGroup_Default();
    g.domain.hostModuleId=1;g.domain.anchorCount=3;g.domain.angularSpread=.8f;
    for(unsigned i=0;i<3;++i)g.domain.anchors[i]=Anatomy_MakeId(1,i+1);
    g.guideCount=32;g.childrenPerGuide=16;g.rootFullness=1;
    LongHairSet a={0},b={0};
    TEST_ASSERT(LongHairSet_Build(&a,&g,1,&graph,1,NeckDistance,NULL),"folículos sobre cuello curvo");
    TEST_ASSERT(LongHairSet_Build(&b,&g,1,&graph,1,NeckDistance,NULL),"folículos reproducibles");
    size_t distributed=0;
    for(size_t i=0;i<a.count;++i){const LongHairGuide* v=&a.guides[i];
        TEST_ASSERT(!memcmp(v->follicles,b.guides[i].follicles,sizeof(v->follicles)),"folículos estables al reconstruir");
        for(unsigned c=0;c<g.childrenPerGuide;++c){const float* f=v->follicles[c];
            TEST_ASSERT(f[3]==1&&f[0]>=0&&f[0]<a.count&&f[1]>0&&f[1]<1,"vecino y peso válidos");
            const LongHairGuide* neighbor=&a.guides[(size_t)f[0]];
            Vector3 normal=Vec3_Normalize(Vec3_Lerp(v->posedNormal,neighbor->posedNormal,f[1]));
            Vector3 root=Vec3_Add(Vec3_Lerp(v->points[0],neighbor->points[0],f[1]),Vec3_Scale(normal,f[2]));
            TEST_ASSERT(fabsf(NeckDistance(NULL,root)-.002f)<.0001f,"folículo proyectado a piel curva, no a una cuerda interior");
            for(unsigned q=1;q<=144;++q){float t=q/144.f;
                Vector3 point=Vec3_Add(Vec3_Lerp(LongHairGuide_Evaluate(v,t),LongHairGuide_Evaluate(neighbor,t),f[1]),Vec3_Scale(normal,f[2]));
                TEST_ASSERT(NeckDistance(NULL,point)>-.0001f,"fibra interpolada no atraviesa el cuello entre muestras");}
            distributed+=Vec3_Distance(root,v->points[0])>.0001f;
        }}
    TEST_ASSERT(distributed==a.count*g.childrenPerGuide,"todas las fibras tienen inserción independiente");
    LongHairSet_Free(&a);LongHairSet_Free(&b);
}
static void MultiBandTests(void){
    AnatomyGraph graph=Graph();LongHairGroup groups[3];
    for(unsigned i=0;i<3;++i){groups[i]=LongHairGroup_Default();groups[i].id=i+1;groups[i].seed=17+i;
        groups[i].guideCount=12;groups[i].domain.hostModuleId=1;groups[i].domain.anchorCount=3;
        for(unsigned j=0;j<3;++j)groups[i].domain.anchors[j]=Anatomy_MakeId(1,j+1);
        groups[i].domain.angle=((float)i-1)*.6f;groups[i].domain.angularSpread=0;}
    LongHairSet set={0};TEST_ASSERT(LongHairSet_Build(&set,groups,3,&graph,1,NULL,NULL),"bandas anatómicas múltiples");
    TEST_ASSERT(set.count==36&&set.groupCount==3,"todas las bandas generan guías");
    for(unsigned k=0;k<3;++k)for(unsigned i=0;i<12;++i){LongHairGuide* guide=&set.guides[k*12+i];
        TEST_ASSERT(guide->groupIndex==k&&guide->root.groupId==groups[k].id,"identidad de banda conservada");
        TEST_ASSERT(guide->root.rootParameter>=0&&guide->root.rootParameter<=1,"raíces cubren el dominio");}
    TEST_ASSERT(set.guides[0].root.restPosition.x*set.guides[24].root.restPosition.x<0,"bandas en ambos lados del eje");
    TEST_ASSERT(set.guides[12].root.restPosition.y>set.guides[0].root.restPosition.y,"banda central en la cresta");
    LongHairSet_Free(&set);
}
static void CollisionTests(void){
    AnatomyGraph graph=Graph();LongHairGroup g=LongHairGroup_Default();g.domain.hostModuleId=1;g.domain.anchorCount=1;g.domain.anchors[0]=Anatomy_MakeId(1,1);
    g.direction=Vec3_Create(1,-1,0);g.rootSpread=.04f;LongHairSet set={0};float floor=.3f;
    TEST_ASSERT(LongHairSet_Build(&set,&g,1,&graph,1,FloorDistance,&floor),"campo de contacto reutilizable");
    bool openedNearRoot=false;
    for(size_t i=0;i<set.count;++i){LongHairGuide* v=&set.guides[i];
        TEST_ASSERT(Vec3_Distance(v->points[0],v->root.restPosition)<1e-6f,"contacto no desplaza raíz");
        for(unsigned p=1;p<=90;++p){float t=p/90.f;Vector3 point=LongHairGuide_Evaluate(v,t);
            TEST_ASSERT(FloorDistance(&floor,point)>-.002f,"curva suave no atraviesa plano");
            if(t>.12f)TEST_ASSERT(FloorDistance(&floor,point)>=LongHairGroup_Envelope(&g,1,t)-.006f,"holgura incluye hijos y grosor");}
        for(unsigned p=1;p<LONG_HAIR_POINTS;++p)
            TEST_ASSERT(fabsf(Vec3_Distance(v->points[p],v->points[p-1])-v->length/9)<.02f,"contacto conserva longitud acotada");
        for(unsigned p=0;p<1;++p){Vector3 a=v->points[p?p-1:0],b=v->points[p],c=v->points[p+1],d=v->points[p+2<LONG_HAIR_POINTS?p+2:LONG_HAIR_POINTS-1];
            Vector3 d0=Vec3_Scale(Vec3_Sub(c,a),.5f),d2=Vec3_Scale(Vec3_Sub(d,b),.5f),d1=Vec3_Sub(Vec3_Scale(Vec3_Sub(c,b),3),Vec3_Add(d0,d2));
            float speed=fmaxf(Vec3_Length(d0),fmaxf(Vec3_Length(d1),Vec3_Length(d2)));
            for(unsigned q=0;q<=40;++q){float u=q/40.f,t=(p+u)/9.f;
                float safe=fmaxf(0,v->rootClearance-speed*(1-u))*(1-u);
                float actual=FloorDistance(&floor,LongHairGuide_Evaluate(v,t))-.003f-g.width*1.5f*(1+g.rootFullness);
                TEST_ASSERT(safe<=fmaxf(0,actual)+1e-5f,"radio local conservador contra plano");
                if(p==0&&u<.8f&&safe>.0001f)openedNearRoot=true;}}
    }
    TEST_ASSERT(openedNearRoot,"hijos pueden abrirse dentro del primer tramo");
    LongHairSet_Free(&set);
    /* Obstáculo ajeno que se acerca después del peinado: equivale a pata o mandíbula
     * móvil y no puede quedar desactivado por la calibración de la raíz. */
    AnatomyNode obstacle={.id=Anatomy_MakeId(2,1),.moduleInstanceId=2,.localNodeId=1,.center={2,.5f,0},
        .envelopeRadii={.18f,.18f,.18f},.widthRadius=.18f,.heightRadius=.18f,.development=1};
    TEST_ASSERT(AnatomyGraph_AddNode(&graph,obstacle),"obstáculo animable");
    g.guideCount=4;g.direction=Vec3_Create(1,0,0);g.gravityInfluence=0;g.stiffness=1;
    TEST_ASSERT(LongHairSet_Build(&set,&g,1,&graph,1,FloorDistance,&floor),"groom anterior al contacto móvil");
    Skeleton skeleton={.jointCount=4};
    for(unsigned i=0;i<4;++i)skeleton.joints[i]=(SkeletonJoint){.id=graph.nodes[i].id,.anatomyId=graph.nodes[i].id,.parentIndex=-1,
        .restPosition=graph.nodes[i].center,.restRotation={0,0,0,1}};
    SkeletonPose pose;TEST_ASSERT(SkeletonPose_Init(&pose,&skeleton),"pose independiente de obstáculos");pose.joints[3].localPosition.x=.45f;
    TEST_ASSERT(SkeletonPose_UpdateWorldTransforms(&pose,&skeleton)&&LongHairSet_Pose(&set,&skeleton,&pose),"contacto tras animar");
    for(size_t i=0;i<set.count;++i){LongHairGuide saved=set.guides[i];
        TEST_ASSERT(Vec3_Distance(saved.points[0],saved.root.restPosition)<1e-6f,"obstáculo no desancla raíz");
        for(unsigned p=1;p<=90;++p)TEST_ASSERT(Vec3_Distance(LongHairGuide_Evaluate(&saved,p/90.f),pose.joints[3].worldPosition)>.176f,"segmentos no atraviesan pata móvil");
        TEST_ASSERT(LongHairSet_Pose(&set,&skeleton,&pose),"repetición estable de pose");
        TEST_ASSERT(!memcmp(saved.points,set.guides[i].points,sizeof(saved.points)),"sin deriva ni jitter en pose fija");
        TEST_ASSERT(fabsf(Vec3_Length(saved.posedSide)-1)<.001f&&fabsf(Vec3_Dot(saved.posedSide,saved.posedNormal))<.001f,"marco estable y ortogonal tras contacto");
        for(unsigned p=0;p<LONG_HAIR_POINTS;++p)TEST_ASSERT(isfinite(saved.clearance[p])&&saved.clearance[p]>=0,"certificado de hijos finito");
    }
    set.guides[0].attachmentJoint=skeleton.joints[3].id;
    Vector3 expected=Vec3_Add(set.guides[0].root.restPosition,Vec3_Sub(pose.joints[3].worldPosition,skeleton.joints[3].restPosition));
    TEST_ASSERT(LongHairSet_Pose(&set,&skeleton,&pose)&&Vec3_Distance(set.guides[0].points[0],expected)<1e-6f,"anclaje auxiliar sigue mandíbula sin cambiar identidad anatómica");
    uint32_t identity=set.guides[0].root.stableRootId;set.attachedDistance=FloorDistance;set.attachedContext=&floor;
    floor=.31f;TEST_ASSERT(LongHairSet_Pose(&set,&skeleton,&pose)&&!set.guides[0].visible,"inserción cubierta no emite fibras a través del cuerpo");
    floor=.3f;TEST_ASSERT(LongHairSet_Pose(&set,&skeleton,&pose)&&set.guides[0].visible&&set.guides[0].root.stableRootId==identity,"inserción recupera cobertura sin perder identidad");
    LongHairSet_Free(&set);
}
void run_long_hair_tests(void){
    FollicleTests();
    MultiBandTests();
    CollisionTests();
    AnatomyGraph graph=Graph();LongHairGroup g=LongHairGroup_Default();g.domain.hostModuleId=1;g.domain.anchorCount=3;
    for(unsigned i=0;i<3;++i)g.domain.anchors[i]=Anatomy_MakeId(1,i+1);
    LongHairSet a={0},b={0};TEST_ASSERT(LongHairSet_Build(&a,&g,1,&graph,1,NULL,NULL),"guías construidas");
    TEST_ASSERT(LongHairSet_Build(&b,&g,1,&graph,1,NULL,NULL),"regeneración sin malla");TEST_ASSERT(a.count==g.guideCount,"cantidad acotada");
    LongHairGroup styled=g;styled.bend=1;styled.wave=.8f;styled.rootFullness=.8f;styled.transparency=.4f;
    TEST_ASSERT(LongHairGroups_Fingerprint(&g,1)!=LongHairGroups_Fingerprint(&styled,1),"huella incluye peinado y transparencia");
    TEST_ASSERT(LongHairGroup_Envelope(&styled,1,.05f)>LongHairGroup_Envelope(&g,1,.05f),"holgura para raíz frondosa");
    TEST_ASSERT(LongHairSet_Build(&b,&styled,1,&graph,1,NULL,NULL),"peinado controlable");
    TEST_ASSERT(Vec3_Distance(a.guides[0].points[9],b.guides[0].points[9])>.01f,"curvatura modifica la forma");
    TEST_ASSERT(a.guides[0].root.stableRootId==b.guides[0].root.stableRootId&&Vec3_Distance(a.guides[0].points[0],b.guides[0].points[0])<1e-6f,"peinado conserva raíces");
    float styledRows[LONG_HAIR_MAX_GROUPS*256*LONG_HAIR_GPU_ROWS][4];
    TEST_ASSERT(LongHairSet_Pack(&b,styledRows,LONG_HAIR_MAX_GROUPS*256*LONG_HAIR_GPU_ROWS),"controles enviados a GPU");
    TEST_ASSERT(FLOAT_NEAR(styledRows[10][3],.6f)&&FLOAT_NEAR(styledRows[15][2],.8f),"opacidad y frondosidad empaquetadas");
    TEST_ASSERT(LongHairSet_Build(&b,&g,1,&graph,1,NULL,NULL),"restauración del peinado");
    LongHairGroup vertical=g;vertical.stiffness=1;vertical.gravityInfluence=0;vertical.verticalBias=-1;
    TEST_ASSERT(LongHairSet_Build(&b,&vertical,1,&graph,1,NULL,NULL),"caída vertical sin gravedad ni contacto");
    for(size_t i=0;i<b.count;++i){Vector3 delta=Vec3_Sub(b.guides[i].points[9],b.guides[i].points[8]);
        TEST_ASSERT(delta.y<0&&fabsf(delta.x)<1e-6f&&fabsf(delta.z)<1e-6f,"extremo de caída completamente vertical");
        TEST_ASSERT(b.guides[i].root.stableRootId==a.guides[i].root.stableRootId,"caída conserva identidad");}
    vertical.verticalBias=1;TEST_ASSERT(LongHairSet_Build(&b,&vertical,1,&graph,1,NULL,NULL),"peinado erguido");
    for(size_t i=0;i<b.count;++i)for(unsigned p=1;p<LONG_HAIR_POINTS;++p){Vector3 delta=Vec3_Sub(b.guides[i].points[p],b.guides[i].points[p-1]);
        TEST_ASSERT(delta.y>0&&fabsf(delta.x)<1e-6f&&fabsf(delta.z)<1e-6f,"guía erguida completamente vertical");}
    vertical.verticalBias=NAN;LongHairGroup_Normalize(&vertical);TEST_ASSERT(vertical.verticalBias==0,"valor vertical no finito saneado");
    TEST_ASSERT(LongHairSet_Build(&b,&g,1,&graph,1,NULL,NULL),"restauración tras extremos verticales");
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
    y=x;y.longHair[0].rootFullness=1;y.longHair[0].bend=1;y.longHair[0].wave=1;y.longHair[0].transparency=1;y.longHair[0].verticalBias=-1;
    middle=SurfacePhenotype_Interpolate(&x,&y,.5f);
    TEST_ASSERT(FLOAT_NEAR(middle.longHair[0].rootFullness,.5f)&&FLOAT_NEAR(middle.longHair[0].transparency,.5f)&&FLOAT_NEAR(middle.longHair[0].bend,.5f)&&FLOAT_NEAR(middle.longHair[0].wave,.5f),"controles interpolados en morph");
    TEST_ASSERT(FLOAT_NEAR(middle.longHair[0].verticalBias,-.5f),"caída interpolada en morph");
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));TEST_ASSERT(doc&&m,"memoria de integración");char error[256];
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,"docs/validation/long-hair-foundation/recipes/caballo-curve.creature.json",error,sizeof(error)),"receta antigua sin controles optativos");
    TEST_ASSERT(doc->recipe.adult.surface.longHairCount>0&&doc->recipe.adult.surface.longHair[0].verticalBias==0,"controles omitidos conservan valor neutro");
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,"recipes/caballo.creature.json",error,sizeof(error)),"receta v1 anterior");
    CreaturePhenotype phenotype=doc->recipe.adult;phenotype.surface.longHairCount=1;g=LongHairGroup_Default();g.domain.hostModuleId=1;
    g.domain.anchorCount=2;g.domain.anchors[0]=Anatomy_MakeId(1,1);g.domain.anchors[1]=Anatomy_MakeId(1,2);phenotype.surface.longHair[0]=g;
    phenotype.surface.longHair[0].rootFullness=.8f;phenotype.surface.longHair[0].bend=.5f;phenotype.surface.longHair[0].wave=.3f;phenotype.surface.longHair[0].transparency=.4f;phenotype.surface.longHair[0].verticalBias=-.75f;
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

    /* Transporte ligero de guías y construcción asíncrona no bloqueante */
    LongHairSet transported={0};
    TEST_ASSERT(MonsterVisual_UpdateLongHair(&transported,m)&&transported.count==g.guideCount,"base para transporte");
    Vector3 beforeRoot=transported.guides[0].restPoints[0];
    AnatomyGraph moved=m->anatomyGraph;
    for(size_t i=0;i<moved.nodeCount;++i){moved.nodes[i].center.y+=.12f;}
    TEST_ASSERT(LongHairSet_TransportToAnatomyGraph(&transported,&moved,m->phenotype.axial.totalScale),"transporte por estaciones");
    TEST_ASSERT(FLOAT_NEAR(transported.guides[0].restPoints[0].y-beforeRoot.y,.12f),"raíz transportada con la estación");
    LongHairSet_Free(&transported);

    LongHairAsync* hairAsync=LongHairAsync_Create();TEST_ASSERT(hairAsync,"creación LongHairAsync");
    TEST_ASSERT(LongHairAsync_Update(hairAsync,m,true),"envío no bloqueante");
    MonsterAger_SetPerc(ager,.35f);
    TEST_ASSERT(LongHairAsync_Update(hairAsync,MonsterAger_GetResultConst(ager),true),"coalescencia intermedia");
    MonsterAger_SetPerc(ager,.80f);
    const Monster* finalTarget=MonsterAger_GetResultConst(ager);
    TEST_ASSERT(LongHairAsync_Flush(hairAsync,finalTarget),"flush final de pelo asíncrono");
    const LongHairSet* flushed=LongHairAsync_GetDisplay(hairAsync);
    TEST_ASSERT(flushed&&flushed->count==g.guideCount,"resultado final completo tras flush");
    TEST_ASSERT(flushed->groups[0].rootColor.r==finalTarget->surface.longHair[0].rootColor.r,"apariencia reflejada en flush");
    LongHairAsync_Destroy(hairAsync);

    MonsterAger_Free(ager);free(ager);Monster_Free(young);free(young);
    LongHairSet_Free(&a);Monster_Free(m);free(m);free(doc);
}
