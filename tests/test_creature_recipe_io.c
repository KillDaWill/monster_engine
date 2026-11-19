/** @file test_creature_recipe_io.c
 * @brief Persistencia fiel, rechazo transaccional y construcción desde archivos externos.
 */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "CreatureRecipeRegistry.h"
#include "HeadModule.h"
#include "AxialBody.h"
#include "Limb.h"
#include "MonsterVisual.h"
#include "MonsterAnimation.h"
#include "SDFPrimitives.h"
#include "Ornament.h"
#include <math.h>
#include "Monster.h"
#include "test_utils.h"
#include <stdlib.h>
#include <string.h>

static char* Encode(const CreatureRecipe* recipe){
    FILE* file=tmpfile();TEST_ASSERT(file,"Flujo temporal JSON");
    TEST_ASSERT(CreatureRecipeIO_WriteJSON(file,recipe),"Serializar receta completa");
    long size=ftell(file);rewind(file);char* text=malloc((size_t)size+1);
    TEST_ASSERT(text,"Reservar documento");TEST_ASSERT(fread(text,1,(size_t)size,file)==(size_t)size,"Leer JSON");
    text[size]=0;fclose(file);return text;
}
static void PoseNasalHorns(const Monster* monster) {
    const Skeleton* skeleton=&monster->animation->rig.skeleton;
    const AnatomyGraph* rest=&monster->anatomyGraph;
    AnatomyGraph* posed=malloc(sizeof(*posed));
    SkeletonPose pose;
    TEST_ASSERT(posed && SkeletonPose_Init(&pose,skeleton),"Pose de reposo del rinoceronte");
    int head=Skeleton_FindJoint(skeleton,Anatomy_MakeId(2,HEAD_NODE_MAIN));
    TEST_ASSERT(head>=0,"La cabeza forma parte del rig");
    Vector3 pivot=pose.joints[head].worldPosition;
    Quaternion before=pose.joints[head].worldRotation;
    pose.joints[head].localRotation=Quat_Multiply(pose.joints[head].localRotation,
        Quat_FromAxisAngle(Vec3_Create(0,1,0),.31f));
    TEST_ASSERT(SkeletonPose_UpdateWorldTransforms(&pose,skeleton) &&
        SkeletonPose_ToAnatomy(skeleton,&pose,rest,posed),"Posar cabeza y ornamentos");
    Quaternion delta=Quat_Multiply(pose.joints[head].worldRotation,Quat_Conjugate(before));
    for(unsigned horn=30;horn<=31;++horn) {
        const AnatomyNode* tip=AnatomyGraph_FindModuleNode(rest,horn,ORNAMENT_NODE_TIP);
        const AnatomyNode* moved=AnatomyGraph_FindModuleNode(posed,horn,ORNAMENT_NODE_TIP);
        TEST_ASSERT(tip && moved,"Puntas de cuerno con IDs de pose");
        Vector3 expected=Vec3_Add(pivot,Quat_RotateVector(delta,Vec3_Sub(tip->center,pivot)));
        TEST_ASSERT(Vec3_Distance(expected,moved->center)<1e-4f,"El cuerno sigue el marco cefálico");
    }
    free(posed);
}

static void RatTailDuringWalking(Monster* monster) {
    MonsterAnimation* animation=monster->animation;
    TEST_ASSERT(animation,"La rata tiene rig de marcha");
    AnatomyGraph* posed=malloc(sizeof(*posed));
    TEST_ASSERT(posed,"Grafo posado para el barrido de cola");
    animation->animator.desiredVelocity=Vec3_Create(0,0,.8f*monster->phenotype.axial.totalScale);
    for(int frame=0;frame<60;++frame) {
        TEST_ASSERT(MonsterAnimation_Update(monster,1.f/60),"Avanzar marcha de la rata");
        TEST_ASSERT(SkeletonPose_ToAnatomy(&animation->rig.skeleton,&animation->pose,
            &monster->anatomyGraph,posed),"Obtener estaciones caudales posadas");
        for(unsigned station=1;station<=16;++station) {
            const AnatomyNode* tail=AnatomyGraph_FindModuleNode(posed,20,station);
            TEST_ASSERT(tail && tail->center.y-tail->heightRadius>=-.015f,
                "La cola mantiene holgura con el plano de apoyo en marcha");
        }
    }
    free(posed);
}

