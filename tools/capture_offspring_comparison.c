/** Captura cinco recetas y su referencia en un encuadre anatómico común. */
#include "AxialBody.h"
#include "CreatureMorph.h"
#include "CreatureRecipeIO.h"
#include "FlexibleAppendage.h"
#include "MonsterAnimation.h"
#include "MonsterAger.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include "../src/OffspringMorphFitter.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH=504, HEIGHT=378, COLUMNS=5 };

typedef struct {
    Monster monster;
    CreatureRecipeDocument document;
    MonsterSDF field;
    Vector3 origin[2];
    float scale;
} Subject;

static bool Load(Subject *subject,const char *path)
{
    const CreatureRecipe *recipe;
    if (*path=='@') recipe=CreatureRecipes_Find((CreatureRecipeId)strtoul(path+1,NULL,10));
    else if (CreatureRecipeIO_LoadFile(&subject->document,path,NULL,0)) recipe=&subject->document.recipe;
    else return false;
    return recipe && Creature_BuildMonster(&subject->monster,recipe,&recipe->adult);
}

static void UnionNormalized(AABB3D *total,AABB3D bounds,Vector3 origin,float scale)
{
    AABB_ExpandPoint(total,Vec3_Scale(Vec3_Sub(bounds.start,origin),1/scale));
    AABB_ExpandPoint(total,Vec3_Scale(Vec3_Sub(bounds.end,origin),1/scale));
}

static bool Compile(Subject *s,AABB3D shared[2])
{
    Monster *m=&s->monster;
    const AnatomyNode *shoulder=NULL;
    for (size_t i=0; i<m->anatomyGraph.nodeCount; ++i)
        if (m->anatomyGraph.nodes[i].localNodeId==AXIAL_NODE_PECTORAL &&
            m->anatomyGraph.nodes[i].region==ANATOMY_REGION_TRUNK) {
            shoulder=&m->anatomyGraph.nodes[i]; break;
        }
    const AnatomyNode *head=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_HEAD);
    s->scale=m->phenotype.axial.totalScale;
    if (!shoulder || !head || s->scale<=0 ||
        !MonsterSDF_Build(&s->field,m,MonsterSDF_DefaultConfig()) || !s->field.mouthCount) return false;
    s->origin[0]=shoulder->center;
    s->origin[1]=Vec3_Add(head->center,m->head.anatomy.landmarks.neckAttachment);
    AABB3D body=s->field.bounds;
    AABB_Pad(&body,-s->field.config.boundsPadding);
    UnionNormalized(&shared[0],body,s->origin[0],s->scale);
    const HeadSurfaceRecipe *face = &m->head.anatomy.surface;
    AABB3D headBounds = AABB_Empty();
    AABB_ExpandRadius(&headBounds,Vec3_Add(head->center,face->craniumCenter),face->craniumRadii);
    AABB_ExpandRadius(&headBounds,Vec3_Add(head->center,face->faceTip),face->faceTipRadii);
    if (face->hasEars) {
        AABB_ExpandRadius(&headBounds,Vec3_Add(head->center,face->leftEarCenter),face->earRadii);
        AABB_ExpandRadius(&headBounds,Vec3_Add(head->center,face->rightEarCenter),face->earRadii);
    }
    UnionNormalized(&shared[1],headBounds,s->origin[1],s->scale);
    return true;
}

static ICamera Camera(const Subject *s,AABB3D bounds,Vector3 direction,bool head)
{
    Vector3 forward=Vec3_Normalize(direction);
    Vector3 referenceUp=fabsf(forward.y)>.9f?Vec3_Create(0,0,1):Vec3_Create(0,1,0);
    Vector3 right=Vec3_Normalize(Vec3_Cross(referenceUp,forward));
    Vector3 up=Vec3_Cross(forward,right);
    Vector3 size=AABB_Size(bounds);
    float x=fabsf(right.x)*size.x+fabsf(right.y)*size.y+fabsf(right.z)*size.z;
    float y=fabsf(up.x)*size.x+fabsf(up.y)*size.y+fabsf(up.z)*size.z;
    float height=fmaxf(x*HEIGHT/WIDTH,y)*1.15f*s->scale;
    Vector3 center=Vec3_Add(s->origin[head],Vec3_Scale(Vec3_Add(bounds.start,bounds.end),.5f*s->scale));
    return (ICamera){.target=center,.position=Vec3_Add(center,Vec3_Scale(forward,24*s->scale)),
        .up=referenceUp,.fov=-height,.aspectRatio=(float)WIDTH/HEIGHT,.nearPlane=.01f,.farPlane=100};
}

static float HeadFieldError(const Subject *a,const Subject *target,AABB3D frame)
{
    MonsterSDFHeadField ca,cb;
    SDFField x=MonsterSDF_GetHeadField(&a->field,0,&ca);
    SDFField y=MonsterSDF_GetHeadField(&target->field,0,&cb);
    if (!x.evaluateDistance || !y.evaluateDistance) return NAN;
    Vector3 length=AABB_Size(frame);
    float band=Vec3_Length(length)*.08f, sum=0;
    unsigned count=0;
    for (unsigned i=0; i<16; ++i) for (unsigned j=0; j<16; ++j) for (unsigned k=0; k<16; ++k) {
        Vector3 p=Vec3_Add(frame.start,Vec3_Create(length.x*i/15,length.y*j/15,length.z*k/15));
        float va=x.evaluateDistance(x.context,Vec3_Add(a->origin[1],Vec3_Scale(p,a->scale)))/a->scale;
        float vb=y.evaluateDistance(y.context,Vec3_Add(target->origin[1],Vec3_Scale(p,target->scale)))/target->scale;
        if (fabsf(vb)>band) continue;
        float error=fminf(fabsf(va-vb),band*3);
        sum+=error*error;
        ++count;
    }
    return count ? sqrtf(sum/count)/Vec3_Length(length) : NAN;
}

