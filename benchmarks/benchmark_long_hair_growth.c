/** @file benchmark_long_hair_growth.c
 * @brief Banco headless del camino de producción y 21 edades, sin cambiar el motor. */
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

static double Now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6;}
static double Cpu(void){struct rusage r;getrusage(RUSAGE_SELF,&r);return (r.ru_utime.tv_sec+r.ru_stime.tv_sec)*1000.0+(r.ru_utime.tv_usec+r.ru_stime.tv_usec)*.001;}
static uint64_t Bytes(uint64_t h,const void* p,size_t n){const unsigned char* b=p;while(n--){h^=*b++;h*=UINT64_C(1099511628211);}return h;}
static int Threads(void){char line[128];int n=0;FILE* f=fopen("/proc/self/status","r");if(!f)return -1;while(fgets(line,sizeof(line),f))if(sscanf(line,"Threads: %d",&n)==1)break;fclose(f);return n;}
static bool Row(FILE* out,const char* mode,int stage,int rep,int step,float age,double agerMs,double wallMs,double cpuMs,const LongHairSet* hair){
    size_t visible=0,valid=0;uint64_t ids=UINT64_C(14695981039346656037),packed=ids;
    float (*rows)[4]=calloc(hair->count*LONG_HAIR_GPU_ROWS,sizeof(*rows));if(!rows)return false;
    size_t n=LongHairSet_Pack(hair,rows,hair->count*LONG_HAIR_GPU_ROWS);if(!n){free(rows);return false;}
    packed=Bytes(packed,rows,n*sizeof(*rows));free(rows);
    for(size_t i=0;i<hair->count;++i){const LongHairGuide* g=&hair->guides[i];visible+=g->visible;ids=Bytes(ids,&g->root.stableRootId,sizeof(g->root.stableRootId));
        for(unsigned c=0;c<hair->groups[g->groupIndex].childrenPerGuide;++c)valid+=g->follicles[c][3]>0;}
    struct rusage r;getrusage(RUSAGE_SELF,&r);
    fprintf(out,"%s,%d,%d,%d,%.9g,%.9f,%.9f,%.9f,%zu,%zu,%zu,%016" PRIx64 ",%016" PRIx64 ",%d,%ld\n",mode,stage,rep,step,age,agerMs,wallMs,cpuMs,hair->count,visible,valid,ids,packed,Threads(),r.ru_maxrss);
    fflush(out);return hair->count==288&&visible==288&&valid==9216;
}
int main(int argc,char** argv){
    if(argc!=8){fprintf(stderr,"Usage: %s RECIPE OUTPUT WARMUPS SAMPLES GROWTH_WARMUPS GROWTH_SAMPLES MODE(all|stages|growth)\n",argv[0]);return 2;}
    unsigned warmups,samples,gw,gs;char tail;
    if(sscanf(argv[3],"%u%c",&warmups,&tail)!=1||sscanf(argv[4],"%u%c",&samples,&tail)!=1||sscanf(argv[5],"%u%c",&gw,&tail)!=1||sscanf(argv[6],"%u%c",&gs,&tail)!=1||warmups>100||samples<1||samples>100||gw>100||gs<1||gs>100)return 2;
    bool stages=strcmp(argv[7],"growth")!=0,growth=strcmp(argv[7],"stages")!=0;
    if(strcmp(argv[7],"all")&&strcmp(argv[7],"stages")&&strcmp(argv[7],"growth"))return 2;
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));Monster* young=malloc(sizeof(*young));MonsterAger* ager=malloc(sizeof(*ager));
    if(!doc||!m||!young||!ager){free(doc);free(m);free(young);free(ager);return 1;}
    char error[256]={0};if(!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error))){fprintf(stderr,"%s\n",error);free(doc);free(m);free(young);free(ager);return 1;}
    FILE* out=fopen(argv[2],"w");if(!out){free(doc);free(m);free(young);free(ager);return 1;}
    fputs("mode,stage,rep,step,age,ager_ms,hair_ms,process_cpu_ms,guides,visible,valid_children,stable_ids_fnv64,packed_fnv64,process_threads,maxrss_kib\n",out);
    *m=Monster_Create();*young=Monster_Create();LongHairSet hair={0};bool ok=true;
    if(stages)for(int stage=0;stage<4&&ok;++stage){Monster_Free(m);*m=Monster_Create();ok=Creature_BuildMonster(m,&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage));if(!ok)break;Monster_SetHeadOpenFactor(m,0);
        for(unsigned rep=0;rep<warmups+samples&&ok;++rep){hair.fingerprint=0;double c=Cpu(),t=Now();ok=MonsterVisual_UpdateLongHair(&hair,m);double wall=Now()-t,cpu=Cpu()-c;
            if(ok&&rep>=warmups)ok=Row(out,"stage",stage,(int)(rep-warmups),-1,stage/3.f,0,wall,cpu,&hair);}}
    if(growth&&ok){Monster_Free(m);*m=Monster_Create();ok=Creature_BuildMonster(m,&doc->recipe,&doc->recipe.adult)&&Creature_BuildMonster(young,&doc->recipe,&doc->recipe.juvenile);
        if(ok){*ager=MonsterAger_Create(young,m,0);MonsterAger_SetGeometrySnapshotEnabled(ager,false);
            for(unsigned rep=0;rep<gw+gs&&ok;++rep){LongHairSet_Free(&hair);
                for(int step=0;step<=20&&ok;++step){double t=Now();MonsterAger_SetPerc(ager,step/20.f);double ams=Now()-t,c=Cpu();t=Now();ok=MonsterVisual_UpdateLongHair(&hair,MonsterAger_GetResultConst(ager));double wall=Now()-t,cpu=Cpu()-c;
                    if(ok&&rep>=gw)ok=Row(out,"growth",-1,(int)(rep-gw),step,step/20.f,ams,wall,cpu,&hair);}}
            MonsterAger_Free(ager);}}
    LongHairSet_Free(&hair);Monster_Free(m);Monster_Free(young);free(m);free(young);free(ager);free(doc);if(fclose(out))ok=false;return ok?0:1;
}
