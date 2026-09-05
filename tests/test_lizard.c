#include "test_utils.h"
#include "Lizard.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "SDFPrimitives.h"
#include "Mesh.h"
#include "MathUtils.h"
#include <math.h>

static bool mirrored_nodes(const AnatomyGraph* graph, AnatomyId left, AnatomyId right) {
    const AnatomyNode* a=AnatomyGraph_FindNode(graph,left);
    const AnatomyNode* b=AnatomyGraph_FindNode(graph,right);
    return a&&b&&fabsf(a->center.x+b->center.x)<.001f&&
        fabsf(a->center.y-b->center.y)<.001f&&fabsf(a->center.z-b->center.z)<.001f;
}

static size_t outgoing_digits(const AnatomyGraph* graph, AnatomyId hand) {
    size_t count=0;
    for(size_t i=0;i<graph->connectionCount;++i)
        if(graph->connections[i].fromId==hand&&
           graph->connections[i].kind==BODY_CONNECTION_DIGIT_SEGMENT)++count;
    return count;
}

static size_t mesh_find_root(size_t* parent,size_t index) {
    while(parent[index]!=index) {
        parent[index]=parent[parent[index]];
        index=parent[index];
    }
    return index;
}

static size_t mesh_component_count(const Mesh* mesh) {
    if(!mesh||mesh->vertexCount==0)return 0;
    size_t* parent=(size_t*)malloc(mesh->vertexCount*sizeof(size_t));
    bool* used=(bool*)calloc(mesh->vertexCount,sizeof(bool));
    TEST_ASSERT(parent&&used,"Sin memoria para auditar componentes de malla");
    for(size_t i=0;i<mesh->vertexCount;++i)parent[i]=i;
    for(size_t i=0;i+2<mesh->indexCount;i+=3) {
        size_t a=mesh->indices[i],b=mesh->indices[i+1],c=mesh->indices[i+2];
        if(a>=mesh->vertexCount||b>=mesh->vertexCount||c>=mesh->vertexCount)continue;
        used[a]=used[b]=used[c]=true;
        size_t rootA=mesh_find_root(parent,a),rootB=mesh_find_root(parent,b);
        if(rootA!=rootB)parent[rootB]=rootA;
        rootA=mesh_find_root(parent,a);
        size_t rootC=mesh_find_root(parent,c);
        if(rootA!=rootC)parent[rootC]=rootA;
    }
    size_t count=0;
    for(size_t i=0;i<mesh->vertexCount;++i)
        if(used[i]&&mesh_find_root(parent,i)==i)++count;
    free(parent);free(used);
    return count;
}

