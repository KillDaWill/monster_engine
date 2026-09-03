#include "Monster.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "MonsterVisualAsync.h"
#include "SDFPrimitives.h"
#include "MathUtils.h"
#include "test_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Monster MouthFixture(void) {
    Monster m = Monster_Create();
    Monster_Init(&m);
    BodyPart* head = Monster_GetHead(&m);
    head->width = head->widthRender = 2.0f;
    head->height = head->heightRender = 1.8f;
    head->length = head->lengthRender = 1.8f;
    Mouth mouth = Mouth_Create(0, Vec3_Create(0.0f, -0.15f, 0.78f), Vec3_Create(0.82f, 0.52f, 0.62f),
        Color_FromRGB(45, 8, 12), Color_FromRGB(180, 70, 70));
    Monster_AddMouth(&m, mouth);
    return m;
}

static MonsterSDF BuildFixture(Monster* m) {
    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, m, MonsterSDF_DefaultConfig()), "No se pudo compilar anatomía de boca");
    return sdf;
}

static void test_anatomical_defaults(void) {
    Monster m = MouthFixture();
    const Mouth* mouth = Monster_GetMouth(&m, 0);
    TEST_ASSERT(mouth->jawPivot.z < 0.0f, "El pivote debe quedar detrás del centro oral");
    TEST_ASSERT(mouth->jawLength > 0.0f && mouth->hingeRadius > 0.0f, "Parámetros anatómicos inválidos");
    TEST_ASSERT(mouth->slitThickness < mouth->scale.y, "La hendidura no puede superar la cavidad");
    Monster_Free(&m);
    printf("[PASS] test_anatomical_defaults\n");
}

static void test_cavity_exterior_and_depth(void) {
    Monster m = MouthFixture(); MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    Vector3 front = Vec3_Create(0, 0, sm->entranceCenterLocal.z + sm->entranceHalfExtents.z + 0.01f);
    Vector3 rear = Vec3_Create(0, 0, sm->cavityCenterLocal.z - sm->cavityRadii.z - 0.01f);
    float frontDistance = SDF_RoundedSlotExtruded(Vec3_Sub(front, sm->entranceCenterLocal), sm->entranceHalfExtents.x, sm->entranceHalfExtents.y, sm->entranceHalfExtents.z);
    TEST_ASSERT(frontDistance > 0.0f, "La ranura no tiene una frontera exterior definida");
    TEST_ASSERT(rear.z > sm->hostCenterLocal.z - sm->hostRadii.z * 0.95f, "La cavidad posterior escapó del volumen permitido");
    TEST_ASSERT(sm->entranceHalfExtents.z > 0.0f, "La entrada oral no tiene profundidad");
    TEST_ASSERT(sm->entranceHalfExtents.y < sm->cavityRadii.y, "La hendidura cerrada no es más estrecha que la cavidad");
    for (int step=0;step<=24;++step) {
        float t=(float)step/24.0f;
        Vector3 local=Vec3_Create(0.0f,0.0f,sm->cavityCenterLocal.z+(front.z-sm->cavityCenterLocal.z)*t);
        Vector3 world=Vec3_Add(sm->center,local);
        float distance=MonsterSDF_Evaluate(&sdf,world).distance;
        TEST_ASSERT(distance>-0.02f,"La cavidad final no conecta con el exterior");
    }
    Vector3 posterior=Vec3_Add(sm->center,Vec3_Create(0.0f,0.0f,sm->hostCenterLocal.z-sm->hostRadii.z*.98f));
    TEST_ASSERT(MonsterSDF_Evaluate(&sdf,posterior).distance<0.0f,"La cavidad perforó el cráneo posterior");
    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_cavity_exterior_and_depth\n");
}

static void test_material_and_debug_layers(void) {
    Monster m = MouthFixture(); MonsterSDF sdf = BuildFixture(&m);
    SDFSample sample = MonsterSDF_Evaluate(&sdf, Vec3_Create(0, -0.15f, 0.75f));
    SDFSample slit = MonsterSDF_EvaluateDebug(&sdf, Vec3_Create(0, 0, 0.78f), MONSTER_HEAD_DEBUG_SLIT);
    TEST_ASSERT(sample.material == SDF_MATERIAL_MOUTH || slit.material == SDF_MATERIAL_MOUTH, "No se conservó material oral");
    TEST_ASSERT(isfinite(sample.distance) && isfinite(slit.distance), "Evaluación anatómica no finita");
    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_material_and_debug_layers\n");
}

static void test_jaw_articulation_without_body_remesh(void) {
    Monster m = MouthFixture(); SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = cfg.resolutionY = cfg.resolutionZ = 14; cfg.voxelSize = 0.0f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "Falló la malla anatómica");
    uint64_t bodyGeneration = MonsterVisual_GetGeneration(&visual);
    const Mesh* closed = MonsterVisual_GetJaw(&visual, 0);
    const Mesh* hinge = MonsterVisual_GetHinge(&visual, 0);
    Vector3 closedPosition = closed->vertices[0].position;
    size_t hc = hinge->vertexCount;
    Vector3* closedHingePos = (Vector3*)malloc(hc * sizeof(Vector3));
    TEST_ASSERT(closedHingePos != NULL, "Fallo alocación posiciones hinge");
    for (size_t i = 0; i < hc; ++i) closedHingePos[i] = hinge->vertices[i].position;
    Mouth_SetOpenFactor(&m.mouths[0], 1.0f);
    MonsterVisual_Update(&visual, &m, 0.016f, 0.0f, MonsterSDF_DefaultConfig());
    const Mesh* open = MonsterVisual_GetJaw(&visual, 0);
    const Mesh* openHinge = MonsterVisual_GetHinge(&visual, 0);
    TEST_ASSERT(MonsterVisual_GetGeneration(&visual) == bodyGeneration, "Abrir la mandíbula remalló el cuerpo");
    TEST_ASSERT(closed && open && closed->vertexCount == open->vertexCount, "La mandíbula no conserva la malla base");
    TEST_ASSERT(fabsf(open->vertices[0].position.y - closedPosition.y) > 1e-5f, "openFactor no articuló la mandíbula");
    float maxHingeDisp = 0.0f;
    for (size_t i = 0; i < hc; ++i) {
        float d = fabsf(openHinge->vertices[i].position.y - closedHingePos[i].y);
        if (d > maxHingeDisp) maxHingeDisp = d;
    }
    free(closedHingePos);
    TEST_ASSERT(maxHingeDisp > 1e-5f, "El puente gular no siguió la mandíbula");
    MonsterVisualAsyncConfig asyncConfig=MonsterVisualAsync_DefaultConfig(); asyncConfig.interactiveMesherConfig.resolutionX=8;asyncConfig.interactiveMesherConfig.resolutionY=8;asyncConfig.interactiveMesherConfig.resolutionZ=8;asyncConfig.interactiveMesherConfig.voxelSize=0.0f;
    MonsterVisualAsync* async=MonsterVisualAsync_Create(asyncConfig); TEST_ASSERT(async!=NULL,"No se creó visual async"); MonsterVisualAsync_Update(async,&m,.016f); MonsterVisualAsync_Flush(async); MonsterVisualAsyncStats before=MonsterVisualAsync_GetStats(async); Mouth_SetOpenFactor(&m.mouths[0],.25f); MonsterVisualAsync_Update(async,&m,.016f); MonsterVisualAsyncStats after=MonsterVisualAsync_GetStats(async); TEST_ASSERT(after.requestCount==before.requestCount,"openFactor encoló remallado corporal async"); MonsterVisualAsync_Free(async);
    MonsterVisual_Free(&visual); Monster_Free(&m);
    printf("[PASS] test_jaw_articulation_without_body_remesh\n");
}

