#define GL_GLEXT_PROTOTYPES
#include "SceneLighting.h"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { SCENE_BINDING = 3, SHADOW_UNIT = 12, IRRADIANCE_UNIT = 14 };
static const Vector3 defaultSun = {-.465f, .736f, .426f};

typedef struct SceneFrameUniforms {
    float viewToWorld[16], worldToShadow[16];
    float sunWorld[4], sunView[4], sunRadiance[4], skyFill[4], groundFill[4], haze[4];
    float camera[4], viewAxis[4], extras[4], settings[4], irradianceMapping[4];
} SceneFrameUniforms;

static void unpackVector(float out[4], Vector3 v, float w) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    out[3] = w;
}

static unsigned uploadFrame(unsigned buffer, const SceneFrameUniforms *data) {
    GLint previous = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);
    if (!buffer) glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(*data), data, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, SCENE_BINDING, buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, (GLuint)previous);
    return buffer;
}

unsigned SceneLighting_CreateStudio(void) {
    const SceneFrameUniforms studio = {0};
    return uploadFrame(0, &studio);
}

void SceneLighting_BindStudio(unsigned buffer) {
    GLint previous;
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);
    glBindBufferBase(GL_UNIFORM_BUFFER, SCENE_BINDING, buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, (GLuint)previous);
}

void SceneLighting_LinkProgram(unsigned program) {
    GLuint block = glGetUniformBlockIndex(program, "SceneLightingFrame");
    if (block == GL_INVALID_INDEX) return;
    glUniformBlockBinding(program, block, SCENE_BINDING);
    GLint shadow = glGetUniformLocation(program, "uSceneShadow");
    GLint irradiance = glGetUniformLocation(program, "uSceneIrradiance");
    if (shadow < 0 && irradiance < 0) return;
    GLint previous;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    glUseProgram(program);
    glUniform1i(shadow, SHADOW_UNIT);
    glUniform1i(irradiance, IRRADIANCE_UNIT);
    glUseProgram((GLuint)previous);
}

unsigned SceneLighting_LinkDepth(unsigned vertexShader) {
    const char *source = "#version 330 compatibility\nvoid main() {}\n";
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &source, NULL);
    glCompileShader(fragment);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked) return program;
    char log[1024];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    fprintf(stderr, "[SceneLighting] Programa de profundidad: %s\n", log);
    glDeleteProgram(program);
    return 0;
}

void SceneLighting_Destroy(SceneLighting *scene) {
    SceneLighting_End(scene);
    glDeleteFramebuffers(1, &scene->compositionFbo);
    glDeleteRenderbuffers(1, &scene->compositionColor);
    glDeleteRenderbuffers(1, &scene->compositionDepth);
    glDeleteBuffers(1, &scene->frameBuffer);
    glDeleteTextures(1, &scene->irradiance);
    glDeleteFramebuffers(1, &scene->shadowFbo);
    glDeleteTextures(1, &scene->shadowDepth);
    glDeleteFramebuffers(1, &scene->staticFbo);
    glDeleteTextures(1, &scene->staticDepth);
    *scene = (SceneLighting){0};
}

static bool configureComposition(SceneLighting *scene, int width, int height, int samples, GLenum depthFormat) {
    if (width <= 0 || height <= 0) return false;
    if (!scene->compositionFbo) {
        glGenFramebuffers(1, &scene->compositionFbo);
        glGenRenderbuffers(1, &scene->compositionColor);
        glGenRenderbuffers(1, &scene->compositionDepth);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, scene->compositionFbo);
    if (scene->compositionWidth == width && scene->compositionHeight == height &&
        scene->compositionSamples == samples && scene->compositionDepthFormat == (int)depthFormat) return true;

    GLint previous;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous);
    glBindRenderbuffer(GL_RENDERBUFFER, scene->compositionColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_SRGB8_ALPHA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, scene->compositionColor);
    glBindRenderbuffer(GL_RENDERBUFFER, scene->compositionDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, depthFormat, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, scene->compositionDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)previous);
    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    scene->compositionWidth = complete ? width : 0;
    scene->compositionHeight = height;
    scene->compositionSamples = samples;
    scene->compositionDepthFormat = (int)depthFormat;
    return complete;
}

