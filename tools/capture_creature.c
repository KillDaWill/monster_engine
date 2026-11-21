/** @file capture_creature.c
 * @brief Capturas reproducibles de cualquier receta, edad y pose oral. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include "AnimatedVisual.h"
#include "FlexibleAppendage.h"
#include "Limb.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void Neutral(Mesh* mesh) {
    mesh->hasSurface=false;
    for(size_t i=0;i<mesh->vertexCount;++i)mesh->vertices[i].color=Color_FromRGB(155,155,155);
    Mesh_MarkGeometryChanged(mesh);
}
static float ViewHeight(AABB3D bounds, Vector3 direction) {
    Vector3 forward=Vec3_Normalize(direction);
    Vector3 up=fabsf(forward.y)>.9f?Vec3_Create(0,0,1):Vec3_Create(0,1,0);
    Vector3 right=Vec3_Normalize(Vec3_Cross(up,forward));
    up=Vec3_Cross(forward,right);
    Vector3 size=AABB_Size(bounds);
    float x=fabsf(right.x)*size.x+fabsf(right.y)*size.y+fabsf(right.z)*size.z;
    float y=fabsf(up.x)*size.x+fabsf(up.y)*size.y+fabsf(up.z)*size.z;
    return fmaxf(x*.75f,y)*1.08f;
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
        LongHairSet_Free(&v->longHair);
        Neutral(&v->mesh);Neutral(&v->headMesh);Neutral(&v->membraneMesh);Neutral(&v->armorMesh);
        for(size_t i=0;i<v->mouthCount;++i) {
            Neutral(&v->mouths[i].jawBase);Neutral(&v->mouths[i].hingeBase);
            Neutral(&v->mouths[i].jaw);Neutral(&v->mouths[i].hinge);
        }
    }
    OpenGLDemoWindow* window=OpenGLDemoWindow_Create("Creature capture",1200,900);
    if(!window)return 1;
    ICamera camera={.fov=40,.aspectRatio=4.f/3,.nearPlane=.01f,.farPlane=100};
    Renderer3D renderer=OpenGLRenderer_Create(&camera);
    AABB3D framing=v->sdf.bounds;
    AABB_Pad(&framing,-v->sdf.config.boundsPadding);
    Vector3 center=Vec3_Scale(Vec3_Add(framing.start,framing.end),.5f);
    float height=ViewHeight(framing,Vec3_Create(1,0,0));
    Vector3 directions[]={{1,0,0},{0,0,1},{0,0,-1},{.8f,.24f,.65f},{0,1,0},{-1,0,0}};
    const char* names[]={"side","front","rear","three-quarter","top","opposite-side"};
    bool ok=true;
    for(int i=0;i<6;++i)ok=Capture(&renderer,&camera,v,center,directions[i],ViewHeight(framing,directions[i]),argv[2],names[i])&&ok;
    if(v->sdf.mouthCount && m->hasHead) {
        AABB3D headBox=v->sdf.mouths[0].headBounds;
        const AnatomyNode* head=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_HEAD);
        for (size_t i=0;i<v->sdf.connectorCount;++i) {
            const MonsterSDFConnector* c=&v->sdf.connectors[i];
            if (c->region!=ANATOMY_REGION_ORNAMENT || c->kind!=BODY_CONNECTION_ORNAMENT_SEGMENT) continue;
            for (size_t j=0;j<doc->recipe.bodyPlan.moduleCount;++j) {
                const CreatureModuleInstance* mod=&doc->recipe.bodyPlan.modules[j];
                if(mod->instanceId!=c->moduleInstanceId ||
                    (!head || mod->attachment.hostModuleInstanceId!=head->moduleInstanceId)) continue;
                AABB_ExpandPoint(&headBox,c->bounds.start);
                AABB_ExpandPoint(&headBox,c->bounds.end);
            }
        }
        Vector3 hc=Vec3_Scale(Vec3_Add(headBox.start,headBox.end),.5f);
        for(int i=0;i<4;++i) {
            char name[80];snprintf(name,sizeof(name),"head-%s",names[i]);
            ok=Capture(&renderer,&camera,v,hc,directions[i],ViewHeight(headBox,directions[i]),argv[2],name)&&ok;
        }
    }
    for (int role=LIMB_FORE;role<=LIMB_HIND;++role) {
        for (size_t i=0;i<doc->recipe.bodyPlan.moduleCount;++i) {
            const CreatureModuleInstance* module=&doc->recipe.bodyPlan.modules[i];
            if (module->kind!=CREATURE_MODULE_LIMB) continue;
            const LimbPhenotype* limb=&p.limbs[module->phenotypeIndex];
            if ((int)limb->role!=role || limb->side!=LIMB_LEFT) continue;
            const AnatomyNode* foot=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module->instanceId,LIMB_NODE_AUTOPOD);
            if (!foot) continue;
            Vector3 target=Vec3_Add(foot->center,Vec3_Create(0,-foot->heightRadius*.3f,
                foot->widthRadius*.94f));
            ok=Capture(&renderer,&camera,v,target,Vec3_Create(.43f,.24f,1),
                       limb->thickness*s*3.7f,argv[2],role==LIMB_FORE?"forefoot":"hindfoot")&&ok;
            Vector3 soleTarget=Vec3_Add(target,Vec3_Create(0,0,limb->thickness*s*.40f));
            ok=Capture(&renderer,&camera,v,soleTarget,Vec3_Create(0,-1,0),
                       limb->thickness*s*4.2f,argv[2],role==LIMB_FORE?"forefoot-sole":"hindfoot-sole")&&ok;
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
