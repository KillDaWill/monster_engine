#include "Creature.h"
#include "Limb.h"
/**
 * @file test_morph.c
 * @brief Pruebas unitarias para el módulo de deformación morfológica continua AnatomyDeformer.
 * @author Monster Engine Team
 * @date 2026
 */

#include "test_utils.h"
#include "AnatomyDeformer.h"
#include "Creature.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "MonsterAger.h"
#include "MonsterVisualAsync.h"
#include "MorphSurfaceTracker.h"
#include "CreatureMorph.h"
#include "MorphModuleState.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static double Morph_GetTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static size_t Morph_CountReversedInterface(const Mesh* visible,
    const Mesh* certified,const uint8_t* frozen,
    const MorphAnchoredVertex* anchored) {
    size_t reversed=0;
    if(!visible||!certified||!frozen||
       visible->vertexCount!=certified->vertexCount)return SIZE_MAX;
    for(size_t t=0;t+2<visible->indexCount;t+=3) {
        MeshIndex a=visible->indices[t],b=visible->indices[t+1],
            c=visible->indices[t+2];
        unsigned frozenCount=frozen[a]+frozen[b]+frozen[c];
        if(!frozenCount||frozenCount==3)continue;
        Vector3 p=visible->vertices[a].position,
            q=visible->vertices[b].position,r=visible->vertices[c].position;
        Vector3 x=frozen[a]?anchored[a].position:certified->vertices[a].position,
            y=frozen[b]?anchored[b].position:certified->vertices[b].position,
            z=frozen[c]?anchored[c].position:certified->vertices[c].position;
        Vector3 original=Vec3_Cross(Vec3_Sub(y,x),Vec3_Sub(z,x));
        if(Vec3_LengthSq(original)<=1e-16f)continue;
        Vector3 current=Vec3_Cross(Vec3_Sub(q,p),Vec3_Sub(r,p));
        reversed+=Vec3_Dot(current,original)<=0;
    }
    return reversed;
}

static void test_morph_module_certification(void) {
    const unsigned delays[]={0,100,300,500,1000,3000};
    MorphModuleState foot,torso;
    MorphModuleState_Init(&foot,13,42,true);
    MorphModuleState_Init(&torso,1,42,false);
    MorphModuleState_Request(&foot,.25f);
    MorphModuleState_Request(&torso,.25f);
    MorphPatchToken footBase=MorphModuleState_Token(&foot,.25f);
    MorphPatchToken torsoBase=MorphModuleState_Token(&torso,.25f);
    TEST_ASSERT(MorphModuleState_Accept(&foot,footBase)&&
        MorphModuleState_Accept(&torso,torsoBase),
        "No se certificó la superficie inicial de los módulos");
    for(size_t i=0;i<sizeof(delays)/sizeof(delays[0]);++i) {
        /* El tiempo de terminación no concede validez geométrica. */
        MorphModuleState_Request(&foot,.75f);
        MorphModuleState_Request(&torso,.75f);
        TEST_ASSERT(!MorphModuleState_CanPredict(&foot,.75f)&&
            foot.fallbackActive&&MorphModuleState_CanPredict(&torso,.75f)&&
            MorphModuleState_NeedsExactFallback(&foot,.75f,.05f)&&
            !MorphModuleState_NeedsExactFallback(&torso,.75f,.05f),
            "La latencia permitió extrapolar dedos con conectividad heredada");
        TEST_ASSERT(delays[i]<=3000,"Latencia de la regresión inválida");
    }
    MorphPatchToken obsolete=MorphModuleState_Token(&foot,.35f);
    MorphPatchToken newer=MorphModuleState_Token(&foot,.75f);
    TEST_ASSERT(MorphModuleState_Accept(&foot,newer),
        "No se publicó el parche certificado más reciente");
    TEST_ASSERT(!MorphModuleState_Accept(&foot,obsolete)&&
        fabsf(foot.certifiedAge-.75f)<1e-5f,
        "Un trabajo completado fuera de orden reemplazó topología nueva");
    TEST_ASSERT(MorphModuleState_CanPredict(&foot,.75f)&&
        !MorphModuleState_NeedsExactFallback(&foot,.75f,.05f)&&
        !MorphModuleState_CanPredict(&foot,.749f)&&
        !MorphModuleState_CanPredict(&foot,.751f),
        "La predicción cruzó un borde de certificación");
    obsolete=newer;obsolete.topologyVersion++;
    TEST_ASSERT(!MorphModuleState_Accept(&foot,obsolete),
        "Se aceptó un resultado de otra versión topológica");
    obsolete=newer;obsolete.morphGeneration++;
    TEST_ASSERT(!MorphModuleState_Accept(&foot,obsolete),
        "Se aceptó un resultado de otros extremos morfológicos");
    printf("[PASS] test_morph_module_certification\n");
}

