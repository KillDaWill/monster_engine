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
#include "test_utils.h"
#include <stdlib.h>
#include <math.h>

void run_bovine_editor_tests(void){
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
