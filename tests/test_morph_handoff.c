/** @file test_morph_handoff.c
 * @brief Pruebas unitarias de la interfaz continua de cesión (MorphHandoff).
 */
#include "test_utils.h"
#include "MorphHandoff.h"
#include "CreatureRecipes.h"
#include "Creature.h"
#include "Monster.h"
#include "MorphModuleState.h"
#include "SurfaceMapper.h"
#include <stdio.h>
#include <math.h>

static void test_morph_handoff_lifecycle(void) {
    MorphHandoffSet set;
    MorphHandoffSet_Init(&set);
    TEST_ASSERT(set.count == 0, "MorphHandoffSet_Init no reinició el conteo a 0");

    Monster dog = Monster_Create();
    CreaturePhenotype dp = CreatureRecipes_Dog()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &dp),
        "No se pudo construir el perro para probar MorphHandoff");

    /* Recopilar módulos de extremidades y cola */
    uint32_t moduleIds[8];
    size_t moduleCount = 0;
    for(size_t n = 0; n < dog.anatomyGraph.nodeCount && moduleCount < 8; ++n) {
        const AnatomyNode* node = &dog.anatomyGraph.nodes[n];
        if(node->region == ANATOMY_REGION_FORELIMB ||
           node->region == ANATOMY_REGION_HINDLIMB ||
           node->region == ANATOMY_REGION_TAIL) {
            bool exists = false;
            for(size_t i = 0; i < moduleCount; ++i) {
                if(moduleIds[i] == node->moduleInstanceId) { exists = true; break; }
            }
            if(!exists && node->moduleInstanceId != 0) {
                moduleIds[moduleCount++] = node->moduleInstanceId;
            }
        }
    }

    TEST_ASSERT(moduleCount >= 3, "No se encontraron suficientes módulos apendiculares en el perro");

    size_t built = MorphHandoff_BuildFromGraph(&dog.anatomyGraph, moduleIds, moduleCount, &set);
    TEST_ASSERT(built >= 3 && set.count == built, "MorphHandoff_BuildFromGraph no construyó las interfaces");

    for(size_t i = 0; i < set.count; ++i) {
        const MorphHandoffInterface* h = &set.interfaces[i];
        TEST_ASSERT(h->active, "La interfaz no fue marcada como activa");
        TEST_ASSERT(h->radius >= 0.04f, "Radio de la articulación demasiado pequeño");
        TEST_ASSERT(h->blendWidth >= 0.006f, "Banda de mezcla demasiado estrecha");
        float len = Vec3_Length(h->normal);
        TEST_ASSERT(FLOAT_NEAR(len, 1.0f), "La normal del miembro no es unitaria");
    }

    Monster_Free(&dog);
    printf("[PASS] test_morph_handoff_lifecycle\n");
}

static void test_morph_handoff_distance_and_clipping(void) {
    MorphHandoffInterface h={0};
    h.moduleId = 42;
    h.chunkId = 1;
    h.origin = Vec3_Create(0.0f, 1.0f, 0.0f);
    h.normal = Vec3_Create(0.0f, -1.0f, 0.0f); /* Apunta hacia abajo (-Y) */
    h.radius = 0.20f;
    h.hardClipOffset = 0.0f;
    h.blendWidth = 0.05f;
    h.transitionFactor = 1.0f;
    h.active = true;

    /* Punto distal (y = 0.5 < 1.0, a favor de normal -Y): d > 0 */
    Vector3 distal = Vec3_Create(0.0f, 0.5f, 0.0f);
    float dDistal = MorphHandoff_EvaluateDistance(&h, distal);
    TEST_ASSERT(FLOAT_NEAR(dDistal, 0.5f), "Distancia al punto distal incorrecta");

    /* Punto proximal (y = 1.5 > 1.0, contra normal -Y): d < 0 */
    Vector3 proximal = Vec3_Create(0.0f, 1.5f, 0.0f);
    float dProximal = MorphHandoff_EvaluateDistance(&h, proximal);
    TEST_ASSERT(FLOAT_NEAR(dProximal, -0.5f), "Distancia al punto proximal incorrecta");

    /* Plano de corte para la malla del cuerpo */
    float plane[4];
    MorphHandoff_GetMeshClipPlane(&h, plane);
    /* En punto proximal (dentro del cuerpo): plane . proximal >= 0 */
    float evalProx = plane[0]*proximal.x + plane[1]*proximal.y + plane[2]*proximal.z + plane[3];
    TEST_ASSERT(evalProx > 0.0f, "El punto proximal del cuerpo no fue conservado por el plano de corte");

    /* En punto distal profundo (y = 0.0, d = 1.0 > blendWidth): plane . distal < 0 */
    Vector3 deepDistal = Vec3_Create(0.0f, 0.0f, 0.0f);
    float evalDist = plane[0]*deepDistal.x + plane[1]*deepDistal.y + plane[2]*deepDistal.z + plane[3];
    TEST_ASSERT(evalDist < 0.0f, "El punto distal apendicular no fue recortado por el plano de corte");

    printf("[PASS] test_morph_handoff_distance_and_clipping\n");
}

