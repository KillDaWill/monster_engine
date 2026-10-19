/** @file demo_ecosystem.c
 * @brief Adaptador visual del mismo mundo que el ejecutable headless.
 */
#include "EcosystemScenario.h"
#include "EcosystemVisual.h"
#include "CreatureRecipeRegistry.h"
#include "OpenGLRenderer.h"
#include "PrimitiveMesh.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static int compareDouble(const void *a,const void *b) {double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}

typedef enum ViewMode { VIEW_SIDE, VIEW_FRONT, VIEW_THREE_QUARTER, VIEW_OVERVIEW } ViewMode;
typedef enum CaptureState { CAPTURE_OVERVIEW, CAPTURE_ADULT, CAPTURE_JUVENILE, CAPTURE_NEWBORN,
    CAPTURE_WALKING, CAPTURE_DRINKING, CAPTURE_HUNTING, CAPTURE_FLEEING, CAPTURE_FEEDING, CAPTURE_RESTING, CAPTURE_ATTACKING } CaptureState;

static bool parseView(const char *value,ViewMode *out) {
    if(!strcmp(value,"side"))*out=VIEW_SIDE;
    else if(!strcmp(value,"front"))*out=VIEW_FRONT;
    else if(!strcmp(value,"three-quarter"))*out=VIEW_THREE_QUARTER;
    else if(!strcmp(value,"overview"))*out=VIEW_OVERVIEW;
    else return false;
    return true;
}

static bool parseCaptureState(const char *value,CaptureState *out) {
    const char *names[]={"overview","adult","juvenile","newborn","walking","drinking","hunting","fleeing","feeding","resting","attacking"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i) if(!strcmp(value,names[i])) {*out=(CaptureState)i;return true;}
    return false;
}

static void draw(Renderer3D *r,const Mesh *mesh,Vector3 p,float yaw,float scale) {
    if(!mesh||!mesh->vertexCount)return;
    OpenGLRenderer_ModelTransform(p,yaw*57.29578f,scale,Vec3_Zero());r->renderMesh(r,mesh);OpenGLRenderer_PopModelMatrix();
}

static void ring(Vector3 p,float radius,Color c) {
    for(unsigned i=0;i<24;++i) {
        float a=i*6.2831853f/24,b=(i+1)*6.2831853f/24;
        OpenGLRenderer_DebugLine(Vec3_Add(p,Vec3_Create(sinf(a)*radius,.03f,cosf(a)*radius)),
            Vec3_Add(p,Vec3_Create(sinf(b)*radius,.03f,cosf(b)*radius)),c);
    }
}

static uint64_t packetGeneration(const EcosystemVisualDrawPacket *packet) {
    /* Los pesos pertenecen a la geometría compartida, no al individuo. */
    return packet->bodyMesh?packet->bodyMesh->geometryGeneration:1;
}

static bool projectEntity(const ICamera *camera,float orthoHeight,int width,int height,
                          const EcosystemEntity *entity,EcosystemVisualProjection *projection) {
    Vector3 forward=Vec3_Normalize(Vec3_Sub(camera->target,camera->position));
    Vector3 right=Vec3_Normalize(Vec3_Cross(forward,camera->up));
    Vector3 up=Vec3_Normalize(Vec3_Cross(right,forward));
    float radius=fmaxf(.18f,.75f*entity->developmentScale*fmaxf(.1f,entity->genome.size));
    Vector3 relative=Vec3_Sub(entity->body.position,camera->target);
    float horizontal=fabsf(Vec3_Dot(relative,right)),vertical=fabsf(Vec3_Dot(relative,up));
    float depth=Vec3_Dot(Vec3_Sub(entity->body.position,camera->position),forward);
    float halfHeight=orthoHeight*.5f,halfWidth=halfHeight*(float)width/(float)height;
    projection->visible=depth+radius>=camera->nearPlane&&depth-radius<=camera->farPlane&&
        horizontal<=halfWidth+radius&&vertical<=halfHeight+radius;
    projection->projectedPixels=2.0f*radius*(float)height/orthoHeight;
    return isfinite(projection->projectedPixels);
}

static void setCameraPose(ICamera *camera,Vector3 target,float zoom,double angle,ViewMode view) {
    float x,z;
    if(view==VIEW_SIDE){x=cosf((float)angle);z=-sinf((float)angle);}
    else if(view==VIEW_FRONT){x=sinf((float)angle);z=cosf((float)angle);}
    else if(view==VIEW_OVERVIEW){camera->target=target;camera->position=Vec3_Add(target,Vec3_Create(0,(float)(zoom*1.2),0));camera->up=Vec3_Create(0,0,-1);return;}
    else {x=sinf((float)angle);z=cosf((float)angle);}
    target.y+=zoom<8?.55f:0;
    camera->target=target;
    camera->position=Vec3_Add(target,Vec3_Create(x*(float)zoom,(float)(zoom*(view==VIEW_SIDE||view==VIEW_FRONT?.18:.48)),z*(float)zoom));
    camera->up=Vec3_Create(0,1,0);
}

