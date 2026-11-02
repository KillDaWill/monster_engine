/** @file test_equine_anatomy.c
 * @brief Équidos reutilizables: cráneo, casco, apoyo, rig y desarrollo v1. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
#include "Limb.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>

static void CranialComposition(void) {
    TEST_ASSERT(HEAD_ARCHETYPE_BOVID==3&&HEAD_ARCHETYPE_EQUINE==4,"Conservar identidades de arquetipos antiguos");
    for(uint32_t seed=1;seed<=180;++seed) {
        HeadPhenotype p=HeadPhenotype_RandomValid(HEAD_ARCHETYPE_EQUINE,seed);
        HeadAnatomy h;
        TEST_ASSERT(HeadAnatomy_Resolve(&p,0,Vec3_Create(.5f,.45f,.5f),&h),"Variaciones cefálicas équidas legales");
        TEST_ASSERT(HeadAnatomy_Validate(&h)==HEAD_VALID,"Simetría, finitud y orden cefálico");
        TEST_ASSERT(h.surface.faceTipRadii.x>h.surface.faceMidRadii.x,"Labios más anchos que el puente nasal");
        TEST_ASSERT(h.surface.faceTip.y<h.surface.faceRoot.y&&h.surface.oralSlope<0,"Cara y labios inclinados juntos");
        TEST_ASSERT(!h.surface.hasNasalPad&&h.surface.hasEars,"Sin trufa canina; conservar pabellones");
        TEST_ASSERT(h.landmarks.leftOrbitNormal.x>h.landmarks.leftOrbitNormal.z,"Órbitas laterales de herbívoro");
        HeadPhenotype q=HeadPhenotype_RandomValid(HEAD_ARCHETYPE_EQUINE,seed);
        TEST_ASSERT(FLOAT_NEAR(p.muzzleLength,q.muzzleLength)&&p.archetype==q.archetype,"Variación determinista por semilla");
    }
    HeadPhenotype p=HeadPhenotype_EquinePreset();p.muzzleLength=NAN;p.earHeightRatio=INFINITY;
    HeadPhenotype_Normalize(&p);
    TEST_ASSERT(isfinite(p.muzzleLength)&&isfinite(p.earHeightRatio)&&p.archetype==HEAD_ARCHETYPE_EQUINE,"Normalizar parámetros inválidos sin perder familia");
}

static void FeetAndStages(const char* path) {
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));char error[256];
    TEST_ASSERT(doc&&m,"Reservar receta équida");
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,path,error,sizeof(error)),"Cargar documento équido v1");
    const CreatureRecipe* r=&doc->recipe;
    for(unsigned stage=0;stage<4;++stage) {
        const CreaturePhenotype* p=CreatureRecipe_GetStage(r,(CreatureStage)stage);
        *m=Monster_Create();TEST_ASSERT(Creature_BuildMonster(m,r,p),"Resolver cuatro etapas por la tubería común");
        TEST_ASSERT(m->animation&&m->animation->rig.limbCount==4,"Cuatro cadenas locomotoras ungulígradas");
        TEST_ASSERT(HeadAnatomy_Validate(&m->head.anatomy)==HEAD_VALID,"Cráneo de receta válido");
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig()),"Compilar campo y materiales");
        for(unsigned leg=0;leg<4;++leg) {
            uint32_t module=10+leg;
            const AnatomyNode* foot=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,LIMB_NODE_AUTOPOD);
            const AnatomyNode* heel=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,Limb_DigitLocalId(0,1));
            const AnatomyNode* toe=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,Limb_DigitLocalId(0,2));
            TEST_ASSERT(foot&&heel&&toe&&!AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,Limb_DigitLocalId(1,0)),"Un dedo principal, sin segundo casco escondido");
            TEST_ASSERT(fabsf(heel->center.x-foot->center.x)<1e-5f&&fabsf(toe->center.x-foot->center.x)<1e-5f,"Casco central alineado con metapodio");
            LimbRigDescriptor rig;
            TEST_ASSERT(Limb_BuildRigDescriptor(&p->limbs[leg],module,&m->anatomyGraph,&rig),"Descriptor compartido de apoyo");
            float sole=foot->center.y-rig.soleHeight;
            TEST_ASSERT(fabsf(sole)<1e-5f,"Los cuatro cascos llegan al suelo de la receta");
            TEST_ASSERT(fabsf(heel->center.y-heel->heightRadius-sole)<1e-5f,"El rig y la cápsula usan la misma suela");
            unsigned count=0;
            for(size_t i=0;i<sdf.connectorCount;++i) {
                const MonsterSDFConnector* c=&sdf.connectors[i];
                if(c->moduleInstanceId!=module||c->material!=SDF_MATERIAL_HOOF)continue;
                ++count;
                for(unsigned j=0;j<=12;++j) {
                    Vector3 point=Vec3_Lerp(c->a,c->b,j/12.f);
                    TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,point)<=1e-5f,"Continuidad longitudinal de queratina");
                    Vector3 h={.004f,.004f,.004f};AABB3D cell={Vec3_Sub(point,h),Vec3_Add(point,h)};
                    SDFField field=MonsterSDF_GetField(&sdf);float lo,hi;
                    float value=MonsterSDF_EvaluateDistance(&sdf,point);
                    TEST_ASSERT(field.getCellRange(field.context,cell,&lo,&hi)&&value>=lo&&value<=hi,"Cotas conservadoras del casco central");
                }
            }
            TEST_ASSERT(count==1,"Exactamente una cápsula de casco por miembro");
            for(unsigned j=0;j<=16;++j) {
                Vector3 point=Vec3_Lerp(foot->center,heel->center,j/16.f);
                TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,point)<=1e-5f,"Casco conectado al menudillo, sin isla flotante");
            }
        }
        TEST_ASSERT(MonsterAnimation_Update(m,1.f/60),"La pose usa el rig existente");
        MonsterSDF_Free(&sdf);Monster_Free(m);
    }
    CreaturePhenotype invalid=r->adult;
    invalid.limbs[0].digitCount=3;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&invalid),"Rechazar tres cascos en un contrato ungulado mono/bidáctilo");
    invalid=r->adult;invalid.limbs[0].digitCount=0;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&invalid),"Rechazar ungulado sin casco");
    invalid=r->adult;invalid.limbs[0].dewclawSize=.2f;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&invalid),"El casco único no genera espolones de rumiante");
    Monster* juvenile=malloc(sizeof(*juvenile));Monster* adult=malloc(sizeof(*adult));
    TEST_ASSERT(juvenile&&adult,"Reservar transición équida");
    *juvenile=Monster_Create();*adult=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(juvenile,r,&r->juvenile)&&Creature_BuildMonster(adult,r,&r->adult),"Extremos de crecimiento");
    TEST_ASSERT(CreatureMorph_CollectTopologyEvents(juvenile,adult,NULL,0)==0,"Crecimiento équido sin cambios de topología falsos");
    for(unsigned step=0;step<=10;++step) {
        *m=Monster_Create();
        TEST_ASSERT(CreatureMorph_Interpolate(juvenile,adult,step/10.f,m),"Metamorfosis juvenil-adulto real");
        TEST_ASSERT(m->anatomyGraph.nodeCount==adult->anatomyGraph.nodeCount,"Cantidad estable de estaciones durante crecimiento");
        for(size_t i=0;i<m->anatomyGraph.nodeCount;++i) {
            const AnatomyNode* n=&m->anatomyGraph.nodes[i];
            TEST_ASSERT(isfinite(n->center.x)&&isfinite(n->center.y)&&isfinite(n->center.z),"Estaciones de crecimiento finitas");
            TEST_ASSERT(AnatomyGraph_FindNode(&adult->anatomyGraph,n->id),"IDs estables durante crecimiento");
        }
        Monster_Free(m);
    }
    Monster_Free(juvenile);Monster_Free(adult);free(juvenile);free(adult);free(m);free(doc);
}

void run_equine_anatomy_tests(void) {
    CranialComposition();
    FeetAndStages("recipes/caballo.creature.json");
    FeetAndStages("recipes/asno.creature.json");
    puts("[PASS] test_equine_anatomy: cráneo, casco único, apoyo, cuatro etapas, rig y metamorfosis");
}
