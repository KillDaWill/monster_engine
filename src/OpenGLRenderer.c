#define GL_GLEXT_PROTOTYPES
#include "OpenGLRenderer.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "EyeTexture.h"
#include "MathUtils.h"
#include "Fur.h"
#include <string.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define GPU_MESH_CACHE_CAPACITY 128
#define EYE_TEXTURE_CACHE_CAPACITY 8
typedef struct GeometryGpuVertex { float position[3],normal[3]; unsigned char color[4]; float padding; } GeometryGpuVertex;
typedef struct SurfaceGpuVertex { float position[3],normal[3],region[4]; int material; float flowDirection[3]; float integumentMask; } SurfaceGpuVertex;
typedef struct GpuMesh {
    const Mesh* owner;
    uint64_t identity;
    const void* vertexPointer;
    const void* indexPointer;
    GLuint vao,geometryVbo,surfaceVbo,indexBuffer;
    uint64_t geometryGeneration,surfaceGeneration,lastUse;
    size_t vertexCount,indexCount;
    GLuint rootVbo,rootVao,geometryTexture,surfaceTexture;
    size_t rootCount;
    uint32_t rootSeed;
    bool rootsValid;
} GpuMesh;
typedef struct EyeTextureCache { GLuint texture; uint64_t fingerprint,lastUse; } EyeTextureCache;
static void OpenGL_RenderEyeCallback(Renderer3D*,const struct MonsterVisualEye*);

/* ============================================================
 * RENDER STATE DATA
 * ============================================================ */

typedef struct OpenGLRendererData {
    bool wireframe;
    GLuint sdfProgram, sdfBuffer, sdfTexture;
    GLuint surfaceProgram,guardProgram;
    GLuint eyeProgram;
    struct { GLint texture,center,forward,right,up,scale,gloss; } eye;
    EyeTextureCache eyeTextures[EYE_TEXTURE_CACHE_CAPACITY];
    uint64_t eyeTextureClock;
    GLint shellSamplesLocation,guardVisibilityLocation,shellResolutionLocation,regionalLODLocation;
    int forcedShells;
    bool disableShells,disableGuards;
    struct { GLint recipe,pigmentSeed,furSeed,debug,visibility,geometry,surface,coverage; } guard;
    GLint surfaceRecipeLocation,pigmentSeedLocation,scaleSeedLocation,furSeedLocation,surfaceDebugLocation;
    GLint shellFractionLocation,furPassLocation,shellCountLocation;
    int surfaceDebug;
    bool surfaceFailed;
    struct {
        GLint validationMode,validationPointBase,sceneData;
        GLint partBase,partCount,connectorBase,connectorCount,mouthBase,mouthCount,axialBase,axialCount;
        GLint bodySmoothness,tileDataBase,tileColumns,cameraPosition,cameraForward,cameraRight,cameraUp;
        GLint boundsMin,boundsMax,viewportSize,tanHalfFov,aspectRatio,hitTolerance,viewProjection;
    } uniforms;
    float (*sdfData)[4];
    size_t sdfCapacity,sdfCount;
    int* tileIndices;
    size_t tileCapacity;
    GLuint sdfFramebuffer,sdfColor,sdfDepth,sdfQueries[4];
    unsigned queryFrame,querySerial[4];
    bool queryPending[4], sdfFrameActive, queryIssued;
    int sdfWidth,sdfHeight,sdfAllocatedWidth,sdfAllocatedHeight;
    float resolutionScale,gpuMs,filteredGpuMs,queryScale[4];
    unsigned overBudget,underBudget;
    GpuMesh meshCache[GPU_MESH_CACHE_CAPACITY];
    GeometryGpuVertex* geometryScratch;
    SurfaceGpuVertex* surfaceScratch;
    size_t geometryScratchCapacity,surfaceScratchCapacity;
    uint64_t meshFrame;
    OpenGLMeshPerformanceStats meshStats;
    GLuint meshQueries[4];unsigned meshQueryFrame,meshQuerySerial[4];bool meshQueryPending[4],meshQueryIssued;
} OpenGLRendererData;

static double RendererNowMs(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec*1000.0+(double)t.tv_nsec/1000000.0;
}

void OpenGLRenderer_SetWireframe(Renderer3D* renderer, bool enabled) {
    if (renderer && renderer->user_data) {
        OpenGLRendererData* data = (OpenGLRendererData*)renderer->user_data;
        data->wireframe = enabled;
    }
}

bool OpenGLRenderer_SavePPM(const char* path, int width, int height) {
    if (!path || width <= 0 || height <= 0) return false;
    size_t rowBytes=(size_t)width*3u,total=rowBytes*(size_t)height;
    unsigned char* pixels=(unsigned char*)malloc(total);
    if(!pixels)return false;
    GLint previousPack=4;glGetIntegerv(GL_PACK_ALIGNMENT,&previousPack);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glFinish();
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels);
    glPixelStorei(GL_PACK_ALIGNMENT,previousPack);
    FILE* file=fopen(path,"wb");
    bool ok=file&&fprintf(file,"P6\n%d %d\n255\n",width,height)>0;
    for(int y=height-1;ok&&y>=0;--y)
        ok=fwrite(pixels+(size_t)y*rowBytes,1,rowBytes,file)==rowBytes;
    if(file&&fclose(file)!=0)ok=false;
    free(pixels);return ok;
}

/* ============================================================
 * FRAME LIFECYCLE
 * ============================================================ */

