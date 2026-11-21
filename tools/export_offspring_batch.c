/** Exporta recetas y diagnósticos reproducibles para comparaciones de lotes. */
#define _POSIX_C_SOURCE 200809L
#include "CreatureOffspring.h"
#include "CreatureRecipes.h"
#ifndef OFFSPRING_V2_BASELINE
#include "../src/CreatureOffspringInternal.h"
#include "../src/OffspringRepair.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const CreatureRecipe *Load(CreatureRecipeDocument *doc, const char *source)
{
    if (source[0] == '@') return CreatureRecipes_Find((CreatureRecipeId)strtoul(source+1,NULL,10));
    return CreatureRecipeIO_LoadFile(doc,source,NULL,0) ? &doc->recipe : NULL;
}

static double Seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC,&now);
    return now.tv_sec + now.tv_nsec*1e-9;
}

#ifndef OFFSPRING_V2_BASELINE
static bool Save(const CreatureRecipe *recipe, const char *path)
{
    FILE *output = fopen(path,"w");
    if (!output) return false;
    bool ok = CreatureRecipeIO_WriteJSON(output,recipe);
    return fclose(output)==0 && ok;
}
#endif

int main(int argc, char **argv)
{
#ifndef OFFSPRING_V2_BASELINE
    if (argc == 4 && !strcmp(argv[1],"--adapt-saved")) {
        CreatureRecipeDocument *doc = calloc(1,sizeof(*doc));
        bool ok = doc && CreatureRecipeIO_LoadFile(doc,argv[2],NULL,0);
        if (ok) {
            OffspringRepair_Integrate(&doc->recipe);
            ok = Save(&doc->recipe,argv[3]);
        }
        free(doc);
        return !ok;
    }
    if (argc == 9 && !strcmp(argv[1],"--replay")) {
        CreatureRecipeDocument *docs = calloc(3,sizeof(*docs));
        if (!docs) return 1;
        const CreatureRecipe *a = Load(&docs[0],argv[2]), *b = Load(&docs[1],argv[3]);
        CreatureOffspringConfig config = CreatureOffspring_Default(strtoul(argv[4],NULL,10),strtoul(argv[5],NULL,10));
        char error[256] = "Cannot replay proposal";
        bool ok = CreatureOffspring_ReplayProposal(a,b,config,strtoul(argv[6],NULL,10)-1,
            strtoul(argv[7],NULL,10)-1,&docs[2],error,sizeof(error)) && Save(&docs[2].recipe,argv[8]);
        if (!ok) fprintf(stderr,"%s\n",error);
        free(docs);
        return !ok;
    }
#endif
    if (argc != 7 && (argc != 8 || strcmp(argv[7],"--fixed-global"))) {
        fprintf(stderr,"Uso: %s padreX padreY cantidad semilla repeticiones directorio [--fixed-global]\n",argv[0]);
        return 2;
    }
    CreatureRecipeDocument *parents = calloc(2,sizeof(*parents));
    if (!parents) return 1;
    const CreatureRecipe *a = Load(&parents[0],argv[1]), *b = Load(&parents[1],argv[2]);
    CreatureOffspringConfig config = CreatureOffspring_Default(strtoul(argv[3],NULL,10),
        (uint32_t)strtoul(argv[4],NULL,10));
    if (argc==8) config.minimumContribution=config.maximumContribution=.5f;
    unsigned repetitions = strtoul(argv[5],NULL,10);
    CreatureOffspringBatch *batch = NULL;
    char error[256];
    bool ok = false;
    for (unsigned i=0; i<repetitions; ++i) {
        CreatureOffspring_Destroy(batch);
        double start = Seconds();
        batch = CreatureOffspring_Generate(a,b,config,NULL,NULL,error,sizeof(error));
        if (!batch) { fprintf(stderr,"%s\n",error); goto done; }
        printf("seconds\t%.6f\n",Seconds()-start);
    }
    if (!batch) goto done;
    for (size_t i=0; i<CreatureOffspring_Count(batch); ++i) {
        const OffspringReport *r;
        const CreatureRecipe *recipe = CreatureOffspring_Get(batch,i,&r);
        char path[4096];
        snprintf(path,sizeof(path),"%s/%zu.creature.json",argv[6],i+1);
        FILE *out = fopen(path,"w");
        if (!out) goto done;
        bool written = CreatureRecipeIO_WriteJSON(out,recipe);
        if (fclose(out) || !written) goto done;
        printf("offspring\t%zu\t%u\t%.6f\t%.6f\t%u\t%u\t%u\n",
            i+1,r->seed,r->global,r->head,r->attempts,r->repairs,r->fitEvaluations);
#ifndef OFFSPRING_V2_BASELINE
        printf("selection\t%zu\t%u\t%u\t%.6f\t%.6f\t%.6f\t%u\t%u\n",
            i+1,r->proposalsEvaluated,r->proposalsValid,r->shapeDiversity,
            r->appearanceDiversity,r->quality,r->scaffoldParent,r->grafts);
        printf("anatomy\t%zu\t%.6f\n",i+1,r->anatomicalCoherence);
        const HeadRegionalInheritance *h=&r->headGenes;
        printf("head_genes\t%zu\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
            i+1,recipe->adult.head.archetype,h->cranium,h->rostrum,h->jaw,h->orbit,h->ear,
            h->earHeight,h->earWidth,h->earCurve);
#endif
    }
    ok = true;
done:
    CreatureOffspring_Destroy(batch);
    free(parents);
    return ok ? 0 : 1;
}