static void test_morph_async_delayed_topology(void) {
    Monster dog=Monster_Create(),lizard=Monster_Create();
    CreaturePhenotype dp=CreatureRecipes_Dog()->adult;
    CreaturePhenotype lp=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog,CreatureRecipes_Dog(),&dp)&&
        Creature_BuildMonster(&lizard,CreatureRecipes_Lizard(),&lp),
        "No se construyeron los extremos del ensayo asíncrono");
    MonsterAger ager=MonsterAger_Create(&dog,&lizard,.25f);
    MonsterAger_SetGeometrySteps(&ager,20);
    MonsterVisualAsyncConfig config=MonsterVisualAsync_DefaultConfig();
    config.interactiveMesherConfig.voxelSize=.075f;
    config.settledMesherConfig.voxelSize=.055f;
    config.morphMesherConfig.voxelSize=.070f;
    MonsterVisualAsync* visual=MonsterVisualAsync_Create(config);
    TEST_ASSERT(visual&&MonsterVisualAsync_SetMorphEndpoints(visual,&dog,&lizard),
        "No se inicializó el worker para topología cambiante");
    MonsterVisualAsync_SetMorphMode(visual,true);
    MonsterVisualAsync_SetContinuousMotion(visual,true);
    TEST_ASSERT(MonsterVisualAsync_NeedsExactFallback(visual,.25f,.05f),
        "El primer fotograma quedó vacío mientras el worker preparaba la malla");
    MonsterVisualAsync_UpdateWithAppearance(visual,
        MonsterAger_GetGeometryResultConst(&ager),
        MonsterAger_GetResultConst(&ager),.016f);
    MonsterVisualAsync_Flush(visual);
    MonsterVisualAsyncStats initial=MonsterVisualAsync_GetStats(visual);
    TEST_ASSERT(visual->displayMesh.vertexCount>0&&initial.topologyModuleCount>0&&
        isfinite(initial.certifiedAge),
        "La referencia inicial no quedó certificada");
    size_t initialVertices=visual->displayMesh.vertexCount;
    bool observed[5]={0};
    const uint32_t expectedModules[5]={10,11,12,13,20};
    for(size_t i=0;i<visual->morphModuleCount;++i)
        for(size_t j=0;j<5;++j)
            if(visual->morphModules[i].moduleId==expectedModules[j])
                observed[j]=true;
    for(size_t j=0;j<5;++j)
        TEST_ASSERT(observed[j],"Falta un evento semántico de pata o cola");
    MeshIndex vertex=0;
    while(vertex<visual->displayMesh.vertexCount &&
          (!visual->displayTopologyMask||!visual->displayTopologyMask[vertex]))
        vertex++;
    TEST_ASSERT(vertex<visual->displayMesh.vertexCount,
        "No se identificó la superficie sensible de las patas");
    MonsterAger_SetPerc(&ager,.26f);
    MonsterVisualAsync_UpdateWithAppearance(visual,
        MonsterAger_GetGeometryResultConst(&ager),
        MonsterAger_GetResultConst(&ager),.016f);
    Mesh anchoredReference=visual->displayCertifiedMesh;
    anchoredReference.vertices=malloc(initialVertices*sizeof(MeshVertex));
    TEST_ASSERT(anchoredReference.vertices,"No se reservó referencia de pose congelada");
    memcpy(anchoredReference.vertices,visual->displayCertifiedMesh.vertices,
        initialVertices*sizeof(MeshVertex));
    TEST_ASSERT(AnatomyDeformer_AnchorFrozenVertices(visual->displayMorph,
        &MonsterAger_GetResultConst(&ager)->anatomyGraph,
        &visual->displayCertifiedMesh,&anchoredReference,visual->displayTopologyMask),
        "No se transportó la topología congelada a la pose actual");
    TEST_ASSERT(Vec3_Distance(anchoredReference.vertices[vertex].position,
        visual->displayMesh.vertices[vertex].position)<1e-5f,
        "La predicción sobrescribió el anclaje congelado");
    free(anchoredReference.vertices);
    bool stableMoved=false;
    for(size_t v=0;v<visual->displayMesh.vertexCount;++v)
        if(!visual->displayTopologyMask[v]&&
           Vec3_Distance(visual->displayCertifiedMesh.vertices[v].position,
               visual->displayMesh.vertices[v].position)>1e-5f) {
            stableMoved=true;break;
        }
    TEST_ASSERT(stableMoved,
        "El cuerpo estable dejó de deformarse por un pie pendiente");
    TEST_ASSERT(Morph_CountReversedInterface(&visual->displayMesh,
        &visual->displayCertifiedMesh,visual->displayTopologyMask,
        visual->displayAnchoredFrozen)==0,
        "La interfaz entre cuerpo móvil y pie certificado invirtió caras");
    TEST_ASSERT(!MonsterVisualAsync_NeedsExactFallback(visual,.26f,.05f),
        "Un desfase pequeño activó el fallback implícito");
    AABB3D fbBounds = AABB_Empty();
    TEST_ASSERT(!MonsterVisualAsync_GetFallbackBounds(visual,.26f,.05f,&fbBounds),
        "Un desfase pequeño activó el cálculo de fallback bounds");
    MonsterVisualAsync_SetWorkerDelay(visual,3000);
    MonsterAger_SetPerc(&ager,.75f);
    MonsterVisualAsync_UpdateWithAppearance(visual,
        MonsterAger_GetGeometryResultConst(&ager),
        MonsterAger_GetResultConst(&ager),.016f);
    MonsterVisualAsyncStats delayed=MonsterVisualAsync_GetStats(visual);
    TEST_ASSERT(delayed.topologyFreezeCount>0&&
        fabsf(visual->morphModules[0].certifiedAge-initial.certifiedAge)<1e-5f&&
        MonsterVisualAsync_NeedsExactFallback(visual,.75f,.05f)&&
        Vec3_Distance(visual->displayAnchoredFrozen[vertex].position,
            visual->displayMesh.vertices[vertex].position)<1e-5f,
        "El visual perdió la pose anclada mientras el worker estaba retrasado");
    TEST_ASSERT(MonsterVisualAsync_GetFallbackBounds(visual,.75f,.05f,&fbBounds),
        "El visual no reportó región AABB para el módulo desfasado");
    TEST_ASSERT(fbBounds.end.x > fbBounds.start.x &&
                fbBounds.end.y > fbBounds.start.y &&
                fbBounds.end.z > fbBounds.start.z,
        "El AABB delimitador del módulo en fallback no es válido");
    Vector3 fbSz = AABB_Size(fbBounds);
    TEST_ASSERT(fbSz.x > 0.05f && fbSz.y > 0.05f && fbSz.z > 0.05f,
        "El AABB delimitador del módulo en fallback tiene dimensiones nulas");
    MonsterSDF exact=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&exact,MonsterAger_GetResultConst(&ager),config.sdfConfig),
        "No se compiló el campo exacto para certificar propiedad de módulos");
    MorphFallbackRegion regions[MORPH_MAX_FALLBACK_REGIONS];
    size_t regionCount=MonsterVisualAsync_GetFallbackRegions(visual,&exact,
        &MonsterAger_GetResultConst(&ager)->anatomyGraph,
        .75f,.05f,regions,MORPH_MAX_FALLBACK_REGIONS);
    TEST_ASSERT(regionCount>=5,"Las patas y la cola se fusionaron en una sola región");
    MonsterSDFConnector* filtered=malloc(exact.connectorCount*sizeof(*filtered));
    TEST_ASSERT(filtered,"No se reservó campo SDF por módulo");
    for(size_t i=0;i<regionCount;++i) {
        Vector3 size=AABB_Size(regions[i].bounds);
        TEST_ASSERT(size.x>0&&size.y>0&&size.z>0,
            "La región local tiene volumen nulo");
        for(size_t j=0;j<i;++j)
            TEST_ASSERT(regions[i].kind!=regions[j].kind||
                regions[i].moduleId!=regions[j].moduleId,
                "Dos regiones reclamaron el mismo módulo");
        if(regions[i].kind!=MORPH_FALLBACK_MODULE)continue;
        MonsterSDF moduleView;
        TEST_ASSERT(MonsterSDF_BuildModuleView(&exact,regions[i].moduleId,
            &moduleView,filtered,exact.connectorCount),
            "No se pudo aislar el campo SDF del módulo");
        TEST_ASSERT(moduleView.bodyPartCount==0&&moduleView.mouthCount==0&&
            moduleView.axialStationCount==0&&moduleView.connectorCount<exact.connectorCount,
            "El campo local todavía contiene el cuerpo completo");
        for(size_t c=0;c<moduleView.connectorCount;++c)
            TEST_ASSERT(moduleView.connectors[c].moduleInstanceId==regions[i].moduleId,
                "El campo local incluye conectores de otro módulo");
    }
    MonsterSDF axialView,headView;
    TEST_ASSERT(MonsterSDF_BuildAxialView(&exact,&axialView)&&
        axialView.axialStationCount>1&&axialView.mouthCount==0&&
        axialView.connectorCount==0&&axialView.bodyPartCount==0,
        "El fallback axial conserva módulos ajenos al tronco");
    TEST_ASSERT(MonsterSDF_BuildHeadView(&exact,&headView)&&
        headView.mouthCount>0&&headView.axialStationCount==0&&
        headView.connectorCount==0&&headView.bodyPartCount==0,
        "El fallback cefálico conserva el cuerpo completo");
    size_t masked=0;
    const Mesh* owned=MonsterVisualAsync_GetOwnedMesh(visual,regions,regionCount,&masked);
    TEST_ASSERT(owned==&visual->displayMesh&&masked==0&&
        owned->indexCount==visual->displayMesh.indexCount,
        "La cesión eliminó triángulos completos antes del recorte GPU");
    MonsterSDF removedModules=exact;
    removedModules.connectorCount=0;
    removedModules.connectorNodes=NULL;
    removedModules.connectorNodeCount=0;
    MorphFallbackRegion absent[MORPH_MAX_FALLBACK_REGIONS];
    size_t absentCount=MonsterVisualAsync_GetFallbackRegions(visual,&removedModules,
        &MonsterAger_GetResultConst(&ager)->anatomyGraph,
        .75f,.05f,absent,MORPH_MAX_FALLBACK_REGIONS);
    size_t absentModules=0;
    for(size_t i=0;i<absentCount;++i)
        if(absent[i].kind==MORPH_FALLBACK_MODULE) {
            TEST_ASSERT(absent[i].representation==MORPH_REP_ABSENT,
                "Un módulo desaparecido intentaría renderizar un SDF vacío");
            ++absentModules;
        }
    TEST_ASSERT(absentModules>=5,
        "La desaparición de miembros no retiró la representación anterior");
    free(filtered);
    MonsterSDF_Free(&exact);
    MonsterVisualAsync_Flush(visual);
    for(int step=0;step<20;++step)
        MonsterVisualAsync_UpdateHandoffs(visual,.75f,.05f,.016f);
    MonsterVisualAsyncStats finished=MonsterVisualAsync_GetStats(visual);
    TEST_ASSERT(fabsf(finished.certifiedAge-.75f)<.001f&&
        !MonsterVisualAsync_NeedsExactFallback(visual,.75f,.05f)&&
        finished.processedBrickCount>0&&finished.activeDirtyBrickCount==0&&
        finished.bodyOpposedTriangles<100&&
        finished.bodyDegenerateTriangles==0&&
        finished.bodyBoundaryEdges==0,
        "El worker publicó conectividad corporal plegada tras la espera");
    MonsterVisualAsync_SetWorkerDelay(visual,0);
    MonsterAger_SetPerc(&ager,1.0f);
    MonsterVisualAsync_UpdateWithAppearance(visual,
        MonsterAger_GetGeometryResultConst(&ager),
        MonsterAger_GetResultConst(&ager),.016f);
    MonsterVisualAsync_Flush(visual);
    MonsterVisualAsyncStats endpoint=MonsterVisualAsync_GetStats(visual);
    TEST_ASSERT(fabsf(endpoint.certifiedAge-1.0f)<.001f&&
        endpoint.bodyOpposedTriangles<100&&
        endpoint.bodyDegenerateTriangles==0&&endpoint.bodyBoundaryEdges==0&&
        visual->displayMesh.vertexCount!=initialVertices,
        "La pata final no recibió la conectividad exacta del SDF");
    MonsterVisualAsync_Free(visual);
    MonsterAger_Free(&ager);
    Monster_Free(&dog);Monster_Free(&lizard);
    printf("[PASS] test_morph_async_delayed_topology\n");
}

