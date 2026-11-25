/**
 * @file WaterRenderer.c
 * @brief Malla estática de regiones húmedas físicas y shader GLSL 330 con color por profundidad,
 *        Fresnel, normales animadas alineadas con el flujo del río y espuma sutil de orilla.
 */
#define GL_GLEXT_PROTOTYPES
#include "WaterRenderer.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct WaterGpuVertex {
    float pos[3];
    float depth;
    float flow[2];
    float bodyTint[3];
} WaterGpuVertex;

typedef struct WaterRendererImpl {
    GLuint program;
    bool programFailed;
    GLint uCamPosLoc;
    GLint uTimeLoc;
    GLint uQualityLoc;

    GLuint vao, vbo, ebo;
    uint32_t indexCount;
    uint64_t worldEpoch;
    uint64_t worldFingerprint;
    int qualityTier;
    WaterRenderStats stats;
} WaterRendererImpl;

static GLuint compileWaterShader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    if (!s) return 0u;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[WaterRenderer] Shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0u;
    }
    return s;
}

static bool initWaterShader(WaterRendererImpl *impl) {
    if (impl->program) return true;
    if (impl->programFailed) return false;

    const char *vs =
        "#version 330 compatibility\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in float aDepth;\n"
        "layout(location=2) in vec2 aFlow;\n"
        "layout(location=3) in vec3 aTint;\n"
        "out vec3 vWorldPos;\n"
        "out float vDepth;\n"
        "out vec2 vFlow;\n"
        "out vec3 vTint;\n"
        "void main() {\n"
        "  vWorldPos = aPos;\n"
        "  vDepth = aDepth;\n"
        "  vFlow = aFlow;\n"
        "  vTint = aTint;\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);\n"
        "}\n";

    const char *fs =
        "#version 330 compatibility\n"
        "uniform vec3 uCamPos;\n"
        "uniform float uTime;\n"
        "uniform int uQuality;\n"
        "in vec3 vWorldPos;\n"
        "in float vDepth;\n"
        "in vec2 vFlow;\n"
        "in vec3 vTint;\n"
        "out vec4 fragColor;\n"
        "float waveH(vec2 p) {\n"
        "  float w = sin(p.x * 1.9 + p.y * 1.3) * 0.5;\n"
        "  w += sin(p.x * 3.1 - p.y * 2.7) * 0.3;\n"
        "  w += cos(p.x * 5.3 + p.y * 4.1) * 0.2;\n"
        "  return w;\n"
        "}\n"
        "void main() {\n"
        "  if (vDepth <= 0.002) discard;\n"
        "  vec3 viewDir = normalize(uCamPos - vWorldPos);\n"
        "  vec3 sunDir = normalize(vec3(-0.48, 0.76, 0.44));\n"
        "  vec3 n = vec3(0.0, 1.0, 0.0);\n"
        "  if (uQuality >= 1) {\n"
        "    vec2 flowVel = length(vFlow) > 0.05 ? vFlow : vec2(0.18, 0.11);\n"
        "    vec2 p1 = vWorldPos.xz * 0.75 - flowVel * uTime * 0.85;\n"
        "    vec2 p2 = vWorldPos.xz * 1.25 - flowVel * uTime * 1.35 + vec2(3.7, -2.1);\n"
        "    float eps = 0.08;\n"
        "    float h0 = waveH(p1) + 0.65 * waveH(p2);\n"
        "    float hx = waveH(p1 + vec2(eps, 0.0)) + 0.65 * waveH(p2 + vec2(eps, 0.0));\n"
        "    float hz = waveH(p1 + vec2(0.0, eps)) + 0.65 * waveH(p2 + vec2(0.0, eps));\n"
        "    float amp = mix(0.035, 0.085, clamp(length(vFlow), 0.0, 1.0));\n"
        "    n = normalize(vec3((h0 - hx) / eps * amp, 1.0, (h0 - hz) / eps * amp));\n"
        "  }\n"
        "  float depthT = clamp(vDepth / 1.8, 0.0, 1.0);\n"
        "  vec3 shallowCol = mix(vec3(0.28, 0.56, 0.58), vTint * 1.35, 0.55);\n"
        "  vec3 deepCol    = vTint * vec3(0.32, 0.50, 0.68);\n"
        "  vec3 waterCol   = mix(shallowCol, deepCol, smoothstep(0.0, 1.0, depthT));\n"
        "  float fresnel = pow(1.0 - max(dot(n, viewDir), 0.0), 4.0);\n"
        "  fresnel = clamp(0.05 + 0.78 * fresnel, 0.05, 0.85);\n"
        "  vec3 skyCol = vec3(0.58, 0.72, 0.84);\n"
        "  vec3 col = mix(waterCol, skyCol, fresnel * (uQuality >= 1 ? 0.72 : 0.35));\n"
        "  vec3 halfV = normalize(sunDir + viewDir);\n"
        "  float spec = pow(max(dot(n, halfV), 0.0), 96.0) * 0.75;\n"
        "  col += vec3(1.0, 0.96, 0.86) * spec;\n"
        "  /* Espuma discreta únicamente en la línea de orilla somera. */\n"
        "  if (uQuality >= 2) {\n"
        "    float foamBand = (1.0 - smoothstep(0.02, 0.24, vDepth)) * smoothstep(0.003, 0.03, vDepth);\n"
        "    float foamNoise = 0.5 + 0.5 * sin(vWorldPos.x * 3.8 + vWorldPos.z * 3.1 - uTime * 2.2);\n"
        "    col = mix(col, vec3(0.86, 0.92, 0.94), foamBand * foamNoise * 0.42);\n"
        "  }\n"
        "  float alpha = clamp(mix(0.30, 0.90, smoothstep(0.0, 0.85, depthT)) + fresnel * 0.12, 0.22, 0.94);\n"
        "  float dist = length(uCamPos - vWorldPos);\n"
        "  float fog = smoothstep(95.0, 460.0, dist) * 0.45;\n"
        "  col = mix(col, vec3(0.56, 0.66, 0.74), fog);\n"
        "  fragColor = vec4(col, alpha);\n"
        "}\n";

    GLuint v = compileWaterShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileWaterShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        impl->programFailed = true;
        return false;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        impl->programFailed = true;
        return false;
    }
    impl->program = p;
    impl->uCamPosLoc = glGetUniformLocation(p, "uCamPos");
    impl->uTimeLoc = glGetUniformLocation(p, "uTime");
    impl->uQualityLoc = glGetUniformLocation(p, "uQuality");
    return true;
}

