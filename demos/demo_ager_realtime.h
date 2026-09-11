/** @file demo_ager_realtime.h
 * @brief Reproducción continua del ager mediante el campo SDF compilado por fotograma.
 */
#ifndef DEMO_AGER_REALTIME_H
#define DEMO_AGER_REALTIME_H
#include "MonsterVisual.h"

static double Demo_ClockMs(void) {
    return (double)SDL_GetPerformanceCounter()*1000.0/(double)SDL_GetPerformanceFrequency();
}
static int Demo_CompareTime(const void* a,const void* b) {
    double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);
}
static void Demo_RealtimeCamera(ICamera* camera,const Monster* monster,int view,float zoom) {
    camera->target=Vec3_Create(0,.25f,-5.3f);camera->up=Vec3_Create(0,1,0);
    Vector3 offset=Vec3_Create(-11,9,14);
    if(view==1)offset=Vec3_Create(18,1,0);
    if(view==2)offset=Vec3_Create(0,2,18);
    if(view==3){offset=Vec3_Create(0,20,0);camera->up=Vec3_Create(0,0,-1);}
    if(view>=4&&view<8) {
        camera->target=Vec3_Create(0,.42f,.15f);
        if(view==5)offset=Vec3_Create(3.4f,.2f,0);
        else if(view==6)offset=Vec3_Create(0,.25f,3.6f);
        else if(view==7)offset=Vec3_Create(.01f,3.5f,.01f);
        else offset=Vec3_Create(2.8f,1.8f,2.8f);
    }
    if(view>=8&&monster->hasAnatomyGraph) {
        const AnatomyNode* n=AnatomyGraph_FindNode(&monster->anatomyGraph,view%2?ANATOMY_ID_HIND_LEFT_FOOT:ANATOMY_ID_FORE_LEFT_HAND);
        if(n)camera->target=n->center;
        float scale=monster->hasLizardPhenotype?monster->lizardPhenotype.totalScale:1;
        if(view<10){offset=Vec3_Create(0,2.6f*scale,0);camera->up=Vec3_Create(0,0,-1);}
        else offset=Vec3_Scale(Vec3_Create(1.8f,1.4f,1.2f),scale);
    }
    camera->position=Vec3_Add(camera->target,Vec3_Scale(offset,zoom));
}
static int Demo_RunRealtime(SDL_Window* window,Renderer3D* renderer,ICamera* camera,
    MonsterAger* ager,float requestedAge,const char* capturePrefix,int benchmarkFrames,
    const char* profilePrefix,bool nativeScale,bool inspectHead,bool validateGPU) {
    MonsterSDF sdf=MonsterSDF_Create();MonsterVisualEye* eyes=NULL;size_t eyeCapacity=0;
    bool running=true,automatic=requestedAge<0,ready=false;float age=ager->perc,direction=1,hold=0,zoom=1;
    int width=1024,height=768,view=inspectHead?4:0,captureView=0,captureFrames=0,error=0;
    unsigned frame=0,animatedFrames=0,titleFrames=0;double last=Demo_ClockMs(),start=last,titleTime=last;
    double* times=benchmarkFrames>0?calloc((size_t)benchmarkFrames,sizeof(double)):NULL;
    FILE* csv=NULL;char path[768];
    if(profilePrefix){snprintf(path,sizeof(path),"%s.csv",profilePrefix);csv=fopen(path,"w");if(csv)fprintf(csv,"frame,age,frame_ms,cpu_ms,gpu_ms,resolution_scale\n");}
    const char* names[]={"whole","body-lateral","body-front","body-dorsal","head-oblique","head-lateral","head-frontal","head-dorsal","manus-dorsal","pes-dorsal","manus-oblique","pes-oblique"};
    printf("[AGER GPU] Campo directo: la anatomía avanza cada fotograma; SPACE pausa, 0..4 edades, H/F1..F4 cabeza.\n");
    while(running) {
        double begin=Demo_ClockMs();float dt=(float)((begin-last)/1000.);last=begin;
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_QUIT)running=false;
            else if(event.type==SDL_WINDOWEVENT&&(event.window.event==SDL_WINDOWEVENT_RESIZED||event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED)){width=event.window.data1;height=event.window.data2;}
            else if(event.type==SDL_MOUSEWHEEL)zoom=Math_Clamp(zoom*(event.wheel.y>0?.9f:1.1f),.25f,2.5f);
            else if(event.type==SDL_KEYDOWN) {
                SDL_Keycode key=event.key.keysym.sym;
                if(key==SDLK_ESCAPE)running=false;
                if(key==SDLK_SPACE){automatic=!automatic;hold=0;}
                if(key>=SDLK_0&&key<=SDLK_4){age=(key-SDLK_0)*.25f;automatic=false;hold=0;}
                if(key==SDLK_RIGHT||key==SDLK_UP){age=Math_Clamp01(age+.05f);automatic=false;hold=0;}
                if(key==SDLK_LEFT||key==SDLK_DOWN){age=Math_Clamp01(age-.05f);automatic=false;hold=0;}
                if(key==SDLK_h)view=view?0:4;
                if(key>=SDLK_F1&&key<=SDLK_F4)view=4+(key-SDLK_F1);
            }
        }
        if(!running)break;
        if(automatic&&ready) {
            if(hold>0)hold=fmaxf(0,hold-dt);
            else {float old=age;age+=direction*.2f*dt;
                if(age>=1){age=1;direction=-1;hold=.75f;}
                if(age<=0){age=0;direction=1;hold=.75f;}
                if(age!=old)++animatedFrames;
            }
        }
        if(validateGPU){const float phases[]={0,.1f,.25f,.3f,.5f,.75f,.9f,1,.5f,0};age=phases[frame%10];automatic=false;}
        MonsterAger_SetPerc(ager,age);const Monster* monster=MonsterAger_GetResultConst(ager);
        if(!MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig())){error=1;break;}
        if(monster->eyeCount>eyeCapacity) {
            for(size_t i=0;i<eyeCapacity;++i){Mesh_Free(&eyes[i].sclera);Mesh_Free(&eyes[i].iris);Mesh_Free(&eyes[i].pupil);}
            free(eyes);eyeCapacity=monster->eyeCount;eyes=calloc(eyeCapacity,sizeof(*eyes));
            if(!eyes){error=1;break;}
        }
        if(!MonsterVisual_UpdateEyes(eyes,eyeCapacity,monster)){error=1;break;}
        Demo_RealtimeCamera(camera,monster,capturePrefix?captureView:view,zoom);
        double cpu=Demo_ClockMs()-begin;
        renderer->beginFrame(renderer);
        if(!OpenGLRenderer_BeginSDFFrame(renderer,camera,width,height,!nativeScale&&!capturePrefix)||
           !OpenGLRenderer_RenderSDF(renderer,&sdf,camera,width,height)){error=1;break;}
        for(size_t i=0;i<monster->eyeCount;++i){renderer->renderMesh(renderer,&eyes[i].sclera);renderer->renderMesh(renderer,&eyes[i].iris);renderer->renderMesh(renderer,&eyes[i].pupil);}
        OpenGLRenderer_EndSDFFrame(renderer,width,height);renderer->endFrame(renderer);
        if(capturePrefix&&ready&&++captureFrames>=3) {
            captureFrames=0;snprintf(path,sizeof(path),"%s-%s.ppm",capturePrefix,names[captureView]);
            if(!OpenGLRenderer_SavePPM(path,width,height))error=1;
            printf("[CAPTURA GPU] edad=%.6f %s\n",age,path);
            if(++captureView>=12)running=false;
        }
        if(validateGPU) {
            Vector3 points[4096];unsigned random=1729;size_t count=0;
            Vector3 size=AABB_Size(sdf.bounds);
            for(size_t i=0;i<768;++i) {
                float t[3];for(int axis=0;axis<3;++axis){random=random*1664525u+1013904223u;t[axis]=(float)(random>>8)/16777216.f;}
                points[count++]=Vec3_Add(sdf.bounds.start,Vec3_Create(t[0]*size.x,t[1]*size.y,t[2]*size.z));
            }
            for(size_t i=0;i<monster->anatomyGraph.nodeCount&&count<1024;++i)
                points[count++]=monster->anatomyGraph.nodes[i].center;
            /* Rayos CPU de referencia: puntos superficiales y stencil normal,
             * además del muestreo volumétrico. La GPU comprueba ambas rutas. */
            Vector3 forward=Vec3_Normalize(Vec3_Sub(camera->target,camera->position));
            Vector3 right=Vec3_Normalize(Vec3_Cross(forward,camera->up));
            Vector3 up=Vec3_Cross(right,forward);
            float tangent=tanf(camera->fov*.00872664626f);
            for(int y=0;y<18;++y)for(int x=0;x<24;++x) {
                Vector3 directionRay=Vec3_Normalize(Vec3_Add(forward,Vec3_Add(
                    Vec3_Scale(right,((x+.5f)/24.f*2.f-1.f)*tangent*(float)width/height),
                    Vec3_Scale(up,((y+.5f)/18.f*2.f-1.f)*tangent))));
                float t=0;
                for(int step=0;step<256&&t<camera->farPlane;++step) {
                    Vector3 point=Vec3_Add(camera->position,Vec3_Scale(directionRay,t));
                    float distance=MonsterSDF_EvaluateVisualDistance(&sdf,point);
                    if(distance<.001f) {
                        if(count+7<=4096) {
                            points[count++]=point;
                            const Vector3 offsets[]={{.00065f,0,0},{-.00065f,0,0},{0,.00065f,0},{0,-.00065f,0},{0,0,.00065f},{0,0,-.00065f}};
                            for(int axis=0;axis<6;++axis)points[count++]=Vec3_Add(point,offsets[axis]);
                        }
                        break;
                    }
                    t+=fmaxf(distance*.8f,.00035f);
                }
            }
            float maxError=0;
            if(!OpenGLRenderer_ValidateSDF(renderer,&sdf,points,count,&maxError)||maxError>.0002f){error=1;running=false;}
            printf("[PARIDAD GPU] edad=%.3f puntos=%zu error=%.9f %s\n",age,count,maxError,error?"FAIL":"PASS");
            if(frame>=9)running=false;
        }
        SDL_GL_SwapWindow(window);
        double end=Demo_ClockMs();
        if(!ready){OpenGLRenderer_Finish();ready=true;last=start=titleTime=Demo_ClockMs();continue;}
        double elapsed=end-begin;
        if(times&&frame<(unsigned)benchmarkFrames)times[frame]=elapsed;
        if(csv)fprintf(csv,"%u,%.7f,%.6f,%.6f,%.6f,%.6f\n",frame,age,elapsed,cpu,OpenGLRenderer_GetSDFGpuMs(renderer),OpenGLRenderer_GetSDFResolutionScale(renderer));
        ++frame;
        ++titleFrames;
        if(end-titleTime>=500) {
            double fps=(titleFrames>0&&end>titleTime)?((double)titleFrames*1000.0/(end-titleTime)):0.0;
            char title[256];snprintf(title,sizeof(title),"Monster Ager | FPS %.0f | %.1f%% | GPU %.2f ms | resolución %.0f%% | %s",fps,age*100,OpenGLRenderer_GetSDFGpuMs(renderer),OpenGLRenderer_GetSDFResolutionScale(renderer)*100,automatic?"ANIMANDO":"PAUSA");
            SDL_SetWindowTitle(window,title);titleTime=end;titleFrames=0;
        }
        if(benchmarkFrames>0&&frame>=(unsigned)benchmarkFrames)running=false;
    }
    if(benchmarkFrames>0&&frame>0&&times) {
        double elapsed=Demo_ClockMs()-start;qsort(times,frame,sizeof(double),Demo_CompareTime);
        printf("[RENDIMIENTO GPU] frames=%u anatómicos=%u segundos=%.3f fps=%.3f frame_p50=%.3f frame_p95=%.3f frame_p99=%.3f ms\n",frame,animatedFrames,elapsed/1000.,frame*1000./elapsed,times[frame/2],times[(frame-1)*95/100],times[(frame-1)*99/100]);
    }
    if(csv)fclose(csv);
    free(times);
    for(size_t i=0;i<eyeCapacity;++i){Mesh_Free(&eyes[i].sclera);Mesh_Free(&eyes[i].iris);Mesh_Free(&eyes[i].pupil);}
    free(eyes);MonsterSDF_Free(&sdf);return error;
}
#endif
