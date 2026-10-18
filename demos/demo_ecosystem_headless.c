/** @file demo_ecosystem_headless.c
 * @brief Ejecución ecológica reproducible sin SDL/OpenGL. */
#include "EcosystemScenario.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
static bool number(const char *s,double *out) {
    char *end;errno=0;*out=strtod(s,&end);return !errno&&*s&&!*end&&isfinite(*out)&&*out>=0;
}
int main(int argc,char **argv) {
    EcosystemConfig c=EcosystemConfig_Default();const char *summary=NULL,*history=NULL,*events=NULL;bool trace=false;
    for(int i=1;i<argc;++i)if(!strncmp(argv[i],"--scenario=",11)&&!EcosystemConfig_Scenario(&c,argv[i]+11)) {
        fprintf(stderr,"Escenario desconocido\n");return 2;
    }
    for(int i=1;i<argc;++i) {
        const char *a=argv[i];double n=0;
        if(!strcmp(a,"--trace")){trace=true;continue;}
        if(!strcmp(a,"--help")) {
            puts("--seed=N --days=N --herbivores=N --carnivores=N --plants=N --capacity=N --fixed-dt=N --day-duration=N --scenario=reference|A|B|C|D|E --summary=archivo.json --history=archivo.csv --events=eventos.csv --trace");return 0;
        }
        if(!strncmp(a,"--scenario=",11))continue;
        if(!strncmp(a,"--summary=",10)){summary=a+10;continue;}
        if(!strncmp(a,"--events=",9)){events=a+9;continue;}
        if(!strncmp(a,"--history=",10)){history=a+10;continue;}
        const char *value=strchr(a,'=');if(!value||!number(value+1,&n))goto invalid;
        if(!strncmp(a,"--days=",7)){c.days=n;continue;}
        if(!strncmp(a,"--fixed-dt=",11)){c.step=n;continue;}
        if(!strncmp(a,"--day-duration=",15)){c.day=n;continue;}
        if(!strncmp(a,"--seed=",7)){if(n>UINT32_MAX||floor(n)!=n)goto invalid;c.seed=(uint32_t)n;continue;}
        if(n>100000||floor(n)!=n)goto invalid;
        if(!strncmp(a,"--herbivores=",13))c.herbivores=(unsigned)n;
        else if(!strncmp(a,"--carnivores=",13))c.carnivores=(unsigned)n;
        else if(!strncmp(a,"--plants=",9))c.plants=(unsigned)n;
        else if(!strncmp(a,"--capacity=",11))c.capacity=(unsigned)n;
        else goto invalid;
        continue;
invalid:
        fprintf(stderr,"Argumento inválido: %s\n",a);return 2;
    }
    double ticks=c.days*c.day/c.step;
    if(!isfinite(ticks)||ticks<0||ticks>1e10||c.step<=0||c.day<=0)return 2;
    EcosystemWorld w;if(!EcosystemScenario_Create(&w,&c)){fprintf(stderr,"No se pudo crear el ecosistema\n");return 1;}
    FILE *csv=history?fopen(history,"w"):NULL;
    if(history&&!csv){perror(history);EcosystemWorld_Free(&w);return 1;}
    FILE *eventFile=events?fopen(events,"w"):NULL;
    if(events&&!eventFile){perror(events);if(csv)fclose(csv);EcosystemWorld_Free(&w);return 1;}
    EcosystemWorld_SetEventOutput(&w,eventFile);
    clock_t start=clock();uint64_t total=(uint64_t)ceil(ticks-1e-9);double next=0;
    for(uint64_t i=0;i<total;++i) {
        EcosystemWorld_Step(&w);
        if(trace&&i%600==0)for(unsigned j=0;j<w.capacity;++j) {
            EcosystemEntity *e=&w.entities[j];if(!e->id)continue;
            fprintf(stderr,"t=%.1f id=%u species=%u action=%s target=%u pos=%.2f,%.2f E=%.2f H=%.2f ready=%d memory=%u failures=%u\n",
                w.clock.seconds,e->id,e->body.species,CreatureAction_Name(e->mind.action),e->mind.target,
                e->body.position.x,e->body.position.z,e->physiology.energy,e->physiology.hydration,e->body.mateReady,e->mind.perception.count,e->mind.actionFailures);
        }
        if(csv&&w.clock.seconds>=next) {
            if(!EcosystemWorld_WriteCSV(&w,csv,next==0)){fclose(csv);if(eventFile)fclose(eventFile);EcosystemWorld_Free(&w);return 1;}
            next+=c.day/4;
        }
    }
    double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;
    bool ok=true;if(csv&&fclose(csv))ok=false;
    EcosystemWorld_SetEventOutput(&w,NULL);
    if(eventFile){if(ferror(eventFile))ok=false;if(fclose(eventFile))ok=false;}
    FILE *json=summary?fopen(summary,"w"):stdout;
    if(!json){perror(summary);ok=false;}
    else {ok=EcosystemWorld_WriteJSON(&w,json)&&ok;if(summary&&fclose(json))ok=false;}
    fprintf(stderr,"ticks=%llu cpu_seconds=%.6f mean_tick_ms=%.6f population=%u births=%llu deaths=%llu\n",
        (unsigned long long)total,elapsed,total?elapsed*1000/total:0,w.count,
        (unsigned long long)w.statistics.births,(unsigned long long)w.statistics.deaths);
    EcosystemWorld_Free(&w);return ok?0:1;
}
