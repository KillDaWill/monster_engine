/** @file validate_recipe_gpu.c
 * @brief Comprueba una receta externa en los evaluadores CPU, compute y fragmento.
 */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "Monster.h"
#include "OpenGLRenderer.h"
#include <math.h>
#include <stdlib.h>

int main(int argc,char** argv){
    if(argc<2||argc>4){fprintf(stderr,"Uso: %s receta.json [etapa 0..3] [captura.ppm]\n",argv[0]);return 2;}
    int stage=argc>2?atoi(argv[2]):3;if(stage<0||stage>3)return 2;
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* monster=malloc(sizeof(*monster));
    if(!doc||!monster){free(doc);free(monster);return 1;}
    char error[256];bool ok=CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error));
    if(!ok){fprintf(stderr,"%s\n",error);free(doc);free(monster);return 1;}
    *monster=Monster_Create();MonsterSDF sdf=MonsterSDF_Create();
    ok=Creature_BuildMonster(monster,&doc->recipe,CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage));
    for(size_t i=0;i<monster->mouthCount;++i)Mouth_SetOpenFactor(&monster->mouths[i],0);
    ok=ok&&MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig());
    OpenGLDemoWindow* window=ok?OpenGLDemoWindow_Create("Validación GPU de receta",960,720):NULL;
    if(!window){MonsterSDF_Free(&sdf);Monster_Free(monster);free(monster);free(doc);return 1;}
    Vector3 center=Vec3_Scale(Vec3_Add(sdf.bounds.start,sdf.bounds.end),.5f);
    float extent=Vec3_Length(Vec3_Sub(sdf.bounds.end,sdf.bounds.start));
    ICamera camera={.position=Vec3_Add(center,Vec3_Create(extent*.8f,extent*.25f,extent*.5f)),
        .target=center,.up={0,1,0},.fov=40,.aspectRatio=960.f/720,.nearPlane=.01f,.farPlane=100};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    Vector3 points[8192];size_t count=0;
    for(size_t i=0;i<monster->anatomyGraph.nodeCount&&count<1024;++i)
        points[count++]=monster->anatomyGraph.nodes[i].center;
    Vector3 size=Vec3_Sub(sdf.bounds.end,sdf.bounds.start);
    for(unsigned i=0;i<1024;++i){
        float x=(float)((i*97u)%1021u)/1020,y=(float)((i*193u)%1019u)/1018,z=(float)((i*251u)%1013u)/1012;
        points[count++]=Vec3_Add(sdf.bounds.start,Vec3_Create(x*size.x,y*size.y,z*size.z));
    }
    /* Sondeos densos alrededor de las estructuras nuevas, además del dominio global. */
    if(sdf.mouthCount) {
        const MonsterSDFMouth* m=&sdf.mouths[0];
        Vector3 centers[]={m->leftNostrilCenterLocal,m->rightNostrilCenterLocal,m->leftOrbitCenterLocal,m->rightOrbitCenterLocal,m->faceMidLocal};
        for(unsigned feature=0;feature<5;++feature)for(unsigned j=0;j<125&&count<8192;++j) {
            Vector3 r=feature<2?m->nostrilRadii:feature<4?m->orbitRadii:m->faceMidRadii;
            Vector3 local=Vec3_Add(centers[feature],Vec3_Create(((j%5)*.5f-1)*r.x,(((j/5)%5)*.5f-1)*r.y,((j/25)*.5f-1)*r.z));
            RotationBasis3D b=m->inverseRotation;
            points[count++]=Vec3_Add(m->center,Vec3_Create(b.row0.x*local.x+b.row1.x*local.y+b.row2.x*local.z,b.row0.y*local.x+b.row1.y*local.y+b.row2.y*local.z,b.row0.z*local.x+b.row1.z*local.y+b.row2.z*local.z));
        }
    }
    /* Cada uña y espolón recibe sondeos de corona, hendidura, talón y suela. */
    for(size_t c=0;c<sdf.connectorCount;++c)if(sdf.connectors[c].material==SDF_MATERIAL_HOOF) {
        const MonsterSDFConnector* claw=&sdf.connectors[c];
        for(unsigned j=0;j<216&&count<8192;++j) {
            Vector3 local={((j%6)*.5f-1.25f)*claw->widthA,
                (((j/6)%6)*.5f-1.25f)*claw->heightB,
                ((j/36)*.3f-.25f)*claw->length};
            points[count++]=Vec3_Add(claw->a,Vec3_Add(Vec3_Scale(claw->side,local.x),
                Vec3_Add(Vec3_Scale(claw->up,local.y),Vec3_Scale(claw->forward,local.z))));
        }
    }
    float compute=0,fragment=0;
    ok=OpenGLRenderer_ValidateComputeSDF(&renderer,&sdf,points,count,&compute);
    for(size_t base=0;base<count;base+=2048) {
        size_t n=count-base<2048?count-base:2048;float chunkError=0;
        ok=OpenGLRenderer_ValidateSDF(&renderer,&sdf,points+base,n,&chunkError)&&ok;
        fragment=fmaxf(fragment,chunkError);
    }
    ok=ok&&compute<.0002f&&fragment<.0002f;
    float intervalError=0;
    ok=OpenGLRenderer_ValidatePinnaIntervals(&renderer,&sdf,&intervalError)&&ok;
    printf("[RECETA GPU] intervalo_error_m=%.9g\n",intervalError);
    printf("[RECETA GPU] etapa=%d muestras=%zu compute_error_m=%.9g fragment_error_m=%.9g %s\n",
        stage,count,compute,fragment,ok?"PASS":"FAIL");
    if(ok&&argc==4){
        GpuSDFSurfaceGrid grid;ok=GpuSDFSurfaceGrid_Configure(&grid,sdf.bounds,.045f,3);
        renderer.beginFrame(&renderer);OpenGLRenderer_SetupCamera(&camera,960,720);
        ok=ok&&OpenGLRenderer_RenderGpuSDFSurface(&renderer,&sdf,&grid,&camera);
        ok=ok&&OpenGLRenderer_SavePPM(argv[3],960,720);
        renderer.endFrame(&renderer);
        printf("[RECETA GPU] extracción y captura %s\n",ok?"PASS":"FAIL");
    }
    OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);
    MonsterSDF_Free(&sdf);Monster_Free(monster);free(monster);free(doc);return ok?0:1;
}
