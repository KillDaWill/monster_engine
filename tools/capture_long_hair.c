/** @file capture_long_hair.c
 * @brief Evidencia de guías e hijos reales, pose, remallado y costes separados. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "MonsterVisual.h"
#include "AnimatedVisual.h"
#include "MonsterAnimation.h"
#include "MonsterAger.h"
#include "OpenGLRenderer.h"
#include <stdlib.h>
#include <math.h>
#include <time.h>
static double Now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.+t.tv_nsec*1e-6;}
static void Frame(Renderer3D* r,ICamera* c,MonsterVisual* v,float height){r->beginFrame(r);OpenGLRenderer_SetupCameraOrtho(c,height,900,900);MonsterVisual_Render(v,r);}
int main(int argc,char** argv){
    if(argc<3||argc>4)return 2;
    char error[256];int stage=argc==4?atoi(argv[3]):3;
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));MonsterVisual* v=malloc(sizeof(*v));
    if(!doc||!m||!v||stage<0||stage>3||!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error))){fprintf(stderr,"receta: %s\n",error);return 1;}
    *m=Monster_Create();if(!Creature_BuildMonster(m,&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,stage)))return 1;
    Monster_SetHeadOpenFactor(m,0);float scale=m->phenotype.axial.totalScale;
    SDFMesherConfig config=SDFMesher_DefaultConfig();config.voxelSize=.045f*scale;config.maxCells=3000000;config.maxResolution=512;
    *v=MonsterVisual_Create(config);if(!MonsterVisual_RebuildNow(v,m,MonsterSDF_DefaultConfig())||!v->longHair.count)return 1;
    double begin=Now();LongHairSet temp={0};
    if(!LongHairSet_Build(&temp,m->surface.longHair,m->surface.longHairCount,&m->anatomyGraph,scale,MonsterSDF_EvaluateDistanceWrapper,&v->sdf))return 1;
    double rootMs=Now()-begin;LongHairSet_Free(&temp);
    size_t visible=0;float maxRootError=0;
    for(size_t i=0;i<v->longHair.count;++i){LongHairGuide* g=&v->longHair.guides[i];visible+=g->visible;if(g->visible)maxRootError=fmaxf(maxRootError,fabsf(MonsterSDF_EvaluateDistance(&v->sdf,g->root.restPosition)));}
    printf("roots %zu visible %zu root_build_ms %.6f max_root_error %.6g runtime_bytes %zu gpu_bytes %zu\n",v->longHair.count,visible,rootMs,maxRootError,
        sizeof(LongHairSet)+v->longHair.count*sizeof(LongHairGuide),v->longHair.count*LONG_HAIR_GPU_ROWS*4*sizeof(float));
    char rootsPath[1024];snprintf(rootsPath,sizeof(rootsPath),"%s/roots.json",argv[2]);FILE* roots=fopen(rootsPath,"w");if(!roots)return 1;
    fprintf(roots,"[\n");for(size_t i=0;i<v->longHair.count;++i){const LongHairRoot* r=&v->longHair.guides[i].root;
        fprintf(roots,"%s{\"id\":%u,\"group\":%u,\"module\":%u,\"from\":%u,\"to\":%u,\"parameter\":%.9g,\"visible\":%s}",i?",\n":"",r->stableRootId,r->groupId,r->moduleId,r->fromId,r->toId,r->rootParameter,v->longHair.guides[i].visible?"true":"false");}fprintf(roots,"\n]\n");fclose(roots);
    OpenGLDemoWindow* window=OpenGLDemoWindow_Create("Guías anatómicas",900,900);if(!window)return 1;
    ICamera camera={.fov=40,.aspectRatio=1,.nearPlane=.01f,.farPlane=100,.up={0,1,0}};Renderer3D renderer=OpenGLRenderer_Create(&camera);
    if(!OpenGLRenderer_SurfaceReady(&renderer))return 1;
    AABB3D hairBounds=AABB_Empty();
    for(size_t i=0;i<v->longHair.count;++i)if(v->longHair.guides[i].visible)
        for(unsigned j=0;j<LONG_HAIR_POINTS;++j)AABB_ExpandPoint(&hairBounds,v->longHair.guides[i].points[j]);
    if(!visible)hairBounds=v->sdf.bounds;
    Vector3 target=Vec3_Scale(Vec3_Add(hairBounds.start,hairBounds.end),.5f);
    float height=fmaxf(3.5f*scale,Vec3_Length(AABB_Size(hairBounds))*1.25f);
    const Vector3 directions[]={{1,0,0},{0,0,1},{.8f,.15f,-.7f}};const char* names[]={"side","front","rear-three-quarter"};bool ok=true;
    for(unsigned view=0;view<3;++view){camera.target=target;camera.position=Vec3_Add(target,Vec3_Scale(directions[view],20));
        for(int quality=0;quality<=4;++quality){OpenGLRenderer_SetLongHairQuality(&renderer,quality);Frame(&renderer,&camera,v,height);
            char path[1024];snprintf(path,sizeof(path),"%s/%s-q%d.ppm",argv[2],names[view],quality);ok=OpenGLRenderer_SavePPM(path,900,900)&&ok;renderer.endFrame(&renderer);}}
    camera.target=target;camera.position=Vec3_Add(target,Vec3_Create(20,2,-12));
    for(int quality=0;quality<=3;quality+=3){OpenGLRenderer_SetLongHairQuality(&renderer,quality);double elapsed=0,gpu=0;unsigned timed=0;
        for(unsigned frame=0;frame<45;++frame){double start=Now();Frame(&renderer,&camera,v,height);OpenGLRenderer_Finish();renderer.endFrame(&renderer);double ms=Now()-start;
            LongHairRenderStats stats=OpenGLRenderer_GetLongHairStats(&renderer);
            if(frame>=15){elapsed+=ms;if(quality&&stats.timingValid){gpu+=stats.gpuMs;++timed;}}}
        printf("draw quality %d frame_ms %.6f fps %.3f hair_gpu_ms %.6f gpu_samples %u\n",quality,elapsed/30,30000/elapsed,timed?gpu/timed:0,timed);}
    OpenGLRenderer_SetLongHairQuality(&renderer,3);
    LongHairRenderStats stats=OpenGLRenderer_GetLongHairStats(&renderer);
    printf("render guides %zu strands %zu segments %u ready %d\n",stats.guides,stats.strands,stats.segments,stats.ready);
    uint64_t generation=v->rebuildGeneration,hairGeneration=v->longHair.generation;
    AnimatedVisual animation={0};bool animated=AnimatedVisual_Bind(&animation,v,m);
    double poseMs=0,animationMs=0;
    for(unsigned frame=0;frame<300;++frame){begin=Now();if(m->animation)MonsterAnimation_Update(m,.016f);animationMs+=Now()-begin;
        begin=Now();ok=MonsterVisual_UpdateLongHair(&v->longHair,m)&&ok;poseMs+=Now()-begin;}
    printf("guide_pose_ms %.6f animation_ms %.6f mesh_generation_unchanged %d hair_generation_unchanged %d\n",poseMs/300,animationMs/300,generation==v->rebuildGeneration,hairGeneration==v->longHair.generation);
    Vector3 beforePose=v->longHair.guides[0].points[0];
    if(animated){m->animation->animator.desiredVelocity=Vec3_Create(0,0,.6f*scale);
        for(unsigned frame=0;frame<3;++frame){m->animation->animator.lookDirection=Vec3_Normalize(Vec3_Create(.35f*sinf(frame*.7f),-.08f,1));
        for(unsigned j=0;j<12;++j)MonsterAnimation_Update(m,.016f);
        ok=AnimatedVisual_Deform(&animation,v,m)&&ok;Frame(&renderer,&camera,v,height);char path[1024];snprintf(path,sizeof(path),"%s/pose-%u.ppm",argv[2],frame);ok=OpenGLRenderer_SavePPM(path,900,900)&&ok;renderer.endFrame(&renderer);}}
    float rootMotion=Vec3_Distance(beforePose,v->longHair.guides[0].points[0]);
    printf("animation_bound %d root_motion %.9g\n",animated,rootMotion);
    ok=ok&&(!animated||rootMotion>.0001f*scale);
    float curveError=0;bool parity=OpenGLRenderer_ValidateLongHair(&renderer,&v->longHair,&curveError);
    printf("guide_gpu_parity %d max_error %.9g\n",parity,curveError);ok=ok&&parity&&curveError<.00002f*scale;
    if(stage==3){Monster* young=malloc(sizeof(*young));MonsterAger* ager=malloc(sizeof(*ager));if(!young||!ager)return 1;
        *young=Monster_Create();if(!Creature_BuildMonster(young,&doc->recipe,&doc->recipe.juvenile))return 1;
        *ager=MonsterAger_Create(young,m,0);MonsterAger_SetGeometrySnapshotEnabled(ager,false);LongHairSet morphHair={0};double morphMs=0,hairMs=0;
        for(unsigned frame=0;frame<=20;++frame){begin=Now();MonsterAger_SetPerc(ager,frame/20.f);morphMs+=Now()-begin;
            begin=Now();ok=MonsterVisual_UpdateLongHair(&morphHair,MonsterAger_GetResultConst(ager))&&ok;hairMs+=Now()-begin;}
        printf("morph_cpu_ms %.6f morph_hair_update_ms %.6f steps 21\n",morphMs/21,hairMs/21);
        printf("sizes SurfacePhenotype %zu CreaturePhenotype %zu CreatureRecipe %zu LongHairGroup %zu LongHairGuide %zu\n",sizeof(SurfacePhenotype),sizeof(CreaturePhenotype),sizeof(CreatureRecipe),sizeof(LongHairGroup),sizeof(LongHairGuide));
        LongHairSet_Free(&morphHair);MonsterAger_Free(ager);free(ager);Monster_Free(young);free(young);}
    ok=ok&&visible>0&&stats.ready&&OpenGLRenderer_CheckErrors();
    AnimatedVisual_Free(&animation);OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);MonsterVisual_Free(v);Monster_Free(m);free(v);free(m);free(doc);
    return ok?0:1;
}