static void test_lizard_graph_topology(void) {
    LizardPhenotype p=LizardPreset_Adult(); AnatomyGraph graph;
    TEST_ASSERT(Lizard_ResolveAnatomy(&p,&graph),"No se resolvió el grafo del lagarto");
    TEST_ASSERT(AnatomyGraph_Validate(&graph),"El grafo anatómico no es válido");
    TEST_ASSERT(graph.nodeCount==47&&graph.connectionCount==46,"La topología estable cambió inesperadamente");
    TEST_ASSERT(AnatomyGraph_HasConnection(&graph,ANATOMY_ID_PECTORAL,ANATOMY_ID_FORE_LEFT_SHOULDER),"Falta la rama anterior izquierda");
    TEST_ASSERT(AnatomyGraph_HasConnection(&graph,ANATOMY_ID_PECTORAL,ANATOMY_ID_FORE_RIGHT_SHOULDER),"Falta la rama anterior derecha");
    TEST_ASSERT(AnatomyGraph_HasConnection(&graph,ANATOMY_ID_PELVIS,ANATOMY_ID_HIND_LEFT_HIP),"Falta la rama posterior izquierda");
    TEST_ASSERT(AnatomyGraph_HasConnection(&graph,ANATOMY_ID_PELVIS,ANATOMY_ID_HIND_RIGHT_HIP),"Falta la rama posterior derecha");
    TEST_ASSERT(!AnatomyGraph_HasConnection(&graph,ANATOMY_ID_FORE_LEFT_HAND,ANATOMY_ID_FORE_RIGHT_SHOULDER),"Las extremidades izquierda y derecha quedaron cruzadas");
    TEST_ASSERT(mirrored_nodes(&graph,ANATOMY_ID_FORE_LEFT_ELBOW,ANATOMY_ID_FORE_RIGHT_ELBOW),"Los miembros anteriores no son espejados");
    TEST_ASSERT(mirrored_nodes(&graph,ANATOMY_ID_HIND_LEFT_KNEE,ANATOMY_ID_HIND_RIGHT_KNEE),"Los miembros posteriores no son espejados");
    TEST_ASSERT(outgoing_digits(&graph,ANATOMY_ID_FORE_LEFT_HAND)==5&&
                outgoing_digits(&graph,ANATOMY_ID_FORE_RIGHT_HAND)==5&&
                outgoing_digits(&graph,ANATOMY_ID_HIND_LEFT_FOOT)==5&&
                outgoing_digits(&graph,ANATOMY_ID_HIND_RIGHT_FOOT)==5,"Cada mano o pie debe tener cinco dedos");
    const AnatomyNode* shoulder=AnatomyGraph_FindNode(&graph,ANATOMY_ID_FORE_LEFT_SHOULDER);
    const AnatomyNode* elbow=AnatomyGraph_FindNode(&graph,ANATOMY_ID_FORE_LEFT_ELBOW);
    const AnatomyNode* thorax=AnatomyGraph_FindNode(&graph,ANATOMY_ID_THORAX_ANTERIOR);
    const AnatomyNode* hand=AnatomyGraph_FindNode(&graph,ANATOMY_ID_FORE_LEFT_HAND);
    TEST_ASSERT(shoulder&&elbow&&thorax&&hand&&elbow->center.x>shoulder->center.x,"El húmero no se proyecta lateralmente");
    TEST_ASSERT(hand->center.y<thorax->center.y,"La mano no alcanza el plano inferior del torso");

    /* El orden de almacenamiento se mezcla a propósito: sólo la arista manda. */
    Monster explicitMonster=Monster_Create(); AnatomyGraph_Init(&explicitMonster.anatomyGraph);
    TEST_ASSERT(AnatomyGraph_AddNode(&explicitMonster.anatomyGraph,(AnatomyNode){501,Vec3_Create(0,0,0),.4f,.2f,0,ANATOMY_ROLE_AXIAL})&&
                AnatomyGraph_AddNode(&explicitMonster.anatomyGraph,(AnatomyNode){503,Vec3_Create(8,0,0),.3f,.2f,0,ANATOMY_ROLE_AXIAL})&&
                AnatomyGraph_AddNode(&explicitMonster.anatomyGraph,(AnatomyNode){502,Vec3_Create(0,0,-2),.3f,.2f,0,ANATOMY_ROLE_AXIAL})&&
                AnatomyGraph_Connect(&explicitMonster.anatomyGraph,(BodyConnection){900,501,502,BODY_CONNECTION_AXIAL_LOFT}),"No se pudo preparar el grafo desordenado");
    explicitMonster.hasAnatomyGraph=true;
    MonsterSDF explicitSdf=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&explicitSdf,&explicitMonster,MonsterSDF_DefaultConfig())&&explicitSdf.connectorCount==1&&
                explicitSdf.connectors[0].fromId==501&&explicitSdf.connectors[0].toId==502&&explicitSdf.connectors[0].b.z==-2.0f,"MonsterSDF infirió una conexión por índices consecutivos");
    MonsterSDF_Free(&explicitSdf); Monster_Free(&explicitMonster);
    printf("[PASS] test_lizard_graph_topology\n");
}

