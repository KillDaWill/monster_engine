#define GL_GLEXT_PROTOTYPES
#include "UiCreatureEngine.h"
#include "UiCreatureScene.h"
#include "UiCreatureOrbit.h"
#include "OpenGLRenderer.h"
#include <GL/gl.h>
#include <SDL2/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { GLuint framebuffer, color, depth; int width, height; } Target;

struct UiCreatureEngine {
    UiCreatureScene *scene, *pending;
    const UiCreatureScene *materialScene;
    float materialAge;
    Renderer3D renderer;
    Target front, work;
    GLsync fence;
    double elapsed;
    Uint64 requestStart;
    int failedWidth, failedHeight;
    UiCreatureOrbit orbit;
    bool stopped, profile, hairDirty, waitingShader;
};

static bool Supported(void)
{
    const char *version = (const char *)glGetString(GL_VERSION);
    int major = 0, minor = 0;
    if (!version || sscanf(version,"%d.%d",&major,&minor) != 2 ||
        major < 3 || (major == 3 && minor < 3)) return false;
    GLint profile = 0, bufferSize = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&bufferSize);
    return (profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) && bufferSize > 0;
}

static bool AcceptScene(UiCreatureEngine **owner, UiCreatureScene *pending, char *error, size_t capacity)
{
    if (!pending) return false;
    if (!*owner) {
        *owner = calloc(1,sizeof(**owner));
        if (*owner) (*owner)->orbit = UiCreatureOrbit_Default();
    }
    if (!*owner) {
        UiCreatureScene_Destroy(pending);
        snprintf(error,capacity,"ERROR: Cannot allocate the visualizer renderer");
        return false;
    }
    UiCreatureEngine *engine = *owner;
    engine->profile = getenv("NEXUS_VISUALIZER_PROFILE") != NULL;
    UiCreatureScene_Destroy(engine->pending);
    engine->pending = pending;
    return true;
}

static bool CheckSupport(char *error, size_t capacity)
{
    if (Supported()) return true;
    snprintf(error,capacity,"ERROR: Creature rendering requires OpenGL 3.3 compatibility");
    return false;
}

bool UiCreatureEngine_Request(UiCreatureEngine **owner, const char *directory,
    const UiVfsNode *first, const UiVfsNode *second, char *error, size_t capacity)
{
    return CheckSupport(error,capacity) && AcceptScene(owner,
        UiCreatureScene_Create(directory,first,second,error,capacity),error,capacity);
}

