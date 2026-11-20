#define _POSIX_C_SOURCE 200809L
#include "CreatureOffspring.h"
#include "Monster.h"
#include "CreatureMorph.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
#include "../src/OffspringMorphFitter.h"
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
        {horse,elephant}, {lizard,rat}, {rat,rhino}, {rat,elephant}, {CreatureRecipes_Brumacerno(),CreatureRecipes_Velkar()}
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

static void HybridMorphology(void)
{
    CreatureRecipeDocument *documents=calloc(3,sizeof(*documents));
    Monster *monsters=calloc(5,sizeof(*monsters));
    TEST_ASSERT(documents && monsters,"memoria");
    const CreatureRecipe *a=Load(&documents[0],"rata"), *b=Load(&documents[1],"elefante");
    CreatureOffspringBatch *batch=Batch(a,b,5,48216);
    float chestA=a->adult.axial.thoraxHeight/a->adult.axial.withersHeight;
    float chestB=b->adult.axial.thoraxHeight/b->adult.axial.withersHeight;
    for (size_t i=0; i<CreatureOffspring_Count(batch); ++i) {
        const AxialPhenotype *axial=&CreatureOffspring_Get(batch,i,NULL)->adult.axial;
        float chest=axial->thoraxHeight/axial->withersHeight;
        TEST_ASSERT(chest>=fminf(chestA,chestB)-.001f && chest<=fmaxf(chestA,chestB)+.001f,
            "el cambio de plan mantiene la altura torácica relativa entre ambos padres");
    }
    const OffspringReport *report;
    const CreatureRecipe *child=CreatureOffspring_Get(batch,2,&report);
    TEST_ASSERT(report->hasMorphTarget && report->fitEvaluations && report->headError<.025f &&
        report->bodyError<.12f,"ajuste medible al morph de referencia");
    for (unsigned i=0; i<5; ++i) monsters[i]=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&monsters[0],a,&a->adult) &&
        Creature_BuildMonster(&monsters[1],b,&b->adult) &&
        CreatureMorph_Interpolate(&monsters[0],&monsters[1],report->global,&monsters[2]),
        "referencia independiente");
    TEST_ASSERT(Creature_BuildMonster(&monsters[3],child,&child->adult),"cabeza híbrida construida");
    float visualScale=monsters[3].phenotype.axial.totalScale;
    float morphScale=monsters[2].phenotype.axial.totalScale;
    float ordinaryA=OffspringMorph_HeadError(&monsters[0].head.anatomy,monsters[0].phenotype.axial.totalScale,&monsters[2].head.anatomy,morphScale);
    float ordinaryB=OffspringMorph_HeadError(&monsters[1].head.anatomy,monsters[1].phenotype.axial.totalScale,&monsters[2].head.anatomy,morphScale);
    float hybrid=OffspringMorph_HeadError(&monsters[3].head.anatomy,visualScale,&monsters[2].head.anatomy,morphScale);
    TEST_ASSERT(hybrid<ordinaryA && hybrid<ordinaryB,"forma más cercana al morph que ambas bases");
    const HeadHybridProfile *profile=&child->adult.headHybrid;
    TEST_ASSERT(profile->active && profile->hasRegionalProfile &&
        HeadAnatomy_ValidateProfile(&profile->regional),"perfil regional serializable");
    TEST_ASSERT(child->offspringVersion==CREATURE_OFFSPRING_VERSION && child->offspringSeed==report->seed,
        "semilla y versión persistentes");
    CreaturePhenotype age=CreatureRecipe_EvaluateDevelopment(child,.52f);
    TEST_ASSERT(age.headHybrid.active && age.surface.inheritedBlend && age.surface.transition.active &&
        FLOAT_NEAR(age.surface.transition.weight,child->adult.surface.transition.weight),
        "perfil y dos bancos persistentes a edad intermedia");
    char *json=JSON(child);
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(&documents[2],json,NULL,0),"carga de perfil editable");
    free(json);
    const CreatureRecipe *saved=&documents[2].recipe;
    TEST_ASSERT(Creature_BuildMonster(&monsters[4],saved,&saved->adult),"reconstrucción desde archivo");
    TEST_ASSERT(OffspringMorph_HeadError(&monsters[4].head.anatomy,visualScale,
        &monsters[3].head.anatomy,visualScale)<1e-6f,"fidelidad cefálica");
    MonsterSDF original=MonsterSDF_Create(), reloaded=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&original,&monsters[3],MonsterSDF_DefaultConfig()) &&
        MonsterSDF_Build(&reloaded,&monsters[4],MonsterSDF_DefaultConfig()),"SDF original y recargado");
    Vector3 extent=AABB_Size(original.bounds);
    for (unsigned i=0; i<256; ++i) {
        Vector3 point=Vec3_Add(original.bounds.start,Vec3_Create(
            extent.x*(i*37%251)/250,extent.y*(i*51%251)/250,extent.z*(i*67%251)/250));
        float x=MonsterSDF_EvaluateDistance(&original,point);
        float y=MonsterSDF_EvaluateDistance(&reloaded,point);
        TEST_ASSERT(isfinite(x) && fabsf(x-y)<1e-6f,"fidelidad SDF al volver a cargar");
    }
    SurfaceRecipe pa=SurfaceRecipe_Compile(&monsters[3].surface), pb=SurfaceRecipe_Compile(&monsters[4].surface);
    TEST_ASSERT(!memcmp(&pa,&pb,sizeof(pa)),"pigmento y materiales reconstruidos");
    const SurfacePhenotype *surface=&child->adult.surface;
    TEST_ASSERT(surface->transition.from.seed==a->adult.surface.pigment.seed &&
        surface->transition.to.seed==b->adult.surface.pigment.seed,"semillas parentales de pigmento");
    TEST_ASSERT(monsters[4].mouths[0].lipColor.r==profile->regional.oralSystem.lipColor.r &&
        monsters[4].mouths[0].insideColor.r==profile->regional.oralSystem.insideColor.r,
        "colores faciales aplicados por el resolutor normal");
    SurfaceCoordinate c={.position={.3f,.4f,1},.region=SURFACE_REGION_HEAD,
        .secondaryRegion=SURFACE_REGION_TAIL};
    Vector3 primary=Surface_ResolveBaseAlbedo(surface,&c,COLOR_WHITE);
    c.blend=1;
    Vector3 distal=Surface_ResolveBaseAlbedo(surface,&c,COLOR_WHITE);
    c.blend=.5f;
    TEST_ASSERT(Vec3_Distance(primary,distal)>.003f &&
        Vec3_Distance(Surface_ResolveBaseAlbedo(surface,&c,COLOR_WHITE),Vec3_Lerp(primary,distal,.5f))<1e-6f,
        "pigmento regional independiente y continuo");
    Monster_SetHeadOpenFactor(&monsters[4],.55f);
    TEST_ASSERT(MonsterSDF_Build(&reloaded,&monsters[4],MonsterSDF_DefaultConfig()) &&
        fabsf(reloaded.mouths[0].visualJawAngle)>.02f,"articulación mandibular");
    for (unsigned i=0; i<24; ++i)
        TEST_ASSERT(MonsterAnimation_Update(&monsters[4],1/60.f),"animación normal");
    MonsterSDF_Free(&original);
    MonsterSDF_Free(&reloaded);
    for (unsigned i=0; i<5; ++i) Monster_Free(&monsters[i]);
    CreatureOffspring_Destroy(batch);
    free(monsters);
    free(documents);
}

