#define _POSIX_C_SOURCE 200809L
#include "CreatureOffspring.h"
#include "SurfaceMapper.h"
#include "../src/interface/UiCreatureScene.h"
#include "../src/interface/UiCreatureOrbit.h"
#include "interface/UiRecipeCatalog.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const UiVfsNode *Find(const UiVfs *fs, UiVfsId folder, const char *name)
{
    const UiVfsNode *node = UiVfs_Get(fs,UiVfs_FindChild(fs,folder,name));
    assert(node);
    return node;
}

static void CheckField(const UiCreatureScene *scene)
{
    const Monster *monster = UiCreatureScene_Monster(scene);
    assert(monster->hasAnatomyGraph && monster->anatomyGraph.nodeCount &&
           (monster->eyeCount || scene->sdf.hasMedusaBell));
    assert(scene->sdf.hasSurface);
    assert(scene->sdf.bodyPartCount || scene->sdf.connectorCount ||
           scene->sdf.axialStationCount || scene->sdf.hasMedusaBell);
    AABB3D bounds = scene->sdf.bounds;
    assert(isfinite(bounds.start.x) && isfinite(bounds.end.x) && bounds.start.x < bounds.end.x);
    assert(isfinite(bounds.start.y) && isfinite(bounds.end.y) && bounds.start.y < bounds.end.y);
    assert(isfinite(bounds.start.z) && isfinite(bounds.end.z) && bounds.start.z < bounds.end.z);
}

static void Write(const char *path, const char *text)
{
    FILE *file = fopen(path,"wb");
    assert(file && fputs(text,file) >= 0 && fclose(file) == 0);
}

static void InvalidFullRecipe(void)
{
    char root[] = "/tmp/nexus-engine-XXXXXX";
    assert(mkdtemp(root));
    char demos[256], recipes[256], marker[256], manifest[256], file[256];
    snprintf(demos,sizeof(demos),"%s/demos",root);
    snprintf(recipes,sizeof(recipes),"%s/recipes",root);
    snprintf(marker,sizeof(marker),"%s/demos/demo_morph_species.c",root);
    snprintf(manifest,sizeof(manifest),"%s/recipes/catalog.txt",root);
    snprintf(file,sizeof(file),"%s/recipes/invalid.creature.json",root);
    assert(mkdir(demos,0700) == 0 && mkdir(recipes,0700) == 0);
    Write(marker,"/* fixture */\n");
    Write(manifest,"invalid.creature.json\n");
    Write(file,"{\"version\":1,\"recipe\":{\"id\":1008,\"name\":\"Incomplete\"}}");
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId folder = UiVfs_Add(fs,UiVfs_Root(fs),"recepies",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
    UiRecipeCatalogReport report = UiRecipeCatalog_LoadProject(fs,folder,demos);
    assert(report.external == 1);
    const UiVfsNode *node = Find(fs,folder,"invalid.creature.json");
    char error[256];
    assert(!UiCreatureScene_Create(demos,node,NULL,error,sizeof(error)));
    assert(strstr(error,"Recipe validation failed"));
    assert(unlink(file) == 0 && unlink(manifest) == 0 && unlink(marker) == 0);
    assert(rmdir(demos) == 0 && rmdir(recipes) == 0 && rmdir(root) == 0);
    UiVfs_Destroy(fs);
}

static void Orbit(void)
{
    UiCreatureOrbit orbit = UiCreatureOrbit_Default();
    assert(!orbit.dirty && !orbit.dragging);
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_PRESS,.x=100,.y=100,.clicks=1});
    assert(orbit.dragging && !orbit.dirty);
    for (int i=1; i<=100; ++i) {
        UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_POINTER_MOVE,.x=100+i*9,.y=800});
        assert(fabsf(orbit.yaw) <= 3.141593f);
    }
    assert(fabsf(orbit.yaw - remainderf(UI_ORBIT_DEFAULT_YAW - 10.8f,6.28318531f)) < .00002f);
    assert(orbit.dirty && orbit.pitch == 1.42f);
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_POINTER_MOVE,.x=100,.y=-200});
    assert(orbit.pitch == -1.42f && fabsf(orbit.yaw-UI_ORBIT_DEFAULT_YAW) < .00002f);
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_POINTER_RELEASE});
    orbit.dirty = false;
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_POINTER_MOVE,.x=500,.y=50});
    assert(!orbit.dragging && !orbit.dirty);
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_WHEEL,.preciseWheel=.5f});
    assert(orbit.zoom < UI_ORBIT_DEFAULT_ZOOM && orbit.zoom > .55f && orbit.dirty);
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_WHEEL,.wheel=100});
    assert(orbit.zoom == .55f);
    orbit.dirty = false;
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_WHEEL,.wheel=1});
    assert(!orbit.dirty);
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_WHEEL,.wheel=-100});
    assert(orbit.zoom == 1.85f);
    UiCreatureOrbit_Input(&orbit,&(UiEvent){.type=UI_PRESS,.clicks=2});
    assert(orbit.yaw == UI_ORBIT_DEFAULT_YAW && orbit.pitch == UI_ORBIT_DEFAULT_PITCH &&
           orbit.zoom == UI_ORBIT_DEFAULT_ZOOM && orbit.dirty && !orbit.dragging);
}