static void test_lizard_axial_loft_and_tail(void) {
    Monster monster=Monster_Create(); LizardPhenotype p=LizardPreset_Adult();
    TEST_ASSERT(Lizard_BuildMonster(&monster,&p),"No se construyó el lagarto adulto");
    MonsterSDF sdf=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"No se compiló el loft axial");
    TEST_ASSERT(sdf.bodyPartCount==0,"Los nodos anatómicos no deben compilarse como elipsoides visibles");
    TEST_ASSERT(sdf.connectorCount==monster.anatomyGraph.connectionCount,"El SDF no usa todas las aristas explícitas");
    for(size_t i=0;i<sdf.connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf.connectors[i];
        TEST_ASSERT(c->fromId!=0&&c->toId!=0,"Una conexión anatómica perdió sus IDs");
        if(c->kind==BODY_CONNECTION_AXIAL_LOFT) {
            Vector3 midpoint=Vec3_Lerp(c->a,c->b,.5f);
            TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,midpoint)<0.0f,"El loft axial contiene una discontinuidad");
        }
    }
    AnatomyId tail[]={ANATOMY_ID_TAIL_BASE,ANATOMY_ID_TAIL_MIDDLE,ANATOMY_ID_TAIL_DISTAL,ANATOMY_ID_TAIL_TIP};
    float previous=1e6f;
    for(size_t i=0;i<4;++i) {
        const AnatomyNode* node=AnatomyGraph_FindNode(&monster.anatomyGraph,tail[i]);
        TEST_ASSERT(node&&isfinite(node->widthRadius)&&isfinite(node->heightRadius)&&node->widthRadius>0&&node->heightRadius>0,"Estación axial inválida");
        TEST_ASSERT(node->widthRadius<previous,"La cola no se ahúsa monótonamente"); previous=node->widthRadius;
    }
    TEST_ASSERT(AnatomyGraph_HasConnection(&monster.anatomyGraph,ANATOMY_ID_PELVIS,ANATOMY_ID_TAIL_BASE),"La cola no está conectada a la pelvis");

    MonsterSDFMouth before=sdf.mouths[0];
    monster.bodyParts[0].widthRender*=3.0f; monster.bodyParts[0].heightRender*=.25f; monster.bodyParts[0].lengthRender*=2.0f;
    TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"Falló recompilar tras mutar el anfitrión heredado");
    TEST_ASSERT(sdf.bodyPartCount==0,"El anfitrión heredado reapareció como segundo volumen cefálico");
    TEST_ASSERT(Vec3_Distance(before.craniumCenterLocal,sdf.mouths[0].craniumCenterLocal)<.001f&&
                Vec3_Distance(before.craniumRadii,sdf.mouths[0].craniumRadii)<.001f,"El elipsoide anfitrión volvió a gobernar la cabeza anatómica");
    MonsterSDF_Free(&sdf); Monster_Free(&monster);
    printf("[PASS] test_lizard_axial_loft_and_tail\n");
}

