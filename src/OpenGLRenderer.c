#define GL_GLEXT_PROTOTYPES
#include "OpenGLRenderer.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "Monster.h"
#include "MonsterAnimation.h"
#include "SurfaceMapper.h"
#include "EyeTexture.h"
#include "MathUtils.h"
#include "Fur.h"
#include "GpuDevicePolicy.h"
#ifdef __linux__
#include <unistd.h>
#endif
#ifdef _WIN32
/* Optimus/PowerXpress solicitan el adaptador dedicado antes del contexto. */
__declspec(dllexport) unsigned long NvOptimusEnablement=1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance=1;
#endif
#include "MarchingCubesTables.h"
#include <string.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* Algunas pantallas PRIME ofrecen un framebuffer de ventana sin MSAA.
 * Conservar las muestras solicitadas en un destino propio y resolver al presentar. */
static GLuint demoFramebuffer=0;
static int demoWidth=0,demoHeight=0;
static void ResolveDemoFramebuffer(void) {
    if(!demoFramebuffer)return;
    GLint read,draw;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,demoFramebuffer);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,demoWidth,demoHeight,0,0,demoWidth,demoHeight,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)read);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)draw);
}
static GLint BeginDemoReadback(void) {
    GLint read;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    if(demoFramebuffer&&(GLuint)read==demoFramebuffer){ResolveDemoFramebuffer();glBindFramebuffer(GL_READ_FRAMEBUFFER,0);}
    return read;
}
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
    GLuint skinBase,skinWeights;
    const AnatomyDeformer* skinOwner;
    uint64_t skinBindingGeneration,skinGeometryGeneration;
    bool gpuPosed;
} GpuMesh;
typedef struct EyeTextureCache { GLuint texture; uint64_t fingerprint,lastUse; } EyeTextureCache;
static void OpenGL_RenderEyeCallback(Renderer3D*,const struct MonsterVisualEye*);

/* ============================================================
 * RENDER STATE DATA
 * ============================================================ */

typedef struct SDFUniformLocations {
        GLint validationMode,validationPointBase,sceneData;
        GLint partBase,partCount,connectorBase,connectorCount,mouthBase,mouthCount,axialBase,axialCount;
        GLint bodySmoothness,tileDataBase,tileColumns,cameraPosition,cameraForward,cameraRight,cameraUp;
        GLint boundsMin,boundsMax,viewportSize,tanHalfFov,aspectRatio,hitTolerance,viewProjection;
        GLint surfaceRecipe,pigmentSeed,scaleSeed,furSeed,sdfHasSurface,sdfFurEnabled;
} SDFUniformLocations;

typedef struct OpenGLRendererData {
    struct {
        GLuint sampleProgram,classifyProgram,emitProgram,finalizeProgram,resetProgram,prepareProgram,validateProgram,intervalProgram,renderProgram;
        GLuint samples,cases,tables,command,vertices,validation,intervalValidation,vao;
        GLuint* timestamps[4];unsigned queryCapacity[4],timingSlabs[4];
        uint64_t timingGeneration[4];bool pending[4];unsigned frame;GLsync frameFences[2];
        size_t sampleCapacity,cellCapacity,vertexCapacity;
        float (*cachedScene)[4];size_t cachedSceneCapacity,cachedSceneCount;
        GpuSDFSurfaceGrid cachedGrid;
        size_t cachedParts,cachedConnectors,cachedMouths;int cachedAxial;
        float cachedSmoothness;bool denseValid;
        float extractionVoxel;
        GpuSDFSurfaceStats stats;
    } compute;
    struct {
        GLuint releaseProgram,sampleProgram,classifyProgram,allocateProgram,persistProgram,emitProgram,prepareProgram,rootProgram,statisticsProgram;
        GLuint bank1,scratch,bricks,jobs,draws,allocator,links,indices,edges,rootQueue,metrics[4],completion[2];
        uint64_t submittedGeneration[2];bool failed;
        size_t bank1Capacity,scratchCapacity,brickCapacity,jobCapacity,drawCapacity,allocatorCapacity,linkCapacity,indexCapacity,edgeBytes,rootQueueBytes;
        unsigned brickCells,pageCapacity,sampleSlots,bankSize,batchSize,edgeCapacity;bool initialized;
        GLuint cachedSampleProgram,cachedRootProgram,sampleClaimProgram,materialClaimProgram,materialProgram,cachedMaterialProgram;
        uint64_t paletteFingerprint;bool hasPalette;
        GpuSDFBrickJob* uploadJobs;size_t uploadJobCapacity;
        GLuint pointMap;GLuint* pointMapCPU;size_t pointMapCapacity,pointMapCPUCapacity;
    } sparse;
    struct {int partBase,connectorBase,mouthBase,axialBase,tileDataBase,columns;} scene;
    bool wireframe;
    struct {bool enabled;float maxStep;unsigned steps;} reference;
    GLuint sdfProgram,sdfCachedProgram,sdfMouthCache,sdfBuffer,sdfTexture;
    bool sweepSorted;
    GLuint surfaceProgram,guardProgram;
    struct {
        GLuint program,paletteBuffer;
        GLint countLocation;
        const Mesh* mesh;
        const AnatomyDeformer* binding;
        uint64_t generation;
        AnatomyPosePalette palette;
        bool failed;
    } skin;
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
    MorphHandoffSet handoffs;
    int handoffPass;
    AABB3D traceRegions[16];
    size_t traceRegionCount;
    GLuint handoffBuffer,handoffTexture;
    SurfaceMorphDomain materialDomain;
    GLuint materialBuffer,materialTexture;
    GLuint poseBuffer,poseTexture;
    size_t poseSegmentCount;
    Vector3 cranialAttachment,cranialAxis;
    float cranialCollar;
    Quaternion surfaceJawRotation;
    Quaternion surfaceHeadRotation;
    Vector3 surfaceHeadShift;
    Vector3 surfaceJawPivot,surfaceJawTarget;
    bool surfaceJawActive;
    SurfaceMorphDomain poseDomain;
    int surfaceDebug;
    bool surfaceFailed;
    SDFUniformLocations uniforms,cachedUniforms;
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

/* API antigua conservada solo para llamadas vacías de otros demostradores. */
void OpenGLRenderer_SetMeshClipRegions(Renderer3D* renderer,const AABB3D* bounds,size_t count) {
    (void)bounds;(void)count;
    OpenGLRenderer_SetMorphHandoffs(renderer,NULL,-1);
}

bool OpenGLRenderer_SetMorphHandoffs(Renderer3D* renderer,const MorphHandoffSet* set,int pass) {
    if(!renderer||!renderer->user_data)return false;
    OpenGLRendererData* d=renderer->user_data;
    size_t total=0;
    if(set) {
        if(set->count>MORPH_HANDOFF_MAX_INTERFACES)return false;
        for(size_t i=0;i<set->count;++i) {
            if(set->interfaces[i].segmentCount>MORPH_HANDOFF_MAX_SEGMENTS)return false;
            total+=set->interfaces[i].segmentCount;
        }
        if(total>MORPH_HANDOFF_GPU_SEGMENTS)return false;
        d->handoffs=*set;
    } else MorphHandoffSet_Init(&d->handoffs);
    d->handoffPass=pass;
    return true;
}

bool OpenGLRenderer_SetGpuSurfacePose(Renderer3D* renderer,const Monster* monster) {
    if(!renderer||!renderer->user_data)return false;
    OpenGLRendererData* d=renderer->user_data;
    d->poseSegmentCount=0;
    d->cranialCollar=0;
    d->surfaceJawActive=false;
    if(!monster)return true;
    const MonsterAnimation* animation=monster->animation;
    if(!animation||animation->restFingerprint!=AnatomyGraph_Fingerprint(&monster->anatomyGraph))return false;
    const Skeleton* skeleton=&animation->rig.skeleton;
    d->cranialAttachment=animation->rig.cranialAttachment;
    d->cranialAxis=animation->rig.cranialAxis;
    d->cranialCollar=animation->rig.cranialCollar;
    SkeletonPose rest;
    if(!SkeletonPose_Init(&rest,skeleton))return false;
    const Rig* rig=&animation->rig;
    if(rig->headJoint>=0) {
        d->surfaceHeadRotation=Quat_Multiply(animation->pose.joints[rig->headJoint].worldRotation,
            Quat_Inverse(rest.joints[rig->headJoint].worldRotation));
        d->surfaceHeadShift=Vec3_Sub(animation->pose.joints[rig->headJoint].worldPosition,
            Quat_RotateVector(d->surfaceHeadRotation,rest.joints[rig->headJoint].worldPosition));
    }
    if(rig->jawJoint>=0&&rig->headJoint>=0&&monster->mouthCount==1) {
        const Mouth* mouth=&monster->mouths[0];
        Quaternion local=animation->pose.joints[rig->jawJoint].localRotation;
        Vector3 axis=Transform3D_RotateVector(mouth->rotation,Vec3_Create(1,0,0));
        float angle=2*atan2f(local.x*axis.x+local.y*axis.y+local.z*axis.z,local.w)-Mouth_GetJawAngle(mouth)*.01745329252f;
        Quaternion head=Quat_Multiply(animation->pose.joints[rig->headJoint].worldRotation,
            Quat_Inverse(rest.joints[rig->headJoint].worldRotation));
        d->surfaceJawRotation=Quat_Multiply(head,Quat_FromAxisAngle(axis,angle));
        Vector3 pivot=rest.joints[rig->jawJoint].worldPosition;
        Vector3 target=Vec3_Add(animation->pose.joints[rig->headJoint].worldPosition,
            Quat_RotateVector(head,Vec3_Sub(pivot,rest.joints[rig->headJoint].worldPosition)));
        d->surfaceHeadRotation=head;d->surfaceJawPivot=pivot;d->surfaceJawTarget=target;
        d->surfaceJawActive=true;
    }
    if(!SurfaceMapper_BuildMorphDomain(&d->poseDomain,&monster->anatomyGraph,
        &monster->anatomyGraph,&monster->surfaceMapping))return false;
    float rows[6*ANATOMY_MAX_CONNECTIONS][4];size_t count=d->poseDomain.count;
    for(size_t i=0;i<count;++i) {
        for(int end=0;end<2;++end) {
            float* segment=d->poseDomain.data[i][end];
            Vector3 p=Vec3_Create(segment[0],segment[1],segment[2]);
            const AnatomyNode* node=NULL;
            for(size_t n=0;n<monster->anatomyGraph.nodeCount;++n)
                if(Vec3_LengthSq(Vec3_Sub(p,monster->anatomyGraph.nodes[n].center))<1e-12f) {node=&monster->anatomyGraph.nodes[n];break;}
            if(!node)return false;
            int j=Skeleton_FindJoint(skeleton,node->id);if(j<0)return false;
            Quaternion q=Quat_Multiply(animation->pose.joints[j].worldRotation,Quat_Inverse(rest.joints[j].worldRotation));
            Vector3 shift=Vec3_Sub(animation->pose.joints[j].worldPosition,Quat_RotateVector(q,rest.joints[j].worldPosition));
            float data[3][4]={{p.x,p.y,p.z,segment[3]},{q.x,q.y,q.z,q.w},{shift.x,shift.y,shift.z,node->region==ANATOMY_REGION_HEAD?1.f:0.f}};
            memcpy(rows+6*i+3*end,data,sizeof(data));
        }
    }
    if(!count)return false;
    if(!d->poseBuffer)glGenBuffers(1,&d->poseBuffer);
    if(!d->poseTexture)glGenTextures(1,&d->poseTexture);
    glBindBuffer(GL_TEXTURE_BUFFER,d->poseBuffer);
    size_t segmentBytes=6*count*sizeof(rows[0]),nodeBytes=d->poseDomain.nodeCount*sizeof(d->poseDomain.nodes[0]);
    glBufferData(GL_TEXTURE_BUFFER,segmentBytes+nodeBytes,NULL,GL_STREAM_DRAW);
    glBufferSubData(GL_TEXTURE_BUFFER,0,segmentBytes,rows);
    glBufferSubData(GL_TEXTURE_BUFFER,segmentBytes,nodeBytes,d->poseDomain.nodes);
    glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_BUFFER,d->poseTexture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,d->poseBuffer);
    glActiveTexture(GL_TEXTURE0);glBindBuffer(GL_TEXTURE_BUFFER,0);
    d->poseSegmentCount=count;return true;
}
static void UploadSurfacePose(OpenGLRendererData* d,GLuint program) {
    glUniform1i(glGetUniformLocation(program,"surfacePoseCount"),(GLint)d->poseSegmentCount);
    glUniform1i(glGetUniformLocation(program,"surfacePoseData"),5);
    glUniform4f(glGetUniformLocation(program,"surfaceCranialPlane"),d->cranialAxis.x,d->cranialAxis.y,
        d->cranialAxis.z,Vec3_Dot(d->cranialAttachment,d->cranialAxis));
    glUniform1f(glGetUniformLocation(program,"surfaceCranialCollar"),d->cranialCollar);
    glUniform1i(glGetUniformLocation(program,"surfaceJawActive"),d->surfaceJawActive);
    glUniform4f(glGetUniformLocation(program,"surfaceJawRotation"),d->surfaceJawRotation.x,d->surfaceJawRotation.y,
        d->surfaceJawRotation.z,d->surfaceJawRotation.w);
    glUniform4f(glGetUniformLocation(program,"surfaceHeadRotation"),d->surfaceHeadRotation.x,d->surfaceHeadRotation.y,
        d->surfaceHeadRotation.z,d->surfaceHeadRotation.w);
    glUniform3f(glGetUniformLocation(program,"surfaceHeadShift"),d->surfaceHeadShift.x,d->surfaceHeadShift.y,d->surfaceHeadShift.z);
    glUniform3f(glGetUniformLocation(program,"surfaceJawPivot"),d->surfaceJawPivot.x,d->surfaceJawPivot.y,d->surfaceJawPivot.z);
    glUniform3f(glGetUniformLocation(program,"surfaceJawTarget"),d->surfaceJawTarget.x,d->surfaceJawTarget.y,d->surfaceJawTarget.z);
    glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_BUFFER,d->poseTexture);glActiveTexture(GL_TEXTURE0);
}
bool OpenGLRenderer_SetMorphSurfaceDomain(Renderer3D* renderer,const Monster* current,const Monster* reference) {
    if(!renderer||!renderer->user_data)return false;
    OpenGLRendererData* d=renderer->user_data;
    if(!current||!reference) {d->materialDomain.count=0;return true;}
    if(!SurfaceMapper_BuildMorphDomain(&d->materialDomain,&current->anatomyGraph,
        &reference->anatomyGraph,&reference->surfaceMapping))return false;
    if(!d->materialBuffer)glGenBuffers(1,&d->materialBuffer);
    if(!d->materialTexture)glGenTextures(1,&d->materialTexture);
    glBindBuffer(GL_TEXTURE_BUFFER,d->materialBuffer);
    size_t segmentBytes=d->materialDomain.count*sizeof(d->materialDomain.data[0]);
    size_t nodeBytes=d->materialDomain.nodeCount*sizeof(d->materialDomain.nodes[0]);
    glBufferData(GL_TEXTURE_BUFFER,segmentBytes+nodeBytes,NULL,GL_STREAM_DRAW);
    glBufferSubData(GL_TEXTURE_BUFFER,0,segmentBytes,d->materialDomain.data);
    glBufferSubData(GL_TEXTURE_BUFFER,segmentBytes,nodeBytes,d->materialDomain.nodes);
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_BUFFER,d->materialTexture);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,d->materialBuffer);
    glActiveTexture(GL_TEXTURE0);glBindBuffer(GL_TEXTURE_BUFFER,0);
    return true;
}

static void UploadMorphMaterial(OpenGLRendererData* d,GLuint program) {
    glUniform1i(glGetUniformLocation(program,"morphMaterialCount"),(GLint)d->materialDomain.count);
    glUniform1i(glGetUniformLocation(program,"morphMaterialData"),4);
    GLfloat mv[16],rotation[9];glGetFloatv(GL_MODELVIEW_MATRIX,mv);
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)rotation[c*3+r]=mv[c*4+r];
    glUniformMatrix3fv(glGetUniformLocation(program,"morphWorldToView"),1,GL_FALSE,rotation);
    glUniform1i(glGetUniformLocation(program,"sdfSurfaceDebug"),d->surfaceDebug);
}

static void UploadHandoffs(OpenGLRendererData* d,GLuint program) {
    float origins[16][4],normals[16][4],meta[16][4],workMin[16][3],workMax[16][3];
    GLint retire[16];
    float segments[2*MORPH_HANDOFF_GPU_SEGMENTS][4];
    UploadMorphMaterial(d,program);
    size_t cursor=0;
    for(size_t i=0;i<d->handoffs.count;++i) {
        const MorphHandoffInterface* h=&d->handoffs.interfaces[i];
        retire[i]=h->retireOnly;
        AABB3D bounds=MorphHandoff_WorkBounds(h);
        workMin[i][0]=bounds.start.x;workMin[i][1]=bounds.start.y;workMin[i][2]=bounds.start.z;
        workMax[i][0]=bounds.end.x;workMax[i][1]=bounds.end.y;workMax[i][2]=bounds.end.z;
        origins[i][0]=h->origin.x;origins[i][1]=h->origin.y;origins[i][2]=h->origin.z;origins[i][3]=h->radius;
        normals[i][0]=h->normal.x;normals[i][1]=h->normal.y;normals[i][2]=h->normal.z;normals[i][3]=h->hardClipOffset;
        meta[i][0]=h->blendWidth;meta[i][1]=h->active?h->transitionFactor:0;
        meta[i][2]=(float)cursor;meta[i][3]=(float)h->segmentCount;
        for(size_t j=0;j<h->segmentCount;++j,++cursor) {
            const MorphHandoffSegment* s=&h->segments[j];
            float* a=segments[2*cursor],*b=segments[2*cursor+1];
            a[0]=s->a.x;a[1]=s->a.y;a[2]=s->a.z;a[3]=s->radius;
            b[0]=s->b.x;b[1]=s->b.y;b[2]=s->b.z;b[3]=0;
        }
    }
    glUniform1i(glGetUniformLocation(program,"handoffCount"),(GLint)d->handoffs.count);
    glUniform1i(glGetUniformLocation(program,"handoffPass"),d->handoffPass);
    glUniform1i(glGetUniformLocation(program,"handoffDebug"),d->surfaceDebug);
    if(d->handoffs.count) {
        glUniform1iv(glGetUniformLocation(program,"handoffRetire"),(GLsizei)d->handoffs.count,retire);
        glUniform3fv(glGetUniformLocation(program,"handoffWorkMin"),(GLsizei)d->handoffs.count,&workMin[0][0]);
        glUniform3fv(glGetUniformLocation(program,"handoffWorkMax"),(GLsizei)d->handoffs.count,&workMax[0][0]);
        glUniform4fv(glGetUniformLocation(program,"handoffOrigin"),(GLsizei)d->handoffs.count,&origins[0][0]);
        glUniform4fv(glGetUniformLocation(program,"handoffNormal"),(GLsizei)d->handoffs.count,&normals[0][0]);
        glUniform4fv(glGetUniformLocation(program,"handoffMeta"),(GLsizei)d->handoffs.count,&meta[0][0]);
    }
    if(cursor) {
        if(!d->handoffBuffer)glGenBuffers(1,&d->handoffBuffer);
        if(!d->handoffTexture)glGenTextures(1,&d->handoffTexture);
        glBindBuffer(GL_TEXTURE_BUFFER,d->handoffBuffer);
        glBufferData(GL_TEXTURE_BUFFER,2*cursor*sizeof(*segments),segments,GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_BUFFER,d->handoffTexture);
        glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,d->handoffBuffer);
        glUniform1i(glGetUniformLocation(program,"handoffSegments"),3);
        glActiveTexture(GL_TEXTURE0);glBindBuffer(GL_TEXTURE_BUFFER,0);
    }
}