static bool matchesCapture(const EcosystemWorld *world,const EcosystemEntity *entity,CaptureState state) {
    if(state==CAPTURE_OVERVIEW)return false;
    if(entity->physiology.stage==LIFE_DEAD)return false;
    switch(state) {
        case CAPTURE_ADULT:return entity->physiology.stage!=LIFE_JUVENILE;
        case CAPTURE_JUVENILE:return entity->physiology.stage==LIFE_JUVENILE;
        case CAPTURE_NEWBORN: {
            const EcosystemSpecies *species=&world->species[entity->speciesIndex];
            return entity->physiology.stage==LIFE_JUVENILE&&entity->physiology.age<=species->physiology.maturityAge*.25;
        }
        case CAPTURE_WALKING:return entity->body.speed>.05f;
        case CAPTURE_DRINKING:return entity->mind.action==ACTION_DRINK&&entity->body.reach.status==REACH_REACHED;
        case CAPTURE_FEEDING:return entity->mind.action==ACTION_EAT&&entity->body.reach.status==REACH_REACHED;
        case CAPTURE_ATTACKING:return entity->mind.action==ACTION_ATTACK&&entity->body.reach.status==REACH_REACHED;
        case CAPTURE_RESTING:return entity->mind.action==ACTION_REST;
        case CAPTURE_HUNTING:return entity->mind.action==ACTION_CHASE||entity->mind.action==ACTION_ATTACK||entity->mind.goal==GOAL_HUNT;
        case CAPTURE_FLEEING:return entity->mind.action==ACTION_ESCAPE||entity->mind.goal==GOAL_FLEE;
        default:return false;
    }
}

static const EcosystemEntity *findEntity(const EcosystemWorld *world,AIObjectId id) {
    if(!id)return NULL;
    for(unsigned i=0;i<world->capacity;++i)if(world->entities[i].id==id)return &world->entities[i];
    return NULL;
}

static double mouthFrameError(const EcosystemWorld *world,const EcosystemVisualDrawPacket *packet,bool *hasSample) {
    const EcosystemEntity *entity=findEntity(world,packet->entityId);
    if(!entity||!packet->ready||!packet->pose||packet->headJoint<0)return 0;
    *hasSample=true;
    return Vec3_Distance(packet->renderedMouth,entity->body.mouth);
}