static void test_lizard_head_semantics(void) {
    HeadPhenotype shallow=HeadPhenotype_LizardPreset(),deep=shallow;
    shallow.jawDepth=.15f; deep.jawDepth=.90f;
    HeadAnatomy a,b;
    TEST_ASSERT(HeadAnatomy_Resolve(&shallow,0,Vec3_Create(1,.6f,1.1f),&a)&&
                HeadAnatomy_Resolve(&deep,0,Vec3_Create(1,.6f,1.1f),&b),"No se resolvieron variantes de mandíbula");
    TEST_ASSERT(fabsf(a.surface.faceRootRadii.y-b.surface.faceRootRadii.y)<.001f&&
                fabsf(a.surface.faceTipRadii.y-b.surface.faceTipRadii.y)<.001f,"La profundidad rostral aún depende de jawDepth");
    TEST_ASSERT(a.surface.craniumRadii.x>a.surface.faceTipRadii.x,"El cráneo posterior no es más ancho que el rostro distal");
    TEST_ASSERT(a.landmarks.muzzleTip.z>a.landmarks.muzzleRoot.z,"El extremo del rostro no está delante de la raíz");
    float globeFraction=a.eyeScale.y/a.surface.orbitRadii.y;
    TEST_ASSERT(globeFraction>=.70f&&globeFraction<=.92f,"El globo no ocupa una fracción plausible de la órbita");
    float eyeExposure=Vec3_Dot(Vec3_Sub(a.landmarks.leftEyeCenter,a.landmarks.leftOrbit),a.landmarks.leftOrbitNormal);
    TEST_ASSERT(eyeExposure>0.0f&&eyeExposure<a.eyeScale.y*.25f,"La exposición ocular no está controlada por la profundidad del globo");
    float lateralFraction=fabsf(a.landmarks.leftOrbit.x-a.surface.craniumCenter.x)/a.surface.craniumRadii.x;
    TEST_ASSERT(lateralFraction>=.72f&&lateralFraction<=.84f,"La órbita de lagarto sigue en el extremo lateral del cráneo");
    TEST_ASSERT(a.surface.orbitRadii.y>=a.eyeScale.y*1.12f,"El socket no deja margen anatómico alrededor del globo");
    TEST_ASSERT(fabsf(a.landmarks.leftOrbitNormal.x+a.landmarks.rightOrbitNormal.x)<.001f&&
                fabsf(a.landmarks.leftOrbitNormal.z-a.landmarks.rightOrbitNormal.z)<.001f,"Las normales orbitales no son espejadas");
    TEST_ASSERT(a.landmarks.leftTympanum.z<a.landmarks.leftOrbit.z&&
                a.landmarks.leftTympanum.x>0,"El tímpano no está detrás y lateral a la órbita");
    TEST_ASSERT(a.landmarks.leftJawHinge.z<a.landmarks.leftMouthCorner.z,"La bisagra no está detrás de la comisura");
    TEST_ASSERT(a.landmarks.leftMouthCorner.z<a.landmarks.muzzleTip.z,"La comisura quedó delante de la punta rostral");
    TEST_ASSERT(a.landmarks.leftOrbit.y>a.landmarks.skullCenter.y+a.surface.craniumRadii.y*.35f,"La órbita no es dorsolateral");
    TEST_ASSERT(a.surface.leftBrowCenter.x>0&&a.surface.rightBrowCenter.x<0&&
                fabsf(a.surface.leftBrowCenter.x+a.surface.rightBrowCenter.x)<.001f,"Las crestas supraorbitales no son bilaterales");
    TEST_ASSERT(a.surface.faceRootRadii.x>a.surface.faceMidRadii.x&&
                a.surface.faceMidRadii.x>a.surface.faceTipRadii.x,"El rostro no se ahúsa raíz -> nasal -> premaxila");
    float linearMid=Math_Lerp(a.surface.faceRoot.y,a.surface.faceTip.y,.52f);
    TEST_ASSERT(fabsf(a.surface.faceTip.y-a.surface.faceRoot.y)>a.surface.faceRootRadii.y*.10f&&
                a.surface.faceMid.y>linearMid,"El perfil dorsal del rostro sigue siendo plano o lineal");
    TEST_ASSERT(a.landmarks.leftNostril.z>a.surface.faceMid.z&&a.landmarks.leftNostril.x>a.surface.faceTipRadii.x*.45f,
                "La narina no está en el rostro anterior dorsolateral");
    TEST_ASSERT(a.oralSystem.jawPivot.z==a.landmarks.leftJawHinge.z,"La articulación oral no usa el landmark posterior");
    float voxel=HeadAnatomy_RecommendedVoxelSize(&a);
    float smallest=Math_Min(Math_Min(a.surface.nostrilRadii.x,a.surface.nostrilRadii.y),a.surface.tympanumRadii.y);
    TEST_ASSERT(isfinite(voxel)&&voxel>0&&2.0f*smallest/voxel>=3.0f,"La regla visual deja una característica bajo tres vóxeles");

    Monster monster=Monster_Create(); LizardPhenotype p=LizardPreset_Adult();
    TEST_ASSERT(Lizard_BuildMonster(&monster,&p),"No se construyó el preset para comprobar ojos");
    TEST_ASSERT(monster.eyeCount==2&&Vec3_Dot(monster.eyes[0].forward,monster.head.anatomy.landmarks.leftOrbitNormal)>.999f&&
                Vec3_Dot(monster.eyes[1].forward,monster.head.anatomy.landmarks.rightOrbitNormal)>.999f,"La orientación visual no sigue la normal orbital");
    Monster_Free(&monster);

    TEST_ASSERT(SDF_RoundedTaperedWedge(Vec3_Create(0,0,.5f),Vec3_Zero(),Vec3_Create(0,0,1),.5f,.2f,.3f,.15f,.05f)<0.0f,"La cuña redondeada no contiene su eje");
    TEST_ASSERT(SDF_RoundedTaperedWedge(Vec3_Create(1,0,.5f),Vec3_Zero(),Vec3_Create(0,0,1),.5f,.2f,.3f,.15f,.05f)>0.0f,"La cuña redondeada acepta un punto exterior");
    TEST_ASSERT(SDF_ThreeSectionEllipticalLoftApprox(Vec3_Create(0,.04f,1.0f),Vec3_Zero(),Vec3_Create(0,.08f,1),Vec3_Create(0,0,2),Vec3_Create(.6f,.3f,.18f),Vec3_Create(.4f,.22f,.16f),Vec3_Create(.22f,.16f,.12f),.04f)<0.0f,
                "El loft anatómico de tres secciones no contiene su eje curvo");
    printf("[PASS] test_lizard_head_semantics\n");
}

