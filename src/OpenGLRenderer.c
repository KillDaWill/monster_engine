#include "OpenGLRenderer.h"
#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>

/* ============================================================
 * FRAME LIFECYCLE
 * ============================================================ */

static void OpenGL_BeginFrame(MonsterRenderer* self) {
    (void)self;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void OpenGL_EndFrame(MonsterRenderer* self) {
    (void)self;
}

/* ============================================================
 * MESH RENDERER
 * ============================================================ */

void OpenGLRenderer_RenderMesh(const Mesh* mesh) {
    if (!mesh || mesh->vertexCount == 0 || mesh->indexCount == 0) return;

    glBegin(GL_TRIANGLES);
    for (size_t i = 0; i < mesh->indexCount; ++i) {
        unsigned int idx = mesh->indices[i];
        if (idx < mesh->vertexCount) {
            MeshVertex v = mesh->vertices[idx];
            glColor4ub(v.color.r, v.color.g, v.color.b, v.color.a);
            glNormal3f(v.normal.x, v.normal.y, v.normal.z);
            glVertex3f(v.position.x, v.position.y, v.position.z);
        }
    }
    glEnd();
}

static void OpenGL_RenderMeshCallback(MonsterRenderer* self, const Mesh* mesh) {
    (void)self;
    OpenGLRenderer_RenderMesh(mesh);
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

MonsterRenderer OpenGLRenderer_Create(ICamera* camera) {
    (void)camera;
    MonsterRenderer renderer;
    renderer.user_data = NULL;
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