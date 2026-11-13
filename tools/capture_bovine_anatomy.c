/** @file capture_bovine_anatomy.c
 * @brief Vistas ortográficas del SDF real, sin textura, y detalle de cada pie. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include "Limb.h"
#include "SDFPrimitives.h"
#include "PrimitiveMesh.h"
#include "AnimatedVisual.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

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
/* Diagnóstico puro del barrido: no incluye los planos oral ni posterior. */
static SDFSample Envelope(const void* context,Vector3 p) {
    const MonsterSDFMouth* mouth=context;
    return SDFSample_Create(SDF_EllipticalSweepZ(p,mouth->jawStations,6),Color_FromRGB(170,170,170),SDF_MATERIAL_SKIN);
}
static SDFSample Section(const void* context,Vector3 p) {
    const MonsterSDF* sdf=context;
    float d=MonsterSDF_EvaluateVisualDistance(sdf,p);
    return SDFSample_Create(fmaxf(d,p.x),Color_FromRGB(170,170,170),SDF_MATERIAL_SKIN);
}
static bool MarkStation(Mesh* mesh,Vector3 position,float radius) {
    Mesh marker=Mesh_Create();size_t offset=mesh->vertexCount;
    bool ok=PrimitiveMesh_GenerateUVSphere(&marker,position,radius,12,6,Color_FromRGB(210,65,35))&&
        Mesh_ReserveVertices(mesh,offset+marker.vertexCount)&&Mesh_ReserveIndices(mesh,mesh->indexCount+marker.indexCount);
    if(ok) {
        memcpy(mesh->vertices+offset,marker.vertices,marker.vertexCount*sizeof(*marker.vertices));
        for(size_t i=0;i<marker.indexCount;++i)mesh->indices[mesh->indexCount+i]=marker.indices[i]+offset;
        mesh->vertexCount+=marker.vertexCount;mesh->indexCount+=marker.indexCount;
    }
    Mesh_Free(&marker);return ok;
}
int main(int argc,char** argv) {
    if(argc<3||argc>6)return 2;
    int stage=argc>=4?atoi(argv[3]):3;
    bool natural=argc>=5&&!strcmp(argv[4],"surface");
    bool muscles=argc>=5&&!strcmp(argv[4],"muscle");
    /* Estudio rápido con encuadre fijo completo, sin remallar diagnósticos. */
    bool study=argc>=5&&!strcmp(argv[4],"study");
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));
    MonsterVisual* v=malloc(sizeof(*v));char error[256];
    if(!doc||!m||!v||stage<0||stage>3||!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error)))return 1;
    CreaturePhenotype sampled;
    const CreaturePhenotype* phenotype=CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage);
    if(argc==6) {
        char* end;float age=strtof(argv[5],&end);
        if(*end||!isfinite(age)||age<0||age>1)return 2;
        sampled=CreatureRecipe_EvaluateDevelopment(&doc->recipe,age);phenotype=&sampled;
    }
    *m=Monster_Create();if(!Creature_BuildMonster(m,&doc->recipe,phenotype))return 1;
    for(size_t i=0;i<m->mouthCount;++i)Mouth_SetOpenFactor(&m->mouths[i],0);
    SDFMesherConfig config=SDFMesher_DefaultConfig();config.voxelSize=.055f*m->phenotype.axial.totalScale;
    config.maxCells=2500000;config.maxResolution=512;
    *v=MonsterVisual_Create(config);
    if(!MonsterVisual_RebuildNow(v,m,MonsterSDF_DefaultConfig()))return 1;
    if(!natural) {
        Neutral(&v->mesh);Neutral(&v->headMesh);
        for(size_t i=0;i<v->mouthCount;++i){Neutral(&v->mouths[i].jawBase);Neutral(&v->mouths[i].hingeBase);Neutral(&v->mouths[i].jaw);Neutral(&v->mouths[i].hinge);}
    }
    OpenGLDemoWindow* window=OpenGLDemoWindow_Create("Anatomía bovina",1200,900);if(!window)return 1;
    ICamera camera={.fov=40,.aspectRatio=4.f/3,.nearPlane=.01f,.farPlane=100};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    float s=m->phenotype.axial.totalScale;
    if(muscles) {
        char path[1024];snprintf(path,sizeof(path),"%s/anatomy.txt",argv[2]);
        FILE* out=fopen(path,"w");
        if(!out)return 1;
        for(int i=0;i<v->sdf.axialStationCount;++i) {
            const SDFSweepStation* a=&v->sdf.axialStations[i];
            fprintf(out,"axial %d center %g %g %g radii %g %g\n",i,a->center.x,a->center.y,a->center.z,a->width,a->height);
        }
        for(size_t i=0;i<v->sdf.connectorCount;++i) {
            const MonsterSDFConnector* c=&v->sdf.connectors[i];
            fprintf(out,"connector %u kind %d axial %d from %u to %u a %g %g %g b %g %g %g radii %g %g %g %g smooth %g\n",
                c->moduleInstanceId,c->kind,c->axialSweepMember,c->fromId,c->toId,c->a.x,c->a.y,c->a.z,c->b.x,c->b.y,c->b.z,c->widthA,c->heightA,c->widthB,c->heightB,c->localSmoothness);
        }
        fclose(out);
    }
    Vector3 target={0,2.8f*s,-.8f*s};
    Vector3 directions[]={{1,0,0},{0,0,1},{0,0,-1},{.8f,.24f,.65f}};
    const char* names[]={"side","front","rear","three-quarter"};bool ok=true;
    for(unsigned view=0;view<4;++view)ok=Capture(&renderer,&camera,&v->mesh,v,target,directions[view],(study?9.5f:7.6f)*s,argv[2],names[view])&&ok;
    if(muscles) {
        /* Caja completa del campo: el encuadre histórico corta el hocico en
         * lateral. No se oculta ese recorte en las comparaciones antiguas. */
        Vector3 center=Vec3_Scale(Vec3_Add(v->sdf.bounds.start,v->sdf.bounds.end),.5f);
        Vector3 extent=Vec3_Sub(v->sdf.bounds.end,v->sdf.bounds.start);
        float height=fmaxf(extent.y,extent.z*.75f)*1.12f;
        for(unsigned view=0;view<4;++view) {
            char name[80];snprintf(name,sizeof(name),"full-%s",names[view]);
            ok=Capture(&renderer,&camera,&v->mesh,v,center,directions[view],height,argv[2],name)&&ok;
        }
    }
    ok=Capture(&renderer,&camera,&v->mesh,v,target,Vec3_Create(0,1,0),(study?14.0f:9.5f)*s,argv[2],"top")&&ok;
    const AnatomyNode* head=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_HEAD);
    if(head&&study) {
        Vector3 center=Vec3_Add(head->center,Vec3_Create(0,0,.3f*s));
        const unsigned views[]={0,1,3};
        for(unsigned i=0;i<3;++i) {
            char name[80];snprintf(name,sizeof(name),"head-%s",names[views[i]]);
            ok=Capture(&renderer,&camera,&v->mesh,v,center,directions[views[i]],4.8f*s,argv[2],name)&&ok;
        }
    }
    if(head&&!muscles&&!study) {
        Vector3 center=Vec3_Add(head->center,Vec3_Add(m->head.anatomy.surface.faceMid,Vec3_Create(0,.25f,-.10f)));
        ok=Capture(&renderer,&camera,&v->mesh,v,center,directions[0],2.8f*s,argv[2],"head-side")&&ok;
        ok=Capture(&renderer,&camera,&v->mesh,v,center,directions[1],2.8f*s,argv[2],"head-front")&&ok;
        ok=Capture(&renderer,&camera,&v->mesh,v,center,directions[3],2.8f*s,argv[2],"head-three-quarter")&&ok;
        Mesh upper=Mesh_Create();MonsterSDFHeadField headContext;
        SDFField headField=MonsterSDF_GetHeadField(&v->sdf,0,&headContext);
        SDFMesherConfig hc=config;hc.voxelSize=.015f*s;
        SDFMesher hm=SDFMesher_Create(hc);
        if(!SDFMesher_GenerateMesh(&hm,&headField,&upper))ok=false;
        if(!natural)Neutral(&upper);
        ok=Capture(&renderer,&camera,&upper,NULL,center,directions[0],2.8f*s,argv[2],"upper-head")&&ok;
        if(v->mouthCount) {
            const MonsterSDFMouth* mouth=&v->sdf.mouths[0];
            Vector3 localCenter=Vec3_Sub(center,mouth->center);
            ok=Capture(&renderer,&camera,&v->mouths[0].jawBase,NULL,localCenter,directions[0],2.8f*s,argv[2],"jaw-alone")&&ok;
            SDFField field={0};field.context=(void*)mouth;field.evaluate=Envelope;
            SDFMesherConfig diagnostic=config;diagnostic.useAutoBounds=false;
            MonsterSDFJawField jawContext;SDFField jawField=MonsterSDF_GetJawField(&v->sdf,0,&jawContext);
            diagnostic.bounds=jawField.getBounds(jawField.context);diagnostic.voxelSize=.015f*s;
            SDFMesher mesher=SDFMesher_Create(diagnostic);Mesh envelope=Mesh_Create();
            if(SDFMesher_GenerateMesh(&mesher,&field,&envelope)) {
                Neutral(&envelope);
                ok=Capture(&renderer,&camera,&envelope,NULL,localCenter,directions[0],2.8f*s,argv[2],"jaw-envelope")&&ok;
            } else ok=false;
            Mesh_Free(&envelope);SDFMesher_Free(&mesher);
            MonsterSDF diagnosticSdf=v->sdf;MonsterSDFMouth diagnosticMouth=*mouth;
            diagnosticSdf.mouths=&diagnosticMouth;diagnosticSdf.mouthCount=1;
            diagnosticMouth.jawPosteriorZ=-1e6f;
            jawField=MonsterSDF_GetJawField(&diagnosticSdf,0,&jawContext);
            mesher=SDFMesher_Create(diagnostic);envelope=Mesh_Create();
            if(SDFMesher_GenerateMesh(&mesher,&jawField,&envelope)) {
                Neutral(&envelope);
                ok=Capture(&renderer,&camera,&envelope,NULL,localCenter,directions[0],2.8f*s,argv[2],"jaw-unclipped")&&ok;
            } else ok=false;
            Mesh_Free(&envelope);SDFMesher_Free(&mesher);
            SDFField section={.context=&v->sdf,.evaluate=Section};
            diagnostic.bounds=headField.getBounds(headField.context);
            mesher=SDFMesher_Create(diagnostic);envelope=Mesh_Create();
            if(SDFMesher_GenerateMesh(&mesher,&section,&envelope)) {
                Neutral(&envelope);
                for(int j=0;j<6;++j) {
                    Vector3 station=Vec3_Add(mouth->center,mouth->jawStations[j].center);
                    station.x=.02f*s;
                    ok=MarkStation(&envelope,station,.022f*s)&&ok;
                }
                ok=Capture(&renderer,&camera,&envelope,NULL,center,directions[0],2.8f*s,argv[2],"head-section")&&ok;
            } else ok=false;
            Mesh_Free(&envelope);SDFMesher_Free(&mesher);
            char path[1024];snprintf(path,sizeof(path),"%s/jaw.txt",argv[2]);FILE* out=fopen(path,"w");
            if(out) {
                fprintf(out,"openFactor %.9g\nhinge %.9g %.9g %.9g\nentrance %.9g %.9g %.9g\nhalfExtents %.9g %.9g %.9g\njawCenter %.9g %.9g %.9g\njawRadii %.9g %.9g %.9g\nfaceCapWeight %.9g\nlegacyRear %.9g\nsymphysis %.9g %.9g %.9g\n",
                    m->mouths[0].openFactor,mouth->hingeCenterLocal.x,mouth->hingeCenterLocal.y,mouth->hingeCenterLocal.z,
                    mouth->entranceCenterLocal.x,mouth->entranceCenterLocal.y,mouth->entranceCenterLocal.z,
                    mouth->entranceHalfExtents.x,mouth->entranceHalfExtents.y,mouth->entranceHalfExtents.z,
                    mouth->jawCenterLocal.x,mouth->jawCenterLocal.y,mouth->jawCenterLocal.z,
                    mouth->jawRadii.x,mouth->jawRadii.y,mouth->jawRadii.z,mouth->faceCapWeight,
                    mouth->hingeCenterLocal.z+mouth->faceCapWeight*(mouth->entranceCenterLocal.z-mouth->entranceHalfExtents.z-mouth->hingeCenterLocal.z),
                    m->head.anatomy.landmarks.mandibularSymphysis.x,m->head.anatomy.landmarks.mandibularSymphysis.y,m->head.anatomy.landmarks.mandibularSymphysis.z);
                fprintf(out,"posteriorZ %.9g\n",mouth->jawPosteriorZ);
                for(int j=0;j<6;++j)fprintf(out,"station %d %.9g %.9g %.9g %.9g %.9g\n",j,mouth->jawStations[j].center.x,mouth->jawStations[j].center.y,mouth->jawStations[j].center.z,mouth->jawStations[j].width,mouth->jawStations[j].height);
                fclose(out);
            } else ok=false;
            const float openings[]={0,.35f,1};const char* poses[]={"closed","partial","open"};
            for(int j=0;j<3;++j) {
                Monster_SetHeadOpenFactor(m,openings[j]);MonsterVisual_UpdateMouthArticulation(&v->mouths[0],&m->mouths[0],m);
                Vector3 framing=center;float height=2.8f*s;
                if(j==2){framing.y-=.20f*s;height=3.3f*s;}
                ok=Capture(&renderer,&camera,&upper,v,framing,directions[0],height,argv[2],poses[j])&&ok;
            }
            Monster_SetHeadOpenFactor(m,0);MonsterVisual_UpdateMouthArticulation(&v->mouths[0],&m->mouths[0],m);
        }
        Mesh_Free(&upper);SDFMesher_Free(&hm);
    }
    ok=Capture(&renderer,&camera,&v->mesh,v,Vec3_Create(0,2.9f*s,-.10f*s),directions[3],3.8f*s,argv[2],"shoulder-chest")&&ok;
    ok=Capture(&renderer,&camera,&v->mesh,v,Vec3_Create(0,4.0f*s,.6f*s),directions[0],3.8f*s,argv[2],"neck")&&ok;
    ok=Capture(&renderer,&camera,&v->mesh,v,Vec3_Create(0,2.4f*s,-m->phenotype.axial.trunkLength*s),directions[0],3.8f*s,argv[2],"hip-hock")&&ok;
    /* Encuadres idénticos antes/después, derivados de las anclas del grafo. */
    for(unsigned region=0;region<2;++region) {
        const AnatomyNode* root=AnatomyGraph_FindModuleNode(&m->anatomyGraph,region?12:10,LIMB_NODE_ROOT);
        if(!root){ok=false;continue;}
        Vector3 center={0,root->center.y+.10f*s,root->center.z};
        const unsigned views[]={0,region?2:1,3};
        for(unsigned view=0;view<3;++view) {
            char name[80];snprintf(name,sizeof(name),"%s-%s",region?"hip":"shoulder",names[views[view]]);
            ok=Capture(&renderer,&camera,&v->mesh,v,center,directions[views[view]],3.2f*s,argv[2],name)&&ok;
        }
    }
    for(unsigned leg=0;!muscles&&!study&&leg<4;++leg) {
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
    if(!ok)fprintf(stderr,"Fallo previo a la captura animada\n");
    bool walking=false;
    if(m->animation) {
        AnimatedVisual binding={0};
        if(AnimatedVisual_Bind(&binding,v,m)) {
            m->animation->animator.desiredVelocity=Vec3_Create(0,0,.8f*s);
            m->animation->animator.mouthOpen=0;
            for(int frame=0;frame<30;++frame)ok=MonsterAnimation_Update(m,1.f/60)&&ok;
            ok=AnimatedVisual_Deform(&binding,v,m)&&ok;
            ok=Capture(&renderer,&camera,&v->mesh,v,target,directions[0],7.6f*s,argv[2],"walk-side")&&ok;
            ok=Capture(&renderer,&camera,&v->mesh,v,target,directions[3],7.6f*s,argv[2],"walk-three-quarter")&&ok;
            walking=true;
        } else fprintf(stderr,"WALK_UNAVAILABLE: no se pudo vincular la malla al esqueleto; no se valida marcha\n");
        AnimatedVisual_Free(&binding);
    }
    size_t components=0,largest=0,second=0;
    Mesh_ComponentStatistics(&v->mesh,&components,&largest,&second);
    printf("anatomy_capture stage=%d vertices=%zu triangles=%zu components=%zu largest=%zu second=%zu walk=%s %s\n",stage,v->mesh.vertexCount,v->mesh.indexCount/3,components,largest,second,walking?"CAPTURED":"UNAVAILABLE",ok?"PASS":"FAIL");
    OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);MonsterVisual_Free(v);Monster_Free(m);free(v);free(m);free(doc);return ok?0:1;
}