static void test_lizard_local_head_field_quality(void) {
    const bool stages[]={false,true};
    for(size_t stage=0;stage<2;++stage) {
        Monster monster=Monster_Create();LizardPhenotype p=stages[stage]?LizardPreset_Adult():LizardPreset_Juvenile();
        TEST_ASSERT(Lizard_BuildMonster(&monster,&p),"No se construyó una etapa para el campo local cefálico");
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig())&&sdf.hasPartitionedHead,
                    "La cabeza anatómica no activó la partición local");
        TEST_ASSERT(sdf.mouths[0].headBodySmoothness<sdf.mouths[0].muzzleSmoothness*.55f,
                    "La unión cabeza/cuerpo sigue reutilizando mouthSmoothness");
        MonsterSDFBodyField bodyContext;MonsterSDFHeadField headContext;
        SDFField body=MonsterSDF_GetBodyField(&sdf,&bodyContext),head=MonsterSDF_GetHeadField(&sdf,0,&headContext);
        Vector3 host=monster.bodyParts[monster.head.anatomy.attachmentBodyPartIndex].positionRender;
        Vector3 skull=Vec3_Add(host,monster.head.anatomy.landmarks.skullCenter);
        TEST_ASSERT(body.evaluateDistance(body.context,skull)>0.0f&&head.evaluateDistance(head.context,skull)<0.0f,
                    "Cuerpo grueso y cabeza local todavía duplican la superficie craneal");
        Vector3 collarTip=Vec3_Add(sdf.mouths[0].center,sdf.mouths[0].neckCollarTipLocal);
        TEST_ASSERT(body.evaluateDistance(body.context,collarTip)<0.0f&&head.evaluateDistance(head.context,collarTip)<0.0f,
                    "El collar no termina enterrado dentro del cuello corporal");

        SDFMesherConfig bodyCfg=SDFMesher_DefaultConfig();bodyCfg.voxelSize=.06f;bodyCfg.maxCells=850000;bodyCfg.maxResolution=192;
        SDFMesherConfig headCfg=SDFMesher_DefaultConfig();headCfg.voxelSize=HeadAnatomy_RecommendedVoxelSize(&monster.head.anatomy);headCfg.maxCells=2200000;headCfg.maxResolution=384;
        SDFMesher bodyMesher=SDFMesher_Create(bodyCfg),headMesher=SDFMesher_Create(headCfg);Mesh bodyMesh=Mesh_Create(),headMesh=Mesh_Create();
        TEST_ASSERT(SDFMesher_GenerateMesh(&bodyMesher,&body,&bodyMesh)&&SDFMesher_GenerateMesh(&headMesher,&head,&headMesh),
                    "Falló la extracción separada de cuerpo/cabeza");
        TEST_ASSERT(Mesh_KeepLargestComponent(&headMesh),"No se pudo compactar la cabeza local");
        const SDFMesherStats* bs=SDFMesher_GetLastStats(&bodyMesher);const SDFMesherStats* hs=SDFMesher_GetLastStats(&headMesher);
        MeshValidationResult hv=Mesh_Validate(&headMesh);
        size_t headComponents=mesh_component_count(&headMesh);
        printf("  [debug] topología cabeza %s: válida=%d componentes=%zu bordes=%zu noManifold=%zu degenerados=%zu\n",
            stages[stage]?"adulta":"juvenil",hv.valid,headComponents,hv.boundaryEdgeCount,hv.nonManifoldEdgeCount,hv.degenerateTriangleCount);
        TEST_ASSERT(hv.valid&&hv.nonManifoldEdgeCount==0&&headComponents==1,
                    "La cabeza local es inválida, no-manifold o está fragmentada");
        TEST_ASSERT(hs->effectiveVoxelSize<bs->effectiveVoxelSize*.35f,
                    "La cabeza local no conserva una resolución materialmente mayor que el cuerpo");
        float smallest=Math_Min(monster.head.anatomy.surface.nostrilRadii.x,monster.head.anatomy.surface.nostrilRadii.y);
        TEST_ASSERT((2.0f*smallest)/hs->effectiveVoxelSize>=3.5f,
                    "La calidad settled no asigna varias muestras a la narina");
        TEST_ASSERT(hs->effectiveVoxelSize<=hs->requestedVoxelSize*1.001f||hs->cellBudgetAdjusted,
                    "El mesher coarsenizó la cabeza sin declararlo en estadísticas");
        size_t socketVertices=0,nostrilVertices=0;
        for(size_t i=0;i<headMesh.vertexCount;++i){if(headMesh.vertices[i].material==SDF_MATERIAL_EYE_SOCKET)socketVertices++;if(headMesh.vertices[i].material==SDF_MATERIAL_NOSTRIL)nostrilVertices++;}
        TEST_ASSERT(socketVertices>0&&nostrilVertices>0,"La malla local perdió sockets o narinas en la calidad real de Ager");
        printf("  [debug] %s cuerpo %dx%dx%d voxel=%.5f celdas=%zu ajuste=%d | cabeza %dx%dx%d voxel=%.5f celdas=%zu ajuste=%d bordes=%zu\n",
            stages[stage]?"adulto":"juvenil",bs->resolutionX,bs->resolutionY,bs->resolutionZ,bs->effectiveVoxelSize,bs->cellCount,bs->cellBudgetAdjusted,
            hs->resolutionX,hs->resolutionY,hs->resolutionZ,hs->effectiveVoxelSize,hs->cellCount,hs->cellBudgetAdjusted,hv.boundaryEdgeCount);
        Mesh_Free(&bodyMesh);Mesh_Free(&headMesh);SDFMesher_Free(&bodyMesher);SDFMesher_Free(&headMesher);MonsterSDF_Free(&sdf);Monster_Free(&monster);
    }
    printf("[PASS] test_lizard_local_head_field_quality\n");
}