static void test_morph_conflicting_module_ids(void) {
    Monster dog=Monster_Create(),lizard=Monster_Create();
    CreaturePhenotype dp=CreatureRecipes_Dog()->adult,
        lp=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog,CreatureRecipes_Dog(),&dp)&&
        Creature_BuildMonster(&lizard,CreatureRecipes_Lizard(),&lp),
        "No se construyeron los extremos de la prueba de identidades");
    AnatomyId conflicted=0;uint32_t oldModule=0;
    for(size_t i=0;i<lizard.anatomyGraph.nodeCount;++i) {
        AnatomyNode* node=&lizard.anatomyGraph.nodes[i];
        const AnatomyNode* peer=AnatomyGraph_FindNode(&dog.anatomyGraph,node->id);
        if(!peer||node->region!=ANATOMY_REGION_DIGIT)continue;
        conflicted=node->id;oldModule=node->moduleInstanceId;
        node->role=node->role==ANATOMY_ROLE_DIGIT?
            ANATOMY_ROLE_JOINT:ANATOMY_ROLE_DIGIT;
        break;
    }
    TEST_ASSERT(conflicted&&AnatomyGraph_Validate(&lizard.anatomyGraph),
        "No se generó una colisión semántica válida");
    CreatureMorphTopologyEvent events[ANATOMY_MAX_NODES];
    size_t eventCount=CreatureMorph_CollectTopologyEvents(&dog,&lizard,
        events,ANATOMY_MAX_NODES);
    TEST_ASSERT(eventCount>0&&eventCount<ANATOMY_MAX_NODES,
        "No se detectó el cambio de blueprint entre módulos");
    Monster one=Monster_Create(),two=Monster_Create();
    TEST_ASSERT(CreatureMorph_Interpolate(&dog,&lizard,.3f,&one)&&
        CreatureMorph_Interpolate(&dog,&lizard,.7f,&two),
        "Una colisión de ID entre recetas bloqueó el grafo unión");
    TEST_ASSERT(AnatomyGraph_TopologyCompatible(&one.anatomyGraph,&two.anatomyGraph),
        "La traducción de módulos depende de la edad de morfología");
    const AnatomyNode* inherited=AnatomyGraph_FindNode(&one.anatomyGraph,conflicted);
    bool foundRemapped=false;
    for(size_t i=0;i<one.anatomyGraph.nodeCount;++i) {
        const AnatomyNode* node=&one.anatomyGraph.nodes[i];
        if(node->moduleInstanceId!=oldModule&&
           node->localNodeId==(uint16_t)conflicted&&
           node->region==ANATOMY_REGION_DIGIT)foundRemapped=true;
    }
    TEST_ASSERT(inherited&&foundRemapped,
        "La identidad en conflicto no conservó ambos módulos semánticos");
    size_t reverseCount=CreatureMorph_CollectTopologyEvents(&lizard,&dog,
        events,ANATOMY_MAX_NODES);
    TEST_ASSERT(reverseCount==eventCount,
        "Los eventos topológicos dependen del sentido del recorrido");
    Monster_Free(&one);Monster_Free(&two);Monster_Free(&dog);Monster_Free(&lizard);
    printf("[PASS] test_morph_conflicting_module_ids\n");
}

