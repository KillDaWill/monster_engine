#include "Creature.h"
#include "Limb.h"
/** @file test_local_detail.c
 * @brief Regresiones geométricas del muestreo local conformante y de la alometría.
 */
#include "test_utils.h"
#include "SDFMesher.h"
#include "SDFPrimitives.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "LizardVariations.h"
#include <math.h>
#include <string.h>

typedef struct DetailFixture { Vector3 center; float radius; } DetailFixture;
static float detail_distance(const void* context,Vector3 p) {
    const DetailFixture* f=context;
    return fmaxf(SDF_Sphere(p,.82f),-SDF_Sphere(Vec3_Sub(p,f->center),f->radius));
}
static SDFSample detail_sample(const void* context,Vector3 p) {
    return SDFSample_Create(detail_distance(context,p),COLOR_WHITE,SDF_MATERIAL_SKIN);
}
static size_t cavity_vertices(const Mesh* mesh,const DetailFixture* f) {
    size_t count=0;
    for(size_t i=0;i<mesh->vertexCount;++i) {
        Vector3 p=mesh->vertices[i].position;
        if(Vec3_Length(Vec3_Sub(p,f->center))<f->radius*1.3f) ++count;
    }
    return count;
}
static void test_budget_preserves_octants(void) {
    DetailFixture fixture={Vec3_Create(10,10,10),.1f};
    SDFField field={.context=&fixture,.evaluate=detail_sample,.evaluateDistance=detail_distance};
    SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.useAutoBounds=true;cfg.adaptiveDetail=true;
    cfg.bounds=(AABB3D){Vec3_Create(-1,-1,-1),Vec3_Create(1,1,1)};
    cfg.voxelSize=.25f;cfg.maxCells=8;
    SDFDetailRegion region={.bounds=cfg.bounds,.targetVoxelSize=.03125f};
    SDFMesher mesher=SDFMesher_Create(cfg);Mesh mesh=Mesh_Create();
    TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&mesher,&field,&region,1,&mesh),"Falló el muestreo limitado");
    TEST_ASSERT(mesher.lastStats.cellBudgetAdjusted&&mesher.lastStats.cellCount<=8,"Presupuesto incorrecto");
    unsigned directions=0;
    for(size_t i=0;i<mesh.vertexCount;++i) {
        Vector3 p=mesh.vertices[i].position;
        if(p.x<0)directions|=1u;
        if(p.x>0)directions|=2u;
        if(p.y<0)directions|=4u;
        if(p.y>0)directions|=8u;
        if(p.z<0)directions|=16u;
        if(p.z>0)directions|=32u;
    }
    TEST_ASSERT(directions==63,"El presupuesto descartó octantes pendientes");
    MeshValidationResult result=Mesh_Validate(&mesh);
    TEST_ASSERT(result.valid&&result.watertight,"El presupuesto abrió la superficie");
    Mesh_Free(&mesh);SDFMesher_Free(&mesher);
}
static void test_connector_cell_ranges(void) {
    Monster monster=Monster_Create();
    const CreatureRecipe* recipe=CreatureRecipes_Lizard();
    TEST_ASSERT(Creature_BuildMonster(&monster,recipe,&recipe->adult),"Falló el campo de conectores");
    MonsterSDF sdf=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"Falló la compilación del campo");
    /* Aísla la ruta de conectores de una cadena axial no reducible a Z. */
    sdf.axialStationCount=0;sdf.mouthCount=0;
    SDFField field=MonsterSDF_GetField(&sdf);
    Vector3 extent=AABB_Size(sdf.bounds);
    unsigned seed=13579,checked=0;
    for(unsigned i=0;i<600;++i) {
        float u[3];
        for(int a=0;a<3;++a) { seed=seed*1664525u+1013904223u;u[a]=(seed>>8)*(1.0f/16777216.0f); }
        Vector3 center=Vec3_Add(sdf.bounds.start,Vec3_Create(extent.x*u[0],extent.y*u[1],extent.z*u[2]));
        float radius=.001f+(.12f*(i%11)/10);
        Vector3 half=Vec3_Create(radius,radius,radius);
        AABB3D box={Vec3_Sub(center,half),Vec3_Add(center,half)};
        float lo,hi;
        if(!field.getCellRange(field.context,box,&lo,&hi))continue;
        ++checked;
        for(unsigned k=0;k<27;++k) {
            Vector3 p=Vec3_Add(box.start,Vec3_Create(radius*(k%3),radius*((k/3)%3),radius*(k/9)));
            float d=field.evaluateDistance(field.context,p);
            TEST_ASSERT(d>=lo&&d<=hi,"El intervalo de conectores excluyó una muestra del campo");
        }
    }
    TEST_ASSERT(checked>500,"No se calcularon intervalos sin barrido axial");
    MonsterSDF_Free(&sdf);Monster_Free(&monster);
}
static void test_head_cell_ranges(void) {
    const CreatureRecipe* recipe=CreatureRecipes_Lizard();unsigned seed=13579;
    for(size_t variantIndex=0;variantIndex<LIZARD_VARIATIONS_COUNT;++variantIndex) {
        CreatureVariation variation=LizardVariations_Get(variantIndex,42);CreatureVariant variant;
        TEST_ASSERT(CreatureVariation_Apply(recipe,&recipe->adult,&variation,&variant),"Variante de intervalo inválida");
        Monster monster=Monster_Create();MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(Creature_BuildMonster(&monster,&variant.recipe,&variant.phenotype),"Anatomía de intervalo inválida");
        TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"Campo de intervalo inválido");
        SDFField field=MonsterSDF_GetField(&sdf);
        for(unsigned i=0;i<600;++i) {
            AABB3D bounds=i%2?sdf.bounds:sdf.mouths[0].headBounds;Vector3 extent=AABB_Size(bounds);float u[3];
            for(int a=0;a<3;++a) { seed=seed*1664525u+1013904223u;u[a]=(seed>>8)*(1.0f/16777216.0f); }
            Vector3 center=Vec3_Add(bounds.start,Vec3_Create(extent.x*u[0],extent.y*u[1],extent.z*u[2]));
            float radius=.0001f+.2f*(i%17)/16;Vector3 half=Vec3_Create(radius,radius,radius);
            AABB3D box={Vec3_Sub(center,half),Vec3_Add(center,half)};float lo,hi;
            TEST_ASSERT(field.getCellRange(field.context,box,&lo,&hi),"No se resolvió un intervalo craneal");
            for(unsigned k=0;k<27;++k) {
                Vector3 p=Vec3_Add(box.start,Vec3_Create(radius*(k%3),radius*((k/3)%3),radius*(k/9)));
                float d=field.evaluateDistance(field.context,p);
                TEST_ASSERT(d>=lo&&d<=hi,"El intervalo craneal excluyó una muestra del campo");
            }
        }
        MonsterSDF_Free(&sdf);Monster_Free(&monster);
    }
}
static void test_local_cavities(void) {
    for(int adaptive=0;adaptive<2;++adaptive)
    for(int k=0;k<3;++k) {
        DetailFixture fixture={Vec3_Create(.125f,.125f,.125f),.035f+.02f*k};
        SDFField field={.context=&fixture,.evaluate=detail_sample,.evaluateDistance=detail_distance};
        SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.useAutoBounds=false;
        cfg.bounds=(AABB3D){Vec3_Create(-1,-1,-4),Vec3_Create(1,1,4)};
        cfg.voxelSize=.25f;cfg.maxCells=200000;cfg.maxResolution=256;
        SDFMesher mesher=SDFMesher_Create(cfg);Mesh coarse=Mesh_Create(),fine=Mesh_Create(),again=Mesh_Create();
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&coarse),"Falló la referencia gruesa");
        TEST_ASSERT(cavity_vertices(&coarse,&fixture)==0,"La referencia no reproduce la desaparición subvoxel");
        mesher.config.useAutoBounds=adaptive!=0;
        mesher.config.adaptiveDetail=adaptive!=0;
        Vector3 extent=Vec3_Create(fixture.radius*1.6f,fixture.radius*1.6f,fixture.radius*1.6f);
        SDFDetailRegion region={.bounds={Vec3_Sub(fixture.center,extent),Vec3_Add(fixture.center,extent)},.targetVoxelSize=fixture.radius/4};
        TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&mesher,&field,&region,1,&fine),"Falló el detalle local");
        TEST_ASSERT(cavity_vertices(&fine,&fixture)>100,"Se perdió la cavidad negativa refinada");
        MeshValidationResult result=Mesh_Validate(&fine);
        TEST_ASSERT(result.valid&&result.watertight&&result.nonManifoldEdgeCount==0,"El refinamiento introdujo grietas, NaN o índices inválidos");
        TEST_ASSERT(mesher.lastStats.refinedCellCount>0&&mesher.lastStats.activeCellCount>0,"No se refinó ninguna celda");
        TEST_ASSERT(mesher.lastStats.cellCount<=cfg.maxCells&&!mesher.lastStats.detailBudgetAdjusted,"No se respetó el presupuesto de detalle");
        for(size_t i=0;i<fine.vertexCount;++i) {
            Vector3 p=fine.vertices[i].position;
            TEST_ASSERT(p.x>=-1&&p.x<=1&&p.y>=-1&&p.y<=1&&p.z>=-4&&p.z<=4,"Vértice fuera de los límites");
            if(Vec3_Length(Vec3_Sub(p,fixture.center))<fixture.radius*1.3f)
                TEST_ASSERT(Vec3_Dot(fine.vertices[i].normal,Vec3_Sub(p,fixture.center))<0,"Normal invertida en una cavidad");
        }
        TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&mesher,&field,&region,1,&again),"Falló el segundo muestreo");
        TEST_ASSERT(fine.vertexCount==again.vertexCount&&fine.indexCount==again.indexCount&&
            memcmp(fine.indices,again.indices,fine.indexCount*sizeof(MeshIndex))==0,"La triangulación no es determinista");
        for(size_t i=0;i<fine.vertexCount;++i)
            TEST_ASSERT(Vec3_LengthSq(Vec3_Sub(fine.vertices[i].position,again.vertices[i].position))==0,"La posición no es determinista");
        mesher.config.maxCells=1000;
        TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&mesher,&field,&region,1,&again)&&mesher.lastStats.cellCount<=1000&&mesher.lastStats.detailBudgetAdjusted,"El presupuesto estricto no limita el refinamiento");
        /* Agotar el presupuesto no puede omitir los octantes pendientes. */
        MeshValidationResult limited=Mesh_Validate(&again);
        TEST_ASSERT(limited.valid&&limited.watertight&&limited.nonManifoldEdgeCount==0,
            "Agotar el presupuesto dejó una superficie abierta");
        mesher.config.maxCells=7;
        mesher.config.useAutoBounds=false;
        TEST_ASSERT(!SDFMesher_GenerateMesh(&mesher,&field,&again),"Un presupuesto imposible no debe bloquear el muestreador");
        Mesh_Free(&coarse);Mesh_Free(&fine);Mesh_Free(&again);SDFMesher_Free(&mesher);
    }
    printf("[PASS] test_local_cavities\n");
}
static void test_growth_ownership(void) {
    Monster a=Monster_Create(),b=Monster_Create();CreaturePhenotype pa=CreatureRecipes_Lizard()->juvenile,pb=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&a,CreatureRecipes_Lizard(),&pa)&&Creature_BuildMonster(&b,CreatureRecipes_Lizard(),&pb),"Extremos anatómicos inválidos");
    Monster_SetHeadOpenFactor(&a,.10f);Monster_SetHeadOpenFactor(&b,.10f);
    MonsterAger ager=MonsterAger_Create(&a,&b,0);
    const float ages[]={0,.10f,.25f,.50f,.75f,.90f,1};float lastScale=0,lastEye=1,lastJaw=0;
    for(size_t i=0;i<sizeof(ages)/sizeof(ages[0]);++i) {
        MonsterAger_SetPerc(&ager,ages[i]);Monster* m=&ager.result;
        TEST_ASSERT(m->phenotype.axial.totalScale>lastScale,"El tamaño no crece monótonamente");
        TEST_ASSERT(m->head.phenotype.eyeSize<=lastEye&&m->head.phenotype.jawStrength>=lastJaw,"La alometría invierte su dirección");
        lastScale=m->phenotype.axial.totalScale;lastEye=m->head.phenotype.eyeSize;lastJaw=m->head.phenotype.jawStrength;
        TEST_ASSERT(FLOAT_NEAR(m->mouths[0].openFactor,.10f),"La edad modificó la pose mandibular");
        TEST_ASSERT(HeadAnatomy_Validate(&m->head.anatomy)==HEAD_VALID&&AnatomyGraph_Validate(&m->anatomyGraph),"Anatomía intermedia inválida");
        Monster expected=Monster_Create();TEST_ASSERT(Creature_BuildMonster(&expected,CreatureRecipes_Lizard(),&m->phenotype),"No se pudo resolver el fenotipo interpolado");
        TEST_ASSERT(Vec3_LengthSq(Vec3_Sub(expected.head.anatomy.landmarks.muzzleTip,m->head.anatomy.landmarks.muzzleTip))<1e-10f,"La cabeza no procede del mismo fenotipo que el cuerpo");
        TEST_ASSERT(Vec3_LengthSq(Vec3_Sub(expected.eyes[0].offset,m->eyes[0].offset))<1e-10f,"El ojo quedó interpolado fuera de su propietario");
        const AnatomyNode* neck=AnatomyGraph_FindNode(&m->anatomyGraph,Anatomy_MakeId(1,1));
        const AnatomyNode* chest=AnatomyGraph_FindNode(&m->anatomyGraph,Anatomy_MakeId(1,2));
        TEST_ASSERT(neck->widthRadius<chest->widthRadius,"Cuello más ancho que cintura pectoral");
        float lastZ=1e6f,lastRadius=1e6f;
        for(size_t j=0;j<m->anatomyGraph.nodeCount;++j) {
            const AnatomyNode* n=&m->anatomyGraph.nodes[j];
            if(n->role==ANATOMY_ROLE_AXIAL&&n->region!=ANATOMY_REGION_HEAD) {
                TEST_ASSERT(n->center.z<lastZ,"Estaciones axiales duplicadas o desordenadas");lastZ=n->center.z;
                if(n->region==ANATOMY_REGION_TAIL) {
                    TEST_ASSERT(n->widthRadius<lastRadius,"La cola no se ahúsa");lastRadius=n->widthRadius;
                }
            }
        }
        AnatomyGraph before=m->anatomyGraph;Vector3 eye=m->eyes[0].offset;
        MonsterAger_SetPerc(&ager,fminf(1,ages[i]+.0001f));
        for(size_t j=0;j<before.nodeCount;++j)
            TEST_ASSERT(Vec3_Length(Vec3_Sub(before.nodes[j].center,m->anatomyGraph.nodes[j].center))<.01f,"Salto de landmark con delta de edad pequeño");
        TEST_ASSERT(Vec3_Length(Vec3_Sub(eye,m->eyes[0].offset))<.001f,"Salto ocular durante el crecimiento");
        Monster_Free(&expected);
    }
    MonsterAger_Free(&ager);Monster_Free(&a);Monster_Free(&b);
    printf("[PASS] test_growth_ownership\n");
}
void run_local_detail_tests(void) { test_budget_preserves_octants();test_connector_cell_ranges();test_head_cell_ranges();test_local_cavities();test_growth_ownership(); }
