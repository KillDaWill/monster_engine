/** @file validate_morph_gpu_regressions.c
 * @brief Regresiones ejecutables de raíces propietarias y reutilización de extracción.
 */
#include "OpenGLRenderer.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureRecipes.h"
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define CHECK(c,m) do {if(!(c)){fprintf(stderr,"[FAIL] %s\n",m);return false;}}while(0)

static void Begin(Renderer3D* r,ICamera* camera) {
    r->beginFrame(r);OpenGLRenderer_SetupCamera(camera,64,64);
}

static bool RootOwnership(Renderer3D* r,ICamera* camera) {
    MonsterSDFBodyPart parts[2]={0};
    for(unsigned i=0;i<2;++i){parts[i].center=Vec3_Create(0,0,i?-1:1);
        parts[i].radii=Vec3_Create(.4f,.4f,.4f);parts[i].color=Color_FromRGB(255,255,255);}
    MonsterSDF sdf={0};sdf.bodyParts=parts;sdf.bodyPartCount=2;
    sdf.bounds=(AABB3D){Vec3_Create(-1,-1,-2),Vec3_Create(1,1,2)};
    MorphHandoffSet handoffs={0};handoffs.count=1;
    MorphHandoffInterface* h=&handoffs.interfaces[0];
    h->normal=Vec3_Create(0,0,-1);h->radius=2;h->blendWidth=.05f;
    h->transitionFactor=1;h->active=true;h->segmentCount=1;
    h->segments[0]=(MorphHandoffSegment){Vec3_Create(0,0,-2),Vec3_Create(0,0,2),1};
    for(unsigned reference=0;reference<2;++reference) {
        OpenGLRenderer_SetSDFReference(r,reference!=0,.002f,8192);
        CHECK(OpenGLRenderer_SetMorphHandoffs(r,&handoffs,0),"Configurar propiedad distal");
        Begin(r,camera);
        CHECK(OpenGLRenderer_RenderSDFRegion(r,&sdf,camera,64,64,sdf.bounds),"Trazar escena de dos raíces");
        float candidate[64*64];glReadPixels(0,0,64,64,GL_DEPTH_COMPONENT,GL_FLOAT,candidate);
        r->endFrame(r);
        // Referencia independiente: sólo la esfera propietaria posterior.
        sdf.bodyParts=parts+1;sdf.bodyPartCount=1;
        CHECK(OpenGLRenderer_SetMorphHandoffs(r,NULL,-1),"Desactivar cesión en referencia");
        Begin(r,camera);
        CHECK(OpenGLRenderer_RenderSDFRegion(r,&sdf,camera,64,64,sdf.bounds),"Trazar esfera de referencia");
        float expected[64*64];glReadPixels(0,0,64,64,GL_DEPTH_COMPONENT,GL_FLOAT,expected);
        r->endFrame(r);
        CHECK(candidate[32*64+32]<1,"Una raíz rechazada no oculta la superficie propietaria posterior");
        unsigned observed=0;
        for(unsigned i=0;i<64*64;++i) {
            CHECK((candidate[i]<1)==(expected[i]<1),"Silueta tras rechazar primera raíz equivale a referencia");
            if(expected[i]<1){++observed;CHECK(fabsf(candidate[i]-expected[i])<.00002f,"Profundidad de primera raíz propietaria");}
        }
        CHECK(observed>0,"Referencia de propiedad no vacía");
        sdf.bodyParts=parts;sdf.bodyPartCount=2;
    }
    return true;
}

static bool DenseReuse(Renderer3D* r,ICamera* camera) {
    MonsterSDFBodyPart part={0};part.radii=Vec3_Create(.4f,.4f,.4f);part.color=Color_FromRGB(255,255,255);
    MonsterSDF sdf={0};sdf.bodyParts=&part;sdf.bodyPartCount=1;
    sdf.bounds=(AABB3D){Vec3_Create(-1,-1,-1),Vec3_Create(1,1,1)};
    GpuSDFSurfaceGrid grid={0};CHECK(GpuSDFSurfaceGrid_Configure(&grid,sdf.bounds,.1f,2),"Retícula de extracción");
    for(unsigned frame=0;frame<4;++frame) {
        if(frame==1)camera->position.x=.1f;
        if(frame==2)part.center.y=.13f;
        if(frame==3)grid.refinementSteps=3;
        Begin(r,camera);
        CHECK(OpenGLRenderer_RenderGpuSDFSurface(r,&sdf,&grid,camera),"Extracción densa");
        r->endFrame(r);
        GpuSDFSurfaceStats stats=OpenGLRenderer_GetGpuSDFSurfaceStats(r);
        CHECK(frame==1?stats.samples==0:stats.samples>0,"Reusar sólo campo y retícula idénticos; mover cámara no remalla");
    }
    return true;
}

