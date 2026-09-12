/** @file demo_lizard_animation.c
 * @brief Etapas aisladas IK, mandíbula y marcha sobre una malla SDF persistente.
 */
#include "Monster.h"
#include "MonsterAnimation.h"
#include "AnimatedVisual.h"
#include "OpenGLRenderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void DebugRig(const MonsterAnimation* a) {
    for(size_t i=0;i<a->pose.jointCount;++i) {
        int parent=a->rig.skeleton.joints[i].parentIndex;
        if(parent>=0)OpenGLRenderer_DebugLine(a->pose.joints[parent].worldPosition,a->pose.joints[i].worldPosition,Color_FromRGB(255,180,40));
    }
    for(size_t i=0;i<a->rig.limbCount;++i) {
        Vector3 p=a->animator.locomotion.feet[i].targetPosition;
        Color c=a->animator.locomotion.feet[i].phase==FOOT_STANCE?Color_FromRGB(40,220,255):Color_FromRGB(255,90,70);
        OpenGLRenderer_DebugLine(Vec3_Add(p,Vec3_Create(-.1f,0,0)),Vec3_Add(p,Vec3_Create(.1f,0,0)),c);
        OpenGLRenderer_DebugLine(Vec3_Add(p,Vec3_Create(0,0,-.1f)),Vec3_Add(p,Vec3_Create(0,0,.1f)),c);
    }
}
static void Camera(ICamera* c,int view,Vector3 body,const char* mode) {
    c->up=Vec3_Create(0,1,0); c->target=Vec3_Add(body,Vec3_Create(0,0,-4.5f));
    Vector3 offset=Vec3_Create(12,8,12);
    if(view==1)offset=Vec3_Create(16,2,0);
    if(view==2) { offset=Vec3_Create(0,22,.01f); c->up=Vec3_Create(0,0,-1); }
    if(view==3)offset=Vec3_Create(0,3,18);
    if(!strcmp(mode,"jaw")) { c->target=Vec3_Add(body,Vec3_Create(0,.4f,.3f)); offset=Vec3_Create(3,1.4f,3); }
    c->position=Vec3_Add(c->target,offset);
}
int main(int argc,char** argv) {
    const char* mode=strstr(argv[0],"walk")?"walk":strstr(argv[0],"jaw")?"jaw":"ik";
    const char* capture=NULL; const char* prefix=NULL; int frames=0,view=0,limb=0; bool headless=false,debug=false;
    for(int i=1;i<argc;++i) {
        if(!strncmp(argv[i],"--mode=",7))mode=argv[i]+7;
        else if(!strncmp(argv[i],"--frames=",9))frames=atoi(argv[i]+9);
        else if(!strncmp(argv[i],"--view=",7))view=atoi(argv[i]+7);
        else if(!strncmp(argv[i],"--limb=",7))limb=atoi(argv[i]+7);
        else if(!strncmp(argv[i],"--capture=",10))capture=argv[i]+10;
        else if(!strncmp(argv[i],"--capture-prefix=",17))prefix=argv[i]+17;
        else if(!strcmp(argv[i],"--headless"))headless=true;
        else if(!strcmp(argv[i],"--debug"))debug=true;
    }
    if(limb<0 || limb>=4 || frames<0)return 2;
    if(headless && !frames)frames=600;
    OpenGLDemoWindow* window=NULL; Renderer3D renderer={0};
    ICamera camera={.fov=45,.nearPlane=.05f,.farPlane=200};
    if(!headless) {
        window=OpenGLDemoWindow_Create("Monster Engine | IK / mandíbula / marcha | D: rig | 1..4: vistas | espacio: pausa",1100,800);
        if(!window) { fprintf(stderr,"No se pudo crear ventana OpenGL\n"); return 1; }
        renderer=OpenGLRenderer_Create(&camera);
    }
    Monster monster=Monster_Create(); LizardPhenotype phenotype=LizardPreset_Adult();
    MonsterVisual* visual=calloc(1,sizeof(*visual)); AnimatedVisual binding={0};
    int status=1;
    if(!visual || !Lizard_BuildMonster(&monster,&phenotype))goto cleanup;
    SDFMesherConfig cfg=SDFMesher_DefaultConfig(); cfg.voxelSize=.075f; cfg.maxCells=1800000;
    *visual=MonsterVisual_Create(cfg);
    if(!MonsterVisual_RebuildNow(visual,&monster,MonsterSDF_DefaultConfig()) || !AnimatedVisual_Bind(&binding,visual,&monster))goto cleanup;
    MonsterAnimation* a=monster.animation;
    uint64_t fingerprint=AnatomyGraph_Fingerprint(&monster.anatomyGraph),generation=visual->rebuildGeneration;
    size_t vertices=visual->mesh.vertexCount,indices=visual->mesh.indexCount;
    MeshVertex* vertexPointer=visual->mesh.vertices; MeshIndex* indexPointer=visual->mesh.indices;
    double totalPose=0,totalDeform=0,maxPose=0,maxDeform=0;
    float maxError=0,maxLengthError=0,stanceSlip=0; unsigned swingFrames=0,stanceFrames=0;
    bool paused=false; float animTime=0; int frame=0; double last=OpenGLDemoWindow_Time();
    printf("modo=%s vértices=%zu índices=%zu generación=%llu\n",mode,vertices,indices,(unsigned long long)generation); fflush(stdout);
    while(!frames || frame<frames) {
        if(window) {
            OpenGLDemoInput input=OpenGLDemoWindow_Poll(window);
            if(input.quit)break;
            if(input.togglePause)paused=!paused;
            if(input.toggleDebug)debug=!debug;
            if(input.view>=0)view=input.view;
        }
        double now=OpenGLDemoWindow_Time();
        float dt=frames?1.0f/60.0f:fminf(.05f,(float)(now-last)); last=now;
        if(paused)dt=0;
        animTime+=dt; float t=animTime;
        double start=OpenGLDemoWindow_Time();
        a->animator.mouthOpen=.2f+.2f*sinf(t*2);
        a->animator.lookDirection=Vec3_Create(.08f*sinf(t*.6f),.03f,1);
        if(!strcmp(mode,"walk")) {
            a->animator.desiredVelocity=Vec3_Create(0,0,.38f);
            if(!MonsterAnimation_Update(&monster,dt))goto cleanup;
        } else {
            SkeletonPose_ResetToRest(&a->pose,&a->rig.skeleton);
            a->pose.joints[0].localPosition=Vec3_Add(a->pose.joints[0].localPosition,a->animator.locomotion.bodyPosition);
            if(a->rig.jawJoint>=0)a->pose.joints[a->rig.jawJoint].localRotation=Quat_FromAxisAngle(Vec3_Create(1,0,0),a->animator.mouthOpen*a->rig.maxJawAngle);
            SkeletonPose_UpdateWorldTransforms(&a->pose,&a->rig.skeleton);
            if(!strcmp(mode,"ik")) {
                IKChain chain=a->rig.limbs[limb].chain;
                Vector3 rest=a->pose.joints[chain.endEffectorJoint].worldPosition;
                chain.targetPosition=Vec3_Add(rest,Vec3_Create(.08f*sinf(t),.10f+.09f*sinf(t*1.3f),.15f*cosf(t)));
                a->animator.locomotion.feet[limb].targetPosition=chain.targetPosition;
                a->animator.limbResults[limb]=IK_SolveFABRIK(&a->rig.skeleton,&a->pose,&chain);
            }
        }
        double poseMs=(OpenGLDemoWindow_Time()-start)*1000; totalPose+=poseMs; maxPose=fmax(maxPose,poseMs);
        start=OpenGLDemoWindow_Time();
        if(!AnimatedVisual_Deform(&binding,visual,&monster))goto cleanup;
        double deformMs=(OpenGLDemoWindow_Time()-start)*1000; totalDeform+=deformMs; maxDeform=fmax(maxDeform,deformMs);
        for(size_t i=0;i<a->rig.limbCount;++i) {
            const LimbRig* l=&a->rig.limbs[i]; const FootState* foot=&a->animator.locomotion.feet[i];
            if(a->animator.limbResults[i].valid)maxError=fmaxf(maxError,a->animator.limbResults[i].error);
            if(foot->phase==FOOT_SWING)++swingFrames;
            else {
                ++stanceFrames;
                if(!strcmp(mode,"walk"))stanceSlip=fmaxf(stanceSlip,Vec3_Distance(a->pose.joints[l->endEffectorJoint].worldPosition,foot->plantedPosition));
            }
            for(size_t k=1;k<l->chain.jointCount;++k) {
                int j=l->chain.jointIndices[k],p=l->chain.jointIndices[k-1];
                float error=fabsf(Vec3_Distance(a->pose.joints[j].worldPosition,a->pose.joints[p].worldPosition)-Vec3_Length(a->rig.skeleton.joints[j].restPosition));
                maxLengthError=fmaxf(maxLengthError,error);
            }
        }
        if(frame%60==0)for(size_t v=0;v<visual->mesh.vertexCount;++v) {
            Vector3 p=visual->mesh.vertices[v].position,n=visual->mesh.vertices[v].normal;
            if(!isfinite(p.x)||!isfinite(p.y)||!isfinite(p.z)||!isfinite(n.x)||!isfinite(n.y)||!isfinite(n.z))goto cleanup;
        }
        if(AnatomyGraph_Fingerprint(&monster.anatomyGraph)!=fingerprint || visual->rebuildGeneration!=generation ||
            visual->mesh.vertices!=vertexPointer || visual->mesh.indices!=indexPointer || visual->mesh.vertexCount!=vertices || visual->mesh.indexCount!=indices)goto cleanup;
        if(window) {
            Camera(&camera,view,a->animator.locomotion.bodyPosition,mode);
            renderer.beginFrame(&renderer); OpenGLRenderer_SetupCamera(&camera,1100,800);
            for(int line=-25;line<=25;++line) {
                OpenGLRenderer_DebugLine(Vec3_Create(line,0,-25),Vec3_Create(line,0,25),Color_FromRGB(80,90,95));
                OpenGLRenderer_DebugLine(Vec3_Create(-25,0,line),Vec3_Create(25,0,line),Color_FromRGB(80,90,95));
            }
            MonsterVisual_Render(visual,&renderer); if(debug)DebugRig(a);
            renderer.endFrame(&renderer);
            if(capture && (frames==0 || frame==frames-1)) {
                if(!OpenGLRenderer_SavePPM(capture,1100,800))goto cleanup;
                if(!frames)break;
            }
            if(prefix && ((frame+1)%30==0 || frame==0)) {
                for(int v=0;v<4;++v) {
                    Camera(&camera,v,a->animator.locomotion.bodyPosition,mode);
                    renderer.beginFrame(&renderer); OpenGLRenderer_SetupCamera(&camera,1100,800);
                    for(int line=-20;line<=20;++line) {
                        OpenGLRenderer_DebugLine(Vec3_Create(line,0,-20),Vec3_Create(line,0,20),Color_FromRGB(80,90,95));
                        OpenGLRenderer_DebugLine(Vec3_Create(-20,0,line),Vec3_Create(20,0,line),Color_FromRGB(80,90,95));
                    }
                    MonsterVisual_Render(visual,&renderer); if(debug)DebugRig(a);
                    renderer.endFrame(&renderer);
                    char path[1024]; snprintf(path,sizeof(path),"%s-%04d-v%d.ppm",prefix,frame+1,v);
                    if(!OpenGLRenderer_SavePPM(path,1100,800))goto cleanup;
                }
            }
            OpenGLDemoWindow_Swap(window);
        }
        ++frame;
    }
    printf("frames=%d pose_media_ms=%.3f pose_max_ms=%.3f deform_media_ms=%.3f deform_max_ms=%.3f\n",frame,totalPose/fmax(1,frame),maxPose,totalDeform/fmax(1,frame),maxDeform);
    printf("error_IK_max=%.6f longitud_error_max=%.6f apoyo_error_max=%.6f stance=%u swing=%u avance=%.3f\n",maxError,maxLengthError,stanceSlip,stanceFrames,swingFrames,a->animator.locomotion.bodyPosition.z);
    printf("rebuilds_animación=%llu huella_inmutable=%d topología_inmutable=1\n",(unsigned long long)(visual->rebuildGeneration-generation),fingerprint==AnatomyGraph_Fingerprint(&monster.anatomyGraph));
    status=maxLengthError>.001f || !isfinite(maxError) || (!strcmp(mode,"walk") && stanceSlip>.1f) ? 1:0;
cleanup:
    AnimatedVisual_Free(&binding); if(visual) { MonsterVisual_Free(visual); free(visual); } Monster_Free(&monster);
    if(window) { OpenGLRenderer_Destroy(&renderer); OpenGLDemoWindow_Free(window); }
    return status;
}
