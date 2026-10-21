/** @file capture_bovine_anatomy.c
 * @brief Vistas ortográficas del SDF real, sin textura, y detalle de cada pie. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include "Limb.h"
#include <stdlib.h>
#include <math.h>

static void Neutral(Mesh* mesh) {
    mesh->hasSurface=false;
    for(size_t i=0;i<mesh->vertexCount;++i)mesh->vertices[i].color=Color_FromRGB(170,170,170);
    Mesh_MarkGeometryChanged(mesh);
}
static bool Capture(Renderer3D* r,ICamera* camera,const Mesh* body,const MonsterVisual* visual,
    Vector3 target,Vector3 direction,float height,const char* directory,const char* name) {
    camera->target=target;camera->position=Vec3_Add(target,Vec3_Scale(direction,20));
    camera->up=fabsf(direction.y)>.9f?Vec3_Create(0,0,1):Vec3_Create(0,1,0);
    r->beginFrame(r);OpenGLRenderer_SetupCameraOrtho(camera,height,1200,900);
    r->renderMesh(r,body);
    if(visual) {
        r->renderMesh(r,&visual->headMesh);
        for(size_t i=0;i<visual->mouthCount;++i){
            r->renderMesh(r,&visual->mouths[i].jaw);r->renderMesh(r,&visual->mouths[i].hinge);
        }
        for(size_t i=0;i<visual->eyeCount;++i)r->renderMesh(r,&visual->eyes[i].globe);
    }
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    bool ok=OpenGLRenderer_SavePPM(path,1200,900);r->endFrame(r);return ok;
}
int main(int argc,char** argv) {
    if(argc<3||argc>4)return 2;
    int stage=argc==4?atoi(argv[3]):3;
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));
    MonsterVisual* v=malloc(sizeof(*v));char error[256];
    if(!doc||!m||!v||stage<0||stage>3||!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error)))return 1;
    *m=Monster_Create();if(!Creature_BuildMonster(m,&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage)))return 1;
    for(size_t i=0;i<m->mouthCount;++i)Mouth_SetOpenFactor(&m->mouths[i],0);
    SDFMesherConfig config=SDFMesher_DefaultConfig();config.voxelSize=.055f*m->phenotype.axial.totalScale;
    config.maxCells=2500000;config.maxResolution=512;
    *v=MonsterVisual_Create(config);
    if(!MonsterVisual_RebuildNow(v,m,MonsterSDF_DefaultConfig()))return 1;
    Neutral(&v->mesh);Neutral(&v->headMesh);
    for(size_t i=0;i<v->mouthCount;++i){Neutral(&v->mouths[i].jaw);Neutral(&v->mouths[i].hinge);}
    OpenGLDemoWindow* window=OpenGLDemoWindow_Create("Anatomía bovina",1200,900);if(!window)return 1;
    ICamera camera={.fov=40,.aspectRatio=4.f/3,.nearPlane=.01f,.farPlane=100};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    float s=m->phenotype.axial.totalScale;
    Vector3 target={0,2.8f*s,-.8f*s};
    Vector3 directions[]={{1,0,0},{0,0,1},{0,0,-1},{.8f,.24f,.65f}};
    const char* names[]={"side","front","rear","three-quarter"};bool ok=true;
    for(unsigned view=0;view<4;++view)ok=Capture(&renderer,&camera,&v->mesh,v,target,directions[view],7.6f*s,argv[2],names[view])&&ok;
    for(unsigned leg=0;leg<4;++leg) {
        MonsterSDF limb;MonsterSDFConnector connectors[100];
        if(!MonsterSDF_BuildModuleView(&v->sdf,10+leg,&limb,connectors,100)){ok=false;continue;}
        SDFField field=MonsterSDF_GetField(&limb);SDFMesherConfig detail=config;
        detail.voxelSize=.009f*s;detail.adaptiveDetail=false;detail.useAutoBounds=false;
        const AnatomyNode* paw=AnatomyGraph_FindModuleNode(&m->anatomyGraph,10+leg,LIMB_NODE_AUTOPOD);
        detail.bounds=(AABB3D){Vec3_Add(paw->center,Vec3_Create(-1.0f*s,-.7f*s,-.8f*s)),Vec3_Add(paw->center,Vec3_Create(1.0f*s,1.3f*s,1.3f*s))};
        SDFMesher mesher=SDFMesher_Create(detail);Mesh mesh=Mesh_Create();
        if(!SDFMesher_GenerateMesh(&mesher,&field,&mesh)){ok=false;SDFMesher_Free(&mesher);continue;}
        Neutral(&mesh);
        for(unsigned view=0;view<5;++view){
            char name[80];snprintf(name,sizeof(name),"foot-%u-%s",leg,view<4?names[view]:"sole");
            Vector3 direction=view<4?directions[view]:Vec3_Create(0,-1,0);
            ok=Capture(&renderer,&camera,&mesh,NULL,Vec3_Add(paw->center,Vec3_Create(0,0,.2f*s)),direction,1.6f*s,argv[2],name)&&ok;
        }
        Mesh_Free(&mesh);SDFMesher_Free(&mesher);
    }
    printf("anatomy_capture stage=%d vertices=%zu triangles=%zu %s\n",stage,v->mesh.vertexCount,v->mesh.indexCount/3,ok?"PASS":"FAIL");
    OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);MonsterVisual_Free(v);Monster_Free(m);free(v);free(m);free(doc);return ok?0:1;
}