static void freeWaterMesh(WaterRendererImpl *impl) {
    if (!impl) return;
    if (impl->vao) { glDeleteVertexArrays(1, &impl->vao); impl->vao = 0; }
    if (impl->vbo) { glDeleteBuffers(1, &impl->vbo); impl->vbo = 0; }
    if (impl->ebo) { glDeleteBuffers(1, &impl->ebo); impl->ebo = 0; }
    impl->indexCount = 0u;
    impl->worldEpoch = 0u;
    impl->worldFingerprint = 0u;
    impl->stats = (WaterRenderStats){0};
}

bool WaterRenderer_Init(WaterRenderer *wr) {
    if (!wr) return false;
    memset(wr, 0, sizeof(*wr));
    WaterRendererImpl *impl = calloc(1, sizeof(*impl));
    if (!impl) return false;
    impl->qualityTier = 2;
    wr->impl = impl;
    return true;
}

void WaterRenderer_Destroy(WaterRenderer *wr) {
    if (!wr || !wr->impl) return;
    WaterRendererImpl *impl = (WaterRendererImpl *)wr->impl;
    freeWaterMesh(impl);
    if (impl->program) glDeleteProgram(impl->program);
    free(impl);
    wr->impl = NULL;
}

void WaterRenderer_SetQuality(WaterRenderer *wr, int qualityTier) {
    if (!wr || !wr->impl) return;
    ((WaterRendererImpl *)wr->impl)->qualityTier = qualityTier < 0 ? 0 : (qualityTier > 2 ? 2 : qualityTier);
}