void OpenGLRenderer_SetSDFReference(Renderer3D* renderer,bool enabled,float maxStep,unsigned steps) {
    if(!renderer||!renderer->user_data)return;
    OpenGLRendererData* d=renderer->user_data;
    d->reference.enabled=enabled;
    d->reference.maxStep=isfinite(maxStep)&&maxStep>0?maxStep:0;
    d->reference.steps=steps>=256&&steps<=65536?steps:512;
}

bool OpenGLRenderer_SaveDepth(const char* path,int width,int height,const ICamera* camera) {
    if(!path||!camera||width<=0||height<=0)return false;
    size_t count=(size_t)width*height;float* depth=malloc(count*sizeof(float));
    if(!depth)return false;
    GLint previousRead=BeginDemoReadback();
    glReadPixels(0,0,width,height,GL_DEPTH_COMPONENT,GL_FLOAT,depth);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previousRead);
    if(!OpenGLRenderer_CheckErrors()){free(depth);return false;}
    for(size_t i=0;i<count;++i) {
        float z=depth[i]*2-1,n=camera->nearPlane,f=camera->farPlane;
        depth[i]=depth[i]>=1?INFINITY:2*n*f/(f+n-z*(f-n));
    }
    FILE* out=fopen(path,"wb");bool ok=false;
    if(out) {fprintf(out,"Pf\n%d %d\n-1.0\n",width,height);
        ok=fwrite(depth,sizeof(float),count,out)==count;fclose(out);}
    free(depth);return ok;
}

bool OpenGLRenderer_SavePPM(const char* path, int width, int height) {
    if (!path || width <= 0 || height <= 0) return false;
    size_t rowBytes=(size_t)width*3u,total=rowBytes*(size_t)height;
    unsigned char* pixels=(unsigned char*)malloc(total);
    if(!pixels)return false;
    GLint previousPack=4;glGetIntegerv(GL_PACK_ALIGNMENT,&previousPack);
    GLint previousRead=BeginDemoReadback();
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glFinish();
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previousRead);
    if(!OpenGLRenderer_CheckErrors()){free(pixels);return false;}
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
    if(demoFramebuffer)glBindFramebuffer(GL_FRAMEBUFFER,demoFramebuffer);
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
            d->meshStats.gpuRenderMs=(float)((double)ns/1e6);
            d->meshStats.gpuTimingGeneration=(uint64_t)d->meshQuerySerial[oldest]+1;
            d->meshQueryPending[oldest]=false;
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
    if(data&&data->handoffs.count) {
        /* Nunca mostrar la representación sin propiedad si falla el shader. */
        if(!mesh||!mesh->hasSurface||!RenderSurfaceMesh(data,mesh)) {
            if(mesh&&data->skin.mesh==mesh)data->skin.failed=true;
            fprintf(stderr,"[Cesión] No se pudo dibujar la malla con propiedad compartida.\n");
        }
    } else if(data&&mesh&&data->skin.mesh==mesh) {
        if(!RenderSurfaceMesh(data,mesh)){data->skin.failed=true;fprintf(stderr,"[ERROR] Skinning GPU incompleto\n");}
    } else if(!data || !mesh || (!mesh->hasSurface && data->surfaceDebug!=10) || !RenderSurfaceMesh(data,mesh))OpenGLRenderer_RenderMesh(mesh);

    if (wireframe) {
        glPolygonMode(GL_FRONT, previousPolygonMode[0]);
        glPolygonMode(GL_BACK, previousPolygonMode[1]);
    }
}

void OpenGLRenderer_Destroy(Renderer3D* renderer) {
    if (!renderer) return;

    if (renderer->user_data) {
        OpenGLRendererData* data=renderer->user_data;
        GLuint programs[]={data->compute.sampleProgram,data->compute.classifyProgram,
            data->compute.emitProgram,data->compute.finalizeProgram,data->compute.resetProgram,data->compute.prepareProgram,data->compute.validateProgram,data->compute.intervalProgram,data->compute.renderProgram};
        for(size_t i=0;i<sizeof(programs)/sizeof(*programs);++i)if(programs[i])glDeleteProgram(programs[i]);
        GLuint buffers[]={data->compute.samples,data->compute.cases,data->compute.tables,
            data->compute.command,data->compute.vertices,data->compute.validation,data->compute.intervalValidation};
        glDeleteBuffers(7,buffers);
        free(data->compute.cachedScene);
        GLuint sparsePrograms[]={data->sparse.releaseProgram,data->sparse.sampleProgram,data->sparse.classifyProgram,
            data->sparse.allocateProgram,data->sparse.persistProgram,data->sparse.emitProgram,data->sparse.prepareProgram,data->sparse.rootProgram,data->sparse.statisticsProgram};
        for(size_t i=0;i<sizeof(sparsePrograms)/sizeof(*sparsePrograms);++i)if(sparsePrograms[i])glDeleteProgram(sparsePrograms[i]);
        GLuint sparseBuffers[]={data->sparse.bank1,data->sparse.scratch,data->sparse.bricks,data->sparse.jobs,
            data->sparse.draws,data->sparse.allocator,data->sparse.links,data->sparse.indices,data->sparse.edges,data->sparse.rootQueue};glDeleteBuffers(10,sparseBuffers);
        glDeleteBuffers(4,data->sparse.metrics);
        glDeleteBuffers(2,data->sparse.completion);
        if(data->compute.vao)glDeleteVertexArrays(1,&data->compute.vao);
        for(unsigned i=0;i<2;++i)if(data->compute.frameFences[i])glDeleteSync(data->compute.frameFences[i]);
        for(int i=0;i<4;++i) {
            if(data->compute.queryCapacity[i])glDeleteQueries((GLsizei)data->compute.queryCapacity[i],data->compute.timestamps[i]);
            free(data->compute.timestamps[i]);
        }
        if(data->eyeProgram)glDeleteProgram(data->eyeProgram);
        for(unsigned i=0;i<EYE_TEXTURE_CACHE_CAPACITY;++i)if(data->eyeTextures[i].texture)glDeleteTextures(1,&data->eyeTextures[i].texture);
        if(data->materialBuffer)glDeleteBuffers(1,&data->materialBuffer);
        if(data->materialTexture)glDeleteTextures(1,&data->materialTexture);
        if(data->poseBuffer)glDeleteBuffers(1,&data->poseBuffer);
        if(data->poseTexture)glDeleteTextures(1,&data->poseTexture);
        if(data->sparse.cachedSampleProgram)glDeleteProgram(data->sparse.cachedSampleProgram);
        if(data->sparse.cachedRootProgram)glDeleteProgram(data->sparse.cachedRootProgram);
        if(data->sparse.sampleClaimProgram)glDeleteProgram(data->sparse.sampleClaimProgram);
        if(data->sparse.materialClaimProgram)glDeleteProgram(data->sparse.materialClaimProgram);
        if(data->sparse.materialProgram)glDeleteProgram(data->sparse.materialProgram);
        if(data->sparse.cachedMaterialProgram)glDeleteProgram(data->sparse.cachedMaterialProgram);
        free(data->sparse.uploadJobs);free(data->sparse.pointMapCPU);
        if(data->sparse.pointMap)glDeleteBuffers(1,&data->sparse.pointMap);
        if(data->handoffBuffer)glDeleteBuffers(1,&data->handoffBuffer);
        if(data->handoffTexture)glDeleteTextures(1,&data->handoffTexture);
        if(data->guardProgram)glDeleteProgram(data->guardProgram);
        if(data->surfaceProgram)glDeleteProgram(data->surfaceProgram);
        if(data->skin.program)glDeleteProgram(data->skin.program);
        if(data->skin.paletteBuffer)glDeleteBuffers(1,&data->skin.paletteBuffer);
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
            if(m->skinBase)glDeleteBuffers(1,&m->skinBase);
            if(m->skinWeights)glDeleteBuffers(1,&m->skinWeights);
        }
        if(data->sdfProgram)glDeleteProgram(data->sdfProgram);
        if(data->sdfCachedProgram)glDeleteProgram(data->sdfCachedProgram);
        glDeleteBuffers(1,&data->sdfMouthCache);
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

void OpenGLRenderer_SetupCameraOrtho(ICamera* camera, float orthoHeight, int width, int height) {
    if (height <= 0) height = 1;

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    float aspect = (float)width / (float)height;
    float h = (orthoHeight > 0.0f) ? orthoHeight : 16.0f;
    float w = h * aspect;
    float nearP = camera ? camera->nearPlane : 0.1f;
    float farP = camera ? camera->farPlane : 200.0f;
    glOrtho(-w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, nearP, farP);

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

void OpenGLRenderer_SetupCamera(ICamera* camera, int width, int height) {
    if (camera && camera->fov <= 0.0f) {
        float h = (camera->fov < 0.0f) ? -camera->fov : 16.0f;
        OpenGLRenderer_SetupCameraOrtho(camera, h, width, height);
        return;
    }

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
static GLuint LinkSDFProgram(const char* const* source,size_t sourceCount) {
    const char* vertex="#version 330 compatibility\nvoid main(){gl_Position=gl_Vertex;}\n";
    GLuint vs=CompileShader(GL_VERTEX_SHADER,&vertex,1);
    GLuint fs=CompileShader(GL_FRAGMENT_SHADER,source,sourceCount);
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return 0;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"[SDF GPU] %s\n",log);glDeleteProgram(p);return 0;}
    return p;
}
static SDFUniformLocations SDFLocations(GLuint p) {
    SDFUniformLocations u;
    /* Las ubicaciones pertenecen al programa enlazado, no al fotograma. */
    u.validationMode=glGetUniformLocation(p,"validationMode");
    u.validationPointBase=glGetUniformLocation(p,"validationPointBase");
    u.sceneData=glGetUniformLocation(p,"sceneData");
    u.partBase=glGetUniformLocation(p,"partBase");
    u.partCount=glGetUniformLocation(p,"partCount");
    u.connectorBase=glGetUniformLocation(p,"connectorBase");
    u.connectorCount=glGetUniformLocation(p,"connectorCount");
    u.mouthBase=glGetUniformLocation(p,"mouthBase");
    u.mouthCount=glGetUniformLocation(p,"mouthCount");
    u.axialBase=glGetUniformLocation(p,"axialBase");
    u.axialCount=glGetUniformLocation(p,"axialCount");
    u.bodySmoothness=glGetUniformLocation(p,"bodySmoothness");
    u.tileDataBase=glGetUniformLocation(p,"tileDataBase");
    u.tileColumns=glGetUniformLocation(p,"tileColumns");
    u.cameraPosition=glGetUniformLocation(p,"cameraPosition");
    u.cameraForward=glGetUniformLocation(p,"cameraForward");
    u.cameraRight=glGetUniformLocation(p,"cameraRight");
    u.cameraUp=glGetUniformLocation(p,"cameraUp");
    u.boundsMin=glGetUniformLocation(p,"boundsMin");
    u.boundsMax=glGetUniformLocation(p,"boundsMax");
    u.viewportSize=glGetUniformLocation(p,"viewportSize");
    u.tanHalfFov=glGetUniformLocation(p,"tanHalfFov");
    u.aspectRatio=glGetUniformLocation(p,"aspectRatio");
    u.hitTolerance=glGetUniformLocation(p,"hitTolerance");
    u.viewProjection=glGetUniformLocation(p,"viewProjection");
    u.surfaceRecipe=glGetUniformLocation(p,"surfaceRecipe");
    u.pigmentSeed=glGetUniformLocation(p,"pigmentSeed");
    u.scaleSeed=glGetUniformLocation(p,"scaleSeed");
    u.furSeed=glGetUniformLocation(p,"furSeed");
    u.sdfHasSurface=glGetUniformLocation(p,"sdfHasSurface");
    u.sdfFurEnabled=glGetUniformLocation(p,"sdfFurEnabled");
    return u;
}
static bool InitSDFProgram(OpenGLRendererData* d) {
    if(d->sdfProgram)return true;
    d->sdfProgram=LinkSDFProgram(sdfFragmentSource,sizeof(sdfFragmentSource)/sizeof(*sdfFragmentSource));
    if(!d->sdfProgram)return false;
    d->uniforms=SDFLocations(d->sdfProgram);
    if(!d->sdfBuffer)glGenBuffers(1,&d->sdfBuffer);
    if(!d->sdfTexture)glGenTextures(1,&d->sdfTexture);
    fprintf(stdout,"[SDF GPU] %s | %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    return true;
}
static bool InitSDFCachedProgram(OpenGLRendererData* d) {
    if(d->sdfCachedProgram)return true;
    d->sdfCachedProgram=LinkSDFProgram(sdfCachedFragmentSource,sizeof(sdfCachedFragmentSource)/sizeof(*sdfCachedFragmentSource));
    if(!d->sdfCachedProgram)return false;
    d->cachedUniforms=SDFLocations(d->sdfCachedProgram);
    GLuint block=glGetUniformBlockIndex(d->sdfCachedProgram,"SDFMouthCache");
    if(block!=GL_INVALID_INDEX)glUniformBlockBinding(d->sdfCachedProgram,block,0);
    return true;
}
static void UniformVector(GLint location,Vector3 v){glUniform3f(location,v.x,v.y,v.z);}

static bool UploadSDFScene(OpenGLRendererData* d,const MonsterSDF* sdf,
    const ICamera* camera,int width,int height) {
    if(!d->sdfBuffer)glGenBuffers(1,&d->sdfBuffer);
    if(!d->sdfTexture)glGenTextures(1,&d->sdfTexture);
    d->sweepSorted=true;
    for(int i=1;i<sdf->axialStationCount;++i)
        if(sdf->axialStations[i].center.z>sdf->axialStations[i-1].center.z)d->sweepSorted=false;
    for(size_t m=0;m<sdf->mouthCount;++m)if(sdf->mouths[m].sweptSkull)
        for(int i=1;i<6;++i)if(sdf->mouths[m].headStations[i].center.z>sdf->mouths[m].headStations[i-1].center.z)d->sweepSorted=false;
    Vector3 forward=Vec3_Zero(),right=Vec3_Zero(),up=Vec3_Zero();
    float tangent=1,aspect=1;
    if(camera) {
        forward=Vec3_Normalize(Vec3_Sub(camera->target,camera->position));
        right=Vec3_Normalize(Vec3_Cross(forward,camera->up));up=Vec3_Cross(right,forward);
        tangent=tanf(camera->fov*.00872664626f);aspect=(float)width/height;
    }
    int columns=camera?(width+15)/16:0,rows=camera?(height+15)/16:0;
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
        if(!camera)break;
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
    // El empaquetado escalar deja componentes libres: inicializarlas también
    // permite comparar escenas sin depender de bytes indeterminados.
    memset(d->sdfData,0,count*sizeof(*d->sdfData));
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
    if(sdf->mouthCount==1&&M_STRIDE<=1024) {
        if(!d->sdfMouthCache)glGenBuffers(1,&d->sdfMouthCache);
        glBindBuffer(GL_UNIFORM_BUFFER,d->sdfMouthCache);
        glBufferData(GL_UNIFORM_BUFFER,1024*sizeof(*d->sdfData),NULL,GL_STREAM_DRAW);
        glBufferSubData(GL_UNIFORM_BUFFER,0,M_STRIDE*sizeof(*d->sdfData),d->sdfData+mouthBase);
        glBindBufferBase(GL_UNIFORM_BUFFER,0,d->sdfMouthCache);glBindBuffer(GL_UNIFORM_BUFFER,0);
    }
    glBindBuffer(GL_TEXTURE_BUFFER,d->sdfBuffer);glBufferData(GL_TEXTURE_BUFFER,count*sizeof(*d->sdfData),d->sdfData,GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_BUFFER,d->sdfTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,d->sdfBuffer);
    d->scene.partBase=(int)partBase;d->scene.connectorBase=(int)connectorBase;
    d->scene.mouthBase=(int)mouthBase;d->scene.axialBase=(int)axialBase;
    d->scene.tileDataBase=(int)tileDataBase;d->scene.columns=columns;
    return true;
}

#include "MonsterSDFCompute.generated.h"
#include "SDFInterval.h"

bool OpenGLRenderer_SupportsComputeSurface(Renderer3D* r) {
    if(!r||!r->user_data)return false;
    GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    return major>4||(major==4&&minor>=3);
}
static GLuint LinkComputeSource(const char* const* source,size_t count) {
    GLuint shader=CompileShader(GL_COMPUTE_SHADER,source,count);if(!shader)return 0;
    GLuint p=glCreateProgram();glAttachShader(p,shader);glLinkProgram(p);glDeleteShader(shader);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);
        fprintf(stderr,"[SDF compute] %s\n",log);glDeleteProgram(p);return 0;}
    return p;
}
static bool InitComputeSurface(OpenGLRendererData* d) {
    if(d->compute.renderProgram)return true;
#define LINK(member,source) do {d->compute.member=LinkComputeSource(source,sizeof(source)/sizeof(*source));if(!d->compute.member)return false;} while(0)
    LINK(sampleProgram,sdfSampleSource);LINK(classifyProgram,sdfClassifySource);
    LINK(emitProgram,sdfEmitSource);LINK(finalizeProgram,sdfFinalizeSource);
    LINK(resetProgram,sdfResetSource);LINK(prepareProgram,sdfPrepareSource);LINK(validateProgram,sdfValidateSource);
#undef LINK
    GLuint vs=CompileShader(GL_VERTEX_SHADER,sdfMeshVertexSource,sizeof(sdfMeshVertexSource)/sizeof(*sdfMeshVertexSource));
    GLuint fs=CompileShader(GL_FRAGMENT_SHADER,sdfMeshFragmentSource,sizeof(sdfMeshFragmentSource)/sizeof(*sdfMeshFragmentSource));
    if(!vs||!fs) {if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);
    const char* captured[]={"surfacePosedPosition","surfacePosedNormal"};
    glTransformFeedbackVaryings(p,2,captured,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);
        fprintf(stderr,"[SDF mesh GLSL] %s\n",log);glDeleteProgram(p);return false;}
    d->compute.renderProgram=p;
    glGenBuffers(1,&d->compute.tables);glGenBuffers(1,&d->compute.command);
    glGenVertexArrays(1,&d->compute.vao);
    int tables[4096+24+24];memcpy(tables,MARCHING_CUBES_TRI_TABLE,4096*sizeof(int));
    memcpy(tables+4096,MARCHING_CUBES_EDGE_ENDPOINTS,24*sizeof(int));
    memcpy(tables+4096+24,MARCHING_CUBES_CORNER_OFFSETS,24*sizeof(int));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.tables);
    glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(tables),tables,GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.command);
    glBufferData(GL_SHADER_STORAGE_BUFFER,9*sizeof(GLuint),NULL,GL_DYNAMIC_DRAW);
    fprintf(stdout,"[SDF compute] %s | %s; extracción experimental sin readback\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    return true;
}
static void BindSceneProgram(OpenGLRendererData* d,GLuint p,const MonsterSDF* sdf) {
    glUseProgram(p);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_BUFFER,d->sdfTexture);