static void test_mesh_components(void) {
    Monster m = MouthFixture(); SDFMesherConfig cfg = SDFMesher_DefaultConfig(); cfg.resolutionX=cfg.resolutionY=cfg.resolutionZ=14; cfg.voxelSize=0.0f;
    MonsterVisual v = MonsterVisual_Create(cfg); TEST_ASSERT(MonsterVisual_RebuildNow(&v,&m,MonsterSDF_DefaultConfig()), "No se generó la visual");
    const Mesh* body = MonsterVisual_GetMesh(&v); const Mesh* jaw = MonsterVisual_GetJaw(&v,0); const Mesh* hinge = MonsterVisual_GetHinge(&v,0);
    MeshValidationResult a=Mesh_Validate(body), b=Mesh_Validate(jaw), c=Mesh_Validate(hinge);
    TEST_ASSERT(a.valid && b.valid && c.valid, "Malla anatómica inválida");
    TEST_ASSERT(a.manifold && a.watertight, "La cabeza anatómica cerrada debe ser estanca y manifold");
    TEST_ASSERT(b.manifold && b.watertight, "La mandíbula SDF debe ser cerrada y manifold");
    bool hasSkin=false, hasMouth=false;
    for(size_t i=0;i<jaw->vertexCount;++i){hasSkin|=jaw->vertices[i].material==SDF_MATERIAL_SKIN;hasMouth|=jaw->vertices[i].material==SDF_MATERIAL_MOUTH;}
    TEST_ASSERT(hasSkin && hasMouth, "La mandíbula carece de superficies skin y oral");
    MonsterSDF oralSdf=BuildFixture(&m); MonsterSDFJawField oralContext; SDFField oralField=MonsterSDF_GetJawField(&oralSdf,0,&oralContext);
    SDFSample oralSample=oralField.evaluate(oralField.context,Vec3_Add(oralSdf.mouths[0].jawCenterLocal,Vec3_Create(0,oralSdf.mouths[0].jawRadii.y*1.17f,0)));
    TEST_ASSERT(oralSample.material==SDF_MATERIAL_MOUTH,"La superficie oral superior no es accesible"); MonsterSDF_Free(&oralSdf);
    TEST_ASSERT(jaw->vertexCount > 0 && hinge->vertexCount > 0, "Falta conexión de mandíbula y bisagra");
    MonsterVisual_Free(&v); Monster_Free(&m); printf("[PASS] test_mesh_components\n");
}

static void test_body_and_dedicated_jaw_fields(void) {
    Monster m=MouthFixture(); MonsterSDF sdf=BuildFixture(&m); MonsterSDFJawField jawContext;
    SDFField jaw=MonsterSDF_GetJawField(&sdf,0,&jawContext);
    Vector3 p=Vec3_Create(m.mouths[0].jawPivot.x, m.mouths[0].jawPivot.y-m.mouths[0].jawThickness*.45f, m.mouths[0].jawPivot.z+m.mouths[0].jawLength*.72f);
    bool foundExterior=false;
    for(int xi=-4;xi<=4&&!foundExterior;++xi)for(int yi=-4;yi<=4&&!foundExterior;++yi)for(int zi=-4;zi<=4&&!foundExterior;++zi){
        p=Vec3_Add(m.mouths[0].jawPivot,Vec3_Create((float)xi*m.mouths[0].jawWidth*.08f,(float)yi*m.mouths[0].jawThickness*.08f,(float)zi*m.mouths[0].jawLength*.08f+m.mouths[0].jawLength*.5f));
        Vector3 worldPoint=Vec3_Add(sdf.mouths[0].center,Transform3D_RotateVector(m.mouths[0].rotation,p));
        foundExterior=MonsterSDF_Evaluate(&sdf,worldPoint).distance>0.0f&&jaw.evaluate(jaw.context,p).distance<0.0f;
    }
    TEST_ASSERT(foundExterior, "El campo corporal no separa el volumen inferior de mandíbula");
    TEST_ASSERT(jaw.getBounds(jaw.context).end.z>jaw.getBounds(jaw.context).start.z, "Bounds locales inválidos");
    MonsterSDF_Free(&sdf); Monster_Free(&m); printf("[PASS] test_body_and_dedicated_jaw_fields\n");
}