static void test_morph_handoff_blend_smoothness(void) {
    MorphHandoffSet set;
    MorphHandoffSet_Init(&set);

    MorphHandoffInterface* h = &set.interfaces[set.count++];
    h->moduleId = 10;
    h->chunkId = 0;
    h->origin = Vec3_Create(1.0f, 0.0f, 0.0f);
    h->normal = Vec3_Create(1.0f, 0.0f, 0.0f); /* Apunta hacia +X */
    h->radius = 0.25f;
    h->hardClipOffset = 0.0f;
    h->blendWidth = 0.05f;
    h->transitionFactor = 1.0f;
    h->active = true;

    /* x < 1.0 - blendWidth -> blend = 0.0 (malla corporal pura) */
    Vector3 meshPt = Vec3_Create(0.90f, 0.0f, 0.0f);
    TEST_ASSERT(FLOAT_NEAR(MorphHandoff_EvaluateBlend(&set, meshPt), 0.0f),
        "Punto del dominio de malla no retornó factor 0");

    /* x > 1.0 + blendWidth -> blend = 1.0 (SDF puro) */
    Vector3 sdfPt = Vec3_Create(1.10f, 0.0f, 0.0f);
    TEST_ASSERT(FLOAT_NEAR(MorphHandoff_EvaluateBlend(&set, sdfPt), 1.0f),
        "Punto del dominio SDF no retornó factor 1");

    /* x = 1.0 (en el plano de la interfaz) -> blend = 0.5 */
    Vector3 midPt = Vec3_Create(1.0f, 0.0f, 0.0f);
    float midBlend = MorphHandoff_EvaluateBlend(&set, midPt);
    TEST_ASSERT(fabsf(midBlend - 0.5f) < 0.02f, "El plano medio no dio mezcla centrada en 0.5");

    /* Punto alejado radialmente (y = 1.0 >> radius = 0.25) -> blend = 0.0 */
    Vector3 farRadial = Vec3_Create(1.10f, 1.0f, 0.0f);
    TEST_ASSERT(FLOAT_NEAR(MorphHandoff_EvaluateBlend(&set, farRadial), 0.0f),
        "Punto alejado radialmente no fue amortiguado a 0");

    printf("[PASS] test_morph_handoff_blend_smoothness\n");
}

static void test_handoff_curved_support_and_locality(void) {
    MorphHandoffSet set={0};set.count=1;
    MorphHandoffInterface* h=&set.interfaces[0];
    h->active=true;h->normal=Vec3_Create(1,0,0);h->radius=.2f;
    h->blendWidth=.02f;h->transitionFactor=1;h->segmentCount=2;
    h->segments[0]=(MorphHandoffSegment){Vec3_Zero(),Vec3_Create(1,0,0),.3f};
    h->segments[1]=(MorphHandoffSegment){Vec3_Create(1,0,0),Vec3_Create(-1,1,0),.3f};
    TEST_ASSERT(MorphHandoff_EvaluateBlend(&set,Vec3_Create(-1,1,0))>.99f,
        "El plano proximal cortó una cola curvada lejos de la raíz");
    TEST_ASSERT(MorphHandoff_EvaluateBlend(&set,Vec3_Create(2,2,0))==0,
        "La cesión recortó un módulo ajeno fuera del soporte");
    AABB3D bounds=MorphHandoff_WorkBounds(h);
    TEST_ASSERT(bounds.start.x< -1.3f&&bounds.end.x>1.3f,
        "La caja de trabajo perdió banda o collar del soporte");
    /* Una cara cruzada debe tener muestras de ambos dominios; el recorte no
     * depende de que su primer vértice esté en el lado SDF. */
    float a=MorphHandoff_EvaluateBlend(&set,Vec3_Create(-.1f,0,0));
    float b=MorphHandoff_EvaluateBlend(&set,Vec3_Create(.1f,0,0));
    TEST_ASSERT(a==0&&b==1,"Una cara cruzada no conserva ambos lados del plano");
}