static void OrbitFraming(void)
{
    const AABB3D bounds = {.start={-2,1,-5},.end={1,4,7}};
    const float aspects[] = {.45f,1.0f,1.7f};
    const float zooms[] = {UI_ORBIT_MIN_ZOOM,UI_ORBIT_DEFAULT_ZOOM,UI_ORBIT_MAX_ZOOM};
    UiCreatureOrbit orbit = UiCreatureOrbit_Default();
    for (size_t a=0; a<sizeof(aspects)/sizeof(aspects[0]); ++a) {
        float previousHeight = 0;
        for (size_t z=0; z<sizeof(zooms)/sizeof(zooms[0]); ++z) {
            orbit.zoom = zooms[z];
            ICamera reference = UiCreatureOrbit_Camera(&orbit,bounds,aspects[a]);
            assert(-reference.fov > previousHeight);
            previousHeight = -reference.fov;
            float distance = Vec3_Length(Vec3_Sub(reference.target,reference.position));
            for (int pitch=-1; pitch<=1; ++pitch) {
                orbit.pitch = pitch*1.42f;
                for (int angle=0; angle<32; ++angle) {
                    orbit.yaw = angle*(6.28318531f/32);
                    ICamera camera = UiCreatureOrbit_Camera(&orbit,bounds,aspects[a]);
                    assert(camera.fov == reference.fov && camera.aspectRatio == aspects[a]);
                    assert(camera.target.x == -.5f && camera.target.y == 2.5f && camera.target.z == 1);
                    Vector3 offset = Vec3_Sub(camera.target,camera.position);
                    assert(fabsf(Vec3_Length(offset)-distance) < .0001f);
                    Vector3 forward = Vec3_Normalize(offset);
                    Vector3 right = Vec3_Normalize(Vec3_Cross(forward,camera.up));
                    Vector3 up = Vec3_Cross(right,forward);
                    float halfHeight = -.5f*camera.fov;
                    for (int i=0; i<8; ++i) {
                        Vector3 corner = {(i&1)?bounds.end.x:bounds.start.x,
                                          (i&2)?bounds.end.y:bounds.start.y,
                                          (i&4)?bounds.end.z:bounds.start.z};
                        Vector3 delta = Vec3_Sub(corner,camera.position);
                        ICamera preview = UiCreatureOrbit_PreviewCamera(&orbit,bounds,aspects[a]);
                    assert(preview.fov == reference.fov && preview.aspectRatio == aspects[a]);
                    float previewHalf = -.5f*preview.fov;
                    assert(fabsf(Vec3_Dot(delta,right)) < previewHalf*aspects[a]);
                    assert(fabsf(Vec3_Dot(delta,up)) < previewHalf);
                    assert(fabsf(Vec3_Dot(delta,right)) < halfHeight*aspects[a]);
                        assert(fabsf(Vec3_Dot(delta,up)) < halfHeight);
                        float depth = Vec3_Dot(delta,forward);
                        assert(depth > camera.nearPlane && depth < camera.farPlane);
                    }
                }
            }
        }
    }
}