/** @brief Remallar con paleta constante conserva cobertura y material de piel. */
static bool SparseMaterialRefresh(Renderer3D* r,ICamera* camera) {
    MonsterSDFBodyPart part={0};part.radii=Vec3_Create(.6f,.6f,.6f);
    part.color=Color_FromRGB(255,255,255);
    MonsterSDF sdf={0};sdf.bodyParts=&part;sdf.bodyPartCount=1;sdf.hasSurface=true;
    SurfacePhenotype surface=SurfacePhenotype_Default();
    surface.pigment.baseColor=Color_FromRGB(25,130,45);
    surface.pigment.secondaryColor=surface.pigment.ventralColor=surface.pigment.baseColor;
    sdf.surfaceRecipe=SurfaceRecipe_Compile(&surface);
    sdf.bounds=(AABB3D){Vec3_Create(-1,-1,-1),Vec3_Create(1,1,1)};
    GpuSDFSurfaceGrid grid;
    CHECK(GpuSDFSurfaceGrid_Configure(&grid,sdf.bounds,.1f,2),"Retícula material disperso");
    GpuSDFSurfaceCache* cache=GpuSDFSurfaceCache_Create(&grid,8);
    CHECK(cache,"Caché material disperso");
    OpenGLRenderer_SetSurfaceDebug(r,27);
    for(unsigned frame=0;frame<8;++frame) {
        if(frame==1)part.center.y=.13f;
        if(frame==3)part.color=Color_FromRGB(180,40,25);
        if(frame==4)sdf.hasSurface=false;
        if(frame==5)part.center.y=-.07f;
        if(frame==6)sdf.hasSurface=true;
        if(frame==7)part.center.y=.03f;
        unsigned char cached[64*64*3],exact[64*64*3];
        unsetenv("MONSTER_GPU_EXACT_MATERIAL");
        Begin(r,camera);
        CHECK(OpenGLRenderer_RenderGpuSparseSDFSurface(r,&sdf,cache,camera),"Material disperso almacenado");
        glReadPixels(0,0,64,64,GL_RGB,GL_UNSIGNED_BYTE,cached);r->endFrame(r);
        setenv("MONSTER_GPU_EXACT_MATERIAL","1",1);
        Begin(r,camera);
        CHECK(OpenGLRenderer_RenderGpuSparseSDFSurface(r,&sdf,cache,camera),"Referencia material SDF exacto");
        glReadPixels(0,0,64,64,GL_RGB,GL_UNSIGNED_BYTE,exact);r->endFrame(r);
        unsigned maximum=0;
        for(size_t i=0;i<sizeof(cached);++i) {
            unsigned error=(unsigned)abs((int)cached[i]-(int)exact[i]);
            if(error>maximum)maximum=error;
        }
        printf("[MATERIAL GPU] frame=%u error_rgb=%u\n",frame,maximum);
        CHECK(maximum<=1,"Remallado con paleta constante conserva textura frente al SDF exacto");
    }
    unsetenv("MONSTER_GPU_EXACT_MATERIAL");OpenGLRenderer_SetSurfaceDebug(r,0);
    GpuSDFSurfaceCache_Destroy(cache);
    return true;
}

static bool FieldParity(Renderer3D* r) {
    Monster dog=Monster_Create(),lizard=Monster_Create(),current=Monster_Create();
    const CreatureRecipe* a=CreatureRecipes_Dog();const CreatureRecipe* b=CreatureRecipes_Lizard();
    CHECK(Creature_BuildMonster(&dog,a,&a->adult)&&Creature_BuildMonster(&lizard,b,&b->adult),"Extremos de paridad");
    MonsterSDF field=MonsterSDF_Create();MonsterSDFConfig config=MonsterSDF_DefaultConfig();
    float ages[]={0,.1f,.2f,.35f,.5f,.65f,.8f,.9f,1,.5f,0};
    for(unsigned age=0;age<sizeof(ages)/sizeof(*ages);++age) {
        CHECK(CreatureMorph_Interpolate(&dog,&lizard,ages[age],&current)&&MonsterSDF_Build(&field,&current,config),"Campo de paridad por edad");
        Vector3 points[4096];size_t count=0;
        for(size_t i=0;i<current.anatomyGraph.nodeCount&&count<2048;++i) {
            const AnatomyNode* node=&current.anatomyGraph.nodes[i];
            points[count++]=node->center;
            points[count++]=Vec3_Add(node->center,Vec3_Scale(node->envelopeRadii,.7f));
        }
        for(unsigned i=0;i<2048&&count<4096;++i) {
            float x=(float)((i*97u)%2039u)/2038,y=(float)((i*193u)%2029u)/2028,z=(float)((i*251u)%2017u)/2016;
            Vector3 size=Vec3_Sub(field.bounds.end,field.bounds.start);
            points[count++]=Vec3_Add(field.bounds.start,Vec3_Create(x*size.x,y*size.y,z*size.z));
        }
        float compute=0,ray=0;
        CHECK(OpenGLRenderer_ValidateComputeSDF(r,&field,points,count,&compute)&&compute<.0002f,"Paridad CPU/compute con grupos contiguos");
        CHECK(OpenGLRenderer_ValidateSDF(r,&field,points,count,&ray)&&ray<.0002f,"Paridad CPU/fragment con grupos contiguos");
        printf("[PARIDAD] edad=%.2f puntos=%zu compute=%g fragment=%g\n",ages[age],count,compute,ray);
    }
    MonsterSDF_Free(&field);Monster_Free(&dog);Monster_Free(&lizard);Monster_Free(&current);
    return true;
}

int main(void) {
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 2;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window* window=SDL_CreateWindow("Regresión Ager GPU",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!window){fprintf(stderr,"%s\n",SDL_GetError());SDL_Quit();return 2;}
    SDL_GLContext context=SDL_GL_CreateContext(window);if(!context){SDL_DestroyWindow(window);SDL_Quit();return 2;}
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glClearDepth(1);glDisable(GL_CULL_FACE);
    ICamera camera={.position={0,0,4},.target={0,0,0},.up={0,1,0},.fov=60,.aspectRatio=1,.nearPlane=.1f,.farPlane=20};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    bool ok=RootOwnership(&renderer,&camera)&&DenseReuse(&renderer,&camera)&&SparseMaterialRefresh(&renderer,&camera)&&FieldParity(&renderer)&&OpenGLRenderer_CheckErrors();
    OpenGLRenderer_Destroy(&renderer);SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    if(ok)puts("[PASS] Raíz propietaria posterior y caché densa GPU");
    return ok?0:1;
}
