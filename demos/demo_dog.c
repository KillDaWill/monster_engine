/** @file demo_dog.c
 * @brief Inspección estática del cánido mediante la misma tubería de producción.
 */
#include "Creature.h"
#include "CreatureRig.h"
#include "Limb.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void Overlay(const AnatomyGraph* g) {
    for (size_t i=0;i<g->connectionCount;++i) {
        const AnatomyNode* a=AnatomyGraph_FindNode(g,g->connections[i].fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(g,g->connections[i].toId);
        if (a && b) OpenGLRenderer_DebugLine(a->center,b->center,Color_FromRGB(255,80,30));
    }
}
int main(int argc,char** argv) {
    const char* capture=NULL;
    bool shells=true,guards=true;int forcedShells=0,msaa=4;
    int profileFrames=0,profileView=4,profileTick=0; double cpuSum=0,gpuSum=0;
    bool sdfMode=false,debug=false,rotate=false,neutral=false,headViews=false,faceViews=false;
    for (int i=1;i<argc;++i) {
        if(strcmp(argv[i],"--no-shells")==0) shells=false;
        else if(strcmp(argv[i],"--no-guards")==0) guards=false;
        else if(strncmp(argv[i],"--shells=",9)==0) forcedShells=atoi(argv[i]+9);
        else if(strncmp(argv[i],"--msaa=",7)==0) msaa=atoi(argv[i]+7);
        else if (strncmp(argv[i],"--profile=",10)==0) profileFrames=atoi(argv[i]+10);
        else if (strncmp(argv[i],"--view=",7)==0) profileView=atoi(argv[i]+7);
        else if (strcmp(argv[i],"--head-views")==0) headViews=true;
        else if (strcmp(argv[i],"--face-views")==0) faceViews=true;
        else if (strncmp(argv[i],"--capture-dir=",14)==0) capture=argv[i]+14;
        else if (strcmp(argv[i],"--neutral")==0) neutral=true;
        else if (strcmp(argv[i],"--sdf")==0) sdfMode=true;
        else if (strcmp(argv[i],"--rotate")==0) rotate=true;
    }
    Monster m=Monster_Create();
    const CreatureRecipe* recipe=CreatureRecipes_Dog();
    CreaturePhenotype phenotype=recipe->adult;
    if (neutral) {
        phenotype.surface=SurfacePhenotype_Default();
        phenotype.surface.integument.coverage=0;
        phenotype.surface.pigment.baseColor=phenotype.surface.pigment.secondaryColor=phenotype.surface.pigment.ventralColor=Color_FromRGB(150,140,125);
        phenotype.dorsalColor=phenotype.ventralColor=phenotype.unpigmentedVentralColor=phenotype.surface.pigment.baseColor;
    }
    if (!Creature_BuildMonster(&m,recipe,&phenotype)) { fprintf(stderr,"Fallo de anatomía canina\n"); Monster_Free(&m); return 1; }
    Monster_SetHeadOpenFactor(&m,0);
    SDFMesherConfig cfg=SDFMesher_DefaultConfig();
    cfg.voxelSize=.045f;
    MonsterVisual visual=MonsterVisual_Create(cfg);
    if (!MonsterVisual_RebuildNow(&visual,&m,MonsterSDF_DefaultConfig())) {
        fprintf(stderr,"Fallo del mallado canino\n");MonsterVisual_Free(&visual);Monster_Free(&m);return 1;
    }
    const HeadSurfaceRecipe* headSurface=&m.head.anatomy.surface;
    printf("Cabeza: cráneo=(%.3f %.3f %.3f), hocico=%.3f, raíz=%.3f, punta=%.3f, oreja=(%.3f %.3f %.3f)\n",
        headSurface->craniumRadii.x*2,headSurface->craniumRadii.y*2,headSurface->craniumRadii.z*2,
        headSurface->faceTip.z-headSurface->faceRoot.z,headSurface->faceRootRadii.x*2,
        headSurface->faceTipRadii.x*2,headSurface->earShape.x*2,headSurface->earShape.y,headSurface->earShape.z*2);
    HeadResolvedMeasurements headMeasure=HeadAnatomy_Measure(&m.head.anatomy);
    printf("Medidas: longitud=%.3f, ancho cigomático=%.3f, proporción=%.3f, raíz/punta=%.3f/%.3f, ojos=%.3f, comisuras=%.3f\n",
        headMeasure.actualHeadLength,headMeasure.actualMaxZygomaticWidth,headMeasure.actualWidthLengthRatio,
        headMeasure.muzzleRootWidth,headMeasure.muzzleTipWidth,headMeasure.interEyeDistance,headMeasure.mouthCornerSpan);
    size_t components=0,largest=0,second=0;
    Mesh_ComponentStatistics(&visual.mesh,&components,&largest,&second);
    printf("Perro: nodos=%zu, vértices=%zu, componentes=%zu, segundo=%zu\n",m.anatomyGraph.nodeCount,visual.mesh.vertexCount,components,second);
    for (unsigned i=0;i<4;++i) {
        LimbRigDescriptor d;
        Limb_BuildRigDescriptor(&m.phenotype.limbs[i],10+i,&m.anatomyGraph,&d);
        const AnatomyNode* paw=AnatomyGraph_FindNode(&m.anatomyGraph,d.endEffector);
        printf("pata %u suelo=%.6f soleHeight=%.4f\n",i,paw->center.y-d.soleHeight,d.soleHeight);
    }
    SurfacePhenotype furCoat = m.surface;
    SurfacePhenotype smoothCoat = SurfacePhenotype_Default();
    smoothCoat.integument.type = INTEGUMENT_SMOOTH_SKIN;
    smoothCoat.integument.coverage = 0.0f;
    smoothCoat.pigment = furCoat.pigment;
    bool furEnabled = true;
    int surfaceDebug = 0;

    printf("\n=== Monster Engine | Perro Digitígrado con Pelaje Procedural ===\n");
    printf("Controles de cámara y visualización:\n");
    printf("  1: lateral | 2: frontal | 3: tres cuartos | 4: superior\n");
    printf("  5: primer plano cabeza/trufa | 6: primer plano cola\n");
    printf("  F: pelo/piel | G: hebras | S: conchas | R: LOD auto/8/16/32\n");
    printf("  [ / ]: disminuir / aumentar longitud del pelaje (guardLength)\n");
    printf("  ; / ': disminuir / aumentar densidad del pelaje\n");
    printf("  Arriba / Abajo (+/-): ajustar longitud de pelaje\n");
    printf("  TAB: ciclar modo diagnóstico de superficie (0..22)\n");
    printf("  Espacio: rotar espécimen | A: overlay anatomía | Escape: salir\n\n");

    OpenGLDemoWindow* window=OpenGLDemoWindow_CreateMSAA("Monster Engine | Perro digitígrado con pelaje",1100,850,msaa);
    if (!window) { MonsterVisual_Free(&visual);Monster_Free(&m);return 1; }
    ICamera camera={.target={0,2.45f,-1.4f},.up={0,1,0},.fov=38,.nearPlane=.1f,.farPlane=100};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    OpenGLRenderer_SetFurQuality(&renderer,shells,guards,forcedShells);
    if(!OpenGLRenderer_SurfaceReady(&renderer))return 1;
    int view=2,frame=0,result=0;
    double last=OpenGLDemoWindow_Time();float angle=1.0f;
    const char* names[]={
        "lateral","frontal","tres-cuartos","superior",
        "primer-plano-cabeza","primer-plano-cola","cuello-hombro","pata","cabeza-frontal",
        "lateral-derecha","oreja-interior","oreja-exterior"
    };
    bool running=true;
    while (running) {
        OpenGLDemoInput input=OpenGLDemoWindow_Poll(window);
        if (input.quit) break;
        if (input.togglePause) rotate=!rotate;
        if (input.keyA || input.toggleDebug) debug=!debug;
        if (input.view>=0 && input.view<=8) {view=input.view;rotate=false;}

        if(input.keyG)guards=!guards;
        if(input.surfaceToggle)shells=!shells;
        if(input.keyR)forcedShells=forcedShells==0?8:forcedShells==8?16:forcedShells==16?32:0;
        OpenGLRenderer_SetFurQuality(&renderer,shells,guards,forcedShells);
        /* Alternar pelaje y piel lisa */
        if (input.keyF) {
            furEnabled = !furEnabled;
            Monster_SetSurface(&m, furEnabled ? &furCoat : &smoothCoat);
            MonsterVisual_SetSurface(&visual, &m.surface);
            printf("[Superficie] Pelaje %s (sin alterar geometría)\n",
                   furEnabled ? "ACTIVADO" : "DESACTIVADO (piel lisa)");
        }

        /* Ajuste de longitud de pelo */
        if (input.furLengthAdjust != 0 || input.surfaceAdjust != 0) {
            int delta = input.furLengthAdjust != 0 ? input.furLengthAdjust : input.surfaceAdjust;
            furCoat.integument.fur.length = fmaxf(0.02f, fminf(0.40f, furCoat.integument.fur.length + (float)delta * 0.01f));
            furCoat.integument.fur.undercoatLength = 0.55f;
            if (furEnabled) {
                Monster_SetSurface(&m, &furCoat);
                MonsterVisual_SetSurface(&visual, &m.surface);
            }
            printf("[Pelaje] Longitud: primaria=%.3fm subpelo=%.3fm\n",
                   furCoat.integument.fur.length, furCoat.integument.fur.undercoatLength);
        }

        /* Ajuste de densidad de pelo */
        if (input.furDensityAdjust != 0) {
            furCoat.integument.fur.density = fmaxf(0.1f, fminf(3.5f, furCoat.integument.fur.density + (float)input.furDensityAdjust * 0.1f));
            if (furEnabled) {
                Monster_SetSurface(&m, &furCoat);
                MonsterVisual_SetSurface(&visual, &m.surface);
            }
            printf("[Pelaje] Densidad: %.2f\n", furCoat.integument.fur.density);
        }

        /* Modos diagnóstico GLSL */
        if (input.surfaceDebugNext) {
            surfaceDebug = (surfaceDebug + 1) % 23;
            OpenGLRenderer_SetSurfaceDebug(&renderer, surfaceDebug);
            printf("[Diagnóstico] Modo %d\n", surfaceDebug);
        }

        if (capture) {
            const int faceSequence[]={1,0,9,2,3,10,11};
            view=faceViews?faceSequence[frame]:frame;
        }
        if(profileFrames) view=profileView;
        double now=OpenGLDemoWindow_Time();
        if (rotate) angle+=(float)(now-last)*.3f;
        last=now;

        Vector3 target = {0, 2.45f, -1.4f};
        float radius = 13.0f;
        float az = rotate ? angle : (view == 0 ? 1.5707963f : (view == 1 ? 0.0f : (view == 3 ? 1.0f : (view == 4 ? 0.35f : (view == 5 ? 2.8f : 1.0f)))));
        float elevation = (view == 3 ? 1.40f : (view == 0 ? 0.03f : (view == 4 ? 0.15f : (view == 5 ? 0.25f : 0.12f))));

        const AnatomyNode* headNode = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 2, 1);
        Vector3 headPos = headNode ? headNode->center : Vec3_Add(m.bodyParts[0].positionRender, m.head.anatomy.landmarks.skullCenter);
        const AnatomyNode* tailNode = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 20, 3);
        if (!tailNode) tailNode = AnatomyGraph_FindModuleNode(&m.anatomyGraph, 20, 1);
        Vector3 tailPos = tailNode ? tailNode->center : Vec3_Add(m.bodyParts[0].positionRender, Vec3_Create(0, 2.6f, 1.0f));

        if (view == 4) {
            /* Primer plano cabeza/trufa */
            target = headPos;
            target.y += .12f;
            radius = 3.7f;
            az = rotate ? angle : 0.45f;
            elevation = 0.12f;
        } else if (view == 5) {
            /* Primer plano cola */
            target = tailPos;
            radius = 3.0f;
            az = rotate ? angle : 2.55f;
            elevation = 0.18f;
        }

        if(view==6){target=Vec3_Create(0,3.3f,-.05f);radius=3.6f;az=1.1f;elevation=.15f;}
        if(view==7){const AnatomyNode* paw=AnatomyGraph_FindModuleNode(&m.anatomyGraph,10,4);target=paw?paw->center:Vec3_Create(.6f,.3f,0);radius=1.8f;az=.7f;elevation=.12f;}
        if(view==8){target=Vec3_Add(headPos,Vec3_Create(0,.16f,.28f));radius=3.2f;az=0;elevation=.06f;}
        if(view==9){az=-1.5707963f;elevation=.03f;}
        if(view==10||view==11){
            target=Vec3_Add(headPos,m.head.anatomy.surface.leftEarCenter);
            radius=1.15f;
            az=view==10?.22f:2.90f;
            elevation=.08f;
        }
        if (headViews) {
            /* Las tres vistas comparten encuadre para comparar la anatomía. */
            target = Vec3_Add(headPos, Vec3_Create(0,.16f,.28f));
            radius = 3.9f;
            az = view == 0 ? 1.5707963f : (view == 1 ? 0 : .72f);
            elevation = .06f;
        }
        camera.target = target;
        camera.position = Vec3_Add(camera.target, Vec3_Create(sinf(az)*cosf(elevation)*radius, sinf(elevation)*radius, cosf(az)*cosf(elevation)*radius));
        double renderStart=OpenGLDemoWindow_Time();
        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera,1100,850);
        if (sdfMode) {
            if (!OpenGLRenderer_RenderSDF(&renderer,&visual.sdf,&camera,1100,850)) {result=1;break;}
            if (frame==0) {
                Vector3 points[2048];
                unsigned pointCount=256;
                Vector3 ear=Vec3_Add(m.bodyParts[0].positionRender,m.head.anatomy.surface.leftEarCenter);
                for (unsigned i=0;i<256;++i) points[i]=Vec3_Add(ear,Vec3_Create(((int)(i%8)-4)*.07f,((int)((i/8)%8)-4)*.1f,((int)(i/64)-2)*.08f));
                /* Concha, hueco interior, helix, escafa y ápice en ambos lados. */
                const HeadSurfaceRecipe* pinna=&m.head.anatomy.surface;
                for(unsigned e=0;e<2;++e) for(unsigned station=0;station<5;++station) {
                    float t=.08f+.22f*station;
                    Vector3 up=e?pinna->rightEarDirection:pinna->earDirection;
                    Vector3 side=e?pinna->rightEarSide:pinna->leftEarSide;
                    Vector3 normal=e?pinna->rightEarNormal:pinna->leftEarNormal;
                    Vector3 center=e?pinna->rightEarCenter:pinna->leftEarCenter;
                    float bow=(e?-1:1)*pinna->earMarginBow*pinna->earShape.x*
                              (.45f*sinf(3.14159265f*t)+.24f*t*t);
                    float bend=pinna->earLongitudinalCurve*sinf(3.14159265f*t)+pinna->earFold*t*t;
                    center=Vec3_Add(m.bodyParts[0].positionRender,Vec3_Add(center,
                        Vec3_Add(Vec3_Scale(up,(t-.5f)*pinna->earShape.y),
                                 Vec3_Add(Vec3_Scale(side,bow),Vec3_Scale(normal,bend)))));
                    points[pointCount++]=center;
                    points[pointCount++]=Vec3_Add(center,Vec3_Scale(normal,pinna->earShape.z));
                    points[pointCount++]=Vec3_Add(center,Vec3_Scale(normal,-pinna->earShape.z));
                    points[pointCount++]=Vec3_Add(center,Vec3_Scale(side,pinna->earShape.x*.85f));
                    points[pointCount++]=Vec3_Add(center,Vec3_Scale(side,-pinna->earShape.x*.85f));
                }
                for (size_t c=0;c<visual.sdf.connectorCount;++c) {
                    const MonsterSDFConnector* connector=&visual.sdf.connectors[c];
                    if (!connector->widthBulge && !connector->heightBulge) continue;
                    for (unsigned t=0;t<5;++t) for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y) {
                        if (pointCount>=2048) break;
                        points[pointCount++]=Vec3_Add(Vec3_Lerp(connector->a,connector->b,t*.25f),
                            Vec3_Add(Vec3_Scale(connector->side,x*connector->maxRadius),Vec3_Scale(connector->up,y*connector->maxRadius)));
                    }
                }
                /* Muestras del volumen cervical completo, incluidas sus
                 * superficies y la unión occipital, para comprobar CPU/GPU. */
                if (visual.sdf.axialStationCount>=4) {
                    const SDFSweepStation* neck=&visual.sdf.axialStations[0];
                    const SDFSweepStation* shoulder=&visual.sdf.axialStations[3];
                    float low=shoulder->center.y-shoulder->height-.15f;
                    float high=neck->center.y+neck->height+.15f;
                    for (unsigned z=0;z<9;++z) for (unsigned y=0;y<9;++y) for (unsigned x=0;x<9;++x) {
                        if (pointCount>=2048) break;
                        float t=z/8.0f;
                        points[pointCount++]=Vec3_Create(
                            ((float)x/4.0f-1.0f)*(shoulder->width+.15f),
                            low+(high-low)*y/8.0f,
                            shoulder->center.z+(neck->center.z+.20f-shoulder->center.z)*t);
                    }
                }
                float error=0;
                if (!OpenGLRenderer_ValidateSDF(&renderer,&visual.sdf,points,pointCount,&error) || error>.002f) result=1;
                printf("Paridad CPU/GPU orejas, cuello y músculos (%u puntos): error máximo %.8f\n",pointCount,error);
            }
        } else MonsterVisual_Render(&visual,&renderer);
        if (debug) Overlay(&m.anatomyGraph);
        renderer.endFrame(&renderer);
        if(!OpenGLRenderer_CheckErrors()){result=1;break;}
        if (capture) {
            char path[1024];snprintf(path,sizeof(path),"%s/%s%s.ppm",capture,names[view],sdfMode?"-sdf":"");
            if (!OpenGLRenderer_SavePPM(path,1100,850)) result=1;
            if (++frame==(headViews?3:faceViews?7:12)) running=false;
        }
        if(profileFrames && profileTick++>=20) {
            OpenGLMeshPerformanceStats ps=OpenGLRenderer_GetMeshPerformanceStats(&renderer);
            cpuSum+=(OpenGLDemoWindow_Time()-renderStart)*1000; gpuSum+=ps.gpuRenderMs;
            if(profileTick>=profileFrames+20) {printf("PERF view=%d frames=%d cpu=%.4f gpu=%.4f uploads=%llu bytes=%llu roots=%llu candidates=%zu shells=%d pixels=%.2f\n",view,profileFrames,cpuSum/profileFrames,gpuSum/profileFrames,(unsigned long long)ps.uploadCount,(unsigned long long)ps.totalBytesUploaded,(unsigned long long)ps.furRootBuilds,ps.guardCandidates,ps.furShells,ps.furPixels);running=false;}
        }
        OpenGLDemoWindow_Swap(window);
    }
    OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);
    MonsterVisual_Free(&visual);Monster_Free(&m);
    return result;
}