int main(int argc,char **argv) {
    EcosystemConfig config=EcosystemConfig_Default();
    const char* recipesDirectory="recipes";
    unsigned frames=0,validateMouthFrames=0;const char *capture=NULL,*profile=NULL;double initialSpeed=1,initialZoom=22,initialAge=-1;bool showHud=true,captured=false,validateSkin=false,forceCpu=false;
    ViewMode view=VIEW_THREE_QUARTER;CaptureState captureState=CAPTURE_OVERVIEW;AIObjectId focusId=0;unsigned captureSpecies=0;bool hasFocus=false;
    for(int i=1;i<argc;++i) {
        if(!strncmp(argv[i],"--seed=",7))config.seed=(uint32_t)strtoul(argv[i]+7,NULL,10);
        else if(!strncmp(argv[i],"--frames=",9))frames=(unsigned)strtoul(argv[i]+9,NULL,10);
        else if(!strncmp(argv[i],"--scenario=",11)){if(!EcosystemConfig_Scenario(&config,argv[i]+11))return 2;}
        else if(!strncmp(argv[i],"--herbivores=",13))config.herbivores=(unsigned)strtoul(argv[i]+13,NULL,10);
        else if(!strncmp(argv[i],"--carnivores=",13))config.carnivores=(unsigned)strtoul(argv[i]+13,NULL,10);
        else if(!strncmp(argv[i],"--plants=",9))config.plants=(unsigned)strtoul(argv[i]+9,NULL,10);
        else if(!strncmp(argv[i],"--capacity=",11))config.capacity=(unsigned)strtoul(argv[i]+11,NULL,10);
        else if(!strncmp(argv[i],"--speed=",8))initialSpeed=strtod(argv[i]+8,NULL);
        else if(!strcmp(argv[i],"--validate-skinning"))validateSkin=true;
        else if(!strcmp(argv[i],"--cpu-skinning"))forceCpu=true;
        else if(!strncmp(argv[i],"--recipes-dir=",14))recipesDirectory=argv[i]+14;
        else if(!strcmp(argv[i],"--no-hud"))showHud=false;
        else if(!strncmp(argv[i],"--initial-age=",14))initialAge=strtod(argv[i]+14,NULL);
        else if(!strncmp(argv[i],"--zoom=",7))initialZoom=strtod(argv[i]+7,NULL);
        else if(!strncmp(argv[i],"--capture=",10))capture=argv[i]+10;
        else if(!strncmp(argv[i],"--profile=",10))profile=argv[i]+10;
        else if(!strncmp(argv[i],"--view=",7)){if(!parseView(argv[i]+7,&view))return 2;}
        else if(!strncmp(argv[i],"--focus=",8)){focusId=(AIObjectId)strtoul(argv[i]+8,NULL,10);hasFocus=true;}
        else if(!strncmp(argv[i],"--capture-species=",18))captureSpecies=(unsigned)strtoul(argv[i]+18,NULL,10);
        else if(!strncmp(argv[i],"--capture-state=",16)){if(!parseCaptureState(argv[i]+16,&captureState))return 2;}
        else if(!strncmp(argv[i],"--validate-mouth=",17))validateMouthFrames=(unsigned)strtoul(argv[i]+17,NULL,10);
        else if(!strcmp(argv[i],"--help")){puts("--recipes-dir=DIR --seed=N --scenario=A..E --frames=N --speed=1|2|4|16 --herbivores=N --carnivores=N --plants=N --capacity=N --capture=imagen.ppm --profile=metricas.json --view=side|front|three-quarter|overview --focus=ID --capture-state=adult|juvenile|newborn|walking|drinking|feeding|hunting|attacking|fleeing|resting|overview --validate-mouth=N --zoom=2..90 --no-hud --initial-age=SEGUNDOS --capture-species=ID --validate-skinning --cpu-skinning");return 0;}
        else return 2;
    }
    if(!isfinite(initialAge)||initialAge>10000||frames>1000000||!isfinite(initialZoom)||initialZoom<2||initialZoom>90||(initialSpeed!=1&&initialSpeed!=2&&initialSpeed!=4&&initialSpeed!=16))return 2;
    char recipeError[256]={0};
    if(!CreatureRecipeRegistry_LoadDirectory(recipesDirectory,recipeError,sizeof(recipeError))){fprintf(stderr,"%s\n",recipeError);return 1;}
    EcosystemWorld w;if(!EcosystemScenario_Create(&w,&config))return 1;
    if(initialAge>=0)for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id)w.entities[i].physiology.age=initialAge;
    w.clock.speed=initialSpeed;
    OpenGLDemoWindow *window=OpenGLDemoWindow_Create("Monster Engine - Ecosistema",1280,800);
    if(!window){EcosystemWorld_Free(&w);return 1;}
    EcosystemVisual visual={0};
    if(!EcosystemVisual_Init(&visual,&w)){
        fprintf(stderr,"[ADVERTENCIA] No se pudo inicializar EcosystemVisual.\n");
        OpenGLDemoWindow_Free(window);EcosystemWorld_Free(&w);return 1;
    }
    ICamera camera={.position={28,28,32},.target={0,0,0},.up={0,1,0},.fov=50,.aspectRatio=1.6f,.nearPlane=.1f,.farPlane=200};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    Mesh meshes[4];for(unsigned i=0;i<4;++i)meshes[i]=Mesh_Create();
    Color decorColors[]={{54,70,53,255},{65,139,191,255},{74,150,66,255},{124,92,68,255}};
    bool ok=true;
    ok=PrimitiveMesh_GenerateEllipsoid(&meshes[0],(Transform3D){.position={0,-.0001f,0},.rotation={0,0,0},.scale={1,.0001f,1}},12,6,decorColors[0])&&ok;
    ok=PrimitiveMesh_GenerateEllipsoid(&meshes[1],(Transform3D){.position={0,0,0},.rotation={0,0,0},.scale={1,.03f,1}},12,6,decorColors[1])&&ok;
    ok=PrimitiveMesh_GenerateEllipsoid(&meshes[2],(Transform3D){.position={0,.25f,0},.rotation={0,0,0},.scale={.28f,.24f,.48f}},12,6,decorColors[2])&&ok;
    ok=PrimitiveMesh_GenerateEllipsoid(&meshes[3],(Transform3D){.position={0,.25f,0},.rotation={0,0,0},.scale={.28f,.24f,.48f}},12,6,decorColors[3])&&ok;
    if(!ok){for(unsigned i=0;i<4;++i)Mesh_Free(&meshes[i]);EcosystemVisual_Shutdown(&visual);OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);EcosystemWorld_Free(&w);return 1;}
    double *times=frames?calloc(frames,sizeof(double)):NULL,*simulationTimes=frames?calloc(frames,sizeof(double)):NULL;
    double *mouthWindow=validateMouthFrames?calloc(validateMouthFrames,sizeof(double)):NULL;
    if((frames&&(!times||!simulationTimes))||(validateMouthFrames&&!mouthWindow)){
        free(times);free(simulationTimes);free(mouthWindow);for(unsigned i=0;i<4;++i)Mesh_Free(&meshes[i]);EcosystemVisual_Shutdown(&visual);OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);EcosystemWorld_Free(&w);return 1;
    }
    /* 22 conserva el mapa de 28 unidades legible y da unas 75 px al cuerpo de 2,05. */
    double last=OpenGLDemoWindow_Time(),angle=.65,zoom=initialZoom,gpuSum=0;unsigned gpuSamples=0,gpuTimingGeneration=0;
    OpenGLMeshPerformanceStats finalGpu={0};float footErrorMax=0,stanceFootErrorMax=0,skinPositionError=0,skinNormalError=0;
    unsigned initialPopulation=w.count,drawnMin=ECOSYSTEM_VISUAL_MAX_SLOTS,drawnMax=0;double drawnSum=0,adapterSum=0,renderSum=0,geometryWarmSeconds=0;
    AIObjectId validatedSkin[ECOSYSTEM_VISUAL_MAX_SLOTS]={0};
    Mesh cpuBodies[ECOSYSTEM_VISUAL_MAX_SLOTS]={0};
    unsigned frame=0,selectedSlot=0;bool debug=false,follow=hasFocus;Vector3 cameraCenter=Vec3_Zero();float history[160]={0};unsigned historyCount=0,historyIndex=0;double nextSample=0;
    if(hasFocus)for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id==focusId){selectedSlot=i;break;}
    const double speeds[]={1,2,4,16};unsigned speedIndex=initialSpeed==16?3:initialSpeed==4?2:initialSpeed==2?1:0;
    AIObjectId selectedRequestId=0,captureTargetId=0;bool focusWarned=false,captureWarned=false,updateWarned=false,projectionWarned=false;
    unsigned mouthWindowCount=0,mouthWindowIndex=0;bool scaleProofPrinted[5]={false};
    clock_t processStart=clock();
    EcosystemVisualDrawPacket displayedPackets[ECOSYSTEM_VISUAL_MAX_SLOTS];size_t displayedCount=0;
    while(!frames||frame<frames) {
        double start=OpenGLDemoWindow_Time(),dt=frames?1./60:start-last;last=start;
        OpenGLDemoInput input=OpenGLDemoWindow_Poll(window);if(input.quit)break;
        if(input.mouseLeftPressed&&displayedCount) {
            Vector3 forward=Vec3_Normalize(Vec3_Sub(camera.target,camera.position));
            Vector3 right=Vec3_Normalize(Vec3_Cross(forward,camera.up));
            Vector3 up=Vec3_Normalize(Vec3_Cross(right,forward));
            float x=((input.mouseX+.5f)/1280.0f-.5f)*(float)zoom*1.6f;
            float y=(.5f-(input.mouseY+.5f)/800.0f)*(float)zoom;
            Vector3 origin=Vec3_Add(camera.position,Vec3_Add(Vec3_Scale(right,x),Vec3_Scale(up,y)));
            AIObjectId picked=EcosystemVisual_PickRay(displayedPackets,displayedCount,origin,forward,camera.nearPlane,camera.farPlane);
            if(picked)for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id==picked) {
                selectedSlot=i;hasFocus=false;focusId=0;follow=true;
                printf("mouse_focus_entity=%u\n",picked);break;
            }
        }
        if(input.togglePause)w.clock.paused=!w.clock.paused;
        if(input.toggleDebug)debug=!debug;
        if(input.digitKey>=0&&input.digitKey<4){speedIndex=(unsigned)input.digitKey;w.clock.speed=speeds[speedIndex];}
        if(input.surfaceSelect)angle+=input.surfaceSelect*.12;
        if(input.surfaceAdjust)zoom=fmax(2,fmin(90,zoom-input.surfaceAdjust*fmax(.3,zoom*.12)));
        if(input.surfaceDebugNext){hasFocus=false;focusId=0;for(unsigned i=0;i<w.capacity;++i){selectedSlot=(selectedSlot+1)%w.capacity;if(w.entities[selectedSlot].id)break;}}
        if(input.keyR) {
            EcosystemWorld_Free(&w);if(!EcosystemScenario_Create(&w,&config)){ok=false;break;}
            w.clock.speed=speeds[speedIndex];nextSample=0;historyCount=historyIndex=0;selectedSlot=0;selectedRequestId=0;captureTargetId=0;
            displayedCount=0;follow=false;cameraCenter=Vec3_Zero();
            if(!EcosystemVisual_BindWorld(&visual,&w))fprintf(stderr,"[ADVERTENCIA] Fallo al volver a enlazar EcosystemVisual.\n");
            if(!EcosystemVisual_Validate(&visual))fprintf(stderr,"[ADVERTENCIA] EcosystemVisual inválido tras reinicio.\n");
        }
        double simStart=OpenGLDemoWindow_Time();EcosystemWorld_Advance(&w,dt);
        if(simulationTimes)simulationTimes[frame]=(OpenGLDemoWindow_Time()-simStart)*1000;
        if(w.clock.seconds>=nextSample) {
            history[historyIndex]=w.count;historyIndex=(historyIndex+1)%160;if(historyCount<160)++historyCount;
            nextSample=w.clock.seconds+config.day/12;
        }
        if(input.keyF){follow=!follow;hasFocus=false;focusId=0;}
        cameraCenter.x+=input.cameraRight*(float)dt*10;cameraCenter.z-=input.cameraForward*(float)dt*10;
        if(!w.entities[selectedSlot].id)for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id){selectedSlot=i;break;}
        EcosystemEntity *selected=w.entities[selectedSlot].id?&w.entities[selectedSlot]:NULL;
        const EcosystemEntity *focus=findEntity(&w,focusId);
        if(hasFocus&&!focus&&!focusWarned){fprintf(stderr,"[ADVERTENCIA] --focus=%u no corresponde a una entidad viva.\n",focusId);focusWarned=true;}
        if(captureTargetId){const EcosystemEntity* current=findEntity(&w,captureTargetId);if(!current||!matchesCapture(&w,current,captureState))captureTargetId=0;}
        if(captureState!=CAPTURE_OVERVIEW&&!captureTargetId)for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id&&(!captureSpecies||w.entities[i].body.species==captureSpecies)&&(!hasFocus||w.entities[i].id==focusId)&&matchesCapture(&w,&w.entities[i],captureState)){captureTargetId=w.entities[i].id;break;}
        if(selectedRequestId&&(!selected||selectedRequestId!=selected->id))EcosystemVisual_ClearTierRequest(&visual,selectedRequestId);
        selectedRequestId=selected?selected->id:0;if(selected)EcosystemVisual_RequestTier(&visual,selected->id,VISUAL_TIER_NEAR);
        if(focus)EcosystemVisual_RequestTier(&visual,focus->id,VISUAL_TIER_NEAR);
        if(captureTargetId){const EcosystemEntity *target=findEntity(&w,captureTargetId);if(target)EcosystemVisual_RequestTier(&visual,target->id,VISUAL_TIER_NEAR);}
        Vector3 target=cameraCenter;
        if(focus)target=focus->body.position;
        else if(captureTargetId){const EcosystemEntity *captureTarget=findEntity(&w,captureTargetId);if(captureTarget)target=captureTarget->body.position;}
        else if(follow&&selected)target=selected->body.position;
        cameraCenter=target;
        const EcosystemEntity* cameraSubject=focus?focus:findEntity(&w,captureTargetId);
        double cameraAngle=cameraSubject?cameraSubject->body.yaw+(view==VIEW_THREE_QUARTER?.65:0):angle;
        angle=cameraAngle;
        setCameraPose(&camera,target,zoom,cameraAngle,view);
        if(!EcosystemVisual_Sync(&visual)&&!projectionWarned){fprintf(stderr,"[ADVERTENCIA] No se pudo sincronizar EcosystemVisual.\n");projectionWarned=true;}
        for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id) {
            EcosystemVisualProjection projection={0};
            if(!projectEntity(&camera,(float)zoom,1280,800,&w.entities[i],&projection)&&!projectionWarned){fprintf(stderr,"[ADVERTENCIA] Proyección visual no finita.\n");projectionWarned=true;}
            EcosystemVisual_SetProjection(&visual,w.entities[i].id,projection);
        }
        if(frame==0) {
            EcosystemVisual_PrewarmVisible(&visual);
            double warmStart=OpenGLDemoWindow_Time();
            bool loading=true;
            while(loading&&OpenGLDemoWindow_Time()-warmStart<60) {
                EcosystemVisual_Update(&visual,.016f);
                EcosystemVisualDrawPacket warm[ECOSYSTEM_VISUAL_MAX_SLOTS];
                size_t count=EcosystemVisual_GetDrawPackets(&visual,warm,ECOSYSTEM_VISUAL_MAX_SLOTS);
                loading=false;for(size_t i=0;i<count;++i)if(!warm[i].ready)loading=true;
                if(OpenGLDemoWindow_Poll(window).quit){ok=false;loading=false;}
                if(loading) {
                    renderer.beginFrame(&renderer);
                    OpenGLRenderer_DrawTextPanel(1280,800,450,380,"GENERANDO ANATOMIA PROCEDURAL");
                    renderer.endFrame(&renderer);OpenGLDemoWindow_Swap(window);
                    OpenGLDemoWindow_LimitFPS(OpenGLDemoWindow_Time(),100);
                }
            }
            geometryWarmSeconds=OpenGLDemoWindow_Time()-warmStart;
            printf("geometry_warmup_seconds=%.6f cache_builds=%zu\n",geometryWarmSeconds,EcosystemVisual_BuildCount(&visual));
            start=OpenGLDemoWindow_Time();last=start;
        }
        double adapterStart=OpenGLDemoWindow_Time();
        if(!EcosystemVisual_Update(&visual,(float)dt)&&!updateWarned){fprintf(stderr,"[ADVERTENCIA] EcosystemVisual_Update agotó su estado; se usa malla fallback.\n");updateWarned=true;}
        adapterSum+=(OpenGLDemoWindow_Time()-adapterStart)*1000;
        double renderStart=OpenGLDemoWindow_Time();
        renderer.beginFrame(&renderer);OpenGLRenderer_SetupCameraOrtho(&camera,(float)zoom,1280,800);
        draw(&renderer,&meshes[0],Vec3_Create(0,-.03f,0),0,34);
        float limit=w.observations.halfSize;Vector3 corners[]={{-limit,0,-limit},{limit,0,-limit},{limit,0,limit},{-limit,0,limit}};
        for(unsigned i=0;i<4;++i)OpenGLRenderer_DebugLine(corners[i],corners[(i+1)%4],Color_FromRGB(99,122,93));
        for(unsigned i=0;i<w.resourceCapacity;++i) {
            const EcosystemResource *resource=&w.resources[i];if(!resource->id)continue;
            const AIWorldObject *object=AIWorld_Get(&w.observations,resource->id);if(!object)continue;
            if(resource->category==ECO_WATER)draw(&renderer,&meshes[1],Vec3_Create(object->position.x,-.03f*object->radius,object->position.z),0,object->radius);
            else {float biomass=resource->maximumBiomass>0?resource->biomass/resource->maximumBiomass:0;if(biomass>.001f)draw(&renderer,&meshes[resource->category==ECO_PLANT?2:3],object->position,0,.3f+1.4f*sqrtf(biomass));}
        }
        EcosystemVisualDrawPacket packets[ECOSYSTEM_VISUAL_MAX_SLOTS];size_t packetCount=EcosystemVisual_GetDrawPackets(&visual,packets,ECOSYSTEM_VISUAL_MAX_SLOTS);bool frameHasMouth=false,captureTargetReady=false;double frameMouthError=0;
        memcpy(displayedPackets,packets,packetCount*sizeof(*packets));displayedCount=packetCount;
        drawnSum+=packetCount;if(packetCount<drawnMin)drawnMin=(unsigned)packetCount;if(packetCount>drawnMax)drawnMax=(unsigned)packetCount;
         for(size_t i=0;i<packetCount;++i) {
             EcosystemVisualDrawPacket *packet=&packets[i];
             if(packet->entityId==captureTargetId&&packet->ready&&packet->tier==VISUAL_TIER_NEAR)captureTargetReady=true;
             const Mesh *body=packet->bodyMesh?packet->bodyMesh:EcosystemVisual_FallbackMesh(&visual);
             const EcosystemEntity *packetEntity=findEntity(&w,packet->entityId);
             if(packet->ready&&packetEntity&&packetEntity->speciesIndex<w.speciesCount) {
                 const EcosystemSpecies *packetSpecies=&w.species[packetEntity->speciesIndex];
                 uint32_t recipeId=packetSpecies->recipeId;
                 if((recipeId==3u||recipeId==4u)&&!scaleProofPrinted[recipeId]&&packet->meshRadius>1e-4f) {
                     float renderedWidth=packet->scale*packet->meshRadius;
                     float size=packetEntity->genome.size*packetEntity->developmentScale;
                 float expectedWidth=packetSpecies->radius*size;
                 float normalizedWidth=renderedWidth/fmaxf(.01f,size);
                     printf("scale_proof recipeId=%u meshRadius=%.6f measuredMeshRadius=%.6f meshLength=%.6f calibrationLength=%.6f totalScale=%.6f k=%.9f rendered_width=%.6f species_radius=%.6f expected_width=%.6f normalized_width=%.6f\n",
                         recipeId,packet->meshRadius,packet->measuredMeshRadius,packet->meshLength,packet->calibrationLength,
                         packet->axialTotalScale,packet->calibrationFactor,renderedWidth,packetSpecies->radius,expectedWidth,normalizedWidth);
                     scaleProofPrinted[recipeId]=true;
                 }
             }
             if(body){OpenGLRenderer_ModelTransform(packet->position,packet->yaw*57.29578f,packet->scale,Vec3_Zero());
                bool skinning=!forceCpu&&packet->deformer&&packet->skeleton&&packet->pose&&
                    OpenGLRenderer_SetMeshMorphSkinning(&renderer,body,packet->deformer,packet->skeleton,packet->pose,packet->targetGraph,packet->headJoint,packet->headScale,packetGeneration(packet));
                OpenGLRenderer_SetMeshSurfaceOverride(&renderer,&packet->surfaceRecipe);
                if(skinning||!packet->ready)renderer.renderMesh(&renderer,body);
                else if(EcosystemVisual_DeformBody(packet,&cpuBodies[packet->slotIndex]))renderer.renderMesh(&renderer,&cpuBodies[packet->slotIndex]);
                if(validateSkin&&skinning&&validatedSkin[packet->slotIndex]!=packet->entityId) {
                    float positionError=0,normalError=0;
                    bool valid=OpenGLRenderer_ValidateMeshSkinning(&renderer,body,packet->deformer,packet->skeleton,packet->pose,&positionError,&normalError);
                    ok=valid&&positionError<.0001f&&normalError<.0002f&&ok;
                    skinPositionError=fmaxf(skinPositionError,positionError);skinNormalError=fmaxf(skinNormalError,normalError);
                    validatedSkin[packet->slotIndex]=packet->entityId;
                }
                footErrorMax=fmaxf(footErrorMax,packet->footError);stanceFootErrorMax=fmaxf(stanceFootErrorMax,packet->stanceFootError);
                OpenGLRenderer_SetMeshSkinning(&renderer,NULL,NULL,NULL,NULL,0);
                OpenGLRenderer_PushJointMorphTransform(packet->restPose,packet->pose,packet->headJoint,packet->headScale);
                if(packet->headMesh&&packet->headMesh->vertexCount)renderer.renderMesh(&renderer,packet->headMesh);
                if(renderer.renderEye)for(size_t eye=0;eye<packet->eyeCount;++eye){
                    OpenGLRenderer_ModelTransform(packet->eyes[eye].center,0,packet->eyeScale,packet->eyes[eye].center);
                    renderer.renderEye(&renderer,&packet->eyes[eye]);OpenGLRenderer_PopModelMatrix();
                }
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_PushJointMorphTransform(packet->restPose,packet->pose,packet->jawJoint,packet->jawScale);
                for(size_t mouth=0;mouth<packet->mouthCount;++mouth)if(packet->mouths[mouth].jaw.vertexCount)renderer.renderMesh(&renderer,&packet->mouths[mouth].jaw);
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_PushJointMorphTransform(packet->restPose,packet->pose,packet->headJoint,packet->headScale);
                for(size_t mouth=0;mouth<packet->mouthCount;++mouth)if(packet->mouths[mouth].hinge.vertexCount)renderer.renderMesh(&renderer,&packet->mouths[mouth].hinge);
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_SetMeshSurfaceOverride(&renderer,NULL);(void)skinning;OpenGLRenderer_SetMeshSkinning(&renderer,NULL,NULL,NULL,NULL,0);OpenGLRenderer_PopModelMatrix();
            }
            if(validateMouthFrames){bool hasSample=false;double error=mouthFrameError(&w,packet,&hasSample);if(hasSample){frameHasMouth=true;if(error>frameMouthError)frameMouthError=error;}}
        }
        if(validateMouthFrames&&frameHasMouth){mouthWindow[mouthWindowIndex]=frameMouthError;mouthWindowIndex=(mouthWindowIndex+1)%validateMouthFrames;if(mouthWindowCount<validateMouthFrames)++mouthWindowCount;}
        for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id){
            EcosystemEntity *entity=&w.entities[i];
            Color pigment=Color_FromRGB((unsigned char)(90+140*entity->genome.pigment),(unsigned char)(170-90*entity->genome.pigment),110);
            if(debug)OpenGLRenderer_DebugLine(Vec3_Add(entity->body.position,Vec3_Create(0,.5f*entity->developmentScale*entity->genome.size,-.2f*entity->developmentScale*entity->genome.size)),Vec3_Add(entity->body.position,Vec3_Create(0,.5f*entity->developmentScale*entity->genome.size,.2f*entity->developmentScale*entity->genome.size)),pigment);
            if(debug){OpenGLRenderer_DebugLine(entity->oralPivot,entity->oralElbow,Color_FromRGB(245,200,100));OpenGLRenderer_DebugLine(entity->oralElbow,entity->body.mouth,Color_FromRGB(245,200,100));if(entity->mind.target){const AIWorldObject *object=AIWorld_Get(&w.observations,entity->mind.target);if(object)OpenGLRenderer_DebugLine(entity->body.eye,object->position,Color_FromRGB(246,209,81));}}
        }
        if(selected&&showHud)ring(selected->body.position,.65f,Color_FromRGB(250,240,130));
        if(showHud) {
        unsigned herbs=0,carns=0;for(unsigned i=0;i<w.capacity;++i)if(w.entities[i].id){if(w.species[w.entities[i].speciesIndex].brain.preyMask)++carns;else ++herbs;}
        char panel[2048];unsigned minute=(unsigned)(SimulationClock_TimeOfDay(&w.clock)*1440);
        snprintf(panel,sizeof(panel),"ECOSISTEMA  SEMILLA %u\nDIA %.0f  %02u:%02u  %.0fX  %s%s\nHERBIVOROS %u  CARNIVOROS %u\nNACIMIENTOS %llu  MUERTES %llu\nINANICION %llu  SED %llu\nDEPREDACION %llu  EDAD %llu\nCAZAS %llu  BIOMASA %.1f\nCOMIDA %.1f  AGUA %.1f\n\nESPACIO PAUSA  1-4 VELOCIDAD\nFLECHAS ZOOM  IJKL PAN  TAB\nD DEBUG  R REINICIAR  F SEGUIR\nCLIC IZQUIERDO ENFOCAR Y SEGUIR\nHISTORIA DE POBLACION",config.seed,floor(SimulationClock_Days(&w.clock)),minute/60,minute%60,w.clock.speed,w.clock.paused?"PAUSA":"ACTIVO",w.clock.behind?" ATRASO":"",herbs,carns,(unsigned long long)w.statistics.births,(unsigned long long)w.statistics.deaths,(unsigned long long)w.statistics.deathCauses[DEATH_STARVATION],(unsigned long long)w.statistics.deathCauses[DEATH_DEHYDRATION],(unsigned long long)w.statistics.deathCauses[DEATH_PREDATION],(unsigned long long)w.statistics.deathCauses[DEATH_AGE],(unsigned long long)w.statistics.kills,w.statistics.plantBiomass,w.statistics.foodConsumed,w.statistics.waterConsumed);
        OpenGLRenderer_DrawTextPanel(1280,800,24,26,panel);
        if(selected){snprintf(panel,sizeof(panel),"INDIVIDUO %u  ESPECIE %u\nEDAD %.1f  ETAPA %u\nSALUD %.2f  ENERGIA %.2f\nAGUA %.2f  FATIGA %.2f\nOBJETIVO %s\nACCION %s\nDESTINO %u\nREPRODUCCION %s\nHAMBRE %.2f  SED %.2f\nDESCANSO %.2f  SOCIAL %.2f",selected->id,selected->body.species,selected->physiology.age,selected->physiology.stage,selected->physiology.health,selected->physiology.energy,selected->physiology.hydration,selected->physiology.fatigue,CreatureGoal_Name(selected->mind.goal),CreatureAction_Name(selected->mind.action),selected->mind.target,selected->reproduction.gestating?"GESTACION":selected->body.mateReady?"DISPONIBLE":"ESPERA",selected->mind.needs.urgency[NEED_HUNGER],selected->mind.needs.urgency[NEED_HYDRATION],selected->mind.needs.urgency[NEED_REST],selected->mind.needs.urgency[NEED_SOCIAL]);OpenGLRenderer_DrawTextPanel(1280,800,870,26,panel);}
        float graph[160];for(unsigned i=0;i<historyCount;++i)graph[i]=history[(historyIndex+160-historyCount+i)%160];OpenGLRenderer_DrawHistory(1280,800,14,335,360,100,graph,historyCount);
        double renderCpu=(OpenGLDemoWindow_Time()-start)*1000;snprintf(panel,sizeof(panel),"FPS %.0f  ENVIO %.2f MS",1000/fmax(.01,renderCpu),renderCpu);OpenGLRenderer_DrawTextPanel(1280,800,870,745,panel);
        }
        renderSum+=(OpenGLDemoWindow_Time()-renderStart)*1000;
        bool captureNow=capture&&frames&&(captureState==CAPTURE_OVERVIEW?frame+1==frames:
            frame>=60&&captureTargetId&&captureTargetReady&&(!hasFocus||captureTargetId==focusId));
        if(captureNow){
            captured=OpenGLRenderer_SavePPM(capture,1280,800);ok=captured&&ok;
            const EcosystemEntity* subject=findEntity(&w,captureTargetId?captureTargetId:focusId);
            if(subject)printf("capture_entity=%u species=%u recipe=%u action=%s stage=%u\n",subject->id,subject->body.species,w.species[subject->speciesIndex].recipeId,CreatureAction_Name(subject->mind.action),subject->physiology.stage);
        }
        renderer.endFrame(&renderer);finalGpu=OpenGLRenderer_GetMeshPerformanceStats(&renderer);
        if(finalGpu.gpuTimingGeneration!=gpuTimingGeneration){gpuTimingGeneration=(unsigned)finalGpu.gpuTimingGeneration;gpuSum+=finalGpu.gpuRenderMs;++gpuSamples;}
        OpenGLDemoWindow_Swap(window);if(times)times[frame]=(OpenGLDemoWindow_Time()-start)*1000;++frame;if(captured&&captureState!=CAPTURE_OVERVIEW)break;if(!frames)OpenGLDemoWindow_LimitFPS(start,60);
    }
    if(captureState!=CAPTURE_OVERVIEW&&!captureTargetId&&!captureWarned){fprintf(stderr,"[ADVERTENCIA] --capture-state no encontró una entidad con la heurística solicitada.\n");captureWarned=true;}
    if(capture&&!captured){fprintf(stderr,"No se capturó el estado solicitado.\n");ok=false;}
    double mouthMax=0,mouthP95=0;
    if(mouthWindowCount){double *sorted=malloc(mouthWindowCount*sizeof(*sorted));if(sorted){for(unsigned i=0;i<mouthWindowCount;++i)sorted[i]=mouthWindow[i];qsort(sorted,mouthWindowCount,sizeof(*sorted),compareDouble);mouthMax=sorted[mouthWindowCount-1];mouthP95=sorted[(mouthWindowCount-1)*95/100];free(sorted);}}
    if(profile&&times&&frame) {
        double sum=0,sim=0;for(unsigned i=0;i<frame;++i){sum+=times[i];sim+=simulationTimes[i];}qsort(times,frame,sizeof(*times),compareDouble);struct rusage usage;getrusage(RUSAGE_SELF,&usage);
         FILE *f=fopen(profile,"w");
         if(!f)ok=false;
         else {
             fprintf(f,"{\"frames\":%u,\"width\":1280,\"height\":800,\"frame_mean_ms\":%.6f,\"frame_p50_ms\":%.6f,\"frame_p95_ms\":%.6f,\"frame_p99_ms\":%.6f,\"simulation_mean_ms\":%.6f,\"gpu_mean_ms\":%.6f,\"gpu_samples\":%u,\"peak_rss_kib\":%ld,\"meshCacheMisses\":%llu,\"staticGeometryUploads\":%llu,\"staticSurfaceUploads\":%llu,\"indexUploads\":%llu,\"furRootUploads\":%llu,\"skinUploads\":%llu",
                 frame,sum/frame,times[frame/2],times[(frame-1)*95/100],times[(frame-1)*99/100],sim/frame,
                 gpuSamples?gpuSum/gpuSamples:0,gpuSamples,usage.ru_maxrss,(unsigned long long)finalGpu.meshCacheMisses,
                 (unsigned long long)finalGpu.staticGeometryUploads,(unsigned long long)finalGpu.staticSurfaceUploads,
                 (unsigned long long)finalGpu.indexUploads,(unsigned long long)finalGpu.furRootUploads,
                 (unsigned long long)finalGpu.skinUploads);
             fprintf(f,",\"initial_population\":%u,\"final_population\":%u,\"drawn_mean\":%.3f,\"drawn_min\":%u,\"drawn_max\":%u,\"adapter_mean_ms\":%.6f,\"render_submit_mean_ms\":%.6f,\"geometry_warmup_seconds\":%.6f",initialPopulation,w.count,drawnSum/frame,drawnMin,drawnMax,adapterSum/frame,renderSum/frame,geometryWarmSeconds);
             if(validateSkin)fprintf(f,",\"skin_position_error_max\":%.9f,\"skin_normal_error_max\":%.9f",skinPositionError,skinNormalError);
             fprintf(f,",\"stance_foot_error_max_m\":%.9f",stanceFootErrorMax);
             fprintf(f,",\"foot_error_max_m\":%.9f,\"anatomy_cache_builds\":%zu,\"anatomy_cache_hits\":%zu",footErrorMax,EcosystemVisual_BuildCount(&visual),EcosystemVisual_CacheHitCount(&visual));
             if(validateMouthFrames)fprintf(f,",\"mouth_err_max_m\":%.9f,\"mouth_err_p95_m\":%.9f,\"mouth_validation_frames\":%u",
                 mouthMax,mouthP95,mouthWindowCount);
             fprintf(f,"}\n");
             if(fclose(f))ok=false;
             fprintf(stdout,"process_cpu_mean_ms=%.6f\n",1000.*(clock()-processStart)/CLOCKS_PER_SEC/frame);
         }
    }
    fprintf(stdout,"meshCacheMisses=%llu staticGeometryUploads=%llu staticSurfaceUploads=%llu indexUploads=%llu furRootUploads=%llu skinUploads=%llu\n",(unsigned long long)finalGpu.meshCacheMisses,(unsigned long long)finalGpu.staticGeometryUploads,(unsigned long long)finalGpu.staticSurfaceUploads,(unsigned long long)finalGpu.indexUploads,(unsigned long long)finalGpu.furRootUploads,(unsigned long long)finalGpu.skinUploads);
    if(validateMouthFrames)printf("mouth_validation_method=Rig.oralPoint_en_pose_vs_boca_ecologica\nmouth_err_max_m=%.9f mouth_err_p95_m=%.9f mouth_validation_frames=%u\n",mouthMax,mouthP95,mouthWindowCount);
    for(unsigned slot=0;slot<ECOSYSTEM_VISUAL_MAX_SLOTS;++slot)Mesh_Free(&cpuBodies[slot]);
    ok=OpenGLRenderer_CheckErrors()&&ok;free(times);free(simulationTimes);free(mouthWindow);for(unsigned i=0;i<4;++i)Mesh_Free(&meshes[i]);EcosystemVisual_Shutdown(&visual);OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);EcosystemWorld_Free(&w);return ok?0:1;
}
