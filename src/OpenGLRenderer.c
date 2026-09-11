#define GL_GLEXT_PROTOTYPES
#include "OpenGLRenderer.h"
#include "MonsterSDF.h"
#include <string.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ============================================================
 * RENDER STATE DATA
 * ============================================================ */

typedef struct OpenGLRendererData {
    bool wireframe;
    GLuint sdfProgram, sdfBuffer, sdfTexture;
    float (*sdfData)[4];
    size_t sdfCapacity,sdfCount;
    int* tileIndices;
    size_t tileCapacity;
    GLuint sdfFramebuffer,sdfColor,sdfDepth,sdfQueries[4];
    unsigned queryFrame;
    bool queryPending[4], sdfFrameActive, queryIssued;
    int sdfWidth,sdfHeight;
    float resolutionScale,gpuMs;
} OpenGLRendererData;

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
    (void)self;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void OpenGL_EndFrame(Renderer3D* self) {
    (void)self;
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

    OpenGLRenderer_RenderMesh(mesh);

    if (wireframe) {
        glPolygonMode(GL_FRONT, previousPolygonMode[0]);
        glPolygonMode(GL_BACK, previousPolygonMode[1]);
    }
}

void OpenGLRenderer_Destroy(Renderer3D* renderer) {
    if (!renderer) return;

    if (renderer->user_data) {
        OpenGLRendererData* data=renderer->user_data;
        if(data->sdfProgram)glDeleteProgram(data->sdfProgram);
        if(data->sdfBuffer)glDeleteBuffers(1,&data->sdfBuffer);
        if(data->sdfTexture)glDeleteTextures(1,&data->sdfTexture);
        if(data->sdfFramebuffer)glDeleteFramebuffers(1,&data->sdfFramebuffer);
        if(data->sdfColor)glDeleteTextures(1,&data->sdfColor);
        if(data->sdfDepth)glDeleteRenderbuffers(1,&data->sdfDepth);
        if(data->sdfQueries[0])glDeleteQueries(4,data->sdfQueries);
        free(data->sdfData);
        free(data->tileIndices);
        free(renderer->user_data);
        renderer->user_data = NULL;
    }
    renderer->beginFrame = NULL;
    renderer->endFrame = NULL;
    renderer->renderMesh = NULL;
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

static GLuint CompileSDFShader(GLenum type,const char* const* source,size_t count) {
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,(GLsizei)count,source,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"[SDF GPU] %s\n",log);glDeleteShader(shader);return 0;}
    return shader;
}
static bool InitSDFProgram(OpenGLRendererData* d) {
    const char* vertex="#version 330 compatibility\nvoid main(){gl_Position=gl_Vertex;}\n";
    GLuint vs=CompileSDFShader(GL_VERTEX_SHADER,&vertex,1);
    const size_t sourceCount=sizeof(sdfFragmentSource)/sizeof(*sdfFragmentSource);
    GLuint fs=CompileSDFShader(GL_FRAGMENT_SHADER,sdfFragmentSource,sourceCount);
    if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"[SDF GPU] %s\n",log);glDeleteProgram(p);return false;}
    d->sdfProgram=p;
    if(!d->sdfBuffer)glGenBuffers(1,&d->sdfBuffer);
    if(!d->sdfTexture)glGenTextures(1,&d->sdfTexture);
    fprintf(stdout,"[SDF GPU] %s | %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    return true;
}
static void UniformVector(GLuint p,const char* name,Vector3 v){glUniform3f(glGetUniformLocation(p,name),v.x,v.y,v.z);}

bool OpenGLRenderer_RenderSDF(Renderer3D* renderer,const MonsterSDF* sdf,const ICamera* camera,int width,int height) {
    if(!renderer||!renderer->user_data||!sdf||!camera||width<=0||height<=0)return false;
    OpenGLRendererData* d=renderer->user_data;
    if(d->sdfFrameActive){width=d->sdfWidth;height=d->sdfHeight;}
    if(!d->sdfProgram&&!InitSDFProgram(d))return false;
    Vector3 forward=Vec3_Normalize(Vec3_Sub(camera->target,camera->position));
    Vector3 right=Vec3_Normalize(Vec3_Cross(forward,camera->up)),up=Vec3_Cross(right,forward);
    float tangent=tanf(camera->fov*.00872664626f),aspect=(float)width/height;
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
    for(size_t i=0;i<sdf->mouthCount;++i){PackM(d->sdfData+offset,&sdf->mouths[i]);offset+=M_STRIDE;}
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
#define UI(name,value) glUniform1i(glGetUniformLocation(p,name),(GLint)(value))
#define UF(name,value) glUniform1f(glGetUniformLocation(p,name),(float)(value))
    UI("validationMode",0);
    UI("sceneData",0);UI("partBase",partBase);UI("partCount",sdf->bodyPartCount);UI("connectorBase",connectorBase);UI("connectorCount",sdf->connectorCount);
    UI("mouthBase",mouthBase);UI("mouthCount",sdf->mouthCount);UI("axialBase",axialBase);UI("axialCount",sdf->axialStationCount);
    UF("bodySmoothness",sdf->config.bodySmoothness);
    UI("tileDataBase",tileDataBase);UI("tileColumns",columns);
    UniformVector(p,"cameraPosition",camera->position);UniformVector(p,"cameraForward",forward);UniformVector(p,"cameraRight",right);UniformVector(p,"cameraUp",up);
    UniformVector(p,"boundsMin",sdf->bounds.start);UniformVector(p,"boundsMax",sdf->bounds.end);
    glUniform2f(glGetUniformLocation(p,"viewportSize"),(float)width,(float)height);
    UF("tanHalfFov",tanf(camera->fov*.00872664626f));UF("aspectRatio",(float)width/(float)height);
    UF("hitTolerance",.001f);
    GLfloat mv[16],projection[16],vp[16];glGetFloatv(GL_MODELVIEW_MATRIX,mv);glGetFloatv(GL_PROJECTION_MATRIX,projection);
    for(int c=0;c<4;++c)for(int r=0;r<4;++r){float sum=0;for(int k=0;k<4;++k)sum+=projection[k*4+r]*mv[c*4+k];vp[c*4+r]=sum;}
    glUniformMatrix4fv(glGetUniformLocation(p,"viewProjection"),1,GL_FALSE,vp);
    glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
    glUseProgram(0);glBindTexture(GL_TEXTURE_BUFFER,0);glBindBuffer(GL_TEXTURE_BUFFER,0);
#undef UI
#undef UF
    return true;
}
void OpenGLRenderer_Finish(void){glFinish();}

