#include "Creature.h"
#include "Limb.h"
#include "test_utils.h"
#include "SurfacePresets.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "MonsterVisualAsync.h"
#include "MonsterAnimation.h"
#include "AnatomyDeformer.h"
#include "MonsterAger.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static void TestPhenotype(void) {
    for(uint32_t seed=0;seed<100;++seed) {
        SurfacePhenotype p=SurfacePreset_ScaledReptile(seed,1),q=SurfacePreset_ScaledReptile(seed,1);
        SurfaceRecipe a=SurfaceRecipe_Compile(&p),b=SurfaceRecipe_Compile(&q);
        TEST_ASSERT(memcmp(&a,&b,sizeof(a))==0,"Recetas reproducibles desde semilla");
        SurfacePhenotype other=SurfacePreset_ScaledReptile(seed+1,1); b=SurfaceRecipe_Compile(&other);
        TEST_ASSERT(memcmp(&a,&b,sizeof(a))!=0,"Individuos diferentes");
        p.integument.scales.relief=NAN; p.integument.scales.size=-1;
        p.integument.scales.roughness=INFINITY; p.pigment.patternScale=NAN;
        p.regions[SURFACE_REGION_HEAD].size=INFINITY;
        a=SurfaceRecipe_Compile(&p);
        for(int i=0;i<SURFACE_RECIPE_ROWS;++i)for(int j=0;j<4;++j)
            TEST_ASSERT(isfinite(a.data[i][j]),"Receta sin NaN/Inf");
        TEST_ASSERT(a.data[4][0]>=.012f && a.data[6][0]>=.12f,"Rangos seguros");
    }
    CreaturePhenotype juvenile=CreatureRecipes_Lizard()->juvenile,adult=CreatureRecipes_Lizard()->adult;
    uint32_t seed=juvenile.surface.integument.scales.seed;
    for(int i=0;i<=100;++i) {
        CreaturePhenotype p=CreaturePhenotype_Interpolate(&juvenile,&adult,i*.01f);
        SurfaceRecipe r=SurfaceRecipe_Compile(&p.surface);
        TEST_ASSERT(r.scaleSeed==seed,"Maduración no cambia identidad celular");
        TEST_ASSERT(r.data[5][0]>=juvenile.surface.integument.scales.relief-1e-6f &&
                    r.data[5][0]<=adult.surface.integument.scales.relief+1e-6f,"Relieve ontogenético continuo");
    }
    SurfacePhenotype skin=SurfacePhenotype_Default(),scales=SurfacePreset_ScaledReptile(1,1);
    SurfacePhenotype mid=SurfacePhenotype_Interpolate(&skin,&scales,.5f);
    TEST_ASSERT(FLOAT_NEAR(mid.integument.coverage,.5f),"Transición piel a escamas por cobertura");
}
static void TestMappingAndPose(void) {
    Monster m=Monster_Create(); CreaturePhenotype p=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&p),"Construir rig para dominio estable");
    uint64_t rigGeneration=m.animation->rigGeneration,anatomy=AnatomyGraph_Fingerprint(&m.anatomyGraph);
    SurfacePhenotype appearance=m.surface; appearance.pigment.seed++;
    uint32_t scaleSeed=appearance.integument.scales.seed;
    Monster_SetSurface(&m,&appearance);
    TEST_ASSERT(m.phenotype.surface.pigment.seed==appearance.pigment.seed && m.surface.integument.scales.seed==scaleSeed,"Pigmento independiente y fenotipo sincronizado");
    TEST_ASSERT(m.animation->rigGeneration==rigGeneration && AnatomyGraph_Fingerprint(&m.anatomyGraph)==anatomy,"Setter no altera anatomía ni rig");
    Mesh mesh=Mesh_Create();
    SurfaceCoordinate saved[ANATOMY_MAX_NODES];
    for(size_t i=0;i<m.anatomyGraph.nodeCount;++i) {
        const AnatomyNode* node=&m.anatomyGraph.nodes[i];
        MeshVertex v={.position=Vec3_Add(node->center,Vec3_Create(0,node->heightRadius,0)),
            .normal={0,1,0},.material=SDF_MATERIAL_SKIN,.color={100,120,80,255}};
        TEST_ASSERT(Mesh_AddVertex(&mesh,v,NULL),"Crear muestras de cada estación");
    }
    SurfaceMapperStats mapStats={0};
    SurfaceMapper_MapMeshWithStats(&mesh,&m.anatomyGraph,&m.surfaceMapping,&mapStats);
    TEST_ASSERT(mapStats.candidateTests<mapStats.bruteForceTests,
        "El BVH reduce candidatos frente a vértices por todas las conexiones");
    for(size_t i=0;i<mesh.vertexCount;++i)saved[i]=mesh.vertices[i].surface;
    SurfaceCoordinate unknown=SurfaceMapper_MapPoint(Vec3_Zero(),Vec3_Create(0,1,0),SDF_MATERIAL_SKIN,NULL,NULL);
    TEST_ASSERT(unknown.region==SURFACE_REGION_UNKNOWN && unknown.blend==0,"Sin anatomía no se inventa una región");
    SurfaceCoordinate oral=SurfaceMapper_MapPoint(Vec3_Zero(),Vec3_Create(0,1,0),SDF_MATERIAL_MOUTH,&m.anatomyGraph,&m.surfaceMapping);
    TEST_ASSERT(oral.region==SURFACE_REGION_ORAL,"Tejidos orales excluidos de escamas");
    const AnatomyNode* tail=AnatomyGraph_FindNode(&m.anatomyGraph,Anatomy_MakeId(20,2));
    SurfaceCoordinate t=SurfaceMapper_MapPoint(tail->center,Vec3_Create(0,1,0),SDF_MATERIAL_SKIN,&m.anatomyGraph,&m.surfaceMapping);
    TEST_ASSERT(t.region==SURFACE_REGION_TAIL,"Identidad de cola");
    const AnatomyNode* digit=AnatomyGraph_FindNode(&m.anatomyGraph,Anatomy_MakeId(10+(0),Limb_DigitLocalId(2,3)));
    t=SurfaceMapper_MapPoint(digit->center,Vec3_Create(0,1,0),SDF_MATERIAL_SKIN,&m.anatomyGraph,&m.surfaceMapping);
    TEST_ASSERT(t.region==SURFACE_REGION_DIGIT,"Identidad de dígito");
    /* Reordenar estaciones no debe alterar el dominio ni sus etiquetas por ID. */
    AnatomyGraph reordered=m.anatomyGraph;
    for(size_t i=0;i<reordered.nodeCount/2;++i) {
        AnatomyNode tmp=reordered.nodes[i]; reordered.nodes[i]=reordered.nodes[reordered.nodeCount-1-i]; reordered.nodes[reordered.nodeCount-1-i]=tmp;
    }
    SurfaceMapper_MapMesh(&mesh,&reordered,&m.surfaceMapping);
    for(size_t i=0;i<mesh.vertexCount;++i)TEST_ASSERT(memcmp(&saved[i],&mesh.vertices[i].surface,sizeof(saved[i]))==0,"Identidad independiente del orden del grafo");
    AnatomyDeformer* deformer=AnatomyDeformer_Create();
    TEST_ASSERT(AnatomyDeformer_BindSkeleton(deformer,&mesh,&m.anatomyGraph,&m.animation->rig.skeleton),"Bind de reposo");
    Vector3 initial=mesh.vertices[0].position;
    m.animation->animator.desiredVelocity=Vec3_Create(0,0,.38f);
    for(int frame=0;frame<120;++frame) {
        TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Marcha válida");
        TEST_ASSERT(AnatomyDeformer_DeformPose(deformer,&m.animation->rig.skeleton,&m.animation->pose,&mesh),"Aplicar pose");
        for(size_t i=0;i<mesh.vertexCount;++i)
            TEST_ASSERT(memcmp(&saved[i],&mesh.vertices[i].surface,sizeof(saved[i]))==0,"Pose conserva coordenada, normal de reposo y regiones");
    }
    TEST_ASSERT(Vec3_Distance(initial,mesh.vertices[0].position)>.05f,"La prueba realmente mueve la piel");
    AnatomyDeformer_Free(deformer); Mesh_Free(&mesh); Monster_Free(&m);
}
static void TestAppearanceNoRebuild(void) {
    Monster m=Monster_Create(); Monster_Init(&m);
    m.surface=SurfacePreset_ScaledReptile(42,1); m.surfaceMapping.unitScale=2.5f;
    SDFMesherConfig cfg=SDFMesher_DefaultConfig(); cfg.voxelSize=.3f; cfg.maxCells=10000;
    MonsterVisual* v=malloc(sizeof(*v)); TEST_ASSERT(v!=NULL,"Reserva visual"); *v=MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(v,&m,MonsterSDF_DefaultConfig()),"Construcción inicial");
    uint64_t generation=v->rebuildGeneration,fingerprint=v->geometryFingerprint;
    MeshVertex* vertices=v->mesh.vertices; MeshIndex* indices=v->mesh.indices;
    SurfaceCoordinate coord=v->mesh.vertices[0].surface;
    TEST_ASSERT(!v->mesh.hasSurface && Vec3_LengthSq(coord.normal)>.5f,"Malla heredada ya posee dominio de reposo válido");
    Monster_SetSurface(&m,&m.surface);
    TEST_ASSERT(!MonsterVisual_Update(v,&m,1,0,MonsterSDF_DefaultConfig()) && v->mesh.hasSurface,"Activar superficie sin remallar");
    SurfaceRecipe old=v->mesh.surfaceRecipe;
    m.surface.pigment.baseColor=Color_FromRGB(180,90,30); m.surface.pigment.patternStrength=.9f;
    m.surface.integument.scales.size=.25f; m.surface.integument.scales.keelStrength=1;
    TEST_ASSERT(!MonsterVisual_Update(v,&m,1,0,MonsterSDF_DefaultConfig()),"Editar apariencia no reconstruye");
    TEST_ASSERT(v->rebuildGeneration==generation && v->geometryFingerprint==fingerprint,"Huella geométrica independiente");
    TEST_ASSERT(vertices==v->mesh.vertices && indices==v->mesh.indices,"Buffers persistentes");
    TEST_ASSERT(memcmp(&coord,&vertices[0].surface,sizeof(coord))==0,"Edición no remapea");
    TEST_ASSERT(memcmp(&old,&v->mesh.surfaceRecipe,sizeof(old))!=0,"La receta sí cambia");
    Monster clone=Monster_Clone(&m); SurfaceRecipe copied=SurfaceRecipe_CompileScaled(&clone.surface,clone.surfaceMapping.unitScale);
    TEST_ASSERT(clone.hasSurface && memcmp(&copied,&v->mesh.surfaceRecipe,sizeof(copied))==0,"Snapshots conservan apariencia");
    Monster_Free(&clone); MonsterVisual_Free(v); free(v);
    MonsterVisualAsyncConfig asyncCfg=MonsterVisualAsync_DefaultConfig();
    asyncCfg.interactiveMesherConfig=cfg; asyncCfg.settledMesherConfig=cfg; asyncCfg.settledDelaySec=100;
    MonsterVisualAsync* async=MonsterVisualAsync_Create(asyncCfg);
    TEST_ASSERT(async!=NULL,"Gestor asíncrono");
    MonsterVisualAsync_Update(async,&m,.016f); MonsterVisualAsync_Flush(async);
    MonsterVisualAsyncStats before=MonsterVisualAsync_GetStats(async);
    SurfaceRecipe beforeRecipe=async->displayMesh.surfaceRecipe;
    TEST_ASSERT(FLOAT_NEAR(beforeRecipe.data[11][2],2.5f),"Async conserva escala física de sync");
    m.surface.pigment.seed++; m.surface.integument.scales.size=.1f;
    Monster_SetSurface(&m,&m.surface);
    MonsterVisualAsync_Update(async,&m,.016f); MonsterVisualAsync_Flush(async);
    MonsterVisualAsyncStats after=MonsterVisualAsync_GetStats(async);
    TEST_ASSERT(FLOAT_NEAR(async->displayMesh.surfaceRecipe.data[11][2],2.5f),"Editar apariencia conserva escala async");
    TEST_ASSERT(before.requestCount==after.requestCount && before.completedBuildCount==after.completedBuildCount,"Edición asíncrona no encola SDF");
    TEST_ASSERT(memcmp(&beforeRecipe,&async->displayMesh.surfaceRecipe,sizeof(beforeRecipe))!=0,"Receta asíncrona actualizada");
    MonsterVisualAsync_Free(async); Monster_Free(&m);
}
static void TestGenericAger(void) {
    Monster a=Monster_Create(),b=Monster_Create(),dst=Monster_Create();
    Monster_Init(&a); Monster_Init(&b); Monster_Init(&dst);
    a.surfaceMapping.origin=Vec3_Create(1,2,3); a.surfaceMapping.unitScale=1;
    b.surfaceMapping.origin=Vec3_Create(4,5,6); b.surfaceMapping.unitScale=2;
    SurfacePhenotype surface=SurfacePreset_ScaledReptile(9,1); Monster_SetSurface(&b,&surface);
    MonsterAger_Interpolate(&a,&b,0,&dst);
    TEST_ASSERT(!dst.hasSurface && dst.surfaceMapping.origin.x==1,"Endpoint heredado conserva ruta y dominio");
    MonsterAger_Interpolate(&a,&b,.5f,&dst);
    SurfaceRecipe recipe=SurfaceRecipe_Compile(&dst.surface);
    TEST_ASSERT(dst.hasSurface && FLOAT_NEAR(dst.surfaceMapping.unitScale,1.5f),"Dominio genérico interpola escala");
    for(int i=0;i<SURFACE_RECIPE_ROWS;++i)for(int j=0;j<4;++j)
        TEST_ASSERT(isfinite(recipe.data[i][j]),"Fallback de superficie válido");
    MonsterAger_Interpolate(&a,&b,1,&dst);
    TEST_ASSERT(dst.hasSurface && dst.surfaceMapping.origin.x==4 && dst.surfaceMapping.unitScale==2,"Endpoint final conserva dominio destino");
    MonsterAger_Interpolate(&b,&a,1,&dst);
    TEST_ASSERT(!dst.hasSurface && dst.surfaceMapping.origin.x==1,"Retorno a render heredado");
    Monster_Free(&a); Monster_Free(&b); Monster_Free(&dst);
}
static void TestGeometryAgeQuantization(void) {
    Monster young=Monster_Create(),adult=Monster_Create();
    CreaturePhenotype a=CreatureRecipes_Lizard()->juvenile,b=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&young,CreatureRecipes_Lizard(),&a)&&Creature_BuildMonster(&adult,CreatureRecipes_Lizard(),&b),"Extremos del Ager");
    MonsterAger ager=MonsterAger_Create(&young,&adult,0);MonsterAger_SetGeometrySteps(&ager,32);
    uint64_t initial=ager.geometryInterpolationCount;
    for(unsigned i=1;i<=600;++i)MonsterAger_SetPerc(&ager,(float)i/600.f);
    TEST_ASSERT(ager.geometryInterpolationCount-initial<=32,
        "La edad continua no interpola geometría más de una vez por bucket");
    TEST_ASSERT(FLOAT_NEAR(ager.perc,1)&&FLOAT_NEAR(ager.geometryPerc,1),"Los extremos siguen siendo exactos");
    TEST_ASSERT(ager.interpolationCount>ager.geometryInterpolationCount,
        "La apariencia continua permanece desacoplada de la geometría");
    MonsterAger_Free(&ager);Monster_Free(&young);Monster_Free(&adult);
}
static void TestCanonicalBinding(void) {
    AnatomyGraph canonical={0};
    TEST_ASSERT(AnatomyGraph_AddNode(&canonical,(AnatomyNode){.id=1,.center={0,0,0},.widthRadius=1,.heightRadius=1,.colorIndex=0,.role=ANATOMY_ROLE_AXIAL,.region=ANATOMY_REGION_TRUNK,.side=ANATOMY_SIDE_CENTER,.moduleInstanceId=0,.localNodeId=0,.development=1.0f}),"Raíz canónica");
    TEST_ASSERT(AnatomyGraph_AddNode(&canonical,(AnatomyNode){.id=2,.center={0,0,2},.widthRadius=1,.heightRadius=1,.colorIndex=0,.role=ANATOMY_ROLE_AXIAL,.region=ANATOMY_REGION_TRUNK,.side=ANATOMY_SIDE_CENTER,.moduleInstanceId=0,.localNodeId=0,.development=1.0f}),"Extremo canónico");
    TEST_ASSERT(AnatomyGraph_Connect(&canonical,(BodyConnection){.id=1,.fromId=1,.toId=2,.kind=BODY_CONNECTION_LIMB_SEGMENT,.moduleInstanceId=0,.development=1.0f}),"Conexión canónica");
    AnatomyDeformer* binder=AnatomyDeformer_Create();
    for(unsigned age=0;age<=20;++age) {
        float radius=.2f+.04f*age,length=.5f+.1f*age;
        AnatomyGraph graph=canonical;
        graph.nodes[0].widthRadius=graph.nodes[0].heightRadius=radius;
        graph.nodes[1].widthRadius=graph.nodes[1].heightRadius=radius;
        graph.nodes[1].center.z=length;
        Mesh mesh=Mesh_Create();
        for(unsigned k=0;k<16;++k) {
            float angle=k*6.2831853f/16;
            MeshVertex v={.position={radius*cosf(angle),radius*sinf(angle),length*.63f},
                .normal={cosf(angle),sinf(angle),0},.material=SDF_MATERIAL_SKIN};
            TEST_ASSERT(Mesh_AddVertex(&mesh,v,NULL),"Muestra superficial");
        }
        TEST_ASSERT(AnatomyDeformer_Bind(binder,&mesh,&graph),"Vincular nueva malla");
        SurfaceMapperStats stats;
        TEST_ASSERT(SurfaceMapper_MapBoundMesh(&mesh,binder,&canonical,NULL,&stats),"Dominio canónico compartido");
        TEST_ASSERT(stats.candidateTests==0,"No repetir búsqueda anatómica");
        for(unsigned k=0;k<16;++k) {
            float angle=k*6.2831853f/16;
            TEST_ASSERT(Vec3_Distance(mesh.vertices[k].surface.position,
                Vec3_Create(cosf(angle),sinf(angle),1.26f))<1e-5f,"Identidad material estable al remallar y crecer");
        }
        Mesh_Free(&mesh);
    }
    AnatomyDeformer_Free(binder);
}
static void TestHeadCage(void) {
    HeadPhenotype p=HeadPhenotype_LizardPreset();HeadAnatomy head;
    TEST_ASSERT(HeadAnatomy_Resolve(&p,0,Vec3_Create(1,.6f,1.1f),&head),"Resolver cabeza base");
    Mesh mesh=Mesh_Create();Vector3 base=Vec3_Add(head.surface.craniumCenter,Vec3_Create(head.surface.craniumRadii.x,0,0));
    MeshVertex v={.position=base,.normal={1,0,0},.surface={.region=SURFACE_REGION_HEAD,.secondaryRegion=SURFACE_REGION_HEAD}};
    TEST_ASSERT(Mesh_AddVertex(&mesh,v,NULL),"Muestra de cráneo");
    MeshVertex hand=v;hand.position=Vec3_Create(1,-2,base.z);
    TEST_ASSERT(Mesh_AddVertex(&mesh,hand,NULL),"Mano por delante del cuello");
    HeadMorph* morph=HeadMorph_Create();
    TEST_ASSERT(HeadMorph_Bind(morph,&mesh,&head,Vec3_Zero()),"Vinculación cefálica");
    TEST_ASSERT(HeadMorph_Deform(morph,&mesh,&head,Vec3_Zero()),"Pose de referencia");
    TEST_ASSERT(Vec3_Distance(base,mesh.vertices[0].position)<1e-5f,"Jaula identidad en referencia");
    Vector3 previous=base;
    for(unsigned i=1;i<=100;++i) {
        HeadPhenotype target=p;target.skullWidth=p.skullWidth+.003f*i;
        TEST_ASSERT(HeadAnatomy_Resolve(&target,0,Vec3_Create(1,.6f,1.1f),&head),"Fenotipo continuo");
        TEST_ASSERT(HeadMorph_Deform(morph,&mesh,&head,Vec3_Zero()),"Deformar sin remallar");
        TEST_ASSERT(Vec3_Distance(previous,mesh.vertices[0].position)<.02f,"Continuidad de proporciones cefálicas");
        previous=mesh.vertices[0].position;
    }
    TEST_ASSERT(Vec3_Distance(base,previous)>.01f,"El cráneo responde al fenotipo sin cambiar anfitrión");
    TEST_ASSERT(Vec3_Distance(hand.position,mesh.vertices[1].position)<1e-6f,"La jaula cefálica no deforma extremidades");
    HeadMorph_Free(morph);Mesh_Free(&mesh);
}
static void TestDormantAppendages(void) {
    Monster monster=Monster_Create();CreaturePhenotype larva=CreatureRecipes_Lizard()->larva,adult=CreatureRecipes_Lizard()->adult;
    size_t previous=0;
    for(unsigned i=0;i<=40;++i) {
        CreaturePhenotype p=CreaturePhenotype_Interpolate(&larva,&adult,i*.025f);
        TEST_ASSERT(Creature_ResolveAppearance(&monster,CreatureRecipes_Lizard(),&p),"Resolver desarrollo");
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"Compilar desarrollo");
        size_t active=0;
        for(size_t c=0;c<monster.anatomyGraph.connectionCount;++c)
            if(!monster.anatomyGraph.dormantConnections[c] && monster.anatomyGraph.connections[c].kind != BODY_CONNECTION_SUPPORT)active++;
        TEST_ASSERT(sdf.connectorCount==active,"SDF sólo compila conexiones desarrolladas");
        TEST_ASSERT(active>=previous,"Emergencia progresiva");previous=active;
        if(i==0)TEST_ASSERT(active<20,"Larva sin jerarquía digital degenerada en SDF");
        if(i==0) {
            SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.adaptiveDetail=true;cfg.voxelSize=.08f;
            SDFMesher mesher=SDFMesher_Create(cfg);Mesh mesh=Mesh_Create();SDFField field=MonsterSDF_GetField(&sdf);
            TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&mesher,&field,NULL,0,&mesh),"Adaptativo sin regiones de detalle");
            MeshValidationResult validation=Mesh_Validate(&mesh);
            TEST_ASSERT(validation.valid&&validation.watertight&&mesh.vertexCount>0,"Malla adaptativa vacía de regiones válida");
            Mesh_Free(&mesh);SDFMesher_Free(&mesher);
        }
        MonsterSDF_Free(&sdf);
    }
    Monster_Free(&monster);
}
void run_surface_tests(void) {
    TestHeadCage();TestDormantAppendages();TestCanonicalBinding(); TestPhenotype(); TestMappingAndPose(); TestAppearanceNoRebuild(); TestGenericAger();TestGeometryAgeQuantization();
    printf("[PASS] superficie: determinismo, límites, regiones, marcha estable, edición sin reconstrucción\n");
}