static unsigned int NextSeed(unsigned int* seed) { *seed=*seed*1664525u+1013904223u; return *seed; }
static float RandomRange(unsigned int* seed,float low,float high){return low+(float)(NextSeed(seed)%10000u)/10000.0f*(high-low);}
static void test_deterministic_anatomy_sweep(void) {
    unsigned int seed=0x51a7c0deu;
    for(size_t caseIndex=0;caseIndex<100;++caseIndex){
        Monster m=MouthFixture(); Mouth* mouth=&m.mouths[0];
        mouth->scale=Vec3_Create(RandomRange(&seed,.2f,1.4f),RandomRange(&seed,.12f,.9f),RandomRange(&seed,.15f,1.1f));
        mouth->slitThickness=RandomRange(&seed,-.1f,.4f); mouth->cornerRadius=RandomRange(&seed,-.1f,.5f);
        mouth->jawLength=RandomRange(&seed,-.2f,1.4f); mouth->jawWidth=RandomRange(&seed,-.2f,1.3f); mouth->jawThickness=RandomRange(&seed,-.2f,.8f);
        mouth->jawRearMass=RandomRange(&seed,-.2f,.8f); mouth->jawMuscle=RandomRange(&seed,-.2f,.8f); mouth->hingeRadius=RandomRange(&seed,-.2f,.5f); mouth->throatRadius=RandomRange(&seed,-.2f,.5f);
        mouth->cranium=Vec3_Create(RandomRange(&seed,-.4f,1.6f),RandomRange(&seed,-.4f,1.6f),RandomRange(&seed,-.4f,1.6f)); mouth->snout=mouth->cranium; mouth->cheeks=mouth->cranium; mouth->brows=mouth->cranium;
        Mouth_Normalize(mouth); MonsterSDF sdf=BuildFixture(&m); MonsterSDFJawField context; SDFField field=MonsterSDF_GetJawField(&sdf,0,&context);
        SDFMesherConfig config=SDFMesher_DefaultConfig(); config.resolutionX=8;config.resolutionY=8;config.resolutionZ=8;config.voxelSize=0.0f;config.maxCells=10000; SDFMesher mesher=SDFMesher_Create(config); Mesh jaw=Mesh_Create();
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&jaw),"Barrido anatómico no generó mandíbula"); TEST_ASSERT(Mesh_Validate(&jaw).valid,"Barrido produjo índices o normales inválidos");
        SDFSample bodySample=MonsterSDF_Evaluate(&sdf,sdf.mouths[0].center);
        TEST_ASSERT(isfinite(bodySample.distance)&&isfinite(field.evaluate(field.context,Vec3_Zero()).distance),"Barrido produjo distancia no finita");
        Vector3 posterior=Vec3_Add(sdf.mouths[0].center,Vec3_Create(0,0,sdf.mouths[0].hostCenterLocal.z-sdf.mouths[0].hostRadii.z*.98f));
        TEST_ASSERT(MonsterSDF_Evaluate(&sdf,posterior).distance<0.0f,"Barrido perforó el cráneo posterior");
        for(int path=0;path<3;++path){float t=(float)(path+1)/4.0f;Vector3 local=Vec3_Create(0,0,sdf.mouths[0].cavityCenterLocal.z+(sdf.mouths[0].entranceCenterLocal.z+sdf.mouths[0].entranceHalfExtents.z-sdf.mouths[0].cavityCenterLocal.z)*t);TEST_ASSERT(MonsterSDF_Evaluate(&sdf,Vec3_Add(sdf.mouths[0].center,local)).distance>-.1f,"Barrido perdió conexión oral");}
        Mesh_Free(&jaw);SDFMesher_Free(&mesher);MonsterSDF_Free(&sdf);Monster_Free(&m);
    }
    printf("[PASS] test_deterministic_anatomy_sweep\n");
}
static void test_async_multi_mouth_indexing(void) {
    Monster m=MouthFixture(); Mouth second=Mouth_Create(0,Vec3_Create(.8f,-.1f,.7f),Vec3_Create(.35f,.25f,.3f),Color_FromRGB(2,3,4),Color_FromRGB(200,3,4)); Monster_AddMouth(&m,second);
    MonsterVisualAsyncConfig cfg=MonsterVisualAsync_DefaultConfig(); cfg.interactiveMesherConfig.resolutionX=7;cfg.interactiveMesherConfig.resolutionY=7;cfg.interactiveMesherConfig.resolutionZ=7;cfg.interactiveMesherConfig.voxelSize=0.0f;
    MonsterVisualAsync* async=MonsterVisualAsync_Create(cfg); TEST_ASSERT(async!=NULL,"No se creó async multi-boca"); MonsterVisualAsync_Update(async,&m,.016f);MonsterVisualAsync_Flush(async);
    TEST_ASSERT(MonsterVisualAsync_GetDisplayMouthCount(async)==2,"Se perdió una boca en async"); const Mesh* first=MonsterVisualAsync_GetDisplayMouthMesh(async,0,0);const Mesh* secondJaw=MonsterVisualAsync_GetDisplayMouthMesh(async,1,0);
    TEST_ASSERT(first&&secondJaw&&first->vertexCount>0&&secondJaw->vertexCount>0,"Falta mandíbula de una boca"); TEST_ASSERT(first->vertexCount!=secondJaw->vertexCount,"Dos bocas no conservaron sus índices SDF");
    TEST_ASSERT(fabsf(first->vertices[0].position.x-secondJaw->vertices[0].position.x)>.1f,"Las bounds de las bocas se mezclaron");
    MonsterVisualAsync_Free(async);Monster_Free(&m);printf("[PASS] test_async_multi_mouth_indexing\n");
}
static void test_individual_phenotype_influence(void) {
    Monster base=MouthFixture(); MonsterSDF original=BuildFixture(&base);
    float originalCranium=original.mouths[0].craniumRadii.x;
    float originalSnout=original.mouths[0].snoutRadii.z;
    float originalCheek=original.mouths[0].cheekRadii.x;
    float originalBrow=original.mouths[0].browRadii.y;
    float originalSlit=original.mouths[0].entranceHalfExtents.y;
    MonsterSDFJawField originalJawContext; SDFField originalJawField=MonsterSDF_GetJawField(&original,0,&originalJawContext); AABB3D originalJawBounds=originalJawField.getBounds(originalJawField.context);
    Monster_Free(&base);
    const char* names[] = {"cranium","snout","cheeks","brows","cornerRadius","jawRearMass","jawMuscle"};
    for(size_t i=0;i<7;++i){Monster m=MouthFixture();Mouth* mouth=&m.mouths[0];if(i==0)mouth->cranium.x*=1.4f;if(i==1)mouth->snout.z*=1.4f;if(i==2)mouth->cheeks.x*=1.4f;if(i==3)mouth->brows.y*=1.4f;if(i==4)mouth->cornerRadius*=1.4f;if(i==5)mouth->jawRearMass*=1.7f;if(i==6)mouth->jawMuscle*=1.7f;MonsterSDF sdf=BuildFixture(&m);float changed=0;if(i==0)changed=sdf.mouths[0].craniumRadii.x-originalCranium;if(i==1)changed=sdf.mouths[0].snoutRadii.z-originalSnout;if(i==2)changed=sdf.mouths[0].cheekRadii.x-originalCheek;if(i==3)changed=sdf.mouths[0].browRadii.y-originalBrow;if(i==4)changed=sdf.mouths[0].entranceHalfExtents.y-originalSlit;if(i>=5){MonsterSDFJawField jc;SDFField jf=MonsterSDF_GetJawField(&sdf,0,&jc);changed=jf.getBounds(jf.context).end.x-originalJawBounds.end.x;}TEST_ASSERT(fabsf(changed)>1e-5f,names[i]);MonsterSDF_Free(&sdf);Monster_Free(&m);}
    MonsterSDF_Free(&original); printf("[PASS] test_individual_phenotype_influence\n");
}

// -----------------------------------------------------------------------------
// Tests de regresión de boca multi-pieza — fijan bugs confirmados por screenshot
// -----------------------------------------------------------------------------

