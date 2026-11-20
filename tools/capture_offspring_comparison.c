/** Captura cinco recetas y su referencia en un encuadre anatómico común. */
#include "AxialBody.h"
#include "CreatureMorph.h"
#include "CreatureRecipeIO.h"
#include "MonsterVisual.h"
#include "OpenGLRenderer.h"
#include "../src/OffspringMorphFitter.h"
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
    Vector3 right=Vec3_Normalize(Vec3_Cross(Vec3_Create(0,1,0),forward));
    Vector3 up=Vec3_Cross(forward,right);
    Vector3 size=AABB_Size(bounds);
    float x=fabsf(right.x)*size.x+fabsf(right.y)*size.y+fabsf(right.z)*size.z;
    float y=fabsf(up.x)*size.x+fabsf(up.y)*size.y+fabsf(up.z)*size.z;
    float height=fmaxf(x*HEIGHT/WIDTH,y)*1.15f*s->scale;
    Vector3 center=Vec3_Add(s->origin[head],Vec3_Scale(Vec3_Add(bounds.start,bounds.end),.5f*s->scale));
    return (ICamera){.target=center,.position=Vec3_Add(center,Vec3_Scale(forward,24*s->scale)),
        .up={0,1,0},.fov=-height,.aspectRatio=(float)WIDTH/HEIGHT,.nearPlane=.01f,.farPlane=100};
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

static bool Render(Renderer3D *r,const Subject *s,ICamera camera,const char *path)
{
    MonsterVisualEye eyes[2]={0};
    OpenGLRenderer_SetupCamera(&camera,WIDTH,HEIGHT);
    r->beginFrame(r);
    bool ok=OpenGLRenderer_BeginSDFFrame(r,&camera,WIDTH,HEIGHT,false);
    if (ok) {
        ok=OpenGLRenderer_RenderSDF(r,&s->field,&camera,WIDTH,HEIGHT);
        ok=OpenGLRenderer_EndSDFFrame(r,WIDTH,HEIGHT)&&ok;
    }
    if (ok && s->monster.eyeCount==2) {
        OpenGLRenderer_SetupCamera(&camera,WIDTH,HEIGHT);
        ok=MonsterVisual_UpdateEyeInstances(eyes,2,&s->monster);
        if (ok) for (unsigned i=0; i<2; ++i) r->renderEye(r,&eyes[i]);
    }
    for (unsigned i=0; i<2; ++i) Mesh_Free(&eyes[i].globe);
    if (ok) ok=OpenGLRenderer_SavePPM(path,WIDTH,HEIGHT);
    r->endFrame(r);
    return ok;
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

int main(int argc,char **argv)
{
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
            ok=Render(&renderer,s,Camera(s,shared[head],directions[v],head),path);
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