static void OpenGL_BeginFrame(Renderer3D* self) {
    OpenGLRendererData* d=self?self->user_data:NULL;
    if(d) {
        if(!d->meshQueries[0])glGenQueries(4,d->meshQueries);
        for(unsigned pending=0;pending<4;++pending) {
            unsigned oldest=4,ageBest=0;
            for(unsigned i=0;i<4;++i)if(d->meshQueryPending[i]) {
                unsigned age=d->meshQueryFrame-d->meshQuerySerial[i];
                if(oldest==4||age>ageBest){oldest=i;ageBest=age;}
            }
            if(oldest==4)break;
            GLint ready=0;glGetQueryObjectiv(d->meshQueries[oldest],GL_QUERY_RESULT_AVAILABLE,&ready);if(!ready)break;
            GLuint64 ns=0;glGetQueryObjectui64v(d->meshQueries[oldest],GL_QUERY_RESULT,&ns);
            d->meshStats.gpuRenderMs=(float)((double)ns/1e6);d->meshQueryPending[oldest]=false;
        }
        unsigned q=d->meshQueryFrame%4;d->meshQueryIssued=!d->meshQueryPending[q];
        if(d->meshQueryIssued){d->meshQuerySerial[q]=d->meshQueryFrame;glBeginQuery(GL_TIME_ELAPSED,d->meshQueries[q]);}
        ++d->meshFrame;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void OpenGL_EndFrame(Renderer3D* self) {
    OpenGLRendererData* d=self?self->user_data:NULL;
    if(d&&d->meshQueryIssued){glEndQuery(GL_TIME_ELAPSED);d->meshQueryPending[d->meshQueryFrame%4]=true;}
    if(d){++d->meshQueryFrame;d->meshQueryIssued=false;}
}

#include <stddef.h>
#include <limits.h>

_Static_assert(sizeof(MeshIndex) == sizeof(GLuint), "MeshIndex must match GLuint size for glDrawElements");

/* ============================================================
 * MESH RENDERER
 * ============================================================ */

void OpenGLRenderer_RenderMesh(const Mesh* mesh) {
    if (!mesh || mesh->vertexCount == 0 || mesh->indexCount < 3 || !mesh->vertices || !mesh->indices) return;

    const unsigned char* base = (const unsigned char*)mesh->vertices;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glVertexPointer(3, GL_FLOAT, sizeof(MeshVertex), base + offsetof(MeshVertex, position));
    glNormalPointer(GL_FLOAT, sizeof(MeshVertex), base + offsetof(MeshVertex, normal));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(MeshVertex), base + offsetof(MeshVertex, color));

    const size_t MAX_CHUNK_INDICES = (size_t)INT32_MAX - ((size_t)INT32_MAX % 3);
    size_t indexOffset = 0;

    while (indexOffset < mesh->indexCount) {
        size_t count = mesh->indexCount - indexOffset;
        if (count > MAX_CHUNK_INDICES) {
            count = MAX_CHUNK_INDICES;
        }

        glDrawElements(
            GL_TRIANGLES,
            (GLsizei)count,
            GL_UNSIGNED_INT,
            &mesh->indices[indexOffset]
        );

        indexOffset += count;
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static bool RenderSurfaceMesh(OpenGLRendererData* data,const Mesh* mesh);

static void OpenGL_RenderMeshCallback(Renderer3D* self, const Mesh* mesh) {
    bool wireframe = false;
    if (self && self->user_data) {
        OpenGLRendererData* data = (OpenGLRendererData*)self->user_data;
        wireframe = data->wireframe;
    }

    GLint previousPolygonMode[2] = {GL_FILL, GL_FILL};
    if (wireframe) {
        glGetIntegerv(GL_POLYGON_MODE, previousPolygonMode);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    OpenGLRendererData* data=self?self->user_data:NULL;
    if(!data || !mesh || (!mesh->hasSurface && data->surfaceDebug!=10) || !RenderSurfaceMesh(data,mesh))OpenGLRenderer_RenderMesh(mesh);

    if (wireframe) {
        glPolygonMode(GL_FRONT, previousPolygonMode[0]);
        glPolygonMode(GL_BACK, previousPolygonMode[1]);
    }
}

void OpenGLRenderer_Destroy(Renderer3D* renderer) {
    if (!renderer) return;

    if (renderer->user_data) {
        OpenGLRendererData* data=renderer->user_data;
        if(data->eyeProgram)glDeleteProgram(data->eyeProgram);
        for(unsigned i=0;i<EYE_TEXTURE_CACHE_CAPACITY;++i)if(data->eyeTextures[i].texture)glDeleteTextures(1,&data->eyeTextures[i].texture);
        if(data->guardProgram)glDeleteProgram(data->guardProgram);
        if(data->surfaceProgram)glDeleteProgram(data->surfaceProgram);
        for(unsigned i=0;i<GPU_MESH_CACHE_CAPACITY;++i) {
            GpuMesh* m=&data->meshCache[i];
            if(m->rootVbo)glDeleteBuffers(1,&m->rootVbo);
            if(m->rootVao)glDeleteVertexArrays(1,&m->rootVao);
            if(m->geometryTexture)glDeleteTextures(1,&m->geometryTexture);
            if(m->surfaceTexture)glDeleteTextures(1,&m->surfaceTexture);
            if(m->vao)glDeleteVertexArrays(1,&m->vao);
            if(m->geometryVbo)glDeleteBuffers(1,&m->geometryVbo);
            if(m->surfaceVbo)glDeleteBuffers(1,&m->surfaceVbo);
            if(m->indexBuffer)glDeleteBuffers(1,&m->indexBuffer);
        }
        if(data->sdfProgram)glDeleteProgram(data->sdfProgram);
        if(data->sdfBuffer)glDeleteBuffers(1,&data->sdfBuffer);
        if(data->sdfTexture)glDeleteTextures(1,&data->sdfTexture);
        if(data->sdfFramebuffer)glDeleteFramebuffers(1,&data->sdfFramebuffer);
        if(data->sdfColor)glDeleteTextures(1,&data->sdfColor);
        if(data->sdfDepth)glDeleteRenderbuffers(1,&data->sdfDepth);
        if(data->sdfQueries[0])glDeleteQueries(4,data->sdfQueries);
        if(data->meshQueries[0])glDeleteQueries(4,data->meshQueries);
        free(data->geometryScratch);free(data->surfaceScratch);
        free(data->sdfData);
        free(data->tileIndices);
        free(renderer->user_data);
        renderer->user_data = NULL;
    }
    renderer->beginFrame = NULL;
    renderer->endFrame = NULL;
    renderer->renderMesh = NULL;
    renderer->renderEye = NULL;
}

/* ============================================================
 * CAMERA & LIGHTING SETUP
 * ============================================================ */

void OpenGLRenderer_SetupCamera(ICamera* camera, int width, int height) {
    if (height <= 0) height = 1;

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    float aspect = (float)width / (float)height;
    if (camera) {
        gluPerspective(camera->fov, aspect, camera->nearPlane, camera->farPlane);
    } else {
        gluPerspective(45.0f, aspect, 0.1f, 100.0f);
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (camera) {
        gluLookAt(
            camera->position.x, camera->position.y, camera->position.z,
            camera->target.x,   camera->target.y,   camera->target.z,
            camera->up.x,       camera->up.y,       camera->up.z
        );
    }
}

Renderer3D OpenGLRenderer_Create(ICamera* camera) {
    (void)camera;
    Renderer3D renderer;

    OpenGLRendererData* data = (OpenGLRendererData*)calloc(1, sizeof(OpenGLRendererData));
    renderer.user_data = data;
    renderer.beginFrame = OpenGL_BeginFrame;
    renderer.endFrame = OpenGL_EndFrame;
    renderer.renderMesh = OpenGL_RenderMeshCallback;
    renderer.renderEye = OpenGL_RenderEyeCallback;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);
#ifdef GL_MULTISAMPLE
    {
        GLint sampleBuffers=0;
        glGetIntegerv(GL_SAMPLE_BUFFERS,&sampleBuffers);
        if(sampleBuffers>0)glEnable(GL_MULTISAMPLE);
    }
#endif

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat lightPos[] = {10.0f, 20.0f, 15.0f, 1.0f};
    GLfloat lightAmbient[] = {0.4f, 0.4f, 0.4f, 1.0f};
    GLfloat lightDiffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};

    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);

    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);

    return renderer;
}

/* Transporte explícito: no depende del padding de las estructuras C. */
static void PackVector(float out[4],Vector3 v){out[0]=v.x;out[1]=v.y;out[2]=v.z;out[3]=0;}
static void PackColor(float out[4],Color c){out[0]=c.r/255.f;out[1]=c.g/255.f;out[2]=c.b/255.f;out[3]=c.a/255.f;}
static void PackStation(float (*out)[4],const SDFSweepStation* s){
    PackVector(out[0],s->center);out[0][3]=s->width;
    out[1][0]=s->height;out[1][1]=s->widthSlope;out[1][2]=s->heightSlope;out[1][3]=s->centerSlope;
}
#include "MonsterSDFShader.generated.h"
#include "MonsterSDFRayBounds.h"

static GLuint CompileShader(GLenum type,const char* const* source,size_t count) {
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,(GLsizei)count,source,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"[GLSL] %s\n",log);glDeleteShader(shader);return 0;}
    return shader;
}
static bool InitEyeProgram(OpenGLRendererData* d) {
    if(d->eyeProgram)return true;
    const char* vs="#version 120\n"
        "uniform vec3 eyeCenter,eyeForward,eyeRight,eyeUp,eyeScale;\n"
        "varying vec3 vLocal,vNormal,vEyePos;\n"
        "void main(){vec3 q=gl_Vertex.xyz-eyeCenter;vLocal=vec3(dot(q,eyeRight)/eyeScale.x,dot(q,eyeUp)/eyeScale.y,dot(q,eyeForward)/eyeScale.z);"
        "vec4 p=gl_ModelViewMatrix*gl_Vertex;vEyePos=p.xyz;vNormal=normalize(gl_NormalMatrix*gl_Normal);gl_Position=gl_ProjectionMatrix*p;}\n";
    const char* fs="#version 120\n"
        "uniform sampler2D eyeTexture;uniform float eyeGloss;varying vec3 vLocal,vNormal,vEyePos;\n"
        "void main(){const float pi=3.14159265359;vec3 q=normalize(vLocal);vec2 uv=vec2(atan(q.x,q.z)/(2.0*pi)+0.5,asin(clamp(q.y,-1.0,1.0))/pi+0.5);"
        "vec3 base=texture2D(eyeTexture,uv).rgb;vec3 n=normalize(vNormal);vec3 v=normalize(-vEyePos);"
        "vec3 l=normalize(gl_LightSource[0].position.xyz-vEyePos*gl_LightSource[0].position.w);vec3 h=normalize(l+v);"
        "float diff=max(dot(n,l),0.0);float spec=pow(max(dot(n,h),0.0),96.0)*eyeGloss;float fres=pow(1.0-max(dot(n,v),0.0),5.0);"
        "vec3 col=base*(0.34+0.72*diff)+vec3(spec*0.9+fres*0.13);gl_FragColor=vec4(col,1.0);}\n";
    GLuint v=CompileShader(GL_VERTEX_SHADER,&vs,1),f=CompileShader(GL_FRAGMENT_SHADER,&fs,1);
    if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[2048];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"[Eye GLSL] %s\n",log);glDeleteProgram(p);return false;}
    d->eyeProgram=p;d->eye.texture=glGetUniformLocation(p,"eyeTexture");d->eye.center=glGetUniformLocation(p,"eyeCenter");
    d->eye.forward=glGetUniformLocation(p,"eyeForward");d->eye.right=glGetUniformLocation(p,"eyeRight");d->eye.up=glGetUniformLocation(p,"eyeUp");
    d->eye.scale=glGetUniformLocation(p,"eyeScale");d->eye.gloss=glGetUniformLocation(p,"eyeGloss");return true;
}
static GLuint EyeTextureFor(OpenGLRendererData* d,const MonsterVisualEye* eye) {
    uint64_t fp=eye->appearanceFingerprint?eye->appearanceFingerprint:EyeTexture_Fingerprint(&eye->appearance);
    for(unsigned i=0;i<EYE_TEXTURE_CACHE_CAPACITY;++i)if(d->eyeTextures[i].texture&&d->eyeTextures[i].fingerprint==fp){d->eyeTextures[i].lastUse=++d->eyeTextureClock;return d->eyeTextures[i].texture;}
    unsigned slot=0;for(unsigned i=1;i<EYE_TEXTURE_CACHE_CAPACITY;++i)if(!d->eyeTextures[i].texture||d->eyeTextures[i].lastUse<d->eyeTextures[slot].lastUse)slot=i;
    EyeTextureCache* c=&d->eyeTextures[slot];if(!c->texture)glGenTextures(1,&c->texture);
    unsigned char pixels[128u*128u*4u];if(!EyeTexture_Generate(&eye->appearance,128,pixels,sizeof(pixels)))return 0;
    glBindTexture(GL_TEXTURE_2D,c->texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,128,128,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);c->fingerprint=fp;c->lastUse=++d->eyeTextureClock;return c->texture;
}
static void OpenGL_RenderEyeCallback(Renderer3D* self,const struct MonsterVisualEye* visualEye) {
    OpenGLRendererData* d=self?self->user_data:NULL;const MonsterVisualEye* e=(const MonsterVisualEye*)visualEye;
    if(!d||!e||!e->globe.vertices||!e->globe.indexCount||!InitEyeProgram(d))return;
    GLuint texture=EyeTextureFor(d,e);if(!texture)return;GLint oldProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glUseProgram(d->eyeProgram);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(d->eye.texture,0);
    glUniform3f(d->eye.center,e->center.x,e->center.y,e->center.z);glUniform3f(d->eye.forward,e->forward.x,e->forward.y,e->forward.z);
    glUniform3f(d->eye.right,e->right.x,e->right.y,e->right.z);glUniform3f(d->eye.up,e->up.x,e->up.y,e->up.z);
    glUniform3f(d->eye.scale,e->scale.x,e->scale.y,e->scale.z);glUniform1f(d->eye.gloss,Math_Clamp01(e->appearance.cornealGloss));
    const unsigned char* b=(const unsigned char*)e->globe.vertices;glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3,GL_FLOAT,sizeof(MeshVertex),b+offsetof(MeshVertex,position));glNormalPointer(GL_FLOAT,sizeof(MeshVertex),b+offsetof(MeshVertex,normal));
    size_t max=(size_t)INT32_MAX-((size_t)INT32_MAX%3);for(size_t i=0;i<e->globe.indexCount;){size_t n=e->globe.indexCount-i;if(n>max)n=max;glDrawElements(GL_TRIANGLES,(GLsizei)n,GL_UNSIGNED_INT,&e->globe.indices[i]);i+=n;}
    glDisableClientState(GL_NORMAL_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);glBindTexture(GL_TEXTURE_2D,0);glUseProgram((GLuint)oldProgram);
}
static bool InitSDFProgram(OpenGLRendererData* d) {
    const char* vertex="#version 330 compatibility\nvoid main(){gl_Position=gl_Vertex;}\n";
    GLuint vs=CompileShader(GL_VERTEX_SHADER,&vertex,1);
    const size_t sourceCount=sizeof(sdfFragmentSource)/sizeof(*sdfFragmentSource);
    GLuint fs=CompileShader(GL_FRAGMENT_SHADER,sdfFragmentSource,sourceCount);
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"[SDF GPU] %s\n",log);glDeleteProgram(p);return false;}
    d->sdfProgram=p;
    /* Las ubicaciones pertenecen al programa enlazado, no al fotograma. */
    d->uniforms.validationMode=glGetUniformLocation(p,"validationMode");
    d->uniforms.validationPointBase=glGetUniformLocation(p,"validationPointBase");
    d->uniforms.sceneData=glGetUniformLocation(p,"sceneData");
    d->uniforms.partBase=glGetUniformLocation(p,"partBase");
    d->uniforms.partCount=glGetUniformLocation(p,"partCount");
    d->uniforms.connectorBase=glGetUniformLocation(p,"connectorBase");
    d->uniforms.connectorCount=glGetUniformLocation(p,"connectorCount");
    d->uniforms.mouthBase=glGetUniformLocation(p,"mouthBase");
    d->uniforms.mouthCount=glGetUniformLocation(p,"mouthCount");
    d->uniforms.axialBase=glGetUniformLocation(p,"axialBase");
    d->uniforms.axialCount=glGetUniformLocation(p,"axialCount");
    d->uniforms.bodySmoothness=glGetUniformLocation(p,"bodySmoothness");
    d->uniforms.tileDataBase=glGetUniformLocation(p,"tileDataBase");
    d->uniforms.tileColumns=glGetUniformLocation(p,"tileColumns");
    d->uniforms.cameraPosition=glGetUniformLocation(p,"cameraPosition");
    d->uniforms.cameraForward=glGetUniformLocation(p,"cameraForward");
    d->uniforms.cameraRight=glGetUniformLocation(p,"cameraRight");
    d->uniforms.cameraUp=glGetUniformLocation(p,"cameraUp");
    d->uniforms.boundsMin=glGetUniformLocation(p,"boundsMin");
    d->uniforms.boundsMax=glGetUniformLocation(p,"boundsMax");
    d->uniforms.viewportSize=glGetUniformLocation(p,"viewportSize");
    d->uniforms.tanHalfFov=glGetUniformLocation(p,"tanHalfFov");
    d->uniforms.aspectRatio=glGetUniformLocation(p,"aspectRatio");
    d->uniforms.hitTolerance=glGetUniformLocation(p,"hitTolerance");
    d->uniforms.viewProjection=glGetUniformLocation(p,"viewProjection");
    if(!d->sdfBuffer)glGenBuffers(1,&d->sdfBuffer);
    if(!d->sdfTexture)glGenTextures(1,&d->sdfTexture);
    fprintf(stdout,"[SDF GPU] %s | %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    return true;
}
static void UniformVector(GLint location,Vector3 v){glUniform3f(location,v.x,v.y,v.z);}