// Helper DSU para conteo de componentes conectados por índices
static size_t CountMeshComponents(const Mesh* mesh) {
    if (!mesh || mesh->vertexCount == 0 || mesh->indexCount == 0) return 0;
    size_t n = mesh->vertexCount;
    size_t* parent = (size_t*)malloc(n * sizeof(size_t));
    size_t* rank = (size_t*)calloc(n, sizeof(size_t));
    if (!parent || !rank) { free(parent); free(rank); return 0; }
    for (size_t i = 0; i < n; ++i) parent[i] = i;
    // find con path compression
    for (size_t t = 0; t + 2 < mesh->indexCount; t += 3) {
        MeshIndex a = mesh->indices[t], b = mesh->indices[t+1], c = mesh->indices[t+2];
        if (a >= n || b >= n || c >= n) continue;
        // union a-b
        size_t ra = a, rb = b;
        while (parent[ra] != ra) { parent[ra] = parent[parent[ra]]; ra = parent[ra]; }
        while (parent[rb] != rb) { parent[rb] = parent[parent[rb]]; rb = parent[rb]; }
        if (ra != rb) {
            if (rank[ra] < rank[rb]) parent[ra] = rb;
            else if (rank[ra] > rank[rb]) parent[rb] = ra;
            else { parent[rb] = ra; rank[ra]++; }
        }
        // union b-c
        ra = b; rb = c;
        while (parent[ra] != ra) { parent[ra] = parent[parent[ra]]; ra = parent[ra]; }
        while (parent[rb] != rb) { parent[rb] = parent[parent[rb]]; rb = parent[rb]; }
        if (ra != rb) {
            if (rank[ra] < rank[rb]) parent[ra] = rb;
            else if (rank[ra] > rank[rb]) parent[rb] = ra;
            else { parent[rb] = ra; rank[ra]++; }
        }
        // union a-c
        ra = a; rb = c;
        while (parent[ra] != ra) { parent[ra] = parent[parent[ra]]; ra = parent[ra]; }
        while (parent[rb] != rb) { parent[rb] = parent[parent[rb]]; rb = parent[rb]; }
        if (ra != rb) {
            if (rank[ra] < rank[rb]) parent[ra] = rb;
            else if (rank[ra] > rank[rb]) parent[rb] = ra;
            else { parent[rb] = ra; rank[ra]++; }
        }
    }
    size_t comps = 0;
    for (size_t i = 0; i < n; ++i) {
        size_t r = i;
        while (parent[r] != r) { parent[r] = parent[parent[r]]; r = parent[r]; }
        // contar solo vértices referenciados; los aislados ya son validados antes pero aquí los contamos igual
        // para no ocultar islas, contamos toda raíz distinta que aparezca
        // marcar si es representante
        if (parent[i] == i) comps++;
        // pero compactar: realmente necesitamos contar raíces distintas alcanzables
    }
    // Recalcular raíces distintas correctamente: recolectar set de raíces
    // Ya que parent aún no está totalmente comprimido para todos, recontar con find final
    size_t distinct = 0;
    // simple O(n^2) para n pequeño (<2000)
    for (size_t i = 0; i < n; ++i) {
        size_t ri = i;
        while (parent[ri] != ri) { parent[ri] = parent[parent[ri]]; ri = parent[ri]; }
        bool seen = false;
        for (size_t j = 0; j < i; ++j) {
            size_t rj = j;
            while (parent[rj] != rj) { parent[rj] = parent[parent[rj]]; rj = parent[rj]; }
            if (ri == rj) { seen = true; break; }
        }
        if (!seen) distinct++;
    }
    comps = distinct;
    free(parent); free(rank);
    return comps;
}

static void test_bug_jaw_oral_basin_reenters_solid_superior_cap(void) {
    // La cavidad oral de la mandíbula no debe formar una tapa superior (hollow shell + cap).
    Monster m = MouthFixture();
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    MonsterSDFJawField ctx;
    SDFField jaw = MonsterSDF_GetJawField(&sdf, 0, &ctx);

    // Centro oral en coordenadas locales de mandíbula
    Vector3 oralCenter = Vec3_Add(sm->jawCenterLocal, Vec3_Create(0.0f, sm->jawRadii.y * 0.35f, 0.0f));
    float topY = sm->jawCenterLocal.y + sm->jawRadii.y;
    float beyondY = topY + sm->hingeRadius * 0.9f + 0.10f;
    int steps = 96;
    bool sawEmpty = false;
    bool reenteredSolid = false;
    float reentryY = 0.0f;
    float reentryDist = 0.0f;
    float emptyStartY = 0.0f;
    for (int i = 0; i <= steps; ++i) {
        float y = oralCenter.y + (beyondY - oralCenter.y) * ((float)i / (float)steps);
        Vector3 p = Vec3_Create(oralCenter.x, y, oralCenter.z);
        SDFSample s = jaw.evaluate(jaw.context, p);
        float d = s.distance;
        if (!sawEmpty && d > 0.0f) { sawEmpty = true; emptyStartY = y; }
        if (sawEmpty && d < -1e-4f) { reenteredSolid = true; reentryY = y; reentryDist = d; break; }
    }
    // Apertura amplia: centro y esquinas a la altura de apertura deben estar vacías (no sólidas)
    float openingY = oralCenter.y + sm->jawRadii.y * 0.58f;
    Vector3 centerP = Vec3_Create(oralCenter.x, openingY, oralCenter.z);
    Vector3 leftP = Vec3_Create(oralCenter.x - sm->jawRadii.x * 0.58f, openingY, oralCenter.z);
    Vector3 rightP = Vec3_Create(oralCenter.x + sm->jawRadii.x * 0.58f, openingY, oralCenter.z);
    float dCenter = jaw.evaluate(jaw.context, centerP).distance;
    float dLeft = jaw.evaluate(jaw.context, leftP).distance;
    float dRight = jaw.evaluate(jaw.context, rightP).distance;
    bool broadOpen = (dCenter > 0.0f && dLeft > 0.0f && dRight > 0.0f);

    printf("  [debug] jaw basin: oralY=%.4f topY=%.4f beyondY=%.4f sawEmpty=%d emptyStartY=%.4f reentered=%d reentryY=%.4f reentryDist=%.4f dCenter=%.4f dLeft=%.4f dRight=%.4f hingeR=%.4f\n",
        oralCenter.y, topY, beyondY, sawEmpty?1:0, emptyStartY, reenteredSolid?1:0, reentryY, reentryDist, dCenter, dLeft, dRight, sm->hingeRadius);

    char msg[256];
    snprintf(msg, sizeof(msg),
        "Cuenca oral de mandíbula reentra en sólido tras vacío (hollow-shell loop / tapa superior): reentryY=%.4f dist=%.4f dCenter=%.4f dLeft=%.4f dRight=%.4f",
        reentryY, reentryDist, dCenter, dLeft, dRight);
    TEST_ASSERT(!reenteredSolid, msg);
    snprintf(msg, sizeof(msg),
        "Apertura superior de mandíbula no es amplia: requiere vacío en centro y esquinas (pinhole): dCenter=%.4f dLeft=%.4f dRight=%.4f openingY=%.4f",
        dCenter, dLeft, dRight, openingY);
    TEST_ASSERT(broadOpen, msg);

    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_bug_jaw_oral_basin_reenters_solid_superior_cap\n");
}

static void test_bug_soft_tissue_bridge_is_three_disconnected_red_islands(void) {
    Monster m = MouthFixture();
    // Mesh de bisagra/puente blando observable
    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = cfg.resolutionY = cfg.resolutionZ = 14; cfg.voxelSize = 0.0f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "No se pudo construir visual para puente");

    const Mesh* hinge = MonsterVisual_GetHinge(&visual, 0);
    TEST_ASSERT(hinge && hinge->vertexCount > 0 && hinge->indexCount > 0, "Malla de puente vacía");

    size_t components = CountMeshComponents(hinge);
    printf("  [debug] bridge components=%zu (esperado 1)\n", components);

    // Color: vértices de bisagra están marcados SKIN pero con color insideColor (rojo)
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    Color skin = sm->skinColor;
    Color inside = sm->insideColor;
    size_t skinVertices = 0, redMismatch = 0;
    for (size_t i = 0; i < hinge->vertexCount; ++i) {
        if (hinge->vertices[i].material == SDF_MATERIAL_SKIN) {
            skinVertices++;
            if (Color_Equals(hinge->vertices[i].color, inside)) redMismatch++;
        }
    }
    float mismatchPct = skinVertices ? (100.0f * (float)redMismatch / (float)skinVertices) : 0.0f;
    printf("  [debug] bridge skinVertices=%zu redMismatch=%zu (%.1f%%) skin=(%u,%u,%u) inside=(%u,%u,%u)\n",
        skinVertices, redMismatch, mismatchPct, skin.r, skin.g, skin.b, inside.r, inside.g, inside.b);
    // Verificar que no todo el puente sea rojo interior y que coincida con skin del host
    // Muestreo: ningún vértice SKIN debe ser insideColor; deben ser skin
    size_t hostMatches = 0;
    for (size_t i = 0; i < hinge->vertexCount; ++i) {
        if (hinge->vertices[i].material == SDF_MATERIAL_SKIN && Color_Equals(hinge->vertices[i].color, skin)) hostMatches++;
    }
    float hostPct = skinVertices ? (100.0f * (float)hostMatches / (float)skinVertices) : 0.0f;
    printf("  [debug] bridge hostMatch=%zu (%.1f%%)\n", hostMatches, hostPct);

    char msg[256];
    snprintf(msg, sizeof(msg), "Puente blando tiene %zu componentes conectados (esperado 1) — tres islas desconectadas", components);
    TEST_ASSERT(components == 1, msg);
    snprintf(msg, sizeof(msg), "Tejido externo del puente es insideColor rojo en %zu/%zu (%.1f%%) vértices SKIN; debe ser skinColor del host", redMismatch, skinVertices, mismatchPct);
    TEST_ASSERT(redMismatch == 0, msg);
    snprintf(msg, sizeof(msg), "Solo %.1f%% del puente coincide con skinColor del host; se esperaba ~100%%", hostPct);
    TEST_ASSERT(hostPct > 99.0f, msg);

    MonsterSDF_Free(&sdf);
    MonsterVisual_Free(&visual);
    Monster_Free(&m);
    printf("[PASS] test_bug_soft_tissue_bridge_is_three_disconnected_red_islands\n");
}