static void SpeciesPairs(const UiVfs *fs, UiVfsId folder)
{
    const UiVfsNode *rhino=Find(fs,folder,"rinoceronte.creature.json");
    const UiVfsNode *rat=Find(fs,folder,"rata.creature.json");
    const UiVfsNode *pairs[][2] = {
        {rhino,Find(fs,folder,"elefante.creature.json")},
        {rhino,Find(fs,folder,"brumacerno.creature.json")},
        {rat,Find(fs,folder,"Perro.recipe")},
        {rat,rhino}
    };
    char error[256];
    for (size_t i=0;i<sizeof(pairs)/sizeof(pairs[0]);++i) {
        for (int direction=0;direction<2;++direction) {
            UiCreatureScene *scene=UiCreatureScene_Create("demos",pairs[i][direction],
                pairs[i][1-direction],error,sizeof(error));
            if(!scene) fprintf(stderr,"%s\n",error);
            assert(scene);
            const float ages[]={0,.2f,.5f,.8f,1,.8f,.5f,.2f,0};
            for (size_t k=0;k<sizeof(ages)/sizeof(ages[0]);++k) {
                assert(UiCreatureScene_SetAge(scene,ages[k]));
                CheckField(scene);
                if (pairs[i][0]==rat && pairs[i][1]==rhino && ages[k]>0 && ages[k]<1) {
                    const AnatomyGraph *graph=&UiCreatureScene_Monster(scene)->anatomyGraph;
                    assert(graph->separateMorphTail);
                    const AnatomyNode *pelvis=AnatomyGraph_FindFirstRegion(graph,ANATOMY_REGION_PELVIS);
                    const AnatomyNode *tail=AnatomyGraph_FindFirstRegion(graph,ANATOMY_REGION_TAIL);
                    assert(pelvis && tail);
                    const SDFSweepStation *last=&scene->sdf.axialStations[scene->sdf.axialStationCount-1];
                    assert(last->center.z>pelvis->center.z-pelvis->widthRadius);
                    assert(MonsterSDF_EvaluateDistance(&scene->sdf,tail->center)<0);
                }
                unsigned builds=scene->fieldBuilds;
                assert(UiCreatureScene_SetAge(scene,ages[k]) && scene->fieldBuilds==builds);
            }
            UiCreatureScene_Destroy(scene);
        }
    }
}

static void FiveNonMammals(const UiVfs *fs, UiVfsId folder)
{
    const struct {
        const char *file;
        bool expectBell;
        bool expectDetail;
        bool expectTranslucent;
    } cases[] = {
        {"halcon_peregrino.creature.json", false, true, false},
        {"mantarraya.creature.json", false, true, false},
        {"pulpo_gigante.creature.json", false, true, false},
        {"libelula_emperador.creature.json", false, true, true},
        {"medusa_ortiga.creature.json", true, true, true},
    };
    char error[256];
    SurfaceMorphDomain *domain = malloc(sizeof(*domain));
    assert(domain);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const UiVfsNode *node = Find(fs, folder, cases[i].file);
        UiCreatureScene *scene = UiCreatureScene_Create("demos", node, NULL, error, sizeof(error));
        if (!scene) fprintf(stderr, "%s: %s\n", cases[i].file, error);
        assert(scene);
        CheckField(scene);
        assert(scene->sdf.hasMedusaBell == cases[i].expectBell);
        assert((scene->detail.indexCount > 0) == cases[i].expectDetail);
        assert((scene->translucent.indexCount > 0) == cases[i].expectTranslucent);
        const Monster *m = UiCreatureScene_Monster(scene);
        assert(SurfaceMapper_BuildMorphDomain(domain, &m->anatomyGraph, &m->anatomyGraph, &m->surfaceMapping));
        assert(domain->count > 0);
        UiCreatureScene_Destroy(scene);

        UiCreatureScene *aging = UiCreatureScene_Create("demos", node, node, error, sizeof(error));
        if (!aging) fprintf(stderr, "%s aging: %s\n", cases[i].file, error);
        assert(aging);
        const float ages[] = {0.0f, 0.5f, 1.0f};
        for (size_t k = 0; k < sizeof(ages) / sizeof(ages[0]); ++k) {
            assert(UiCreatureScene_SetAge(aging, ages[k]));
            CheckField(aging);
            assert(aging->sdf.hasMedusaBell == cases[i].expectBell);
            assert((aging->detail.indexCount > 0) == cases[i].expectDetail);
            assert((aging->translucent.indexCount > 0) == cases[i].expectTranslucent);
            const Monster *cur = UiCreatureScene_Monster(aging);
            assert(SurfaceMapper_BuildMorphDomain(domain, &cur->anatomyGraph, &aging->reference.anatomyGraph, &cur->surfaceMapping));
            assert(domain->count > 0);
        }
        UiCreatureScene_Destroy(aging);
    }
    free(domain);
}