bool OpenGLRenderer_BeginSDFFrame(Renderer3D* renderer,ICamera* camera,int width,int height,bool adaptive) {
    if(!renderer||!renderer->user_data||!camera||width<=0||height<=0)return false;
    OpenGLRendererData* d=renderer->user_data;
    /* Compilar antes de iniciar la consulta: el arranque no es coste de reproducción. */
    if(!d->sdfProgram&&!InitSDFProgram(d))return false;
    if(!d->sdfQueries[0])glGenQueries(4,d->sdfQueries);
    for(unsigned i=0;i<4;++i)if(d->queryPending[i]) {
        GLint ready=0;glGetQueryObjectiv(d->sdfQueries[i],GL_QUERY_RESULT_AVAILABLE,&ready);
        if(ready){GLuint64 ns=0;glGetQueryObjectui64v(d->sdfQueries[i],GL_QUERY_RESULT,&ns);d->gpuMs=(float)((double)ns/1e6);d->queryPending[i]=false;}
    }
    if(d->resolutionScale<=0)d->resolutionScale=adaptive?.65f:1.f;
    if(!adaptive)d->resolutionScale=1.f;
    else if(d->gpuMs>0 && d->queryFrame%8==0) {
        float target=d->resolutionScale*sqrtf(8.f/d->gpuMs);
        target=fmaxf(.30f,fminf(1.f,target));
        /* Histéresis evita recrear buffers por pequeñas variaciones de carga. */
        if(target<d->resolutionScale*.94f||target>d->resolutionScale*1.12f)d->resolutionScale=target;
    }
    int rw=(int)ceilf(width*d->resolutionScale),rh=(int)ceilf(height*d->resolutionScale);
    if(!d->sdfFramebuffer){glGenFramebuffers(1,&d->sdfFramebuffer);glGenTextures(1,&d->sdfColor);glGenRenderbuffers(1,&d->sdfDepth);}
    glBindFramebuffer(GL_FRAMEBUFFER,d->sdfFramebuffer);
    if(rw!=d->sdfWidth||rh!=d->sdfHeight) {
        glBindTexture(GL_TEXTURE_2D,d->sdfColor);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,rw,rh,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,d->sdfColor,0);
        glBindRenderbuffer(GL_RENDERBUFFER,d->sdfDepth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,rw,rh);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,d->sdfDepth);
        glBindTexture(GL_TEXTURE_2D,0);glBindRenderbuffer(GL_RENDERBUFFER,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_FRAMEBUFFER,0);return false;}
        d->sdfWidth=rw;d->sdfHeight=rh;
    }
    unsigned q=d->queryFrame%4;
    d->queryIssued=!d->queryPending[q];
    if(d->queryIssued)glBeginQuery(GL_TIME_ELAPSED,d->sdfQueries[q]);
    d->sdfFrameActive=true;
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);OpenGLRenderer_SetupCamera(camera,rw,rh);
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
        glUniform1i(glGetUniformLocation(p,"validationMode"),1);
        glUniform1i(glGetUniformLocation(p,"validationPointBase"),(GLint)d->sdfCount);
        glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);glEnd();
        glReadPixels(0,0,(GLsizei)count,1,GL_RGBA,GL_FLOAT,samples);
        glUniform1i(glGetUniformLocation(p,"validationMode"),0);
        *maxError=0;
        for(size_t i=0;i<count;++i) {
            float gpu=samples[i*4];
            float cpu=MonsterSDF_EvaluateVisualDistance(sdf,points[i]);
            if(!isfinite(gpu)||!isfinite(cpu)){ok=false;break;}
            *maxError=fmaxf(*maxError,fabsf(cpu-gpu));
        }
        glUseProgram(0);glEnable(GL_DEPTH_TEST);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)previousDraw);glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previousRead);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glBindTexture(GL_TEXTURE_2D,0);glBindTexture(GL_TEXTURE_BUFFER,0);glBindBuffer(GL_TEXTURE_BUFFER,0);
    glDeleteTextures(1,&texture);glDeleteFramebuffers(1,&framebuffer);free(samples);return ok;
}