bool OpenGLRenderer_RenderSDF(Renderer3D* renderer,const MonsterSDF* sdf,const ICamera* camera,int width,int height) {
    if(!renderer||!renderer->user_data||!sdf||!camera||width<=0||height<=0)return false;
    OpenGLRendererData* d=renderer->user_data;
    float displayAspect=(float)width/height;
    if(d->sdfFrameActive){width=d->sdfWidth;height=d->sdfHeight;}
    if(!d->sdfProgram&&!InitSDFProgram(d))return false;
    Vector3 forward=Vec3_Normalize(Vec3_Sub(camera->target,camera->position));
    Vector3 right=Vec3_Normalize(Vec3_Cross(forward,camera->up)),up=Vec3_Cross(right,forward);
    float tangent=tanf(camera->fov*.00872664626f),aspect=displayAspect;
    int columns=(width+15)/16,rows=(height+15)/16;
    size_t tiles=(size_t)columns*rows,stride=sdf->connectorCount+1;
    if(tiles*stride>d->tileCapacity){int* indices=realloc(d->tileIndices,tiles*stride*sizeof(int));if(!indices)return false;d->tileIndices=indices;d->tileCapacity=tiles*stride;}
    for(size_t tile=0;tile<tiles;++tile)d->tileIndices[tile*stride]=0;
    /* Una unión suave puede bajar la distancia como máximo k/4. La suma
     * restante delimita la influencia incluso para varias ramas solapadas. */
    float remainingSmoothness=0;
    for(size_t i=0;i<sdf->connectorCount;++i)
        if(sdf->axialStationCount<=1||sdf->connectors[i].kind!=BODY_CONNECTION_AXIAL_LOFT)
            remainingSmoothness+=sdf->connectors[i].localSmoothness;
    for(size_t i=0;i<sdf->mouthCount;++i)
        remainingSmoothness+=sdf->mouths[i].anatomicalHead?sdf->mouths[i].headBodySmoothness:2*sdf->mouths[i].muzzleSmoothness;
    for(size_t i=0;i<sdf->connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf->connectors[i];
        if(sdf->axialStationCount>1&&c->kind==BODY_CONNECTION_AXIAL_LOFT)continue;
        remainingSmoothness-=c->localSmoothness;
        float pad=(remainingSmoothness*.25f+c->localSmoothness+.005f)/fmaxf(c->distanceLowerBoundScale,.0001f),minX=(float)width,maxX=0,minY=(float)height,maxY=0;
        for(int corner=0;corner<8;++corner) {
            Vector3 pos=Vec3_Create((corner&1)?c->bounds.end.x+pad:c->bounds.start.x-pad,
                (corner&2)?c->bounds.end.y+pad:c->bounds.start.y-pad,
                (corner&4)?c->bounds.end.z+pad:c->bounds.start.z-pad);
            Vector3 delta=Vec3_Sub(pos,camera->position);float z=Vec3_Dot(delta,forward);
            if(z<camera->nearPlane){minX=0;maxX=(float)width;minY=0;maxY=(float)height;break;}
            float x=(Vec3_Dot(delta,right)/(z*tangent*aspect)+1)*.5f*width;
            float y=(Vec3_Dot(delta,up)/(z*tangent)+1)*.5f*height;
            minX=fminf(minX,x);maxX=fmaxf(maxX,x);minY=fminf(minY,y);maxY=fmaxf(maxY,y);
        }
        if(maxX<0||maxY<0||minX>=width||minY>=height)continue;
        int x0=(int)fmaxf(0,floorf(minX/16)),x1=(int)fminf(columns-1,floorf(maxX/16));
        int y0=(int)fmaxf(0,floorf(minY/16)),y1=(int)fminf(rows-1,floorf(maxY/16));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){int* list=d->tileIndices+((size_t)y*columns+x)*stride;list[++list[0]]=(int)i;}
    }
    size_t tileSlots=tiles;
    for(size_t tile=0;tile<tiles;++tile)tileSlots+=(size_t)(d->tileIndices[tile*stride]+3)/4;
    size_t count=tileSlots+sdf->bodyPartCount*P_STRIDE+sdf->connectorCount*C_STRIDE+sdf->mouthCount*M_STRIDE+(size_t)sdf->axialStationCount*2;
    if(count==0)return true;
    if(count>d->sdfCapacity){float (*data)[4]=realloc(d->sdfData,count*sizeof(*data));if(!data)return false;d->sdfData=data;d->sdfCapacity=count;}
    size_t offset=0,partBase=0;
    for(size_t i=0;i<sdf->bodyPartCount;++i){PackP(d->sdfData+offset,&sdf->bodyParts[i]);offset+=P_STRIDE;}
    size_t connectorBase=offset;
    for(size_t i=0;i<sdf->connectorCount;++i){PackC(d->sdfData+offset,&sdf->connectors[i]);offset+=C_STRIDE;}
    size_t mouthBase=offset;
    float mouthSmoothness=0;
    for(size_t i=0;i<sdf->mouthCount;++i)
        mouthSmoothness+=sdf->mouths[i].anatomicalHead?sdf->mouths[i].headBodySmoothness:2.f*sdf->mouths[i].muzzleSmoothness;
    for(size_t i=0;i<sdf->mouthCount;++i){
        MonsterSDFMouth packed=RayBounds_PackedMouth(&sdf->mouths[i],mouthSmoothness,.001f);
        PackM(d->sdfData+offset,&packed);offset+=M_STRIDE;
    }
    size_t axialBase=offset;
    for(int i=0;i<sdf->axialStationCount;++i){PackStation(d->sdfData+offset,&sdf->axialStations[i]);offset+=2;}
    size_t tileDataBase=offset;offset+=tiles;
    for(size_t tile=0;tile<tiles;++tile) {
        int* list=d->tileIndices+tile*stride;
        d->sdfData[tileDataBase+tile][0]=(float)offset;d->sdfData[tileDataBase+tile][1]=(float)list[0];
        for(int i=0;i<list[0];++i)d->sdfData[offset+(size_t)i/4][i%4]=(float)list[i+1];
        offset+=(size_t)(list[0]+3)/4;
    }
    d->sdfCount=count;
    glBindBuffer(GL_TEXTURE_BUFFER,d->sdfBuffer);glBufferData(GL_TEXTURE_BUFFER,count*sizeof(*d->sdfData),d->sdfData,GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_BUFFER,d->sdfTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,d->sdfBuffer);
    GLuint p=d->sdfProgram;glUseProgram(p);
