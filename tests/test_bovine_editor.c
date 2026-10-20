/** @file test_bovine_editor.c
 * @brief Receta editada: rig ungulado, material de cuernos/cascos y cobertura real.
 */
#include "CreatureRecipeIO.h"
#include "CreatureRecipeRegistry.h"
#include "Creature.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "Fur.h"
#include "VisualVariation.h"
#include "Limb.h"
#include "MathUtils.h"
#include "EcosystemScenario.h"
#include "EcosystemVisual.h"
#include <time.h>
#include "test_utils.h"
#include <stdlib.h>
#include <math.h>

/** @brief Compara campos serializados, incluidas las cuatro etapas del respaldo C. */
static void BovineCanonical(void) {
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));
    CreatureRecipe* normalized=malloc(sizeof(*normalized));char error[256];
    TEST_ASSERT(doc&&normalized,"Reservar comparación canónica");
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,"recipes/brumacerno.creature.json",error,sizeof(error)),"Leer JSON bovino");
    *normalized=doc->recipe;
    CreaturePhenotype_Normalize(&normalized->seed);CreaturePhenotype_Normalize(&normalized->larva);
    CreaturePhenotype_Normalize(&normalized->juvenile);CreaturePhenotype_Normalize(&normalized->adult);
    FILE* external=tmpfile();FILE* builtin=tmpfile();TEST_ASSERT(external&&builtin,"Flujos de comparación");
    TEST_ASSERT(CreatureRecipeIO_WriteJSON(external,normalized)&&CreatureRecipeIO_WriteJSON(builtin,CreatureRecipes_Brumacerno()),"Serializar ambos catálogos");
    TEST_ASSERT(ftell(external)==ftell(builtin),"Catálogo JSON y respaldo C tienen los mismos campos");
    rewind(external);rewind(builtin);int a,b;
    do{a=fgetc(external);b=fgetc(builtin);TEST_ASSERT(a==b,"El toro incorporado no puede divergir silenciosamente del JSON");}while(a!=EOF);
    fclose(external);fclose(builtin);free(normalized);free(doc);
}

