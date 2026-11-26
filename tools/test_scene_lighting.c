/** @brief Diagnóstico GL de la proyección solar, sombras y composición de color. */
#define GL_GLEXT_PROTOTYPES
#include "SceneLighting.h"
#include "OpenGLRenderer.h"
#include "environment_glsl.inc"
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { SIDE = 32 };

static GLuint shader(GLenum stage, const char *const *source, int count) {
    GLuint id = glCreateShader(stage);
    glShaderSource(id, count, source, NULL);
    glCompileShader(id);
    GLint success;
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[2048];
        glGetShaderInfoLog(id, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

static GLuint fixtureProgram(void) {
    const char *vs[] = {"#version 330 compatibility\nvoid main(){gl_Position=gl_Vertex;}\n"};
    const char *fs[] = {"#version 330 compatibility\n", ENV_SCENE_LIGHTING, ENV_SCENE_MATERIAL_LIGHTING,
        "uniform vec3 shadePoint, clearPoint; uniform int mode;\n"
        "out vec4 color;\n"
        "void main(){if(mode==1){color=vec4(1,1,1,.5);return;}"
        "if(gl_FragCoord.y<8){vec3 n=vec3(0,1,0);"
        "SceneLightResponse a=sceneDielectric(clearPoint,n,n,sceneViewDirection(clearPoint),.7,.04,1);"
        "SceneLightResponse b=sceneDielectric(clearPoint,n,n,sceneViewDirection(clearPoint),.7,.04,.7);"
        "if(gl_FragCoord.x<8)color=vec4(sceneOutput(sceneDecode(vec3(.461356))),1);"
        "else if(gl_FragCoord.x<16)color=vec4(sceneOutput(a.ambient),1);"
        "else if(gl_FragCoord.x<24)color=vec4(sceneOutput(b.ambient),1);"
        "else color=vec4(vec3(float(length(a.sun-b.sun)<1e-6)),1);return;}"
        "if(gl_FragCoord.x<8)color=vec4(vec3(sceneVisibility(shadePoint,vec3(0,1,0))),1);"
        "else if(gl_FragCoord.x<16)color=vec4(vec3(sceneVisibility(clearPoint,vec3(0,1,0))),1);"
        "else if(gl_FragCoord.x<24){vec3 n=transpose(mat3(sceneViewToWorld))*vec3(0,1,0);"
        "color=vec4(vec3(dot(n,sceneSunView.xyz)),1);}"
        "else color=vec4(vec3(.18),1);}\n"};
    GLuint v = shader(GL_VERTEX_SHADER, vs, 1);
    GLuint f = shader(GL_FRAGMENT_SHADER, fs, (int)(sizeof(fs) / sizeof(fs[0])));
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint linked;
    glGetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(p);
        return 0;
    }
    SceneLighting_LinkProgram(p);
    return p;
}

typedef struct Target {
    GLuint fbo, color, depth;
} Target;

static bool createTarget(Target *target, GLenum encoding, int samples) {
    glGenFramebuffers(1, &target->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
    glGenRenderbuffers(1, &target->color);
    glBindRenderbuffer(GL_RENDERBUFFER, target->color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, encoding, SIDE, SIDE);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, target->color);
    glGenRenderbuffers(1, &target->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, target->depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, SIDE, SIDE);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target->depth);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void clearTarget(Target target) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &target.fbo);
    glDeleteRenderbuffers(1, &target.color);
    glDeleteRenderbuffers(1, &target.depth);
}

static void quad(float left, float right, float y, bool world) {
    glBegin(GL_QUADS);
    if (world) {
        glVertex3f(left,y,1);
        glVertex3f(right,y,1);
        glVertex3f(right,y,-1);
        glVertex3f(left,y,-1);
    } else {
        glVertex2f(left,-1);
        glVertex2f(right,-1);
        glVertex2f(right,1);
        glVertex2f(left,1);
    }
    glEnd();
}

static int channel(int x) {
    unsigned char color[4] = {0};
    glReadPixels(x, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color);
    return color[0];
}