#define INT(name,value) glUniform1i(glGetUniformLocation(p,#name),(GLint)(value))
    INT(sceneData,0);INT(partBase,d->scene.partBase);INT(partCount,sdf->bodyPartCount);
    INT(connectorBase,d->scene.connectorBase);INT(connectorCount,sdf->connectorCount);
    INT(mouthBase,d->scene.mouthBase);INT(mouthCount,sdf->mouthCount);
    INT(axialBase,d->scene.axialBase);INT(axialCount,sdf->axialStationCount);
    glUniform1f(glGetUniformLocation(p,"bodySmoothness"),sdf->config.bodySmoothness);
    INT(sdfHasSurface,sdf->hasSurface);INT(sdfFurEnabled,!d->disableShells||!d->disableGuards);
    INT(sweepBinarySearch,d->sweepSorted&&getenv("MONSTER_SDF_LINEAR_SWEEP")==NULL);
    INT(disableGroupPruning,getenv("MONSTER_SDF_NO_GROUP_PRUNING")!=NULL);
    INT(exactMeshMaterial,getenv("MONSTER_GPU_EXACT_MATERIAL")!=NULL);
#undef INT
    if(sdf->hasSurface) {
        glUniform4fv(glGetUniformLocation(p,"surfaceRecipe"),SURFACE_RECIPE_ROWS,&sdf->surfaceRecipe.data[0][0]);
        glUniform1ui(glGetUniformLocation(p,"pigmentSeed"),sdf->surfaceRecipe.pigmentSeed);
        glUniform1ui(glGetUniformLocation(p,"scaleSeed"),sdf->surfaceRecipe.scaleSeed);
        glUniform1ui(glGetUniformLocation(p,"furSeed"),sdf->surfaceRecipe.furSeed);
    }
}
static bool ComputeBuffer(GLuint* buffer,size_t* capacity,size_t bytes) {
    GLint64 limit=0;glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&limit);
    if(bytes>(size_t)limit) {fprintf(stderr,"[SDF compute] SSBO excede límite: %zu > %lld\n",bytes,(long long)limit);return false;}
    if(!*buffer)glGenBuffers(1,buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,*buffer);
    if(bytes>*capacity) {glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,NULL,GL_DYNAMIC_DRAW);*capacity=bytes;}
    return true;
}
static void BindGridProgram(OpenGLRendererData* d,GLuint p,const MonsterSDF* sdf,const GpuSDFSurfaceGrid* g) {
    BindSceneProgram(d,p,sdf);
    glUniform3ui(glGetUniformLocation(p,"gridCells"),g->cells[0],g->cells[1],g->cells[2]);
    UniformVector(glGetUniformLocation(p,"gridOrigin"),g->origin);
    glUniform1f(glGetUniformLocation(p,"gridStep"),g->voxelSize);
    glUniform1i(glGetUniformLocation(p,"refinementSteps"),(GLint)g->refinementSteps);
    glUniform1ui(glGetUniformLocation(p,"vertexCapacity"),(GLuint)(d->compute.vertexCapacity/(12*sizeof(float))));
}
static bool ReserveComputeTimers(OpenGLRendererData* d,unsigned slot,unsigned count) {
    if(d->compute.queryCapacity[slot]>=count)return true;
    unsigned previous=d->compute.queryCapacity[slot];
    GLuint* ids=realloc(d->compute.timestamps[slot],count*sizeof(*ids));
    if(!ids)return false;
    d->compute.timestamps[slot]=ids;
    glGenQueries((GLsizei)(count-previous),ids+previous);
    d->compute.queryCapacity[slot]=count;
    return true;
}
static void BindSlabProgram(OpenGLRendererData* d,GLuint p,const MonsterSDF* sdf,
    const GpuSDFSurfaceGrid* g,GLuint z,GLuint depth) {
    BindGridProgram(d,p,sdf,g);
    glUniform1ui(glGetUniformLocation(p,"sampleCornerBase"),z*(g->cells[0]+1)*(g->cells[1]+1));
    glUniform1ui(glGetUniformLocation(p,"slabCellBase"),z*g->cells[0]*g->cells[1]);
    glUniform1ui(glGetUniformLocation(p,"slabCellCount"),depth*g->cells[0]*g->cells[1]);
    glUniform1ui(glGetUniformLocation(p,"sampleEndZ"),z+depth);
}
static void CollectComputeTimers(OpenGLRendererData* d) {
    for(unsigned slot=0;slot<4;++slot)if(d->compute.pending[slot]) {
        GLuint* q=d->compute.timestamps[slot];unsigned slabs=d->compute.timingSlabs[slot];
        unsigned last=6*slabs+1;GLint ready=0;
        glGetQueryObjectiv(q[last],GL_QUERY_RESULT_AVAILABLE,&ready);
        if(!ready)continue;
        if(d->compute.timingGeneration[slot]>d->compute.stats.timingGeneration) {
            double stages[3]={0};GLuint64 first=0,end=0;
            for(unsigned slab=0;slab<slabs;++slab)for(unsigned stage=0;stage<3;++stage) {
                GLuint64 a,b;unsigned index=6*slab+2*stage;
                glGetQueryObjectui64v(q[index],GL_QUERY_RESULT,&a);
                glGetQueryObjectui64v(q[index+1],GL_QUERY_RESULT,&b);
                if(!index)first=a;
                stages[stage]+=(b-a)*1e-6;
            }
            GLuint64 renderBegin;
            glGetQueryObjectui64v(q[last-1],GL_QUERY_RESULT,&renderBegin);
            glGetQueryObjectui64v(q[last],GL_QUERY_RESULT,&end);
            if(d->sparse.initialized) {
                GLuint counters[10];glBindBuffer(GL_COPY_READ_BUFFER,d->sparse.metrics[slot]);
                glGetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(counters),counters);
                d->compute.stats.activeBricks=counters[2];d->compute.stats.residentSampleBricks=counters[3];
                d->compute.stats.updatedEdges=counters[4];d->compute.stats.publishedIndices=counters[5];
                d->compute.stats.reusedSamples=counters[6];d->compute.stats.uniqueEdges=counters[7];d->compute.stats.exactNormals=counters[8];d->compute.stats.samples=counters[9];
            }
            d->compute.stats.timingGeneration=d->compute.timingGeneration[slot];
            d->compute.stats.samplingGPUms=stages[0];
            d->compute.stats.classificationGPUms=stages[1];
            d->compute.stats.emissionGPUms=stages[2];
            d->compute.stats.renderGPUms=(end-renderBegin)*1e-6;
            d->compute.stats.totalGPUms=(end-first)*1e-6;
        }
        d->compute.pending[slot]=false;
    }
}
bool OpenGLRenderer_RenderGpuSDFSurface(Renderer3D* r,const MonsterSDF* sdf,
    const GpuSDFSurfaceGrid* grid,const ICamera* camera) {
    if(!OpenGLRenderer_SupportsComputeSurface(r)||!sdf||!grid||!camera||
       !GpuSDFSurfaceGrid_Contains(grid,sdf->bounds))return false;
    OpenGLRendererData* d=r->user_data;
    d->compute.extractionVoxel=grid->voxelSize;
    GLint maxGroups[3];
    for(GLuint axis=0;axis<3;++axis)glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,axis,&maxGroups[axis]);
    if((grid->cells[0]+8)/8>(GLuint)maxGroups[0]||
       (grid->cells[1]+4)/4>(GLuint)maxGroups[1]) {
        fprintf(stderr,"[GPU SUPERFICIE] Despacho de muestras excede límites de la GPU\n");return false;
    }
    if(!InitComputeSurface(d))return false;
    unsigned fenceSlot=d->compute.frame%2;
    if(d->compute.frameFences[fenceSlot]) {
        // Acotar frames pendientes sin descargar geometría ni falsear los FPS.
        GLenum wait;glFlush();
        do {
            wait=glClientWaitSync(d->compute.frameFences[fenceSlot],0,0);
            if(wait==GL_TIMEOUT_EXPIRED)SDL_Delay(1);
        } while(wait==GL_TIMEOUT_EXPIRED);
        if(wait==GL_WAIT_FAILED)return false;
        glDeleteSync(d->compute.frameFences[fenceSlot]);d->compute.frameFences[fenceSlot]=NULL;
    }
    double begin=RendererNowMs();
    if(!UploadSDFScene(d,sdf,NULL,0,0))return false;
    bool reuse=d->compute.denseValid&&d->compute.cachedSceneCount==d->sdfCount&&
        d->compute.cachedParts==sdf->bodyPartCount&&d->compute.cachedConnectors==sdf->connectorCount&&
        d->compute.cachedMouths==sdf->mouthCount&&d->compute.cachedAxial==sdf->axialStationCount&&
        d->compute.cachedSmoothness==sdf->config.bodySmoothness&&
        memcmp(&d->compute.cachedGrid,grid,sizeof(*grid))==0&&
        memcmp(d->compute.cachedScene,d->sdfData,d->sdfCount*sizeof(*d->sdfData))==0;
    if(!reuse)d->compute.denseValid=false;
    d->compute.stats.uploadCPUms=RendererNowMs()-begin;
    GLint64 limit;glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&limit);
    size_t scratchLimit=(size_t)limit/sizeof(float);
    if(scratchLimit>32000000u)scratchLimit=32000000u;
    GpuSDFSurfacePlan plan;
    if(!GpuSDFSurfaceGrid_PlanDense(grid,scratchLimit,(size_t)limit/(2*sizeof(GLuint)),128,&plan)) {
        fprintf(stderr,"[GPU SUPERFICIE] No cabe una franja densa completa en los buffers GPU\n");return false;
    }
    size_t maxVertices=grid->cellCount*15;
    // Presupuesto explícito de salida: el desbordamiento produce un cuadro rojo.
    size_t outputLimit=(size_t)limit<64u*1024u*1024u?(size_t)limit:64u*1024u*1024u;
    if(maxVertices>outputLimit/(12*sizeof(float)))maxVertices=outputLimit/(12*sizeof(float));
    if(!ComputeBuffer(&d->compute.samples,&d->compute.sampleCapacity,plan.maxSamples*sizeof(float))||
       !ComputeBuffer(&d->compute.cases,&d->compute.cellCapacity,plan.maxCells*2*sizeof(GLuint))||
       !ComputeBuffer(&d->compute.vertices,&d->compute.vertexCapacity,maxVertices*12*sizeof(float)))return false;
    GLuint buffers[]={d->compute.samples,d->compute.cases,d->compute.tables,d->compute.command,d->compute.vertices};
    for(int i=0;i<5;++i)glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i+1,buffers[i]);
    unsigned timingSlabs=reuse?1:plan.slabCount;
    if(!reuse) {
    glUseProgram(d->compute.resetProgram);
    glUniform1i(glGetUniformLocation(d->compute.resetProgram,"resetSurface"),1);glDispatchCompute(1,1,1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    CollectComputeTimers(d);unsigned slot=d->compute.frame%4;
    bool timing=!d->compute.pending[slot]&&ReserveComputeTimers(d,slot,6*timingSlabs+2);
    GLuint* q=d->compute.timestamps[slot];
#define STAMP(i) do {if(timing)glQueryCounter(q[i],GL_TIMESTAMP);} while(0)
    // Todas las celdas se extraen una vez; solo el plano limítrofe se remuestrea.
    for(GLuint slab=0,z=0;!reuse&&z<grid->cells[2];++slab,z+=plan.slabDepth) {
        GLuint depth=grid->cells[2]-z;if(depth>plan.slabDepth)depth=plan.slabDepth;
        glUseProgram(d->compute.resetProgram);
        glUniform1i(glGetUniformLocation(d->compute.resetProgram,"resetSurface"),0);glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        BindSlabProgram(d,d->compute.sampleProgram,sdf,grid,z,depth);STAMP(6*slab);
        for(GLuint plane=z;plane<=z+depth;plane+=16) {
            GLuint planes=z+depth+1-plane;if(planes>16)planes=16;
            glUniform1ui(glGetUniformLocation(d->compute.sampleProgram,"sampleZOffset"),plane);
            glDispatchCompute((grid->cells[0]+8)/8,(grid->cells[1]+4)/4,(planes+3)/4);
            glFlush();
        }
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);STAMP(6*slab+1);
        BindSlabProgram(d,d->compute.classifyProgram,sdf,grid,z,depth);STAMP(6*slab+2);
        GLuint count=depth*grid->cells[0]*grid->cells[1];
        GLuint groups=(count+127)/128;
        glDispatchCompute(groups>65535?65535:groups,(groups+65534)/65535,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glUseProgram(d->compute.prepareProgram);glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);STAMP(6*slab+3);
        BindSlabProgram(d,d->compute.emitProgram,sdf,grid,z,depth);STAMP(6*slab+4);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,d->compute.command);
        glDispatchComputeIndirect(6*sizeof(GLuint));glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,0);glFlush();
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);STAMP(6*slab+5);
    }
    if(reuse){for(unsigned stamp=0;stamp<6;++stamp)STAMP(stamp);}
    else {glUseProgram(d->compute.finalizeProgram);glDispatchCompute(1,1,1);}
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
    BindSceneProgram(d,d->compute.renderProgram,sdf);UploadMorphMaterial(d,d->compute.renderProgram);
    UploadSurfacePose(d,d->compute.renderProgram);
    Vector3 f=Vec3_Normalize(Vec3_Sub(camera->target,camera->position));
    Vector3 right=Vec3_Normalize(Vec3_Cross(f,camera->up)),up=Vec3_Cross(right,f);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraPosition"),camera->position);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraForward"),f);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraRight"),right);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraUp"),up);
    glBindVertexArray(d->compute.vao);glBindBuffer(GL_DRAW_INDIRECT_BUFFER,d->compute.command);STAMP(6*timingSlabs);
    glDrawArraysIndirect(GL_TRIANGLES,0);STAMP(6*timingSlabs+1);
    glBindVertexArray(0);glBindBuffer(GL_DRAW_INDIRECT_BUFFER,0);glUseProgram(0);
    if(timing) {d->compute.pending[slot]=true;
        d->compute.timingGeneration[slot]=d->compute.stats.generation+1;
        d->compute.timingSlabs[slot]=timingSlabs;}
    d->compute.frameFences[fenceSlot]=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!d->compute.frameFences[fenceSlot])return false;
    ++d->compute.frame;++d->compute.stats.generation;
    d->compute.stats.samples=reuse?0:plan.updatedSamples;d->compute.stats.cells=reuse?0:grid->cellCount;
    d->compute.stats.slabs=reuse?0:plan.slabCount;
    d->compute.stats.gpuBytes=d->compute.sampleCapacity+d->compute.cellCapacity+d->compute.vertexCapacity;
#undef STAMP
    if(!OpenGLRenderer_CheckErrors())return false;
    if(!reuse) {
        if(d->sdfCount>d->compute.cachedSceneCapacity) {
            float (*scene)[4]=realloc(d->compute.cachedScene,d->sdfCount*sizeof(*scene));
            if(!scene)return false;
            d->compute.cachedScene=scene;d->compute.cachedSceneCapacity=d->sdfCount;
        }
        memcpy(d->compute.cachedScene,d->sdfData,d->sdfCount*sizeof(*d->sdfData));
        d->compute.cachedSceneCount=d->sdfCount;d->compute.cachedGrid=*grid;
        d->compute.cachedParts=sdf->bodyPartCount;d->compute.cachedConnectors=sdf->connectorCount;
        d->compute.cachedMouths=sdf->mouthCount;d->compute.cachedAxial=sdf->axialStationCount;
        d->compute.cachedSmoothness=sdf->config.bodySmoothness;d->compute.denseValid=true;
    }
    return true;
}
static bool InitSparseSurface(OpenGLRendererData* d,const GpuSDFSurfaceCache* cache) {
    if(d->sparse.initialized)return d->sparse.brickCells==cache->brickCells;
    if(!InitComputeSurface(d))return false;
#define LINK(member,source) do {d->sparse.member=LinkComputeSource(source,sizeof(source)/sizeof(*source));if(!d->sparse.member)return false;} while(0)
    LINK(releaseProgram,sdfSparseReleaseSource);LINK(sampleProgram,sdfSparseSampleSource);
    LINK(sampleClaimProgram,sdfSparseSampleClaimSource);
    LINK(materialClaimProgram,sdfSparseMaterialClaimSource);LINK(materialProgram,sdfSparseMaterialSource);
    LINK(classifyProgram,sdfSparseClassifySource);LINK(allocateProgram,sdfSparseAllocateSource);
    LINK(persistProgram,sdfSparsePersistSource);LINK(emitProgram,sdfSparseEmitSource);LINK(prepareProgram,sdfSparsePrepareSource);LINK(rootProgram,sdfSparseRootSource);LINK(statisticsProgram,sdfSparseStatisticsSource);
#undef LINK
    d->sparse.brickCells=cache->brickCells;d->sparse.batchSize=512;
    d->sparse.edgeCapacity=2097152;d->sparse.pageCapacity=5461;d->sparse.sampleSlots=cache->brickCells==16?8192:32768;
    size_t side=cache->brickCells+3,samples=1;
    size_t bank0=samples,bank1=samples;
    const GpuSDFSurfaceGrid* g=&cache->grid;size_t metadata=1;
    for(unsigned axis=0;axis<3;++axis)metadata*=(g->cells[axis]+cache->brickCells-1)/cache->brickCells;
    size_t batch=d->sparse.batchSize,cells=cache->brickCells*cache->brickCells*cache->brickCells;
    if(!ComputeBuffer(&d->compute.samples,&d->compute.sampleCapacity,bank0*sizeof(float))||
       !ComputeBuffer(&d->sparse.bank1,&d->sparse.bank1Capacity,bank1*sizeof(float))||
       !ComputeBuffer(&d->sparse.scratch,&d->sparse.scratchCapacity,batch*side*side*side*sizeof(float))||
       !ComputeBuffer(&d->compute.cases,&d->compute.cellCapacity,batch*cells*sizeof(GLuint))||
       !ComputeBuffer(&d->compute.vertices,&d->compute.vertexCapacity,d->sparse.edgeCapacity*12u*sizeof(float))||
       !ComputeBuffer(&d->sparse.bricks,&d->sparse.brickCapacity,metadata*8*sizeof(GLuint))||
       !ComputeBuffer(&d->sparse.jobs,&d->sparse.jobCapacity,metadata*sizeof(GpuSDFBrickJob))||
       !ComputeBuffer(&d->sparse.draws,&d->sparse.drawCapacity,d->sparse.pageCapacity*5*sizeof(GLuint))||
       !ComputeBuffer(&d->sparse.allocator,&d->sparse.allocatorCapacity,(4+d->sparse.pageCapacity+d->sparse.sampleSlots)*sizeof(GLuint))||
       !ComputeBuffer(&d->sparse.links,&d->sparse.linkCapacity,d->sparse.pageCapacity*sizeof(GLuint))||
       !ComputeBuffer(&d->sparse.indices,&d->sparse.indexCapacity,d->sparse.pageCapacity*384u*sizeof(GLuint))||
       !ComputeBuffer(&d->sparse.edges,&d->sparse.edgeBytes,d->sparse.edgeCapacity*16u*sizeof(GLuint))||
       !ComputeBuffer(&d->sparse.rootQueue,&d->sparse.rootQueueBytes,
          (12u+2u*d->sparse.edgeCapacity+batch*side*side*side)*sizeof(GLuint)))return false;
    GLuint* initial=calloc(metadata,8*sizeof(GLuint));if(!initial)return false;
    for(size_t i=0;i<metadata;++i){initial[8*i]=UINT32_MAX;initial[8*i+3]=UINT32_MAX;}
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.bricks);glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,metadata*8*sizeof(GLuint),initial);free(initial);
    GLuint zero=0;glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.draws);
    glClearBufferData(GL_SHADER_STORAGE_BUFFER,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.bank1);
    glClearBufferData(GL_SHADER_STORAGE_BUFFER,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.edges);
    glClearBufferData(GL_SHADER_STORAGE_BUFFER,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
    size_t count=4+d->sparse.pageCapacity+d->sparse.sampleSlots;
    initial=calloc(count,sizeof(GLuint));if(!initial)return false;
    initial[0]=d->sparse.pageCapacity;initial[1]=d->sparse.sampleSlots;
    for(unsigned i=0;i<d->sparse.pageCapacity;++i)initial[4+i]=i;
    for(unsigned i=0;i<d->sparse.sampleSlots;++i)initial[4+d->sparse.pageCapacity+i]=i;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.allocator);glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,count*sizeof(GLuint),initial);free(initial);
    GLuint command[11]={0,1,0,0,0,0,0,0,0,0,0};glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.command);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(command),command,GL_DYNAMIC_DRAW);
    glGenBuffers(4,d->sparse.metrics);
    for(unsigned i=0;i<4;++i){glBindBuffer(GL_COPY_WRITE_BUFFER,d->sparse.metrics[i]);glBufferData(GL_COPY_WRITE_BUFFER,10*sizeof(GLuint),NULL,GL_STREAM_READ);}
    glGenBuffers(2,d->sparse.completion);
    for(unsigned i=0;i<2;++i){glBindBuffer(GL_COPY_WRITE_BUFFER,d->sparse.completion[i]);glBufferData(GL_COPY_WRITE_BUFFER,sizeof(GLuint),NULL,GL_STREAM_READ);}
    d->sparse.initialized=true;
    return OpenGLRenderer_CheckErrors();
}
static void BindSparseProgram(OpenGLRendererData* d,GLuint p,const MonsterSDF* sdf,
    const GpuSDFSurfaceCache* cache,unsigned base,unsigned count) {
    BindGridProgram(d,p,sdf,&cache->grid);
    glUniform1i(glGetUniformLocation(p,"resetMaterialQueue"),0);
#define UINT(name,value) glUniform1ui(glGetUniformLocation(p,#name),(GLuint)(value))
    UINT(brickCells,cache->brickCells);UINT(jobCount,count);UINT(jobBase,base);
    UINT(pageCapacity,d->sparse.pageCapacity);UINT(sampleSlotCapacity,d->sparse.sampleSlots);
    UINT(sampleBankSize,d->sparse.bankSize);UINT(generation,cache->generation);UINT(edgeCapacity,d->sparse.edgeCapacity);
    glUniform1i(glGetUniformLocation(p,"cachedNormals"),cache->cachedNormals);
#undef UINT
}
static void DispatchSparse(unsigned count,unsigned workgroup) {
    if(!count)return;
    GLuint groups=(count+workgroup-1)/workgroup;
    glDispatchCompute(groups>65535?65535:groups,(groups+65534)/65535,1);
}
/* Mantener el reinicio en la cola GPU: glBufferSubData sobre una cola
 * todavía leída por compute puede esperar o copiar su almacenamiento completo. */