static void RatPaw(const MonsterSDF* sdf,unsigned module,unsigned toes) {
    MonsterSDF foot;
    MonsterSDFConnector storage[64];
    TEST_ASSERT(MonsterSDF_BuildModuleView(sdf,module,&foot,storage,64),"Campo aislado del pie");
    const MonsterSDFConnector* pad=NULL;
    for(size_t i=0;i<foot.connectorCount;++i) {
        const MonsterSDFConnector* c=&foot.connectors[i];
        if((c->toId&65535u)==LIMB_NODE_PALM_BALL) {
            TEST_ASSERT(!pad,"Cada pie tiene una sola planta de tejido continuo");
            pad=c;
        }
        TEST_ASSERT((c->fromId&65535u)!=LIMB_NODE_PALM_BALL,
            "Las conexiones internas a las bases digitales no emiten radios desde la palma");
    }
    TEST_ASSERT(pad && pad->kind==BODY_CONNECTION_MUSCLE_UNION && pad->material==SDF_MATERIAL_SKIN,"La planta es tejido blando");
    float sole=pad->a.y-pad->heightA;
    for(int sample=1;sample<5;++sample) {
        float t=sample/5.f;
        Vector3 q=Vec3_Lerp(pad->a,pad->b,t);
        q.y=sole+pad->heightA*.15f;
        TEST_ASSERT(MonsterSDF_EvaluateDistance(&foot,q)<0,"Superficie plantar continua por encima de la suela");
    }
    for(unsigned d=0;d<toes;++d) {
        AnatomyId root=Anatomy_MakeId(module,Limb_DigitLocalId(d,0));
        for(size_t i=0;i<foot.connectorCount;++i) {
            const MonsterSDFConnector* c=&foot.connectors[i];
            if(c->fromId!=root)continue;
            float d=SDF_TaperedEllipticalSegmentApprox(c->a,pad->a,pad->b,
                pad->widthA,pad->heightA,pad->widthB,pad->heightB);
            TEST_ASSERT(d<c->widthA*.60f,"Cada base digital solapa el tejido plantar");
        }
    }
}

