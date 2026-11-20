#define _POSIX_C_SOURCE 200809L
#include "CreatureOffspring.h"
#include "Monster.h"
#include "test_utils.h"
#include <string.h>

static char *JSON(const CreatureRecipe *recipe)
{
    char *json = NULL;
    size_t length = 0;
    FILE *file = open_memstream(&json,&length);
    TEST_ASSERT(file && CreatureRecipeIO_WriteJSON(file,recipe),"serialización JSON");
    TEST_ASSERT(fclose(file)==0 && length>0,"cierre de serialización JSON");
    return json;
}

static void CheckRecipe(const CreatureRecipe *recipe, const OffspringReport *report)
{
    CreatureRecipeDocument *doc = malloc(sizeof(*doc));
    Monster *monster = malloc(sizeof(*monster));
    TEST_ASSERT(doc && monster,"memoria");
    char *json = JSON(recipe), error[256];
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(doc,json,error,sizeof(error)),error);
    TEST_ASSERT(doc->recipe.name == doc->name && !strcmp(doc->name,recipe->name),"nombre propietario");
    char *roundtrip = JSON(&doc->recipe);
    TEST_ASSERT(!strcmp(json,roundtrip),"JSON estable tras la carga");
    free(roundtrip);
    free(json);
    *monster = Monster_Create();
    float lastSize = 0;
    for (int i=0; i<=6; ++i) {
        CreaturePhenotype p = CreatureRecipe_EvaluateDevelopment(&doc->recipe,(float)i/6);
        TEST_ASSERT(p.axial.totalScale >= lastSize,"progresión de escala");
        TEST_ASSERT(CreatureRecipe_Validate(&doc->recipe,&p),"plan corporal");
        TEST_ASSERT(Creature_BuildMonster(monster,&doc->recipe,&p),"anatomía y rig reconstruidos");
        TEST_ASSERT(!Creature_ValidateGeometry(monster),"geometría en desarrollo");
        if (p.axial.archetype == AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD && p.limbCount &&
            p.development.appendages >= .65f) {
            for (size_t j=0; j<monster->anatomyGraph.nodeCount; ++j) {
                const AnatomyNode *node = &monster->anatomyGraph.nodes[j];
                if (node->region == ANATOMY_REGION_FLEXIBLE)
                    TEST_ASSERT(node->center.y-node->heightRadius >= -.02f*p.axial.totalScale,
                        "apéndice flexible por encima del suelo");
            }
        }
        lastSize = p.axial.totalScale;
    }
    TEST_ASSERT(isfinite(report->signature.bodyLengthWidthRatio),"firma geométrica");
    Monster_Free(monster);
    free(monster);
    free(doc);
}

static const CreatureRecipe *Load(CreatureRecipeDocument *doc, const char *filename)
{
    char path[200], error[256];
    snprintf(path,sizeof(path),"recipes/%s.creature.json",filename);
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,path,error,sizeof(error)),error);
    return &doc->recipe;
}

static CreatureOffspringBatch *Batch(const CreatureRecipe *a, const CreatureRecipe *b,
    size_t count, uint32_t seed)
{
    char error[256];
    CreatureOffspringBatch *batch = CreatureOffspring_Generate(a,b,
        CreatureOffspring_Default(count,seed),NULL,NULL,error,sizeof(error));
    if (!batch) fprintf(stderr,"%s × %s: %s\n",a->name,b->name,error);
    TEST_ASSERT(batch,error);
    TEST_ASSERT(CreatureOffspring_Count(batch)==count && CreatureOffspring_Seed(batch)==seed,"datos del lote");
    return batch;
}

static void Matrix(void)
{
    CreatureRecipeDocument *docs = malloc(6*sizeof(*docs));
    TEST_ASSERT(docs,"memoria");
    const CreatureRecipe *horse = Load(&docs[0],"caballo"), *donkey = Load(&docs[1],"asno");
    const CreatureRecipe *rhino = Load(&docs[2],"rinoceronte"), *rat = Load(&docs[3],"rata");
    const CreatureRecipe *pig = Load(&docs[4],"cerdo"), *elephant = Load(&docs[5],"elefante");
    const CreatureRecipe *dog = CreatureRecipes_Dog(), *lizard = CreatureRecipes_Lizard();
    const CreatureRecipe *pairs[][2] = {
        {horse,donkey}, {rhino,rat}, {pig,elephant}, {dog,lizard}, {rat,rat},
        {horse,elephant}, {lizard,rat}, {rat,rhino}, {CreatureRecipes_Brumacerno(),CreatureRecipes_Velkar()}
    };
    for (size_t i=0; i<sizeof(pairs)/sizeof(*pairs); ++i) {
        CreatureOffspringBatch *batch = Batch(pairs[i][0],pairs[i][1],5,48216);
        unsigned scaffolds=0, grafts=0;
        float previous = 0;
        for (size_t j=0; j<5; ++j) {
            const OffspringReport *report;
            const CreatureRecipe *candidate = CreatureOffspring_Get(batch,j,&report);
            TEST_ASSERT(candidate && candidate->id && candidate->id != pairs[i][0]->id &&
                candidate->id != pairs[i][1]->id,"identidad temporal");
            TEST_ASSERT(report->global > previous && report->global>=.35f && report->global<=.65f,"contribución estratificada");
            previous = report->global;
            scaffolds |= 1u << report->scaffoldParent;
            grafts += report->grafts;
            for (size_t k=0; k<j; ++k)
                TEST_ASSERT(candidate->id != CreatureOffspring_Get(batch,k,NULL)->id,"ID único");
            CheckRecipe(candidate,report);
        }
        if (i==1 || i==2) TEST_ASSERT(grafts>0 && scaffolds==3,"injertos y dos alternativas estructurales");
        CreatureOffspring_Destroy(batch);
    }
    free(docs);
}