static void ClearSparseCounters(GLuint buffer,size_t first,size_t count) {
    GLuint zero=0;glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    glClearBufferSubData(GL_SHADER_STORAGE_BUFFER,GL_R32UI,(GLintptr)(first*sizeof(GLuint)),
        (GLsizeiptr)(count*sizeof(GLuint)),GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
}
/* Intervalos cerrados del campo, incluida la banda usada por las normales.
 * Una caja certificada sin ceros libera su geometría antigua, pero no necesita
 * evaluar miles de esquinas para volver a demostrar que está vacía. */
static size_t CertifySparseJobs(OpenGLRendererData* d,const MonsterSDF* sdf,const GpuSDFSurfaceCache* cache) {
    if(cache->dirtyCount>d->sparse.uploadJobCapacity) {
        GpuSDFBrickJob* jobs=realloc(d->sparse.uploadJobs,cache->dirtyCount*sizeof(*jobs));
        if(!jobs)return SIZE_MAX;
        d->sparse.uploadJobs=jobs;d->sparse.uploadJobCapacity=cache->dirtyCount;
    }
    if(getenv("MONSTER_SPARSE_NO_RANGE_CERTIFICATION")) {
        memcpy(d->sparse.uploadJobs,cache->dirty,cache->dirtyCount*sizeof(*cache->dirty));return 0;
    }
    SDFField field=MonsterSDF_GetField(sdf);size_t certified=0;
    AABB3D jawBounds[sdf->mouthCount?sdf->mouthCount:1];
    for(size_t i=0;i<sdf->mouthCount;++i)jawBounds[i]=RayBounds_PosedJaw(&sdf->mouths[i],.001f);
    for(size_t i=0;i<cache->dirtyCount;++i) {
        GpuSDFBrickJob job=cache->dirty[i];float h=cache->grid.voxelSize;
        AABB3D box={Vec3_Add(cache->grid.origin,Vec3_Create(((float)job.x-1)*h,((float)job.y-1)*h,((float)job.z-1)*h)),
            Vec3_Add(cache->grid.origin,Vec3_Create((job.x+cache->brickCells+1)*h,(job.y+cache->brickCells+1)*h,(job.z+cache->brickCells+1)*h))};
        AABB_Pad(&box,1e-5f);float lo,hi;
        if(field.getCellRange && field.getCellRange(field.context,box,&lo,&hi)) {
            bool empty=hi<-.0001f;
            if(lo>.0001f) {
                empty=true;
                for(size_t m=0;m<sdf->mouthCount;++m) {
                    AABB3D b=jawBounds[m];
                    if(!(box.end.x<b.start.x||box.start.x>b.end.x||box.end.y<b.start.y||
                         box.start.y>b.end.y||box.end.z<b.start.z||box.start.z>b.end.z)){empty=false;break;}
                }
            }
            if(empty){job.slot|=0x80000000u;if(hi<0)job.slot|=0x40000000u;++certified;}
        }
        d->sparse.uploadJobs[i]=job;
    }
    return certified;
}

/* Páginas compactas de esquinas. Los halos vecinos consultan el mismo punto
 * de la retícula; se reserva sólo la unión de páginas realmente solicitadas,
 * sin convertir la envolvente global del cache sparse en un volumen denso. */
static bool PrepareSparsePointPages(OpenGLRendererData* d,const GpuSDFSurfaceCache* cache) {
    if(!cache->dirtyCount)return true;
    unsigned b=cache->brickCells,counts[3]={cache->grid.cells[0]/b+1,cache->grid.cells[1]/b+1,cache->grid.cells[2]/b+1};
    size_t total=(size_t)counts[0]*counts[1]*counts[2];
    if(total>d->sparse.pointMapCPUCapacity) {
        GLuint* map=realloc(d->sparse.pointMapCPU,total*sizeof(*map));if(!map)return false;
        d->sparse.pointMapCPU=map;d->sparse.pointMapCPUCapacity=total;
    }
    memset(d->sparse.pointMapCPU,0xff,total*sizeof(*d->sparse.pointMapCPU));unsigned next=0;
    for(size_t i=0;i<cache->dirtyCount;++i) {
        const GpuSDFBrickJob* job=&d->sparse.uploadJobs[i];
        if(job->slot&0x80000000u)continue;
        uint32_t coords[3]={job->x,job->y,job->z};unsigned lo[3],hi[3];
        for(unsigned axis=0;axis<3;++axis) {
            lo[axis]=(coords[axis]?coords[axis]-1:0)/b;
            uint64_t end=(uint64_t)coords[axis]+b+1;
            if(end>cache->grid.cells[axis])end=cache->grid.cells[axis];
            hi[axis]=(unsigned)end/b;
        }
        for(unsigned z=lo[2];z<=hi[2];++z)for(unsigned y=lo[1];y<=hi[1];++y)for(unsigned x=lo[0];x<=hi[0];++x) {
            size_t id=x+(size_t)counts[0]*(y+(size_t)counts[1]*z);
            if(d->sparse.pointMapCPU[id]==UINT32_MAX)d->sparse.pointMapCPU[id]=next++;
        }
    }
    if(!ComputeBuffer(&d->sparse.pointMap,&d->sparse.pointMapCapacity,total*sizeof(GLuint)))return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.pointMap);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,total*sizeof(GLuint),d->sparse.pointMapCPU);
    size_t bytes=(size_t)next*b*b*b*sizeof(float);if(!bytes)bytes=sizeof(float);
    bool growing=bytes>d->sparse.bank1Capacity;
    if(!ComputeBuffer(&d->compute.samples,&d->compute.sampleCapacity,bytes)||
       !ComputeBuffer(&d->sparse.bank1,&d->sparse.bank1Capacity,bytes))return false;
    if(growing) {
        GLuint zero=0;glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.bank1);
        glClearBufferData(GL_SHADER_STORAGE_BUFFER,GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,&zero);
    }
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);return true;
}

static bool VerifySparseSamples(OpenGLRendererData* d,const MonsterSDF* sdf,const GpuSDFSurfaceCache* cache,size_t base,size_t count) {
    size_t side=cache->brickCells+3,n=side*side*side,total=count*n;
    float* values=malloc(total*sizeof(*values));if(!values)return false;
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER,d->sparse.scratch);glGetBufferSubData(GL_COPY_READ_BUFFER,0,total*sizeof(*values),values);
    float error=0;
    for(size_t job=0;job<count;++job)for(unsigned probe=0;probe<17;++probe) {
        size_t x=probe<8?((probe&1)?side-1:0):(probe*7)%side;
        size_t y=probe<8?((probe&2)?side-1:0):(probe*11)%side;
        size_t z=probe<8?((probe&4)?side-1:0):(probe*13)%side;
        const GpuSDFBrickJob* key=&cache->dirty[base+job];float h=cache->grid.voxelSize;
        uint32_t certified=d->sparse.uploadJobs[base+job].slot;
        Vector3 p=Vec3_Add(cache->grid.origin,Vec3_Create(((float)key->x+x-1)*h,((float)key->y+y-1)*h,((float)key->z+z-1)*h));
        float cpu=MonsterSDF_EvaluateVisualDistance(sdf,p),gpu=values[job*n+x+side*(y+side*z)];
        float delta=(certified&0x80000000u)?((certified&0x40000000u)?(cpu<0?0:1):(cpu>0?0:1)):fabsf(cpu-gpu);if(!isfinite(delta)){free(values);return false;}
        error=fmaxf(error,delta);
    }
    free(values);
    printf("[MUESTRAS SDF] bricks=%zu puntos=%zu error=%.9f\n",count,count*17,error);
    return error<1e-4f;
}

static uint64_t SparsePaletteFingerprint(const MonsterSDF* sdf) {
    uint64_t h=1469598103934665603ULL;
#define HASH_COLOR(c) do {const unsigned char* bytes=(const unsigned char*)&(c);for(size_t k=0;k<sizeof(c);++k){h^=bytes[k];h*=1099511628211ULL;}} while(0)
    HASH_COLOR(sdf->hasSurface);
    for(size_t i=0;i<sdf->bodyPartCount;++i)HASH_COLOR(sdf->bodyParts[i].color);
    for(size_t i=0;i<sdf->connectorCount;++i)HASH_COLOR(sdf->connectors[i].color);
    for(size_t i=0;i<sdf->mouthCount;++i){HASH_COLOR(sdf->mouths[i].skinColor);HASH_COLOR(sdf->mouths[i].insideColor);}
#undef HASH_COLOR
    return h;
}

bool OpenGLRenderer_RenderGpuSparseSDFSurface(Renderer3D* r,const MonsterSDF* sdf,
    GpuSDFSurfaceCache* cache,const ICamera* camera) {
    if(!OpenGLRenderer_SupportsComputeSurface(r)||!sdf||!cache||!camera)return false;
    OpenGLRendererData* d=r->user_data;
    if(cache->grid.sampleCount>UINT32_MAX/3u||!InitSparseSurface(d,cache))return false;
    d->compute.denseValid=false;
    d->compute.extractionVoxel=cache->grid.voxelSize;
    if(d->sparse.failed)return false;
    unsigned fenceSlot=d->compute.frame%2;
    if(d->compute.frameFences[fenceSlot]) {
        GLenum wait;glFlush();do {wait=glClientWaitSync(d->compute.frameFences[fenceSlot],0,0);if(wait==GL_TIMEOUT_EXPIRED)SDL_Delay(1);}while(wait==GL_TIMEOUT_EXPIRED);
        if(wait==GL_WAIT_FAILED)return false;
        glDeleteSync(d->compute.frameFences[fenceSlot]);d->compute.frameFences[fenceSlot]=NULL;
        if(d->sparse.submittedGeneration[fenceSlot]) {
            GLuint overflow=0;glBindBuffer(GL_COPY_READ_BUFFER,d->sparse.completion[fenceSlot]);
            glGetBufferSubData(GL_COPY_READ_BUFFER,0,sizeof(overflow),&overflow);
            if(overflow){d->sparse.failed=true;
                fprintf(stderr,"[GPU SUPERFICIE] Generación %llu incompleta: overflow=%u; no certificada\n",
                    (unsigned long long)d->sparse.submittedGeneration[fenceSlot],overflow);return false;}
            GpuSDFSurfaceCache_Commit(cache,d->sparse.submittedGeneration[fenceSlot]);
        }
    }
    // La publicación anterior consume estos mismos buffers: ordenar sus lecturas
    // antes de las escrituras del nuevo cuadro, sin esperar desde la CPU.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT|
        GL_ELEMENT_ARRAY_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
    uint64_t palette=SparsePaletteFingerprint(sdf);
    bool materialChanged=!d->sparse.hasPalette||palette!=d->sparse.paletteFingerprint;
    GLuint sampleProgram=d->sparse.sampleProgram,rootProgram=d->sparse.rootProgram,materialProgram=d->sparse.materialProgram;
    if(sdf->mouthCount==1&&M_STRIDE<=1024&&getenv("MONSTER_SDF_NO_HEAD_CACHE")==NULL) {
        if(!d->sparse.cachedSampleProgram)
            d->sparse.cachedSampleProgram=LinkComputeSource(sdfSparseCachedSampleSource,sizeof(sdfSparseCachedSampleSource)/sizeof(*sdfSparseCachedSampleSource));
        if(!d->sparse.cachedRootProgram)
            d->sparse.cachedRootProgram=LinkComputeSource(sdfSparseCachedRootSource,sizeof(sdfSparseCachedRootSource)/sizeof(*sdfSparseCachedRootSource));
        if(!d->sparse.cachedSampleProgram||!d->sparse.cachedRootProgram)return false;
        if(!d->sparse.cachedMaterialProgram)
            d->sparse.cachedMaterialProgram=LinkComputeSource(sdfSparseCachedMaterialSource,sizeof(sdfSparseCachedMaterialSource)/sizeof(*sdfSparseCachedMaterialSource));
        if(!d->sparse.cachedMaterialProgram)return false;
        sampleProgram=d->sparse.cachedSampleProgram;rootProgram=d->sparse.cachedRootProgram;materialProgram=d->sparse.cachedMaterialProgram;
    }
    bool diagnostic=getenv("MONSTER_SPARSE_GPU_DIAGNOSTIC")!=NULL;
#define DIAG(stage) do{if(diagnostic){fprintf(stderr,"[GPU DIAG] gen=%llu stage=%s\n",(unsigned long long)cache->generation,stage);fflush(stderr);glFinish();}}while(0)
    double begin=RendererNowMs();
    if(!GpuSDFSurfaceCache_Update(cache,sdf)||!UploadSDFScene(d,sdf,NULL,0,0))return false;
    if(cache->nextSlot*8u*sizeof(GLuint)>d->sparse.brickCapacity)return false;
    double certifyStart=RendererNowMs();size_t certifiedJobs=CertifySparseJobs(d,sdf,cache);
    if(certifiedJobs==SIZE_MAX||!PrepareSparsePointPages(d,cache))return false;
    d->compute.stats.certificationCPUms=RendererNowMs()-certifyStart;
    d->compute.stats.certifiedEmptyBricks=certifiedJobs;
    GLuint buffers[]={d->compute.samples,d->compute.cases,d->compute.tables,d->compute.command,d->compute.vertices,
        d->sparse.scratch,d->sparse.bricks,d->sparse.jobs,d->sparse.draws,d->sparse.allocator,d->sparse.links,d->sparse.bank1,d->sparse.indices,d->sparse.edges,d->sparse.rootQueue};
    for(unsigned i=0;i<15;++i)glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i+1,buffers[i]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,16,d->sparse.pointMap);
    ClearSparseCounters(d->compute.command,5,3);
    ClearSparseCounters(d->compute.command,9,2);
    ClearSparseCounters(d->sparse.rootQueue,0,4);
    ClearSparseCounters(d->sparse.rootQueue,4u+d->sparse.edgeCapacity,4);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
    CollectComputeTimers(d);unsigned slot=d->compute.frame%4;
    bool timing=!d->compute.pending[slot]&&ReserveComputeTimers(d,slot,8);
    GLuint* q=d->compute.timestamps[slot];
    // Los retiros preceden al reuso de slots; sólo se transfieren identificadores.
    if(cache->retiredCount) {
        GpuSDFBrickJob* retired=calloc(cache->retiredCount,sizeof(*retired));if(!retired)return false;
        for(size_t i=0;i<cache->retiredCount;++i)retired[i].slot=cache->retired[i];
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.jobs);glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,cache->retiredCount*sizeof(*retired),retired);free(retired);
        BindSparseProgram(d,d->sparse.releaseProgram,sdf,cache,0,(unsigned)cache->retiredCount);
        glUniform1i(glGetUniformLocation(d->sparse.releaseProgram,"retireOnly"),1);DispatchSparse((unsigned)cache->retiredCount,64);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.jobs);
    if(cache->dirtyCount)glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,cache->dirtyCount*sizeof(*cache->dirty),d->sparse.uploadJobs);
    BindSparseProgram(d,d->sparse.releaseProgram,sdf,cache,0,(unsigned)cache->dirtyCount);
    glUniform1i(glGetUniformLocation(d->sparse.releaseProgram,"retireOnly"),0);DispatchSparse((unsigned)cache->dirtyCount,64);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    DIAG("release");
    d->compute.stats.uploadCPUms=RendererNowMs()-begin;
    unsigned side=cache->brickCells+3,n=side*side*side,cells=cache->brickCells*cache->brickCells*cache->brickCells;
    // Pares de consultas por lote: cada etapa se mide sin glFinish ni readback de geometría.
    unsigned batches=(unsigned)((cache->dirtyCount+d->sparse.batchSize-1)/d->sparse.batchSize);
    if(!batches)batches=1;
    timing=!d->compute.pending[slot]&&ReserveComputeTimers(d,slot,6*batches+2);q=d->compute.timestamps[slot];
