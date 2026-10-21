/** @file test_cloven_hoof.c
 * @brief Cápsulas unguladas: apoyo, controles, poda y cotas del campo real. */
#include "CreatureRecipes.h"
#include "Creature.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "Limb.h"
#include "SDFInterval.h"
#include "test_utils.h"
#include <stdlib.h>
#include <math.h>

static void ClawField(void) {
    for(unsigned shape=0;shape<6;++shape) {
        float taper=.05f+.07f*shape,roundness=.35f+.12f*shape;
        for(unsigned cell=0;cell<120;++cell) {
            Vector3 center={((cell*19)%31-15.f)*.023f,((cell*11)%29-14.f)*.026f,((cell*7)%37-9.f)*.025f};
            Vector3 half={.013f,.017f,.019f};
            AABB3D box={Vec3_Sub(center,half),Vec3_Add(center,half)};
            SDFInterval range=SDF_KeratinClawInterval(box,Vec3_Zero(),Vec3_Create(0,0,1),Vec3_Create(1,0,0),Vec3_Create(0,1,0),
                .45f,.22f,.19f,.16f,.22f,taper,roundness);
            for(unsigned corner=0;corner<9;++corner) {
                Vector3 p=corner==8?center:Vec3_Create(corner&1?box.end.x:box.start.x,corner&2?box.end.y:box.start.y,corner&4?box.end.z:box.start.z);
                float d=SDF_KeratinClaw(p,.45f,.22f,.19f,.16f,.22f,taper,roundness);
                TEST_ASSERT(isfinite(d)&&d>=range.lo&&d<=range.hi,"Intervalo incluye cápsulas con paredes y coronas variables");
            }
        }
        for(unsigned x=0;x<5;++x)for(unsigned z=0;z<5;++z) {
            Vector3 p={((float)x-2)*.045f,-.16f,.08f+z*.065f};
            TEST_ASSERT(fabsf(SDF_KeratinClaw(p,.45f,.22f,.19f,.16f,.22f,taper,roundness))<1e-6f,"La suela sustenta un área plana, no un único punto");
            p.y-=.001f;TEST_ASSERT(SDF_KeratinClaw(p,.45f,.22f,.19f,.16f,.22f,taper,roundness)>0,"No hay queratina bajo el plano de apoyo");
        }
    }
}