static Vector3 MouthWorld(const MonsterSDFMouth *mouth, Vector3 local)
{
    RotationBasis3D basis=mouth->inverseRotation;
    return Vec3_Add(mouth->center,Vec3_Add(Vec3_Scale(basis.row0,local.x),
        Vec3_Add(Vec3_Scale(basis.row1,local.y),Vec3_Scale(basis.row2,local.z))));
}

static void DrawAnatomy(const Subject *s)
{
    const Monster *m = &s->monster;
    const AnatomyGraph *graph = &m->anatomyGraph;
    const HeadAnatomy *head = &m->head.anatomy;
    glPushAttrib(GL_ENABLE_BIT|GL_LINE_BIT|GL_POINT_BIT|GL_CURRENT_BIT|GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glLineWidth(2);
    glColor3f(.25f,1,.94f);
    glBegin(GL_LINES);
    for (size_t i=0; i<graph->connectionCount; ++i) {
        if (graph->dormantConnections[i]) continue;
        const AnatomyNode *a = AnatomyGraph_FindNode(graph,graph->connections[i].fromId);
        const AnatomyNode *b = AnatomyGraph_FindNode(graph,graph->connections[i].toId);
        glVertex3f(a->center.x,a->center.y,a->center.z);
        glVertex3f(b->center.x,b->center.y,b->center.z);
    }
    glEnd();
    Vector3 origin = m->bodyParts[head->attachmentBodyPartIndex].positionRender;
    const HeadLandmarks *l = &head->landmarks;
    Vector3 landmarks[] = {l->leftOrbit,l->rightOrbit,l->leftJawHinge,l->rightJawHinge,
        l->leftMouthCorner,l->rightMouthCorner,
        l->mandibularSymphysis,l->muzzleRoot,l->muzzleTip,l->leftEarBase,l->rightEarBase};
    glPointSize(6);
    glColor3f(1,.66f,.20f);
    glBegin(GL_POINTS);
    for (size_t i=0; i<sizeof(landmarks)/sizeof(*landmarks); ++i) {
        Vector3 point = Vec3_Add(origin,landmarks[i]);
        glVertex3f(point.x,point.y,point.z);
    }
    glEnd();
    if (s->field.mouthCount) {
        const MonsterSDFMouth *mouth=&s->field.mouths[0];
        glColor3f(.9f,.30f,.70f);
        glBegin(GL_POINTS);
        if (mouth->nostrilRadii.x>0) {
            Vector3 left=MouthWorld(mouth,mouth->leftNostrilCenterLocal);
            Vector3 right=MouthWorld(mouth,mouth->rightNostrilCenterLocal);
            glVertex3f(left.x,left.y,left.z);
            glVertex3f(right.x,right.y,right.z);
        }
        glEnd();
        glColor3f(.4f,.70f,1);
        glBegin(GL_LINE_STRIP);
        if(mouth->sweptSkull) for(size_t i=0;i<6;++i) {
            Vector3 point=MouthWorld(mouth,mouth->headStations[i].center);
            glVertex3f(point.x,point.y,point.z);
        }
        glEnd();
    }
    glPopAttrib();
}

static bool RenderView(Renderer3D *r,const Subject *s,ICamera camera,const char *path,
    bool renderEyes,bool overlay)
{
    MonsterVisualEye eyes[2]={0};
    OpenGLRenderer_SetupCamera(&camera,WIDTH,HEIGHT);
    r->beginFrame(r);
    bool ok=OpenGLRenderer_BeginSDFFrame(r,&camera,WIDTH,HEIGHT,false);
    if (ok) {
        ok=OpenGLRenderer_RenderSDF(r,&s->field,&camera,WIDTH,HEIGHT);
        ok=OpenGLRenderer_EndSDFFrame(r,WIDTH,HEIGHT)&&ok;
    }
    if (ok && renderEyes && s->monster.eyeCount==2) {
        OpenGLRenderer_SetupCamera(&camera,WIDTH,HEIGHT);
        ok=MonsterVisual_UpdateEyeInstances(eyes,2,&s->monster);
        if (ok) for (unsigned i=0; i<2; ++i) r->renderEye(r,&eyes[i]);
    }
    for (unsigned i=0; i<2; ++i) Mesh_Free(&eyes[i].globe);
    if (ok && overlay) {
        OpenGLRenderer_SetupCamera(&camera,WIDTH,HEIGHT);
        DrawAnatomy(s);
    }
    if (ok) ok=OpenGLRenderer_SavePPM(path,WIDTH,HEIGHT);
    r->endFrame(r);
    return ok;
}

static bool Render(Renderer3D *r,const Subject *s,ICamera camera,const char *path,bool renderEyes)
{
    return RenderView(r,s,camera,path,renderEyes,false);
}

static float OrdinaryError(const HeadPhenotype *phenotype,Vector3 radii,const HeadAnatomy *reference)
{
    HeadAnatomy rebuilt;
    if (!HeadAnatomy_Resolve(phenotype,0,radii,&rebuilt)) return INFINITY;
    return OffspringMorph_HeadError(&rebuilt,1,reference,1);
}

static void FitOrdinary(const Subject *parent,const HeadAnatomy *reference)
{
    HeadPhenotype shape=parent->monster.phenotype.head;
    const BodyPart *host=&parent->monster.bodyParts[parent->monster.head.anatomy.attachmentBodyPartIndex];
    Vector3 r=Vec3_Create(host->width*.5f,host->height*.5f,host->length*.5f);
#define P(field) &shape.field
    float *controls[]={&r.x,&r.y,&r.z,P(skullWidth),P(skullHeight),P(skullLength),
        P(muzzleLength),P(muzzleWidth),P(muzzleTaper),P(rostrumDepth),P(rostrumDorsalSlope),
        P(temporalWidth),P(temporalDepth),P(jawLength),P(jawDepth),P(jawStrength),P(cheekMass),
        P(maxillaryWidth),P(eyeSize),P(eyeLaterality),P(eyeForwardness),P(eyeDorsality),P(earSize),
        P(earWidthRatio),P(earHeightRatio),P(earOutward)};
#undef P
    float initial=OrdinaryError(&shape,r,reference),best=initial;
    for (float step=.22f; step>.001f; step*=.5f) {
        for (unsigned pass=0; pass<9; ++pass) {
            for (size_t i=0; i<sizeof(controls)/sizeof(*controls); ++i) {
                float *parameter=controls[i],start=*parameter,chosen=start;
                for (int sign=-1; sign<=1; sign+=2) {
                    *parameter=start+sign*step;
                    if (*parameter<(i<3?.08f:0) || *parameter>(i<3?3.0f:(i==23||i==24?4.0f:1.0f))) continue;
                    float error=OrdinaryError(&shape,r,reference);
                    if (error<best) { best=error; chosen=*parameter; }
                }
                *parameter=chosen;
            }
        }
    }
    printf("%s %.5f -> %.5f\n",parent->monster.recipe.name,initial,best);
}

static bool RenderGallery(Subject *subjects, unsigned count, AABB3D shared[2], const char *directory,
    bool overview)
{
    OpenGLDemoWindow *window = OpenGLDemoWindow_Create("Offspring gallery",WIDTH,HEIGHT);
    if (!window) return false;
    const Vector3 directions[] = {{1,0,0},{0,0,1},{.8f,.24f,.65f},{.72f,.13f,.68f},{1,0,0}};
    const char *views[] = {"side","front","three-quarter","head","head-side"};
    ICamera initial = Camera(&subjects[0],shared[0],directions[0],false);
    Renderer3D renderer = OpenGLRenderer_Create(&initial);
    bool ok = true;
    for (unsigned i=0; i<count && ok; ++i) {
        const Subject *s = &subjects[i];
        ok = OpenGLRenderer_SetMorphSurfaceDomain(&renderer,&s->monster,&s->monster);
        for (unsigned mode=0; mode<(overview ? 1u : 3u) && ok; ++mode) {
            OpenGLRenderer_SetSurfaceDebug(&renderer,mode==1 ? 25 : mode==2 ? 27 : 0);
            unsigned viewsInMode = mode==2 ? 1 : 5;
            for (unsigned v=0; v<viewsInMode && ok; ++v) {
                char path[4096];
                unsigned view = mode==2 ? 2 : v;
                if (overview && view != 0 && view != 2 && view != 4) continue;
                const char *prefix = mode==1 ? "shape-" : mode==2 ? "albedo-" : "";
                snprintf(path,sizeof(path),"%s/%s%s-%u.ppm",directory,prefix,views[view],i);
                bool head = view>=3;
                ok = Render(&renderer,s,Camera(s,shared[head],directions[view],head),path,mode==0);
            }
        }
    }
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    return ok;
}

enum { GRID_SIDE=16, GRID_POINTS=GRID_SIDE*GRID_SIDE*GRID_SIDE, ALBEDO_POINTS=66 };

static bool Section(const Subject *s, ICamera camera, const char *path)
{
    FILE *output = fopen(path,"wb");
    if (!output) return false;
    unsigned char row[WIDTH*3];
    const MonsterSDFMouth *mouth = &s->field.mouths[0];
    MonsterSDFJawField context;
    SDFField jaw = MonsterSDF_GetJawField(&s->field,0,&context);
    float cs = cosf(mouth->visualJawAngle), sn = sinf(mouth->visualJawAngle);
    fprintf(output,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    for (unsigned y=0; y<HEIGHT; ++y) {
        for (unsigned x=0; x<WIDTH; ++x) {
            Vector3 p = camera.target;
            float step = -camera.fov/HEIGHT;
            p.x = 0;
            p.y += (HEIGHT*.5f-y)*step;
            p.z -= (x-WIDTH*.5f)*step;
            float upper = MonsterSDF_EvaluateDebug(&s->field,p,MONSTER_HEAD_DEBUG_UPPER_HEAD).distance;
            Vector3 local = Transform3D_ApplyRotationBasis(mouth->inverseRotation,Vec3_Sub(p,mouth->center));
            Vector3 v = Vec3_Sub(local,mouth->visualJawPivot);
            Vector3 rotated = {v.x,cs*v.y+sn*v.z,-sn*v.y+cs*v.z};
            Vector3 rest = Vec3_Add(mouth->visualJawPivot,Vec3_Scale(rotated,1/mouth->visualJawScale));
            float lower = jaw.evaluateDistance(jaw.context,rest);
            row[3*x] = lower<=0 ? 232 : 18;
            row[3*x+1] = upper<=0 ? 204 : lower<=0 ? 138 : 20;
            row[3*x+2] = upper<=0 ? 224 : 28;
        }
        if (fwrite(row,1,sizeof(row),output) != sizeof(row)) {
            fclose(output);
            return false;
        }
    }
    return fclose(output)==0;
}

static bool DiagnosticViews(Renderer3D *renderer,Subject *s,AABB3D shared[2],
    AABB3D insertion,AABB3D shoulder,AABB3D tail,const char *directory,unsigned i)
{
    Monster *monster = &s->monster;
    const Vector3 side = {1,0,0};
    ICamera head = Camera(s,shared[1],side,true);
    ICamera body = Camera(s,shared[0],side,false);
    ICamera root = Camera(s,insertion,(Vector3){.88f,.14f,.42f},true);
    ICamera limb = Camera(s,shoulder,(Vector3){.94f,.12f,.32f},false);
    char path[4096];
#define PATH(name) snprintf(path,sizeof(path),"%s/" name "-%u.ppm",directory,i)
    OpenGLRenderer_SetSurfaceDebug(renderer,0);
    PATH("insertion");
    if (!Render(renderer,s,root,path,true)) return false;
    OpenGLRenderer_SetSurfaceDebug(renderer,10);
    PATH("insertion-neutral");
    if (!Render(renderer,s,root,path,false)) return false;
    PATH("body-neutral");
    if (!Render(renderer,s,body,path,false)) return false;
    OpenGLRenderer_SetSurfaceDebug(renderer,25);
    PATH("insertion-normals");
    if (!RenderView(renderer,s,root,path,false,true)) return false;
    PATH("limb-chain");
    if (!RenderView(renderer,s,limb,path,false,true)) return false;
    if (isfinite(tail.start.x)) {
        ICamera caudal = Camera(s,tail,side,false);
        PATH("tail-root");
        if (!RenderView(renderer,s,caudal,path,false,true)) return false;
    }
    PATH("graph");
    if (!RenderView(renderer,s,body,path,false,true)) return false;
    PATH("landmarks");
    if (!RenderView(renderer,s,head,path,false,true)) return false;
    PATH("section-closed");
    if (!Section(s,head,path)) return false;

    Mesh mesh = {0};
    SDFMesher mesher = SDFMesher_Create(SDFMesher_DefaultConfig());
    SDFField field = MonsterSDF_GetField(&s->field);
    Vector3 start = Vec3_Add(s->origin[1],Vec3_Scale(insertion.start,s->scale));
    Vector3 end = Vec3_Add(s->origin[1],Vec3_Scale(insertion.end,s->scale));
    AABB3D box = {start,end};
    bool ok = SDFMesher_MeshRegion(&mesher,&field,box,-root.fov/75,&mesh);
    if (ok) {
        OpenGLRenderer_SetupCamera(&root,WIDTH,HEIGHT);
        OpenGLRenderer_SetWireframe(renderer,true);
        renderer->beginFrame(renderer);
        renderer->renderMesh(renderer,&mesh);
        PATH("insertion-wire");
        ok = OpenGLRenderer_SavePPM(path,WIDTH,HEIGHT);
        renderer->endFrame(renderer);
        OpenGLRenderer_SetWireframe(renderer,false);
    }
    Mesh_Free(&mesh);
    SDFMesher_Free(&mesher);
    if (!ok) return false;

    Monster_SetHeadOpenFactor(monster,.60f);
    if (!MonsterSDF_Build(&s->field,monster,MonsterSDF_DefaultConfig())) return false;
    PATH("section-open");
    if (!Section(s,head,path)) return false;
    PATH("jaw-open");
    if (!Render(renderer,s,head,path,false)) return false;
    Monster_SetHeadOpenFactor(monster,0);
    if (!monster->animation) return true;
    AnatomyGraph *rest = malloc(sizeof(*rest));
    if (!rest) return false;
    *rest = monster->anatomyGraph;
    const Skeleton *skeleton = &monster->animation->rig.skeleton;
    const BodyPlan *plan = &monster->recipe.bodyPlan;
    const char *gestures[] = {"curled","reaching","feeding"};
    for (unsigned pose=FLEXIBLE_POSE_CURLED; pose<FLEXIBLE_POSE_COUNT && ok; ++pose) {
        SkeletonPose *state = &monster->animation->pose;
        ok = SkeletonPose_ResetToRest(state,skeleton);
        bool flexible = false;
        for (size_t j=0; j<plan->moduleCount && ok; ++j) {
            const CreatureModuleInstance *m = &plan->modules[j];
            if (m->kind != CREATURE_MODULE_FLEXIBLE_APPENDAGE) continue;
            flexible = true;
            ok = FlexibleAppendage_ApplyPose(m->instanceId,pose,1,skeleton,state);
        }
        if (!flexible) break;
        ok = ok && SkeletonPose_ToAnatomy(skeleton,state,rest,&monster->anatomyGraph) &&
            MonsterSDF_Build(&s->field,monster,MonsterSDF_DefaultConfig());
        snprintf(path,sizeof(path),"%s/pose-%s-%u.ppm",directory,gestures[pose-1],i);
        ok = ok && Render(renderer,s,body,path,false);
    }
    monster->anatomyGraph = *rest;
    if (ok && monster->animation->rig.limbCount) {
        MonsterAnimation *animation = monster->animation;
        animation->animator.desiredVelocity = Vec3_Create(0,0,.55f*s->scale);
        for (unsigned frame=0; frame<32 && ok; ++frame)
            ok = MonsterAnimation_Update(monster,1.f/60);
        ok = ok && SkeletonPose_ToAnatomy(skeleton,&animation->pose,rest,&monster->anatomyGraph) &&
            MonsterSDF_Build(&s->field,monster,MonsterSDF_DefaultConfig());
        PATH("walk");
        ok = ok && RenderView(renderer,s,body,path,false,true);
        monster->anatomyGraph = *rest;
    }
    free(rest);
#undef PATH
    return ok;
}

static bool RenderDiagnostics(Subject *subjects,unsigned count,AABB3D shared[2],const char *directory)
{
    AABB3D insertion = AABB_Empty(), shoulder = AABB_Empty(), tail = AABB_Empty();
    for (unsigned i=0; i<count; ++i) {
        Subject *s = &subjects[i];
        const AnatomyGraph *graph = &s->monster.anatomyGraph;
        const HeadSurfaceRecipe *face = &s->monster.head.anatomy.surface;
        Vector3 origin = s->monster.bodyParts[s->monster.head.anatomy.attachmentBodyPartIndex].positionRender;
        AABB3D roots = AABB_Empty(), girdle = AABB_Empty(), caudal = AABB_Empty();
        AABB_ExpandRadius(&roots,Vec3_Add(origin,face->faceTip),face->faceTipRadii);
        for (size_t j=0; j<graph->nodeCount; ++j) {
            const AnatomyNode *n = &graph->nodes[j];
            Vector3 radius = {n->widthRadius,n->heightRadius,n->widthRadius};
            if (n->localNodeId == 1 &&
                (n->region == ANATOMY_REGION_ORNAMENT || n->region == ANATOMY_REGION_FLEXIBLE) &&
                Vec3_Distance(n->center,s->origin[1]) < 2.6f*s->scale)
                AABB_ExpandRadius(&roots,n->center,radius);
            if (n->region == ANATOMY_REGION_FORELIMB || n->region == ANATOMY_REGION_HINDLIMB)
                AABB_ExpandRadius(&girdle,n->center,radius);
            if (n->region == ANATOMY_REGION_TAIL && n->localNodeId<=2)
                AABB_ExpandRadius(&caudal,n->center,radius);
        }
        AABB_Pad(&roots,.13f*s->scale);
        UnionNormalized(&insertion,roots,s->origin[1],s->scale);
        if (isfinite(girdle.start.x)) UnionNormalized(&shoulder,girdle,s->origin[0],s->scale);
        if (isfinite(caudal.start.x)) {
            AABB_Pad(&caudal,.35f*s->scale);
            UnionNormalized(&tail,caudal,s->origin[0],s->scale);
        }
    }
    OpenGLDemoWindow *window = OpenGLDemoWindow_Create("Offspring anatomy",WIDTH,HEIGHT);
    if (!window) return false;
    ICamera initial = Camera(&subjects[0],shared[1],(Vector3){1,0,0},true);
    Renderer3D renderer = OpenGLRenderer_Create(&initial);
    bool ok = true;
    for (unsigned i=0; i<count && ok; ++i) {
        Subject *s = &subjects[i];
        ok = OpenGLRenderer_SetMorphSurfaceDomain(&renderer,&s->monster,&s->monster) &&
             DiagnosticViews(&renderer,s,shared,insertion,isfinite(shoulder.start.x) ? shoulder : shared[0],
                 tail,directory,i);
    }
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    return ok;
}

static void GalleryGrid(const Subject *s, AABB3D frame, float grid[GRID_POINTS])
{
    MonsterSDFHeadField context;
    SDFField field=MonsterSDF_GetHeadField(&s->field,0,&context);
    Vector3 size=AABB_Size(frame);
    size_t at=0;
    for (unsigned z=0; z<GRID_SIDE; ++z) for (unsigned y=0; y<GRID_SIDE; ++y)
        for (unsigned x=0; x<GRID_SIDE; ++x) {
            float unit=1.f/(GRID_SIDE-1);
            Vector3 p=Vec3_Add(frame.start,Vec3_Create(size.x*x*unit,size.y*y*unit,size.z*z*unit));
            grid[at++]=field.evaluateDistance(field.context,Vec3_Add(s->origin[1],Vec3_Scale(p,s->scale)))/s->scale;
        }
}

static float GridDistance(const float a[GRID_POINTS], const float b[GRID_POINTS], float size)
{
    float band=size*.08f,sum=0;
    unsigned count=0;
    for (size_t i=0; i<GRID_POINTS; ++i) {
        if (fabsf(a[i])>band && fabsf(b[i])>band) continue;
        float distance=fminf(fabsf(a[i]-b[i]),band*3);
        sum+=distance*distance;
        ++count;
    }
    return count ? sqrtf(sum/count)/size : NAN;
}

static void GalleryAlbedo(const Subject *s, Vector3 colors[ALBEDO_POINTS])
{
    const SurfacePhenotype *surface=&s->monster.surface;
    unsigned at=0;
    for (unsigned region=0; region<SURFACE_REGION_COUNT; ++region)
        for (unsigned i=0; i<6; ++i) {
            SurfaceCoordinate p={.position={.18f+.18f*(i%3),.38f*(i%2),.32f*i},
                .region=region,.secondaryRegion=region,.ventral=region==SURFACE_REGION_VENTRAL_TRUNK};
            colors[at++]=Surface_EvaluatePigmentAlbedo(surface,&p);
        }
}

static float AppearanceDistance(const Vector3 a[ALBEDO_POINTS], const Vector3 b[ALBEDO_POINTS])
{
    float sum=0;
    for (size_t i=0; i<ALBEDO_POINTS; ++i) sum+=Vec3_LengthSq(Vec3_Sub(a[i],b[i]));
    return sqrtf(sum/(3*ALBEDO_POINTS));
}

/* Corte transversal por detrás del plano distal y por el centro de ambas narinas. */
static bool NasalSection(const Subject *s,const char *path)
{
    const MonsterSDFMouth *m=&s->field.mouths[0];
    FILE *out=fopen(path,"wb");
    if(!out)return false;
    fprintf(out,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    Vector3 center=Vec3_Lerp(m->leftNostrilCenterLocal,m->rightNostrilCenterLocal,.5f);
    Vector3 side=m->nasalTerminalWeight>0?m->nostrilSideLocal:Vec3_Create(1,0,0);
    Vector3 forward=m->nasalTerminalWeight>0?m->nostrilForwardLocal:Vec3_Create(0,0,1);
    Vector3 up=Vec3_Cross(forward,side);
    center=Vec3_Sub(center,Vec3_Scale(forward,m->nostrilRadii.z*.28f));
    float width=fmaxf(Vec3_Distance(m->leftNostrilCenterLocal,m->rightNostrilCenterLocal)+
        4*m->nostrilRadii.x,.005f);
    unsigned char row[WIDTH*3];
    for(unsigned y=0;y<HEIGHT;++y) {
        for(unsigned x=0;x<WIDTH;++x) {
            Vector3 p=Vec3_Add(center,Vec3_Add(Vec3_Scale(side,width*(x-WIDTH*.5f)/WIDTH),
                Vec3_Scale(up,width*(HEIGHT*.5f-y)/WIDTH)));
            Vector3 world=MouthWorld(m,p);
            float distance=MonsterSDF_EvaluateDistance(&s->field,world);
            float cut=MonsterSDF_EvaluateDebug(&s->field,world,MONSTER_HEAD_DEBUG_NOSTRILS).distance;
            row[3*x]=cut<0?231:distance<0?101:18;
            row[3*x+1]=cut<0?121:distance<0?190:20;
            row[3*x+2]=cut<0?177:distance<0?212:28;
        }
        if(fwrite(row,1,sizeof(row),out)!=sizeof(row)) {fclose(out);return false;}
    }
    return fclose(out)==0;
}

static void FacialMetrics(const Subject *s,unsigned index,float age)
{
    const MonsterSDFMouth *mouth=&s->field.mouths[0];
    const MonsterSDFConnector *root=NULL,*tip=NULL;
    float length=0;
    for(size_t j=0;j<s->field.connectorCount;++j) {
        const MonsterSDFConnector *c=&s->field.connectors[j];
        if(c->nasalTerminal)tip=c;
    }
    if(tip)for(size_t j=0;j<s->field.connectorCount;++j) {
        const MonsterSDFConnector *c=&s->field.connectors[j];
        if(c->moduleInstanceId!=tip->moduleInstanceId || (uint16_t)c->toId>FLEXIBLE_APPENDAGE_STATIONS)
            continue;
        if(!root)root=c;
        length+=c->length;
    }
    Vector3 left=mouth->leftNostrilCenterLocal;
    Vector3 right=mouth->rightNostrilCenterLocal;
    Vector3 septum=Vec3_Lerp(left,right,.5f);
    float insideL=MonsterSDF_EvaluateDistance(&s->field,MouthWorld(mouth,left));
    float insideR=MonsterSDF_EvaluateDistance(&s->field,MouthWorld(mouth,right));
    Vector3 forward=mouth->nasalTerminalWeight>0?mouth->nostrilForwardLocal:Vec3_Create(0,0,1);
    septum=Vec3_Sub(septum,Vec3_Scale(forward,mouth->nostrilRadii.z*.24f));
    float between=MonsterSDF_EvaluateDistance(&s->field,MouthWorld(mouth,septum));
    float rootDepth=root?MonsterSDF_EvaluateDebug(&s->field,root->a,MONSTER_HEAD_DEBUG_UPPER_HEAD).distance:0;
    printf("nasal\t%u\t%.3f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
        index,age,length,root?root->widthA:0,root?root->heightA:0,rootDepth,
        left.x,left.y,left.z,mouth->nasalTerminalWeight,insideL,insideR,between);
}

static bool RenderFaces(Subject *subjects,unsigned count,AABB3D frame,const char *directory,
    bool diagnostics)
{
    const Vector3 directions[]={{1,0,0},{0,0,1},{.8f,.24f,.65f},{0,1,0}};
    const char *names[]={"side","front","three-quarter","top"};
    OpenGLDemoWindow *window=OpenGLDemoWindow_Create("Perfil facial",WIDTH,HEIGHT);
    if(!window)return false;
    ICamera initial=Camera(&subjects[0],frame,directions[0],true);
    Renderer3D renderer=OpenGLRenderer_Create(&initial);
    bool ok=true;
    for(unsigned i=0;i<count&&ok;++i) {
        Subject *s=&subjects[i];
        ok=OpenGLRenderer_SetMorphSurfaceDomain(&renderer,&s->monster,&s->monster);
        for(unsigned v=0;v<4&&ok;++v) {
            char path[4096];
            ICamera camera=Camera(s,frame,directions[v],true);
            if(v==0 || v==2 || diagnostics) {
                OpenGLRenderer_SetSurfaceDebug(&renderer,0);
                snprintf(path,sizeof(path),"%s/%s-%u.ppm",directory,names[v],i);
                ok=Render(&renderer,s,camera,path,true);
            }
            OpenGLRenderer_SetSurfaceDebug(&renderer,10);
            snprintf(path,sizeof(path),"%s/neutral-%s-%u.ppm",directory,names[v],i);
            ok=ok&&Render(&renderer,s,camera,path,false);
        }
        if(diagnostics&&ok) {
            char path[4096];
            ICamera side=Camera(s,frame,directions[0],true);
            snprintf(path,sizeof(path),"%s/stations-%u.ppm",directory,i);
            ok=RenderView(&renderer,s,side,path,false,true);
            if(ok) {
                snprintf(path,sizeof(path),"%s/section-%u.ppm",directory,i);
                ok=Section(s,side,path);
            }
            if(ok) {
                snprintf(path,sizeof(path),"%s/nasal-section-%u.ppm",directory,i);
                ok=NasalSection(s,path);
            }
        }
    }
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    return ok;
}

static void IncludeNasalExtent(const Subject *s,AABB3D *frame)
{
    AABB3D bounds=AABB_Empty();
    for(size_t i=0;i<s->field.connectorCount;++i) {
        const MonsterSDFConnector *c=&s->field.connectors[i];
        if(c->region!=ANATOMY_REGION_FLEXIBLE)continue;
        AABB_ExpandPoint(&bounds,c->bounds.start);
        AABB_ExpandPoint(&bounds,c->bounds.end);
    }
    if(bounds.start.x<=bounds.end.x)UnionNormalized(frame,bounds,s->origin[1],s->scale);
}

static float EndpointDifference(const MonsterSDF *parent,const MonsterSDF *morph)
{
    float maximum=0;
    Vector3 size=AABB_Size(parent->bounds);
    for(unsigned x=0;x<12;++x)for(unsigned y=0;y<12;++y)for(unsigned z=0;z<12;++z) {
        Vector3 p=Vec3_Add(parent->bounds.start,Vec3_Create(size.x*x/11,size.y*y/11,size.z*z/11));
        float difference=fabsf(MonsterSDF_EvaluateVisualDistance(parent,p)-
            MonsterSDF_EvaluateVisualDistance(morph,p));
        maximum=fmaxf(maximum,difference);
    }
    return maximum;
}

static bool NasalMorph(const char *directory,const char *from,const char *to)
{
    static const float ages[]={0,.10f,.25f,.40f,.50f,.60f,.75f,.90f,1};
    enum { SAMPLES=sizeof(ages)/sizeof(*ages) };
    Subject *subjects=calloc(SAMPLES,sizeof(*subjects));
    Subject *parents=calloc(2,sizeof(*parents));
    if(!subjects || !parents) {free(subjects);free(parents);return false;}
    AABB3D frame[]={AABB_Empty(),AABB_Empty()};
    bool ok=Load(&parents[0],from)&&Load(&parents[1],to);
    for(unsigned i=0;i<2&&ok;++i)
        ok=MonsterSDF_Build(&parents[i].field,&parents[i].monster,MonsterSDF_DefaultConfig());
    puts("kind\tindex\tage\tlength\troot_width\troot_height\troot_sdf\tleft_x\tleft_y\tleft_z\tterminal_weight\tleft_sdf\tright_sdf\tseptum_sdf");
    for(unsigned i=0;i<SAMPLES&&ok;++i) {
        MonsterAger_Interpolate(&parents[0].monster,&parents[1].monster,ages[i],&subjects[i].monster);
        ok=subjects[i].monster.hasResolvedMorph && Compile(&subjects[i],frame);
        if(ok) {
            IncludeNasalExtent(&subjects[i],&frame[1]);
            FacialMetrics(&subjects[i],i,ages[i]);
            if(i==0 || i==SAMPLES-1)
                printf("endpoint\t%.3f\t%.8g\n",ages[i],
                    EndpointDifference(&parents[i==0?0:1].field,&subjects[i].field));
        }
    }
    if(ok)ok=RenderFaces(subjects,SAMPLES,frame[1],directory,true);
    for(unsigned i=0;i<SAMPLES;++i) {
        MonsterSDF_Free(&subjects[i].field);
        Monster_Free(&subjects[i].monster);
    }
    for(unsigned i=0;i<2;++i) {
        MonsterSDF_Free(&parents[i].field);
        Monster_Free(&parents[i].monster);
    }
    free(parents);free(subjects);
    return ok;
}

static bool Gallery(const char *directory, unsigned count, char **files, const char *mode)
{
    if (count < 2 || count > 2*CREATURE_OFFSPRING_MAX+2) return false;
    Subject *subjects = calloc(count,sizeof(*subjects));
    float (*grid)[GRID_POINTS] = calloc(count,sizeof(*grid));
    Vector3 (*albedo)[ALBEDO_POINTS] = calloc(count,sizeof(*albedo));
    if (!subjects || !grid || !albedo) {
        free(subjects); free(grid); free(albedo);
        return false;
    }
    AABB3D shared[] = {AABB_Empty(),AABB_Empty()};
    bool ok = false;
    for (unsigned i=0; i<count; ++i) {
        if (!Load(&subjects[i],files[i]) || !Compile(&subjects[i],shared)) {
            fprintf(stderr,"No se pudo cargar %s\n",files[i]);
            goto done;
        }
    }
    if(!strcmp(mode,"--faces")) {
        for(unsigned i=0;i<count;++i) {
            IncludeNasalExtent(&subjects[i],&shared[1]);
            FacialMetrics(&subjects[i],i,1);
        }
        ok=RenderFaces(subjects,count,shared[1],directory,false);
        goto done;
    }
    for (unsigned i=0; i<count; ++i) {
        const HeadAnatomy *head=&subjects[i].monster.head.anatomy;
        HeadResolvedMeasurements m=HeadAnatomy_Measure(head);
        const HeadSurfaceRecipe *s=&head->surface;
        float length=m.actualHeadLength;
        printf("descriptor\t%u\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",i,m.actualWidthLengthRatio,
            (s->faceTip.z-s->faceRoot.z)/length,head->oralSystem.jawThickness/length,
            s->earShape.y/length,m.interEyeDistance/length);
        GalleryGrid(&subjects[i],shared[1],grid[i]);
        GalleryAlbedo(&subjects[i],albedo[i]);
    }
    float diameter=Vec3_Length(AABB_Size(shared[1]));
    puts("i\tj\thead_sdf\thead_landmarks\tbody_stations\tlimb_ratios\tappearance");
    for (unsigned i=0; i<count; ++i) for (unsigned j=i+1; j<count; ++j) {
        const Subject *a = &subjects[i], *b = &subjects[j];
        OffspringReport ab = {.signature=Creature_MorphologicalSignature(&a->monster)};
        OffspringReport ba = {.signature=Creature_MorphologicalSignature(&b->monster)};
        OffspringMorph_Measure(&a->monster,&b->monster,&ab);
        OffspringMorph_Measure(&b->monster,&a->monster,&ba);
        printf("%u\t%u\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",i,j,
            GridDistance(grid[i],grid[j],diameter),.5f*(ab.headError+ba.headError),
            .5f*(ab.bodyError+ba.bodyError),.5f*(ab.limbError+ba.limbError),
            AppearanceDistance(albedo[i],albedo[j]));
    }
    ok = !strcmp(mode,"--metrics") || (!strcmp(mode,"--anatomy") ?
        RenderDiagnostics(subjects,count,shared,directory) :
        RenderGallery(subjects,count,shared,directory,!strcmp(mode,"--overview")));
done:
    for (unsigned i=0; i<count; ++i) {
        MonsterSDF_Free(&subjects[i].field);
        Monster_Free(&subjects[i].monster);
    }
    free(subjects); free(grid); free(albedo);
    return ok;
}

int main(int argc,char **argv)
{
    if(argc==5&&!strcmp(argv[1],"--nasal-morph"))
        return NasalMorph(argv[2],argv[3],argv[4]) ? 0 : 1;
    if (argc>4 && (!strcmp(argv[1],"--gallery") || !strcmp(argv[1],"--faces") || !strcmp(argv[1],"--anatomy") ||
        !strcmp(argv[1],"--metrics") || !strcmp(argv[1],"--overview")))
        return Gallery(argv[2],argc-3,argv+3,argv[1]) ? 0 : 1;
    bool onlyReference=argc==8 && !strcmp(argv[7],"--reference");
    bool roundtrip=argc==8 && !strcmp(argv[7],"--roundtrip");
    bool ordinary=argc==8 && !strcmp(argv[7],"--ordinary-fit");
    if (argc!=7 && !onlyReference && !roundtrip && !ordinary) {
        fprintf(stderr,"Uso: %s padreX padreY receta-v1 receta-v2 edad-morph directorio [--reference|--roundtrip|--ordinary-fit]\n",argv[0]);
        return 2;
    }
    char *end;
    float age=strtof(argv[5],&end);
    if (*end || !isfinite(age) || age<0 || age>1) return 2;
    Subject *subjects=calloc(COLUMNS,sizeof(*subjects));
    if (!subjects) return 1;
    AABB3D shared[]={AABB_Empty(),AABB_Empty()};
    bool ok=false;
    CreatureOffspringBatch *batch=NULL;
    if (!Load(&subjects[0],argv[1]) || !Load(&subjects[4],argv[2]) ||
        (!roundtrip && !Load(&subjects[2],argv[3])) || !Load(&subjects[3],argv[4]) ||
        !CreatureMorph_Interpolate(&subjects[0].monster,&subjects[4].monster,age,&subjects[1].monster)) goto done;
    if (roundtrip) {
        batch=CreatureOffspring_Generate(&subjects[0].monster.recipe,&subjects[4].monster.recipe,
            CreatureOffspring_Default(5,48216),NULL,NULL,NULL,0);
        const CreatureRecipe *live=CreatureOffspring_Get(batch,2,NULL);
        if (!live || !Creature_BuildMonster(&subjects[2].monster,live,&live->adult)) goto done;
    }
    for (unsigned i=0; i<COLUMNS; ++i)
        if (!Compile(&subjects[i],shared)) goto done;
    printf("Morph %.5f, peso geométrico %.5f\n",age,CreatureMorph_GeometricWeight(age));
    puts("subject head-landmarks body-stations limb-ratios head-sdf");
    for (unsigned i=0; i<COLUMNS; ++i) {
        OffspringReport report={.signature=Creature_MorphologicalSignature(&subjects[i].monster)};
        OffspringMorph_Measure(&subjects[i].monster,&subjects[1].monster,&report);
        printf("%u %.5f %.5f %.5f %.5f\n",i,report.headError,report.bodyError,report.limbError,
            HeadFieldError(&subjects[i],&subjects[1],shared[1]));
    }
    if (ordinary) {
        puts("Ajuste con 26 controles ordinarios y arquetipo fijo");
        FitOrdinary(&subjects[0],&subjects[1].monster.head.anatomy);
        FitOrdinary(&subjects[4],&subjects[1].monster.head.anatomy);
        ok=true;
        goto done;
    }
    OpenGLDemoWindow *window=OpenGLDemoWindow_Create("Offspring comparison",WIDTH,HEIGHT);
    if (!window) goto done;
    ICamera initial=Camera(&subjects[0],shared[0],Vec3_Create(1,0,0),false);
    Renderer3D renderer=OpenGLRenderer_Create(&initial);
    const Vector3 directions[]={{1,0,0},{0,0,1},{.8f,.24f,.65f},{.72f,.13f,.68f},{1,0,0}};
    const char *views[]={"side","front","three-quarter","head","head-side"};
    ok=true;
    for (unsigned i=0; i<COLUMNS && ok; ++i) {
        if (onlyReference && i!=1) continue;
        const Subject *s=&subjects[i];
        ok=OpenGLRenderer_SetMorphSurfaceDomain(&renderer,&s->monster,&s->monster);
        for (unsigned v=0; v<5 && ok; ++v) {
            char path[1024];
            snprintf(path,sizeof(path),"%s/%s-%u.ppm",argv[6],views[v],i);
            bool head=v>=3;
            ok=Render(&renderer,s,Camera(s,shared[head],directions[v],head),path,true);
        }
    }
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
done:
    for (unsigned i=0; i<COLUMNS; ++i) {
        MonsterSDF_Free(&subjects[i].field);
        Monster_Free(&subjects[i].monster);
    }
    CreatureOffspring_Destroy(batch);
    free(subjects);
    return ok?0:1;
}