static unsigned Morph_CountVisibleDigits(const Mesh* mesh, float scale) {
    float lo = 0, hi = 1;
    for (unsigned i = 0; i < 24; ++i) {
        float t = (lo + hi) * 0.5f;
        CreaturePhenotype p = CreaturePhenotype_Interpolate(&CreatureRecipes_Lizard()->juvenile,&CreatureRecipes_Lizard()->adult, t);
        if (p.axial.totalScale < scale) lo = t; else hi = t;
    }
    CreaturePhenotype p = CreaturePhenotype_Interpolate(&CreatureRecipes_Lizard()->juvenile,&CreatureRecipes_Lizard()->adult, (lo + hi) * 0.5f);
    AnatomyGraph graph;
    if (!Creature_ResolveAnatomy(CreatureRecipes_Lizard(),&p, &graph)) return 0;
    const unsigned formula[2][5] = {{2, 3, 4, 5, 3}, {2, 3, 4, 5, 4}};
    unsigned visible = 0;
    for (unsigned limb = 0; limb < 4; ++limb) {
        for (unsigned digit = 0; digit < 5; ++digit) {
            const AnatomyNode* n = AnatomyGraph_FindNode(&graph,
                Anatomy_MakeId(10+(limb),Limb_DigitLocalId( digit, formula[limb / 2][digit] + 1)));
            if (!n) continue;
            float nearest = 1e6f;
            for (size_t v = 0; v < mesh->vertexCount; ++v) {
                nearest = fminf(nearest, Vec3_Distance(n->center, mesh->vertices[v].position));
            }
            bool ok = nearest <= n->widthRadius * 1.8f;
            visible += ok;
        }
    }
    return visible;
}

static void test_lizard_morph_lifecycle(void) {
    AnatomyDeformer* morph = AnatomyDeformer_Create();
    TEST_ASSERT(morph != NULL, "AnatomyDeformer_Create debe retornar un puntero no nulo");
    TEST_ASSERT(!AnatomyDeformer_IsBound(morph), "Un morph recién creado no debe estar vinculado");
    TEST_ASSERT(AnatomyDeformer_GetVertexCount(morph) == 0, "Conteo de vértices inicial debe ser 0");

    Mesh emptyMesh = Mesh_Create();
    AnatomyGraph emptyGraph;
    AnatomyGraph_Init(&emptyGraph);

    TEST_ASSERT(!AnatomyDeformer_Bind(morph, &emptyMesh, &emptyGraph), "No debe vincular malla vacía");
    TEST_ASSERT(!AnatomyDeformer_Deform(morph, &emptyGraph, &emptyMesh), "No debe deformar sin vinculación");

    Mesh_Free(&emptyMesh);
    AnatomyDeformer_Free(morph);
    printf("[PASS] test_lizard_morph_lifecycle\n");
}

static void test_morph_incremental_reanchor(void) {
    Monster dog=Monster_Create();
    CreaturePhenotype phenotype=CreatureRecipes_Dog()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog,CreatureRecipes_Dog(),&phenotype),
        "No se construyó la anatomía del reanclaje incremental");
    Mesh oldMesh=Mesh_Create(),newMesh=Mesh_Create();
    for(size_t i=0;i<10;++i) {
        MeshVertex vertex={.position=dog.anatomyGraph.nodes[i].center,
            .normal={0,1,0},.color=COLOR_WHITE,.material=SDF_MATERIAL_SKIN};
        TEST_ASSERT(Mesh_AddVertex(&oldMesh,vertex,NULL),
            "No se preparó la malla de reanclaje");
    }
    AnatomyDeformer* deformer=AnatomyDeformer_Create();
    TEST_ASSERT(deformer&&AnatomyDeformer_Bind(deformer,&oldMesh,&dog.anatomyGraph),
        "No se vinculó la referencia del reanclaje");
    AnatomyId retainedConnectionId=dog.anatomyGraph.connections[
        deformer->bindings[7].connIndex[0]].id;
    uint32_t origin[12]={7,3,0,9,2,6,1,5,4,8,UINT32_MAX,UINT32_MAX};
    for(size_t i=0;i<12;++i) {
        MeshVertex vertex=i<10?oldMesh.vertices[origin[i]]:oldMesh.vertices[i-10];
        if(i>=10)vertex.position=Vec3_Add(vertex.position,Vec3_Create(.03f,0,.02f));
        TEST_ASSERT(Mesh_AddVertex(&newMesh,vertex,NULL),
            "No se preparó la malla cosida de reanclaje");
    }
    AnatomyGraph reordered=dog.anatomyGraph;
    BodyConnection swap=reordered.connections[0];
    reordered.connections[0]=reordered.connections[1];
    reordered.connections[1]=swap;
    TEST_ASSERT(AnatomyGraph_Validate(&reordered)&&
        AnatomyGraph_TopologyCompatible(&dog.anatomyGraph,&reordered),
        "El grafo permutado dejó de ser compatible");
    size_t newlyBound=0;
    TEST_ASSERT(AnatomyDeformer_Reanchor(deformer,&newMesh,&reordered,origin,
        &newlyBound)&&newlyBound==2,
        "El reanclaje no conservó las diez vinculaciones existentes");
    Vector3 expected[12];
    for(size_t i=0;i<12;++i)expected[i]=newMesh.vertices[i].position;
    TEST_ASSERT(AnatomyDeformer_Deform(deformer,&reordered,&newMesh),
        "El grafo permutado impidió deformar la malla reanclada");
    for(size_t i=0;i<12;++i)
        TEST_ASSERT(Vec3_Distance(expected[i],newMesh.vertices[i].position)<1e-4f,
            "El reanclaje desplazó un vértice en su propia pose de referencia");
    TEST_ASSERT(reordered.connections[deformer->bindings[0].connIndex[0]].id==
        retainedConnectionId,
        "La permutación del grafo cambió la conexión semántica conservada");
    AnatomyDeformer_Free(deformer);
    Mesh_Free(&oldMesh);Mesh_Free(&newMesh);Monster_Free(&dog);
    printf("[PASS] test_morph_incremental_reanchor\n");
}