static void RegionalInheritance(void)
{
    CreatureRecipeDocument *docs=calloc(3,sizeof(*docs));
    Monster *monster=calloc(1,sizeof(*monster));
    TEST_ASSERT(docs && monster,"memoria");
    const CreatureRecipe *rat=Load(&docs[0],"rata"), *elephant=Load(&docs[1],"elefante");
    CreatureOffspringConfig config=CreatureOffspring_Default(8,48216);
    config.minimumContribution=config.maximumContribution=.5f;
    char error[256];
    CreatureOffspringBatch *batch=CreatureOffspring_Generate(rat,elephant,config,NULL,NULL,error,sizeof(error));
    TEST_ASSERT(batch,error);
    const CreatureRecipe *first=NULL, *second=NULL;
    float separation=0;
    for (size_t i=0; i<config.count; ++i) {
        const OffspringReport *a;
        const CreatureRecipe *x=CreatureOffspring_Get(batch,i,&a);
        TEST_ASSERT(a->global==.5f && a->proposalsEvaluated<=config.attempts &&
            a->proposalsValid<=config.proposalsPerOffspring && a->shapeDiversity>0,
            "misma contribución, selección y presupuesto");
        TEST_ASSERT(a->headGenes.muzzleLengthGain>=.83f && a->headGenes.muzzleLengthGain<=1.17f &&
            a->headGenes.earHeightGain>=.81f && a->headGenes.earHeightGain<=1.19f,
            "mutaciones proporcionadas");
        const HeadAnatomy *hx=&x->adult.headHybrid.regional;
        for (size_t j=0; j<i; ++j) {
            const CreatureRecipe *y=CreatureOffspring_Get(batch,j,NULL);
            const HeadAnatomy *hy=&y->adult.headHybrid.regional;
            if (hx->archetype!=hy->archetype) continue;
            float cranial=fabsf(hx->surface.craniumRadii.x-hy->surface.craniumRadii.x);
            float xMuzzle=(hx->surface.faceTip.z-hx->surface.faceRoot.z)/hx->surface.craniumRadii.z;
            float yMuzzle=(hy->surface.faceTip.z-hy->surface.faceRoot.z)/hy->surface.craniumRadii.z;
            float ear=fabsf(hx->surface.earShape.y/hx->surface.craniumRadii.y-
                hy->surface.earShape.y/hy->surface.craniumRadii.y);
            if (cranial>.04f && fabsf(xMuzzle-yMuzzle)>.04f && ear>.08f && cranial+ear>separation) {
                first=x; second=y; separation=cranial+ear;
            }
        }
    }
    TEST_ASSERT(first && second,"variación craneal, facial y auricular con idéntica familia y peso global");
    const HeadHybridProfile *profile=&first->adult.headHybrid;
    HeadPhenotype edit=first->adult.head;
    HeadAnatomy original, edited;
    Vector3 envelope={1,1,1};
    TEST_ASSERT(HeadAnatomy_ResolveHybrid(&edit,profile,0,envelope,&original),"perfil editable");
    edit.muzzleLength=edit.muzzleLength>.5f ? 0 : 1;
    edit.earSize=edit.earSize>.5f ? 0 : 1;
    TEST_ASSERT(HeadAnatomy_ResolveHybrid(&edit,profile,0,envelope,&edited),"resolución después de editar");
    TEST_ASSERT(!FLOAT_NEAR(original.surface.faceTip.z,edited.surface.faceTip.z) &&
        !FLOAT_NEAR(original.surface.earShape.y,edited.surface.earShape.y) &&
        Vec3_Distance(original.landmarks.leftEarBase,edited.landmarks.leftEarBase)<1e-6f &&
        Vec3_Distance(original.oralSystem.jawPivot,edited.oralSystem.jawPivot)<1e-6f,
        "editar hocico y tamaño de pinna conserva raíz y bisagra");
    char *json=JSON(first);
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(&docs[2],json,error,sizeof(error)),error);
    free(json);
    for (unsigned i=0; i<4; ++i) {
        const CreaturePhenotype *live=CreatureRecipe_GetStage(first,i);
        const CreaturePhenotype *saved=CreatureRecipe_GetStage(&docs[2].recipe,i);
        const HeadAnatomy *a=&live->headHybrid.regional,*b=&saved->headHybrid.regional;
        TEST_ASSERT(Vec3_Distance(a->surface.earShape,b->surface.earShape)<1e-6f &&
            Vec3_Distance(a->surface.earDirection,b->surface.earDirection)<1e-6f &&
            Vec3_Distance(a->landmarks.leftEarBase,b->landmarks.leftEarBase)<1e-6f &&
            FLOAT_NEAR(a->surface.earLongitudinalCurve,b->surface.earLongitudinalCurve),
            "pinna recargada conserva sección, marco, raíz y curvatura");
        Vector3 left=b->landmarks.leftEarBase;
        left.x=-left.x;
        TEST_ASSERT(Vec3_Distance(left,b->landmarks.rightEarBase)<1e-6f,"implantación bilateral coherente");
    }
    config=CreatureOffspring_Default(1,7181);
    config.minimumContribution=config.maximumContribution=0;
    config.regionalVariance=config.mutationIntensity=0;
    CreatureOffspringBatch *descendants=CreatureOffspring_Generate(&docs[2].recipe,second,
        config,NULL,NULL,error,sizeof(error));
    TEST_ASSERT(descendants,error);
    const OffspringReport *report;
    const CreatureRecipe *grandchild=CreatureOffspring_Get(descendants,0,&report);
    TEST_ASSERT(!report->simplifiedHeadBasis && grandchild->adult.headHybrid.hasRegionalProfile,
        "herencia de la geometría híbrida sin seleccionar una base ancestral");
    const HeadAnatomy *inherited=&grandchild->adult.headHybrid.regional;
    TEST_ASSERT(Vec3_Distance(inherited->surface.craniumRadii,original.surface.craniumRadii)<.002f &&
        fabsf(inherited->surface.earShape.y-original.surface.earShape.y)<.002f,
        "segundo cruce conserva medidas híbridas al heredar íntegramente la cabeza");
    TEST_ASSERT(Creature_BuildMonster(monster,grandchild,&grandchild->adult) &&
        !Creature_ValidateGeometry(monster),"segunda generación funcional");
    Monster_Free(monster);
    free(monster);
    CreatureOffspring_Destroy(descendants);
    CreatureOffspring_Destroy(batch);
    free(docs);
}

