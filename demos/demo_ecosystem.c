/** @file demo_ecosystem.c
 * @brief Adaptador visual ligero: el mismo mundo del ejecutable headless. */
#include "EcosystemScenario.h"
#include "OpenGLRenderer.h"
#include "PrimitiveMesh.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
static int compareDouble(const void *a,const void *b) {double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void draw(Renderer3D *r,const Mesh *mesh,Vector3 p,float yaw,float scale) {
    OpenGLRenderer_ModelTransform(p,yaw*57.29578f,scale,Vec3_Zero());r->renderMesh(r,mesh);OpenGLRenderer_PopModelMatrix();
}
static void ring(Vector3 p,float radius,Color c) {
    for(unsigned i=0;i<24;++i) {
        float a=i*6.2831853f/24,b=(i+1)*6.2831853f/24;
        OpenGLRenderer_DebugLine(Vec3_Add(p,Vec3_Create(sinf(a)*radius,.03f,cosf(a)*radius)),
            Vec3_Add(p,Vec3_Create(sinf(b)*radius,.03f,cosf(b)*radius)),c);
    }
}
int main(int argc,char **argv) {
    EcosystemConfig config=EcosystemConfig_Default();unsigned frames=0;const char *capture=NULL,*profile=NULL;double initialSpeed=1;
    for(int i=1;i<argc;++i) {
        if(!strncmp(argv[i],"--seed=",7))config.seed=(uint32_t)strtoul(argv[i]+7,NULL,10);
        else if(!strncmp(argv[i],"--frames=",9))frames=(unsigned)strtoul(argv[i]+9,NULL,10);
        else if(!strncmp(argv[i],"--scenario=",11)){if(!EcosystemConfig_Scenario(&config,argv[i]+11))return 2;}
        else if(!strncmp(argv[i],"--herbivores=",13))config.herbivores=(unsigned)strtoul(argv[i]+13,NULL,10);
        else if(!strncmp(argv[i],"--carnivores=",13))config.carnivores=(unsigned)strtoul(argv[i]+13,NULL,10);
        else if(!strncmp(argv[i],"--plants=",9))config.plants=(unsigned)strtoul(argv[i]+9,NULL,10);
        else if(!strncmp(argv[i],"--capacity=",11))config.capacity=(unsigned)strtoul(argv[i]+11,NULL,10);
        else if(!strncmp(argv[i],"--speed=",8))initialSpeed=strtod(argv[i]+8,NULL);
        else if(!strncmp(argv[i],"--capture=",10))capture=argv[i]+10;
        else if(!strncmp(argv[i],"--profile=",10))profile=argv[i]+10;
        else if(!strcmp(argv[i],"--help")){puts("--seed=N --scenario=A..E --frames=N --speed=1|2|4|16 --herbivores=N --carnivores=N --plants=N --capacity=N --capture=imagen.ppm --profile=metricas.json");return 0;}
        else return 2;
    }
    if(frames>1000000||(initialSpeed!=1&&initialSpeed!=2&&initialSpeed!=4&&initialSpeed!=16))return 2;
    EcosystemWorld w;if(!EcosystemScenario_Create(&w,&config))return 1;
    w.clock.speed=initialSpeed;
    OpenGLDemoWindow *window=OpenGLDemoWindow_Create("Monster Engine - Ecosistema",1280,800);
    if(!window){EcosystemWorld_Free(&w);return 1;}
    ICamera camera={.position={28,28,32},.target={0,0,0},.up={0,1,0},.fov=50,.nearPlane=.1f,.farPlane=200};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    Mesh meshes[6];for(unsigned i=0;i<6;++i)meshes[i]=Mesh_Create();
    Color colors[]={ {127,191,111,255},{212,109,76,255},{74,150,66,255},{65,139,191,255},{124,92,68,255},{54,70,53,255} };
    bool ok=true;
    for(unsigned i=0;i<6;++i) {
        Vector3 scale=i==3?Vec3_Create(1,.03f,1):i==5?Vec3_Create(1,.0001f,1):Vec3_Create(.28f,.24f,.48f);
        ok=PrimitiveMesh_GenerateEllipsoid(&meshes[i],(Transform3D){.position={0,(i==3||i==5)?0:.25f,0},.rotation={0,0,0},.scale=scale},12,6,colors[i])&&ok;
    }
    if(!ok){for(unsigned i=0;i<6;++i)Mesh_Free(&meshes[i]);OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);EcosystemWorld_Free(&w);return 1;}
    double *times=frames?calloc(frames,sizeof(double)):NULL,*simulationTimes=frames?calloc(frames,sizeof(double)):NULL;
    if(frames&&(!times||!simulationTimes)){free(times);free(simulationTimes);for(unsigned i=0;i<6;++i)Mesh_Free(&meshes[i]);OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);EcosystemWorld_Free(&w);return 1;}
    double last=OpenGLDemoWindow_Time(),angle=.65,zoom=48,gpuSum=0;unsigned gpuSamples=0;uint64_t gpuGeneration=0;
    unsigned frame=0,selectedSlot=0;bool debug=false,follow=false;Vector3 cameraCenter=Vec3_Zero();float history[160]={0};unsigned historyCount=0,historyIndex=0;double nextSample=0;
    const double speeds[]={1,2,4,16};unsigned speedIndex=initialSpeed==16?3:initialSpeed==4?2:initialSpeed==2?1:0;
    clock_t processStart=clock();
    while(!frames||frame<frames) {
        double start=OpenGLDemoWindow_Time(),dt=frames?1./60:start-last;last=start;
        OpenGLDemoInput input=OpenGLDemoWindow_Poll(window);if(input.quit)break;
        if(input.togglePause)w.clock.paused=!w.clock.paused;
        if(input.toggleDebug)debug=!debug;
        if(input.digitKey>=0&&input.digitKey<4){speedIndex=(unsigned)input.digitKey;w.clock.speed=speeds[speedIndex];}
        if(input.surfaceSelect)angle+=input.surfaceSelect*.12;
        if(input.surfaceAdjust)zoom=fmax(18,fmin(90,zoom-input.surfaceAdjust*3));
        if(input.surfaceDebugNext) {
            for(unsigned i=0;i<w.capacity;++i){selectedSlot=(selectedSlot+1)%w.capacity;if(w.entities[selectedSlot].id)break;}
        }
        if(input.keyR) {
            EcosystemWorld_Free(&w);if(!EcosystemScenario_Create(&w,&config)){ok=false;break;}
            w.clock.speed=speeds[speedIndex];nextSample=0;historyCount=historyIndex=0;
        }
        double simStart=OpenGLDemoWindow_Time();EcosystemWorld_Advance(&w,dt);
        if(simulationTimes)simulationTimes[frame]=(OpenGLDemoWindow_Time()-simStart)*1000;
        if(w.clock.seconds>=nextSample) {
            history[historyIndex]=w.count;historyIndex=(historyIndex+1)%160;if(historyCount<160)++historyCount;
            nextSample=w.clock.seconds+config.day/12;
        }
        if(input.keyF)follow=!follow;
        cameraCenter.x+=input.cameraRight*(float)dt*10;cameraCenter.z-=input.cameraForward*(float)dt*10;
        camera.target=follow&&w.entities[selectedSlot].id?w.entities[selectedSlot].body.position:cameraCenter;
        camera.position=Vec3_Add(camera.target,Vec3_Create((float)(sin(angle)*zoom),(float)(zoom*.75),(float)(cos(angle)*zoom)));
        renderer.beginFrame(&renderer);OpenGLRenderer_SetupCameraOrtho(&camera,(float)zoom,1280,800);
        draw(&renderer,&meshes[5],Vec3_Create(0,-.03f,0),0,34);
        float limit=w.observations.halfSize;
        Vector3 corners[]={{-limit,0,-limit},{limit,0,-limit},{limit,0,limit},{-limit,0,limit}};
        for(unsigned i=0;i<4;++i)OpenGLRenderer_DebugLine(corners[i],corners[(i+1)%4],Color_FromRGB(99,122,93));
        for(unsigned i=0;i<w.resourceCapacity;++i) {
            const EcosystemResource *r=&w.resources[i];if(!r->id)continue;
            const AIWorldObject *o=AIWorld_Get(&w.observations,r->id);if(!o)continue;
            if(r->category==ECO_WATER)draw(&renderer,&meshes[3],Vec3_Create(o->position.x,-.03f*o->radius,o->position.z),0,o->radius);
            else {
                float biomass=r->maximumBiomass>0?r->biomass/r->maximumBiomass:0;
                if(biomass>.001f)draw(&renderer,&meshes[r->category==ECO_PLANT?2:4],o->position,0,.3f+1.4f*sqrtf(biomass));
            }
        }
        for(unsigned i=0;i<w.capacity;++i) {
            EcosystemEntity *e=&w.entities[i];if(!e->id)continue;
            const EcosystemSpecies *s=&w.species[e->speciesIndex];
            float scale=e->developmentScale*e->genome.size;
            draw(&renderer,&meshes[s->brain.preyMask?1:0],e->body.position,e->body.yaw,scale);
            /* La punta visible corresponde a la boca usada en validación física. */
            draw(&renderer,&meshes[s->brain.preyMask?1:0],Vec3_Sub(e->body.mouth,Vec3_Create(0,.055f,0)),e->body.yaw,.22f*scale);
            Color pigment=Color_FromRGB((unsigned char)(90+140*e->genome.pigment),(unsigned char)(170-90*e->genome.pigment),110);
            OpenGLRenderer_DebugLine(Vec3_Add(e->body.position,Vec3_Create(0,.5f*scale,-.2f*scale)),
                Vec3_Add(e->body.position,Vec3_Create(0,.5f*scale,.2f*scale)),pigment);
            if(e->body.speed>.05f)for(unsigned leg=0;leg<4;++leg) {
                float phase=(float)w.clock.seconds*7+leg*3.14159f;
                float dx=(leg%2?.19f:-.19f)*scale,dz=(leg<2?.27f:-.27f)*scale;
                Vector3 hip=Vec3_Add(e->body.position,Vec3_Create(dx,.28f*scale,dz));
                Vector3 foot=Vec3_Add(e->body.position,Vec3_Create(dx,.02f,dz+sinf(phase)*.14f*scale));
                OpenGLRenderer_DebugLine(hip,foot,colors[s->brain.preyMask?1:0]);
            }
            if(debug) {
                OpenGLRenderer_DebugLine(e->oralPivot,e->oralElbow,Color_FromRGB(245,200,100));
                OpenGLRenderer_DebugLine(e->oralElbow,e->body.mouth,Color_FromRGB(245,200,100));
            }
            if(debug&&e->mind.target) {
                const AIWorldObject *target=AIWorld_Get(&w.observations,e->mind.target);
                if(target)OpenGLRenderer_DebugLine(e->body.eye,target->position,Color_FromRGB(246,209,81));
            }
        }
        if(!w.entities[selectedSlot].id)for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id){selectedSlot=i;break;}
        EcosystemEntity *selected=w.entities[selectedSlot].id?&w.entities[selectedSlot]:NULL;
        if(selected)ring(selected->body.position,.65f,Color_FromRGB(250,240,130));
        unsigned herbs=0,carns=0;
        for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id) {
            if(w.species[w.entities[i].speciesIndex].brain.preyMask)++carns;else ++herbs;
        }
        char panel[2048];unsigned minute=(unsigned)(SimulationClock_TimeOfDay(&w.clock)*1440);
        snprintf(panel,sizeof(panel),"ECOSISTEMA  SEMILLA %u\nDIA %.0f  %02u:%02u  %.0fX  %s%s\nHERBIVOROS %u  CARNIVOROS %u\nNACIMIENTOS %llu  MUERTES %llu\nINANICION %llu  SED %llu\nDEPREDACION %llu  EDAD %llu\nCAZAS %llu  BIOMASA %.1f\nCOMIDA %.1f  AGUA %.1f\n\nESPACIO PAUSA  1-4 VELOCIDAD\nFLECHAS ZOOM  IJKL PAN  TAB\nD DEBUG  R REINICIAR  F SEGUIR\nHISTORIA DE POBLACION",
            config.seed,floor(SimulationClock_Days(&w.clock)),minute/60,minute%60,w.clock.speed,w.clock.paused?"PAUSA":"ACTIVO",w.clock.behind?" ATRASO":"",
            herbs,carns,(unsigned long long)w.statistics.births,(unsigned long long)w.statistics.deaths,
            (unsigned long long)w.statistics.deathCauses[DEATH_STARVATION],(unsigned long long)w.statistics.deathCauses[DEATH_DEHYDRATION],
            (unsigned long long)w.statistics.deathCauses[DEATH_PREDATION],(unsigned long long)w.statistics.deathCauses[DEATH_AGE],
            (unsigned long long)w.statistics.kills,w.statistics.plantBiomass,w.statistics.foodConsumed,w.statistics.waterConsumed);
        OpenGLRenderer_DrawTextPanel(1280,800,24,26,panel);
        if(selected) {
            snprintf(panel,sizeof(panel),"INDIVIDUO %u  ESPECIE %u\nEDAD %.1f  ETAPA %u\nSALUD %.2f  ENERGIA %.2f\nAGUA %.2f  FATIGA %.2f\nOBJETIVO %s\nACCION %s\nDESTINO %u\nREPRODUCCION %s\nHAMBRE %.2f  SED %.2f\nDESCANSO %.2f  SOCIAL %.2f",
                selected->id,selected->body.species,selected->physiology.age,selected->physiology.stage,selected->physiology.health,selected->physiology.energy,
                selected->physiology.hydration,selected->physiology.fatigue,CreatureGoal_Name(selected->mind.goal),CreatureAction_Name(selected->mind.action),selected->mind.target,
                selected->reproduction.gestating?"GESTACION":selected->body.mateReady?"DISPONIBLE":"ESPERA",
                selected->mind.needs.urgency[NEED_HUNGER],selected->mind.needs.urgency[NEED_HYDRATION],selected->mind.needs.urgency[NEED_REST],selected->mind.needs.urgency[NEED_SOCIAL]);
            OpenGLRenderer_DrawTextPanel(1280,800,870,26,panel);
        }
        float graph[160];
        for(unsigned i=0;i<historyCount;++i)graph[i]=history[(historyIndex+160-historyCount+i)%160];
        OpenGLRenderer_DrawHistory(1280,800,14,315,360,100,graph,historyCount);
        double renderCpu=(OpenGLDemoWindow_Time()-start)*1000;
        snprintf(panel,sizeof(panel),"FPS %.0f  ENVIO %.2f MS",1000/fmax(.01,renderCpu),renderCpu);
        OpenGLRenderer_DrawTextPanel(1280,800,870,745,panel);
        if(capture&&frames&&frame+1==frames)ok=OpenGLRenderer_SavePPM(capture,1280,800)&&ok;
        renderer.endFrame(&renderer);
        OpenGLMeshPerformanceStats gpu=OpenGLRenderer_GetMeshPerformanceStats(&renderer);
        if(gpu.gpuTimingGeneration!=gpuGeneration){gpuGeneration=gpu.gpuTimingGeneration;gpuSum+=gpu.gpuRenderMs;++gpuSamples;}
        OpenGLDemoWindow_Swap(window);
        if(times)times[frame]=(OpenGLDemoWindow_Time()-start)*1000;
        ++frame;if(!frames)OpenGLDemoWindow_LimitFPS(start,60);
    }
    if(profile&&times&&frame) {
        double sum=0,sim=0;for(unsigned i=0;i<frame;++i){sum+=times[i];sim+=simulationTimes[i];}
        qsort(times,frame,sizeof(*times),compareDouble);struct rusage usage;getrusage(RUSAGE_SELF,&usage);
        FILE *f=fopen(profile,"w");if(!f)ok=false;
        else {
            fprintf(f,"{\"frames\":%u,\"width\":1280,\"height\":800,\"frame_mean_ms\":%.6f,\"frame_p50_ms\":%.6f,\"frame_p95_ms\":%.6f,\"frame_p99_ms\":%.6f,\"simulation_mean_ms\":%.6f,\"gpu_mean_ms\":%.6f,\"gpu_samples\":%u,\"peak_rss_kib\":%ld}\n",
                frame,sum/frame,times[frame/2],times[(frame-1)*95/100],times[(frame-1)*99/100],sim/frame,gpuSamples?gpuSum/gpuSamples:0,gpuSamples,usage.ru_maxrss);
            if(fclose(f))ok=false;
            fprintf(stdout,"process_cpu_mean_ms=%.6f\n",1000.*(clock()-processStart)/CLOCKS_PER_SEC/frame);
        }
    }
    ok=OpenGLRenderer_CheckErrors()&&ok;
    free(times);free(simulationTimes);for(unsigned i=0;i<6;++i)Mesh_Free(&meshes[i]);
    OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);EcosystemWorld_Free(&w);return ok?0:1;
}