static void test_lizard_morph_sweep_and_perf(void) {
    Monster m = Monster_Create();
    CreaturePhenotype juvP = CreatureRecipes_Lizard()->juvenile;
    TEST_ASSERT(Creature_BuildMonster(&m,CreatureRecipes_Lizard(), &juvP), "Construcción de lagarto juvenil");

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, &m, MonsterSDF_DefaultConfig()), "Construcción SDF");
    SDFField field = MonsterSDF_GetField(&sdf);

    SDFMesher mesher = SDFMesher_Create(SDFMesher_DefaultConfig());
    mesher.config.adaptiveDetail = true;
    mesher.config.voxelSize = 0.10f;
    mesher.config.maxCells = 250000;
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
    size_t rCount = MonsterSDF_GetDetailRegions(&sdf, 3.5f, regions, MONSTER_SDF_DETAIL_REGION_CAPACITY);

    Mesh baseMesh = Mesh_Create();
    TEST_ASSERT(SDFMesher_GenerateMeshDetailed(&mesher, &field, regions, rCount, &baseMesh),
                "Generación de malla base adaptativa");
    TEST_ASSERT(baseMesh.vertexCount > 5000, "La malla base debe tener geometría sustancial");

    AnatomyDeformer* morph = AnatomyDeformer_Create();
    double tBind0 = Morph_GetTimeMs();
    TEST_ASSERT(AnatomyDeformer_Bind(morph, &baseMesh, &m.anatomyGraph), "Vinculación de malla base");
    double tBind = Morph_GetTimeMs() - tBind0;
    TEST_ASSERT(AnatomyDeformer_IsBound(morph), "El deformador debe quedar marcado como vinculado");
    TEST_ASSERT(AnatomyDeformer_GetVertexCount(morph) == baseMesh.vertexCount, "Conteo de vértices debe coincidir");
    printf("  [debug] AnatomyDeformer bind: %zu vértices en %.1f ms\n", baseMesh.vertexCount, tBind);

    Mesh morphMesh = Mesh_Create();
    morphMesh.vertices = (MeshVertex*)malloc(baseMesh.vertexCount * sizeof(MeshVertex));
    memcpy(morphMesh.vertices, baseMesh.vertices, baseMesh.vertexCount * sizeof(MeshVertex));
    morphMesh.vertexCount = baseMesh.vertexCount;
    morphMesh.vertexCapacity = baseMesh.vertexCount;
    morphMesh.indices = (MeshIndex*)malloc(baseMesh.indexCount * sizeof(MeshIndex));
    memcpy(morphMesh.indices, baseMesh.indices, baseMesh.indexCount * sizeof(MeshIndex));
    morphMesh.indexCount = baseMesh.indexCount;
    morphMesh.indexCapacity = baseMesh.indexCount;

    MorphSurfaceTracker tracker={0};
    TEST_ASSERT(MorphSurfaceTracker_Bind(&tracker,&baseMesh,.25f),
                "Reserva de superficie temporal");
    tracker.corrections[0]=Vec3_Create(.10f,0,0);
    tracker.correctedAge=.15f;
    m.hasGrowthAge=true;m.growthAge=.25f;
    TEST_ASSERT(MorphSurfaceTracker_Predict(&tracker,morph,NULL,&m,&morphMesh),
                "Predicción sobre edad de referencia");
    TEST_ASSERT(Vec3_Distance(morphMesh.vertices[0].position,
                baseMesh.vertices[0].position)<.0001f,
                "La corrección histórica debe desaparecer sobre la malla exacta");
    m.growthAge=.20f;
    TEST_ASSERT(MorphSurfaceTracker_Predict(&tracker,morph,NULL,&m,&morphMesh),
                "Transporte de corrección hacia referencia");
    TEST_ASSERT(fabsf(morphMesh.vertices[0].position.x-
                baseMesh.vertices[0].position.x-.05f)<.0001f,
                "La corrección debe decaer proporcionalmente a la edad");
    MorphSurfaceTracker_Free(&tracker);

    /* Barrido completo de ontogenia 0.0 -> 1.0 */
    float ages[] = {0.0f, 0.15f, 0.35f, 0.50f, 0.70f, 0.85f, 1.0f};
    for (size_t i = 0; i < sizeof(ages) / sizeof(ages[0]); ++i) {
        float age = ages[i];
        CreaturePhenotype targetPheno = CreaturePhenotype_Interpolate(&CreatureRecipes_Lizard()->juvenile,&CreatureRecipes_Lizard()->adult, age);
        AnatomyGraph targetGraph;
        TEST_ASSERT(Creature_ResolveAnatomy(CreatureRecipes_Lizard(),&targetPheno, &targetGraph), "Resolución de anatomía destino");

        double tDef0 = Morph_GetTimeMs();
        TEST_ASSERT(AnatomyDeformer_Deform(morph, &targetGraph, &morphMesh), "Deformación O(V) en tiempo real");
        double tDef = Morph_GetTimeMs() - tDef0;

        /* El presupuesto se verifica en release; instrumentar accesos de memoria
         * cambia el coste por vértice y no mide el rendimiento de producción. */
#ifndef MONSTER_TEST_INSTRUMENTED
        TEST_ASSERT(tDef < 15.0, "La deformación debe ejecutarse en menos de 15 ms para soportar 60 FPS");
#endif

        unsigned visible = Morph_CountVisibleDigits(&morphMesh, targetPheno.axial.totalScale);
        printf("  [debug] AnatomyDeformer edad %.2f: %.2f ms, dígitos=%u/20\n", age, tDef, visible);
        TEST_ASSERT(visible == 20, "Los 20 dígitos deben permanecer visibles tras la deformación continua");
    }

    Mesh_Free(&baseMesh);
    Mesh_Free(&morphMesh);
    AnatomyDeformer_Free(morph);
    SDFMesher_Free(&mesher);
    MonsterSDF_Free(&sdf);
    Monster_Free(&m);
    printf("[PASS] test_lizard_morph_sweep_and_perf\n");
}