#define UI(name,value) glUniform1i(d->uniforms.name,(GLint)(value))
#define UF(name,value) glUniform1f(d->uniforms.name,(float)(value))
    UI(validationMode,0);
    UI(sceneData,0);UI(partBase,partBase);UI(partCount,sdf->bodyPartCount);UI(connectorBase,connectorBase);UI(connectorCount,sdf->connectorCount);
    UI(mouthBase,mouthBase);UI(mouthCount,sdf->mouthCount);UI(axialBase,axialBase);UI(axialCount,sdf->axialStationCount);
    UF(bodySmoothness,sdf->config.bodySmoothness);
    UI(tileDataBase,tileDataBase);UI(tileColumns,columns);
    UniformVector(d->uniforms.cameraPosition,camera->position);UniformVector(d->uniforms.cameraForward,forward);UniformVector(d->uniforms.cameraRight,right);UniformVector(d->uniforms.cameraUp,up);
    UniformVector(d->uniforms.boundsMin,sdf->bounds.start);UniformVector(d->uniforms.boundsMax,sdf->bounds.end);
    glUniform2f(d->uniforms.viewportSize,(float)width,(float)height);
    UF(tanHalfFov,tanf(camera->fov*.00872664626f));UF(aspectRatio,displayAspect);
    UF(hitTolerance,.001f);
    GLfloat mv[16],projection[16],vp[16];glGetFloatv(GL_MODELVIEW_MATRIX,mv);glGetFloatv(GL_PROJECTION_MATRIX,projection);
    for(int c=0;c<4;++c)for(int r=0;r<4;++r){float sum=0;for(int k=0;k<4;++k)sum+=projection[k*4+r]*mv[c*4+k];vp[c*4+r]=sum;}
    glUniformMatrix4fv(d->uniforms.viewProjection,1,GL_FALSE,vp);
    float pad=sdf->config.bodySmoothness+0.08f;
    Vector3 bStart=Vec3_Sub(sdf->bounds.start,Vec3_Create(pad,pad,pad));
    Vector3 bEnd=Vec3_Add(sdf->bounds.end,Vec3_Create(pad,pad,pad));
    float sMinX=(float)width,sMaxX=0.0f,sMinY=(float)height,sMaxY=0.0f;
    bool cameraInsideOrNear=false;
    for(int corner=0;corner<8;++corner) {
        Vector3 pos=Vec3_Create((corner&1)?bEnd.x:bStart.x,(corner&2)?bEnd.y:bStart.y,(corner&4)?bEnd.z:bStart.z);
        Vector3 delta=Vec3_Sub(pos,camera->position);float z=Vec3_Dot(delta,forward);
        if(z<camera->nearPlane){cameraInsideOrNear=true;break;}
        float x=(Vec3_Dot(delta,right)/(z*tangent*aspect)+1.0f)*0.5f*(float)width;
        float y=(Vec3_Dot(delta,up)/(z*tangent)+1.0f)*0.5f*(float)height;
        sMinX=fminf(sMinX,x);sMaxX=fmaxf(sMaxX,x);
        sMinY=fminf(sMinY,y);sMaxY=fmaxf(sMaxY,y);
    }
    bool applyScissor=false;
    GLint scX=0,scY=0;GLsizei scW=width,scH=height;
    if(!cameraInsideOrNear&&sMaxX>=0.0f&&sMinX<(float)width&&sMaxY>=0.0f&&sMinY<(float)height) {
        scX=(GLint)fmaxf(0.0f,floorf(sMinX));
        scY=(GLint)fmaxf(0.0f,floorf(sMinY));
        GLint scX1=(GLint)fminf((float)(width-1),ceilf(sMaxX));
        GLint scY1=(GLint)fminf((float)(height-1),ceilf(sMaxY));
        if(scX1>=scX&&scY1>=scY) {
            scW=(GLsizei)(scX1-scX+1);scH=(GLsizei)(scY1-scY+1);
            applyScissor=true;
        }
    }
    if(applyScissor){glEnable(GL_SCISSOR_TEST);glScissor(scX,scY,scW,scH);}
    glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
    if(applyScissor){glDisable(GL_SCISSOR_TEST);}
    glUseProgram(0);glBindTexture(GL_TEXTURE_BUFFER,0);glBindBuffer(GL_TEXTURE_BUFFER,0);
#undef UI
#undef UF
    return true;
}
void OpenGLRenderer_Finish(void){glFinish();}

/* Presupuesto de 30-60 Hz: 29.5 ms para esta consulta (SDF y ojos), con margen
 * de ~3.8 ms para CPU y presentación (total 33.3 ms = 30 FPS).
 * La resolución se mantiene al 100% mientras la tasa sea >= 30 FPS; la calidad
 * baja tras 4 muestras si se cae de 30 FPS y sube tras 24 si hay margen suficiente. */
static const float SDF_TARGET_GPU_MS=29.5f,SDF_MIN_SCALE=.60f,SDF_MAX_SCALE=1.f;
static const float SDF_EMA_ALPHA=.15f,SDF_DOWN_HYSTERESIS=1.06f,SDF_UP_HYSTERESIS=.85f;
static const float SDF_MAX_DOWN_STEP=.05f,SDF_MAX_UP_STEP=.025f;
static const unsigned SDF_OVER_SAMPLES=4,SDF_UNDER_SAMPLES=24;