static void test_lizard_tapered_jaw_has_closed_floor(void) {
    Monster monster=Monster_Create();LizardPhenotype p=LizardPreset_Adult();
    TEST_ASSERT(Lizard_BuildMonster(&monster,&p),"No se construyó el lagarto para auditar la mandíbula");
    MonsterSDF sdf=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig())&&sdf.mouthCount==1,
                "No se compiló la mandíbula del lagarto");
    const MonsterSDFMouth* mouth=&sdf.mouths[0];
    TEST_ASSERT(mouth->taperedMandible,"El preset perdió su mandíbula bilateral ahusada");
    MonsterSDFJawField jawContext;SDFField jaw=MonsterSDF_GetJawField(&sdf,0,&jawContext);
    float rx=mouth->jawRadii.x,ry=mouth->jawRadii.y,rz=mouth->jawRadii.z;
    Vector3 leftRear=Vec3_Add(mouth->hingeCenterLocal,Vec3_Create(rx*.76f,-ry*.18f,0));
    Vector3 leftTip=Vec3_Create(rx*.08f,mouth->jawCenterLocal.y+ry*.64f,mouth->jawCenterLocal.z+rz*.96f);
    for(unsigned i=0;i<=8;++i) {
        float t=(float)i/8.0f;Vector3 left=Vec3_Lerp(leftRear,leftTip,t);Vector3 right=left;right.x*=-1.0f;
        TEST_ASSERT(jaw.evaluateDistance(jaw.context,left)<0.0f&&jaw.evaluateDistance(jaw.context,right)<0.0f,
                    "Una rama mandibular contiene una perforación");
    }
    SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.voxelSize=.035f;cfg.maxCells=300000;cfg.maxResolution=144;
    SDFMesher mesher=SDFMesher_Create(cfg);Mesh mesh=Mesh_Create();
    TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&jaw,&mesh)&&mesh.vertexCount>0,
                "No se generó la malla mandibular de regresión");
    MeshValidationResult validation=Mesh_Validate(&mesh);
    AABB3D jawBounds=jaw.getBounds(jaw.context);Vector3 meshMin=mesh.vertices[0].position,meshMax=meshMin;
    for(size_t i=1;i<mesh.vertexCount;++i) {
        Vector3 v=mesh.vertices[i].position;
        if(v.x<meshMin.x)meshMin.x=v.x;
        if(v.y<meshMin.y)meshMin.y=v.y;
        if(v.z<meshMin.z)meshMin.z=v.z;
        if(v.x>meshMax.x)meshMax.x=v.x;
        if(v.y>meshMax.y)meshMax.y=v.y;
        if(v.z>meshMax.z)meshMax.z=v.z;
    }
    printf("  [debug] mandíbula cerrada: v=%zu comp=%zu boundary=%zu nonManifold=%zu bounds=[%.3f %.3f %.3f]-[%.3f %.3f %.3f] mesh=[%.3f %.3f %.3f]-[%.3f %.3f %.3f]\n",
           mesh.vertexCount,mesh_component_count(&mesh),validation.boundaryEdgeCount,validation.nonManifoldEdgeCount,
           jawBounds.start.x,jawBounds.start.y,jawBounds.start.z,jawBounds.end.x,jawBounds.end.y,jawBounds.end.z,
           meshMin.x,meshMin.y,meshMin.z,meshMax.x,meshMax.y,meshMax.z);
    TEST_ASSERT(validation.valid&&validation.manifold&&validation.watertight,
                "La mandíbula cerrada no es una superficie estanca y manifold");
    TEST_ASSERT(mesh_component_count(&mesh)==1,"La mandíbula contiene piezas desconectadas");
    for(size_t i=0;i<mesh.vertexCount;++i)
        TEST_ASSERT(mesh.vertices[i].material!=SDF_MATERIAL_MOUTH,
                    "La sustracción oral heredada volvió a perforar la mandíbula ahusada");
    Mesh_Free(&mesh);SDFMesher_Free(&mesher);MonsterSDF_Free(&sdf);Monster_Free(&monster);
    printf("[PASS] test_lizard_tapered_jaw_has_closed_floor\n");
}