static void test_lizard_morph_eye_and_jaw_growth(void) {
    Monster young = Monster_Create();
    CreaturePhenotype pYoung = CreatureRecipes_Lizard()->juvenile;
    TEST_ASSERT(Creature_BuildMonster(&young,CreatureRecipes_Lizard(), &pYoung), "Construcción lagarto joven");

    Monster adult = Monster_Create();
    CreaturePhenotype pAdult = CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&adult,CreatureRecipes_Lizard(), &pAdult), "Construcción lagarto adulto");

    MonsterAger ager = MonsterAger_Create(&young, &adult, 0.0f);
    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    MonsterVisualAsync* asyncMgr = MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(asyncMgr != NULL, "Creación de MonsterVisualAsync");

    /* Paso 1: Generación base juvenil */
    const Monster* cur0 = MonsterAger_GetResultConst(&ager);
    MonsterVisualAsync_Update(asyncMgr, cur0, 0.016f);
    MonsterVisualAsync_Flush(asyncMgr);

    TEST_ASSERT(MonsterVisualAsync_GetDisplayEyeCount(asyncMgr) == 2, "Debe tener 2 ojos en display");
    TEST_ASSERT(MonsterVisualAsync_GetDisplayMouthCount(asyncMgr) == 1, "Debe tener 1 boca en display");

    const Mesh* eye0 = MonsterVisualAsync_GetDisplayEyeSclera(asyncMgr, 0);
    const Mesh* jaw0 = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 0);
    TEST_ASSERT(eye0 != NULL && eye0->vertexCount > 0, "Malla de esclerótica ojo 0");
    TEST_ASSERT(jaw0 != NULL && jaw0->vertexCount > 0, "Malla de mandíbula 0");

    Vector3 eyeMin0 = eye0->vertices[0].position, eyeMax0 = eye0->vertices[0].position;
    for (size_t i = 1; i < eye0->vertexCount; ++i) {
        eyeMin0.x = fminf(eyeMin0.x, eye0->vertices[i].position.x);
        eyeMin0.y = fminf(eyeMin0.y, eye0->vertices[i].position.y);
        eyeMin0.z = fminf(eyeMin0.z, eye0->vertices[i].position.z);
        eyeMax0.x = fmaxf(eyeMax0.x, eye0->vertices[i].position.x);
        eyeMax0.y = fmaxf(eyeMax0.y, eye0->vertices[i].position.y);
        eyeMax0.z = fmaxf(eyeMax0.z, eye0->vertices[i].position.z);
    }
    Vector3 jawMin0 = jaw0->vertices[0].position, jawMax0 = jaw0->vertices[0].position;
    for (size_t i = 1; i < jaw0->vertexCount; ++i) {
        jawMin0.x = fminf(jawMin0.x, jaw0->vertices[i].position.x);
        jawMin0.y = fminf(jawMin0.y, jaw0->vertices[i].position.y);
        jawMin0.z = fminf(jawMin0.z, jaw0->vertices[i].position.z);
        jawMax0.x = fmaxf(jawMax0.x, jaw0->vertices[i].position.x);
        jawMax0.y = fmaxf(jawMax0.y, jaw0->vertices[i].position.y);
        jawMax0.z = fmaxf(jawMax0.z, jaw0->vertices[i].position.z);
    }
    float eyeWidth0 = eyeMax0.x - eyeMin0.x;
    float jawLength0 = jawMax0.z - jawMin0.z;
    float eyeCenterX0 = (eyeMin0.x + eyeMax0.x) * 0.5f;

    /* Paso 2: MORPH conserva los anexos juveniles hasta publicar el adulto. */
    Vector3 oldEye=eye0->vertices[0].position,oldJaw=jaw0->vertices[0].position;
    uint64_t oldGeneration=MonsterVisualAsync_GetDisplayGeneration(asyncMgr);
    MonsterVisualAsync_SetMorphMode(asyncMgr, true);
    MonsterAger_SetPerc(&ager, 1.0f);
    const Monster* adultCur = MonsterAger_GetResultConst(&ager);
    MonsterVisualAsync_Update(asyncMgr, adultCur, 0.016f);
    TEST_ASSERT(MonsterVisualAsync_GetDisplayGeneration(asyncMgr)==oldGeneration,
        "La solicitud todavía no debe haber publicado la malla adulta");
    TEST_ASSERT(Vec3_Distance(oldEye,eye0->vertices[0].position)<1e-6f&&
                Vec3_Distance(oldJaw,jaw0->vertices[0].position)<1e-6f,
        "MORPH mezcló anexos adultos con el cuerpo juvenil");
    MonsterVisualAsync_Flush(asyncMgr);
    TEST_ASSERT(MonsterVisualAsync_GetDisplayGeneration(asyncMgr)>oldGeneration,
        "El adulto debe publicarse antes de comprobar su crecimiento");

    const Mesh* eye1 = MonsterVisualAsync_GetDisplayEyeSclera(asyncMgr, 0);
    const Mesh* jaw1 = MonsterVisualAsync_GetDisplayMouthMesh(asyncMgr, 0, 0);
    Vector3 eyeMin1 = eye1->vertices[0].position, eyeMax1 = eye1->vertices[0].position;
    for (size_t i = 1; i < eye1->vertexCount; ++i) {
        eyeMin1.x = fminf(eyeMin1.x, eye1->vertices[i].position.x);
        eyeMin1.y = fminf(eyeMin1.y, eye1->vertices[i].position.y);
        eyeMin1.z = fminf(eyeMin1.z, eye1->vertices[i].position.z);
        eyeMax1.x = fmaxf(eyeMax1.x, eye1->vertices[i].position.x);
        eyeMax1.y = fmaxf(eyeMax1.y, eye1->vertices[i].position.y);
        eyeMax1.z = fmaxf(eyeMax1.z, eye1->vertices[i].position.z);
    }
    Vector3 jawMin1 = jaw1->vertices[0].position, jawMax1 = jaw1->vertices[0].position;
    for (size_t i = 1; i < jaw1->vertexCount; ++i) {
        jawMin1.x = fminf(jawMin1.x, jaw1->vertices[i].position.x);
        jawMin1.y = fminf(jawMin1.y, jaw1->vertices[i].position.y);
        jawMin1.z = fminf(jawMin1.z, jaw1->vertices[i].position.z);
        jawMax1.x = fmaxf(jawMax1.x, jaw1->vertices[i].position.x);
        jawMax1.y = fmaxf(jawMax1.y, jaw1->vertices[i].position.y);
        jawMax1.z = fmaxf(jawMax1.z, jaw1->vertices[i].position.z);
    }
    float eyeWidth1 = eyeMax1.x - eyeMin1.x;
    float jawLength1 = jawMax1.z - jawMin1.z;
    float eyeCenterX1 = (eyeMin1.x + eyeMax1.x) * 0.5f;

    /* Verificaciones de crecimiento anatómico */
    TEST_ASSERT(eyeWidth1 > eyeWidth0 * 1.35f, "El ojo debe crecer más de un 35% en anchura");
    TEST_ASSERT(eyeCenterX1 > eyeCenterX0 * 1.50f, "El ojo debe desplazarse lateralmente con el ensanchamiento craneal");
    TEST_ASSERT(jawLength1 > jawLength0 * 1.50f, "La mandíbula debe crecer más de un 50% en longitud con el hocico adulto");

    MonsterVisualAsync_Free(asyncMgr);
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);
    printf("[PASS] test_lizard_morph_eye_and_jaw_growth\n");
}

/* El campo directo debe contener las mismas piezas orales que el renderer
 * de mallas, incluso al desaparecer y al cambiar la articulación. */