static bool setupOutput(SceneLighting *scene) {
    GLint buffer;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &scene->outputDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &scene->outputReadFbo);
    glGetIntegerv(GL_DRAW_BUFFER0, &buffer);
    if (!scene->outputDrawFbo)
        buffer = (buffer == GL_FRONT || buffer == GL_FRONT_LEFT) ? GL_FRONT_LEFT : GL_BACK_LEFT;
    scene->outputEncoding = GL_LINEAR;
    if (buffer != GL_NONE)
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, (GLenum)buffer,
                                               GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &scene->outputEncoding);
    scene->composite = false;
    if (scene->outputEncoding == GL_SRGB) return true;

    GLint samples, depthBits, stencilBits;
    glGetIntegerv(GL_SAMPLES, &samples);
    glGetIntegerv(GL_DEPTH_BITS, &depthBits);
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
    glGetIntegerv(GL_VIEWPORT, scene->outputBounds);
    GLenum depthFormat = stencilBits ? (depthBits > 24 ? GL_DEPTH32F_STENCIL8 : GL_DEPTH24_STENCIL8) :
        depthBits > 24 ? GL_DEPTH_COMPONENT32F : depthBits > 16 ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT16;
    if (!configureComposition(scene, scene->outputBounds[2], scene->outputBounds[3], samples, depthFormat)) {
        fprintf(stderr, "[SceneLighting] No se pudo crear un destino sRGB de composición.\n");
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)scene->outputDrawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)scene->outputReadFbo);
        return false;
    }
    scene->composite = true;
    scene->outputEncoding = GL_SRGB;
    int w = scene->outputBounds[2], h = scene->outputBounds[3];
    glViewport(0, 0, w, h);
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPopAttrib();
    return true;
}

bool SceneLighting_Begin(SceneLighting *scene, const ICamera *camera) {
    if (scene->active) return false;
    float view[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            scene->viewToWorld[column * 4 + row] = view[row * 4 + column];
    scene->viewToWorld[12] = camera->position.x;
    scene->viewToWorld[13] = camera->position.y;
    scene->viewToWorld[14] = camera->position.z;
    scene->viewToWorld[15] = 1.0f;

    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, SCENE_BINDING, &scene->previousBlock);
    const GLboolean wasSRGB = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    scene->previousSRGB = wasSRGB;
    GLint active;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &scene->previousTexture);
    glGetIntegeri_v(GL_SAMPLER_BINDING, SHADOW_UNIT, &scene->previousSampler);
    glBindSampler(SHADOW_UNIT, 0);
    glActiveTexture(GL_TEXTURE0 + IRRADIANCE_UNIT);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &scene->previousIrradiance);
    glGetIntegeri_v(GL_SAMPLER_BINDING, IRRADIANCE_UNIT, &scene->previousIrradianceSampler);
    glBindSampler(IRRADIANCE_UNIT, 0);
    glActiveTexture((GLenum)active);
    scene->active = true;
    if (setupOutput(scene)) return true;
    SceneLighting_End(scene);
    return false;
}

void SceneLighting_Apply(SceneLighting *scene, const ICamera *camera, bool useShadow) {
    if (!scene->active) return;
    GLint encoding = scene->outputEncoding;
    if (encoding == GL_SRGB) glEnable(GL_FRAMEBUFFER_SRGB);
    else glDisable(GL_FRAMEBUFFER_SRGB);

    SceneFrameUniforms data = {
        .sunRadiance = {.73f, .67f, .56f, 0},
        .skyFill = {.54f, .59f, .67f, 0},
        .groundFill = {.27f, .26f, .25f, 0},
        .haze = {.28f, .37f, .46f, .27f},
        .extras = {!scene->periodicMaterials, !scene->cheapShadows, scene->shadowTexel, (float)scene->debug},
        .settings = {1.0f, encoding != GL_SRGB ? 1.0f : 0.0f,
                     useShadow ? 1.0f / (float)scene->shadowSize : 0.0f, scene->depthBias}
    };
    memcpy(data.viewToWorld, scene->viewToWorld, sizeof(data.viewToWorld));
    memcpy(data.worldToShadow, scene->worldToShadow, sizeof(data.worldToShadow));
    Vector3 direction = (scene->sun.y > 0.0f ? scene->sun : Vec3_Normalize(defaultSun));
    unpackVector(data.sunWorld, direction, (float)(scene->presentationSeed & 0xffffffu));
    Vector3 right = {data.viewToWorld[0], data.viewToWorld[1], data.viewToWorld[2]};
    Vector3 up = {data.viewToWorld[4], data.viewToWorld[5], data.viewToWorld[6]};
    Vector3 back = {data.viewToWorld[8], data.viewToWorld[9], data.viewToWorld[10]};
    unpackVector(data.sunView, Vec3_Create(Vec3_Dot(direction, right), Vec3_Dot(direction, up),
                                          Vec3_Dot(direction, back)), 0);
    unpackVector(data.camera, camera->position, camera->fov <= 0);
    unpackVector(data.viewAxis, back, 1.0f);
    unpackVector(data.irradianceMapping, scene->irradianceMapping,
                 scene->irradiance && !scene->globalFill ? 1.0f : 0.0f);
    scene->frameBuffer = uploadFrame(scene->frameBuffer, &data);
    GLint active;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);
    glBindTexture(GL_TEXTURE_2D, useShadow ? scene->shadowDepth : 0);
    glActiveTexture(GL_TEXTURE0 + IRRADIANCE_UNIT);
    glBindTexture(GL_TEXTURE_2D, scene->irradiance);
    glActiveTexture((GLenum)active);
}