/** @brief Proporciones, labios inclinados y continuidad de cuernos, cascos y cola. */
static void BovineAnatomy(void) {
    const CreatureRecipe* recipe=CreatureRecipes_Brumacerno();Monster* monster=malloc(sizeof(*monster));
    TEST_ASSERT(monster,"Reservar anatomía bovina");
    for(unsigned stage=0;stage<4;++stage) {
        const CreaturePhenotype* p=CreatureRecipe_GetStage(recipe,(CreatureStage)stage);
        *monster=Monster_Create();TEST_ASSERT(Creature_BuildMonster(monster,recipe,p),"Resolver todas las etapas bovinas");
        const HeadAnatomy* h=&monster->head.anatomy;
        TEST_ASSERT(h->landmarks.leftMouthCorner.z>Math_Lerp(h->landmarks.muzzleRoot.z,h->landmarks.muzzleTip.z,.5f),"Comisura distal de rumiante, sin sonrisa canina larga");
        TEST_ASSERT(h->archetype==HEAD_ARCHETYPE_BOVID&&HeadAnatomy_Validate(h)==HEAD_VALID,"Landmarks bóvidos finitos y simétricos");
        TEST_ASSERT(h->surface.faceTipRadii.x>h->surface.faceMidRadii.x*1.2f,"El morro ensancha después del puente nasal");
        TEST_ASSERT(h->surface.faceTip.y<h->surface.faceRoot.y&&h->surface.oralSlope<0,"Rostro y labios descienden juntos");
        TEST_ASSERT(h->surface.hasEars&&h->surface.hasNasalPad,"Conservar pabellones y planum nasal");
        MonsterSDF sdf=MonsterSDF_Create();TEST_ASSERT(MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig()),"Construir SDF bóvido");
        for(size_t i=0;i<sdf.connectorCount;++i) {
            const MonsterSDFConnector* c=&sdf.connectors[i];
            if(c->material!=SDF_MATERIAL_HORN&&c->material!=SDF_MATERIAL_HOOF&&c->region!=ANATOMY_REGION_TAIL)continue;
            if(c->region==ANATOMY_REGION_TAIL)TEST_ASSERT(!c->axialSweepMember,"La cola colgante no se comprime en el barrido Z");
            for(unsigned j=0;j<=12;++j) {
                float d=MonsterSDF_EvaluateDistance(&sdf,Vec3_Lerp(c->a,c->b,(float)j/12));
                TEST_ASSERT(isfinite(d)&&d<=1e-5f,"Cuernos, cascos y cola conservan tejido entre estaciones");
            }
        }
        if(stage==3) {
            SDFField field=MonsterSDF_GetField(&sdf);
            Vector3 features[]={sdf.mouths[0].leftNostrilCenterLocal,sdf.mouths[0].rightNostrilCenterLocal,sdf.mouths[0].leftOrbitCenterLocal,sdf.mouths[0].rightOrbitCenterLocal};
            for(unsigned feature=0;feature<4;++feature)for(unsigned cell=0;cell<32;++cell) {
                Vector3 center=Vec3_Add(sdf.mouths[0].center,features[feature]);
                center=Vec3_Add(center,Vec3_Create(((cell*7)%11-5.0f)*.025f,((cell*13)%17-8.0f)*.018f,((cell*19)%23-11.0f)*.013f));
                Vector3 half=Vec3_Create(.007f,.009f,.011f);AABB3D box={Vec3_Sub(center,half),Vec3_Add(center,half)};
                float minimum,maximum;
                TEST_ASSERT(field.getCellRange&&field.getCellRange(field.context,box,&minimum,&maximum),"Intervalo conservador de rasgos faciales");
                for(unsigned corner=0;corner<9;++corner) {
                    Vector3 point=corner==8?center:Vec3_Create(corner&1?box.end.x:box.start.x,corner&2?box.end.y:box.start.y,corner&4?box.end.z:box.start.z);
                    float d=MonsterSDF_EvaluateDistance(&sdf,point);
                    TEST_ASSERT(d>=minimum&&d<=maximum,"La poda nunca descarta narinas curvas ni bordes orbitales");
                }
            }
            const AnatomyNode* l=AnatomyGraph_FindModuleNode(&monster->anatomyGraph,30,3);
            const AnatomyNode* r=AnatomyGraph_FindModuleNode(&monster->anatomyGraph,31,3);
            TEST_ASSERT(l&&r&&FLOAT_NEAR(l->center.x,-r->center.x)&&FLOAT_NEAR(l->center.y,r->center.y)&&FLOAT_NEAR(l->center.z,r->center.z),"Cuernos simétricos en sus marcos locales reales");
            const MonsterSDFMouth* mouth=&sdf.mouths[0];MonsterSDFJawField context;SDFField jaw=MonsterSDF_GetJawField(&sdf,0,&context);
            Vector3 contact=h->landmarks.oralContactPoint;
            float lower=jaw.evaluate(jaw.context,contact).distance;
            float upper=MonsterSDF_EvaluateDistance(&sdf,Vec3_Add(mouth->center,contact));
            TEST_ASSERT(lower>=0&&upper>=0&&lower<monster->mouths[0].slitThickness*2,"El contacto funcional está en la abertura, no dentro del hueso mandibular");
            float z=Math_Lerp(mouth->entranceCenterLocal.z-mouth->entranceHalfExtents.z,mouth->faceTipLocal.z,.5f);
            float lip=mouth->entranceCenterLocal.y-mouth->entranceHalfExtents.y+mouth->oralSlope*(z-mouth->entranceCenterLocal.z);
            Vector3 sample=Vec3_Create(mouth->faceMidRadii.x*.65f,lip-mouth->jawRadii.y*.06f,z);
            TEST_ASSERT(jaw.evaluate(jaw.context,sample).distance<0,"El labio inferior llega al plano oral también por los laterales");
        }
        MonsterSDF_Free(&sdf);Monster_Free(monster);
    }
    free(monster);
    for(uint32_t seed=1;seed<=180;++seed) {
        HeadPhenotype p=HeadPhenotype_RandomValid(HEAD_ARCHETYPE_BOVID,seed);HeadAnatomy h;
        TEST_ASSERT(HeadAnatomy_Resolve(&p,0,Vec3_Create(.5f,.45f,.5f),&h),"Variaciones del arquetipo bóvido válidas");
    }
}