static void test_handoff_temporal_hysteresis(void) {
    MorphModuleState s;
    MorphModuleState_Init(&s,10,1,true);
    MorphModuleState_Request(&s,.5f);
    TEST_ASSERT(MorphModuleState_Accept(&s,MorphModuleState_Token(&s,.5f)),
        "No se certificó el módulo para probar la salida gradual");
    MorphModuleState_UpdateHandoff(&s,.5f,.05f,true,.016f,.12f);
    TEST_ASSERT(s.handoffTransition==1,"La publicación cambió representación en un frame");
    for(int i=0;i<30;++i)MorphModuleState_UpdateHandoff(&s,.5f,.05f,true,.016f,.12f);
    TEST_ASSERT(s.handoffState==MORPH_MESH_ACTIVE&&s.handoffTransition==0,
        "El certificado no terminó la cesión hacia malla");
    float previous=s.handoffTransition;
    for(int i=0;i<20;++i) {
        MorphModuleState_UpdateHandoff(&s,i%2?.551f:.549f,.05f,true,.016f,.12f);
        TEST_ASSERT(s.handoffTransition>=previous,
            "El umbral ruidoso invirtió la rampa entre frames adyacentes");
        previous=s.handoffTransition;
    }
    for(int i=0;i<20;++i)MorphModuleState_UpdateHandoff(&s,.7f,.05f,true,.016f,.12f);
    TEST_ASSERT(s.handoffState==MORPH_SDF_ACTIVE&&s.handoffTransition==1,
        "El desfase persistente no activó el respaldo");
    for(int i=0;i<20;++i)MorphModuleState_UpdateHandoff(&s,.5f,.05f,false,.016f,.12f);
    TEST_ASSERT(s.handoffTransition==1,"Una interfaz inválida permitió salida de SDF");
}

static void test_shared_material_domain_contract(void) {
    Monster dog=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&dog,CreatureRecipes_Dog(),
        &CreatureRecipes_Dog()->adult),"No se construyó anatomía para dominio material");
    SurfaceMapping mapping=SurfaceMapping_FromAnatomy(&dog.anatomyGraph,2.0f);
    SurfaceMorphDomain* domain=malloc(sizeof(*domain));
    TEST_ASSERT(domain!=NULL,"No se reservó dominio material");
    TEST_ASSERT(SurfaceMapper_BuildMorphDomain(domain,&dog.anatomyGraph,
        &dog.anatomyGraph,&mapping)&&domain->count&&domain->nodeCount,
        "No se compiló el dominio canónico compartido");
    for(size_t i=0;i<domain->count;++i) {
        for(int axis=0;axis<3;++axis) {
            TEST_ASSERT(FLOAT_NEAR(domain->data[i][0][axis]*.5f,domain->data[i][2][axis])&&
                FLOAT_NEAR(domain->data[i][1][axis]*.5f,domain->data[i][3][axis]),
                "La reordenación BVH cambió la identidad o escala de un segmento");
        }
        TEST_ASSERT(fabsf(fabsf(domain->data[i][4][3])-1.0f)<.001f,
            "Una anatomía idéntica giró las coordenadas materiales");
    }
    size_t leaves=0;
    for(size_t n=0;n<domain->nodeCount;++n) {
        float (*node)[4]=domain->nodes[n];int first=(int)node[2][2],count=(int)node[2][3];
        TEST_ASSERT(node[0][3]>0,"Radio BVH no conservador");
        if(!count) {
            TEST_ASSERT(node[2][0]>=0&&node[2][1]>=0&&
                node[2][0]<domain->nodeCount&&node[2][1]<domain->nodeCount,
                "Hijo BVH fuera del dominio GPU");continue;
        }
        TEST_ASSERT(first>=0&&(size_t)(first+count)<=domain->count,
            "Hoja BVH fuera del buffer GPU");leaves+=(size_t)count;
        for(int i=first;i<first+count;++i)for(int endpoint=0;endpoint<2;++endpoint) {
            TEST_ASSERT(node[0][3]+.001f>=domain->data[i][endpoint][3],
                "La poda BVH subestimó el radio de un segmento");
            for(int axis=0;axis<3;++axis)
                TEST_ASSERT(domain->data[i][endpoint][axis]>=node[0][axis]-.001f&&
                    domain->data[i][endpoint][axis]<=node[1][axis]+.001f,
                    "La poda BVH excluyó un extremo anatómico");
        }
    }
    TEST_ASSERT(leaves==domain->count,"El BVH perdió o duplicó segmentos materiales");
    free(domain);Monster_Free(&dog);
}

void run_morph_handoff_tests(void) {
    printf("\n--- Módulo MorphHandoff (Continuidad Geométrica de Cesión) ---\n");
    test_morph_handoff_lifecycle();
    test_morph_handoff_distance_and_clipping();
    test_morph_handoff_blend_smoothness();
    test_handoff_curved_support_and_locality();
    test_handoff_temporal_hysteresis();
    test_shared_material_domain_contract();
}