bool WaterRenderer_SyncWorld(WaterRenderer *wr, const WorldStaticData *world) {
    if (!wr || !wr->impl) return false;
    WaterRendererImpl *impl = (WaterRendererImpl *)wr->impl;
    if (!world || world->isFlat || !world->hydrology.enabled || world->hydrology.totalWetCells == 0u) {
        freeWaterMesh(impl);
        return true;
    }
    if (impl->worldEpoch == world->epochId && impl->worldFingerprint == world->dataFingerprint &&
        impl->vao != 0u) {
        return true;
    }
    if (!initWaterShader(impl)) return false;
    freeWaterMesh(impl);

    const TerrainHeightfield *tf = &world->terrain;
    const WorldHydrology *hy = &world->hydrology;
    uint32_t res = tf->resolution;
    uint32_t cells = res - 1u;

    /* Construir malla estática únicamente sobre celdas con al menos 1 vértice húmedo.
     * Los vértices secos del borde de orilla toman la elevación de agua del vecino húmedo
     * con depth=0 para recortarse limpiamente contra el lecho del terreno por depth-test. */
    size_t gridN = (size_t)res * (size_t)res;
    int32_t *remap = malloc(gridN * sizeof(int32_t));
    if (!remap) return false;
    for (size_t i = 0; i < gridN; ++i) remap[i] = -1;

    uint32_t vCap = hy->totalWetCells * 2u + 256u;
    uint32_t iCap = hy->totalWetCells * 6u + 512u;
    WaterGpuVertex *verts = malloc((size_t)vCap * sizeof(WaterGpuVertex));
    uint32_t *indices = malloc((size_t)iCap * sizeof(uint32_t));
    if (!verts || !indices) { free(remap); free(verts); free(indices); return false; }

    uint32_t vCount = 0u, iCount = 0u;
    for (uint32_t iz = 0; iz < cells; ++iz) {
        for (uint32_t ix = 0; ix < cells; ++ix) {
            size_t corners[4] = {
                (size_t)iz * res + ix,
                (size_t)iz * res + (ix + 1u),
                (size_t)(iz + 1u) * res + ix,
                (size_t)(iz + 1u) * res + (ix + 1u)
            };
            float sumSurf = 0.0f;
            int wetCount = 0;
            uint16_t refBody = 0u;
            for (int c = 0; c < 4; ++c) {
                if (hy->waterBodyId[corners[c]] != 0u) {
                    sumSurf += hy->waterSurfaceY[corners[c]];
                    ++wetCount;
                    if (!refBody) refBody = hy->waterBodyId[corners[c]];
                }
            }
            if (wetCount == 0) continue;
            float avgSurf = sumSurf / (float)wetCount;

            uint32_t vi[4];
            for (int c = 0; c < 4; ++c) {
                size_t gi = corners[c];
                if (remap[gi] < 0) {
                    if (vCount >= vCap) {
                        vCap *= 2u;
                        WaterGpuVertex *nv = realloc(verts, (size_t)vCap * sizeof(WaterGpuVertex));
                        if (!nv) { free(remap); free(verts); free(indices); return false; }
                        verts = nv;
                    }
                    uint32_t gx = (uint32_t)(gi % res);
                    uint32_t gz = (uint32_t)(gi / res);
                    uint16_t bid = hy->waterBodyId[gi];
                    float surfY = (bid != 0u) ? hy->waterSurfaceY[gi] : avgSurf;
                    float depth = (bid != 0u) ? hy->waterDepth[gi] : fmaxf(0.0f, surfY - tf->heights[gi]);
                    const WorldWaterBody *wb = WorldHydrology_GetBody(hy, bid ? bid : refBody);
                    bool contaminated = (wb && strstr(wb->substanceId, "contaminated") != NULL);

                    WaterGpuVertex *v = &verts[vCount];
                    v->pos[0] = tf->originX + (float)gx * tf->spacing;
                    v->pos[1] = surfY;
                    v->pos[2] = tf->originZ + (float)gz * tf->spacing;
                    v->depth = depth;
                    v->flow[0] = hy->flowX ? hy->flowX[gi] : 0.0f;
                    v->flow[1] = hy->flowZ ? hy->flowZ[gi] : 0.0f;
                    if (contaminated) {
                        v->bodyTint[0] = 0.28f; v->bodyTint[1] = 0.52f; v->bodyTint[2] = 0.34f;
                    } else {
                        v->bodyTint[0] = 0.16f; v->bodyTint[1] = 0.46f; v->bodyTint[2] = 0.72f;
                    }
                    remap[gi] = (int32_t)vCount++;
                }
                vi[c] = (uint32_t)remap[gi];
            }

            if (iCount + 6u > iCap) {
                iCap *= 2u;
                uint32_t *ni = realloc(indices, (size_t)iCap * sizeof(uint32_t));
                if (!ni) { free(remap); free(verts); free(indices); return false; }
                indices = ni;
            }
            /* Misma diagonal CCW desde +Y que TerrainHeightfield: T0=(0,3,1), T1=(0,2,3). */
            indices[iCount++] = vi[0]; indices[iCount++] = vi[3]; indices[iCount++] = vi[1];
            indices[iCount++] = vi[0]; indices[iCount++] = vi[2]; indices[iCount++] = vi[3];
        }
    }
    free(remap);

    if (vCount == 0u || iCount == 0u) {
        free(verts); free(indices);
        impl->worldEpoch = world->epochId;
        impl->worldFingerprint = world->dataFingerprint;
        return true;
    }

    GLint prevVao = 0, prevBuf = 0, prevEbo = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);

    glGenVertexArrays(1, &impl->vao);
    glGenBuffers(1, &impl->vbo);
    glGenBuffers(1, &impl->ebo);
    glBindVertexArray(impl->vao);
    glBindBuffer(GL_ARRAY_BUFFER, impl->vbo);
    size_t vBytes = (size_t)vCount * sizeof(WaterGpuVertex);
    size_t iBytes = (size_t)iCount * sizeof(uint32_t);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vBytes, verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)iBytes, indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, depth));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, flow));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, bodyTint));

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);

    free(verts);
    free(indices);
    impl->indexCount = iCount;
    impl->worldEpoch = world->epochId;
    impl->worldFingerprint = world->dataFingerprint;
    impl->stats.triangles = iCount / 3u;
    impl->stats.vramBytes = vBytes + iBytes;
    return true;
}