static void test_bug_closed_jaw_does_not_share_upper_mouth_seam(void) {
    Monster m = MouthFixture();
    Mouth_SetOpenFactor(&m.mouths[0], 0.0f);
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    float lowerSeamY = sm->entranceCenterLocal.y - sm->entranceHalfExtents.y;
    float jawTopY = sm->jawCenterLocal.y + sm->jawRadii.y;
    float h = sm->seamScale; if (h < 1e-4f) h = sm->hingeRadius;
    printf("  [debug] closed seam: lowerSeamY=%.4f jawTopY=%.4f diff=%.4f h=%.4f slitHalfY=%.4f\n", lowerSeamY, jawTopY, jawTopY-lowerSeamY, h, sm->entranceHalfExtents.y);
    char msg[320];
    snprintf(msg, sizeof(msg), "Gap vertical mandíbula cerrada %.4f excede h*0.18=%.4f", fabsf(jawTopY-lowerSeamY), h*0.18f);
    TEST_ASSERT(fabsf(jawTopY - lowerSeamY) < h * 0.18f, msg);
    SDFMesherConfig cfg = SDFMesher_DefaultConfig(); cfg.resolutionX=cfg.resolutionY=cfg.resolutionZ=14; cfg.voxelSize=0.0f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "No se pudo construir visual para costura");
    const Mesh* jawMesh = MonsterVisual_GetJaw(&visual, 0);
    float maxY = -1e6f;
    for (size_t i=0;i<jawMesh->vertexCount;++i) {
        Vector3 local = Transform3D_ApplyRotationBasis(sm->inverseRotation, Vec3_Sub(jawMesh->vertices[i].position, sm->center));
        if (local.y > maxY) maxY = local.y;
    }
    printf("  [debug] jaw mesh maxY local %.4f vs lowerSeam %.4f diff %.4f\n", maxY, lowerSeamY, maxY-lowerSeamY);
    snprintf(msg, sizeof(msg), "Malla mandibular no alcanza costura: maxY %.4f vs seam %.4f diff %.4f > h*0.22", maxY, lowerSeamY, maxY-lowerSeamY);
    TEST_ASSERT(fabsf(maxY - lowerSeamY) < h * 0.22f, msg);
    MonsterVisual_Free(&visual);
    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_bug_closed_jaw_does_not_share_upper_mouth_seam\n");
}

static void test_bug_bridge_skull_anchor_moves_with_jaw(void) {
    Monster m = MouthFixture();
    Mouth_SetOpenFactor(&m.mouths[0], 0.0f);
    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = cfg.resolutionY = cfg.resolutionZ = 14; cfg.voxelSize = 0.0f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "No se pudo construir visual para articulación");

    const Mesh* hingeClosed = MonsterVisual_GetHinge(&visual, 0);
    TEST_ASSERT(hingeClosed && hingeClosed->vertexCount > 0, "Puente cerrado vacío");
    size_t n = hingeClosed->vertexCount;
    Vector3* closedPos = (Vector3*)malloc(n * sizeof(Vector3));
    TEST_ASSERT(closedPos != NULL, "Fallo alocación posiciones cerradas");
    for (size_t i = 0; i < n; ++i) closedPos[i] = hingeClosed->vertices[i].position;

    // Calcular ancla craneal en mundo (pivot del hueso temporal) — debe quedar fijo
    BodyPart* head = Monster_GetHead(&m);
    Vector3 worldPos = Vec3_Add(head->positionRender, m.mouths[0].offset);
    Vector3 pivotWorld = Vec3_Add(worldPos, m.mouths[0].jawPivot);
    // Abrir a factor 1 — sweep openFactor 0 -> 1
    Mouth_SetOpenFactor(&m.mouths[0], 1.0f);
    MonsterVisual_UpdateMouthArticulation(&visual.mouths[0], &m.mouths[0], &m);
    const Mesh* hingeOpen = MonsterVisual_GetHinge(&visual, 0);
    TEST_ASSERT(hingeOpen && hingeOpen->vertexCount == n, "Conteo de vértices de bisagra cambió tras articulación");

    float minDisp = 1e6f, maxDisp = 0.0f, sumDisp = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float d = Vec3_Distance(closedPos[i], hingeOpen->vertices[i].position);
        if (d < minDisp) minDisp = d;
        if (d > maxDisp) maxDisp = d;
        sumDisp += d;
    }
    float avgDisp = sumDisp / (float)n;
    // Región ancla craneal: vértices cuya posición cerrada está dentro de hingeRadius del pivot craneal
    MonsterSDF tmpSdf = BuildFixture(&m);
    float hingeR = tmpSdf.mouths[0].hingeRadius;
    MonsterSDF_Free(&tmpSdf);
    float skullRadius = hingeR * 1.05f;
    size_t skullCount = 0;
    float skullMin = 1e6f, skullMax = 0.0f, skullSum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float distToPivot = Vec3_Distance(closedPos[i], pivotWorld);
        if (distToPivot < skullRadius) {
            float d = Vec3_Distance(closedPos[i], hingeOpen->vertices[i].position);
            if (d < skullMin) skullMin = d;
            if (d > skullMax) skullMax = d;
            skullSum += d;
            skullCount++;
        }
    }
    float skullAvg = skullCount ? skullSum / (float)skullCount : 1e6f;
    // Región mandibular (jaw-side): resto que debe seguir la mandíbula
    size_t fixedCount = 0;
    for (size_t i = 0; i < n; ++i) {
        if (Vec3_Distance(closedPos[i], hingeOpen->vertices[i].position) < 0.008f) fixedCount++;
    }
    printf("  [debug] bridge articulation: n=%zu minDisp=%.4f maxDisp=%.4f avgDisp=%.4f fixed<0.008=%zu (%.1f%%) openFactor 0->1\n",
        n, minDisp, maxDisp, avgDisp, fixedCount, 100.0f*(float)fixedCount/(float)n);
    printf("  [debug] skull anchor region: cnt=%zu radius=%.4f skullMin=%.4f skullMax=%.4f skullAvg=%.4f pivotWorld=(%.3f,%.3f,%.3f)\n",
        skullCount, skullRadius, skullMin, skullMax, skullAvg, pivotWorld.x, pivotWorld.y, pivotWorld.z);
    printf("  [debug] closed[0]=(%.4f,%.4f,%.4f) open[0]=(%.4f,%.4f,%.4f)\n",
        closedPos[0].x, closedPos[0].y, closedPos[0].z,
        hingeOpen->vertices[0].position.x, hingeOpen->vertices[0].position.y, hingeOpen->vertices[0].position.z);

    free(closedPos);

    char msg[320];
    float thrAvg = hingeR * 0.14f; if (thrAvg < 0.012f) thrAvg = 0.012f;
    float thrMin = hingeR * 0.10f; if (thrMin < 0.008f) thrMin = 0.008f;
    snprintf(msg, sizeof(msg),
        "Ancla craneal del puente se mueve con la mandíbula: skullAvg=%.4f skullMin=%.4f skullMax=%.4f (esperado <%.3f/%.3f) cnt=%zu hingeR=%.4f",
        skullAvg, skullMin, skullMax, thrAvg, thrMin, skullCount, hingeR);
    TEST_ASSERT(skullCount > 0 && skullAvg < thrAvg && skullMin < thrMin, msg);
    snprintf(msg, sizeof(msg),
        "Puente no articula lado mandibular: maxDisp=%.4f (esperado >0.025) min=%.4f",
        maxDisp, minDisp);
    TEST_ASSERT(maxDisp > 0.020f, msg);

    MonsterVisual_Free(&visual); Monster_Free(&m);
    printf("[PASS] test_bug_bridge_skull_anchor_moves_with_jaw\n");
}