bool UiCreatureEngine_RequestTemporary(UiCreatureEngine **owner, const CreatureRecipe *recipe,
    unsigned candidate, char *error, size_t capacity)
{
    Uint64 start = SDL_GetPerformanceCounter();
    if (!CheckSupport(error,capacity) || !AcceptScene(owner,
        UiCreatureScene_CreateTemporary(recipe,candidate,error,capacity),error,capacity)) return false;
    if ((*owner)->profile) {
        (*owner)->requestStart = start;
        double ms = (double)(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency();
        fprintf(stderr,"[nexus-offspring-preview] candidate=%u scene_ms=%.3f\n",candidate,ms);
    }
    return true;
}

static void FreeTarget(Target *target)
{
    if (target->framebuffer) glDeleteFramebuffers(1,&target->framebuffer);
    if (target->color) glDeleteTextures(1,&target->color);
    if (target->depth) glDeleteRenderbuffers(1,&target->depth);
    *target = (Target){0};
}

static bool IsTemporary(const UiCreatureScene *scene, unsigned candidate)
{
    return scene && scene->temporaryCandidate && (!candidate || scene->temporaryCandidate == candidate);
}

void UiCreatureEngine_InvalidateTemporary(UiCreatureEngine *engine, unsigned candidate)
{
    if (!engine) return;
    if (IsTemporary(engine->pending,candidate)) {
        if (engine->materialScene == engine->pending) engine->materialScene = NULL;
        UiCreatureScene_Destroy(engine->pending);
        engine->pending = NULL;
        engine->requestStart = 0;
        engine->waitingShader = false;
    }
    if (IsTemporary(engine->scene,candidate)) {
        if (engine->fence) glDeleteSync(engine->fence);
        engine->fence = NULL;
        FreeTarget(&engine->front);
        if (engine->materialScene == engine->scene) engine->materialScene = NULL;
        UiCreatureScene_Destroy(engine->scene);
        engine->scene = NULL;
        engine->hairDirty = false;
    }
}

void UiCreatureEngine_Destroy(UiCreatureEngine *engine)
{
    if (!engine) return;
    if (engine->fence) glDeleteSync(engine->fence);
    OpenGLRenderer_Destroy(&engine->renderer);
    FreeTarget(&engine->front);
    FreeTarget(&engine->work);
    UiCreatureScene_Destroy(engine->pending);
    UiCreatureScene_Destroy(engine->scene);
    free(engine);
}

UiCreatureFrame UiCreatureEngine_Front(const UiCreatureEngine *engine)
{
    if (!engine) return (UiCreatureFrame){0};
    return (UiCreatureFrame){engine->front.color,engine->front.width,engine->front.height};
}

void UiCreatureEngine_OrbitInput(UiCreatureEngine *engine, const UiEvent *event)
{
    if (engine) UiCreatureOrbit_Input(&engine->orbit,event);
}

bool UiCreatureEngine_NeedsFrame(const UiCreatureEngine *engine)
{
    if (!engine) return false;
    if (engine->pending || engine->fence) return true;
    return engine->scene && !engine->stopped &&
           (engine->scene->ager || engine->orbit.dirty || engine->hairDirty || LongHairAsync_IsBusy(engine->scene->hair));
}

static bool Resize(Target *target, int width, int height)
{
    if (target->width == width && target->height == height) return true;
    if (!target->framebuffer) {
        glGenFramebuffers(1,&target->framebuffer);
        glGenTextures(1,&target->color);
        glGenRenderbuffers(1,&target->depth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,target->framebuffer);
    glBindTexture(GL_TEXTURE_2D,target->color);
    /* El pelo ya se compuso sobre el fondo 3D; la interfaz recibe color opaco. */
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target->color,0);
    glBindRenderbuffer(GL_RENDERBUFFER,target->depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,target->depth);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D,0);
    glBindRenderbuffer(GL_RENDERBUFFER,0);
    if (ok) {
        target->width = width;
        target->height = height;
    } else {
        target->width = target->height = 0;
    }
    return ok;
}

static bool Eyes(UiCreatureScene *scene, Renderer3D *renderer, const Monster *monster, float age)
{
    if (!monster->eyeCount) return true;
    if (monster->eyeCount > scene->eyeCapacity) {
        MonsterVisualEye *eyes = realloc(scene->eyes,monster->eyeCount*sizeof(*eyes));
        if (!eyes) return false;
        for (size_t i=scene->eyeCapacity; i<monster->eyeCount; ++i)
            eyes[i] = (MonsterVisualEye){.globe=Mesh_Create()};
        scene->eyes = eyes;
        scene->eyeCapacity = monster->eyeCount;
    }
    if (scene->eyeAge != age) {
        if (!MonsterVisual_UpdateEyeInstances(scene->eyes,scene->eyeCapacity,monster)) return false;
        scene->eyeAge = age;
    }
    for (size_t i=0; i<monster->eyeCount; ++i) renderer->renderEye(renderer,&scene->eyes[i]);
    return true;
}

static bool Hair(UiCreatureScene *scene, const Monster *monster)
{
    if (monster->hasSurface && monster->surface.longHairCount && !scene->hair) {
        scene->hair = LongHairAsync_Create();
        if (!scene->hair) return false;
    }
    return !scene->hair || LongHairAsync_Update(scene->hair,monster,scene->ager != NULL);
}