static void AnatomicalFeet(void) {
    const CreatureRecipe* recipe=CreatureRecipes_Brumacerno();Monster* m=malloc(sizeof(*m));
    TEST_ASSERT(m,"Reservar anatomía de prueba");
    for(unsigned stage=0;stage<4;++stage) {
        *m=Monster_Create();const CreaturePhenotype* p=CreatureRecipe_GetStage(recipe,(CreatureStage)stage);
        TEST_ASSERT(Creature_BuildMonster(m,recipe,p),"Resolver las cuatro etapas unguladas");
        MonsterSDF sdf=MonsterSDF_Create();TEST_ASSERT(MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig()),"Compilar cápsulas y suelas");
        for(unsigned leg=0;leg<4;++leg) {
            MonsterSDF view;MonsterSDFConnector storage[100];
            TEST_ASSERT(MonsterSDF_BuildModuleView(&sdf,10+leg,&view,storage,100),"Campo del pie independiente de la especie");
            const AnatomyNode* foot=AnatomyGraph_FindModuleNode(&m->anatomyGraph,10+leg,4);
            LimbRigDescriptor descriptor;
            TEST_ASSERT(Limb_BuildRigDescriptor(&p->limbs[leg],10+leg,&m->anatomyGraph,&descriptor),"Descriptor de apoyo ungulígrado");
            unsigned bearing=0;
            for(size_t i=0;i<view.connectorCount;++i) {
                const MonsterSDFConnector* c=&view.connectors[i];
                if(c->material!=SDF_MATERIAL_HOOF)continue;
                if(c->a.y-c->heightA>foot->center.y-descriptor.soleHeight+.001f)continue;
                ++bearing;
                TEST_ASSERT(fabsf((c->a.y-c->heightA)-(foot->center.y-descriptor.soleHeight))<1e-5f,"El rig usa la misma suela que el campo");
                for(unsigned sample=0;sample<80;++sample) {
                    Vector3 q=Vec3_Add(c->a,Vec3_Create(((sample*19)%17-8.f)*c->widthA*.19f,
                        ((sample*7)%19-9.f)*c->heightB*.18f,((sample*11)%23-5.f)*c->length*.08f));
                    float pruned=MonsterSDF_EvaluateDistance(&view,q);view.config.enableConnectorPruning=false;
                    float exhaustive=MonsterSDF_EvaluateDistance(&view,q);view.config.enableConnectorPruning=true;
                    TEST_ASSERT(fabsf(pruned-exhaustive)<1e-5f,"La poda conserva el campo alrededor de las ocho uñas");
                    Vector3 h=Vec3_Create(.006f,.007f,.008f);AABB3D cell={Vec3_Sub(q,h),Vec3_Add(q,h)};
                    SDFField field=MonsterSDF_GetField(&sdf);float lo,hi;
                    float full=MonsterSDF_EvaluateDistance(&sdf,q);
                    TEST_ASSERT(field.getCellRange(field.context,cell,&lo,&hi)&&full>=lo&&full<=hi,"Cotas de celda conservadoras alrededor de las suelas");
                }
            }
            TEST_ASSERT(bearing==2,"Exactamente dos uñas sustentantes por pie, sin contar espolones");
            const AnatomyNode* a=AnatomyGraph_FindModuleNode(&m->anatomyGraph,10+leg,Limb_DigitLocalId(0,1));
            const AnatomyNode* b=AnatomyGraph_FindModuleNode(&m->anatomyGraph,10+leg,Limb_DigitLocalId(1,1));
            const AnatomyNode* toeA=AnatomyGraph_FindModuleNode(&m->anatomyGraph,10+leg,Limb_DigitLocalId(0,2));
            const AnatomyNode* toeB=AnatomyGraph_FindModuleNode(&m->anatomyGraph,10+leg,Limb_DigitLocalId(1,2));
            for(unsigned sample=0;sample<=8;++sample) {
                Vector3 cleft=Vec3_Lerp(Vec3_Lerp(a->center,b->center,.5f),Vec3_Lerp(toeA->center,toeB->center,.5f),sample/8.f);
                cleft.y=foot->center.y-descriptor.soleHeight+.02f*p->axial.totalScale;
                if(MonsterSDF_EvaluateDistance(&view,cleft)<=0) {
                    printf("cleft stage=%u leg=%u sample=%u point=%g,%g,%g d=%g\n",stage,leg,sample,cleft.x,cleft.y,cleft.z,MonsterSDF_EvaluateDistance(&view,cleft));
                    for(size_t c=0;c<view.connectorCount;++c){MonsterSDF one=view;one.connectors=&storage[c];one.connectorCount=1;one.config.enableConnectorPruning=false;float d=MonsterSDF_EvaluateDistance(&one,cleft);if(d<.01f)printf(" edge %u->%u material=%u d=%g\n",storage[c].fromId&65535u,storage[c].toId&65535u,storage[c].material,d);}
                }
                TEST_ASSERT(MonsterSDF_EvaluateDistance(&view,cleft)>0,"Hendidura abierta desde talones hasta puntas, sin cojín común en la suela");
            }
        }
        MonsterSDF_Free(&sdf);Monster_Free(m);
    }
    free(m);
}

static void MorphologyControls(void) {
    const CreatureRecipe* recipe=CreatureRecipes_Brumacerno();
    CreaturePhenotype p=recipe->adult;Monster* a=malloc(sizeof(*a));Monster* b=malloc(sizeof(*b));
    TEST_ASSERT(a&&b,"Reservar variantes procedurales");*a=Monster_Create();*b=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(a,recipe,&p),"Pie base");
    p.limbs[0].digitSpread+=.3f;p.limbs[0].digitAngles[0]-=.15f;p.limbs[0].digitLengths[0]*=1.3f;
    p.limbs[0].hoofWallSlope=.4f;p.limbs[0].hoofToeRoundness=.4f;
    TEST_ASSERT(Creature_BuildMonster(b,recipe,&p),"Pie con separación, ángulo y longitud distintos");
    for(unsigned station=1;station<=2;++station) {
        const AnatomyNode* x=AnatomyGraph_FindModuleNode(&a->anatomyGraph,10,Limb_DigitLocalId(0,station));
        const AnatomyNode* y=AnatomyGraph_FindModuleNode(&b->anatomyGraph,10,Limb_DigitLocalId(0,station));
        TEST_ASSERT(Vec3_Distance(x->center,y->center)>.01f,"Los controles digitales ya no se sobrescriben");
    }
    CreaturePhenotype middle=CreaturePhenotype_Interpolate(&recipe->adult,&p,.5f);
    TEST_ASSERT(middle.limbs[0].hoofWallSlope>recipe->adult.limbs[0].hoofWallSlope&&middle.limbs[0].hoofWallSlope<p.limbs[0].hoofWallSlope,"La pendiente se interpola durante desarrollo y metamorfosis");
    Monster_Free(a);Monster_Free(b);free(a);free(b);
}

void run_cloven_hoof_tests(void) {ClawField();AnatomicalFeet();MorphologyControls();}