static bool fixture(GLuint program, SceneLighting *scene, int samples, bool linear, unsigned orbit) {
    Target output = {0}, resolve = {0};
    bool ok = createTarget(&resolve, GL_RGBA8, 0) &&
              createTarget(&output, linear ? GL_RGBA8 : GL_SRGB8_ALPHA8, samples);
    if (!ok) goto exit;

    ICamera camera = {
        .position = orbit ? Vec3_Create(-6,4,7) : Vec3_Create(7,6,8),
        .target = {0,0,0}, .up = {0,1,0}, .aspectRatio = 1, .fov = orbit ? 55 : -8,
        .nearPlane = .1f, .farPlane = 80
    };
    OpenGLRenderer_SetupCamera(&camera, SIDE, SIDE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glViewport(0, 0, SIDE, SIDE);
    glActiveTexture(GL_TEXTURE5);
    glDepthMask(GL_FALSE);
    SceneLighting_SetSun(scene, 149, 47);
    ok = SceneLighting_Begin(scene, &camera);
    ok = SceneLighting_BeginShadow(scene, &camera, Vec3_Create(-14,-2,-14),
                                  Vec3_Create(14,9,14), SCENE_SHADOW_MEDIUM) && ok;
    bool update = SceneLighting_BeginStatic(scene, orbit);
    ok = (update != linear) && ok;
    SceneLighting_EndStatic(scene, orbit, update);
    float shift = orbit >= 2 ? 3.5f : 0.0f;
    quad(-1 + shift, 1 + shift, 2, true);
    SceneLighting_EndShadow(scene);
    SceneLighting_Apply(scene, &camera, true);

    Vector3 shade = Vec3_Sub(Vec3_Create(shift,2,0), Vec3_Scale(scene->sun, 2.0f / scene->sun.y));
    Vector3 clear = Vec3_Add(shade, Vec3_Create(2,0,0));
    glUseProgram(program);
    glUniform3f(glGetUniformLocation(program, "shadePoint"), shade.x, shade.y, shade.z);
    glUniform3f(glGetUniformLocation(program, "clearPoint"), clear.x, clear.y, clear.z);
    glUniform1i(glGetUniformLocation(program, "mode"), 0);
    quad(-1, 1, 0, false);
    glUniform1i(glGetUniformLocation(program, "mode"), 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    quad(.5f, .75f, 0, false);
    glDisable(GL_BLEND);
    glUseProgram(0);

    SceneLighting_End(scene);
    GLint active, framebuffer, viewport[4];
    GLboolean depthMask;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    ok = ok && active == GL_TEXTURE5 && framebuffer == (int)output.fbo &&
         viewport[2] == SIDE && viewport[3] == SIDE && !depthMask && !glIsEnabled(GL_FRAMEBUFFER_SRGB);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve.fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, output.fbo);
    glBlitFramebuffer(0, 0, SIDE, SIDE, 0, 0, SIDE, SIDE, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve.fbo);
    int dark = channel(4), light = channel(12), cosine = channel(20), blend = channel(26), gray = channel(30);
    unsigned char row[4 * SIDE];
    glReadPixels(0, 4, SIDE, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
    printf("swatches decoded_018=%u sky=%u ao=%u direct_unchanged=%u\n", row[4*4], row[4*12], row[4*20], row[4*30]);
    ok = ok && abs((int)row[4*4] - 118) <= 2 && row[4*12] > row[4*20] && row[4*30] == 255;
    printf("fixture samples=%d linear_target=%d orbit=%u visibility=%d/%d normal=%d linear_018=%d blend_half_white=%d\n",
           samples, linear, orbit, dark, light, cosine, gray, blend);
    ok = ok && dark < 8 && light >= 248 && abs(cosine - 222) <= 3 &&
         abs(gray - 118) <= 2 && abs(blend - 202) <= 3;
exit:
    glDepthMask(GL_TRUE);
    clearTarget(output);
    clearTarget(resolve);
    return OpenGLRenderer_CheckErrors() && ok;
}

static bool normalFixture(void) {
    const char *vs[] = {"#version 330 compatibility\nvoid main(){gl_Position=gl_Vertex;}\n"};
    const char *fs[] = {"#version 330 compatibility\n", ENV_SCENE_LIGHTING, ENV_SCENE_MATERIAL_LIGHTING, ENV_MATERIALS, ENV_PROJECTION,
        "out vec4 color;\n"
        "void main(){"
        "GroundMaterial a=tileSample(vec2(0),vec2(.1,0),vec2(0,.1),mat2(0,1,-1,0),vec2(0),0,true);"
        "GroundMaterial b=sampleProjection(vec3(0),vec3(.1,0,0),vec3(0,0,.1),vec3(0,0,-1),"
        "vec3(1,0,0),0,true,false);"
        "color=vec4(float(a.gradient.y < -.5 && abs(a.gradient.x)<.01),"
        "float(b.gradient.z < -.5 && abs(b.gradient.x)<.01),0,1);}\n"};
    GLuint v = shader(GL_VERTEX_SHADER, vs, 1), f = shader(GL_FRAGMENT_SHADER, fs, (int)(sizeof(fs) / sizeof(fs[0])));
    if (!v || !f) return false;
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    Target target = {0};
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked || !createTarget(&target, GL_RGBA8, 0)) {
        glDeleteProgram(program);
        clearTarget(target);
        return false;
    }
    GLuint textures[3];
    const uint8_t texel[] = {191,128,128,255};
    glGenTextures(3, textures);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uMaterialPack"), 2);
    const char *names[] = {"uMaterialAlbedo", "uMaterialNormal", "uMaterialMasks"};
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D_ARRAY, textures[i]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glUniform1i(glGetUniformLocation(program, names[i]), i);
    }
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, SIDE, SIDE);
    quad(-1,1,0,false);
    unsigned char pixel[4];
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("normal_fixture rotated=%u negative_projection=%u\n", pixel[0], pixel[1]);
    glUseProgram(0);
    glDeleteTextures(3, textures);
    glDeleteProgram(program);
    clearTarget(target);
    return OpenGLRenderer_CheckErrors() && pixel[0] == 255 && pixel[1] == 255;
}

int main(void) {
    OpenGLDemoWindow *window = OpenGLDemoWindow_CreateMSAA("Validación de luz solar", 128, 128, 0);
    if (!window) return 1;
    GLuint program = fixtureProgram();
    SceneLighting scene = {0};
    bool ok = program != 0;
    if (program) {
        for (unsigned orbit = 0; orbit < 3; ++orbit) {
            ok = fixture(program, &scene, orbit > 1 ? 4 : 0, false, orbit) && ok;
            ok = fixture(program, &scene, orbit > 1 ? 4 : 0, true, orbit) && ok;
        }
    }
    ok = normalFixture() && ok;
    SceneLighting_Destroy(&scene);
    glDeleteProgram(program);
    OpenGLDemoWindow_Free(window);
    return ok ? 0 : 1;
}