static void ReorderedParent(void)
{
    CreatureRecipeDocument *doc = malloc(sizeof(*doc));
    CreatureRecipe *reordered = malloc(sizeof(*reordered));
    TEST_ASSERT(doc && reordered,"memoria");
    const CreatureRecipe *rat = Load(doc,"rata");
    *reordered = *rat;
    BodyPlan *plan = &reordered->bodyPlan;
    plan->root.moduleInstanceId += 101;
    CreaturePhenotype *stages[] = {&reordered->seed,&reordered->larva,&reordered->juvenile,&reordered->adult};
    for (size_t i=0; i<plan->moduleCount; ++i) {
        CreatureModuleInstance m = rat->bodyPlan.modules[i];
        if (m.kind == CREATURE_MODULE_LIMB) m.phenotypeIndex = 3-m.phenotypeIndex;
        m.instanceId += 101;
        if (m.attachment.hostModuleInstanceId) m.attachment.hostModuleInstanceId += 101;
        plan->modules[plan->moduleCount-1-i] = m;
    }
    for (size_t j=0; j<4; ++j) {
        for (size_t i=0; i<4; ++i)
            stages[j]->limbs[i] = CreatureRecipe_GetStage(rat,j)->limbs[3-i];
    }
    CreatureOffspringBatch *original = Batch(rat,rat,5,48216);
    CreatureOffspringBatch *renumbered = Batch(rat,reordered,5,48216);
    for (size_t i=0; i<5; ++i) {
        const OffspringReport *a, *b;
        CreatureOffspring_Get(original,i,&a);
        CreatureOffspring_Get(renumbered,i,&b);
        TEST_ASSERT(FLOAT_NEAR(a->signature.forelimbBodyRatio,b->signature.forelimbBodyRatio) &&
            FLOAT_NEAR(a->signature.tailBodyLengthRatio,b->signature.tailBodyLengthRatio),
            "correspondencia anatómica independiente de IDs y orden");
    }
    CreatureOffspring_Destroy(original);
    CreatureOffspring_Destroy(renumbered);
    free(doc);
    free(reordered);
}

static void DivergentPlan(void)
{
    const CreatureRecipe *lizard = CreatureRecipes_Lizard();
    CreatureRecipe *limbless = malloc(sizeof(*limbless));
    TEST_ASSERT(limbless,"memoria");
    *limbless = *lizard;
    limbless->id = 4201;
    limbless->name = "Tetrápodo ápodo de prueba";
    limbless->gait = CREATURE_GAIT_NONE;
    size_t count = 0;
    for (size_t i=0; i<lizard->bodyPlan.moduleCount; ++i) {
        CreatureModuleInstance m = lizard->bodyPlan.modules[i];
        if (m.kind != CREATURE_MODULE_LIMB) limbless->bodyPlan.modules[count++] = m;
    }
    limbless->bodyPlan.moduleCount = count;
    limbless->seed.limbCount = limbless->larva.limbCount = 0;
    limbless->juvenile.limbCount = limbless->adult.limbCount = 0;
    CreatureOffspringBatch *batch = Batch(limbless,lizard,5,48216);
    free(limbless);
    unsigned limbs = 0;
    for (size_t i=0; i<5; ++i) {
        const OffspringReport *report;
        const CreatureRecipe *r = CreatureOffspring_Get(batch,i,&report);
        TEST_ASSERT(r->adult.limbCount==0 || r->adult.limbCount==4,"sistema locomotor completo");
        limbs |= r->adult.limbCount ? 2u : 1u;
        CheckRecipe(r,report);
    }
    TEST_ASSERT(limbs==3,"ambos planes corporales difieren en el número de miembros");
    CreatureOffspring_Destroy(batch);
}