static void SceneState(void)
{
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearDepth(1.0);
    glClearColor(0.05f,0.05f,0.08f,1.0f);
}

static const char *Draw(UiCreatureEngine *engine, UiCreatureScene *scene, int width, int height)
{
    SceneState();
    if (!Resize(&engine->work,width,height)) {
        OpenGLRenderer_CheckErrors();
        return "ERROR: Creature framebuffer allocation failed";
    }
    Renderer3D *r = &engine->renderer;
    const Monster *monster = UiCreatureScene_Monster(scene);
    const Mesh *extras[] = {&scene->membranes, &scene->armor};
    float aspect = (float)width/height;
    ICamera camera = UiCreatureOrbit_Camera(&engine->orbit,scene->framing,aspect);
    float age = scene->ager ? scene->ager->perc : 0.0f;
    bool geometryChanged = engine->materialScene != scene || engine->materialAge != age;
    if (geometryChanged) {
        const Monster *reference = scene->ager ? &scene->reference : &scene->adult;
        if (!OpenGLRenderer_SetMorphSurfaceDomain(r,monster,reference))
            return "ERROR: Cannot prepare the creature surface";
        engine->materialScene = scene;
        engine->materialAge = age;
    }
    OpenGLRenderer_SetFrameTarget(r,engine->work.framebuffer);
    r->beginFrame(r);
    bool ok = OpenGLRenderer_BeginSDFFrame(r,&camera,width,height,false);
    if (ok) {
        bool traced = OpenGLRenderer_RenderSDF(r,&scene->sdf,&camera,width,height);
        ok = OpenGLRenderer_EndSDFFrame(r,width,height) && traced;
    }
    const char *error = NULL;
    if (ok) {
        OpenGLRenderer_SetupCamera(&camera,width,height);
        for (size_t i = 0; i < sizeof(extras) / sizeof(extras[0]); ++i)
            if (extras[i]->indexCount) r->renderMesh(r,extras[i]);
        if (!Eyes(scene,r,monster,age)) error = "ERROR: Cannot prepare creature eyes";
        if (geometryChanged && !Hair(scene,monster)) error = "ERROR: Cannot prepare creature hair";
        const LongHairSet *hair = LongHairAsync_GetDisplay(scene->hair);
        if (hair && hair->count) r->renderLongHair(r,hair);
    } else {
        error = "ERROR: Creature SDF shader or composition failed (see console)";
    }
    r->endFrame(r);
    if (!OpenGLRenderer_CheckErrors()) return "ERROR: OpenGL error in the creature prepass (see console)";
    return error;
}