static void test_visual_sdf_posed_mouth_bounds(void) {
    const float ages[]={0,.25f,.5f,1};
    const float openings[]={0,.1f,1};
    Monster first=Monster_Create(),last=Monster_Create();
    CreaturePhenotype a=CreatureRecipes_Lizard()->larva,b=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&first,CreatureRecipes_Lizard(),&a)&&Creature_BuildMonster(&last,CreatureRecipes_Lizard(),&b),"Extremos visuales inválidos");
    MonsterAger ager=MonsterAger_Create(&first,&last,0);
    MonsterSDF sdf=MonsterSDF_Create();
    for(size_t age=0;age<4;++age)for(size_t opening=0;opening<3;++opening) {
        MonsterAger_SetPerc(&ager,ages[age]);Monster* monster=MonsterAger_GetResult(&ager);
        Monster_SetHeadOpenFactor(monster,openings[opening]);
        TEST_ASSERT(MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig()),"Falló el snapshot visual");
        for(size_t i=0;i<monster->mouthCount;++i) {
            MonsterVisualMouth meshes={0};
            TEST_ASSERT(MonsterVisual_BuildMouthMeshesFromSDF(&meshes,&monster->mouths[i],monster,&sdf,i),"Falló la referencia oral");
            const Mesh* parts[]={&meshes.jaw,&meshes.hinge};
            for(size_t part=0;part<2;++part)for(size_t v=0;v<parts[part]->vertexCount;++v) {
                Vector3 p=parts[part]->vertices[v].position;
                TEST_ASSERT(AABB_ContainsPoint(sdf.mouths[i].visualBounds,p),"La caja visual recorta una pieza oral articulada");
                float distance=MonsterSDF_EvaluateVisualDistance(&sdf,p);
                TEST_ASSERT(isfinite(distance)&&distance<.06f,"El campo visual pierde superficie de la referencia oral");
            }
            MonsterVisualMouth_Free(&meshes);
        }
    }
    MonsterSDF_Free(&sdf);MonsterAger_Free(&ager);Monster_Free(&first);Monster_Free(&last);
    printf("[PASS] test_visual_sdf_posed_mouth_bounds\n");
}

/* Tres segmentos compiten por la misma piel. El cambio del segundo vecino
 * no debe abrir una costura cuando dos vértices están casi juntos. */
static void test_morph_continuous_binding(void) {
    AnatomyGraph a,b;AnatomyGraph_Init(&a);
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<2;++j) {
            AnatomyNode n={.id=1+i*2+j,.center={(float)i-1,0,(float)j*2},
                .widthRadius=1,.heightRadius=1,.role=ANATOMY_ROLE_JOINT};
            TEST_ASSERT(AnatomyGraph_AddNode(&a,n),"Nodo de prueba válido");
        }
        TEST_ASSERT(AnatomyGraph_Connect(&a,(BodyConnection){.id=i+1,.fromId=i*2+1,.toId=i*2+2,.kind=BODY_CONNECTION_LIMB_SEGMENT}),"Segmento de prueba válido");
    }
    b=a;b.nodes[0].center.y+=1;b.nodes[1].center.y+=1;
    Mesh mesh=Mesh_Create();Mesh_ReserveVertices(&mesh,2);mesh.vertexCount=2;
    mesh.vertices[0]=(MeshVertex){.position={-.00001f,1,1},.normal={0,1,0}};
    mesh.vertices[1]=(MeshVertex){.position={ .00001f,1,1},.normal={0,1,0}};
    AnatomyDeformer* d=AnatomyDeformer_Create();
    TEST_ASSERT(AnatomyDeformer_Bind(d,&mesh,&a),"Vinculación continua");
    TEST_ASSERT(AnatomyDeformer_Deform(d,&b,&mesh),"Deformación continua");
    TEST_ASSERT(Vec3_Distance(mesh.vertices[0].position,mesh.vertices[1].position)<.001f,"Cambiar vecinos no desgarra la piel");
    AnatomyDeformer_Free(d);Mesh_Free(&mesh);
    printf("[PASS] test_morph_continuous_binding\n");
}

static void test_morph_spatial_connector_candidates(void) {
    Monster dog=Monster_Create(),lizard=Monster_Create();
    CreaturePhenotype dp=CreatureRecipes_Dog()->adult,lp=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog,CreatureRecipes_Dog(),&dp),"Perro de candidatos");
    TEST_ASSERT(Creature_BuildMonster(&lizard,CreatureRecipes_Lizard(),&lp),"Lagarto de candidatos");
    MonsterAger ager=MonsterAger_Create(&dog,&lizard,0);
    MonsterSDF sdf=MonsterSDF_Create();unsigned seed=97131;
    const float ages[]={.05f,.25f,.75f};
    for(size_t age=0;age<3;++age) {
        MonsterAger_SetPerc(&ager,ages[age]);
        TEST_ASSERT(MonsterSDF_Build(&sdf,MonsterAger_GetResultConst(&ager),
            MonsterSDF_DefaultConfig()),"Compilación de candidatos");
        Vector3 extent=AABB_Size(sdf.bounds);
        for(unsigned boxIndex=0;boxIndex<300;++boxIndex) {
            float u[3];for(int axis=0;axis<3;++axis) {
                seed=seed*1664525u+1013904223u;
                u[axis]=(seed>>8)*(1.0f/16777216.0f);
            }
            Vector3 center=Vec3_Add(sdf.bounds.start,
                Vec3_Create(extent.x*u[0],extent.y*u[1],extent.z*u[2]));
            float radius=.01f+.10f*(boxIndex%11)/10;
            AABB3D box={Vec3_Sub(center,Vec3_Create(radius,radius,radius)),
                Vec3_Add(center,Vec3_Create(radius,radius,radius))};
            uint16_t candidates[ANATOMY_MAX_CONNECTIONS];
            size_t count=MonsterSDF_CompileConnectorCandidates(&sdf,box,candidates,
                ANATOMY_MAX_CONNECTIONS);
            for(unsigned sample=0;sample<8;++sample) {
                float v[3];for(int axis=0;axis<3;++axis) {
                    seed=seed*1664525u+1013904223u;
                    v[axis]=(seed>>8)*(1.0f/16777216.0f);
                }
                Vector3 p=Vec3_Add(box.start,Vec3_Create(v[0]*2*radius,
                    v[1]*2*radius,v[2]*2*radius));
                float full=MonsterSDF_EvaluateDistance(&sdf,p);
                float fast=MonsterSDF_EvaluateDistanceCandidates(&sdf,p,candidates,count);
                TEST_ASSERT(fabsf(full-fast)<.00002f,"Los candidatos espaciales cambiaron el SDF");
            }
        }
    }
    MonsterSDF_Free(&sdf);MonsterAger_Free(&ager);Monster_Free(&dog);Monster_Free(&lizard);
    printf("[PASS] test_morph_spatial_connector_candidates\n");
}

