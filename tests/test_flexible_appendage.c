#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureRecipeIO.h"
#include "CreatureRecipes.h"
#include "FlexibleAppendage.h"
#include "MonsterSDF.h"
#include "Monster.h"
#include "MonsterAnimation.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void CheckPoses(const Monster* m) {
    const Skeleton* rig=&m->animation->rig.skeleton;
    SkeletonPose rest,posed;
    TEST_ASSERT(SkeletonPose_Init(&rest,rig),"Pose de reposo del apéndice");
    int root=Skeleton_FindJoint(rig,Anatomy_MakeId(40,1));
    int tip=Skeleton_FindJoint(rig,Anatomy_MakeId(40,FLEXIBLE_APPENDAGE_STATIONS));
    TEST_ASSERT(root>=0&&tip>=0,"Raíz y extremo persistentes");
    for(unsigned gesture=0;gesture<FLEXIBLE_POSE_COUNT;++gesture) {
        posed=rest;
        TEST_ASSERT(FlexibleAppendage_ApplyPose(40,gesture,1,rig,&posed),"Flexión del apéndice");
        TEST_ASSERT(Vec3_Distance(rest.joints[root].worldPosition,posed.joints[root].worldPosition)<1e-6f,
            "La raíz queda anclada al maxilar");
        for(unsigned i=2;i<=FLEXIBLE_APPENDAGE_STATIONS;++i) {
            int joint=Skeleton_FindJoint(rig,Anatomy_MakeId(40,i));
            TEST_ASSERT(joint>=0,"Estación cinemática");
            int parent=rig->joints[joint].parentIndex;
            float before=Vec3_Distance(rest.joints[joint].worldPosition,rest.joints[parent].worldPosition);
            float after=Vec3_Distance(posed.joints[joint].worldPosition,posed.joints[parent].worldPosition);
            TEST_ASSERT(fabsf(before-after)<1e-5f,"Longitud segmentaria conservada");
        }
        if(gesture!=FLEXIBLE_POSE_RELAXED)
            TEST_ASSERT(Vec3_Distance(rest.joints[tip].worldPosition,posed.joints[tip].worldPosition)>.10f,
                "Los gestos desplazan la punta");
    }
}

static void CheckWalk(Monster* m) {
    MonsterAnimation* animation=m->animation;
    animation->animator.desiredVelocity=Vec3_Create(0,0,.55f);
    FootState previous[RIG_MAX_LIMBS]={0};
    float maxError=0;
    unsigned contacts=0,swings=0;
    for(unsigned frame=0;frame<150;++frame) {
        TEST_ASSERT(MonsterAnimation_Update(m,1.f/60),"Marcha columnar mediante la pose existente");
        for(size_t i=0;i<animation->rig.limbCount;++i) {
            FootState state=animation->animator.locomotion.feet[i];
            if(state.phase==FOOT_SWING)++swings;
            if(frame>12&&state.phase==FOOT_STANCE&&previous[i].phase==FOOT_STANCE) {
                ++contacts;
                TEST_ASSERT(Vec3_Distance(state.plantedPosition,previous[i].plantedPosition)<1e-6f,
                    "El contacto queda fijo mientras se apoya la pata");
                const LimbRig* limb=&animation->rig.limbs[i];
                Vector3 foot=animation->pose.joints[limb->endEffectorJoint].worldPosition;
                maxError=fmaxf(maxError,Vec3_Distance(foot,state.plantedPosition));
            }
            previous[i]=state;
        }
    }
    printf("  apoyo columnar: %u muestras, error máximo %.5f\n",contacts,maxError);
    TEST_ASSERT(contacts>100&&swings>0&&maxError<.055f,"Las patas sostienen el cuerpo durante un paso completo");
}