const char *UiCreatureEngine_Render(UiCreatureEngine *engine, int width, int height, double dt)
{
    if (!engine || width <= 0 || height <= 0) return NULL;
    if (!engine->waitingShader) engine->elapsed += dt;
    if (engine->fence) {
        GLenum status = glClientWaitSync(engine->fence,0,0);
        if (status == GL_TIMEOUT_EXPIRED) return NULL;
        glDeleteSync(engine->fence);
        engine->fence = NULL;
        if (status == GL_WAIT_FAILED) {
            OpenGLRenderer_CheckErrors();
            engine->elapsed = 0;
            engine->stopped = true;
            UiCreatureScene_Destroy(engine->pending);
            engine->pending = NULL;
            return "ERROR: Creature frame synchronization failed";
        }
    }
    UiCreatureScene *scene = engine->pending ? engine->pending : engine->scene;
    bool replacing = engine->pending != NULL;
    if (!scene) return NULL;
    if (!replacing && engine->stopped && width == engine->failedWidth && height == engine->failedHeight)
        return NULL;

    bool moving = scene->ager && !replacing && !engine->stopped;
    bool resized = width != engine->front.width || height != engine->front.height;
    const Monster *monster = UiCreatureScene_Monster(scene);
    if (!replacing && !moving && !resized && !engine->hairDirty) {
        if (LongHairAsync_IsBusy(scene->hair)) {
            const LongHairSet *old = LongHairAsync_GetDisplay(scene->hair);
            uint64_t generation = old->generation, fingerprint = old->fingerprint;
            size_t count = old->count;
            if (!Hair(scene,monster)) return "ERROR: Cannot publish creature hair";
            const LongHairSet *next = LongHairAsync_GetDisplay(scene->hair);
            engine->hairDirty = count != next->count || generation != next->generation ||
                                fingerprint != next->fingerprint;
        }
        if (!engine->hairDirty && !engine->orbit.dirty) { engine->elapsed = 0; return NULL; }
    }

    Uint64 start = engine->profile ? SDL_GetPerformanceCounter() : 0;
    const char *error = NULL;
    if (moving && !engine->waitingShader && !UiCreatureScene_Advance(scene,engine->elapsed))
        error = "ERROR: Cannot build the intermediate creature SDF";
    else if (!scene->fieldValid)
        error = "ERROR: Creature SDF is unavailable";
    if (!error && !engine->renderer.user_data) {
        SceneState();
        engine->renderer = OpenGLRenderer_Create(NULL);
        if (!engine->renderer.user_data) error = "ERROR: Creature renderer allocation failed";
        else {
            OpenGLRenderer_SetFurQuality(&engine->renderer,true,true,0);
            OpenGLRenderer_SetLongHairQuality(&engine->renderer,3);
            if (engine->profile) fprintf(stderr,"[nexus-creature-gl] %s | %s\n",
                                          glGetString(GL_RENDERER),glGetString(GL_VERSION));
        }
    }
    OpenGLSDFProgramStatus program = error ? OPENGL_SDF_FAILED :
        OpenGLRenderer_PrepareSDFProgram(&engine->renderer,&scene->sdf);
    engine->waitingShader = program == OPENGL_SDF_COMPILING;
    if (engine->waitingShader) { engine->elapsed = 0; return NULL; }
    if (!error && program == OPENGL_SDF_FAILED)
        error = "ERROR: Creature SDF shader compilation failed (see console)";
    if (!error) error = Draw(engine,scene,width,height);
    engine->elapsed = 0;
    engine->hairDirty = false;
    engine->orbit.dirty = false;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    if (error) {
        engine->materialScene = NULL;
        if (replacing) {
            engine->pending = NULL;
            UiCreatureScene_Destroy(scene);
        } else {
            engine->failedWidth = width;
            engine->failedHeight = height;
            engine->stopped = true;
        }
        return error;
    }

    Target old = engine->front;
    engine->front = engine->work;
    engine->work = old;
    engine->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if (replacing) {
        UiCreatureScene_Destroy(engine->scene);
        engine->scene = scene;
        engine->pending = NULL;
    }
    engine->stopped = false;
    if (engine->profile) {
        if (replacing && scene->temporaryCandidate && engine->requestStart) {
            double firstMs = (double)(SDL_GetPerformanceCounter()-engine->requestStart)*1000.0/
                SDL_GetPerformanceFrequency();
            fprintf(stderr,"[nexus-offspring-preview] candidate=%u first_frame_ms=%.3f\n",
                scene->temporaryCandidate,firstMs);
            engine->requestStart = 0;
        }
        double ms = (double)(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency();
        fprintf(stderr,"[nexus-creature] %u %u age=%.5f dir=%d %dx%d cpu_ms=%.3f sdf_gpu_ms=%.3f tick_ms=%llu field_builds=%u orbit=%.3f,%.3f,%.3f\n",
            scene->from,scene->to,scene->ager ? scene->ager->perc : 0.0f,scene->direction,
            width,height,ms,OpenGLRenderer_GetSDFGpuMs(&engine->renderer),(unsigned long long)SDL_GetTicks64(),
            scene->fieldBuilds,engine->orbit.yaw,engine->orbit.pitch,engine->orbit.zoom);
    }
    return NULL;
}
