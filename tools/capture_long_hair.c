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
typedef struct JawReference {
    SDFField field;Vector3 restPivot,posedPivot,center,x,y,z,localPivot;
    Quaternion inverseDelta;float scale;
} JawReference;
static float JawDistance(const void* context,Vector3 p){
    const JawReference* j=context;if(j->scale<=.001f)return 1;
    Vector3 ref=Vec3_Add(j->restPivot,Quat_RotateVector(j->inverseDelta,Vec3_Sub(p,j->posedPivot))),rel=Vec3_Sub(ref,j->center);
    Vector3 local=Vec3_Create(Vec3_Dot(rel,j->x),Vec3_Dot(rel,j->y),Vec3_Dot(rel,j->z));
    local=Vec3_Add(j->localPivot,Vec3_Scale(Vec3_Sub(local,j->localPivot),1/j->scale));
    return j->field.evaluateDistance(j->field.context,local)*j->scale;
}
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
    if(!MonsterVisual_UpdateLongHair(&temp,m))return 1;
    double rootMs=Now()-begin;LongHairSet_Free(&temp);
    size_t visible=0;float maxRootError=0;
    for(size_t i=0;i<v->longHair.count;++i){LongHairGuide* g=&v->longHair.guides[i];visible+=g->visible;if(g->visible){
        float d=fabsf(MonsterSDF_EvaluateDistance(&v->sdf,g->root.restPosition));
        if(g->attachmentJoint&&v->longHair.attachedDistance)d=fminf(d,fabsf(v->longHair.attachedDistance(v->longHair.attachedContext,g->root.restPosition)));
        maxRootError=fmaxf(maxRootError,d);}}
    printf("roots %zu visible %zu root_build_ms %.6f max_root_error %.6g runtime_bytes %zu gpu_bytes %zu\n",v->longHair.count,visible,rootMs,maxRootError,
        sizeof(LongHairSet)+v->longHair.count*sizeof(LongHairGuide)+(m->anatomyGraph.nodeCount+m->anatomyGraph.connectionCount+1)*sizeof(LongHairCollider)+v->longHair.attachedContextBytes,v->longHair.count*LONG_HAIR_GPU_ROWS*4*sizeof(float));
    size_t follicles=0,positiveSide=0;float follicleError=0,minSeparation=INFINITY;
    for(size_t i=0;i<v->longHair.count;++i){const LongHairGuide* g=&v->longHair.guides[i];if(!g->visible)continue;
        positiveSide+=g->points[9].x>g->points[0].x;
        const LongHairGroup* group=&v->longHair.groups[g->groupIndex];
        for(unsigned c=0;c<group->childrenPerGuide;++c){const float* f=g->follicles[c];if(f[3]<=0)continue;
            const LongHairGuide* n=&v->longHair.guides[(size_t)f[0]];
            Vector3 normal=Vec3_Normalize(Vec3_Lerp(g->posedNormal,n->posedNormal,f[1]));
            Vector3 root=Vec3_Add(Vec3_Lerp(g->points[0],n->points[0],f[1]),Vec3_Scale(normal,f[2]));
            follicleError=fmaxf(follicleError,fabsf(MonsterSDF_EvaluateDistance(&v->sdf,root)-.002f*scale));
            minSeparation=fminf(minSeparation,Vec3_Distance(root,g->points[0]));++follicles;}}
    printf("follicles %zu max_skin_offset_error %.9g min_parent_separation %.9g guides_falling_positive_x %zu/%zu\n",
        follicles,follicleError,follicles?minSeparation:0,positiveSide,visible);
    float penetration=0,envelopeError=0,stretch=0,totalStretch=0;size_t penetrating=0,samples=0;
    for(size_t i=0;i<v->longHair.count;++i){LongHairGuide* guide=&v->longHair.guides[i];if(!guide->visible)continue;
        for(unsigned p=1;p<=90;++p){float t=p/90.f;Vector3 point=LongHairGuide_Evaluate(guide,t);float d=MonsterSDF_EvaluateDistance(&v->sdf,point);
            penetration=fmaxf(penetration,-d);envelopeError=fmaxf(envelopeError,LongHairGroup_Envelope(&v->longHair.groups[guide->groupIndex],scale,t)-d);
            penetrating+=d<-.002f*scale;++samples;}
        float length=0;
        for(unsigned p=1;p<LONG_HAIR_POINTS;++p){float segment=Vec3_Distance(guide->points[p],guide->points[p-1]);length+=segment;stretch=fmaxf(stretch,fabsf(segment/(guide->length/9)-1));}
        totalStretch=fmaxf(totalStretch,fabsf(length/guide->length-1));}
    printf("collision_rest samples %zu penetrating %zu max_penetration %.9g envelope_error %.9g max_segment_strain %.9g\n",samples,penetrating,penetration,envelopeError,stretch);
    printf("curve_length max_relative_error %.9g\n",totalStretch);
    char rootsPath[1024];snprintf(rootsPath,sizeof(rootsPath),"%s/roots.json",argv[2]);FILE* roots=fopen(rootsPath,"w");if(!roots)return 1;
    fprintf(roots,"[\n");for(size_t i=0;i<v->longHair.count;++i){const LongHairRoot* r=&v->longHair.guides[i].root;
        Vector3 tip=v->longHair.guides[i].points[9];
        fprintf(roots,"%s{\"id\":%u,\"group\":%u,\"module\":%u,\"from\":%u,\"to\":%u,\"parameter\":%.9g,\"visible\":%s,\"position\":[%.9g,%.9g,%.9g],\"tip\":[%.9g,%.9g,%.9g]}",i?",\n":"",r->stableRootId,r->groupId,r->moduleId,r->fromId,r->toId,r->rootParameter,v->longHair.guides[i].visible?"true":"false",r->restPosition.x,r->restPosition.y,r->restPosition.z,tip.x,tip.y,tip.z);}fprintf(roots,"\n]\n");fclose(roots);
    const char* msaa=getenv("LONG_HAIR_MSAA");
    OpenGLDemoWindow* window=OpenGLDemoWindow_CreateMSAA("Guías anatómicas",900,900,msaa?atoi(msaa):0);if(!window)return 1;
    ICamera camera={.fov=40,.aspectRatio=1,.nearPlane=.01f,.farPlane=100,.up={0,1,0}};Renderer3D renderer=OpenGLRenderer_Create(&camera);
    if(!OpenGLRenderer_SurfaceReady(&renderer))return 1;
    AABB3D hairBounds=AABB_Empty();
    for(size_t i=0;i<v->longHair.count;++i)if(v->longHair.guides[i].visible)
        for(unsigned j=0;j<LONG_HAIR_POINTS;++j)AABB_ExpandPoint(&hairBounds,v->longHair.guides[i].points[j]);
    if(!visible)hairBounds=v->sdf.bounds;
    Vector3 target=Vec3_Scale(Vec3_Add(hairBounds.start,hairBounds.end),.5f);
    float height=fmaxf(3.5f*scale,Vec3_Length(AABB_Size(hairBounds))*1.25f);
    /* Cámara intercambiable para comparar implementaciones sin autoencuadres distintos. */
    const char* cameraPath=getenv("LONG_HAIR_CAMERA");
    if(cameraPath){FILE* f=fopen(cameraPath,"r");if(f){if(fscanf(f,"%f %f %f %f",&target.x,&target.y,&target.z,&height)!=4)return 1;fclose(f);}
        else {f=fopen(cameraPath,"w");if(!f)return 1;fprintf(f,"%.9g %.9g %.9g %.9g\n",target.x,target.y,target.z,height);fclose(f);}}
    const Vector3 directions[]={{1,0,0},{0,0,1},{.8f,.15f,-.7f}};const char* names[]={"side","front","rear-three-quarter"};bool ok=true;
    for(unsigned view=0;view<3;++view){camera.target=target;camera.position=Vec3_Add(target,Vec3_Scale(directions[view],20));
        for(int quality=0;quality<=4;++quality){OpenGLRenderer_SetLongHairQuality(&renderer,quality);Frame(&renderer,&camera,v,height);
            char path[1024];snprintf(path,sizeof(path),"%s/%s-q%d.ppm",argv[2],names[view],quality);ok=OpenGLRenderer_SavePPM(path,900,900)&&ok;renderer.endFrame(&renderer);}}
    AABB3D whole=v->sdf.bounds;AABB_ExpandPoint(&whole,hairBounds.start);AABB_ExpandPoint(&whole,hairBounds.end);
    camera.target=Vec3_Scale(Vec3_Add(whole.start,whole.end),.5f);camera.position=Vec3_Add(camera.target,Vec3_Create(16,7,20));
    for(int quality=0;quality<=3;quality+=3){OpenGLRenderer_SetLongHairQuality(&renderer,quality);Frame(&renderer,&camera,v,Vec3_Length(AABB_Size(whole))*1.15f);
        char path[1024];snprintf(path,sizeof(path),"%s/whole-q%d.ppm",argv[2],quality);ok=OpenGLRenderer_SavePPM(path,900,900)&&ok;renderer.endFrame(&renderer);}
    camera.target=target;camera.position=Vec3_Add(target,Vec3_Create(20,2,-12));
    for(int quality=0;quality<=3;quality+=3){OpenGLRenderer_SetLongHairQuality(&renderer,quality);double elapsed=0,gpu=0;unsigned timed=0;
        for(unsigned frame=0;frame<45;++frame){double start=Now();Frame(&renderer,&camera,v,height);OpenGLRenderer_Finish();renderer.endFrame(&renderer);double ms=Now()-start;
            LongHairRenderStats stats=OpenGLRenderer_GetLongHairStats(&renderer);
            if(frame>=15){elapsed+=ms;if(quality&&stats.timingValid){gpu+=stats.gpuMs;++timed;}}}
        printf("draw quality %d frame_ms %.6f fps %.3f hair_gpu_ms %.6f gpu_samples %u\n",quality,elapsed/30,30000/elapsed,timed?gpu/timed:0,timed);}
    OpenGLRenderer_SetLongHairQuality(&renderer,3);
    LongHairRenderStats stats=OpenGLRenderer_GetLongHairStats(&renderer);
    size_t childSamples=0;float childPenetration=0;
    ok=OpenGLRenderer_ValidateLongHairCollision(&renderer,&v->longHair,MonsterSDF_EvaluateDistanceWrapper,&v->sdf,&childPenetration,&childSamples)&&ok;
    printf("collision_children samples %zu max_sdf_penetration %.9g\n",childSamples,childPenetration);
    ok=ok&&penetration<.003f*scale&&childPenetration<.003f*scale;
    printf("render guides %zu strands %zu segments %u ready %d\n",stats.guides,stats.strands,stats.segments,stats.ready);
    uint64_t generation=v->rebuildGeneration,hairGeneration=v->longHair.generation;
    size_t jawRoots=0;for(size_t i=0;i<v->longHair.count;++i)jawRoots+=v->longHair.guides[i].attachmentJoint!=0;
    printf("jaw_attached_roots %zu\n",jawRoots);
    AnimatedVisual animation={0};bool animated=AnimatedVisual_Bind(&animation,v,m);
    double poseMs=0,animationMs=0;
    for(unsigned frame=0;frame<300;++frame){begin=Now();if(m->animation)MonsterAnimation_Update(m,.016f);animationMs+=Now()-begin;
        begin=Now();ok=MonsterVisual_UpdateLongHair(&v->longHair,m)&&ok;poseMs+=Now()-begin;}
    printf("guide_pose_ms %.6f animation_ms %.6f mesh_generation_unchanged %d hair_generation_unchanged %d\n",poseMs/300,animationMs/300,generation==v->rebuildGeneration,hairGeneration==v->longHair.generation);
    float maxPosePenetration=0,maxJawPenetration=0;size_t jawSamples=0;
    Vector3 beforePose=v->longHair.guides[0].points[0];
    if(animated){m->animation->animator.desiredVelocity=Vec3_Create(0,0,.6f*scale);
        for(unsigned frame=0;frame<3;++frame){m->animation->animator.lookDirection=Vec3_Normalize(Vec3_Create(.35f*sinf(frame*.7f),-.08f,1));
        for(unsigned j=0;j<12;++j)MonsterAnimation_Update(m,.016f);
        if(jawRoots&&m->animation->rig.jawJoint>=0){int joint=m->animation->rig.jawJoint;
            Vector3 axis=m->animation->rig.skeleton.joints[joint].constraint.hingeAxis;
            m->animation->pose.joints[joint].localRotation=Quat_FromAxisAngle(axis,m->animation->rig.maxJawAngle*frame*.2f);
            ok=SkeletonPose_UpdateWorldTransforms(&m->animation->pose,&m->animation->rig.skeleton)&&ok;}
        ok=AnimatedVisual_Deform(&animation,v,m)&&ok;
        size_t covered=0;for(size_t i=0;i<v->longHair.count;++i)covered+=v->longHair.guides[i].rootValid&&!v->longHair.guides[i].visible;
        printf("pose %u covered_roots %zu\n",frame,covered);
        if(jawRoots){int joint=m->animation->rig.jawJoint;SkeletonPose rest;SkeletonPose_Init(&rest,&m->animation->rig.skeleton);
            MonsterSDFJawField fieldContext;Vector3 rotation=m->mouths[0].rotation;
            Quaternion delta=Quat_Multiply(m->animation->pose.joints[joint].worldRotation,Quat_Inverse(rest.joints[joint].worldRotation));
            JawReference reference={.field=MonsterSDF_GetJawField(&v->sdf,0,&fieldContext),.restPivot=rest.joints[joint].worldPosition,
                .posedPivot=m->animation->pose.joints[joint].worldPosition,.inverseDelta=Quat_Inverse(delta),.center=v->sdf.mouths[0].center,
                .x=Transform3D_RotateVector(rotation,Vec3_Create(1,0,0)),.y=Transform3D_RotateVector(rotation,Vec3_Create(0,1,0)),
                .z=Transform3D_RotateVector(rotation,Vec3_Create(0,0,1)),.localPivot=m->mouths[0].jawPivot,.scale=m->phenotype.development.cephalic};
            float depth=0;size_t samples=0;ok=OpenGLRenderer_ValidateLongHairCollision(&renderer,&v->longHair,JawDistance,&reference,&depth,&samples)&&ok;
            maxJawPenetration=fmaxf(maxJawPenetration,depth);jawSamples+=samples;}
        for(size_t i=0;i<v->longHair.count;++i)if(v->longHair.guides[i].visible)maxPosePenetration=fmaxf(maxPosePenetration,v->longHair.guides[i].contactPenetration);
        Frame(&renderer,&camera,v,height);char path[1024];snprintf(path,sizeof(path),"%s/pose-%u.ppm",argv[2],frame);ok=OpenGLRenderer_SavePPM(path,900,900)&&ok;renderer.endFrame(&renderer);}}
    printf("collision_pose max_proxy_penetration %.9g\n",maxPosePenetration);
    printf("collision_jaw samples %zu max_posed_jaw_penetration %.9g\n",jawSamples,maxJawPenetration);
    ok=ok&&maxPosePenetration<.005f*scale;
    ok=ok&&maxJawPenetration<.003f*scale;
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