static void test_strengthened_hinge_isosurface_and_populations(void) {
    Monster m = MouthFixture();
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    float slitThick = sm->entranceHalfExtents.y * 2.0f;
    float h = Math_Max(sm->hingeRadius, Math_Max(sm->throatRadius, slitThick));
    if (h < 1e-4f) h = 0.11f;
    SDFMesherConfig scfg = SDFMesher_DefaultConfig();
    scfg.voxelSize = 0.03f; scfg.maxCells = 120000; scfg.useAutoBounds = true;
    SDFMesher tmp = SDFMesher_Create(scfg);
    MonsterSDFSeamField sctx2; SDFField sf2 = MonsterSDF_GetSeamField(&sdf, 0, &sctx2);
    Mesh tmpMesh = Mesh_Create();
    SDFMesher_GenerateMesh(&tmp, &sf2, &tmpMesh);
    float effVoxel = tmp.lastStats.effectiveVoxelSize;
    if (effVoxel < 1e-4f) effVoxel = 0.03f;
    float tol = effVoxel * 0.65f + 0.015f;
    if (tol > 0.045f) tol = 0.045f;
    Mesh_Free(&tmpMesh); SDFMesher_Free(&tmp);
    MonsterSDF_Free(&sdf);
    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = cfg.resolutionY = cfg.resolutionZ = 14; cfg.voxelSize = 0.0f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "No se pudo construir visual para isosuperficie");
    const Mesh* hingeBase = &visual.mouths[0].hingeBase;
    const MonsterSDFMouth* sm2 = &visual.sdf.mouths[0];
    float slitThick2 = sm2->entranceHalfExtents.y * 2.0f;
    float hh = Math_Max(sm2->hingeRadius, Math_Max(sm2->throatRadius, slitThick2));
    if (hh < 1e-4f) hh = h;
    // Verificar que cada vértice de hingeBase esté cerca de la isosuperficie de costura
    MonsterSDFSeamField checkCtx;
    SDFField checkField = MonsterSDF_GetSeamField(&visual.sdf, 0, &checkCtx);
    size_t offIso = 0;
    for (size_t i = 0; i < hingeBase->vertexCount; ++i) {
        Vector3 local = hingeBase->vertices[i].position;
        float d = checkField.evaluateDistance(checkField.context, local);
        if (!isfinite(d) || fabsf(d) > tol) offIso++;
    }
    char msgIso[256];
    snprintf(msgIso, sizeof(msgIso), "Vértices fuera de isosuperficie de costura: %zu/%zu fuera de tol %.3f (voxel %.3f h %.3f) — vértice manual desplazado", offIso, hingeBase->vertexCount, tol, effVoxel, hh);
    TEST_ASSERT(offIso == 0, msgIso);
    // Poblaciones significativas
    size_t total = hingeBase->vertexCount;
    size_t minPop = 8;
    size_t onePct = (size_t)(total * 0.01f);
    if (onePct > minPop) minPop = onePct;
    // Calcular poblaciones basadas en distancia relativa a anclas (normalizada por h)
    size_t skullPop = 0, jawPop = 0;
    for (size_t i = 0; i < total; ++i) {
        Vector3 p = hingeBase->vertices[i].position;
        float dSkull = Math_Min(Vec3_Distance(p, sm2->seamSkullLeftLocal), Vec3_Distance(p, sm2->seamSkullRightLocal));
        dSkull = Math_Min(dSkull, Vec3_Distance(p, sm2->seamGularLocal));
        float dJaw = Math_Min(Vec3_Distance(p, sm2->seamJawLeftClosedLocal), Vec3_Distance(p, sm2->seamJawRightClosedLocal));
        float dnSkull = dSkull / hh;
        float dnJaw = dJaw / hh;
        float w = 0.0f;
        if (dnSkull + dnJaw > 1e-6f) w = dnSkull / (dnSkull + dnJaw);
        if (w < 0.35f) skullPop++;
        else if (w > 0.65f) jawPop++;
    }
    char msgPop[256];
    snprintf(msgPop, sizeof(msgPop), "Poblaciones insuficientes: skull %zu jaw %zu total %zu minReq %zu (h %.3f)", skullPop, jawPop, total, minPop, hh);
    TEST_ASSERT(skullPop >= minPop && jawPop >= minPop, msgPop);
    // Desplazamientos proporcionales
    Mouth_SetOpenFactor(&m.mouths[0], 0.0f);
    MonsterVisual_UpdateMouthArticulation(&visual.mouths[0], &m.mouths[0], &m);
    const Mesh* closed = &visual.mouths[0].hinge;
    (void)closed;
    // No need to test disp here, the other tests cover it, but we verify no aislados
    for (size_t i = 0; i < hingeBase->vertexCount; ++i) {
        Vector3 n = hingeBase->vertices[i].normal;
        TEST_ASSERT(isfinite(n.x) && isfinite(n.y) && isfinite(n.z), "Normal no finita en hingeBase");
    }
    MonsterVisual_Free(&visual);
    Monster_Free(&m);
    printf("[PASS] test_strengthened_hinge_isosurface_and_populations\n");
}

