/** @file capture_equine_muzzle.c
 * @brief Capturas OpenGL reproducibles de pigmento, cobertura y piel localizada. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include "MonsterVisualAsync.h"
#include <stdlib.h>
#include <math.h>
#include <time.h>

static double Milliseconds(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
}
static void Draw(Renderer3D* r,const MonsterVisual* v) {
    r->renderMesh(r,&v->mesh);r->renderMesh(r,&v->headMesh);
    for(size_t i=0;i<v->mouthCount;++i){r->renderMesh(r,&v->mouths[i].jaw);r->renderMesh(r,&v->mouths[i].hinge);}
    for(size_t i=0;i<v->eyeCount;++i)r->renderMesh(r,&v->eyes[i].globe);
}

static bool Capture(Renderer3D* r,ICamera* camera,const MonsterVisual* v,Vector3 target,
    Vector3 direction,float height,const char* directory,const char* name,int mode) {
    camera->target=target;camera->position=Vec3_Add(target,Vec3_Scale(direction,20));camera->up=Vec3_Create(0,1,0);
    OpenGLRenderer_SetSurfaceDebug(r,mode);
    r->beginFrame(r);OpenGLRenderer_SetupCameraOrtho(camera,height,1000,900);
    Draw(r,v);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    bool ok=OpenGLRenderer_SavePPM(path,1000,900);r->endFrame(r);return ok;
}
int main(int argc,char** argv) {
    if(argc<3||argc>4)return 2;
    int stage=argc==4?atoi(argv[3]):3;char error[256];
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));MonsterVisual* v=malloc(sizeof(*v));
    if(!doc||!m||!v||stage<0||stage>3||!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error)))return 1;
    *m=Monster_Create();if(!Creature_BuildMonster(m,&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,stage)))return 1;
    Monster_SetHeadOpenFactor(m,0);
    float scale=m->phenotype.axial.totalScale;
    SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.voxelSize=.025f*scale;cfg.maxCells=4000000;cfg.maxResolution=512;
    *v=MonsterVisual_Create(cfg);if(!MonsterVisual_RebuildNow(v,m,MonsterSDF_DefaultConfig()))return 1;
    const AnatomyNode* head=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_HEAD);
    if(!head)return 1;
    Vector3 tip=m->head.anatomy.landmarks.muzzleTip;
    printf("head %u radii %g %g %g localTip %g %g %g nostril %g %g %g\n",head->id,head->envelopeRadii.x,head->envelopeRadii.y,head->envelopeRadii.z,
        tip.x,tip.y,tip.z,m->head.anatomy.landmarks.leftNostril.x,m->head.anatomy.landmarks.leftNostril.y,m->head.anatomy.landmarks.leftNostril.z);
    OpenGLDemoWindow* window=OpenGLDemoWindow_Create("Pigmentación localizada",1000,900);if(!window)return 1;
    ICamera camera={.fov=40,.aspectRatio=1000.f/900,.nearPlane=.01f,.farPlane=100};Renderer3D renderer=OpenGLRenderer_Create(&camera);
    if(!OpenGLRenderer_SurfaceReady(&renderer))return 1;
    SurfaceCoordinate probes[2048];
    const PigmentDomain* d=&m->surface.pigment.layers[0].domain;
    for(unsigned i=0;i<2048;++i) {
        Vector3 q={(float)(i%16)/5-1.5f,(float)((i/16)%16)/5-1.5f,(float)(i/256)/2-1.5f};
        probes[i]=(SurfaceCoordinate){.position=Vec3_Add(d->center,Vec3_Create(q.x*d->radii.x,q.y*d->radii.y,q.z*d->radii.z)),
            .region=(float)(i%11),.secondaryRegion=(float)((i+3)%11),.blend=.3f,.ventral=(i%13)/12.f};
    }
    bool parity=true;
    SurfacePhenotype neutral=SurfacePhenotype_Default();
    for(unsigned frame=0;frame<4;++frame) {
        SurfacePhenotype surface=frame?SurfacePhenotype_Interpolate(&m->surface,&neutral,frame*.25f):m->surface;
        float error=0;bool valid=OpenGLRenderer_ValidatePigmentDomains(&surface,probes,2048,&error);
        printf("pigment GPU frame %u samples 2048 maxError %.9g valid %d\n",frame,error,valid);
        parity=parity&&valid&&error<.00003f;
    }
    for(unsigned pattern=0;pattern<8;++pattern) {
        SurfacePhenotype surface=m->surface;
        surface.pigment.seed=0xabcd1234u;
        for(size_t i=0;i<surface.pigment.layerCount;++i) {
            surface.pigment.layers[i].pattern=(PigmentPattern)pattern;
            surface.pigment.layers[i].regionMask=(1u<<SURFACE_REGION_HEAD)|(1u<<SURFACE_REGION_VENTRAL_TRUNK);
        }
        float error=0;bool valid=OpenGLRenderer_ValidatePigmentDomains(&surface,probes,2048,&error);
        printf("pigment GPU pattern %u samples 2048 maxError %.9g valid %d\n",pattern,error,valid);
        parity=parity&&valid&&error<.00003f;
    }
    const Mesh* parts[]={&v->mesh,&v->headMesh,v->mouthCount?&v->mouths[0].jaw:NULL};
    Vector3 worldTip=Vec3_Add(head->center,tip);
    for(unsigned part=0;part<3;++part)if(parts[part]&&parts[part]->vertexCount) {
        size_t closest=0;float distance=1e20f;
        for(size_t i=0;i<parts[part]->vertexCount;++i) {
            float ds=Vec3_Distance(parts[part]->vertices[i].position,worldTip);
            if(ds<distance){distance=ds;closest=i;}
        }
        const MeshVertex* vertex=&parts[part]->vertices[closest];
        printf("part %u closest tip %.6g material %d rest %g %g %g\n",part,distance,vertex->material,vertex->surface.position.x,vertex->surface.position.y,vertex->surface.position.z);
    }
    Vector3 target=Vec3_Add(head->center,Vec3_Lerp(m->head.anatomy.landmarks.muzzleRoot,tip,.63f));
    Vector3 directions[]={{1,0,0},{0,0,1},{.8f,.12f,.7f}};const char* views[]={"side","front","three-quarter"};
    const int modes[]={0,6,29,30,31};const char* names[]={"appearance","pigment","mask","fur","roughness"};bool ok=parity;
    for(unsigned mode=0;mode<5;++mode)for(unsigned view=0;view<3;++view) {
        char name[80];snprintf(name,sizeof(name),"%s-%s",names[mode],views[view]);
        ok=Capture(&renderer,&camera,v,target,directions[view],2.6f*scale,argv[2],name,modes[mode])&&ok;
    }
    if(stage==0||stage==3) {
        MonsterVisualAsyncConfig asyncConfig=MonsterVisualAsync_DefaultConfig();
        asyncConfig.interactiveMesherConfig=cfg;asyncConfig.interactiveMesherConfig.voxelSize=.045f*scale;
        asyncConfig.settledDelaySec=1000;
        MonsterVisualAsync* async=MonsterVisualAsync_Create(asyncConfig);
        if(!async)ok=false;
        else {
            MonsterVisualAsync_Update(async,m,.016f);MonsterVisualAsync_Flush(async);MonsterVisualAsync_Update(async,m,.016f);
            Mesh sync=v->mesh;v->mesh=async->displayMesh;
            printf("async vertices %zu canonical %g\n",v->mesh.vertexCount,v->mesh.surfaceRecipe.data[SURFACE_RECIPE_METADATA_ROW][1]);
            if(!v->mesh.vertexCount)ok=false;
            for(unsigned mode=0;mode<5;++mode) {
                char name[80];snprintf(name,sizeof(name),"async-%s",names[mode]);
                ok=Capture(&renderer,&camera,v,target,directions[2],2.6f*scale,argv[2],name,modes[mode])&&ok;
            }
            v->mesh=sync;MonsterVisualAsync_Free(async);
        }
    }
    // Coste sincronizado del dibujo completo, no un temporizador GPU aislado.
    if(stage==3) {
        camera.target=target;camera.position=Vec3_Add(target,Vec3_Scale(directions[2],20));
        OpenGLRenderer_SetSurfaceDebug(&renderer,0);
        double totals[2]={0};
        for(unsigned pair=0;pair<6;++pair)for(unsigned sample=0;sample<2;++sample) {
            unsigned enabled=(pair&1)?sample:1-sample;
            SurfacePhenotype surface=m->surface;if(!enabled)surface.pigment.layerCount=0;
            SurfaceRecipe recipe=SurfaceRecipe_CompileScaled(&surface,scale);
            OpenGLRenderer_SetMeshSurfaceOverride(&renderer,&recipe);
            double start=Milliseconds();
            for(unsigned frame=0;frame<12;++frame) {
                renderer.beginFrame(&renderer);OpenGLRenderer_SetupCameraOrtho(&camera,2.6f*scale,1000,900);
                Draw(&renderer,v);OpenGLRenderer_Finish();renderer.endFrame(&renderer);
            }
            if(pair)totals[enabled]+=Milliseconds()-start;
        }
        OpenGLRenderer_SetMeshSurfaceOverride(&renderer,NULL);
        printf("render synchronized off_ms %.6f on_ms %.6f off_fps %.3f on_fps %.3f samples 60\n",totals[0]/60,totals[1]/60,60000/totals[0],60000/totals[1]);
        double start=Milliseconds();volatile float sink=0;
        for(unsigned i=0;i<200000;++i)sink+=Surface_EvaluateLocalMaterial(&m->surface,&probes[i%2048],.65f).furCoverage;
        printf("CPU localized material ns_per_point %.6f checksum %g\n",(Milliseconds()-start)*1e6/200000,(double)sink);
        printf("sizes PigmentLayer %zu SurfacePhenotype %zu SurfaceRecipe %zu CreaturePhenotype %zu CreatureRecipe %zu Monster %zu\n",sizeof(PigmentLayer),sizeof(SurfacePhenotype),sizeof(SurfaceRecipe),sizeof(CreaturePhenotype),sizeof(CreatureRecipe),sizeof(Monster));
    }
    ok=OpenGLRenderer_CheckErrors()&&ok;
    OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);MonsterVisual_Free(v);Monster_Free(m);free(v);free(m);free(doc);
    return ok?0:1;
}