/** @brief Máscaras anatómicas continuas y persistencia en bancos de metamorfosis. */
static void BovineRegionalPigment(void) {
    SurfacePhenotype surface=SurfacePhenotype_Default();
    surface.pigment.layerCount=1;
    surface.pigment.layers[0]=(PigmentLayer){.pattern=PIGMENT_PATTERN_SOLID,.color={220,210,190,255},.strength=1,.scale=1,.regionMask=1u<<SURFACE_REGION_HEAD};
    const PigmentLayer* layer=&surface.pigment.layers[0];
    TEST_ASSERT(FLOAT_NEAR(PigmentPattern_RegionWeight(layer,SURFACE_REGION_HEAD,SURFACE_REGION_NECK,.25f),.75f),"Borde cromático continuo entre cara y cuello");
    Color brown={90,40,20,255};
    TEST_ASSERT(Color_Equals(PigmentPattern_EvaluateRegionalLayer(layer,Vec3_Zero(),brown,SURFACE_REGION_NECK,SURFACE_REGION_NECK,0),brown),"La cara clara no decolora el cuello");
    SurfaceRecipe packed=SurfaceRecipe_Compile(&surface);
    TEST_ASSERT(packed.data[36][3]==2,"Máscara regional empaquetada sin nuevas filas GPU");
    SurfacePhenotype other=surface;other.pigment.layers[0].regionMask=1u<<SURFACE_REGION_NECK;
    SurfacePhenotype middle=SurfacePhenotype_Interpolate(&surface,&other,.5f);packed=SurfaceRecipe_Compile(&middle);
    TEST_ASSERT(packed.data[55][3]==2&&packed.data[72][3]==4,"Los bancos origen y destino mantienen sus máscaras independientes");
}

/** @brief La malla cercana real se publica completa, sin islotes ni proxies. */
static void BovineNearTemplate(void) {
    EcosystemConfig config=EcosystemConfig_Default();
    config.herbivores=1;config.carnivores=0;config.plants=0;config.capacity=4;
    EcosystemWorld world;TEST_ASSERT(EcosystemScenario_Create(&world,&config),"Escenario bovino de captura");
    EcosystemVisual visual={0};TEST_ASSERT(EcosystemVisual_Init(&visual,&world),"Caché procedural bovina");
    AIObjectId id=0;for(unsigned i=0;i<world.capacity;++i)if(world.entities[i].id){id=world.entities[i].id;break;}
    TEST_ASSERT(id&&EcosystemVisual_RequestTier(&visual,id,VISUAL_TIER_NEAR),"Solicitar plantilla cercana");
    EcosystemVisual_PrewarmVisible(&visual);EcosystemVisualDrawPacket packet={0};
    struct timespec start,now,pause={0,1000000};clock_gettime(CLOCK_MONOTONIC,&start);
    do {
        EcosystemVisual_Update(&visual,.016f);EcosystemVisual_GetDrawPackets(&visual,&packet,1);
        if(packet.ready&&packet.tier==VISUAL_TIER_NEAR)break;
        nanosleep(&pause,NULL);clock_gettime(CLOCK_MONOTONIC,&now);
    }while(now.tv_sec-start.tv_sec<30);
    TEST_ASSERT(packet.ready&&packet.tier==VISUAL_TIER_NEAR&&packet.bodyMesh,"La validación usa geometría NEAR residente");
    size_t count=0,largest=0,second=0;
    TEST_ASSERT(Mesh_ComponentStatistics(packet.bodyMesh,&count,&largest,&second)&&count==1,"Cuerpo, cuernos, cascos y cola forman una sola componente sin borrar tejido");
    EcosystemVisual_Shutdown(&visual);EcosystemWorld_Free(&world);
}