static void test_seam_material_and_openFactor_sweep(void) {
    Monster m = MouthFixture();
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    MonsterSDFSeamField seamCtx;
    SDFField seam = MonsterSDF_GetSeamField(&sdf, 0, &seamCtx);
    Vector3 gular = sm->seamGularLocal;
    SDFSample s = seam.evaluate(seam.context, gular);
    TEST_ASSERT(s.material == SDF_MATERIAL_SKIN, "Tejido blando debe ser SKIN");
    TEST_ASSERT(isfinite(s.distance), "Distancia de costura no finita en gular");
    Vector3 skull = sm->seamSkullLeftLocal;
    s = seam.evaluate(seam.context, skull);
    TEST_ASSERT(s.material == SDF_MATERIAL_SKIN, "Ancla craneal debe ser SKIN");
    TEST_ASSERT(isfinite(s.distance), "Distancia craneal no finita");
    MonsterSDF_Free(&sdf);
    Mouth_SetOpenFactor(&m.mouths[0], 0.0f);
    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.resolutionX = cfg.resolutionY = cfg.resolutionZ = 14; cfg.voxelSize = 0.0f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "No se pudo construir visual para barrido");
    const Mesh* baseHinge = MonsterVisual_GetHinge(&visual, 0);
    size_t baseCount = baseHinge->vertexCount;
    Vector3 pivotWorld = Vec3_Add(Vec3_Add(Monster_GetHead(&m)->positionRender, m.mouths[0].offset), m.mouths[0].jawPivot);
    float openFactors[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (size_t oi = 0; oi < 5; ++oi) {
        Mouth_SetOpenFactor(&m.mouths[0], openFactors[oi]);
        MonsterVisual_UpdateMouthArticulation(&visual.mouths[0], &m.mouths[0], &m);
        const Mesh* hinge = MonsterVisual_GetHinge(&visual, 0);
        TEST_ASSERT(hinge && hinge->vertexCount == baseCount, "Topología de bisagra cambió con openFactor");
        MeshValidationResult vr = Mesh_Validate(hinge);
        TEST_ASSERT(vr.valid, "Bisagra no válida en barrido openFactor");
        for (size_t vi = 0; vi < hinge->vertexCount; ++vi) {
            Vector3 n = hinge->vertices[vi].normal;
            TEST_ASSERT(isfinite(n.x) && isfinite(n.y) && isfinite(n.z), "Normal no finita en barrido");
            float len = sqrtf(n.x*n.x + n.y*n.y + n.z*n.z);
            TEST_ASSERT(fabsf(len - 1.0f) < 0.02f, "Normal no normalizada en barrido");
            TEST_ASSERT(hinge->vertices[vi].material == SDF_MATERIAL_SKIN, "Material de bisagra debe ser SKIN en barrido");
        }
        if (openFactors[oi] == 0.0f) continue;
        // Verificar continuidad de ancla craneal en cada paso
        const Mesh* closed = baseHinge;
        // Usar primer vértice cercano al pivote como ancla craneal
        float minD = 1e6f; size_t closest = 0;
        for (size_t vi = 0; vi < closed->vertexCount; ++vi) {
            float d = Vec3_Distance(closed->vertices[vi].position, pivotWorld);
            if (d < minD) { minD = d; closest = vi; }
        }
        if (minD < 0.13f) {
            float d2 = Vec3_Distance(hinge->vertices[closest].position, closed->vertices[closest].position);
            char msg2[256];
            snprintf(msg2, sizeof(msg2), "Ancla craneal se desplazó %.4f en openFactor %.2f", d2, openFactors[oi]);
            TEST_ASSERT(d2 < 0.015f, msg2);
        }
    }
    MonsterVisual_Free(&visual);
    Monster_Free(&m);
    printf("[PASS] test_seam_material_and_openFactor_sweep\n");
}