static void UpdateSDFResolution(OpenGLRendererData* d,float gpuMs) {
    if(!isfinite(gpuMs)||gpuMs<=0)return;
    d->filteredGpuMs=d->filteredGpuMs>0?
        d->filteredGpuMs+(gpuMs-d->filteredGpuMs)*SDF_EMA_ALPHA:gpuMs;
    if(d->filteredGpuMs>SDF_TARGET_GPU_MS*SDF_DOWN_HYSTERESIS) {
        ++d->overBudget;d->underBudget=0;
    } else if(d->filteredGpuMs<SDF_TARGET_GPU_MS*SDF_UP_HYSTERESIS) {
        ++d->underBudget;d->overBudget=0;
    } else {d->overBudget=0;d->underBudget=0;}
    float target=d->resolutionScale*sqrtf(SDF_TARGET_GPU_MS/d->filteredGpuMs);
    if(d->overBudget>=SDF_OVER_SAMPLES) {
        d->resolutionScale=fmaxf(SDF_MIN_SCALE,fmaxf(target,d->resolutionScale-SDF_MAX_DOWN_STEP));
        d->overBudget=0;
    } else if(d->underBudget>=SDF_UNDER_SAMPLES) {
        d->resolutionScale=fminf(SDF_MAX_SCALE,fminf(target,d->resolutionScale+SDF_MAX_UP_STEP));
        d->underBudget=0;
    }
}

bool OpenGLRenderer_BeginSDFFrame(Renderer3D* renderer,ICamera* camera,int width,int height,bool adaptive) {
    if(!renderer||!renderer->user_data||!camera||width<=0||height<=0)return false;
    OpenGLRendererData* d=renderer->user_data;
    /* GL_TIME_ELAPSED no admite consultas anidadas del mismo target. */
    if(d->meshQueryIssued) {
        glEndQuery(GL_TIME_ELAPSED);d->meshQueryPending[d->meshQueryFrame%4]=true;
        d->meshQueryIssued=false;
    }
    /* Compilar antes de iniciar la consulta: el arranque no es coste de reproducción. */
    if(!d->sdfProgram&&!InitSDFProgram(d))return false;
    if(!d->sdfQueries[0])glGenQueries(4,d->sdfQueries);
    if(d->resolutionScale<=0)d->resolutionScale=SDF_MAX_SCALE;
    if(!adaptive){d->resolutionScale=SDF_MAX_SCALE;d->filteredGpuMs=0;d->overBudget=0;d->underBudget=0;}
    /* Seleccionar la consulta más antigua, también si se saltó una ranura
     * ocupada. La cola GPU conserva el orden de emisión. */
    for(unsigned pending=0;pending<4;++pending) {
        unsigned i=4,oldest=0;
        for(unsigned slot=0;slot<4;++slot)if(d->queryPending[slot]) {
            unsigned age=d->queryFrame-d->querySerial[slot];
            if(i==4||age>oldest){i=slot;oldest=age;}
        }
        if(i==4)break;
        GLint ready=0;glGetQueryObjectiv(d->sdfQueries[i],GL_QUERY_RESULT_AVAILABLE,&ready);
        if(!ready)break;
        GLuint64 ns=0;glGetQueryObjectui64v(d->sdfQueries[i],GL_QUERY_RESULT,&ns);
        d->gpuMs=(float)((double)ns/1e6);d->queryPending[i]=false;
        if(adaptive&&fabsf(d->queryScale[i]-d->resolutionScale)<.0001f)
            UpdateSDFResolution(d,d->gpuMs);
    }
    int rw=(int)ceilf(width*d->resolutionScale),rh=(int)ceilf(height*d->resolutionScale);
    if(!d->sdfFramebuffer){glGenFramebuffers(1,&d->sdfFramebuffer);glGenTextures(1,&d->sdfColor);glGenRenderbuffers(1,&d->sdfDepth);}
    glBindFramebuffer(GL_FRAMEBUFFER,d->sdfFramebuffer);
    if(width>d->sdfAllocatedWidth||height>d->sdfAllocatedHeight) {
        int aw=width>d->sdfAllocatedWidth?width:d->sdfAllocatedWidth;
        int ah=height>d->sdfAllocatedHeight?height:d->sdfAllocatedHeight;
        glBindTexture(GL_TEXTURE_2D,d->sdfColor);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,aw,ah,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,d->sdfColor,0);
        glBindRenderbuffer(GL_RENDERBUFFER,d->sdfDepth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,aw,ah);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,d->sdfDepth);
        glBindTexture(GL_TEXTURE_2D,0);glBindRenderbuffer(GL_RENDERBUFFER,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_FRAMEBUFFER,0);return false;}
        d->sdfAllocatedWidth=aw;d->sdfAllocatedHeight=ah;
    }
    d->sdfWidth=rw;d->sdfHeight=rh;
    unsigned q=d->queryFrame%4;
    d->queryIssued=!d->queryPending[q];
    if(d->queryIssued){d->querySerial[q]=d->queryFrame;d->queryScale[q]=d->resolutionScale;glBeginQuery(GL_TIME_ELAPSED,d->sdfQueries[q]);}
    d->sdfFrameActive=true;
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);OpenGLRenderer_SetupCamera(camera,width,height);
    glViewport(0,0,rw,rh);
    return true;
}
void OpenGLRenderer_EndSDFFrame(Renderer3D* renderer,int width,int height) {
    if(!renderer||!renderer->user_data)return;
    OpenGLRendererData* d=renderer->user_data;if(!d->sdfFrameActive)return;
    if(d->queryIssued){glEndQuery(GL_TIME_ELAPSED);d->queryPending[d->queryFrame%4]=true;}
    ++d->queryFrame;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,d->sdfFramebuffer);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,d->sdfWidth,d->sdfHeight,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);d->sdfFrameActive=false;
}
float OpenGLRenderer_GetSDFGpuMs(const Renderer3D* renderer){return renderer&&renderer->user_data?((OpenGLRendererData*)renderer->user_data)->gpuMs:0;}
float OpenGLRenderer_GetSDFResolutionScale(const Renderer3D* renderer){return renderer&&renderer->user_data?((OpenGLRendererData*)renderer->user_data)->resolutionScale:0;}

bool OpenGLRenderer_ValidateSDF(Renderer3D* renderer,const MonsterSDF* sdf,const Vector3* points,size_t count,float* maxError) {
    if(!renderer||!renderer->user_data||!sdf||!points||count==0||count>4096||!maxError)return false;
    OpenGLRendererData* d=renderer->user_data;if(!d->sdfProgram)return false;
    size_t total=d->sdfCount+count;
    if(total>d->sdfCapacity){float (*data)[4]=realloc(d->sdfData,total*sizeof(*data));if(!data)return false;d->sdfData=data;d->sdfCapacity=total;}
    for(size_t i=0;i<count;++i)PackVector(d->sdfData[d->sdfCount+i],points[i]);
    float* samples=calloc(count*4,sizeof(float));if(!samples)return false;
    GLint previousDraw,previousRead,viewport[4];glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);glGetIntegerv(GL_VIEWPORT,viewport);
    GLuint framebuffer,texture;glGenFramebuffers(1,&framebuffer);glGenTextures(1,&texture);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,(GLsizei)count,1,0,GL_RGBA,GL_FLOAT,NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(ok) {
        glViewport(0,0,(GLsizei)count,1);glDisable(GL_DEPTH_TEST);
        glBindBuffer(GL_TEXTURE_BUFFER,d->sdfBuffer);glBufferData(GL_TEXTURE_BUFFER,total*sizeof(*d->sdfData),d->sdfData,GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_BUFFER,d->sdfTexture);
        GLuint p=d->sdfProgram;glUseProgram(p);
        glUniform1i(d->uniforms.validationMode,1);
        glUniform1i(d->uniforms.validationPointBase,(GLint)d->sdfCount);
        glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
        glReadPixels(0,0,(GLsizei)count,1,GL_RGBA,GL_FLOAT,samples);
        glUniform1i(d->uniforms.validationMode,0);
        *maxError=0;
        for(size_t i=0;i<count;++i) {
            float gpu=samples[i*4];
            float cpu=MonsterSDF_EvaluateVisualDistance(sdf,points[i]);
            if(!isfinite(gpu)||!isfinite(cpu)){ok=false;break;}
            *maxError=fmaxf(*maxError,fabsf(cpu-gpu));
            /* La distancia lejana de una fase omitida puede cambiar; su banda
             * superficial y las muestras normales deben conservar la paridad. */
            if(fabsf(cpu)<.002f) {
                if(!isfinite(samples[i*4+1])){ok=false;break;}
                *maxError=fmaxf(*maxError,fabsf(cpu-samples[i*4+1]));
            }
        }
        glUseProgram(0);glEnable(GL_DEPTH_TEST);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)previousDraw);glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previousRead);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glBindTexture(GL_TEXTURE_2D,0);glBindTexture(GL_TEXTURE_BUFFER,0);glBindBuffer(GL_TEXTURE_BUFFER,0);
    glDeleteTextures(1,&texture);glDeleteFramebuffers(1,&framebuffer);free(samples);return ok;
}