static void LegacyProfile(void)
{
    CreatureRecipeDocument *source=calloc(1,sizeof(*source)), *roundtrip=calloc(1,sizeof(*roundtrip));
    TEST_ASSERT(source && roundtrip,"memoria");
    const CreatureRecipe *rat=Load(source,"rata");
    for (unsigned i=0; i<4; ++i) {
        CreaturePhenotype *p=(CreaturePhenotype*)CreatureRecipe_GetStage(rat,i);
        p->headHybrid=(HeadHybridProfile){.active=true,.blend=.5f,.primaryRadii={.5f,.5f,.5f},
            .secondaryRadii={.6f,.6f,.6f},.secondary=p->head};
    }
    char *json=JSON(rat);
    TEST_ASSERT(!strstr(json,"hasRegionalProfile") && CreatureRecipeIO_ReadJSON(roundtrip,json,NULL,0),
        "receta JSON v1 con perfil V2");
    TEST_ASSERT(roundtrip->recipe.adult.headHybrid.active &&
        !roundtrip->recipe.adult.headHybrid.hasRegionalProfile,"representación V2 conservada");
    free(json);
    free(roundtrip);
    free(source);
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
    for (size_t stage=0; stage<4; ++stage) {
        SurfacePhenotype *surface=&stages[stage]->surface;
        for (size_t i=0; i<surface->longHairCount; ++i) {
            LongHairDomain *d=&surface->longHair[i].domain;
            if (d->hostModuleId) d->hostModuleId+=101;
            for (size_t j=0; j<d->anchorCount; ++j)
                if (d->anchors[j]) d->anchors[j]+=101u<<16;
        }
        for (size_t i=0; i<surface->pigment.layerCount; ++i)
            if (surface->pigment.layers[i].domain.anchorNode)
                surface->pigment.layers[i].domain.anchorNode+=101u<<16;
    }
    CreatureOffspringBatch *original = Batch(rat,rat,5,48216);
    CreatureOffspringBatch *renumbered = Batch(rat,reordered,5,48216);
    for (size_t i=0; i<5; ++i) {
        const OffspringReport *a, *b;
        CreatureOffspring_Get(original,i,&a);
        CreatureOffspring_Get(renumbered,i,&b);
        TEST_ASSERT(a->seed==b->seed && FLOAT_NEAR(a->signature.forelimbBodyRatio,b->signature.forelimbBodyRatio) &&
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
    HybridMorphology();
    RegionalInheritance();
    LegacyProfile();
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