static void test_lizard_aging_and_mesh_sweep(void) {
    Monster juvenile=Monster_Create(),adult=Monster_Create();
    LizardPhenotype jp=LizardPreset_Juvenile(),ap=LizardPreset_Adult();
    TEST_ASSERT(Lizard_BuildMonster(&juvenile,&jp)&&Lizard_BuildMonster(&adult,&ap),"No se construyeron los extremos de edad");
    TEST_ASSERT(juvenile.anatomyGraph.nodeCount==adult.anatomyGraph.nodeCount&&
                juvenile.anatomyGraph.connectionCount==adult.anatomyGraph.connectionCount,"Joven y adulto no comparten topología");
    for(size_t i=0;i<juvenile.anatomyGraph.nodeCount;++i)
        TEST_ASSERT(juvenile.anatomyGraph.nodes[i].id==adult.anatomyGraph.nodes[i].id,"Los IDs cambian con la edad");
    MonsterAger ager=MonsterAger_Create(&juvenile,&adult,0);
    const float ages[]={0,.25f,.50f,.75f,1};
    for(size_t i=0;i<sizeof(ages)/sizeof(ages[0]);++i) {
        MonsterAger_SetPerc(&ager,ages[i]); Monster* current=MonsterAger_GetResult(&ager);
        Monster_SetHeadOpenFactor(current,(float)i*.25f);
        TEST_ASSERT(current->hasAnatomyGraph&&current->anatomyGraph.nodeCount==47,"La interpolación perdió extremidades");
        TEST_ASSERT(HeadAnatomy_Validate(&current->head.anatomy)==HEAD_VALID,"Una edad intermedia produjo cabeza inválida");
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,current,MonsterSDF_DefaultConfig()),"Una edad intermedia produjo SDF inválido");
        SDFMesherConfig cfg=SDFMesher_DefaultConfig(); cfg.voxelSize=.18f; cfg.maxCells=300000;
        SDFMesher mesher=SDFMesher_Create(cfg); Mesh mesh=Mesh_Create(); SDFField field=MonsterSDF_GetField(&sdf);
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&mesh)&&mesh.vertexCount>0&&Mesh_Validate(&mesh).valid,"La malla completa de una edad o apertura es inválida");
        Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf);
    }
    TEST_ASSERT(adult.lizardPhenotype.tailLength>juvenile.lizardPhenotype.tailLength&&
                adult.head.phenotype.eyeSize<juvenile.head.phenotype.eyeSize,"Las proporciones ontogenéticas no cambian continuamente");

    const float fragileAges[]={.15f,.35f,.55f,.75f};
    for(size_t i=0;i<sizeof(fragileAges)/sizeof(fragileAges[0]);++i) {
        MonsterAger_SetPerc(&ager,fragileAges[i]);Monster* current=MonsterAger_GetResult(&ager);
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,current,MonsterSDF_DefaultConfig()),"No se compiló un estado visual frágil");
        SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.voxelSize=.06f;cfg.maxCells=850000;cfg.maxResolution=192;
        SDFMesher mesher=SDFMesher_Create(cfg);Mesh mesh=Mesh_Create();MonsterSDFBodyField bodyContext;
        SDFField field=MonsterSDF_GetBodyField(&sdf,&bodyContext);
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&mesh)&&Mesh_Validate(&mesh).valid,
                    "Una edad frágil produjo cuerpo inválido");
        size_t bodyComponents=mesh_component_count(&mesh),bodyVertices=mesh.vertexCount;
        TEST_ASSERT(bodyComponents==1,"Una edad frágil produjo partes corporales desconectadas");
        Mesh_Clear(&mesh);MonsterSDFHeadField headContext;field=MonsterSDF_GetHeadField(&sdf,0,&headContext);
        mesher.config.voxelSize=HeadAnatomy_RecommendedVoxelSize(&current->head.anatomy)*1.75f;
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&mesh)&&Mesh_KeepLargestComponent(&mesh)&&Mesh_Validate(&mesh).valid,
                    "Una edad frágil produjo cabeza local inválida");
        size_t headComponents=mesh_component_count(&mesh);
        printf("  [debug] edad frágil %.2f: cuerpo=%zu/%zu cabeza=%zu/%zu\n",fragileAges[i],bodyComponents,bodyVertices,headComponents,mesh.vertexCount);
        TEST_ASSERT(headComponents==1,"Una edad frágil produjo cabeza local fragmentada");
        Mesh_Free(&mesh);SDFMesher_Free(&mesher);MonsterSDF_Free(&sdf);
    }
    MonsterAger_Free(&ager); Monster_Free(&juvenile); Monster_Free(&adult);

    for(unsigned i=1;i<=96;++i) {
        LizardPhenotype p=LizardPreset_Adult(); float u=(float)(i%17)/16.0f;
        p.bodyFlattening=.30f+.70f*u; p.tailTaperCurve=.60f+1.90f*(1-u);
        p.forelimbLength=1.45f+1.20f*u; p.hindlimbLength=1.80f+1.35f*(1-u*.4f);
        p.head=HeadPhenotype_RandomValid(HEAD_ARCHETYPE_LIZARD,1000u+i);
        AnatomyGraph graph; HeadAnatomy head;
        TEST_ASSERT(Lizard_ResolveAnatomy(&p,&graph)&&AnatomyGraph_Validate(&graph),"Sweep corporal produjo topología inválida");
        TEST_ASSERT(HeadAnatomy_Resolve(&p.head,0,Vec3_Create(1,.6f,1.1f),&head)&&HeadAnatomy_Validate(&head)==HEAD_VALID,"Sweep de lagarto produjo cabeza inválida");
    }
    printf("[PASS] test_lizard_aging_and_mesh_sweep\n");
}

void run_lizard_tests(void) {
    printf("\n--- Módulo Anatomía de Lagarto ---\n");
    test_lizard_graph_topology();
    test_lizard_axial_loft_and_tail();
    test_lizard_head_semantics();
    test_lizard_tapered_jaw_has_closed_floor();
    test_lizard_local_head_field_quality();
    test_lizard_aging_and_mesh_sweep();
}