static void CheckTerminal(const MonsterSDF* sdf,const MonsterSDFConnector* end) {
    Vector3 face=Vec3_Add(end->b,Vec3_Scale(end->forward,end->r2*.34f));
    Vector3 offset=Vec3_Scale(end->side,end->widthB*.39f);
    for(int side=-1;side<=1;side+=2) {
        Vector3 opening=Vec3_Add(face,Vec3_Scale(offset,side));
        Vector3 inner=Vec3_Sub(opening,Vec3_Scale(end->forward,end->r2*1.06f));
        TEST_ASSERT(MonsterSDF_EvaluateDistance(sdf,opening)>0,"Lumen distal abierto");
        TEST_ASSERT(MonsterSDF_EvaluateDistance(sdf,inner)<0,"Cavidad de profundidad finita");
        SDFField field=MonsterSDF_GetField(sdf);
        Vector3 half=Vec3_Create(.004f,.004f,.004f);
        AABB3D box=AABB_FromMinMax(Vec3_Sub(opening,half),Vec3_Add(opening,half));
        float lo,hi;
        TEST_ASSERT(field.getCellRange(field.context,box,&lo,&hi),"Intervalo del extremo");
        for(unsigned corner=0;corner<8;++corner) {
            Vector3 point=Vec3_Create(corner&1?box.end.x:box.start.x,corner&2?box.end.y:box.start.y,
                corner&4?box.end.z:box.start.z);
            float value=field.evaluateDistance(field.context,point);
            TEST_ASSERT(value>=lo-.0001f&&value<=hi+.0001f,"Cota conservadora de la abertura");
        }
    }
    Vector3 septum=Vec3_Sub(face,Vec3_Scale(end->forward,end->r2*.22f));
    TEST_ASSERT(MonsterSDF_EvaluateDistance(sdf,septum)<0,"Tabique entre los dos lúmenes");
}

static void CheckStage(const Monster* m, float age) {
    const HeadAnatomy* head=&m->head.anatomy;
    TEST_ASSERT(head->archetype==HEAD_ARCHETYPE_PROBOSCIDEAN && HeadAnatomy_Validate(head)==HEAD_VALID,
        "Cráneo proboscídeo y órbitas válidos");
    TEST_ASSERT(head->surface.hasEars && head->surface.earShape.y>head->surface.craniumRadii.y*2 &&
        head->surface.earShape.z<head->surface.earShape.x*.12f,"Láminas auriculares anchas y delgadas");
    TEST_ASSERT(!head->surface.hasNasalPad && head->surface.nostrilRadii.x==0,"Narinas sólo en el extremo móvil");
    MonsterSDF sdf=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig())&&sdf.axialStationCount>=8,
        "Barrido axial independiente y campo válido");
    const MonsterSDFConnector* end=NULL;
    unsigned nails=0,pads=0,tusks=0;
    for(size_t i=0;i<sdf.connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf.connectors[i];
        if(c->region==ANATOMY_REGION_FLEXIBLE) {
            TEST_ASSERT(!c->axialSweepMember,"Apéndice excluido del barrido del torso");
            TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,c->a)<0,"Centro de tejido conectado");
        }
        if(c->kind==BODY_CONNECTION_PAIRED_OPENINGS)end=c;
        if(c->kind==BODY_CONNECTION_NAIL) {
            TEST_ASSERT(c->material==SDF_MATERIAL_HOOF,"Placa de queratina");
            ++nails;
        }
        if(c->kind==BODY_CONNECTION_TUSK) {
            TEST_ASSERT(c->material==SDF_MATERIAL_DENTINE,"Incisivo de dentina");
            ++tusks;
        }
        if(c->kind==BODY_CONNECTION_PAD) {
            Vector3 sole=Vec3_Sub(Vec3_Lerp(c->a,c->b,.5f),Vec3_Scale(c->up,c->heightA));
            TEST_ASSERT(fabsf(sole.y)<.003f,"Cuatro suelas en el plano de apoyo");
            ++pads;
        }
    }
    TEST_ASSERT(end&&nails==18&&pads==4,"Extremo, almohadillas y número de placas");
    TEST_ASSERT(tusks==(age==0?0:16),"Incisivos progresivos sin alterar el plano corporal");
    CheckTerminal(&sdf,end);
    MonsterSDF_Free(&sdf);
}

