/** @file benchmark_ecosystem.c
 * @brief Coste por tick, percentiles y reutilización repetida de entidades. */
#include "EcosystemScenario.h"
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>
#include <string.h>
static int compare(const void *a,const void *b) {double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
int main(int argc,char **argv) {
    unsigned count=argc>1?(unsigned)strtoul(argv[1],NULL,10):100;
    unsigned ticks=argc>2?(unsigned)strtoul(argv[2],NULL,10):12000;
    unsigned cycles=argc>3?(unsigned)strtoul(argv[3],NULL,10):10000;
    if(!count||count>2000||!ticks||ticks>1000000||cycles>1000000)return 2;
    EcosystemConfig c=EcosystemConfig_Default();c.herbivores=count*4/5;c.carnivores=count-c.herbivores;c.capacity=count*2;c.plants=40;
    EcosystemWorld w;if(!EcosystemScenario_Create(&w,&c))return 1;
    double *samples=calloc(ticks,sizeof(*samples));if(!samples){EcosystemWorld_Free(&w);return 1;}
    double sum=0;for(unsigned i=0;i<ticks;++i) {
        clock_t start=clock();EcosystemWorld_Step(&w);samples[i]=1000.*(clock()-start)/CLOCKS_PER_SEC;sum+=samples[i];
    }
    qsort(samples,ticks,sizeof(*samples),compare);struct rusage usage;getrusage(RUSAGE_SELF,&usage);
    printf("{\"initial_population\":%u,\"final_population\":%u,\"ticks\":%u,\"mean_ms\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f,\"max_ms\":%.6f,\"peak_rss_kib\":%ld,",
        count,w.count,ticks,sum/ticks,samples[ticks/2],samples[(ticks-1)*95/100],samples[(ticks-1)*99/100],samples[ticks-1],usage.ru_maxrss);
    free(samples);EcosystemWorld_Free(&w);
    if(!EcosystemWorld_Init(&w,128,128,1,42,1./60,120))return 1;
    if(!EcosystemWorld_AddSpecies(&w,EcosystemSpecies_Default(1,1,ECO_PLANT,0,0))){EcosystemWorld_Free(&w);return 1;}
    AIObjectId previous=0;clock_t start=clock();long warmRSS=0;
    for(unsigned i=0;i<cycles;++i) {
        AIObjectId id=EcosystemWorld_Spawn(&w,1,Vec3_Zero(),200,.8f,NULL);
        if(!id||id<=previous||!EcosystemWorld_RequestDeath(&w,id,DEATH_REMOVED)){EcosystemWorld_Free(&w);return 1;}
        previous=id;EcosystemWorld_Step(&w);
        if(w.count||EcosystemWorld_Get(&w,id)){EcosystemWorld_Free(&w);return 1;}
        if(i==100){getrusage(RUSAGE_SELF,&usage);warmRSS=usage.ru_maxrss;}
    }
    getrusage(RUSAGE_SELF,&usage);
    printf("\"spawn_death_cycles\":%u,\"cycle_mean_ms\":%.6f,\"warm_peak_rss_kib\":%ld,\"final_peak_rss_kib\":%ld}\n",
        cycles,cycles?1000.*(clock()-start)/CLOCKS_PER_SEC/cycles:0,warmRSS,usage.ru_maxrss);
    EcosystemWorld_Free(&w);return 0;
}
