/** @file capture_creature.c
 * @brief Capturas reproducibles de cualquier receta, edad y pose oral. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include "AnimatedVisual.h"
#include "FlexibleAppendage.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void Neutral(Mesh* mesh) {
    mesh->hasSurface=false;
    for(size_t i=0;i<mesh->vertexCount;++i)mesh->vertices[i].color=Color_FromRGB(155,155,155);
    Mesh_MarkGeometryChanged(mesh);
}
static bool Capture(Renderer3D* r,ICamera* camera,MonsterVisual* v,Vector3 target,
    Vector3 direction,float height,const char* directory,const char* name) {
    camera->target=target;camera->position=Vec3_Add(target,Vec3_Scale(direction,20));
    camera->up=fabsf(direction.y)>.9f?Vec3_Create(0,0,1):Vec3_Create(0,1,0);
    r->beginFrame(r);OpenGLRenderer_SetupCameraOrtho(camera,height,1200,900);
    if(!MonsterVisual_Render(v,r)) {
        r->endFrame(r);
        return false;
    }
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    bool ok=OpenGLRenderer_SavePPM(path,1200,900);r->endFrame(r);return ok;
}
static bool CaptureFlexible(Renderer3D* renderer, ICamera* camera, MonsterVisual* visual,
    Monster* monster, const AnimatedVisual* binding, uint32_t module, Vector3 center,
    float height, const char* directory) {
    const char* names[]={"relaxed","curled","reaching","feeding"};
    MonsterAnimation* animation=monster->animation;
    const Skeleton* skeleton=&animation->rig.skeleton;
    int tip=Skeleton_FindJoint(skeleton,Anatomy_MakeId(module,FLEXIBLE_APPENDAGE_STATIONS));
    int previous=Skeleton_FindJoint(skeleton,Anatomy_MakeId(module,FLEXIBLE_APPENDAGE_STATIONS-1));
    if(tip<0||previous<0)return false;
    bool ok=true;
    for(unsigned pose=0;pose<FLEXIBLE_POSE_COUNT;++pose) {
        if(!FlexibleAppendage_ApplyPose(module,pose,1,skeleton,&animation->pose) ||
           !AnimatedVisual_Deform(binding,visual,monster))return false;
        char name[80];
        snprintf(name,sizeof(name),"appendage-%s",names[pose]);
        ok=Capture(renderer,camera,visual,center,Vec3_Create(.94f,.12f,.40f),height,directory,name)&&ok;
        Vector3 end=animation->pose.joints[tip].worldPosition;
        Vector3 tangent=Vec3_Normalize(Vec3_Sub(end,animation->pose.joints[previous].worldPosition));
        Vector3 direction=Vec3_Normalize(Vec3_Add(tangent,Vec3_Create(.23f,0,0)));
        snprintf(name,sizeof(name),"tip-%s",names[pose]);
        ok=Capture(renderer,camera,visual,end,direction,monster->phenotype.axial.totalScale*.78f,
            directory,name)&&ok;
    }
    return FlexibleAppendage_ApplyPose(module,FLEXIBLE_POSE_RELAXED,1,skeleton,&animation->pose)&&ok;
}

int main(int argc,char** argv) {
    if(argc<4||argc>6) {
        fprintf(stderr,"Uso: %s receta directorio edad [neutral|surface] [apertura]\n",argv[0]);return 2;
    }
    char* end=NULL;float age=strtof(argv[3],&end);
    if(*end||!isfinite(age)||age<0||age>1)return 2;
    float opening=argc==6?strtof(argv[5],&end):0;
    if(argc==6&&(*end||!isfinite(opening)||opening<0||opening>1))return 2;
    bool neutral=argc<5||!strcmp(argv[4],"neutral");
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));
    MonsterVisual* v=malloc(sizeof(*v));char error[256];
    if(!doc||!m||!v)return 1;
    if(!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error))) {
        fprintf(stderr,"%s\n",error);return 1;
    }
    CreaturePhenotype p=CreatureRecipe_EvaluateDevelopment(&doc->recipe,age);
    *m=Monster_Create();if(!Creature_BuildMonster(m,&doc->recipe,&p))return 1;
    Monster_SetHeadOpenFactor(m,opening);
    float s=p.axial.totalScale;
    SDFMesherConfig config=SDFMesher_DefaultConfig();
    config.voxelSize=.018f*s;config.maxCells=6000000;config.maxResolution=768;
    *v=MonsterVisual_Create(config);
    if(!MonsterVisual_RebuildNow(v,m,MonsterSDF_DefaultConfig()))return 1;
    const SDFMesherStats* stats=&v->mesher.lastStats;
    printf("mesh leaves=%zu minimumVoxel=%g effectiveVoxel=%g budgetAdjusted=%d "
           "classificationMs=%.2f contourMs=%.2f attributesMs=%.2f\n",
        stats->candidateCellCount,stats->minimumVoxelSize,stats->effectiveVoxelSize,
        stats->cellBudgetAdjusted,stats->adaptiveClassificationMs,
        stats->adaptiveContourMs,stats->adaptiveAttributesMs);
    if(v->sdf.mouthCount) {
        const MonsterSDFMouth* mouth=&v->sdf.mouths[0];
        printf("oral angle=%g entrance=%g,%g slope=%g half=%g\n",mouth->visualJawAngle,
            mouth->entranceCenterLocal.y,mouth->entranceCenterLocal.z,mouth->oralSlope,mouth->entranceHalfExtents.y);
        for(int i=0;i<6;++i)printf("station %d head=%g,%g,%g,%g jaw=%g,%g,%g,%g\n",i,
            mouth->headStations[i].center.y,mouth->headStations[i].center.z,mouth->headStations[i].width,mouth->headStations[i].height,
            mouth->jawStations[i].center.y,mouth->jawStations[i].center.z,mouth->jawStations[i].width,mouth->jawStations[i].height);
    }
    if(neutral) {
        Neutral(&v->mesh);Neutral(&v->headMesh);
        for(size_t i=0;i<v->mouthCount;++i) {
            Neutral(&v->mouths[i].jawBase);Neutral(&v->mouths[i].hingeBase);
            Neutral(&v->mouths[i].jaw);Neutral(&v->mouths[i].hinge);
        }
    }
    OpenGLDemoWindow* window=OpenGLDemoWindow_Create("Creature capture",1200,900);
    if(!window)return 1;
    ICamera camera={.fov=40,.aspectRatio=4.f/3,.nearPlane=.01f,.farPlane=100};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    Vector3 center=Vec3_Scale(Vec3_Add(v->sdf.bounds.start,v->sdf.bounds.end),.5f);
    Vector3 extent=Vec3_Sub(v->sdf.bounds.end,v->sdf.bounds.start);
    float height=fmaxf(extent.y,extent.z*.75f)*1.08f;
    Vector3 directions[]={{1,0,0},{0,0,1},{0,0,-1},{.8f,.24f,.65f},{0,1,0}};
    const char* names[]={"side","front","rear","three-quarter","top"};
    bool ok=true;
    for(int i=0;i<5;++i)ok=Capture(&renderer,&camera,v,center,directions[i],height,argv[2],names[i])&&ok;
    if(m->hasHead) {
        const AnatomyNode* head=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_HEAD);
        Vector3 hc=Vec3_Add(head->center,Vec3_Lerp(m->head.anatomy.surface.craniumCenter,m->head.anatomy.surface.faceTip,.45f));
        for(int i=0;i<4;++i) {
            char name[80];snprintf(name,sizeof(name),"head-%s",names[i]);
            ok=Capture(&renderer,&camera,v,hc,directions[i],4.1f*s,argv[2],name)&&ok;
        }
    }
    if(m->animation) {
        AnimatedVisual binding={0};
        if(!AnimatedVisual_Bind(&binding,v,m))ok=false;
        else {
            for(size_t i=0;i<doc->recipe.bodyPlan.moduleCount;++i) {
                const CreatureModuleInstance* module=&doc->recipe.bodyPlan.modules[i];
                if(module->kind==CREATURE_MODULE_FLEXIBLE_APPENDAGE)
                    ok=CaptureFlexible(&renderer,&camera,v,m,&binding,module->instanceId,center,height,argv[2])&&ok;
            }
            m->animation->animator.desiredVelocity=Vec3_Create(0,0,.8f*s);
            m->animation->animator.mouthOpen=opening;
            for(int frame=0;frame<30;++frame)ok=MonsterAnimation_Update(m,1.f/60)&&ok;
            ok=AnimatedVisual_Deform(&binding,v,m)&&ok;
            ok=Capture(&renderer,&camera,v,center,directions[0],height,argv[2],"walk-side")&&ok;
            ok=Capture(&renderer,&camera,v,center,directions[3],height,argv[2],"walk-three-quarter")&&ok;
        }
        AnimatedVisual_Free(&binding);
    }
    printf("capture age=%g voxel=%g vertices=%zu %s\n",age,config.voxelSize,v->mesh.vertexCount,ok?"PASS":"FAIL");
    OpenGLRenderer_Destroy(&renderer);OpenGLDemoWindow_Free(window);
    MonsterVisual_Free(v);Monster_Free(m);free(v);free(m);free(doc);return ok?0:1;
}