static void WildlifeRecipes(void) {
    char error[256];
    TEST_ASSERT(CreatureRecipeRegistry_LoadDirectory("recipes",error,sizeof(error)),"Catálogo externo completo");
    const CreatureRecipeId ids[]={1009,1010};
    CreatureRecipeDocument* doc=malloc(sizeof(*doc));
    Monster* monster=malloc(sizeof(*monster));
    TEST_ASSERT(doc&&monster,"Reservar fauna de prueba");
    TEST_ASSERT(HEAD_SLOT_MAXILLARY_RIGHT==14 && HEAD_SLOT_NASAL_ANTERIOR==15 &&
        HEAD_SLOT_NASAL_POSTERIOR==16,"No renumerar ranuras faciales");
    TEST_ASSERT(HEAD_ARCHETYPE_PROBOSCIDEAN==6 && HEAD_ARCHETYPE_RHINOCEROTID==7 &&
        HEAD_ARCHETYPE_RODENT==8,"No renumerar familias cefálicas");
    for(size_t species=0;species<sizeof(ids)/sizeof(ids[0]);++species) {
        bool rhino=species==0;
        const CreatureRecipe* recipe=CreatureRecipeRegistry_Find(ids[species]);
        TEST_ASSERT(recipe,"Encontrar ID de fauna en el catálogo");
        char* original=Encode(recipe);
        TEST_ASSERT(CreatureRecipeIO_ReadJSON(doc,original,error,sizeof(error)),"Leer receta externa serializada");
        char* restored=Encode(&doc->recipe);
        TEST_ASSERT(!strcmp(original,restored),"La escritura conserva todos los parámetros");
        free(original);free(restored);
        size_t nodes=0,edges=0;
        for(int i=0;i<=6;++i) {
            CreaturePhenotype p=CreatureRecipe_EvaluateDevelopment(&doc->recipe,i/6.f);
            TEST_ASSERT(CreatureRecipe_Validate(&doc->recipe,&p),"Validar etapa o edad intermedia");
            TEST_ASSERT(p.head.archetype==(rhino?HEAD_ARCHETYPE_RHINOCEROTID:HEAD_ARCHETYPE_RODENT),"Familia propia");
            *monster=Monster_Create();
            TEST_ASSERT(Creature_BuildMonster(monster,&doc->recipe,&p),"Construir anatomía, pose y aspecto");
            TEST_ASSERT(monster->eyeCount==2 && HeadAnatomy_Validate(&monster->head.anatomy)==0,"Landmarks cefálicos válidos");
            const AnatomyGraph* g=&monster->anatomyGraph;
            if(i==0){nodes=g->nodeCount;edges=g->connectionCount;}
            TEST_ASSERT(nodes==g->nodeCount && edges==g->connectionCount,"Topología estable durante crecimiento");
            MonsterSDF sdf=MonsterSDF_Create();
            TEST_ASSERT(MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig()),"Construir campo de fauna");
            const MonsterSDFMouth* head=&sdf.mouths[0];
            TEST_ASSERT(sdf.mouthCount==1 && head->neckCollarMidRadii.x >=
                fminf(head->neckCollarRootRadii.x,head->neckCollarTipRadii.x),
                "La transición no introduce un estrechamiento artificial");
            TEST_ASSERT(sdf.axialStationCount>1 && sdf.axialStations[0].widthSlope==0 &&
                sdf.axialStations[0].heightSlope==0 && sdf.axialStations[0].centerSlope==0 &&
                head->headStations[5].widthSlope==0 && head->headStations[5].heightSlope==0 &&
                head->headStations[5].centerSlope==0,"Tangentes compatibles en los extremos de nuca y cuello");
            for(unsigned j=1;j<=16;++j) {
                const AnatomyNode* tail=AnatomyGraph_FindModuleNode(g,20,j);
                TEST_ASSERT(tail && MonsterSDF_EvaluateDistance(&sdf,tail->center)<0,
                    "Las estaciones de la cola quedan en un campo continuo");
            }
            for(unsigned leg=0;leg<4;++leg) {
                unsigned toes=rhino?3:(leg<2?4:5), material=0;
                const LimbPhenotype* limb=&p.limbs[leg];
                TEST_ASSERT(limb->digitCount==toes,"Conteo de dedos por miembro");
                for(unsigned d=0;d<toes;++d)
                    TEST_ASSERT(AnatomyGraph_FindModuleNode(g,10+leg,Limb_DigitLocalId(d,2)),"Falange distal resuelta");
                for(size_t j=0;j<sdf.connectorCount;++j) {
                    const MonsterSDFConnector* c=&sdf.connectors[j];
                    if(c->moduleInstanceId==10+leg && c->material==SDF_MATERIAL_HOOF) ++material;
                }
                TEST_ASSERT(material==toes,"Una uña de queratina por dedo");
                if(!rhino)RatPaw(&sdf,10+leg,toes);
            }
            if(!rhino) {
                /* La separación de las dos plantas traseras incluye la falange
                 * terminal, que se abre más que la base del dedo. */
                MonsterSDF rightFoot;
                MonsterSDFConnector rightFootStorage[64];
                TEST_ASSERT(MonsterSDF_BuildModuleView(&sdf,13,&rightFoot,rightFootStorage,64),
                    "Campo de la planta contralateral");
                for(unsigned digit=0;digit<5;++digit) {
                    const AnatomyNode* tip=AnatomyGraph_FindModuleNode(g,12,Limb_DigitLocalId(digit,4));
                    TEST_ASSERT(tip && MonsterSDF_EvaluateDistance(&rightFoot,tip->center)>tip->widthRadius,
                        "Los dedos traseros no invaden el pie opuesto en reposo");
                }
            }
            if(rhino) {
                const AnatomyNode* shoulder=AnatomyGraph_FindModuleNode(g,1,AXIAL_NODE_PECTORAL);
                const AnatomyNode* thorax=AnatomyGraph_FindModuleNode(g,1,AXIAL_NODE_THORAX_POSTERIOR);
                TEST_ASSERT(shoulder && thorax,"Estaciones de cruz y lomo");
                int crest=1;
                while(crest<sdf.axialStationCount &&
                    fabsf(sdf.axialStations[crest].center.z-shoulder->center.z)>1e-5f)++crest;
                TEST_ASSERT(crest<sdf.axialStationCount,"La cruz forma parte del barrido");
                const SDFSweepStation* stations=sdf.axialStations;
                float peak=stations[crest].center.y+stations[crest].height;
                float before=stations[crest-1].center.y+stations[crest-1].height;
                TEST_ASSERT(peak-before<.08f*p.axial.totalScale &&
                    peak-(thorax->center.y+thorax->heightRadius)>.20f*p.axial.totalScale,
                    "Cresta redondeada que conserva relieve sobre el lomo");
                const AnatomyNode* front=AnatomyGraph_FindModuleNode(g,30,ORNAMENT_NODE_BASE);
                const AnatomyNode* rear=AnatomyGraph_FindModuleNode(g,31,ORNAMENT_NODE_BASE);
                TEST_ASSERT(front && rear && front->center.z>rear->center.z,"Cuernos ordenados sobre el puente nasal");
                TEST_ASSERT(fabsf(front->center.x)<1e-6f && fabsf(rear->center.x)<1e-6f &&
                    front->widthRadius>rear->widthRadius,"Cuernos medianos con bases diferenciadas");
                TEST_ASSERT(MonsterSDF_EvaluateDebug(&sdf,front->center,MONSTER_HEAD_DEBUG_LOCAL_HEAD).distance<0 &&
                    MonsterSDF_EvaluateDebug(&sdf,rear->center,MONSTER_HEAD_DEBUG_LOCAL_HEAD).distance<0,
                    "Las bases penetran el tejido cefálico de cada etapa");
                if(i==6)PoseNasalHorns(monster);
                const AnatomyNode* spread=AnatomyGraph_FindModuleNode(g,10,Limb_DigitLocalId(0,2));
                const AnatomyNode* center=AnatomyGraph_FindModuleNode(g,10,Limb_DigitLocalId(1,2));
                TEST_ASSERT(center->center.z>spread->center.z && center->widthRadius>spread->widthRadius,
                    "El dedo central es el más largo y ancho");
            } else if(i==6) {
                const HeadSurfaceRecipe* face=&monster->head.anatomy.surface;
                TEST_ASSERT(face->craniumRadii.x<1.60f*face->craniumRadii.y &&
                    face->leftMaxillaryCenter.x+face->maxillaryRadii.x<face->faceMidRadii.x,
                    "Cráneo estrecho y cojinetes integrados en el hocico");
                LongHairSet hair={0};
                TEST_ASSERT(MonsterVisual_UpdateLongHair(&hair,monster),"Proyectar las vibrisas en la piel");
                unsigned visible[2]={0};
                for(size_t j=0;j<hair.count;++j) {
                    const LongHairGuide* guide=&hair.guides[j];
                    if(guide->visible) ++visible[guide->groupIndex];
                }
                TEST_ASSERT(visible[0]>=6 && visible[1]>=6,"Vibrisas visibles a ambos lados");
                LongHairSet_Free(&hair);
                RatTailDuringWalking(monster);
            }
            MonsterSDF_Free(&sdf);Monster_Free(monster);
        }
    }
    CreatureRecipeRegistry_Clear();
    free(doc);free(monster);
}