static void test_morph_exact_sdf_appearance_age(void) {
    Monster dog=Monster_Create(),lizard=Monster_Create();
    CreaturePhenotype dp=CreatureRecipes_Dog()->adult,
        lp=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog,CreatureRecipes_Dog(),&dp)&&
        Creature_BuildMonster(&lizard,CreatureRecipes_Lizard(),&lp),
        "No se prepararon los extremos para la apariencia implícita");
    MonsterAger ager=MonsterAger_Create(&dog,&lizard,0);
    MonsterSDF sdf=MonsterSDF_Create();
    const float ages[]={0,.25f,.5f,.75f,1};
    float firstColor=-1,lastColor=-1;
    for(size_t i=0;i<sizeof(ages)/sizeof(ages[0]);++i) {
        MonsterAger_SetPerc(&ager,ages[i]);
        const Monster* visual=MonsterAger_GetResultConst(&ager);
        TEST_ASSERT(MonsterSDF_Build(&sdf,visual,MonsterSDF_DefaultConfig())&&
            sdf.hasSurface,"El SDF exacto perdió la apariencia visual");
        SurfaceRecipe expected=SurfaceRecipe_CompileScaled(&visual->surface,
            visual->surfaceMapping.unitScale);
        for(size_t row=0;row<SURFACE_RECIPE_ROWS;++row)
            for(int channel=0;channel<4;++channel)
                TEST_ASSERT(fabsf(sdf.surfaceRecipe.data[row][channel]-
                    expected.data[row][channel])<1e-6f,
                    "El SDF usó material de otra edad");
        TEST_ASSERT(sdf.surfaceRecipe.pigmentSeed==expected.pigmentSeed&&
            sdf.surfaceRecipe.scaleSeed==expected.scaleSeed&&
            sdf.surfaceRecipe.furSeed==expected.furSeed,
            "El SDF perdió identidad procedural de pigmento o pelaje");
        if(i==0)firstColor=sdf.surfaceRecipe.data[0][0];
        if(i==sizeof(ages)/sizeof(ages[0])-1)
            lastColor=sdf.surfaceRecipe.data[0][0];
    }
    TEST_ASSERT(fabsf(firstColor-lastColor)>.05f,
        "La apariencia implícita quedó anclada a una edad antigua");
    MonsterSDF_Free(&sdf);MonsterAger_Free(&ager);
    Monster_Free(&dog);Monster_Free(&lizard);
    printf("[PASS] test_morph_exact_sdf_appearance_age\n");
}

static void test_morph_anchored_frozen_vertices(void) {
    AnatomyGraph gRest, gMoved;
    AnatomyGraph_Init(&gRest);
    AnatomyGraph_Init(&gMoved);

    AnatomyNode n0 = {
        .id = Anatomy_MakeId(10, 1),
        .moduleInstanceId = 10,
        .localNodeId = 1,
        .center = {0, 0, 0},
        .widthRadius = 0.2f, .heightRadius = 0.2f,
        .development = 1.0f
    };
    AnatomyNode n1 = {
        .id = Anatomy_MakeId(10, 2),
        .moduleInstanceId = 10,
        .localNodeId = 2,
        .center = {0, -1.0f, 0},
        .widthRadius = 0.2f, .heightRadius = 0.2f,
        .development = 1.0f
    };
    BodyConnection conn = {
        .id = 1,
        .fromId = Anatomy_MakeId(10, 1),
        .toId = Anatomy_MakeId(10, 2),
        .kind = BODY_CONNECTION_LIMB_SEGMENT,
        .moduleInstanceId = 10,
        .development = 1.0f
    };

    TEST_ASSERT(AnatomyGraph_AddNode(&gRest, n0) && AnatomyGraph_AddNode(&gRest, n1), "Añadir nodos rest");
    TEST_ASSERT(AnatomyGraph_Connect(&gRest, conn), "Conectar rest");

    AnatomyNode n0m = n0; n0m.center = Vec3_Create(1.0f, 2.0f, 0.0f);
    AnatomyNode n1m = n1; n1m.center = Vec3_Create(1.0f, 1.0f, 0.0f);
    TEST_ASSERT(AnatomyGraph_AddNode(&gMoved, n0m) && AnatomyGraph_AddNode(&gMoved, n1m), "Añadir nodos moved");
    TEST_ASSERT(AnatomyGraph_Connect(&gMoved, conn), "Conectar moved");

    Mesh meshRest = Mesh_Create();
    TEST_ASSERT(Mesh_ReserveVertices(&meshRest, 1), "Reservar vértice");
    meshRest.vertexCount = 1;
    meshRest.vertices[0] = (MeshVertex){
        .position = {0.0f, -0.5f, 0.1f},
        .normal = {0.0f, 0.0f, 1.0f}
    };

    AnatomyDeformer* def = AnatomyDeformer_Create();
    TEST_ASSERT(AnatomyDeformer_Bind(def, &meshRest, &gRest), "Bind deformer");

    Mesh meshTarget = Mesh_Create();
    TEST_ASSERT(Mesh_ReserveVertices(&meshTarget, 1), "Reservar target");
    meshTarget.vertexCount = 1;
    meshTarget.vertices[0] = meshRest.vertices[0];

    uint8_t frozenMask[1] = {1};

    TEST_ASSERT(AnatomyDeformer_AnchorFrozenVertices(def, &gRest, &meshRest, &meshTarget, frozenMask), "Anchor rest");
    TEST_ASSERT(Vec3_Distance(meshTarget.vertices[0].position, meshRest.vertices[0].position) < 1e-6f,
        "Anclaje en misma pose alteró la posición");

    TEST_ASSERT(AnatomyDeformer_AnchorFrozenVertices(def, &gMoved, &meshRest, &meshTarget, frozenMask), "Anchor moved");
    Vector3 expectedPos = Vec3_Add(meshRest.vertices[0].position, Vec3_Create(1.0f, 2.0f, 0.0f));
    TEST_ASSERT(Vec3_Distance(meshTarget.vertices[0].position, expectedPos) < 1e-4f,
        "Anclaje no siguió el desplazamiento del miembro");

    AnatomyDeformer_Free(def);
    Mesh_Free(&meshRest);
    Mesh_Free(&meshTarget);
    printf("[PASS] test_morph_anchored_frozen_vertices\n");
}

void run_morph_tests(void) {
    test_morph_module_certification();
    test_morph_async_delayed_topology();
    test_morph_conflicting_module_ids();
    test_morph_spatial_connector_candidates();
    test_morph_exact_sdf_appearance_age();
    test_morph_continuous_binding();
    test_morph_anchored_frozen_vertices();
    test_visual_sdf_posed_mouth_bounds();
    test_lizard_morph_lifecycle();
    test_morph_incremental_reanchor();
    test_lizard_morph_sweep_and_perf();
    test_lizard_morph_eye_and_jaw_growth();
}