static bool Cancel(void *state)
{
    (void)state;
    return true;
}

static void Determinism(void)
{
    CreatureRecipeDocument *doc = malloc(sizeof(*doc));
    TEST_ASSERT(doc,"memoria");
    const CreatureRecipe *a = Load(doc,"rata"), *b = CreatureRecipes_Lizard();
    char *before = JSON(a);
    CreatureOffspringBatch *first = Batch(a,b,5,48216);
    CreatureOffspringBatch *again = Batch(a,b,5,48216);
    CreatureOffspringBatch *changed = Batch(a,b,5,9258);
    char *after = JSON(a);
    TEST_ASSERT(!strcmp(before,after),"padres inmutables");
    free(before);
    free(after);
    free(doc);
    TEST_ASSERT(!strcmp(CreatureOffspring_Parent(first,0)->name,"Rata"),"padres poseídos");
    for (size_t i=0; i<5; ++i) {
        const CreatureRecipe *generated = CreatureOffspring_Get(first,i,NULL);
        char *x = JSON(generated);
        char *y = JSON(CreatureOffspring_Get(again,i,NULL));
        TEST_ASSERT(!strcmp(x,y),"determinismo con la misma semilla");
        free(x);
        free(y);
    }
    const OffspringReport *aReport, *bReport;
    CreatureOffspring_Get(first,0,&aReport);
    CreatureOffspring_Get(changed,0,&bReport);
    TEST_ASSERT(fabsf(aReport->signature.bodyLengthWidthRatio-bReport->signature.bodyLengthWidthRatio)>.01f,
        "otra semilla cambia proporciones");
    char error[256];
    const CreatureRecipe *candidate = CreatureOffspring_Get(first,2,NULL);
    const CreatureRecipe *neighbor = CreatureOffspring_Get(first,3,NULL);
    TEST_ASSERT(!CreatureOffspring_Reroll(first,2,Cancel,NULL,error,sizeof(error)),"cancelación");
    TEST_ASSERT(CreatureOffspring_Get(first,2,NULL)==candidate,"cancelación transaccional");
    TEST_ASSERT(CreatureOffspring_Reroll(first,2,NULL,NULL,error,sizeof(error)),error);
    TEST_ASSERT(CreatureOffspring_Reroll(again,2,NULL,NULL,error,sizeof(error)),error);
    char *x = JSON(CreatureOffspring_Get(first,2,NULL)), *y = JSON(CreatureOffspring_Get(again,2,NULL));
    TEST_ASSERT(!strcmp(x,y) && CreatureOffspring_Get(first,3,NULL)==neighbor,"reroll local y determinista");
    free(x);
    free(y);
    CreatureOffspring_Destroy(first);
    CreatureOffspring_Destroy(again);
    CreatureOffspring_Destroy(changed);
}

void run_creature_offspring_tests(void)
{
    Matrix();
    Determinism();
    DivergentPlan();
    ReorderedParent();
    char error[256];
    const CreatureRecipe *dog = CreatureRecipes_Dog();
    TEST_ASSERT(!CreatureOffspring_Generate(dog,NULL,CreatureOffspring_Default(1,0),
        NULL,NULL,error,sizeof(error)),"padre ausente");
    TEST_ASSERT(!CreatureOffspring_Generate(dog,dog,CreatureOffspring_Default(17,0),
        NULL,NULL,error,sizeof(error)),"límite de lote");
    CreatureRecipe *invalid = malloc(sizeof(*invalid));
    TEST_ASSERT(invalid,"memoria");
    *invalid = *dog;
    invalid->bodyPlan.modules[1].instanceId = invalid->bodyPlan.modules[0].instanceId;
    TEST_ASSERT(!CreatureOffspring_Generate(invalid,dog,CreatureOffspring_Default(1,0),
        NULL,NULL,error,sizeof(error)),"IDs de módulo duplicados");
    *invalid = *dog;
    invalid->adult.axial.archetype = AXIAL_ARCHETYPE_SERPENTINE;
    TEST_ASSERT(!CreatureOffspring_Generate(invalid,dog,CreatureOffspring_Default(1,0),
        NULL,NULL,error,sizeof(error)),"arquetipo axial sin resolución");
    *invalid = *dog;
    invalid->adult.surface.wrinkleStrength = NAN;
    TEST_ASSERT(!CreatureOffspring_Generate(invalid,dog,CreatureOffspring_Default(1,0),
        NULL,NULL,error,sizeof(error)),"material no finito");
    free(invalid);
    CreatureOffspringBatch *full = Batch(dog,dog,16,0);
    CreatureOffspring_Destroy(full);
    puts("[PASS] CreatureOffspring: anatomía, JSON, diversidad y determinismo");
}