void run_creature_recipe_io_tests(void){
    WildlifeRecipes();
    CreatureRecipeDocument* doc=malloc(sizeof(*doc));Monster* monster=malloc(sizeof(*monster));
    TEST_ASSERT(doc&&monster,"Reservar documentos y criatura");char error[256];
    for(unsigned id=1;id<=4;++id){const CreatureRecipe* recipe=CreatureRecipes_Find(id);char* original=Encode(recipe);
        TEST_ASSERT(CreatureRecipeIO_ReadJSON(doc,original,error,sizeof(error)),"Importar las cuatro recetas actuales");
        char* restored=Encode(&doc->recipe);TEST_ASSERT(!strcmp(original,restored),"Conservar todos los campos y etapas exactamente");
        TEST_ASSERT(doc->recipe.name==doc->name,"El documento posee su nombre");
        for(int stage=0;stage<4;++stage){
            AnatomyGraph* source=malloc(sizeof(*source));AnatomyGraph* loaded=malloc(sizeof(*loaded));
            TEST_ASSERT(source&&loaded,"Reservar comparación anatómica");
            TEST_ASSERT(Creature_ResolveAnatomy(recipe,CreatureRecipe_GetStage(recipe,(CreatureStage)stage),source),"Resolver etapa fuente");
            TEST_ASSERT(Creature_ResolveAnatomy(&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage),loaded),"Resolver etapa persistida");
            TEST_ASSERT(AnatomyGraph_Fingerprint(source)==AnatomyGraph_Fingerprint(loaded),"La receta persistida conserva la anatomía de cada etapa");
            free(source);free(loaded);
        }
        *monster=Monster_Create();TEST_ASSERT(Creature_BuildMonster(monster,&doc->recipe,&doc->recipe.adult),"Construir rig desde JSON");Monster_Free(monster);
        free(original);free(restored);
    }
    doc->recipe.id=12345;strcpy(doc->name,"Criatura \"Ámbar\" \\ bosque");
    doc->recipe.adult.axial.trunkLength*=1.2f;
    char* text=Encode(&doc->recipe);
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(doc,text,error,sizeof(error)),"Nombre UTF-8, comillas y barras");
    TEST_ASSERT(doc->recipe.id==12345&&!strcmp(doc->name,"Criatura \"Ámbar\" \\ bosque"),"Identidad externa estable");
    char* version=strstr(text,"\"version\":1");TEST_ASSERT(version,"Versión explícita");version[10]='2';
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,text,error,sizeof(error)),"Rechazar versiones futuras");
    TEST_ASSERT(doc->recipe.id==12345,"Error no modifica el documento destino");version[10]='1';
    char* modules=strstr(text,"\"moduleCount\":");TEST_ASSERT(modules,"Conteo serializado");
    char previous=modules[14];modules[14]='0';
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,text,error,sizeof(error)),"Rechazar discrepancia entre array y conteo");modules[14]=previous;
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{\"version\":1,\"version\":1}",error,sizeof(error)),"Rechazar campos duplicados");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{}",error,sizeof(error)),"No completar documentos vacíos con otra especie");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{",error,sizeof(error)),"Rechazar JSON truncado");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{\"version\":1,\"recipe\":{\"name\":\"\\",error,sizeof(error)),"Rechazar escape truncado sin leer fuera del texto");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,"{\"version\":1,\"recipe\":{\"name\":\"\\ud800 \\udc00\"}}",error,sizeof(error)),"No aceptar espacios dentro de un par Unicode");
    char* extra=malloc(strlen(text)+8);TEST_ASSERT(extra,"Reservar JSON extendido");strcpy(extra,text);strcat(extra,"false");
    TEST_ASSERT(!CreatureRecipeIO_ReadJSON(doc,extra,error,sizeof(error)),"Rechazar contenido después del documento");
    free(extra);free(text);free(doc);free(monster);
}
