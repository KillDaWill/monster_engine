/** @file benchmark_long_hair_parallel.c
 * @brief Medición headless del camino de producción MonsterVisual_UpdateLongHair. */
#define _POSIX_C_SOURCE 200809L
#include "Creature.h"
#include "CreatureRecipeIO.h"
#include "MonsterAger.h"
#include "MonsterVisual.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double NowMs(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6;}
static double CpuMs(void){struct rusage r;getrusage(RUSAGE_SELF,&r);return (r.ru_utime.tv_sec+r.ru_stime.tv_sec)*1000.0+(r.ru_utime.tv_usec+r.ru_stime.tv_usec)*.001;}
static uint64_t Hash(uint64_t h,const void* data,size_t size){const unsigned char* p=data;while(size--){h^=*p++;h*=UINT64_C(1099511628211);}return h;}
static int ProcessThreads(void){FILE* f=fopen("/proc/self/status","r");if(!f)return -1;char line[128];int n=-1;while(fgets(line,sizeof(line),f))if(sscanf(line,"Threads: %d",&n)==1)break;fclose(f);return n;}
static int RequestedThreads(void){const char* s=getenv("MONSTER_LONG_HAIR_THREADS");if(!s||!*s)return 1;int n=atoi(s);return n>0?n:1;}
static int Row(FILE* out,const char* mode,int stage,int rep,int step,float age,double agerMs,double wallMs,double cpuMs,const LongHairSet* hair){
    float (*rows)[4]=calloc(hair->count*LONG_HAIR_GPU_ROWS,sizeof(*rows));if(!rows)return 0;size_t packed=LongHairSet_Pack(hair,rows,hair->count*LONG_HAIR_GPU_ROWS);if(!packed){free(rows);return 0;}
    uint64_t packHash=Hash(UINT64_C(14695981039346656037),rows,packed*sizeof(*rows));free(rows);uint64_t ids=UINT64_C(14695981039346656037);size_t visible=0,valid=0;
    for(size_t i=0;i<hair->count;i++){const LongHairGuide* g=&hair->guides[i];visible+=g->visible;ids=Hash(ids,&g->root.stableRootId,sizeof(g->root.stableRootId));
        for(unsigned c=0;c<hair->groups[g->groupIndex].childrenPerGuide;c++)valid+=g->follicles[c][3]>0;}
    struct rusage usage;getrusage(RUSAGE_SELF,&usage);
    fprintf(out,"%s,%d,%d,%d,%d,%.9g,%.6f,%.6f,%.6f,%zu,%zu,%zu,%016" PRIx64 ",%016" PRIx64 ",%d,%ld\n",mode,RequestedThreads(),stage,rep,step,age,agerMs,wallMs,cpuMs,hair->count,visible,valid,ids,packHash,ProcessThreads(),usage.ru_maxrss);fflush(out);
    return hair->count>0&&visible==hair->count;
}
static int RunUpdate(FILE* out,LongHairSet* hair,const Monster* m,const char* mode,int stage,int rep,int step,float age,double agerMs){double cpu=CpuMs(),begin=NowMs();int ok=MonsterVisual_UpdateLongHair(hair,m);double wall=NowMs()-begin;cpu=CpuMs()-cpu;return ok&&(rep<0||Row(out,mode,stage,rep,step,age,agerMs,wall,cpu,hair));}
int main(int argc,char** argv){
    if(argc!=8){fprintf(stderr,"Usage: %s RECIPE OUTPUT WARMUPS SAMPLES GROWTH_WARMUPS GROWTH_SAMPLES MODE(all|stages|growth)\n",argv[0]);return 2;}
    unsigned warmups=(unsigned)strtoul(argv[3],NULL,10),samples=(unsigned)strtoul(argv[4],NULL,10),gw=(unsigned)strtoul(argv[5],NULL,10),gs=(unsigned)strtoul(argv[6],NULL,10);
    if(!samples||!gs||warmups>100||samples>100||gw>100||gs>100)return 2;
    int stages=strcmp(argv[7],"growth")!=0,growth=strcmp(argv[7],"stages")!=0;
    if(strcmp(argv[7],"all")&&strcmp(argv[7],"stages")&&strcmp(argv[7],"growth"))return 2;
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m)),*young=malloc(sizeof(*young));MonsterAger* ager=malloc(sizeof(*ager));if(!doc||!m||!young||!ager)return 1;
    char error[256]={0};if(!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error))){fprintf(stderr,"recipe load failed: %s\n",error);return 1;}
    FILE* out=fopen(argv[2],"w");if(!out)return 1;fputs("mode,requested_threads,stage,rep,step,age,ager_ms,hair_ms,process_cpu_ms,guides,visible,valid_children,stable_ids_fnv64,packed_fnv64,process_threads,maxrss_kib\n",out);
    *m=Monster_Create();*young=Monster_Create();LongHairSet hair={0};int ok=1;
    if(stages)for(int stage=0;stage<4&&ok;stage++){Monster_Free(m);*m=Monster_Create();ok=Creature_BuildMonster(m,&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage));if(!ok)break;Monster_SetHeadOpenFactor(m,0);
        for(unsigned rep=0;rep<warmups+samples&&ok;rep++){hair.fingerprint=0;int sample=(int)rep-(int)warmups;ok=RunUpdate(out,&hair,m,"stage",stage,sample,0,stage/3.f,0);}}
    if(growth&&ok){Monster_Free(m);*m=Monster_Create();ok=Creature_BuildMonster(m,&doc->recipe,&doc->recipe.adult)&&Creature_BuildMonster(young,&doc->recipe,&doc->recipe.juvenile);
        if(ok){*ager=MonsterAger_Create(young,m,0);MonsterAger_SetGeometrySnapshotEnabled(ager,false);
            const char* paths[]={"forward","reverse","random"};unsigned randomState=0x6d2b79f5u;
            for(int path=0;path<3&&ok;path++)for(unsigned rep=0;rep<gw+gs&&ok;rep++){LongHairSet_Free(&hair);
                for(int step=0;step<21&&ok;step++){int k=path==1?20-step:step;float age;
                    if(path==2){randomState=randomState*1664525u+1013904223u;age=(randomState>>8)*(1.f/16777216.f);}else age=k/20.f;
                    double t=NowMs();MonsterAger_SetPerc(ager,age);double agerMs=NowMs()-t;int sample=(int)rep-(int)gw;ok=RunUpdate(out,&hair,MonsterAger_GetResultConst(ager),paths[path],-1,sample,step,age,agerMs);}
            }MonsterAger_Free(ager);}}
    LongHairSet_Free(&hair);Monster_Free(m);Monster_Free(young);free(doc);free(m);free(young);free(ager);if(fclose(out))ok=0;return ok?0:1;
}