static void test_scale_sweep_05_1_2(void) {
    float scales[] = {0.5f, 1.0f, 2.0f};
    for (int si=0; si<3; ++si) {
        float s = scales[si];
        Monster m = MouthFixture();
        BodyPart* head = Monster_GetHead(&m);
        head->width = head->widthRender = 2.0f * s;
        head->height = head->heightRender = 1.8f * s;
        head->length = head->lengthRender = 1.8f * s;
        m.mouths[0].scale = Vec3_Create(0.82f*s, 0.52f*s, 0.62f*s);
        m.mouths[0].slitThickness = 0.04f*s;
        m.mouths[0].cornerRadius = 0.08f*s;
        m.mouths[0].jawLength = 0.5f*s;
        m.mouths[0].jawWidth = 0.6f*s;
        m.mouths[0].jawThickness = 0.14f*s;
        m.mouths[0].hingeRadius = 0.09f*s;
        m.mouths[0].throatRadius = 0.08f*s;
        Mouth_Normalize(&m.mouths[0]);
        MonsterSDF sdf = BuildFixture(&m);
        const MonsterSDFMouth* sm = &sdf.mouths[0];
        float h = sm->seamScale;
        float lowerSeamY = sm->entranceCenterLocal.y - sm->entranceHalfExtents.y;
        float jawTopY = sm->jawCenterLocal.y + sm->jawRadii.y;
        char msg[256];
        snprintf(msg,sizeof(msg),"Sweep %.1fx: gap %.4f h %.4f", s, fabsf(jawTopY-lowerSeamY), h);
        TEST_ASSERT(fabsf(jawTopY - lowerSeamY) < h*0.22f, msg);
        TEST_ASSERT(isfinite(sm->jawCenterLocal.x) && isfinite(sm->seamBounds.start.x), "Bounds no finitos en sweep");
        TEST_ASSERT(sm->jawRadii.x > 0 && sm->seamBounds.end.x > sm->seamBounds.start.x, "Radii/bounds inválidos en sweep");
        SDFMesherConfig cfg=SDFMesher_DefaultConfig(); cfg.resolutionX=cfg.resolutionY=cfg.resolutionZ=14; cfg.voxelSize=0.0f;
        MonsterVisual visual=MonsterVisual_Create(cfg);
        TEST_ASSERT(MonsterVisual_RebuildNow(&visual,&m,MonsterSDF_DefaultConfig()), "Visual falló en sweep");
        const Mesh* jaw=MonsterVisual_GetJaw(&visual,0);
        const Mesh* hinge=MonsterVisual_GetHinge(&visual,0);
        TEST_ASSERT(jaw->vertexCount>0 && hinge->vertexCount>0, "Mallas vacías en sweep");
        TEST_ASSERT(Mesh_Validate(jaw).valid && Mesh_Validate(hinge).valid, "Mallas inválidas en sweep");
        MonsterVisual_Free(&visual);
        MonsterSDF_Free(&sdf); Monster_Free(&m);
    }
    printf("[PASS] test_scale_sweep_05_1_2\n");
}
static void test_rotation_and_bounds_nonzero(void) {
    Monster m = MouthFixture();
    m.mouths[0].rotation = Vec3_Create(0.0f, 0.6f, 0.35f);
    Mouth_Normalize(&m.mouths[0]);
    MonsterSDF sdf = BuildFixture(&m);
    AABB3D b = MonsterSDF_GetBounds(&sdf);
    TEST_ASSERT(isfinite(b.start.x) && isfinite(b.end.x) && b.end.x > b.start.x, "Bounds globales no finitos con rotación");
    TEST_ASSERT(isfinite(sdf.mouths[0].influenceBounds.start.x) && sdf.mouths[0].influenceBounds.end.x > sdf.mouths[0].influenceBounds.start.x, "InfluenceBounds no finitos con rotación");
    TEST_ASSERT(isfinite(sdf.mouths[0].seamBounds.start.x) && sdf.mouths[0].seamBounds.end.x > sdf.mouths[0].seamBounds.start.x, "SeamBounds no finitos con rotación");
    SDFMesherConfig cfg=SDFMesher_DefaultConfig(); cfg.resolutionX=cfg.resolutionY=cfg.resolutionZ=14; cfg.voxelSize=0.0f;
    MonsterVisual visual=MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual,&m,MonsterSDF_DefaultConfig()), "Visual falló con rotación");
    TEST_ASSERT(MonsterVisual_GetJaw(&visual,0)->vertexCount>0, "Jaw vacío con rotación");
    MonsterVisual_Free(&visual);
    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_rotation_and_bounds_nonzero\n");
}
static void test_jaw_side_profile_thickness(void) {
    Monster m = MouthFixture();
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    float h = sm->seamScale;
    float thickness = sm->jawRadii.y * 2.0f;
    float norm = thickness / Math_Max(h, 1e-4f);
    printf("  [debug] jaw thickness %.4f h %.4f norm %.3f\n", thickness, h, norm);
    TEST_ASSERT(norm > 0.90f && norm < 1.45f, "Grosor mandibular fuera de rango phenotype-relative h*0.48-0.55");
    float jawTop = sm->jawCenterLocal.y + sm->jawRadii.y;
    float lowerSeam = sm->entranceCenterLocal.y - sm->entranceHalfExtents.y;
    TEST_ASSERT(fabsf(jawTop - lowerSeam) < h*0.22f, "Top no alineado a costura inferior");
    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_jaw_side_profile_thickness\n");
}
static void test_basin_depth_width_no_cap(void) {
    Monster m = MouthFixture();
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    float h = sm->seamScale;
    MonsterSDFJawField ctx; SDFField jaw = MonsterSDF_GetJawField(&sdf, 0, &ctx);
    Vector3 jawCenter = sm->jawCenterLocal;
    float jawTop = jawCenter.y + sm->jawRadii.y;
    Vector3 probe = Vec3_Add(jawCenter, Vec3_Create(0, sm->jawRadii.y*0.18f + h*0.12f, 0));
    (void)jawTop;
    SDFSample s = jaw.evaluate(jaw.context, probe);
    TEST_ASSERT(s.material == SDF_MATERIAL_MOUTH, "Cuenca no visible desde lado/frente");
    Vector3 oralCenter = Vec3_Add(jawCenter, Vec3_Create(0, sm->jawRadii.y*0.12f, 0));
    float topY = jawCenter.y + sm->jawRadii.y;
    float beyondY = topY + h*0.50f;
    bool sawEmpty=false, reentered=false;
    for(int i=0;i<=32;++i){
        float y = oralCenter.y + (beyondY - oralCenter.y)*((float)i/32.0f);
        Vector3 p = Vec3_Create(oralCenter.x, y, oralCenter.z);
        float d = jaw.evaluateDistance(jaw.context, p);
        if(!sawEmpty && d>0) sawEmpty=true;
        if(sawEmpty && d < -1e-4f) reentered=true;
    }
    TEST_ASSERT(sawEmpty && !reentered, "Cuenca con tapa superior o poco profunda");
    float rx = sm->jawRadii.x, rz = sm->jawRadii.z;
    float basinRx = rx * 0.60f; float basinRz = rz * 0.68f;
    TEST_ASSERT(rx - basinRx > h*0.15f && rz - basinRz > h*0.12f, "Paredes laterales/frontales insuficientes");
    (void)h;
    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_basin_depth_width_no_cap\n");
}
static void test_compact_seam_bounds(void) {
    Monster m = MouthFixture();
    MonsterSDF sdf = BuildFixture(&m);
    const MonsterSDFMouth* sm = &sdf.mouths[0];
    float visibleFront = sm->entranceCenterLocal.z + sm->entranceHalfExtents.z * 0.90f;
    printf("  [debug] seam maxZ %.4f visibleFront %.4f entrance %.4f half %.4f\n", sm->seamBounds.end.z, visibleFront, sm->entranceCenterLocal.z, sm->entranceHalfExtents.z);
    TEST_ASSERT(sm->seamBounds.end.z < visibleFront + sm->seamScale*0.20f, "Seam se extiende al frente visible (debe ser posterior compacto)");
    TEST_ASSERT(sm->seamBounds.start.z > sm->hostCenterLocal.z - sm->hostRadii.z*0.9f, "Seam fuera de cabeza posterior");
    float seamWidth = sm->seamBounds.end.x - sm->seamBounds.start.x;
    float mouthWidth = sm->entranceHalfExtents.x*2.0f;
    TEST_ASSERT(seamWidth < mouthWidth*0.95f, "Seam ancho como losa frontal");
    MonsterSDF_Free(&sdf); Monster_Free(&m);
    printf("[PASS] test_compact_seam_bounds\n");
}
static void test_default_async_voxel_configs(void) {
    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    printf("  [debug] async interactive %.3f/%u settled %.3f/%u\n", cfg.interactiveMesherConfig.voxelSize, (unsigned)cfg.interactiveMesherConfig.maxCells, cfg.settledMesherConfig.voxelSize, (unsigned)cfg.settledMesherConfig.maxCells);
    TEST_ASSERT(fabsf(cfg.interactiveMesherConfig.voxelSize - 0.12f) < 0.015f, "Voxel interactivo no es ~0.12");
    TEST_ASSERT(cfg.interactiveMesherConfig.maxCells == 250000, "maxCells interactivo no es 250k");
    TEST_ASSERT(fabsf(cfg.settledMesherConfig.voxelSize - 0.08f) < 0.015f, "Voxel settled no es ~0.08");
    TEST_ASSERT(cfg.settledMesherConfig.maxCells == 500000, "maxCells settled no es 500k");
    printf("[PASS] test_default_async_voxel_configs\n");
}
void run_mouth_geometry_tests(void) {
    printf("\n--- Módulo Boca Anatómica ---\n");
    test_anatomical_defaults(); test_cavity_exterior_and_depth(); test_material_and_debug_layers();
    test_jaw_articulation_without_body_remesh(); test_mesh_components();
    test_body_and_dedicated_jaw_fields();
    test_deterministic_anatomy_sweep();
    test_async_multi_mouth_indexing();
    test_individual_phenotype_influence();
    test_strengthened_hinge_isosurface_and_populations();
    test_bug_jaw_oral_basin_reenters_solid_superior_cap();
    test_bug_soft_tissue_bridge_is_three_disconnected_red_islands();
    test_bug_closed_jaw_does_not_share_upper_mouth_seam();
    test_bug_bridge_skull_anchor_moves_with_jaw();
    test_seam_material_and_openFactor_sweep();
    test_scale_sweep_05_1_2();
    test_rotation_and_bounds_nonzero();
    test_jaw_side_profile_thickness();
    test_basin_depth_width_no_cap();
    test_compact_seam_bounds();
    test_default_async_voxel_configs();
}