/* Adaptador de plataforma para demos nuevas; SDL/GL queda confinado aquí. */
struct OpenGLDemoWindow { SDL_Window* window; SDL_GLContext context; };
OpenGLDemoWindow* OpenGLDemoWindow_Create(const char* title,int width,int height) {
    return OpenGLDemoWindow_CreateMSAA(title,width,height,0);
}
OpenGLDemoWindow* OpenGLDemoWindow_CreateMSAA(const char* title,int width,int height,int samples) {
    if(SDL_Init(SDL_INIT_VIDEO)<0)return NULL;
    OpenGLDemoWindow* w=calloc(1,sizeof(*w));
    if(!w) { SDL_Quit(); return NULL; }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,samples>0?1:0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,samples>0?samples:0);
    w->window=SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    if(w->window)w->context=SDL_GL_CreateContext(w->window);
    if(!w->context && samples>0) {
        if(w->window)SDL_DestroyWindow(w->window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);
        w->window=SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
        if(w->window)w->context=SDL_GL_CreateContext(w->window);
    }
    if(!w->context) { OpenGLDemoWindow_Free(w); return NULL; }
    int actual=0;SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES,&actual);
    printf("[GPU] %s | %s | MSAA=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),actual);
    SDL_GL_SetSwapInterval(0); return w;
}
OpenGLDemoInput OpenGLDemoWindow_Poll(OpenGLDemoWindow* w) {
    (void)w; OpenGLDemoInput input={.view=-1, .digitKey=-1}; SDL_Event e;
    while(SDL_PollEvent(&e)) {
        if(e.type==SDL_QUIT)input.quit=true;
        if(e.type==SDL_KEYDOWN) {
            if(e.key.keysym.sym==SDLK_ESCAPE)input.quit=true;
            if(e.key.keysym.sym==SDLK_SPACE)input.togglePause=true;
            if(e.key.keysym.sym==SDLK_LEFT)input.surfaceSelect=-1;
            if(e.key.keysym.sym==SDLK_RIGHT)input.surfaceSelect=1;
            if(e.key.keysym.sym==SDLK_UP || e.key.keysym.sym==SDLK_EQUALS)input.surfaceAdjust=1;
            if(e.key.keysym.sym==SDLK_DOWN || e.key.keysym.sym==SDLK_MINUS)input.surfaceAdjust=-1;
            if(e.key.keysym.sym==SDLK_r) { input.surfaceSeed=true; input.keyR=true; }
            if(e.key.keysym.sym==SDLK_m) input.keyM=true;
            if(e.key.keysym.sym==SDLK_a) input.keyA=true;
            if(e.key.keysym.sym==SDLK_g) input.keyG=true;
            if(e.key.keysym.sym==SDLK_c)input.surfacePigment=true;
            if(e.key.keysym.sym==SDLK_TAB)input.surfaceDebugNext=true;
            if(e.key.keysym.sym==SDLK_s)input.surfaceToggle=true;
            if(e.key.keysym.sym==SDLK_f)input.keyF=true;
            if(e.key.keysym.sym==SDLK_LEFTBRACKET)input.furLengthAdjust=-1;
            if(e.key.keysym.sym==SDLK_RIGHTBRACKET)input.furLengthAdjust=1;
            if(e.key.keysym.sym==SDLK_SEMICOLON)input.furDensityAdjust=-1;
            if(e.key.keysym.sym==SDLK_QUOTE)input.furDensityAdjust=1;
            if(e.key.keysym.sym==SDLK_d)input.toggleDebug=true;
            if(e.key.keysym.sym>=SDLK_1 && e.key.keysym.sym<=SDLK_9)input.view=e.key.keysym.sym-SDLK_1;
            if(e.key.keysym.sym>=SDLK_1 && e.key.keysym.sym<=SDLK_9)input.digitKey=e.key.keysym.sym-SDLK_1;
            if(e.key.keysym.sym==SDLK_0)input.digitKey=9;
        }
    }
    return input;
}
void OpenGLDemoWindow_Swap(OpenGLDemoWindow* w) { if(w)SDL_GL_SwapWindow(w->window); }
void OpenGLDemoWindow_Free(OpenGLDemoWindow* w) {
    if(!w)return;
    if(w->context)SDL_GL_DeleteContext(w->context);
    if(w->window)SDL_DestroyWindow(w->window);
    free(w); SDL_Quit();
}
double OpenGLDemoWindow_Time(void) { return (double)SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency(); }
void OpenGLRenderer_DebugLine(Vector3 a,Vector3 b,Color color) {
    glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_LINE_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST); glLineWidth(2);
    glColor4ub(color.r,color.g,color.b,color.a);
    glBegin(GL_LINES); glVertex3f(a.x,a.y,a.z); glVertex3f(b.x,b.y,b.z); glEnd();
    glPopAttrib();
}
void OpenGLRenderer_PushModelTranslation(float x, float y, float z) {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(x, y, z);
}
void OpenGLRenderer_ModelTransform(Vector3 translation,float yawDegrees,float scale,Vector3 center) {
    OpenGLRenderer_PushModelTranslation(translation.x,translation.y,translation.z);
    glRotatef(yawDegrees,0,1,0);
    glScalef(scale,scale,scale);
    glTranslatef(-center.x,-center.y,-center.z);
}
void OpenGLRenderer_PopModelMatrix(void) {
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

#include "MonsterSurfaceShader.generated.h"
_Static_assert(SURFACE_RECIPE_ROWS==46,"Actualizar contrato GLSL de superficie al cambiar regiones");
static bool InitSurfaceProgram(OpenGLRendererData* d) {
    if(d->surfaceProgram)return true;
    if(d->surfaceFailed)return false;
    GLuint vs=CompileShader(GL_VERTEX_SHADER,surfaceVertexSource,sizeof(surfaceVertexSource)/sizeof(*surfaceVertexSource));
    GLuint fs=CompileShader(GL_FRAGMENT_SHADER,surfaceFragmentSource,sizeof(surfaceFragmentSource)/sizeof(*surfaceFragmentSource));
    if(!vs || !fs) { if(vs)glDeleteShader(vs); if(fs)glDeleteShader(fs); d->surfaceFailed=true; return false; }
    GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {
        char log[4096]; glGetProgramInfoLog(p,sizeof(log),NULL,log); fprintf(stderr,"[Superficie GLSL] %s\n",log);
        glDeleteProgram(p); d->surfaceFailed=true; return false;
    }
    d->surfaceProgram=p;
    d->shellResolutionLocation=glGetUniformLocation(p,"shellResolution");
    d->regionalLODLocation=glGetUniformLocation(p,"regionalShellLOD");
    d->shellSamplesLocation=glGetUniformLocation(p,"shellSamples");
    d->guardVisibilityLocation=glGetUniformLocation(p,"guardVisibility");
    d->surfaceRecipeLocation=glGetUniformLocation(p,"surfaceRecipe");
    d->pigmentSeedLocation=glGetUniformLocation(p,"pigmentSeed");
    d->scaleSeedLocation=glGetUniformLocation(p,"scaleSeed");
    d->furSeedLocation=glGetUniformLocation(p,"furSeed");
    d->surfaceDebugLocation=glGetUniformLocation(p,"surfaceDebug");
    d->shellFractionLocation=glGetUniformLocation(p,"shellFraction");
    d->furPassLocation=glGetUniformLocation(p,"furPass");
    d->shellCountLocation=glGetUniformLocation(p,"shellCount");
    return true;
}
static bool InitGuardProgram(OpenGLRendererData* d) {
    if(d->guardProgram)return true;
    GLuint vs=CompileShader(GL_VERTEX_SHADER,furGuardVertexSource,sizeof(furGuardVertexSource)/sizeof(*furGuardVertexSource));
    GLuint fs=CompileShader(GL_FRAGMENT_SHADER,furGuardFragmentSource,sizeof(furGuardFragmentSource)/sizeof(*furGuardFragmentSource));
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"[Pelaje] %s\n",log);glDeleteProgram(p);return false;}
    d->guardProgram=p;
#define GUARD_LOCATION(field,name) d->guard.field=glGetUniformLocation(p,name)
    GUARD_LOCATION(recipe,"surfaceRecipe");GUARD_LOCATION(pigmentSeed,"pigmentSeed");GUARD_LOCATION(furSeed,"furSeed");
    GUARD_LOCATION(debug,"surfaceDebug");GUARD_LOCATION(visibility,"guardVisibility");
    GUARD_LOCATION(geometry,"geometryData");GUARD_LOCATION(surface,"surfaceData");GUARD_LOCATION(coverage,"guardCoverage");