static void CheckMorph(const Monster* elephant) {
    const CreatureRecipeId builtins[]={2,1};
    const char* external[]={"recipes/caballo.creature.json","recipes/cerdo.creature.json",
        "recipes/brumacerno.creature.json"};
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));
    Monster* other=malloc(sizeof(*other));
    Monster* blend=malloc(sizeof(*blend));
    TEST_ASSERT(doc&&other&&blend,"Reservar extremos de metamorfosis");
    *other=Monster_Create();
    *blend=Monster_Create();
    for(unsigned i=0;i<5;++i) {
        const CreatureRecipe* recipe;
        if(i<3) {
            char error[256];
            TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,external[i],error,sizeof(error)),error);
            recipe=&doc->recipe;
        } else {
            recipe=CreatureRecipes_Find(builtins[i-3]);
        }
        TEST_ASSERT(recipe&&Creature_BuildMonster(other,recipe,&recipe->adult),"Construir extremo externo o incorporado");
        for(unsigned direction=0;direction<2;++direction) {
            const Monster* from=direction?elephant:other;
            const Monster* to=direction?other:elephant;
            for(unsigned sample=0;sample<=4;++sample) {
                float t=sample/4.f;
                TEST_ASSERT(CreatureMorph_Interpolate(from,to,t,blend),"Interpolación bidireccional");
                if(blend->head.anatomy.surface.hasEars) {
                    const HeadSurfaceRecipe* ear=&blend->head.anatomy.surface;
                    TEST_ASSERT(fabsf(Vec3_Length(ear->earDirection)-1)<1e-4f &&
                        fabsf(Vec3_Length(ear->leftEarSide)-1)<1e-4f &&
                        fabsf(Vec3_Dot(ear->earDirection,ear->leftEarNormal))<1e-4f &&
                        fabsf(Vec3_Dot(ear->earDirection,ear->leftEarSide))<1e-4f,
                        "El marco de la pinna conserva su base ortonormal durante la inversión");
                }
                MonsterSDF sdf=MonsterSDF_Create();
                TEST_ASSERT(MonsterSDF_Build(&sdf,blend,MonsterSDF_DefaultConfig())&&sdf.axialStationCount>=7,
                    "Campo interpolado y barrido axial válidos");
                unsigned terminal=0;
                for(size_t c=0;c<sdf.connectorCount;++c) {
                    const MonsterSDFConnector* connection=&sdf.connectors[c];
                    if(connection->kind!=BODY_CONNECTION_PAIRED_OPENINGS)continue;
                    TEST_ASSERT(!connection->axialSweepMember && connection->widthB<=.16f,
                        "El extremo mantiene propiedad y radio local");
                    ++terminal;
                }
                TEST_ASSERT(terminal==(direction?sample!=4:sample!=0),"El apéndice se retira en su extremo correcto");
                MonsterSDF_Free(&sdf);
            }
        }
    }
    Monster_Free(other); Monster_Free(blend);
    free(other); free(blend); free(doc);
}

void run_flexible_appendage_tests(void) {
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));
    CreatureRecipeDocument* copy=calloc(1,sizeof(*copy));
    Monster* m=malloc(sizeof(*m));
    TEST_ASSERT(doc&&copy&&m,"Reservar receta y anatomía proboscídea");
    char error[256];
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,"recipes/elefante.creature.json",error,sizeof(error)),error);
    FILE* file=tmpfile();
    TEST_ASSERT(file&&CreatureRecipeIO_WriteJSON(file,&doc->recipe),"Serializar el módulo nuevo");
    long size=ftell(file);
    TEST_ASSERT(size>0,"JSON serializado");
    char* json=malloc((size_t)size+1);
    TEST_ASSERT(json,"Reservar JSON para relectura");
    rewind(file);
    TEST_ASSERT(fread(json,1,(size_t)size,file)==(size_t)size,"Leer JSON serializado");
    json[size]=0;
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(copy,json,error,sizeof(error)),error);
    free(json); fclose(file);
    TEST_ASSERT(copy->recipe.id==1008 && copy->recipe.adult.flexibleAppendageCount==1 &&
        copy->recipe.adult.limbs[0].nailCount==5,"Identidad, módulo y placas tras ida y vuelta");
    *m=Monster_Create();
    for(unsigned i=0;i<=6;++i) {
        float age=i/6.f;
        CreaturePhenotype phenotype=CreatureRecipe_EvaluateDevelopment(&copy->recipe,age);
        TEST_ASSERT(Creature_BuildMonster(m,&copy->recipe,&phenotype),"Construir las cuatro etapas y edades intermedias");
        CheckStage(m,age);
    }
    CheckPoses(m);
    CheckWalk(m);
    CheckMorph(m);
    Monster_Free(m);
    free(m); free(copy); free(doc);
    puts("[PASS] test_flexible_appendage_pipeline");
}
