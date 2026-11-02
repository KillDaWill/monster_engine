/** @file benchmark_anatomy.c
 * @brief Coste de resolución anatómica y tamaño real de estructuras embebidas. */
#include "Creature.h"
#include "CreatureRecipeIO.h"
#include "Monster.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc,char** argv) {
    AnatomyGraph* graph=malloc(sizeof(*graph));
    if(!graph)return 1;
    printf("sizeof_recipe=%zu sizeof_phenotype=%zu sizeof_graph=%zu sizeof_monster=%zu\n",
        sizeof(CreatureRecipe),sizeof(CreaturePhenotype),sizeof(AnatomyGraph),sizeof(Monster));
    for(unsigned id=1;id<=(argc>1?(unsigned)argc-1:4);++id) {
        CreatureRecipeDocument document;char error[256];
        const CreatureRecipe* recipe;
        if(argc>1) {
            if(!CreatureRecipeIO_LoadFile(&document,argv[id],error,sizeof(error))){fprintf(stderr,"%s\n",error);free(graph);return 1;}
            recipe=&document.recipe;
        } else recipe=CreatureRecipes_Find(id);
        clock_t start=clock();unsigned iterations=1000;
        for(unsigned i=0;i<iterations;++i)
            if(!Creature_ResolveAnatomy(recipe,&recipe->adult,graph)){free(graph);return 1;}
        double us=1e6*(double)(clock()-start)/CLOCKS_PER_SEC/iterations;
        printf("recipe=%u iterations=%u resolve_us=%.3f nodes=%zu connections=%zu fingerprint=%llu\n",
            recipe->id,iterations,us,graph->nodeCount,graph->connectionCount,
            (unsigned long long)AnatomyGraph_Fingerprint(graph));
    }
    free(graph);return 0;
}