#undef GUARD_LOCATION
    return true;
}
static bool PrepareRoots(OpenGLRendererData* d,GpuMesh* cache,const Mesh* mesh,bool changed) {
    if(cache->rootsValid&&!changed&&cache->rootSeed==mesh->surfaceRecipe.furSeed)return true;
    FurRootSet roots={0};
    if(!FurRootSet_Build(&roots,mesh,mesh->surfaceRecipe.furSeed,1800,90000))return false;
    float (*packed)[22]=calloc(roots.count?roots.count:1,sizeof(*packed));
    if(!packed){FurRootSet_Free(&roots);return false;}
    for(size_t i=0;i<roots.count;++i) {
        const FurRoot* r=&roots.roots[i];
        for(int j=0;j<3;++j)packed[i][j]=(float)mesh->indices[3*r->triangleIndex+j];
        packed[i][3]=r->barycentric.x;packed[i][4]=r->barycentric.y;packed[i][5]=r->barycentric.z;
        packed[i][6]=Fur_Random(r->randomSeed);packed[i][7]=Fur_Random(r->randomSeed^0x913u);
        float weights[3]={r->barycentric.x,r->barycentric.y,r->barycentric.z};
        for(int j=0;j<3;++j) {
            const SurfaceCoordinate* v=&mesh->vertices[mesh->indices[3*r->triangleIndex+j]].surface;
            float values[14]={v->position.x,v->position.y,v->position.z,v->normal.x,v->normal.y,v->normal.z,
                v->flowDirection.x,v->flowDirection.y,v->flowDirection.z,v->region,v->secondaryRegion,v->blend,v->ventral,v->integumentMask};
            for(int k=0;k<14;++k)packed[i][8+k]+=values[k]*weights[j];
        }
    }
    if(!cache->rootVao)glGenVertexArrays(1,&cache->rootVao);
    if(!cache->rootVbo)glGenBuffers(1,&cache->rootVbo);
    glBindVertexArray(cache->rootVao);glBindBuffer(GL_ARRAY_BUFFER,cache->rootVbo);
    glBufferData(GL_ARRAY_BUFFER,roots.count*sizeof(*packed),packed,GL_STATIC_DRAW);
    const unsigned offsets[]={0,3,6,8,11,14,17,21};const int sizes[]={3,3,2,3,3,3,4,1};
    for(unsigned j=0;j<8;++j){glEnableVertexAttribArray(j);glVertexAttribPointer(j,sizes[j],GL_FLOAT,GL_FALSE,sizeof(*packed),(void*)(uintptr_t)(offsets[j]*sizeof(float)));glVertexAttribDivisor(j,1);}
    cache->rootCount=roots.count;cache->rootSeed=mesh->surfaceRecipe.furSeed;cache->rootsValid=true;
    d->meshStats.totalBytesUploaded+=roots.count*sizeof(*packed);++d->meshStats.uploadCount;
    ++d->meshStats.furRootBuilds;
    free(packed);FurRootSet_Free(&roots);return true;
}
static void DrawGuards(OpenGLRendererData* d,GpuMesh* cache,const Mesh* mesh,float visibility) {
    if(visibility<=0||!cache->rootCount||!InitGuardProgram(d))return;
    GLint active=0,bindings[2];glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
    for(int i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_BUFFER,&bindings[i]);}
    if(!cache->geometryTexture)glGenTextures(1,&cache->geometryTexture);
    if(!cache->surfaceTexture)glGenTextures(1,&cache->surfaceTexture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_BUFFER,cache->geometryTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,cache->geometryVbo);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_BUFFER,cache->surfaceTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_R32F,cache->surfaceVbo);
    glUseProgram(d->guardProgram);glUniform1i(d->guard.geometry,0);glUniform1i(d->guard.surface,1);
    glUniform4fv(d->guard.recipe,SURFACE_RECIPE_ROWS,&mesh->surfaceRecipe.data[0][0]);
    glUniform1ui(d->guard.pigmentSeed,mesh->surfaceRecipe.pigmentSeed);glUniform1ui(d->guard.furSeed,mesh->surfaceRecipe.furSeed);
    glUniform1i(d->guard.debug,d->surfaceDebug);glUniform1f(d->guard.visibility,visibility);
    glBindVertexArray(cache->rootVao);glDisable(GL_CULL_FACE);
    GLint samples=0;glGetIntegerv(GL_SAMPLES,&samples);
    GLboolean a2c=glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE),a2one=glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE);
    glUniform1i(d->guard.coverage,samples>0);
    if(samples>0){glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);glEnable(GL_SAMPLE_ALPHA_TO_ONE);glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
    glDrawArraysInstanced(GL_TRIANGLE_STRIP,0,10,(GLsizei)cache->rootCount);
    if(samples>0){glEnable(GL_BLEND);glDepthMask(GL_FALSE);}
    if(!a2c)glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    if(!a2one)glDisable(GL_SAMPLE_ALPHA_TO_ONE);
    d->meshStats.guardCandidates=cache->rootCount;
    for(int i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_BUFFER,(GLuint)bindings[i]);}
    glActiveTexture((GLenum)active);
    glBindVertexArray(cache->vao);glUseProgram(d->surfaceProgram);
}
static bool RenderSurfaceMesh(OpenGLRendererData* d,const Mesh* mesh) {
    if(!mesh->vertexCount || !mesh->indexCount)return true;
    if(!InitSurfaceProgram(d))return false;
    GLint previous=0,buffer=0,indexBuffer=0,vao=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&previous); glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&indexBuffer);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    GpuMesh* cached=NULL,*evict=&d->meshCache[0];
    for(unsigned i=0;i<GPU_MESH_CACHE_CAPACITY;++i) {
        GpuMesh* entry=&d->meshCache[i];
        if(entry->owner==mesh && entry->identity==mesh->identity){cached=entry;break;}
        if(!entry->owner || entry->lastUse<evict->lastUse)evict=entry;
    }
    if(!cached) {
        cached=evict;
        if(!cached->vao)glGenVertexArrays(1,&cached->vao);
        if(!cached->geometryVbo)glGenBuffers(1,&cached->geometryVbo);
        if(!cached->surfaceVbo)glGenBuffers(1,&cached->surfaceVbo);
        if(!cached->indexBuffer)glGenBuffers(1,&cached->indexBuffer);
        cached->owner=mesh;cached->identity=mesh->identity;cached->geometryGeneration=UINT64_MAX;cached->surfaceGeneration=UINT64_MAX;
        cached->vertexPointer=NULL;cached->indexPointer=NULL;cached->rootsValid=false;
    }
    cached->lastUse=d->meshFrame;
    bool indicesChanged=cached->indexPointer!=mesh->indices || cached->indexCount!=mesh->indexCount;
    bool geometryChanged=cached->geometryGeneration!=mesh->geometryGeneration || cached->vertexPointer!=mesh->vertices ||
        cached->vertexCount!=mesh->vertexCount || indicesChanged;
    bool surfaceChanged=cached->surfaceGeneration!=mesh->surfaceGeneration || cached->vertexPointer!=mesh->vertices || cached->vertexCount!=mesh->vertexCount;
    if(surfaceChanged||indicesChanged)cached->rootsValid=false;
    double uploadStart=RendererNowMs();uint64_t uploaded=0;
    if(geometryChanged) {
        if(mesh->vertexCount>d->geometryScratchCapacity) {
            GeometryGpuVertex* p=realloc(d->geometryScratch,mesh->vertexCount*sizeof(*p));if(!p)return false;
            d->geometryScratch=p;d->geometryScratchCapacity=mesh->vertexCount;
        }
        for(size_t i=0;i<mesh->vertexCount;++i) {
            const MeshVertex* v=&mesh->vertices[i];GeometryGpuVertex* p=&d->geometryScratch[i];
            p->position[0]=v->position.x;p->position[1]=v->position.y;p->position[2]=v->position.z;
            p->normal[0]=v->normal.x;p->normal[1]=v->normal.y;p->normal[2]=v->normal.z;
            p->color[0]=v->color.r;p->color[1]=v->color.g;p->color[2]=v->color.b;p->color[3]=v->color.a;p->padding=0;
        }
        glBindBuffer(GL_ARRAY_BUFFER,cached->geometryVbo);glBufferData(GL_ARRAY_BUFFER,mesh->vertexCount*sizeof(GeometryGpuVertex),d->geometryScratch,GL_STREAM_DRAW);
        uploaded+=mesh->vertexCount*sizeof(GeometryGpuVertex);
        cached->geometryGeneration=mesh->geometryGeneration;
    }
    if(indicesChanged) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,cached->indexBuffer);glBufferData(GL_ELEMENT_ARRAY_BUFFER,mesh->indexCount*sizeof(MeshIndex),mesh->indices,GL_STATIC_DRAW);
        uploaded+=mesh->indexCount*sizeof(MeshIndex);
    }
    if(surfaceChanged) {
        if(mesh->vertexCount>d->surfaceScratchCapacity) {
            SurfaceGpuVertex* p=realloc(d->surfaceScratch,mesh->vertexCount*sizeof(*p));if(!p)return false;
            d->surfaceScratch=p;d->surfaceScratchCapacity=mesh->vertexCount;
        }
        for(size_t i=0;i<mesh->vertexCount;++i) {
            const MeshVertex* v=&mesh->vertices[i];SurfaceGpuVertex* p=&d->surfaceScratch[i];
            p->position[0]=v->surface.position.x;p->position[1]=v->surface.position.y;p->position[2]=v->surface.position.z;
            p->normal[0]=v->surface.normal.x;p->normal[1]=v->surface.normal.y;p->normal[2]=v->surface.normal.z;
            p->region[0]=v->surface.region;p->region[1]=v->surface.secondaryRegion;p->region[2]=v->surface.blend;p->region[3]=v->surface.ventral;
            p->material=(int)v->material;
            p->flowDirection[0]=v->surface.flowDirection.x;p->flowDirection[1]=v->surface.flowDirection.y;p->flowDirection[2]=v->surface.flowDirection.z;
            p->integumentMask=v->surface.integumentMask;
        }
        glBindBuffer(GL_ARRAY_BUFFER,cached->surfaceVbo);glBufferData(GL_ARRAY_BUFFER,mesh->vertexCount*sizeof(SurfaceGpuVertex),d->surfaceScratch,GL_STATIC_DRAW);
        uploaded+=mesh->vertexCount*sizeof(SurfaceGpuVertex);cached->surfaceGeneration=mesh->surfaceGeneration;
    }
    if(uploaded){++d->meshStats.uploadCount;d->meshStats.totalBytesUploaded+=uploaded;d->meshStats.lastUploadMs=RendererNowMs()-uploadStart;}
    else ++d->meshStats.cacheHitCount;
    cached->vertexPointer=mesh->vertices;cached->indexPointer=mesh->indices;cached->vertexCount=mesh->vertexCount;cached->indexCount=mesh->indexCount;
    glBindVertexArray(cached->vao);
    glUseProgram(d->surfaceProgram);
    glUniform4fv(d->surfaceRecipeLocation,SURFACE_RECIPE_ROWS,&mesh->surfaceRecipe.data[0][0]);
    glUniform1ui(d->pigmentSeedLocation,mesh->surfaceRecipe.pigmentSeed);
    glUniform1ui(d->scaleSeedLocation,mesh->surfaceRecipe.scaleSeed);
    if(d->furSeedLocation>=0)glUniform1ui(d->furSeedLocation,mesh->surfaceRecipe.furSeed);
    glUniform1i(d->surfaceDebugLocation,d->surfaceDebug);

    glBindBuffer(GL_ARRAY_BUFFER,cached->surfaceVbo);
    size_t offsets[6]={
        offsetof(SurfaceGpuVertex,position),
        offsetof(SurfaceGpuVertex,normal),
        offsetof(SurfaceGpuVertex,region),
        offsetof(SurfaceGpuVertex,material),
        offsetof(SurfaceGpuVertex,flowDirection),
        offsetof(SurfaceGpuVertex,integumentMask)
    };
    int sizes[6]={3,3,4,1,3,1};
    for(unsigned i=0;i<6;++i) {
        glEnableVertexAttribArray(8+i);
        glVertexAttribPointer(8+i,sizes[i],i==3?GL_INT:GL_FLOAT,GL_FALSE,sizeof(SurfaceGpuVertex),(const void*)offsets[i]);
    }
    glBindBuffer(GL_ARRAY_BUFFER,cached->geometryVbo);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3,GL_FLOAT,sizeof(GeometryGpuVertex),(const void*)offsetof(GeometryGpuVertex,position));
    glNormalPointer(GL_FLOAT,sizeof(GeometryGpuVertex),(const void*)offsetof(GeometryGpuVertex,normal));
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(GeometryGpuVertex),(const void*)offsetof(GeometryGpuVertex,color));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,cached->indexBuffer);

    int integumentType=(int)(mesh->surfaceRecipe.data[7][0]+0.5f);
    if(d->furPassLocation>=0)glUniform1i(d->furPassLocation,0);
    if(d->shellFractionLocation>=0)glUniform1f(d->shellFractionLocation,0.0f);
    if(d->shellCountLocation>=0)glUniform1i(d->shellCountLocation,0);

    // 1. Dibujar pase base de superficie
    size_t offset=0;
    while(offset<mesh->indexCount) {
        size_t count=mesh->indexCount-offset,max=(size_t)INT32_MAX-((size_t)INT32_MAX%3);
        if(count>max)count=max;
        glDrawElements(GL_TRIANGLES,(GLsizei)count,GL_UNSIGNED_INT,(const void*)(offset*sizeof(MeshIndex))); offset+=count;
    }

    // Resolución proyectada, con conjuntos anidados y peso óptico conservado.
    if(integumentType==2 && mesh->surfaceRecipe.data[11][3]>0) {
        GLfloat mv[16],projection[16];GLint viewport[4];
        glGetFloatv(GL_MODELVIEW_MATRIX,mv);glGetFloatv(GL_PROJECTION_MATRIX,projection);glGetIntegerv(GL_VIEWPORT,viewport);
        float minDepth=INFINITY;float grazing=0;unsigned samples=0;
        size_t stride=mesh->vertexCount/128+1;
        for(size_t i=0;i<mesh->vertexCount;i+=stride) {
            Vector3 p=mesh->vertices[i].position,n=mesh->vertices[i].normal;
            float depth=-(mv[2]*p.x+mv[6]*p.y+mv[10]*p.z+mv[14]);
            if(depth>0)minDepth=fminf(minDepth,depth);
            grazing+=1-fabsf(mv[2]*n.x+mv[6]*n.y+mv[10]*n.z);++samples;
        }
        float pixels=mesh->surfaceRecipe.data[8][0]*mesh->surfaceRecipe.data[11][2]*projection[5]*viewport[3]*.5f/fmaxf(.1f,minDepth);
        float resolution=Fur_ShellResolution(pixels,samples?grazing/samples:0,1);
        if(d->forcedShells>0)resolution=(float)d->forcedShells;
        float visibility=d->disableGuards?0:fmaxf(0,fminf(1,(pixels-3)/18));
        if(visibility>0 && !PrepareRoots(d,cached,mesh,!cached->rootsValid))visibility=0;
        glBindVertexArray(cached->vao);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,cached->indexBuffer);
        float shellSamples[32][2]={{0}};int count=0;
        if(!d->disableShells)count=Fur_ShellSamples(resolution,shellSamples);
        d->meshStats.furShells=count;d->meshStats.furPixels=pixels;
        GLboolean blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE),depth;
        GLint srcRGB,dstRGB,srcAlpha,dstAlpha,cullMode;
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRGB);glGetIntegerv(GL_BLEND_DST_RGB,&dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
        glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);glEnable(GL_CULL_FACE);glCullFace(GL_BACK);
        glUniform1f(d->guardVisibilityLocation,visibility);
        if(count) {
            glUniform1f(d->shellResolutionLocation,resolution);glUniform1i(d->regionalLODLocation,d->forcedShells==0);
            glUniform1i(d->furPassLocation,1);glUniform1i(d->shellCountLocation,count);
            glUniform2fv(d->shellSamplesLocation,count,&shellSamples[0][0]);
            glDrawElementsInstanced(GL_TRIANGLES,(GLsizei)mesh->indexCount,GL_UNSIGNED_INT,0,count);
        }
        DrawGuards(d,cached,mesh,visibility);
        glDepthMask(depth);glCullFace((GLenum)cullMode);if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
        glBlendFuncSeparate((GLenum)srcRGB,(GLenum)dstRGB,(GLenum)srcAlpha,(GLenum)dstAlpha);if(!blend)glDisable(GL_BLEND);
        glUniform1i(d->furPassLocation,0);
    }

    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_NORMAL_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
    for(unsigned i=0;i<6;++i)glDisableVertexAttribArray(8+i);
    glBindVertexArray((GLuint)vao);glUseProgram((GLuint)previous); glBindBuffer(GL_ARRAY_BUFFER,(GLuint)buffer); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,(GLuint)indexBuffer);
    return true;
}
void OpenGLRenderer_SetSurfaceDebug(Renderer3D* r,int mode) {
    if(r && r->user_data)((OpenGLRendererData*)r->user_data)->surfaceDebug=mode>=0&&mode<=22?mode:0;
}
void OpenGLRenderer_SetFurQuality(Renderer3D* r,bool shells,bool guards,int forcedShells) {
    if(!r||!r->user_data)return;
    OpenGLRendererData* d=r->user_data;d->disableShells=!shells;d->disableGuards=!guards;
    d->forcedShells=forcedShells<0?0:forcedShells>32?32:forcedShells;
}
bool OpenGLRenderer_SurfaceReady(Renderer3D* r) {
    return r && r->user_data && InitSurfaceProgram(r->user_data);
}
bool OpenGLRenderer_CheckErrors(void) {
    bool ok=true; GLenum error;
    while((error=glGetError())!=GL_NO_ERROR) { fprintf(stderr,"[OpenGL] error 0x%x\n",error); ok=false; }
    return ok;
}
OpenGLMeshPerformanceStats OpenGLRenderer_GetMeshPerformanceStats(const Renderer3D* r) {
    OpenGLMeshPerformanceStats empty={0};
    return r&&r->user_data?((const OpenGLRendererData*)r->user_data)->meshStats:empty;
}