bool WaterRenderer_Render(WaterRenderer *wr, const ICamera *camera, float timeSeconds) {
    if (!wr || !wr->impl || !camera) return false;
    WaterRendererImpl *impl = (WaterRendererImpl *)wr->impl;
    impl->stats.drawCalls = 0u;
    if (!impl->vao || impl->indexCount == 0u || !impl->program) return true;

    GLint prevProg = 0, prevVao = 0, prevEbo = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(impl->program);
    if (impl->uCamPosLoc >= 0) glUniform3f(impl->uCamPosLoc, camera->position.x, camera->position.y, camera->position.z);
    if (impl->uTimeLoc >= 0) glUniform1f(impl->uTimeLoc, timeSeconds);
    if (impl->uQualityLoc >= 0) glUniform1i(impl->uQualityLoc, impl->qualityTier);

    glBindVertexArray(impl->vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl->ebo);
    glDrawElements(GL_TRIANGLES, (GLsizei)impl->indexCount, GL_UNSIGNED_INT, 0);
    impl->stats.drawCalls = 1u;

    /* Restaurar estado OpenGL completo para pasadas posteriores (HUD/overlays). */
    glDepthMask(prevDepthMask);
    if (!prevBlend) glDisable(GL_BLEND);
    if (prevCull) glEnable(GL_CULL_FACE);
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);
    glUseProgram((GLuint)prevProg);
    return true;
}

WaterRenderStats WaterRenderer_GetStats(const WaterRenderer *wr) {
    if (!wr || !wr->impl) return (WaterRenderStats){0};
    return ((const WaterRendererImpl *)wr->impl)->stats;
}