int main(void)
{
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId folder = UiVfs_Add(fs,UiVfs_Root(fs),"recepies",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
    UiRecipeCatalogReport report = UiRecipeCatalog_LoadProject(fs,folder,"demos");
    assert(report.manifestAvailable);
    const UiVfsNode *dog = Find(fs,folder,"Perro.recipe");
    const UiVfsNode *elephant = Find(fs,folder,"elefante.creature.json");
    const UiVfsNode *pig = Find(fs,folder,"cerdo.creature.json");
    const UiVfsNode *horse = Find(fs,folder,"caballo.creature.json");
    char error[256];

    UiCreatureScene *scene = UiCreatureScene_Create("demos",dog,NULL,error,sizeof(error));
    assert(scene && scene->from == dog->recipeId && !scene->docs[0] && !scene->ager);
    CheckField(scene);
    UiCreatureScene_Destroy(scene);
    scene = UiCreatureScene_Create("demos",elephant,NULL,error,sizeof(error));
    assert(scene && scene->from == elephant->recipeId && scene->docs[0]);
    assert(scene->docs[0]->recipe.name == scene->docs[0]->name);
    CheckField(scene);
    UiCreatureScene_Destroy(scene);

    const UiVfsNode *pangolin = Find(fs,folder,"pangolin.creature.json");
    const UiVfsNode *bat = Find(fs,folder,"murcielago.creature.json");
    scene = UiCreatureScene_Create("demos",pangolin,NULL,error,sizeof(error));
    assert(scene && scene->armor.indexCount > 0 && scene->membranes.indexCount == 0);
    UiCreatureScene_Destroy(scene);
    scene = UiCreatureScene_Create("demos",bat,bat,error,sizeof(error));
    assert(scene && scene->membranes.indexCount > 0);
    assert(UiCreatureScene_SetAge(scene,1.0f) && scene->membranes.indexCount > 0);
    assert(scene->armor.indexCount == 0);
    size_t membraneCount = scene->membranes.indexCount;
    unsigned fieldBuilds = scene->fieldBuilds;
    assert(UiCreatureScene_SetAge(scene,1.0f) && scene->fieldBuilds == fieldBuilds);
    assert(scene->membranes.indexCount == membraneCount);
    assert(UiCreatureScene_SetAge(scene,0.0f) && scene->membranes.indexCount == membraneCount);
    UiCreatureScene_Destroy(scene);

    scene = UiCreatureScene_Create("demos",pig,elephant,error,sizeof(error));
    assert(scene && scene->ager && scene->ager->perc == 0 && !scene->ager->geometrySnapshotEnabled);
    for (int i=0; i<=8; ++i) {
        float expected = (i<=4 ? i : 8-i)*0.25f;
        assert(UiCreatureScene_SetAge(scene,expected) && scene->ager->perc == expected);
        CheckField(scene);
        uint64_t count = scene->ager->interpolationCount;
        assert(UiCreatureScene_SetAge(scene,expected) && scene->ager->interpolationCount == count);
    }
    bool reachedEnd = false, returned = false;
    for (int i=0; i<110; ++i) {
        assert(UiCreatureScene_Advance(scene,0.1));
        if (scene->ager->perc == 1.0f) reachedEnd = true;
        if (reachedEnd && scene->ager->perc == 0.0f) returned = true;
    }
    assert(reachedEnd && returned);
    UiCreatureScene *next = UiCreatureScene_Create("demos",horse,NULL,error,sizeof(error));
    assert(next && next->adult.surface.longHairCount > 0);
    UiCreatureScene_Destroy(scene);
    next->hair = LongHairAsync_Create();
    assert(next->hair && LongHairAsync_Update(next->hair,UiCreatureScene_Monster(next),false));
    UiCreatureScene_Destroy(next);
    const CreatureRecipe *source = CreatureRecipes_Dog();
    CreatureOffspringBatch *batch = CreatureOffspring_Generate(source,CreatureRecipes_Lizard(),
        CreatureOffspring_Default(1,48216),NULL,NULL,error,sizeof(error));
    assert(batch);
    const CreatureRecipe *child = CreatureOffspring_Get(batch,0,NULL);
    scene = UiCreatureScene_CreateTemporary(child,1,error,sizeof(error));
    assert(scene && scene->temporaryCandidate==1 && scene->docs[0]->recipe.name!=child->name);
    CreatureOffspring_Destroy(batch);
    assert(scene->adult.recipe.name==scene->docs[0]->name);
    CheckField(scene);
    UiCreatureScene_Destroy(scene);
    SpeciesPairs(fs,folder);
    FiveNonMammals(fs,folder);
    UiVfs_Destroy(fs);
    Orbit();
    OrbitFraming();
    InvalidFullRecipe();
    puts("Interface creature engine tests passed");
}