#define STAMP(i) do{if(timing)glQueryCounter(q[i],GL_TIMESTAMP);}while(0)
    for(unsigned batch=0,base=0;batch<batches;++batch,base+=d->sparse.batchSize) {
        unsigned count=cache->dirtyCount>base?(unsigned)(cache->dirtyCount-base):0;if(count>d->sparse.batchSize)count=d->sparse.batchSize;
        STAMP(6*batch);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        ClearSparseCounters(d->sparse.rootQueue,8u+2u*d->sparse.edgeCapacity,4);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
        BindSparseProgram(d,d->sparse.sampleClaimProgram,sdf,cache,base,count);DispatchSparse(count*n,128);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        BindSparseProgram(d,d->sparse.prepareProgram,sdf,cache,base,count);glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
        BindSparseProgram(d,sampleProgram,sdf,cache,base,count);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,d->sparse.rootQueue);glDispatchComputeIndirect((9u+2u*d->sparse.edgeCapacity)*sizeof(GLuint));
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,0);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        BindSparseProgram(d,d->sparse.persistProgram,sdf,cache,base,count);DispatchSparse(count*n,128);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        if(getenv("MONSTER_SPARSE_VERIFY_SAMPLES")&&!VerifySparseSamples(d,sdf,cache,base,count))return false;
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);DIAG("sample");STAMP(6*batch+1);
        STAMP(6*batch+2);BindSparseProgram(d,d->sparse.classifyProgram,sdf,cache,base,count);DispatchSparse(count*cells,128);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        BindSparseProgram(d,d->sparse.allocateProgram,sdf,cache,base,count);
        glUniform1i(glGetUniformLocation(d->sparse.allocateProgram,"allocatePass"),0);DispatchSparse(count,64);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glUniform1i(glGetUniformLocation(d->sparse.allocateProgram,"allocatePass"),1);DispatchSparse(count,64);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);DIAG("classify+allocate");STAMP(6*batch+3);
        STAMP(6*batch+4);
        BindSparseProgram(d,d->sparse.emitProgram,sdf,cache,base,count);DispatchSparse(count*cells,64);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        if(batch+1==batches&&cache->dirtyCount){
            BindSparseProgram(d,d->sparse.prepareProgram,sdf,cache,0,0);glDispatchCompute(1,1,1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
            BindSparseProgram(d,rootProgram,sdf,cache,0,0);
            glUniform1i(glGetUniformLocation(rootProgram,"updateRootMaterial"),!materialChanged);
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,d->sparse.rootQueue);
            glDispatchComputeIndirect(sizeof(GLuint));glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,0);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);}
        DIAG("emit+root");STAMP(6*batch+5);glFlush();
    }
    BindSparseProgram(d,d->sparse.statisticsProgram,sdf,cache,0,0);
    glUniform1ui(glGetUniformLocation(d->sparse.statisticsProgram,"metadataCount"),cache->nextSlot);
    glUniform1i(glGetUniformLocation(d->sparse.statisticsProgram,"statisticsPass"),0);
    DispatchSparse(cache->nextSlot>d->sparse.pageCapacity?cache->nextSlot:d->sparse.pageCapacity,128);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    if(cache->dirtyCount||cache->retiredCount) {
        BindSparseProgram(d,d->sparse.prepareProgram,sdf,cache,0,0);glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
        BindSparseProgram(d,d->sparse.statisticsProgram,sdf,cache,0,0);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,d->sparse.rootQueue);
        glUniform1i(glGetUniformLocation(d->sparse.statisticsProgram,"statisticsPass"),2);
        glDispatchComputeIndirect((5u+d->sparse.edgeCapacity)*sizeof(GLuint));glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glUniform1i(glGetUniformLocation(d->sparse.statisticsProgram,"statisticsPass"),3);
        glDispatchComputeIndirect((5u+d->sparse.edgeCapacity)*sizeof(GLuint));glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,0);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    if(materialChanged) {
        BindSparseProgram(d,d->sparse.prepareProgram,sdf,cache,0,0);
        glUniform1i(glGetUniformLocation(d->sparse.prepareProgram,"resetMaterialQueue"),1);glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        BindSparseProgram(d,d->sparse.materialClaimProgram,sdf,cache,0,0);
        DispatchSparse(d->sparse.pageCapacity*384u,128);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        BindSparseProgram(d,d->sparse.prepareProgram,sdf,cache,0,0);glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT);
        BindSparseProgram(d,materialProgram,sdf,cache,0,0);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,d->sparse.rootQueue);glDispatchComputeIndirect(sizeof(GLuint));
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,0);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        d->sparse.paletteFingerprint=palette;d->sparse.hasPalette=true;
    }
    DIAG("statistics");
    // Publicación completa antes de cualquier draw; las páginas limpias nunca se reescriben.
    glUseProgram(d->compute.finalizeProgram);glDispatchCompute(1,1,1);
    // Los índices se consumen como EBO y la telemetría usa copias de buffers.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT|
        GL_ELEMENT_ARRAY_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
    GpuSDFSurfaceCache_Submit(cache,cache->generation);
    glBindBuffer(GL_COPY_READ_BUFFER,d->compute.command);
    glBindBuffer(GL_COPY_WRITE_BUFFER,d->sparse.completion[fenceSlot]);
    glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,4*sizeof(GLuint),0,sizeof(GLuint));
    d->sparse.submittedGeneration[fenceSlot]=cache->generation;
    BindSceneProgram(d,d->compute.renderProgram,sdf);UploadMorphMaterial(d,d->compute.renderProgram);
    UploadSurfacePose(d,d->compute.renderProgram);
    Vector3 f=Vec3_Normalize(Vec3_Sub(camera->target,camera->position)),right=Vec3_Normalize(Vec3_Cross(f,camera->up)),up=Vec3_Cross(right,f);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraPosition"),camera->position);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraForward"),f);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraRight"),right);
    UniformVector(glGetUniformLocation(d->compute.renderProgram,"cameraUp"),up);
    glBindVertexArray(d->compute.vao);STAMP(6*batches);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,d->sparse.draws);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,d->sparse.indices);
    glMultiDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_INT,NULL,(GLsizei)d->sparse.pageCapacity,0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,d->compute.command);glDrawArraysIndirect(GL_TRIANGLES,0);
    if(timing){glBindBuffer(GL_COPY_READ_BUFFER,d->sparse.allocator);glBindBuffer(GL_COPY_WRITE_BUFFER,d->sparse.metrics[slot]);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,4*sizeof(GLuint));
        glBindBuffer(GL_COPY_READ_BUFFER,d->compute.command);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,5*sizeof(GLuint),4*sizeof(GLuint),6*sizeof(GLuint));}
    STAMP(6*batches+1);
    glBindVertexArray(0);glBindBuffer(GL_DRAW_INDIRECT_BUFFER,0);glUseProgram(0);
    if(timing){d->compute.pending[slot]=true;d->compute.timingGeneration[slot]=d->compute.stats.generation+1;d->compute.timingSlabs[slot]=batches;}
    d->compute.frameFences[fenceSlot]=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!d->compute.frameFences[fenceSlot])return false;
    ++d->compute.frame;++d->compute.stats.generation;
    d->compute.stats.sampleRequests=(cache->dirtyCount-certifiedJobs)*n;d->compute.stats.cells=cache->dirtyCount*cells;d->compute.stats.slabs=batches;
    d->compute.stats.candidateBricks=cache->residentCount;d->compute.stats.dirtyBricks=cache->dirtyCount;d->compute.stats.newBricks=cache->newCount;

    d->compute.stats.gpuBytes=d->compute.sampleCapacity+d->sparse.bank1Capacity+d->sparse.scratchCapacity+d->compute.cellCapacity+
        d->compute.vertexCapacity+d->sparse.brickCapacity+d->sparse.jobCapacity+d->sparse.drawCapacity+d->sparse.allocatorCapacity+d->sparse.linkCapacity+d->sparse.indexCapacity+d->sparse.edgeBytes+d->sparse.rootQueueBytes+d->sparse.pointMapCapacity;
#undef DIAG
#undef STAMP
    return OpenGLRenderer_CheckErrors();
}

/* Auditoría explícita de referencias: no se descarga durante reproducción. */
static unsigned AuditSparseEdgeReferences(OpenGLRendererData* d,unsigned unique) {
    GLuint (*draws)[5]=malloc(d->sparse.drawCapacity);
    GLuint* indices=malloc(d->sparse.indexCapacity);
    GLuint (*edges)[16]=malloc(d->sparse.edgeBytes);
    GLuint* expected=calloc(d->sparse.edgeCapacity,sizeof(*expected));
    if(!draws||!indices||!edges||!expected){free(draws);free(indices);free(edges);free(expected);return 1;}
    glBindBuffer(GL_COPY_READ_BUFFER,d->sparse.draws);glGetBufferSubData(GL_COPY_READ_BUFFER,0,d->sparse.drawCapacity,draws);
    glBindBuffer(GL_COPY_READ_BUFFER,d->sparse.indices);glGetBufferSubData(GL_COPY_READ_BUFFER,0,d->sparse.indexCapacity,indices);
    glBindBuffer(GL_COPY_READ_BUFFER,d->sparse.edges);glGetBufferSubData(GL_COPY_READ_BUFFER,0,d->sparse.edgeBytes,edges);
    unsigned errors=0,live=0;
    for(unsigned page=0;page<d->sparse.pageCapacity;++page)for(unsigned i=0;i<draws[page][0];++i) {
        if(i>=384u){++errors;break;}
        GLuint edge=indices[page*384u+i];
        if(edge>=d->sparse.edgeCapacity)++errors;else ++expected[edge];
    }
    for(unsigned i=0;i<d->sparse.edgeCapacity;++i) {
        if(expected[i]!=edges[i][13])++errors;
        if(expected[i]){++live;if(edges[i][0]==0||edges[i][0]==UINT32_MAX)++errors;}
        else if(edges[i][0]!=0&&edges[i][0]!=UINT32_MAX)++errors;
    }
    if(live!=unique)++errors;
    free(draws);free(indices);free(edges);free(expected);return errors;
}

bool OpenGLRenderer_SaveSparseSDFDiagnostics(Renderer3D* r,const GpuSDFSurfaceCache* cache,const char* path) {
    if(!r||!r->user_data||!cache||!path)return false;
    OpenGLRendererData* d=r->user_data;if(!d->sparse.initialized)return false;
    GLuint allocator[4],command[11];GLuint (*bricks)[8]=malloc(cache->nextSlot*sizeof(*bricks));
    if(!bricks)return false;
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.allocator);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(allocator),allocator);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.command);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(command),command);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.bricks);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,cache->nextSlot*sizeof(*bricks),bricks);
    // Auditoría explícita: propiedad única de páginas y samples, sin readback en reproducción.
    GLuint* links=malloc(d->sparse.linkCapacity),*pool=malloc(d->sparse.allocatorCapacity);
    unsigned char* pageOwners=calloc(d->sparse.pageCapacity,1),*sampleOwners=calloc(d->sparse.sampleSlots,1);
    if(!links||!pool||!pageOwners||!sampleOwners){free(bricks);free(links);free(pool);free(pageOwners);free(sampleOwners);return false;}
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.links);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,d->sparse.linkCapacity,links);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.allocator);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,d->sparse.allocatorCapacity,pool);
    unsigned integrityErrors=0,ownedPages=0,ownedSamples=0,ownedBricks=0;
    for(size_t slot=0;slot<cache->nextSlot;++slot){
        GLuint page=bricks[slot][0],pages=bricks[slot][1],sample=bricks[slot][3];
        if(pages>d->sparse.pageCapacity){++integrityErrors;continue;}
        if(pages)++ownedBricks;
        for(unsigned step=0;step<pages;++step){
            if(page>=d->sparse.pageCapacity||pageOwners[page]){++integrityErrors;break;}
            pageOwners[page]=1;++ownedPages;page=links[page];
        }
        if(page!=UINT32_MAX)++integrityErrors;
        if(sample!=UINT32_MAX){if(sample>=d->sparse.sampleSlots||sampleOwners[sample])++integrityErrors;
            else{sampleOwners[sample]=1;++ownedSamples;}}
    }
    if(allocator[0]>d->sparse.pageCapacity||allocator[1]>d->sparse.sampleSlots)++integrityErrors;
    else{
        for(unsigned i=0;i<allocator[0];++i){GLuint page=pool[4+i];
            if(page>=d->sparse.pageCapacity||pageOwners[page])++integrityErrors;else pageOwners[page]=1;}
        for(unsigned i=0;i<allocator[1];++i){GLuint sample=pool[4+d->sparse.pageCapacity+i];
            if(sample>=d->sparse.sampleSlots||sampleOwners[sample])++integrityErrors;else sampleOwners[sample]=1;}
    }
    if(ownedPages+allocator[0]!=d->sparse.pageCapacity||ownedSamples+allocator[1]!=d->sparse.sampleSlots||
       ownedBricks!=allocator[2]||ownedSamples!=allocator[3])++integrityErrors;
    free(links);free(pool);free(pageOwners);free(sampleOwners);
    unsigned referenceErrors=AuditSparseEdgeReferences(d,command[8]);
    FILE* out=fopen(path,"w");if(!out){free(bricks);return false;}
    fprintf(out,"{\"generation\":%llu,\"origin\":[%.9g,%.9g,%.9g],\"voxel\":%.9g,\"brick_cells\":%u,\"candidate_bricks\":%zu,\"dirty_bricks\":%zu,\"sampled\":%zu,\"active_bricks\":%u,\"resident_sample_bricks\":%u,\"unique_edges\":%u,\"updated_edges\":%u,\"indices\":%u,\"reused_samples\":%u,\"overflow\":%u,\"exact_normals\":%u,\"published_bricks\":[",
        (unsigned long long)cache->generation,cache->grid.origin.x,cache->grid.origin.y,cache->grid.origin.z,cache->grid.voxelSize,
        cache->brickCells,cache->residentCount,cache->dirtyCount,(size_t)command[10],
        allocator[2],allocator[3],command[8],command[5],command[6],command[7],command[4],command[9]);
    bool first=true;
    for(size_t i=0;i<cache->spatial->brickCount;++i){const MorphSpatialBrick* b=&cache->spatial->bricks[i];
        if(b->resident&&bricks[b->gpuSlot][1]>0){fprintf(out,"%s[%d,%d,%d]",first?"":",",b->bx,b->by,b->bz);first=false;}}
    fprintf(out,"],\"allocator_integrity_errors\":%u,\"owned_pages\":%u,\"free_pages\":%u,\"owned_samples\":%u,\"free_samples\":%u,\"edge_reference_errors\":%u}\n",integrityErrors,ownedPages,allocator[0],ownedSamples,allocator[1],referenceErrors);free(bricks);
    bool saved=fclose(out)==0,glOK=OpenGLRenderer_CheckErrors();
    if(referenceErrors)fprintf(stderr,"[GPU REFERENCIAS] errores=%u\n",referenceErrors);
    return saved&&glOK&&integrityErrors==0&&referenceErrors==0&&command[4]==0;
}

