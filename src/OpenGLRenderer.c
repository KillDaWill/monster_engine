#include "OpenGLRenderer.h"
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
} OpenGLRendererData;

void OpenGLRenderer_SetWireframe(Renderer3D* renderer, bool enabled) {
    if (renderer && renderer->user_data) {
        OpenGLRendererData* data = (OpenGLRendererData*)renderer->user_data;
        data->wireframe = enabled;
    }
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