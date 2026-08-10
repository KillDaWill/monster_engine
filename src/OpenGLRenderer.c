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

/* ============================================================
 * MESH RENDERER
 * ============================================================ */

void OpenGLRenderer_RenderMesh(const Mesh* mesh) {
    if (!mesh || mesh->vertexCount == 0 || mesh->indexCount == 0) return;

    size_t triangleCount = mesh->indexCount / 3;
    size_t skippedTriangles = 0;

    glBegin(GL_TRIANGLES);
    for (size_t t = 0; t < triangleCount; ++t) {
        MeshIndex a = mesh->indices[t * 3 + 0];
        MeshIndex b = mesh->indices[t * 3 + 1];
        MeshIndex c = mesh->indices[t * 3 + 2];

        if (a >= mesh->vertexCount || b >= mesh->vertexCount || c >= mesh->vertexCount) {
            ++skippedTriangles;
            continue;
        }

        MeshVertex va = mesh->vertices[a];
        MeshVertex vb = mesh->vertices[b];
        MeshVertex vc = mesh->vertices[c];

        glColor4ub(va.color.r, va.color.g, va.color.b, va.color.a);
        glNormal3f(va.normal.x, va.normal.y, va.normal.z);
        glVertex3f(va.position.x, va.position.y, va.position.z);

        glColor4ub(vb.color.r, vb.color.g, vb.color.b, vb.color.a);
        glNormal3f(vb.normal.x, vb.normal.y, vb.normal.z);
        glVertex3f(vb.position.x, vb.position.y, vb.position.z);

        glColor4ub(vc.color.r, vc.color.g, vc.color.b, vc.color.a);
        glNormal3f(vc.normal.x, vc.normal.y, vc.normal.z);
        glVertex3f(vc.position.x, vc.position.y, vc.position.z);
    }
    glEnd();

    if (skippedTriangles > 0) {
        fprintf(stderr,
            "OpenGLRenderer: %zu triángulo(s) omitido(s) por índice inválido (mesh de %zu vértices)\n",
            skippedTriangles, (size_t)mesh->vertexCount);
    }
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
        glPolygonMode(GL_FRONT_AND_BACK, previousPolygonMode[1]);
    }
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