GpuSDFSurfaceStats OpenGLRenderer_GetGpuSDFSurfaceStats(const Renderer3D* r) {
    return r&&r->user_data?((OpenGLRendererData*)r->user_data)->compute.stats:(GpuSDFSurfaceStats){0};
}
typedef struct SurfaceAuditEdge {uint64_t a,b;int direction;} SurfaceAuditEdge;
static int CompareSurfaceAuditEdges(const void* lhs,const void* rhs) {
    const SurfaceAuditEdge* a=lhs;const SurfaceAuditEdge* b=rhs;
    if(a->a!=b->a)return a->a<b->a?-1:1;
    return a->b==b->b?0:(a->b<b->b?-1:1);
}
static uint64_t SurfaceAuditVertexId(const float* vertex) {
    uint32_t a,b;memcpy(&a,vertex+3,sizeof(a));memcpy(&b,vertex+7,sizeof(b));
    return ((uint64_t)a<<32)|b;
}
static bool ReadGpuSurfaceTriangles(Renderer3D* r,float (**result)[8],size_t* resultCount) {
    if(!r||!r->user_data||!result||!resultCount)return false;
    OpenGLRendererData* d=r->user_data;GLuint command[5];
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.command);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(command),command);
    if(command[4]) {fprintf(stderr,"[GPU GEOMETRÍA] desbordamiento de capacidad\n");return false;}
    size_t count=command[0];float (*v)[8]=NULL;
    if(d->sparse.initialized) {
        GLuint (*draws)[5]=malloc(d->sparse.pageCapacity*sizeof(*draws));
        GLuint* indices=malloc(d->sparse.indexCapacity);
        float (*allVertices)[12]=malloc(d->compute.vertexCapacity);
        if(!draws||!indices||!allVertices){free(draws);free(indices);free(allVertices);return false;}
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.draws);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,d->sparse.drawCapacity,draws);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->sparse.indices);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,d->sparse.indexCapacity,indices);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.vertices);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,d->compute.vertexCapacity,allVertices);
        count=0;for(unsigned p=0;p<d->sparse.pageCapacity;++p)count+=draws[p][0];
        v=malloc(count*sizeof(*v));if(!v){free(draws);free(indices);free(allVertices);return false;}
        size_t cursor=0;
        for(unsigned p=0;p<d->sparse.pageCapacity;++p)for(unsigned j=0;j<draws[p][0];++j) {
            GLuint index=indices[draws[p][2]+j];
            if(index>=d->sparse.edgeCapacity){free(draws);free(indices);free(allVertices);free(v);return false;}
            memcpy(v[cursor++],allVertices[index],sizeof(*v));
        }
        free(draws);free(indices);free(allVertices);
    } else {
        if(!count||count*12*sizeof(float)>d->compute.vertexCapacity)return false;
        float (*allVertices)[12]=malloc(count*sizeof(*allVertices));v=malloc(count*sizeof(*v));
        if(!allVertices||!v){free(allVertices);free(v);return false;}
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.vertices);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,count*sizeof(*allVertices),allVertices);
        for(size_t i=0;i<count;++i)memcpy(v[i],allVertices[i],sizeof(v[i]));
        free(allVertices);
    }
    if(!count){free(v);return false;}
    *result=v;*resultCount=count;return true;
}
bool OpenGLRenderer_ValidateCranialPose(Renderer3D* r,const Monster* m) {
    if(!r||!r->user_data||!m||!m->animation||!m->hasHead||m->mouthCount!=1)return false;
    OpenGLRendererData* d=r->user_data;const MonsterAnimation* a=m->animation;
    const Rig* rig=&a->rig;SkeletonPose rest;
    if(!d->compute.renderProgram||!SkeletonPose_Init(&rest,&rig->skeleton)||rig->jawJoint<0)return false;
    const HeadLandmarks* l=&m->head.anatomy.landmarks;
    Vector3 samples[]={l->skullCenter,l->muzzleRoot,l->muzzleTip,l->leftNostril,l->leftEyeCenter,l->leftEarBase};
    Vector3 host=m->bodyParts[m->head.anatomy.attachmentBodyPartIndex].positionRender;
    float vertices[10][12]={{0}},captured[10][6];Vector3 expected[10];
    Quaternion head=Quat_Multiply(a->pose.joints[rig->headJoint].worldRotation,
        Quat_Inverse(rest.joints[rig->headJoint].worldRotation));
    Vector3 headRest=rest.joints[rig->headJoint].worldPosition,headNow=a->pose.joints[rig->headJoint].worldPosition;
    for(int k=0;k<10;++k) {
        Vector3 p,n=Vec3_Create(0,1,0);
        vertices[k][11]=131327;
        if(k<6) {
            p=Vec3_Add(host,samples[k]);
            expected[k]=Vec3_Add(headNow,Quat_RotateVector(head,Vec3_Sub(p,headRest)));
        } else if(k<8) {
            Vector3 pivot=rest.joints[rig->jawJoint].worldPosition;
            Vector3 rel=Vec3_Sub(Vec3_Add(host,k==6?l->mandibularSymphysis:l->leftMouthCorner),pivot);
            Vector3 axis=Transform3D_RotateVector(m->mouths[0].rotation,Vec3_Create(1,0,0));
            Quaternion reference=Quat_FromAxisAngle(axis,Mouth_GetJawAngle(&m->mouths[0])*.01745329252f);
            p=Vec3_Add(pivot,Quat_RotateVector(reference,rel));vertices[k][11]=65791;
            Quaternion jaw=Quat_Multiply(a->pose.joints[rig->jawJoint].worldRotation,
                Quat_Inverse(rest.joints[rig->jawJoint].worldRotation));
            expected[k]=Vec3_Add(a->pose.joints[rig->jawJoint].worldPosition,Quat_RotateVector(jaw,rel));
        } else {
            Vector3 pivot=rest.joints[rig->jawJoint].worldPosition;
            p=Vec3_Add(pivot,Vec3_Create(.07f,.08f,.09f));
            uint32_t anchor=k==8?1u:129u;vertices[k][11]=(float)(255u|(anchor<<8));
            float w=(float)(anchor-1u)/255.f;
            Vector3 axis=rig->skeleton.joints[rig->jawJoint].constraint.hingeAxis;
            Quaternion local=a->pose.joints[rig->jawJoint].localRotation;
            float delta=2*atan2f(local.x*axis.x+local.y*axis.y+local.z*axis.z,local.w)-Mouth_GetJawAngle(&m->mouths[0])*.01745329252f;
            Quaternion jaw=Quat_Multiply(head,Quat_FromAxisAngle(axis,delta));
            Quaternion blend=Quat_Slerp(head,jaw,w);
            Vector3 target=Vec3_Add(headNow,Quat_RotateVector(head,Vec3_Sub(pivot,headRest)));
            expected[k]=Vec3_Add(target,Quat_RotateVector(blend,Vec3_Sub(p,pivot)));
        }
        vertices[k][0]=p.x;vertices[k][1]=p.y;vertices[k][2]=p.z;
        vertices[k][4]=n.x;vertices[k][5]=n.y;vertices[k][6]=n.z;
    }
    GLuint buffers[3],command[5]={10,1,0,0,0};GLint oldProgram,oldVAO,old4,old5;
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVAO);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,4,&old4);glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,5,&old5);
    glGenBuffers(3,buffers);glUseProgram(d->compute.renderProgram);
    UploadSurfacePose(d,d->compute.renderProgram);UploadMorphMaterial(d,d->compute.renderProgram);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,buffers[0]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(command),command,GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,buffers[1]);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[2]);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,sizeof(captured),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffers[2]);glBindVertexArray(d->compute.vao);
    glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,10);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof(captured),captured);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,(GLuint)old4);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,(GLuint)old5);
    glBindVertexArray((GLuint)oldVAO);glUseProgram((GLuint)oldProgram);glDeleteBuffers(3,buffers);
    float error=0;bool valid=true;
    for(int k=0;k<10;++k) {
        Vector3 actual=Vec3_Create(captured[k][0],captured[k][1],captured[k][2]);
        float e=Vec3_Distance(actual,expected[k]);valid=valid&&isfinite(e);error=fmaxf(error,e);
        if(e>1e-4f)fprintf(stdout,"[CRANIAL GPU SAMPLE] indice=%d error=%.8f reposo=%.5f,%.5f,%.5f peso=%.5f\n",k,e,
            vertices[k][0],vertices[k][1],vertices[k][2],AnatomyDeformer_CranialWeight(
            Vec3_Create(vertices[k][0],vertices[k][1],vertices[k][2]),rig->cranialAttachment,rig->cranialAxis,rig->cranialCollar));
        if(e>1e-4f) {
            float best=INFINITY;size_t chosen=0;Vector3 p=Vec3_Create(vertices[k][0],vertices[k][1],vertices[k][2]);
            for(size_t i=0;i<d->poseDomain.count;++i) {
                const float* aa=d->poseDomain.data[i][0];const float* bb=d->poseDomain.data[i][1];
                Vector3 start=Vec3_Create(aa[0],aa[1],aa[2]),end=Vec3_Create(bb[0],bb[1],bb[2]),ab=Vec3_Sub(end,start);
                float t=fmaxf(0,fminf(1,Vec3_Dot(Vec3_Sub(p,start),ab)/fmaxf(Vec3_LengthSq(ab),1e-12f)));
                float radius=aa[3]+(bb[3]-aa[3])*t;
                float score=Vec3_LengthSq(Vec3_Sub(p,Vec3_Add(start,Vec3_Scale(ab,t))))/(radius*radius);
                if(score<best){best=score;chosen=i;}
            }
            fprintf(stdout,"[CRANIAL DOMAIN] indice=%zu extremos=%.5f,%.5f,%.5f / %.5f,%.5f,%.5f\n",chosen,
                d->poseDomain.data[chosen][0][0],d->poseDomain.data[chosen][0][1],d->poseDomain.data[chosen][0][2],
                d->poseDomain.data[chosen][1][0],d->poseDomain.data[chosen][1][1],d->poseDomain.data[chosen][1][2]);
        }
        Vector3 normal=Vec3_Create(captured[k][3],captured[k][4],captured[k][5]);
        valid=valid&&isfinite(Vec3_LengthSq(normal))&&fabsf(Vec3_LengthSq(normal)-1)<1e-4f;
    }
    fprintf(stdout,"[CRANIAL GPU] muestras=10 error=%.8f\n",error);
    return valid&&error<1e-4f;
}
bool OpenGLRenderer_ValidateGpuSurface(Renderer3D* r,const MonsterSDF* sdf) {
    if(!sdf)return false;
    size_t count=0;float (*v)[8]=NULL;if(!ReadGpuSurfaceTriangles(r,&v,&count))return false;
    float vertexError=0,triangleError=0;size_t opposed=0,degenerate=0;bool finite=true;
    size_t step=(count/3)/512+1;
    for(size_t tri=0;tri<count/3;tri+=step) {
        Vector3 p[3];
        for(int j=0;j<3;++j) {float* a=v[tri*3+j];p[j]=Vec3_Create(a[0],a[1],a[2]);
            float error=fabsf(MonsterSDF_EvaluateVisualDistance(sdf,p[j]));
            finite=finite&&isfinite(error);vertexError=fmaxf(vertexError,error);}
        Vector3 center=Vec3_Scale(Vec3_Add(Vec3_Add(p[0],p[1]),p[2]),1.f/3);
        float error=fabsf(MonsterSDF_EvaluateVisualDistance(sdf,center));
        finite=finite&&isfinite(error);triangleError=fmaxf(triangleError,error);
    }
    for(size_t tri=0;tri<count/3;++tri) {
        Vector3 p[3],normal=Vec3_Create(0,0,0);
        for(int j=0;j<3;++j) {float* a=v[tri*3+j];p[j]=Vec3_Create(a[0],a[1],a[2]);normal=Vec3_Add(normal,Vec3_Create(a[4],a[5],a[6]));
            for(int component=0;component<3;++component)
                finite=finite&&isfinite(a[component])&&isfinite(a[component+4]);}
        Vector3 cross=Vec3_Cross(Vec3_Sub(p[1],p[0]),Vec3_Sub(p[2],p[0]));
        if(Vec3_Dot(cross,cross)<1e-16f)++degenerate;
        else if(Vec3_Dot(cross,normal)<0)++opposed;
    }
    SurfaceAuditEdge* edges=malloc(count*sizeof(*edges));
    if(!edges){free(v);return false;}
    size_t edgeCount=0,collapsed=0;
    for(size_t tri=0;tri<count/3;++tri) {
        uint64_t ids[3];for(int j=0;j<3;++j)ids[j]=SurfaceAuditVertexId(v[3*tri+j]);
        if(ids[0]==ids[1]||ids[1]==ids[2]||ids[2]==ids[0]) {++collapsed;continue;}
        for(int j=0;j<3;++j) {
            uint64_t a=ids[j],b=ids[(j+1)%3];int dir=a<b?1:-1;
            edges[edgeCount++]=(SurfaceAuditEdge){a<b?a:b,a<b?b:a,dir};
        }
    }
    qsort(edges,edgeCount,sizeof(*edges),CompareSurfaceAuditEdges);
    size_t boundary=0,nonManifold=0,orientation=0;
    for(size_t i=0;i<edgeCount;) {
        size_t j=i+1;int winding=edges[i].direction;
        while(j<edgeCount&&edges[j].a==edges[i].a&&edges[j].b==edges[i].b)winding+=edges[j++].direction;
        if(j-i==1)++boundary;
        else if(j-i>2)++nonManifold;
        else if(winding)++orientation;
        i=j;
    }
    printf("[GPU TOPOLOGÍA] fronteras=%zu no_manifold=%zu orientación=%zu colapsados=%zu aristas=%zu\n",
        boundary,nonManifold,orientation,collapsed,edgeCount);
    free(edges);
    printf("[GPU GEOMETRÍA] vertices=%zu residual_vertice_max=%.9f residual_centro_max=%.9f opuestos=%zu degenerados=%zu\n",
        count,vertexError,triangleError,opposed,degenerate);
    float voxel=((OpenGLRendererData*)r->user_data)->compute.extractionVoxel;
    // Tolerancia de raíz: décima de celda; centro: diagonal de la celda.
    // Son gates de extracción, no una certificación de cobertura anatómica.
    bool geometryOK=count>0&&count%3==0&&boundary==0&&nonManifold==0&&orientation==0&&
        collapsed==0&&opposed==0&&degenerate==0&&finite&&voxel>0&&
        vertexError<=.1f*voxel&&triangleError<=sqrtf(3.f)*voxel;
    free(v);return OpenGLRenderer_CheckErrors()&&geometryOK;
}

typedef struct SurfaceSignatureTriangle {uint64_t ids[3];} SurfaceSignatureTriangle;
typedef struct SurfaceSignatureVertex {uint64_t id;float values[6];} SurfaceSignatureVertex;
static int CompareSignatureTriangles(const void* a,const void* b){
    const SurfaceSignatureTriangle* x=a;const SurfaceSignatureTriangle* y=b;
    for(unsigned j=0;j<3;++j)if(x->ids[j]!=y->ids[j])return x->ids[j]<y->ids[j]?-1:1;
    return 0;
}
static int CompareSignatureVertices(const void* a,const void* b){
    uint64_t x=((const SurfaceSignatureVertex*)a)->id,y=((const SurfaceSignatureVertex*)b)->id;
    return x==y?0:x<y?-1:1;
}
static uint64_t SurfaceSignatureHash(uint64_t hash,const void* data,size_t bytes){
    const unsigned char* p=data;for(size_t i=0;i<bytes;++i){hash^=p[i];hash*=1099511628211ULL;}return hash;
}
bool OpenGLRenderer_ExportGpuSurfaceSignature(Renderer3D* r,const char* path){
    size_t count=0;float (*v)[8]=NULL;if(!path||!ReadGpuSurfaceTriangles(r,&v,&count))return false;
    SurfaceSignatureTriangle* triangles=malloc((count/3)*sizeof(*triangles));
    SurfaceSignatureVertex* vertices=malloc(count*sizeof(*vertices));
    if(!triangles||!vertices){free(v);free(triangles);free(vertices);return false;}
    for(size_t i=0;i<count;++i){vertices[i].id=SurfaceAuditVertexId(v[i]);
        memcpy(vertices[i].values,v[i],3*sizeof(float));memcpy(vertices[i].values+3,v[i]+4,3*sizeof(float));}
    for(size_t i=0;i<count/3;++i){uint64_t ids[3]={vertices[3*i].id,vertices[3*i+1].id,vertices[3*i+2].id};
        unsigned first=ids[1]<ids[0]?1:0;if(ids[2]<ids[first])first=2;
        for(unsigned j=0;j<3;++j)triangles[i].ids[j]=ids[(first+j)%3];}
    qsort(triangles,count/3,sizeof(*triangles),CompareSignatureTriangles);
    qsort(vertices,count,sizeof(*vertices),CompareSignatureVertices);
    uint64_t th=1469598103934665603ULL,ph=th,nh=th;size_t unique=0,inconsistent=0;
    for(size_t i=0;i<count/3;++i)th=SurfaceSignatureHash(th,triangles[i].ids,sizeof(triangles[i].ids));
    for(size_t i=0;i<count;++i){if(i&&vertices[i].id==vertices[i-1].id){if(memcmp(vertices[i].values,vertices[i-1].values,sizeof(vertices[i].values)))++inconsistent;continue;}
        ++unique;ph=SurfaceSignatureHash(ph,&vertices[i].id,sizeof(vertices[i].id));ph=SurfaceSignatureHash(ph,vertices[i].values,3*sizeof(float));
        nh=SurfaceSignatureHash(nh,&vertices[i].id,sizeof(vertices[i].id));nh=SurfaceSignatureHash(nh,vertices[i].values+3,3*sizeof(float));
        vertices[unique-1]=vertices[i];}
    char binaryPath[1024];int pathLength=snprintf(binaryPath,sizeof(binaryPath),"%s.vertices.bin",path);
    bool binaryOK=false;
    if(pathLength>0&&(size_t)pathLength<sizeof(binaryPath)){
        FILE* binary=fopen(binaryPath,"wb");uint64_t vertexCount=unique;
        if(binary){binaryOK=fwrite(&vertexCount,sizeof(vertexCount),1,binary)==1&&fwrite(vertices,sizeof(*vertices),unique,binary)==unique;
            if(fclose(binary)!=0)binaryOK=false;}}
    FILE* out=fopen(path,"w");bool ok=false;
    if(out){fprintf(out,"{\"triangles\":%zu,\"unique_edges\":%zu,\"inconsistent_shared_values\":%zu,\"triangle_hash\":\"%016llx\",\"position_hash\":\"%016llx\",\"normal_hash\":\"%016llx\"}\n",
        count/3,unique,inconsistent,(unsigned long long)th,(unsigned long long)ph,(unsigned long long)nh);ok=fclose(out)==0;}
    free(v);free(triangles);free(vertices);return ok&&binaryOK&&OpenGLRenderer_CheckErrors();
}

bool OpenGLRenderer_ValidateComputeSDF(Renderer3D* r,const MonsterSDF* sdf,
    const Vector3* points,size_t count,float* error) {
    if(!OpenGLRenderer_SupportsComputeSurface(r)||!sdf||!points||!count||count>65536||!error)return false;
    OpenGLRendererData* d=r->user_data;
    if(!InitComputeSurface(d)||!UploadSDFScene(d,sdf,NULL,0,0))return false;
    float (*values)[4]=malloc(count*sizeof(*values));if(!values)return false;
    for(size_t i=0;i<count;++i) {values[i][0]=points[i].x;values[i][1]=points[i].y;values[i][2]=points[i].z;values[i][3]=NAN;}
    if(!d->compute.validation)glGenBuffers(1,&d->compute.validation);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.validation);
    glBufferData(GL_SHADER_STORAGE_BUFFER,count*sizeof(*values),values,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,6,d->compute.validation);
    BindSceneProgram(d,d->compute.validateProgram,sdf);
    glUniform1ui(glGetUniformLocation(d->compute.validateProgram,"validationCount"),(GLuint)count);
    glDispatchCompute((GLuint)((count+63)/64),1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    // Lectura reservada al diagnóstico explícito; nunca se usa en el render normal.
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.validation);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,count*sizeof(*values),values);
    *error=0;for(size_t i=0;i<count;++i) {
        float e=fabsf(values[i][3]-MonsterSDF_EvaluateVisualDistance(sdf,points[i]));
        if(!isfinite(e)){free(values);return false;}*error=fmaxf(*error,e);
    }
    free(values);glUseProgram(0);return OpenGLRenderer_CheckErrors();
}