void run_bovine_editor_tests(void){
    BovineCanonical();BovineAnatomy();BovineRegionalPigment();BovineNearTemplate();
    CreatureRecipeDocument* doc=malloc(sizeof(*doc));Monster* monster=malloc(sizeof(*monster));
    TEST_ASSERT(doc&&monster,"Reservar toro externo");char error[256];
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,"recipes/brumacerno.creature.json",error,sizeof(error)),"Cargar el toro guardado desde el editor");
    const CreatureRecipe* fallback=CreatureRecipes_Find(3);
    TEST_ASSERT(CreatureRecipeRegistry_LoadFile("recipes/brumacerno.creature.json",error,sizeof(error)),"Registrar receta externa");
    TEST_ASSERT(CreatureRecipes_Find(3)!=fallback&&CreatureRecipes_Find(3)->adult.limbs[0].archetype==LIMB_ARCHETYPE_UNGULATE,"El catálogo sustituye datos incorporados antes de crear actores");
    TEST_ASSERT(!CreatureRecipeRegistry_LoadFile("recipes/brumacerno.creature.json",error,sizeof(error)),"No publicar dos recetas con el mismo ID");
    for(int stage=0;stage<4;++stage){const CreaturePhenotype* p=CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage);
        *monster=Monster_Create();TEST_ASSERT(Creature_BuildMonster(monster,&doc->recipe,p),"Las cuatro etapas construyen anatomía y rig");
        MonsterSDF sdf=MonsterSDF_Create();TEST_ASSERT(MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig()),"Compilar materiales semánticos");
        for(size_t i=0;i<sdf.connectorCount;++i){const MonsterSDFConnector* c=&sdf.connectors[i];
            if(c->material==SDF_MATERIAL_HORN||c->material==SDF_MATERIAL_HOOF){
                SurfaceCoordinate coord=SurfaceMapper_MapPoint(c->a,Vec3_Create(0,1,0),c->material,&monster->anatomyGraph,&monster->surfaceMapping);
                TEST_ASSERT(coord.integumentMask==0,"La queratina no admite pelo");
            }
            if(c->isDistalLimb&&((c->toId&65535u)==LIMB_NODE_AUTOPOD))
                TEST_ASSERT((c->widthA+c->widthB)*.5f+c->widthBulge>p->limbs[0].thickness*p->axial.totalScale*.12f,"El metapodio no colapsa antes del casco");
        }
        MonsterSDF_Free(&sdf);Monster_Free(monster);
    }
    *monster=Monster_Create();TEST_ASSERT(Creature_BuildMonster(monster,&doc->recipe,&doc->recipe.adult),"Toro adulto");
    for(size_t i=0;i<monster->mouthCount;++i)Mouth_SetOpenFactor(&monster->mouths[i],0);
    SDFMesherConfig config=SDFMesher_DefaultConfig();config.voxelSize=.12f;config.maxCells=160000;
    MonsterVisual* visual=malloc(sizeof(*visual));TEST_ASSERT(visual,"Reservar vista");*visual=MonsterVisual_Create(config);
    TEST_ASSERT(MonsterVisual_RebuildNow(visual,monster,MonsterSDF_DefaultConfig()),"Mallar toro real");
    size_t horns=0,hooves=0;
    for(size_t i=0;i<visual->mesh.vertexCount;++i){SDFMaterial material=visual->mesh.vertices[i].material;horns+=material==SDF_MATERIAL_HORN;hooves+=material==SDF_MATERIAL_HOOF;}
    TEST_ASSERT(horns>20&&hooves>100,"Cuernos y pezuñas conservan material en la malla");
    for(unsigned leg=0;leg<4;++leg)for(unsigned digit=0;digit<2;++digit){
        const AnatomyNode* node=AnatomyGraph_FindModuleNode(&monster->anatomyGraph,10+leg,Limb_DigitLocalId(digit,2));
        TEST_ASSERT(node,"Estación del casco");float distance=INFINITY;
        for(size_t v=0;v<visual->mesh.vertexCount;++v)distance=fminf(distance,Vec3_Distance(node->center,visual->mesh.vertices[v].position));
        TEST_ASSERT(distance<node->widthRadius*1.6f,"Ninguna de las ocho mitades de pezuña desaparece del mallado");
    }
    FurRootSet roots={0};TEST_ASSERT(FurRootSet_Build(&roots,&visual->mesh,123,12,1500),"Generar raíces reales de pelo");
    TEST_ASSERT(roots.count>0,"La piel conserva su pelo");
    for(size_t i=0;i<roots.count;++i)for(unsigned k=0;k<3;++k){size_t vertex=visual->mesh.indices[roots.roots[i].triangleIndex*3+k];
        TEST_ASSERT(visual->mesh.vertices[vertex].material==SDF_MATERIAL_SKIN,"Ninguna fibra nace sobre cuerno o casco");}
    CreatureVisualProfile profile={.primary={100,80,60,255},.pigmentSeed=23};SurfacePhenotype surface;
    TEST_ASSERT(VisualVariation_Surface(&doc->recipe,&doc->recipe.adult,&profile,1,&surface),"Apariencia ecológica desde receta externa");
    TEST_ASSERT(Color_Equals(surface.pigment.baseColor,doc->recipe.adult.surface.pigment.baseColor)&&FLOAT_NEAR(surface.integument.fur.length,doc->recipe.adult.surface.integument.fur.length),"La demo no reemplaza pintura y pelaje por un preset");
    FurRootSet_Free(&roots);MonsterVisual_Free(visual);free(visual);Monster_Free(monster);free(monster);free(doc);
    CreatureRecipeRegistry_Clear();TEST_ASSERT(CreatureRecipes_Find(3)==fallback,"Restaurar catálogo tras cerrar consumidores");
}