void SceneLighting_End(SceneLighting *scene) {
    if (!scene->active) return;
    SceneLighting_EndShadow(scene);
    if (scene->composite) {
        int *v = scene->outputBounds;
        glDisable(GL_FRAMEBUFFER_SRGB);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, scene->compositionFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)scene->outputDrawFbo);
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, v[2], v[3], v[0], v[1], v[0] + v[2], v[1] + v[3],
                          GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)scene->outputReadFbo);
        glViewport(v[0], v[1], v[2], v[3]);
        scene->composite = false;
    }
    GLint active;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);
    glBindTexture(GL_TEXTURE_2D, (GLuint)scene->previousTexture);
    glBindSampler(SHADOW_UNIT, (GLuint)scene->previousSampler);
    glActiveTexture(GL_TEXTURE0 + IRRADIANCE_UNIT);
    glBindTexture(GL_TEXTURE_2D, (GLuint)scene->previousIrradiance);
    glBindSampler(IRRADIANCE_UNIT, (GLuint)scene->previousIrradianceSampler);
    glActiveTexture((GLenum)active);
    SceneLighting_BindStudio((unsigned)scene->previousBlock);
    if (scene->previousSRGB) glEnable(GL_FRAMEBUFFER_SRGB);
    else glDisable(GL_FRAMEBUFFER_SRGB);
    scene->active = false;
}

static bool ensureShadowTarget(GLuint *fbo, GLuint *texture, int *storedSize, int size) {
    if (*storedSize == size) return true;
    if (!*texture) glGenTextures(1, texture);
    if (!*fbo) glGenFramebuffers(1, fbo);
    GLint previousTexture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, *texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[SceneLighting] FBO de sombras incompleto (%#x).\n", status);
        *storedSize = 0;
        return false;
    }
    *storedSize = size;
    return true;
}

static void projectRange(Vector3 axis, Vector3 lo, Vector3 hi, float *minimum, float *maximum) {
    Vector3 negative = {fminf(axis.x,0), fminf(axis.y,0), fminf(axis.z,0)};
    Vector3 positive = {fmaxf(axis.x,0), fmaxf(axis.y,0), fmaxf(axis.z,0)};
    *minimum = Vec3_Dot(negative, hi) + Vec3_Dot(positive, lo);
    *maximum = Vec3_Dot(negative, lo) + Vec3_Dot(positive, hi);
}