bool OpenGLRenderer_ValidatePinnaIntervals(Renderer3D* r,const MonsterSDF* sdf,float* error) {
    if(!OpenGLRenderer_SupportsComputeSurface(r)||!sdf||!error)return false;
    *error=0;
    if(!sdf->mouthCount)return true;
    const MonsterSDFMouth* m=&sdf->mouths[0];OpenGLRendererData* d=r->user_data;
    if(!InitComputeSurface(d)||!UploadSDFScene(d,sdf,NULL,0,0))return false;
    if(!d->compute.intervalProgram)d->compute.intervalProgram=LinkComputeSource(sdfIntervalValidateSource,
        sizeof(sdfIntervalValidateSource)/sizeof(*sdfIntervalValidateSource));
    if(!d->compute.intervalProgram)return false;
    enum {COUNT=512};float probes[COUNT][12]={{0}};
    for(unsigned i=0;i<COUNT;++i) {
        float t=((i*97u)%509u)/508.f,x=((i*193u)%503u)/502.f,z=((i*251u)%499u)/498.f;
        Vector3 center=Vec3_Add(Vec3_Scale(m->earDirection,m->earShape.y*(1.4f*t-.7f)),
            Vec3_Add(Vec3_Scale(m->leftEarSide,m->earShape.x*(2.2f*x-1.1f)),Vec3_Scale(m->leftEarNormal,m->earShape.z*(4*z-2))));
        if(i%5==0)center=Vec3_Scale(m->earDirection,m->earShape.y*.5f);
        float radius=i%3==0?0:.0005f+(i%31)*.002f;Vector3 half=Vec3_Create(radius,radius,radius);
        Vector3 lo=Vec3_Sub(center,half),hi=Vec3_Add(center,half);
        probes[i][0]=lo.x;probes[i][1]=lo.y;probes[i][2]=lo.z;
        probes[i][4]=hi.x;probes[i][5]=hi.y;probes[i][6]=hi.z;
    }
    if(!d->compute.intervalValidation)glGenBuffers(1,&d->compute.intervalValidation);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->compute.intervalValidation);
    glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(probes),probes,GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,d->compute.intervalValidation);
    BindSceneProgram(d,d->compute.intervalProgram,sdf);
    glUniform1ui(glGetUniformLocation(d->compute.intervalProgram,"intervalProbeCount"),COUNT);
    glUniform1i(glGetUniformLocation(d->compute.intervalProgram,"intervalMouth"),d->scene.mouthBase);
    bool ok=true;*error=0;
    Vector3 rshape=m->earShape,a=Vec3_Create(.2f,-.3f,.1f),b=Vec3_Create(-.4f,.8f,-.5f);
    for(int recipe=0;recipe<8;++recipe) {
        if(recipe==7&&!sdf->connectorCount)continue;
        glUniform1i(glGetUniformLocation(d->compute.intervalProgram,"intervalRecipe"),recipe);
        glUniform1i(glGetUniformLocation(d->compute.intervalProgram,"intervalConnector"),d->scene.connectorBase);
        glDispatchCompute(COUNT/64,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(probes),probes);
        bool contained=true;float recipeError=0;
        for(unsigned i=0;i<COUNT;++i) {
            AABB3D box={Vec3_Create(probes[i][0],probes[i][1],probes[i][2]),Vec3_Create(probes[i][4],probes[i][5],probes[i][6])};
            SDFInterval cpu=SDF_CurvedPinnaInterval(box,rshape,m->earDirection,m->leftEarSide,m->leftEarNormal,
                m->earTipFraction,m->earConcavity,m->earLongitudinalCurve,m->earRootRoll,m->earTipRoundness,
                m->earFold,m->earRootFlare,m->earMarginBow,m->earMarginAsymmetry);
            if(recipe==1)cpu=SDF_EllipsoidInterval(box,rshape);
            if(recipe==2)cpu=SDF_CapsuleInterval(box,a,b,rshape.x);
            if(recipe==3)cpu=SDF_RoundedSlotInterval(box,rshape.x,rshape.y,rshape.z);
            if(recipe==4)cpu=SDF_SmoothUnionInterval(SDF_EllipsoidInterval(box,rshape),SDF_CapsuleInterval(box,a,b,rshape.x),.04f);
            if(recipe==5)cpu=SDF_SmoothSubtractInterval(SDF_EllipsoidInterval(box,rshape),SDF_RoundedSlotInterval(box,rshape.x,rshape.y,rshape.z),.04f);
            if(recipe==6)cpu=SDF_EllipticalSweepInterval(box,m->headStations,6);
            MonsterSDFConnector connector={0};MonsterSDF field={0};
            if(recipe==7) {
                connector=sdf->connectors[0];connector.isHeadNeck=false;
                field.connectors=&connector;field.connectorCount=1;
                cpu=SDF_ConnectorInterval(box,connector.a,connector.forward,connector.side,connector.up,connector.length,
                    connector.widthA,connector.widthB,connector.heightA,connector.heightB,connector.widthBulge,connector.heightBulge);
            }
            float lo=INFINITY,hi=-INFINITY;
            for(unsigned corner=0;corner<9;++corner) {
                Vector3 p=corner==8?Vec3_Scale(Vec3_Add(box.start,box.end),.5f):Vec3_Create(corner&1?box.end.x:box.start.x,
                    corner&2?box.end.y:box.start.y,corner&4?box.end.z:box.start.z);
                float v=SDF_CurvedPinna(p,rshape,m->earDirection,m->leftEarSide,m->leftEarNormal,
                    m->earTipFraction,m->earConcavity,m->earLongitudinalCurve,m->earRootRoll,m->earTipRoundness,
                    m->earFold,m->earRootFlare,m->earMarginBow,m->earMarginAsymmetry);
                if(recipe==1)v=SDF_Ellipsoid(p,rshape);
                if(recipe==2)v=SDF_Capsule(p,a,b,rshape.x);
                if(recipe==3)v=SDF_RoundedSlotExtruded(p,rshape.x,rshape.y,rshape.z);
                if(recipe==4)v=SDF_SmoothUnion(SDF_Ellipsoid(p,rshape),SDF_Capsule(p,a,b,rshape.x),.04f);
                if(recipe==5)v=SDF_SmoothSubtract(SDF_Ellipsoid(p,rshape),SDF_RoundedSlotExtruded(p,rshape.x,rshape.y,rshape.z),.04f);
                if(recipe==6)v=SDF_EllipticalSweepZ(p,m->headStations,6);
                if(recipe==7)v=MonsterSDF_EvaluateDistance(&field,p);
                lo=fminf(lo,v);hi=fmaxf(hi,v);
                if(!(v>=probes[i][8]&&v<=probes[i][9])) {
                    if(contained)fprintf(stderr,"Intervalo receta=%d caja=%u CPU=%g GPU=[%g,%g] escalares=[%g,%g]\n",recipe,i,v,probes[i][8],probes[i][9],probes[i][10],probes[i][11]);
                    contained=false;
                }
            }
            if(!(probes[i][10]>=cpu.lo&&probes[i][11]<=cpu.hi&&
                 probes[i][10]>=probes[i][8]&&probes[i][11]<=probes[i][9]))contained=false;
            recipeError=fmaxf(recipeError,fmaxf(fabsf(lo-probes[i][10]),fabsf(hi-probes[i][11])));
        }
        *error=fmaxf(*error,recipeError);ok=ok&&contained;
        printf("[INTERVALOS SDF] receta=%d cajas=%u muestras=%u error=%.9f %s\n",recipe,COUNT,COUNT*9,recipeError,contained?"PASS":"FAILED");
    }
    glUseProgram(0);return ok&&OpenGLRenderer_CheckErrors();
}

bool OpenGLRenderer_RenderSDFRegion(Renderer3D* renderer,const MonsterSDF* sdf,const ICamera* camera,int width,int height,AABB3D regionBounds) {
    if(!renderer||!renderer->user_data||!sdf||!camera||width<=0||height<=0)return false;
    if(regionBounds.start.x>=regionBounds.end.x||
       regionBounds.start.y>=regionBounds.end.y||
       regionBounds.start.z>=regionBounds.end.z)return true;
    OpenGLRendererData* d=renderer->user_data;
    float displayAspect=(float)width/height;
    if(d->sdfFrameActive){width=d->sdfWidth;height=d->sdfHeight;}
    if(!d->sdfProgram&&!InitSDFProgram(d))return false;
    Vector3 forward=Vec3_Normalize(Vec3_Sub(camera->target,camera->position));
    Vector3 right=Vec3_Normalize(Vec3_Cross(forward,camera->up)),up=Vec3_Cross(right,forward);
    float tangent=tanf(camera->fov*.00872664626f),aspect=displayAspect;
    if(!UploadSDFScene(d,sdf,d->reference.enabled?NULL:camera,width,height))return false;
    int partBase=d->scene.partBase,connectorBase=d->scene.connectorBase;
    int mouthBase=d->scene.mouthBase,axialBase=d->scene.axialBase;
    int tileDataBase=d->scene.tileDataBase,columns=d->scene.columns;
    bool cached=sdf->mouthCount==1&&M_STRIDE<=1024&&!d->reference.enabled&&getenv("MONSTER_SDF_NO_HEAD_CACHE")==NULL;
    if(cached&&!InitSDFCachedProgram(d))return false;
    SDFUniformLocations original=d->uniforms;
    if(cached)d->uniforms=d->cachedUniforms;
    GLuint p=cached?d->sdfCachedProgram:d->sdfProgram;glUseProgram(p);
    UploadHandoffs(d,p);
    glUniform1i(glGetUniformLocation(p,"disableGroupPruning"),getenv("MONSTER_SDF_NO_GROUP_PRUNING")!=NULL);
    glUniform1i(glGetUniformLocation(p,"referenceTrace"),d->reference.enabled);
    glUniform1i(glGetUniformLocation(p,"referenceSteps"),(GLint)d->reference.steps);
    glUniform1f(glGetUniformLocation(p,"referenceMaxStep"),d->reference.maxStep);
    float traceMin[16][3],traceMax[16][3];
    size_t traceCount=d->traceRegionCount?d->traceRegionCount:1;
    for(size_t i=0;i<traceCount;++i) {
        AABB3D box=d->traceRegionCount?d->traceRegions[i]:regionBounds;
        traceMin[i][0]=box.start.x;traceMin[i][1]=box.start.y;traceMin[i][2]=box.start.z;
        traceMax[i][0]=box.end.x;traceMax[i][1]=box.end.y;traceMax[i][2]=box.end.z;
    }
    glUniform1i(glGetUniformLocation(p,"traceRegionCount"),(GLint)traceCount);
    glUniform3fv(glGetUniformLocation(p,"traceRegionMin"),(GLsizei)traceCount,&traceMin[0][0]);
    glUniform3fv(glGetUniformLocation(p,"traceRegionMax"),(GLsizei)traceCount,&traceMax[0][0]);
#define UI(name,value) glUniform1i(d->uniforms.name,(GLint)(value))
#define UF(name,value) glUniform1f(d->uniforms.name,(float)(value))
    glUniform1i(glGetUniformLocation(p,"sweepBinarySearch"),d->sweepSorted&&getenv("MONSTER_SDF_LINEAR_SWEEP")==NULL);
    UI(validationMode,0);
    UI(sdfHasSurface,sdf->hasSurface);
    UI(sdfFurEnabled,!d->disableShells||!d->disableGuards);
    if(sdf->hasSurface) {
        glUniform4fv(d->uniforms.surfaceRecipe,SURFACE_RECIPE_ROWS,
            &sdf->surfaceRecipe.data[0][0]);
        glUniform1ui(d->uniforms.pigmentSeed,sdf->surfaceRecipe.pigmentSeed);
        glUniform1ui(d->uniforms.scaleSeed,sdf->surfaceRecipe.scaleSeed);
        glUniform1ui(d->uniforms.furSeed,sdf->surfaceRecipe.furSeed);
    }
    UI(sceneData,0);UI(partBase,partBase);UI(partCount,sdf->bodyPartCount);UI(connectorBase,connectorBase);UI(connectorCount,sdf->connectorCount);
    UI(mouthBase,mouthBase);UI(mouthCount,sdf->mouthCount);UI(axialBase,axialBase);UI(axialCount,sdf->axialStationCount);
    UF(bodySmoothness,sdf->config.bodySmoothness);
    UI(tileDataBase,tileDataBase);UI(tileColumns,columns);
    UniformVector(d->uniforms.cameraPosition,camera->position);UniformVector(d->uniforms.cameraForward,forward);UniformVector(d->uniforms.cameraRight,right);UniformVector(d->uniforms.cameraUp,up);
    /* La caja del llamador limita trabajo, sin perder su collar de influencia. */
    Vector3 bStart=regionBounds.start,bEnd=regionBounds.end;
    UniformVector(d->uniforms.boundsMin,bStart);UniformVector(d->uniforms.boundsMax,bEnd);
    glUniform2f(d->uniforms.viewportSize,(float)width,(float)height);
    UF(tanHalfFov,tanf(camera->fov*.00872664626f));UF(aspectRatio,displayAspect);
    UF(hitTolerance,.001f);
    GLfloat mv[16],projection[16],vp[16];glGetFloatv(GL_MODELVIEW_MATRIX,mv);glGetFloatv(GL_PROJECTION_MATRIX,projection);
    for(int c=0;c<4;++c)for(int r=0;r<4;++r){float sum=0;for(int k=0;k<4;++k)sum+=projection[k*4+r]*mv[c*4+k];vp[c*4+r]=sum;}
    glUniformMatrix4fv(d->uniforms.viewProjection,1,GL_FALSE,vp);
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
    if(d->reference.enabled) {
        // Bandas de framebuffer para acotar cada kernel gráfico del oráculo.
        glEnable(GL_SCISSOR_TEST);
        GLint columns=d->reference.maxStep>0?32:scW;
        for(GLint y=scY;y<scY+scH;y+=4) {
            GLsizei rows=scY+scH-y;if(rows>4)rows=4;
            for(GLint x=scX;x<scX+scW;x+=columns) {
                GLsizei cols=scX+scW-x;if(cols>columns)cols=columns;
                glScissor(x,y,cols,rows);
                glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
                glFlush();
            }
            // Sólo el oráculo de convergencia: drenar cada banda evita lotes largos.
            if(d->reference.maxStep>0)glFinish();
        }
        glDisable(GL_SCISSOR_TEST);
    } else {
        if(applyScissor){glEnable(GL_SCISSOR_TEST);glScissor(scX,scY,scW,scH);}
        glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
        if(applyScissor){glDisable(GL_SCISSOR_TEST);}
    }
    d->uniforms=original;
    glUseProgram(0);glBindTexture(GL_TEXTURE_BUFFER,0);glBindBuffer(GL_TEXTURE_BUFFER,0);
#undef UI
#undef UF
    return true;
}

bool OpenGLRenderer_RenderSDFRegions(Renderer3D* renderer,const MonsterSDF* sdf,
    const ICamera* camera,int width,int height,const AABB3D* boxes,size_t count) {
    if(!renderer||!renderer->user_data||!boxes||!count||count>16)return false;
    OpenGLRendererData* d=renderer->user_data;
    AABB3D bounds=AABB_Empty();
    for(size_t i=0;i<count;++i) {
        d->traceRegions[i]=boxes[i];
        AABB_ExpandPoint(&bounds,boxes[i].start);AABB_ExpandPoint(&bounds,boxes[i].end);
    }
    d->traceRegionCount=count;
    bool ok=OpenGLRenderer_RenderSDFRegion(renderer,sdf,camera,width,height,bounds);
    d->traceRegionCount=0;
    return ok;
}

bool OpenGLRenderer_RenderSDF(Renderer3D* renderer,const MonsterSDF* sdf,const ICamera* camera,int width,int height) {
    if(!sdf)return false;
    return OpenGLRenderer_RenderSDFRegion(renderer,sdf,camera,width,height,sdf->bounds);
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
    if(demoFramebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,demoFramebuffer);
        glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,demoFramebuffer);glViewport(0,0,width,height);d->sdfFrameActive=false;
}
float OpenGLRenderer_GetSDFGpuMs(const Renderer3D* renderer){return renderer&&renderer->user_data?((OpenGLRendererData*)renderer->user_data)->gpuMs:0;}
float OpenGLRenderer_GetSDFResolutionScale(const Renderer3D* renderer){return renderer&&renderer->user_data?((OpenGLRendererData*)renderer->user_data)->resolutionScale:0;}

bool OpenGLRenderer_ValidateSDF(Renderer3D* renderer,const MonsterSDF* sdf,const Vector3* points,size_t count,float* maxError) {
    if(!renderer||!renderer->user_data||!sdf||!points||count==0||count>4096||!maxError)return false;
    OpenGLRendererData* d=renderer->user_data;
    if(!d->sdfProgram&&!InitSDFProgram(d))return false;
    if(!UploadSDFScene(d,sdf,NULL,0,0))return false;
    BindSceneProgram(d,d->sdfProgram,sdf);
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
struct OpenGLDemoWindow { SDL_Window* window; SDL_GLContext context; GLuint framebuffer,color,depth; };
OpenGLDemoWindow* OpenGLDemoWindow_Create(const char* title,int width,int height) {
    return OpenGLDemoWindow_CreateMSAA(title,width,height,0);
}
/** @brief Decide una vez antes de inicializar SDL/GL; respeta la selección explícita. */
static bool automaticGPUFailed=false;
static bool SelectPlatformGPU(void) {
    if(automaticGPUFailed)return false;
#ifdef __linux__
    const char* mode=getenv("MONSTER_GPU");
    if((mode&&strcmp(mode,"auto")) || getenv("DRI_PRIME") || getenv("__NV_PRIME_RENDER_OFFLOAD") ||
       getenv("__GLX_VENDOR_LIBRARY_NAME") || getenv("__NV_PRIME_RENDER_OFFLOAD_PROVIDER"))return false;
    GpuDeviceCapability devices[32];char selectors[32][64];bool nvidia[32];
    size_t count=0,defaultIndex=SIZE_MAX;
    for(unsigned card=0;card<256&&count<32;++card) {
        char base[128],path[192],driver[256],pci[32];unsigned vendor=0,boot=0;unsigned long long bytes=0;
        snprintf(base,sizeof(base),"/sys/class/drm/card%u/device",card);
        snprintf(path,sizeof(path),"%s/vendor",base);FILE* file=fopen(path,"r");
        if(!file)continue;
        int valid=fscanf(file,"%x",&vendor);fclose(file);if(valid!=1)continue;
        snprintf(path,sizeof(path),"%s/boot_vga",base);file=fopen(path,"r");
        if(file){valid=fscanf(file,"%u",&boot);fclose(file);if(valid!=1)boot=0;}
        snprintf(path,sizeof(path),"%s/mem_info_vram_total",base);file=fopen(path,"r");
        if(file){valid=fscanf(file,"%llu",&bytes);fclose(file);if(valid!=1)bytes=0;}
        snprintf(path,sizeof(path),"%s/driver",base);ssize_t driverLength=readlink(path,driver,sizeof(driver)-1);
        if(driverLength<0)continue;
        driver[driverLength]=0;const char* driverName=strrchr(driver,'/');driverName=driverName?driverName+1:driver;
        nvidia[count]=vendor==0x10de&&!strcmp(driverName,"nvidia");
        /* El controlador NVIDIA declara memoria dedicada. Para Mesa, una GPU
         * PCI secundaria o VRAM superior a la reserva UMA habitual es candidata.
         * Es una preferencia de plataforma, no un benchmark entre modelos. */
        devices[count]=(GpuDeviceCapability){true,nvidia[count]||!boot||bytes>(1ull<<30),bytes};
        snprintf(path,sizeof(path),"%s/uevent",base);file=fopen(path,"r");bool found=false;
        if(file) {
            char line[256];
            while(fgets(line,sizeof(line),file))if(sscanf(line,"PCI_SLOT_NAME=%31s",pci)==1){found=true;break;}
            fclose(file);
        }
        if(!found)continue;
        for(char* c=pci;*c;++c)if(*c==':'||*c=='.')*c='_';
        snprintf(selectors[count],sizeof(selectors[count]),"pci-%s",pci);
        if(boot)defaultIndex=count;
        ++count;
    }
    size_t chosen=GpuDevicePolicy_Select(devices,count,defaultIndex);
    if(chosen==SIZE_MAX||chosen==defaultIndex||!devices[chosen].dedicated)return false;
    if(nvidia[chosen]) {
        SDL_setenv("__NV_PRIME_RENDER_OFFLOAD","1",1);
        SDL_setenv("__GLX_VENDOR_LIBRARY_NAME","nvidia",1);
    } else SDL_setenv("DRI_PRIME",selectors[chosen],1);
    printf("[GPU selección] Adaptador dedicado %s solicitado al arrancar.\n",selectors[chosen]);
    return true;
#else
    return false;
#endif
}
static void ResetAutomaticGPU(void) {
#ifdef __linux__
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");unsetenv("__GLX_VENDOR_LIBRARY_NAME");unsetenv("DRI_PRIME");
#endif
}
OpenGLDemoWindow* OpenGLDemoWindow_CreateMSAA(const char* title,int width,int height,int samples) {
    bool automatic=SelectPlatformGPU();
    if(SDL_Init(SDL_INIT_VIDEO)<0) {
        if(!automatic)return NULL;
        SDL_Quit();ResetAutomaticGPU();automaticGPUFailed=true;
        fprintf(stderr,"[GPU selección] No se pudo iniciar la dedicada; usando adaptador predeterminado.\n");
        if(SDL_Init(SDL_INIT_VIDEO)<0)return NULL;
        automatic=false;
    }
    OpenGLDemoWindow* w=calloc(1,sizeof(*w));
    if(!w) { SDL_Quit(); return NULL; }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,samples>0?1:0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,samples>0?samples:0);
    w->window=SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    if(w->window)w->context=SDL_GL_CreateContext(w->window);
    if(!w->context && samples>0) {
        fprintf(stderr,"[GPU contexto] MSAA=%d no disponible: %s\n",samples,SDL_GetError());
        if(w->window)SDL_DestroyWindow(w->window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);
        w->window=SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
        if(w->window)w->context=SDL_GL_CreateContext(w->window);
    }
    if(!w->context&&automatic) {
        /* El fallback es de contexto, previo a cargar recursos; nunca migra
         * geometría ni repite pruebas de adaptadores durante la simulación. */
        if(w->window)SDL_DestroyWindow(w->window);
        free(w);SDL_Quit();ResetAutomaticGPU();automaticGPUFailed=true;
        fprintf(stderr,"[GPU selección] Falló el contexto dedicado; usando adaptador predeterminado.\n");
        return OpenGLDemoWindow_CreateMSAA(title,width,height,samples);
    }
    if(!w->context) { OpenGLDemoWindow_Free(w); return NULL; }
    int actual=0;glGetIntegerv(GL_SAMPLES,&actual);
    if(samples>actual) {
        GLint maximum=0;glGetIntegerv(GL_MAX_SAMPLES,&maximum);
        if(samples<=maximum) {
            glGenFramebuffers(1,&w->framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,w->framebuffer);
            glGenRenderbuffers(1,&w->color);glBindRenderbuffer(GL_RENDERBUFFER,w->color);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,width,height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,w->color);
            glGenRenderbuffers(1,&w->depth);glBindRenderbuffer(GL_RENDERBUFFER,w->depth);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH_COMPONENT24,width,height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,w->depth);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {OpenGLDemoWindow_Free(w);return NULL;}
            demoFramebuffer=w->framebuffer;demoWidth=width;demoHeight=height;
            glGetIntegerv(GL_SAMPLES,&actual);glEnable(GL_MULTISAMPLE);
        }
    }
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
            if(e.key.keysym.sym==SDLK_t)input.keyT=true;
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
void OpenGLDemoWindow_Swap(OpenGLDemoWindow* w) { if(w){ResolveDemoFramebuffer();SDL_GL_SwapWindow(w->window);} }
void OpenGLDemoWindow_Free(OpenGLDemoWindow* w) {
    if(!w)return;
    if(w->framebuffer==demoFramebuffer)demoFramebuffer=0;
    if(w->framebuffer)glDeleteFramebuffers(1,&w->framebuffer);
    if(w->color)glDeleteRenderbuffers(1,&w->color);
    if(w->depth)glDeleteRenderbuffers(1,&w->depth);
    if(w->context)SDL_GL_DeleteContext(w->context);
    if(w->window)SDL_DestroyWindow(w->window);
    free(w); SDL_Quit();
}
void OpenGLDemoWindow_SetTitle(OpenGLDemoWindow* w, const char* title) {
    if (w && w->window && title) SDL_SetWindowTitle(w->window, title);
}
double OpenGLDemoWindow_Time(void) { return (double)SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency(); }
double OpenGLDemoWindow_LimitFPS(double frameStart,double maxFPS) {
    if(!isfinite(frameStart)||!isfinite(maxFPS)||maxFPS<=0)return 0;
    double start=OpenGLDemoWindow_Time(),deadline=frameStart+1/maxFPS;
    double remaining;
    while((remaining=deadline-OpenGLDemoWindow_Time())>0) {
        /* Redondear hacia arriba garantiza el límite sin consumir CPU girando. */
        SDL_Delay((Uint32)fmin(ceil(remaining*1000),1000));
    }
    return OpenGLDemoWindow_Time()-start;
}
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

