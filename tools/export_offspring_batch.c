/** Exporta recetas y diagnósticos reproducibles para comparaciones de lotes. */
#define _POSIX_C_SOURCE 200809L
#include "CreatureOffspring.h"
#include "CreatureRecipes.h"
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

int main(int argc, char **argv)
{
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