bool SceneLighting_BeginShadow(SceneLighting *scene, const ICamera *camera,
                               Vector3 minimum, Vector3 maximum, SceneShadowQuality quality) {
    if (!scene->active || quality <= SCENE_SHADOW_OFF) return false;
    int size = quality == SCENE_SHADOW_HIGH ? 2304 : quality == SCENE_SHADOW_LOW ? 1024 : 2048;
    scene->cheapShadows = quality == SCENE_SHADOW_LOW;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &scene->previousDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &scene->previousReadFbo);
    bool ready = ensureShadowTarget(&scene->shadowFbo, &scene->shadowDepth, &scene->shadowSize, size);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)scene->previousDrawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)scene->previousReadFbo);
    if (!ready) return false;
    Vector3 z = (scene->sun.y > 0.0f ? scene->sun : Vec3_Normalize(defaultSun));
    Vector3 x = Vec3_Normalize(Vec3_Cross(Vec3_Create(0,1,0), z));
    Vector3 y = Vec3_Cross(z, x);
    float xmin, xmax, ymin, ymax, zmin, zmax;
    projectRange(x, minimum, maximum, &xmin, &xmax);
    projectRange(y, minimum, maximum, &ymin, &ymax);
    projectRange(z, minimum, maximum, &zmin, &zmax);
    float worldWidth = fmaxf(xmax - xmin, ymax - ymin);
    float focusDistance = Vec3_Distance(camera->position, camera->target);
    float viewHeight = camera->fov <= 0 ? -camera->fov :
        2.0f * focusDistance * tanf(camera->fov * .00872664626f);
    // Por debajo de la vista global, estabilizar el área en pasos de escala discretos.
    float width = fmaxf(12, viewHeight * fmaxf(camera->aspectRatio, 1) * 1.65f);
    width = powf(1.25f, ceilf(logf(width) / logf(1.25f)));
    bool overview = width >= worldWidth * .87f;
    if (overview) width = worldWidth + 4.0f;
    float cx = overview ? (xmin + xmax) * .5f : Vec3_Dot(x, camera->target);
    float cy = overview ? (ymin + ymax) * .5f : Vec3_Dot(y, camera->target);
    float texel = width / (float)size;
    cx = roundf(cx / texel) * texel;
    cy = roundf(cy / texel) * texel;
    float depth = zmax - zmin;
    scene->shadowTexel = texel;
    scene->depthBias = texel * .23f / depth;
    float view[16] = {x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, -cx, -cy, -zmax, 1};
    float shadow[16] = {
        x.x / width, y.x / width, -z.x / depth, 0,
        x.y / width, y.y / width, -z.y / depth, 0,
        x.z / width, y.z / width, -z.z / depth, 0,
        .5f - cx / width, .5f - cy / width, zmax / depth, 1
    };
    memcpy(scene->worldToShadow, shadow, sizeof(shadow));

    glGetIntegerv(GL_MATRIX_MODE, &scene->previousMatrixMode);
    glPushAttrib(GL_VIEWPORT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, scene->shadowFbo);
    glViewport(0, 0, size, size);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_ALPHA_TO_ONE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(.70f, 2.0f);
    glClearDepth(1.0);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-width * .5, width * .5, -width * .5, width * .5, 0, depth);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf(view);
    scene->depthPass = true;
    return true;
}

bool SceneLighting_BeginStatic(SceneLighting *scene, unsigned epoch) {
    if (scene->staticSize != scene->shadowSize) {
        scene->staticValid = false;
        if (!ensureShadowTarget(&scene->staticFbo, &scene->staticDepth, &scene->staticSize, scene->shadowSize)) {
            glBindFramebuffer(GL_FRAMEBUFFER, scene->shadowFbo);
            glClear(GL_DEPTH_BUFFER_BIT);
            return true;
        }
    }
    if (scene->staticValid && scene->staticEpoch == epoch &&
        memcmp(scene->staticMatrix, scene->worldToShadow, sizeof(scene->staticMatrix)) == 0) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, scene->staticFbo);
    glClear(GL_DEPTH_BUFFER_BIT);
    return true;
}

void SceneLighting_EndStatic(SceneLighting *scene, unsigned epoch, bool updated) {
    if (!scene->staticSize) return;
    if (updated) {
        scene->staticValid = true;
        scene->staticEpoch = epoch;
        memcpy(scene->staticMatrix, scene->worldToShadow, sizeof(scene->staticMatrix));
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, scene->staticFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scene->shadowFbo);
    int size = scene->shadowSize;
    glBlitFramebuffer(0, 0, size, size, 0, 0, size, size, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, scene->shadowFbo);
}

bool SceneLighting_SetSun(SceneLighting *scene, float azimuthDegrees, float elevationDegrees) {
    if (!isfinite(azimuthDegrees) || !isfinite(elevationDegrees) ||
        elevationDegrees < 15 || elevationDegrees > 85) return false;
    float azimuth = fmodf(azimuthDegrees, 360) * .01745329252f;
    float elevation = elevationDegrees * .01745329252f;
    scene->sun = Vec3_Create(cosf(azimuth) * cosf(elevation), sinf(elevation), sinf(azimuth) * cosf(elevation));
    return true;
}

void SceneLighting_EndShadow(SceneLighting *scene) {
    if (!scene->depthPass) return;
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode((GLenum)scene->previousMatrixMode);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)scene->previousDrawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)scene->previousReadFbo);
    glPopAttrib();
    scene->depthPass = false;
}

bool SceneLighting_ParseShadows(const char *name, SceneShadowQuality *quality) {
    const char *options[] = {"off", "low", "medium", "high"};
    for (int i = 0; i < 4; ++i) {
        if (strcmp(name, options[i]) == 0) {
            *quality = (SceneShadowQuality)i;
            return true;
        }
    }
    return false;
}

bool SceneLighting_ParseDebug(const char *name, unsigned *debug) {
    static const char *modes[] = {"final", "authored", "albedo", "radiance", "geometry",
        "normal", "direct", "indirect", "visibility", "ao", "specular", "roughness", "material", "exposure"};
    if (!name || !debug) return false;
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (strcmp(name, modes[i]) == 0) { *debug = i; return true; }
    }
    return false;
}