static void DrawStrokeSegment(float x0, float y0, float x1, float y1) {
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
}

static void DrawStrokeChar(char c, float x, float y, float w, float h) {
    float x1 = x + w, y1 = y + h, ym = y + h * 0.5f;
    switch (c) {
        case '0':
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x1, y, x1, y1);
            DrawStrokeSegment(x1, y1, x, y1);
            DrawStrokeSegment(x, y1, x, y);
            DrawStrokeSegment(x, y1, x1, y);
            break;
        case '1':
            DrawStrokeSegment(x + w * 0.6f, y, x + w * 0.6f, y1);
            DrawStrokeSegment(x + w * 0.2f, y + h * 0.25f, x + w * 0.6f, y);
            break;
        case '2':
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x1, y, x1, ym);
            DrawStrokeSegment(x1, ym, x, ym);
            DrawStrokeSegment(x, ym, x, y1);
            DrawStrokeSegment(x, y1, x1, y1);
            break;
        case '3':
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x1, y, x1, y1);
            DrawStrokeSegment(x, ym, x1, ym);
            DrawStrokeSegment(x, y1, x1, y1);
            break;
        case '4':
            DrawStrokeSegment(x, y, x, ym);
            DrawStrokeSegment(x, ym, x1, ym);
            DrawStrokeSegment(x1, y, x1, y1);
            break;
        case '5':
            DrawStrokeSegment(x1, y, x, y);
            DrawStrokeSegment(x, y, x, ym);
            DrawStrokeSegment(x, ym, x1, ym);
            DrawStrokeSegment(x1, ym, x1, y1);
            DrawStrokeSegment(x1, y1, x, y1);
            break;
        case '6':
            DrawStrokeSegment(x1, y, x, y);
            DrawStrokeSegment(x, y, x, y1);
            DrawStrokeSegment(x, y1, x1, y1);
            DrawStrokeSegment(x1, y1, x1, ym);
            DrawStrokeSegment(x1, ym, x, ym);
            break;
        case '7':
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x1, y, x + w * 0.25f, y1);
            break;
        case '8':
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x, ym, x1, ym);
            DrawStrokeSegment(x, y1, x1, y1);
            DrawStrokeSegment(x, y, x, y1);
            DrawStrokeSegment(x1, y, x1, y1);
            break;
        case '9':
            DrawStrokeSegment(x1, ym, x, ym);
            DrawStrokeSegment(x, y, x, ym);
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x1, y, x1, y1);
            DrawStrokeSegment(x1, y1, x, y1);
            break;
        case 'F':
            DrawStrokeSegment(x, y, x, y1);
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x, ym, x + w * 0.7f, ym);
            break;
        case 'P':
            DrawStrokeSegment(x, y, x, y1);
            DrawStrokeSegment(x, y, x1, y);
            DrawStrokeSegment(x1, y, x1, ym);
            DrawStrokeSegment(x1, ym, x, ym);
            break;
        case 'S':
            DrawStrokeSegment(x1, y, x, y);
            DrawStrokeSegment(x, y, x, ym);
            DrawStrokeSegment(x, ym, x1, ym);
            DrawStrokeSegment(x1, ym, x1, y1);
            DrawStrokeSegment(x1, y1, x, y1);
            break;
        case ':':
            DrawStrokeSegment(x + w * 0.5f, y + h * 0.3f, x + w * 0.5f, y + h * 0.35f);
            DrawStrokeSegment(x + w * 0.5f, y + h * 0.7f, x + w * 0.5f, y + h * 0.75f);
            break;
        case '.':
            DrawStrokeSegment(x + w * 0.4f, y1, x + w * 0.6f, y1);
            break;
        case 'm':
            DrawStrokeSegment(x, ym, x, y1);
            DrawStrokeSegment(x, ym, x + w * 0.5f, ym);
            DrawStrokeSegment(x + w * 0.5f, ym, x + w * 0.5f, y1);
            DrawStrokeSegment(x + w * 0.5f, ym, x1, ym);
            DrawStrokeSegment(x1, ym, x1, y1);
            break;
        case 's':
            DrawStrokeSegment(x1, ym, x, ym);
            DrawStrokeSegment(x, ym, x, y + h * 0.75f);
            DrawStrokeSegment(x, y + h * 0.75f, x1, y + h * 0.75f);
            DrawStrokeSegment(x1, y + h * 0.75f, x1, y1);
            DrawStrokeSegment(x1, y1, x, y1);
            break;
        default:
            break;
    }
}

static void DrawStrokeString(const char* str, float x, float y, float w, float h, float spacing) {
    glBegin(GL_LINES);
    while (*str) {
        if (*str != ' ') {
            DrawStrokeChar(*str, x, y, w, h);
        }
        x += w + spacing;
        str++;
    }
    glEnd();
}

void OpenGLRenderer_DrawHUD(int width, int height, float fps, float frameTimeMs) {
    if (width <= 0 || height <= 0) return;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float padX = 18.0f, padY = 18.0f;
    float boxW = 195.0f, boxH = 36.0f;

    /* Fondo oscuro traslúcido */
    glColor4ub(18, 22, 30, 215);
    glBegin(GL_QUADS);
    glVertex2f(padX, padY);
    glVertex2f(padX + boxW, padY);
    glVertex2f(padX + boxW, padY + boxH);
    glVertex2f(padX, padY + boxH);
    glEnd();

    /* Borde sutil del panel */
    glLineWidth(1.5f);
    glColor4ub(62, 70, 86, 230);
    glBegin(GL_LINE_LOOP);
    glVertex2f(padX, padY);
    glVertex2f(padX + boxW, padY);
    glVertex2f(padX + boxW, padY + boxH);
    glVertex2f(padX, padY + boxH);
    glEnd();

    /* Indicador LED circular */
    if (fps >= 50.0f) glColor4ub(40, 220, 110, 255);
    else if (fps >= 28.0f) glColor4ub(240, 190, 40, 255);
    else glColor4ub(240, 60, 60, 255);

    float ledX = padX + 16.0f, ledY = padY + boxH * 0.5f, ledR = 4.5f;
    glBegin(GL_POLYGON);
    for (int a = 0; a < 8; ++a) {
        float rad = (float)a * 0.785398f;
        glVertex2f(ledX + cosf(rad) * ledR, ledY + sinf(rad) * ledR);
    }
    glEnd();

    /* Texto vectorial "FPS: XX" y "X.X ms" */
    char fpsStr[32], msStr[32];
    snprintf(fpsStr, sizeof(fpsStr), "FPS: %.0f", fps > 0.0f ? fps : 0.0f);
    snprintf(msStr, sizeof(msStr), "%.1f ms", frameTimeMs > 0.0f ? frameTimeMs : 0.0f);

    glLineWidth(2.0f);
    glColor4ub(240, 245, 255, 255);
    DrawStrokeString(fpsStr, padX + 32.0f, padY + 10.0f, 7.5f, 15.0f, 3.5f);

    glColor4ub(160, 175, 195, 240);
    DrawStrokeString(msStr, padX + 124.0f, padY + 12.0f, 5.5f, 12.0f, 2.5f);

    glPopAttrib();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

#include "MonsterSurfaceShader.generated.h"
_Static_assert(SURFACE_RECIPE_ROWS==82,"Actualizar contrato GLSL de superficie al cambiar regiones o bancos de pigmento");
_Static_assert(sizeof(GeometryGpuVertex)==32&&sizeof(AnatomySkinBinding)==64&&sizeof(int)==4,
    "Conservar el contrato std430 del skinning y las texturas de pelaje");
static bool InitMeshSkinProgram(OpenGLRendererData* d) {
    if(d->skin.program)return true;
    d->skin.program=LinkComputeSource(meshSkinComputeSource,sizeof(meshSkinComputeSource)/sizeof(*meshSkinComputeSource));
    if(!d->skin.program)return false;
    d->skin.countLocation=glGetUniformLocation(d->skin.program,"vertexCount");return true;
}
bool OpenGLRenderer_SetMeshSkinning(Renderer3D* renderer,const Mesh* mesh,const AnatomyDeformer* binding,
    const Skeleton* skeleton,const SkeletonPose* pose,uint64_t generation) {
    if(!renderer||!renderer->user_data)return false;
    OpenGLRendererData* d=renderer->user_data;d->skin.mesh=NULL;
    if(d->skin.failed)return false;
    if(!mesh)return true;
    if(!binding||!mesh->hasSurface||mesh->vertexCount!=binding->vertexCount||!mesh->vertexCount||mesh->vertexCount>UINT32_MAX||
       !binding->basePositions||!binding->baseNormals||!binding->skinBindings||
       !OpenGLRenderer_SupportsComputeSurface(renderer)||!InitMeshSkinProgram(d)||
       !AnatomyDeformer_BuildPosePalette(binding,skeleton,pose,&d->skin.palette))return false;
    d->skin.mesh=mesh;d->skin.binding=binding;d->skin.generation=generation;return true;
}
static bool DeformMeshGPU(OpenGLRendererData* d,GpuMesh* cache,const Mesh* mesh) {
    const AnatomyDeformer* binding=d->skin.binding;
    if(cache->skinOwner!=binding||cache->skinBindingGeneration!=d->skin.generation||
       cache->skinGeometryGeneration!=mesh->geometryGeneration) {
        GLint64 maximum=0;glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&maximum);
        if(mesh->vertexCount>(size_t)maximum/sizeof(AnatomySkinBinding))return false;
        if(mesh->vertexCount>d->geometryScratchCapacity) {
            GeometryGpuVertex* rows=realloc(d->geometryScratch,mesh->vertexCount*sizeof(*rows));if(!rows)return false;
            d->geometryScratch=rows;d->geometryScratchCapacity=mesh->vertexCount;
        }
        for(size_t v=0;v<mesh->vertexCount;++v) {
            const AnatomySkinBinding* skin=&binding->skinBindings[v];
            for(int k=0;k<2*ANATOMY_MORPH_INFLUENCES;++k) {
                if(skin->weights[k]<=0)break;
                if(!isfinite(skin->weights[k])||skin->joints[k]<0||(size_t)skin->joints[k]>=d->skin.palette.jointCount)return false;
            }
            GeometryGpuVertex* row=&d->geometryScratch[v];Vector3 p=binding->basePositions[v],n=binding->baseNormals[v];
            row->position[0]=p.x;row->position[1]=p.y;row->position[2]=p.z;
            row->normal[0]=n.x;row->normal[1]=n.y;row->normal[2]=n.z;
            row->color[0]=mesh->vertices[v].color.r;row->color[1]=mesh->vertices[v].color.g;
            row->color[2]=mesh->vertices[v].color.b;row->color[3]=mesh->vertices[v].color.a;row->padding=0;
        }
        if(!cache->skinBase)glGenBuffers(1,&cache->skinBase);
        if(!cache->skinWeights)glGenBuffers(1,&cache->skinWeights);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,cache->skinBase);
        glBufferData(GL_SHADER_STORAGE_BUFFER,mesh->vertexCount*sizeof(GeometryGpuVertex),d->geometryScratch,GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,cache->skinWeights);
        glBufferData(GL_SHADER_STORAGE_BUFFER,mesh->vertexCount*sizeof(AnatomySkinBinding),binding->skinBindings,GL_STATIC_DRAW);
        cache->skinOwner=binding;cache->skinBindingGeneration=d->skin.generation;cache->skinGeometryGeneration=mesh->geometryGeneration;
        cache->rootsValid=false;
        d->meshStats.totalBytesUploaded+=mesh->vertexCount*(sizeof(GeometryGpuVertex)+sizeof(AnatomySkinBinding));
        d->meshStats.uploadCount+=2;
    }
    if(!d->skin.paletteBuffer)glGenBuffers(1,&d->skin.paletteBuffer);
    size_t bytes=d->skin.palette.jointCount*sizeof(d->skin.palette.rows[0]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,d->skin.paletteBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,d->skin.palette.rows,GL_STREAM_DRAW);
    ++d->meshStats.uploadCount;d->meshStats.totalBytesUploaded+=bytes;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,cache->skinBase);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,cache->skinWeights);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,d->skin.paletteBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,cache->geometryVbo);
    glUseProgram(d->skin.program);glUniform1ui(d->skin.countLocation,(GLuint)mesh->vertexCount);
    GLuint groups=(GLuint)((mesh->vertexCount+127)/128);
    /* Ordenar las lecturas del dibujo anterior antes de sobrescribir la pose. */
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glDispatchCompute(groups>65535?65535:groups,(groups+65534)/65535,1);
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
    cache->gpuPosed=true;
    return OpenGLRenderer_CheckErrors();
}
bool OpenGLRenderer_ValidateMeshSkinning(Renderer3D* renderer,const Mesh* mesh,const AnatomyDeformer* binding,
    const Skeleton* skeleton,const SkeletonPose* pose,float* positionError,float* normalError) {
    if(!renderer||!renderer->user_data||!mesh||!binding||mesh->vertexCount!=binding->vertexCount||!positionError||!normalError)return false;
    OpenGLRendererData* d=renderer->user_data;GpuMesh* cached=NULL;
    for(unsigned i=0;i<GPU_MESH_CACHE_CAPACITY;++i)
        if(d->meshCache[i].owner==mesh&&d->meshCache[i].identity==mesh->identity){cached=&d->meshCache[i];break;}
    AnatomyPosePalette palette;
    if(!cached||cached->skinOwner!=binding||!AnatomyDeformer_BuildPosePalette(binding,skeleton,pose,&palette))return false;
    GeometryGpuVertex* vertices=malloc(mesh->vertexCount*sizeof(*vertices));if(!vertices)return false;
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER,cached->geometryVbo);
    glGetBufferSubData(GL_COPY_READ_BUFFER,0,mesh->vertexCount*sizeof(*vertices),vertices);
    *positionError=*normalError=0;
    for(size_t v=0;v<mesh->vertexCount;++v) {
        Vector3 p,n;
        if(!AnatomyDeformer_EvaluatePoseVertex(binding,&palette,v,&p,&n)){free(vertices);return false;}
        Vector3 gp={vertices[v].position[0],vertices[v].position[1],vertices[v].position[2]};
        Vector3 gn={vertices[v].normal[0],vertices[v].normal[1],vertices[v].normal[2]};
        float ep=Vec3_Distance(p,gp),en=Vec3_Distance(n,gn);
        if(!isfinite(ep)||!isfinite(en)){free(vertices);return false;}
        *positionError=fmaxf(*positionError,ep);*normalError=fmaxf(*normalError,en);
    }
    free(vertices);return OpenGLRenderer_CheckErrors();
}

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
    if(d->skin.mesh==mesh) {
        /* La distribución inicial conserva las mismas áreas de la pose CPU.
         * Sólo se evalúa al crear raíces; las poses siguientes viven en GPU. */
        Mesh posed=*mesh;posed.vertices=malloc(mesh->vertexCount*sizeof(*posed.vertices));
        if(!posed.vertices)return false;
        memcpy(posed.vertices,mesh->vertices,mesh->vertexCount*sizeof(*posed.vertices));
        for(size_t v=0;v<mesh->vertexCount;++v)
            if(!AnatomyDeformer_EvaluatePoseVertex(d->skin.binding,&d->skin.palette,v,
                &posed.vertices[v].position,&posed.vertices[v].normal)){free(posed.vertices);return false;}
        bool ok=PrepareRoots(d,cache,&posed,changed);free(posed.vertices);return ok;
    }
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
    UploadHandoffs(d,d->guardProgram);
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
        cached->vertexPointer=NULL;cached->indexPointer=NULL;cached->rootsValid=false;cached->skinOwner=NULL;cached->gpuPosed=false;
    }
    cached->lastUse=d->meshFrame;
    bool indicesChanged=cached->indexPointer!=mesh->indices || cached->indexCount!=mesh->indexCount;
    bool geometryChanged=cached->geometryGeneration!=mesh->geometryGeneration || cached->vertexPointer!=mesh->vertices ||
        cached->vertexCount!=mesh->vertexCount || indicesChanged || (cached->gpuPosed&&d->skin.mesh!=mesh);
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
        cached->gpuPosed=false;
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
    if(d->skin.mesh==mesh&&!DeformMeshGPU(d,cached,mesh))return false;
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
    UploadHandoffs(d,d->surfaceProgram);

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
            if(d->skin.mesh==mesh&&!AnatomyDeformer_EvaluatePoseVertex(d->skin.binding,&d->skin.palette,i,&p,&n))return false;
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
    if(r && r->user_data)((OpenGLRendererData*)r->user_data)->surfaceDebug=mode>=0&&mode<=28?mode:0;
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
