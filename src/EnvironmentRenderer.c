/**
 * @file EnvironmentRenderer.c
 * @brief Implementación compartida de renderizado de entorno (terreno, agua, rocas, minimapa y picking).
 */
#define GL_GLEXT_PROTOTYPES
#include "EnvironmentRenderer.h"
#include "OpenGLRenderer.h"
#include "WorldHydrology.h"

#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { ENV_TIMER_SLOTS = 8, ENV_TIMER_LAG = 3 };

typedef struct EnvironmentPassTimer {
    GLuint queries[ENV_TIMER_SLOTS * 2];
    bool pending[ENV_TIMER_SLOTS];
    unsigned cursor;
    double gpuTotalMs, cpuTotalMs;
    size_t gpuSamples, cpuSamples;
} EnvironmentPassTimer;

static double environmentNowMs(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec * 1000.0 + (double)time.tv_nsec * 1e-6;
}

static double beginPass(EnvironmentPassTimer *timer) {
    if (!timer->queries[0]) glGenQueries(ENV_TIMER_SLOTS * 2, timer->queries);
    unsigned i = (timer->cursor + ENV_TIMER_SLOTS - ENV_TIMER_LAG) % ENV_TIMER_SLOTS;
    for (unsigned k = 0; k < ENV_TIMER_SLOTS - ENV_TIMER_LAG + 1u; ++k, i = (i + ENV_TIMER_SLOTS - 1u) % ENV_TIMER_SLOTS) {
        if (!timer->pending[i]) continue;
        GLint ready = 0;
        glGetQueryObjectiv(timer->queries[2u * i + 1u], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            GLuint64 begin, end;
            glGetQueryObjectui64v(timer->queries[2u * i], GL_QUERY_RESULT, &begin);
            glGetQueryObjectui64v(timer->queries[2u * i + 1u], GL_QUERY_RESULT, &end);
            timer->gpuTotalMs += (double)(end - begin) * 1e-6;
            ++timer->gpuSamples;
            timer->pending[i] = false;
        }
    }
    if (!timer->pending[timer->cursor]) glQueryCounter(timer->queries[2u * timer->cursor], GL_TIMESTAMP);
    return environmentNowMs();
}

static void endPass(EnvironmentPassTimer *timer, double start) {
    timer->cpuTotalMs += environmentNowMs() - start;
    ++timer->cpuSamples;
    if (!timer->pending[timer->cursor]) {
        glQueryCounter(timer->queries[2u * timer->cursor + 1u], GL_TIMESTAMP);
        timer->pending[timer->cursor] = true;
    }
    timer->cursor = (timer->cursor + 1u) % ENV_TIMER_SLOTS;
}

static EnvironmentPassStats passStats(const EnvironmentPassTimer *timer) {
    return (EnvironmentPassStats){
        .cpuMeanMs = timer->cpuSamples ? timer->cpuTotalMs / (double)timer->cpuSamples : 0.0,
        .gpuMeanMs = timer->gpuSamples ? timer->gpuTotalMs / (double)timer->gpuSamples : 0.0,
        .gpuSamples = timer->gpuSamples,
    };
}

typedef struct EnvironmentRendererImpl {
    TerrainMaterials materials;
    EnvironmentPassTimer terrainTime, rockTime, waterTime;
    bool profiling;
    double uploadMs;
    TerrainRenderer terrain;
    WaterRenderer water;
    RockVisualCache rocks;
    const WorldStaticData *boundWorld;
    uint32_t worldEpoch;
    uint64_t worldFingerprint;
    bool isProcedural;

    GLuint minimapTex;
    uint32_t minimapRes;

    bool rockDetails;
    bool showShoreSites;
    bool showRockCollision;
    bool showTerrainGrid;
} EnvironmentRendererImpl;

static void destroyMinimapTexture(EnvironmentRendererImpl *impl) {
    if (impl && impl->minimapTex) {
        glDeleteTextures(1, &impl->minimapTex);
        impl->minimapTex = 0u;
        impl->minimapRes = 0u;
    }
}

static bool buildMinimapTexture(EnvironmentRendererImpl *impl, const WorldStaticData *world) {
    if (!world || world->recipe.flat) return true;

    const uint32_t texSize = 256u;
    uint8_t *rgba = (uint8_t *)malloc((size_t)texSize * texSize * 4u);
    if (!rgba) return false;

    const TerrainHeightfield *tf = &world->terrain;
    const WorldHydrology *hy = &world->hydrology;
    const WorldBiomeField *bm = &world->biome;
    float hSpan = fmaxf(0.1f, tf->maxHeight - tf->minHeight);

    for (uint32_t py = 0; py < texSize; ++py) {
        /* py=0 arriba en la pantalla HUD corresponde a z = originZ (norte). */
        float vz = ((float)py + 0.5f) / (float)texSize;
        float wz = tf->originZ + vz * tf->span;
        uint32_t gz = (uint32_t)(vz * (float)(tf->resolution - 1u));
        if (gz >= tf->resolution) gz = tf->resolution - 1u;

        for (uint32_t px = 0; px < texSize; ++px) {
            float vx = ((float)px + 0.5f) / (float)texSize;
            float wx = tf->originX + vx * tf->span;
            uint32_t gx = (uint32_t)(vx * (float)(tf->resolution - 1u));
            if (gx >= tf->resolution) gx = tf->resolution - 1u;

            size_t idx = (size_t)gz * tf->resolution + gx;
            float h = 0.0f;
            Vector3 n = {0.0f, 1.0f, 0.0f};
            Terrain_SampleHeight(tf, wx, wz, &h);
            Terrain_SampleSmoothNormal(tf, wx, wz, &n);
            float hNorm = (h - tf->minHeight) / hSpan;
            float hillShade = 0.58f + 0.42f * fmaxf(0.0f, n.x * 0.5f + n.y * 0.8f + n.z * 0.3f);

            float r = 0.35f, g = 0.45f, b = 0.32f;
            WaterSample water;
            if (WorldHydrology_SampleWater(hy, tf, wx, wz, &water) && water.wet) {
                float dF = fminf(1.0f, water.depth / 2.2f);
                r = 0.24f * (1.0f - dF) + 0.08f * dF;
                g = 0.56f * (1.0f - dF) + 0.28f * dF;
                b = 0.78f * (1.0f - dF) + 0.58f * dF;
            } else {
                const uint8_t *m = bm->materialRGBA ? &bm->materialRGBA[idx * 4u] : NULL;
                if (m) {
                    float wG = (float)m[0] / 255.0f;
                    float wS = (float)m[1] / 255.0f;
                    float wR = (float)m[2] / 255.0f;
                    float wM = (float)m[3] / 255.0f;
                    r = (wG * 0.27f + wS * 0.52f + wR * 0.46f + wM * 0.34f) * hillShade;
                    g = (wG * 0.48f + wS * 0.44f + wR * 0.45f + wM * 0.30f) * hillShade;
                    b = (wG * 0.22f + wS * 0.31f + wR * 0.43f + wM * 0.22f) * hillShade;
                }
                /* Sutil sombreado hipsométrico y curvas de nivel. */
                float elevBoost = 0.88f + 0.24f * hNorm;
                r *= elevBoost; g *= elevBoost; b *= elevBoost;
                float contour = fabsf(fmodf(h * 0.4f, 1.0f));
                if (contour < 0.045f) {
                    r *= 0.84f; g *= 0.84f; b *= 0.84f;
                }
            }

            /* Marcar rocas colisionables oscuras en el minimapa. */
            if (world->geology.collidingRockCount > 0u &&
                WorldGeology_CollidesDisc(&world->geology, (Vector3){wx, h, wz}, 0.25f)) {
                r = r * 0.45f + 0.16f;
                g = g * 0.45f + 0.15f;
                b = b * 0.45f + 0.15f;
            }

            size_t out = ((size_t)py * texSize + px) * 4u;
            rgba[out + 0u] = (uint8_t)(fminf(1.0f, fmaxf(0.0f, r)) * 255.0f);
            rgba[out + 1u] = (uint8_t)(fminf(1.0f, fmaxf(0.0f, g)) * 255.0f);
            rgba[out + 2u] = (uint8_t)(fminf(1.0f, fmaxf(0.0f, b)) * 255.0f);
            rgba[out + 3u] = 242u;
        }
    }

    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)texSize, (GLsizei)texSize,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    GLint textureSize = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &textureSize);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    free(rgba);
    if (textureSize != (GLint)texSize) {
        glDeleteTextures(1, &texture);
        return false;
    }
    destroyMinimapTexture(impl);
    impl->minimapTex = texture;
    impl->minimapRes = texSize;
    return true;
}

bool EnvironmentRenderer_Init(EnvironmentRenderer *er) {
    if (!er) return false;
    memset(er, 0, sizeof(*er));
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)calloc(1, sizeof(EnvironmentRendererImpl));
    if (!impl) return false;
    if (!TerrainRenderer_Init(&impl->terrain) ||
        !WaterRenderer_Init(&impl->water) ||
        !RockVisualCache_Init(&impl->rocks)) {
        TerrainRenderer_Destroy(&impl->terrain);
        WaterRenderer_Destroy(&impl->water);
        RockVisualCache_Destroy(&impl->rocks);
        free(impl);
        return false;
    }
    impl->rockDetails = true;
    er->impl = impl;
    return true;
}

void EnvironmentRenderer_Destroy(EnvironmentRenderer *er) {
    if (!er || !er->impl) return;
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)er->impl;
    destroyMinimapTexture(impl);
    TerrainMaterials_Free(&impl->materials);
    glDeleteQueries(ENV_TIMER_SLOTS * 2, impl->terrainTime.queries);
    glDeleteQueries(ENV_TIMER_SLOTS * 2, impl->rockTime.queries);
    glDeleteQueries(ENV_TIMER_SLOTS * 2, impl->waterTime.queries);
    TerrainRenderer_Destroy(&impl->terrain);
    WaterRenderer_Destroy(&impl->water);
    RockVisualCache_Destroy(&impl->rocks);
    if (impl->boundWorld) {
        WorldStaticData_Release(impl->boundWorld);
        impl->boundWorld = NULL;
    }
    free(impl);
    er->impl = NULL;
}

bool EnvironmentRenderer_SyncWorld(EnvironmentRenderer *er, const WorldStaticData *world) {
    if (!er || !er->impl) return false;
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)er->impl;

    bool wantProcedural = (world != NULL && !world->recipe.flat);
    uint32_t newEpoch = world ? world->epochId : 0u;
    uint64_t newFp = world ? world->dataFingerprint : 0u;

    if (impl->boundWorld == world &&
        impl->isProcedural == wantProcedural &&
        impl->worldEpoch == newEpoch &&
        impl->worldFingerprint == newFp) {
        return true;
    }

    const WorldStaticData *previous = impl->boundWorld;
    double uploading = environmentNowMs();
    if (wantProcedural) {
        TerrainMaterials_Load(&impl->materials);
        if (!TerrainRenderer_SyncWorld(&impl->terrain, world) ||
            !WaterRenderer_SyncWorld(&impl->water, world) ||
            !RockVisualCache_SyncWorld(&impl->rocks, world) ||
            !buildMinimapTexture(impl, world)) {
            /* El contexto sigue en este hilo. Volver a la época publicada. */
            TerrainRenderer_SyncWorld(&impl->terrain, previous);
            WaterRenderer_SyncWorld(&impl->water, previous);
            RockVisualCache_SyncWorld(&impl->rocks, previous);
            return false;
        }
    } else {
        TerrainRenderer_SyncWorld(&impl->terrain, NULL);
        WaterRenderer_SyncWorld(&impl->water, NULL);
        RockVisualCache_SyncWorld(&impl->rocks, NULL);
        destroyMinimapTexture(impl);
    }
    if (world != previous) {
        if (world) WorldStaticData_Retain(world);
        if (previous) WorldStaticData_Release(previous);
        impl->boundWorld = world;
    }
    impl->worldEpoch = newEpoch;
    impl->worldFingerprint = newFp;
    impl->isProcedural = wantProcedural;
    impl->uploadMs = environmentNowMs() - uploading;
    return true;
}

bool EnvironmentRenderer_IsProcedural(const EnvironmentRenderer *er) {
    if (!er || !er->impl) return false;
    return ((const EnvironmentRendererImpl *)er->impl)->isProcedural;
}

const WorldStaticData *EnvironmentRenderer_GetWorld(const EnvironmentRenderer *er) {
    if (!er || !er->impl) return NULL;
    return ((const EnvironmentRendererImpl *)er->impl)->boundWorld;
}

void EnvironmentRenderer_SetDebugMode(EnvironmentRenderer *er, TerrainDebugMode mode) {
    if (!er || !er->impl) return;
    TerrainRenderer_SetDebugMode(&((EnvironmentRendererImpl *)er->impl)->terrain, mode);
}

TerrainDebugMode EnvironmentRenderer_GetDebugMode(const EnvironmentRenderer *er) {
    if (!er || !er->impl) return TERRAIN_DEBUG_NATURAL;
    return TerrainRenderer_GetDebugMode(&((const EnvironmentRendererImpl *)er->impl)->terrain);
}

void EnvironmentRenderer_ForceTerrainLod0(EnvironmentRenderer *er, bool enabled) {
    if (er && er->impl) TerrainRenderer_ForceLod0(&((EnvironmentRendererImpl *)er->impl)->terrain, enabled);
}

void EnvironmentRenderer_SetQuality(EnvironmentRenderer *er, float terrainLodScale,
                                    int waterQualityTier, bool wireframe) {
    if (!er || !er->impl) return;
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)er->impl;
    TerrainRenderer_SetQuality(&impl->terrain, terrainLodScale, wireframe);
    impl->rockDetails = terrainLodScale >= 0.85f;
    WaterRenderer_SetQuality(&impl->water, waterQualityTier);
}

void EnvironmentRenderer_SetOverlays(EnvironmentRenderer *er, bool showShoreSites,
                                     bool showRockCollision, bool showTerrainGrid) {
    if (!er || !er->impl) return;
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)er->impl;
    impl->showShoreSites = showShoreSites;
    impl->showRockCollision = showRockCollision;
    impl->showTerrainGrid = showTerrainGrid;
}

void EnvironmentRenderer_SetProfiling(EnvironmentRenderer *er, bool enabled) {
    if (er && er->impl) ((EnvironmentRendererImpl *)er->impl)->profiling = enabled;
}

static void drawLowAngleSky(const ICamera *camera) {
    if (camera->fov <= 0.0f) return;
    Vector3 direction = Vec3_Normalize(Vec3_Sub(camera->target, camera->position));
    float blend = fmaxf(0.0f, fminf(1.0f, (direction.y + 0.36f) / 0.16f));
    if (blend <= 0.0f) return;
    blend *= blend * (3.0f - 2.0f * blend);

    GLint program = 0, matrixMode = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
    glUseProgram(0);
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT |
                 GL_COLOR_BUFFER_BIT | GL_LIGHTING_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glDepthMask(GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glShadeModel(GL_SMOOTH);
    glBegin(GL_QUADS);
    glColor4f(0.58f, 0.66f, 0.71f, blend);
    glVertex2f(-1, -1);
    glVertex2f(1, -1);
    glColor4f(0.38f, 0.52f, 0.65f, blend);
    glVertex2f(1, 1);
    glVertex2f(-1, 1);
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode((GLenum)matrixMode);
    glPopAttrib();
    glUseProgram((GLuint)program);
}

bool EnvironmentRenderer_RenderOpaque(EnvironmentRenderer *er, Renderer3D *renderer,
                                      const ICamera *camera,
                                      float flatHalfSize, const Mesh *flatGroundMesh,
                                      float maxSlopeNavRadians, float maxWadeNavDepth) {
    if (!er || !er->impl || !camera) return false;
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)er->impl;

    if (impl->isProcedural && impl->boundWorld) {
        if (TerrainRenderer_GetDebugMode(&impl->terrain) == TERRAIN_DEBUG_NATURAL) drawLowAngleSky(camera);
        double start = impl->profiling ? beginPass(&impl->terrainTime) : 0.0;
        bool ok = TerrainRenderer_Render(&impl->terrain, camera, &impl->materials,
                                         maxSlopeNavRadians, maxWadeNavDepth);
        if (impl->profiling) endPass(&impl->terrainTime, start);
        if (!ok) return false;
        start = impl->profiling ? beginPass(&impl->rockTime) : 0.0;
        ok = RockVisualCache_Render(&impl->rocks, camera, &impl->materials, impl->rockDetails);
        if (impl->profiling) endPass(&impl->rockTime, start);
        if (!ok) return false;
        return true;
    }

    /* Modo plano de compatibilidad: disco/suelo + rejilla exactamente como antes. */
    if (renderer && flatGroundMesh) {
        float gs = flatHalfSize * 1.15f;
        OpenGLRenderer_ModelTransform((Vector3){0.0f, -0.18f, 0.0f}, 0.0f, gs / 26.0f, Vec3_Zero());
        renderer->renderMesh(renderer, flatGroundMesh);
        OpenGLRenderer_ModelTransform(Vec3_Zero(), 0.0f, 1.0f, Vec3_Zero());
    }

    GLboolean prevLight = glIsEnabled(GL_LIGHTING);
    glDisable(GL_LIGHTING);
    glColor3f(0.32f, 0.38f, 0.29f);
    glBegin(GL_LINES);
    float step = (flatHalfSize > 80.0f) ? 16.0f : 8.0f;
    for (float g = -flatHalfSize; g <= flatHalfSize + 0.01f; g += step) {
        glVertex3f(g, 0.015f, -flatHalfSize); glVertex3f(g, 0.015f, flatHalfSize);
        glVertex3f(-flatHalfSize, 0.015f, g); glVertex3f(flatHalfSize, 0.015f, g);
    }
    glEnd();
    if (prevLight) glEnable(GL_LIGHTING);
    return true;
}

bool EnvironmentRenderer_RenderWater(EnvironmentRenderer *er, const ICamera *camera, float timeSeconds) {
    if (!er || !er->impl || !camera) return false;
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)er->impl;
    if (!impl->isProcedural) return true;
    double start = impl->profiling ? beginPass(&impl->waterTime) : 0.0;
    bool ok = WaterRenderer_Render(&impl->water, camera, &impl->materials, timeSeconds,
                                impl->showShoreSites ||
                                TerrainRenderer_GetDebugMode(&impl->terrain) == TERRAIN_DEBUG_HYDROLOGY);
    if (impl->profiling) endPass(&impl->waterTime, start);
    return ok;
}

void EnvironmentRenderer_RenderOverlays(EnvironmentRenderer *er, const ICamera *camera) {
    if (!er || !er->impl || !camera) return;
    EnvironmentRendererImpl *impl = (EnvironmentRendererImpl *)er->impl;
    if (!impl->isProcedural || !impl->boundWorld) return;

    const WorldStaticData *w = impl->boundWorld;
    TerrainDebugMode mode = TerrainRenderer_GetDebugMode(&impl->terrain);

    if (impl->showRockCollision || mode == TERRAIN_DEBUG_COLLISION) {
        RockVisualCache_RenderCollisionDebug(&impl->rocks, w, camera);
    }

    GLint prevProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glUseProgram(0);
    GLboolean prevLight = glIsEnabled(GL_LIGHTING);
    glDisable(GL_LIGHTING);

    if (impl->showShoreSites || mode == TERRAIN_DEBUG_HYDROLOGY) {
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        for (uint32_t i = 0; i < w->hydrology.shoreSiteCount; ++i) {
            const WorldShoreSite *s = &w->hydrology.shoreSites[i];
            float dx = s->bankApproachPoint.x - camera->position.x;
            float dz = s->bankApproachPoint.z - camera->position.z;
            if (dx * dx + dz * dz > 180.0f * 180.0f) continue;
            /* Línea cian desde punto de aproximación en tierra hasta punto de contacto en agua. */
            glColor3f(0.12f, 0.92f, 0.96f);
            glVertex3f(s->bankApproachPoint.x, s->bankApproachPoint.y + 0.14f, s->bankApproachPoint.z);
            glColor3f(0.95f, 0.95f, 0.25f);
            glVertex3f(s->wetContactPoint.x, s->wetContactPoint.y + 0.14f, s->wetContactPoint.z);
            /* Estaca vertical pequeña en el punto de aproximación. */
            glColor3f(0.18f, 0.95f, 0.45f);
            glVertex3f(s->bankApproachPoint.x, s->bankApproachPoint.y + 0.05f, s->bankApproachPoint.z);
            glVertex3f(s->bankApproachPoint.x, s->bankApproachPoint.y + 0.85f, s->bankApproachPoint.z);
        }
        glEnd();
        glLineWidth(1.0f);
    }

    if (impl->showTerrainGrid) {
        const TerrainHeightfield *tf = &w->terrain;
        float half = tf->halfSize;
        float step = (tf->span > 200.0f) ? 16.0f : 8.0f;
        float seg = fmaxf(tf->spacing * 2.0f, 1.5f);
        glColor4f(0.18f, 0.22f, 0.18f, 0.55f);
        for (float gx = -half; gx <= half + 0.01f; gx += step) {
            glBegin(GL_LINE_STRIP);
            for (float z = -half; z <= half + 0.01f; z += seg) {
                float y = 0.0f;
                Terrain_SampleHeight(tf, gx, z, &y);
                glVertex3f(gx, y + 0.08f, z);
            }
            glEnd();
        }
        for (float gz = -half; gz <= half + 0.01f; gz += step) {
            glBegin(GL_LINE_STRIP);
            for (float x = -half; x <= half + 0.01f; x += seg) {
                float y = 0.0f;
                Terrain_SampleHeight(tf, x, gz, &y);
                glVertex3f(x, y + 0.08f, gz);
            }
            glEnd();
        }
    }

    if (prevLight) glEnable(GL_LIGHTING);
    glUseProgram((GLuint)prevProg);
}

void EnvironmentRenderer_DrawMinimapBackground(const EnvironmentRenderer *er, float x, float y, float size) {
    if (!er || !er->impl) return;
    const EnvironmentRendererImpl *impl = (const EnvironmentRendererImpl *)er->impl;

    if (!impl->isProcedural || !impl->minimapTex) {
        glColor4f(0.06f, 0.09f, 0.08f, 0.86f);
        glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x + size, y);
        glVertex2f(x + size, y + size); glVertex2f(x, y + size);
        glEnd();
        return;
    }

    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, impl->minimapTex);
    glColor4f(1.0f, 1.0f, 1.0f, 0.94f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x + size, y);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x + size, y + size);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + size);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glDisable(GL_TEXTURE_2D);
}

bool EnvironmentRenderer_PickGroundRay(const EnvironmentRenderer *er, Vector3 rayOrigin, Vector3 rayDir,
                                       float maxDist, Vector3 *outHitPos, uint16_t *outWaterBodyId) {
    if (outWaterBodyId) *outWaterBodyId = 0u;
    if (!er || !er->impl) return false;
    const EnvironmentRendererImpl *impl = (const EnvironmentRendererImpl *)er->impl;

    if (impl->isProcedural && impl->boundWorld) {
        Vector3 rayEnd = {
            rayOrigin.x + rayDir.x * maxDist,
            rayOrigin.y + rayDir.y * maxDist,
            rayOrigin.z + rayDir.z * maxDist
        };
        Vector3 hitPos = {0};
        const WorldStaticData *world = impl->boundWorld;
        bool hitGround = Terrain_RaycastSegment(&world->terrain, rayOrigin, rayEnd, 0.005f, NULL, &hitPos);
        if (outWaterBodyId && world->hydrology.surfaceBodyId &&
            WorldHydrology_RaycastWater(&world->hydrology, &world->terrain,
                                         rayOrigin, hitGround ? hitPos : rayEnd, outHitPos, outWaterBodyId)) {
            return true;
        }
        if (!hitGround) return false;
        if (outHitPos) *outHitPos = hitPos;
        if (outWaterBodyId && !world->hydrology.surfaceBodyId) {
            WaterSample ws;
            if (WorldHydrology_SampleWater(&impl->boundWorld->hydrology, &impl->boundWorld->terrain,
                                           hitPos.x, hitPos.z, &ws) && ws.wet) {
                *outWaterBodyId = ws.waterBodyId;
            }
        }
        return true;
    }

    /* Plano y = 0 para modo plano. */
    if (fabsf(rayDir.y) < 1e-5f) return false;
    float t = -rayOrigin.y / rayDir.y;
    if (t < 0.0f || t > maxDist) return false;
    if (outHitPos) {
        outHitPos->x = rayOrigin.x + rayDir.x * t;
        outHitPos->y = 0.0f;
        outHitPos->z = rayOrigin.z + rayDir.z * t;
    }
    return true;
}

float EnvironmentRenderer_SampleGroundHeight(const EnvironmentRenderer *er, float x, float z) {
    if (!er || !er->impl) return 0.0f;
    const EnvironmentRendererImpl *impl = (const EnvironmentRendererImpl *)er->impl;
    if (impl->isProcedural && impl->boundWorld) {
        float h = 0.0f;
        if (Terrain_SampleHeight(&impl->boundWorld->terrain, x, z, &h)) return h;
    }
    return 0.0f;
}

void EnvironmentRenderer_DrawTerrainCircle(const EnvironmentRenderer *er, float cx, float cz,
                                           float radius, float liftY, int segments) {
    if (segments < 8) segments = 8;
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; ++i) {
        float a = (float)i * (2.0f * (float)M_PI / (float)segments);
        float px = cx + cosf(a) * radius;
        float pz = cz + sinf(a) * radius;
        float py = EnvironmentRenderer_SampleGroundHeight(er, px, pz) + liftY;
        glVertex3f(px, py, pz);
    }
    glEnd();
}

void EnvironmentRenderer_DrawTerrainPath(const EnvironmentRenderer *er, const Vector3 *pts,
                                         uint32_t count, float liftY) {
    if (!pts || count < 2u) return;
    glBegin(GL_LINE_STRIP);
    for (uint32_t i = 0; i < count; ++i) {
        float py = EnvironmentRenderer_SampleGroundHeight(er, pts[i].x, pts[i].z) + liftY;
        glVertex3f(pts[i].x, py, pts[i].z);
    }
    glEnd();
}

EnvironmentRenderStats EnvironmentRenderer_GetStats(const EnvironmentRenderer *er) {
    EnvironmentRenderStats st;
    memset(&st, 0, sizeof(st));
    if (!er || !er->impl) return st;
    const EnvironmentRendererImpl *impl = (const EnvironmentRendererImpl *)er->impl;
    st.isProcedural = impl->isProcedural;
    if (impl->isProcedural) {
        st.terrain = TerrainRenderer_GetStats(&impl->terrain);
        st.water = WaterRenderer_GetStats(&impl->water);
        st.rocks = RockVisualCache_GetStats(&impl->rocks);
        st.totalTriangles = st.terrain.renderedTriangles + st.water.triangles + st.rocks.renderedTriangles;
        st.totalDrawCalls = st.terrain.drawCalls + st.water.drawCalls + st.rocks.drawCalls;
        st.materialVramBytes = impl->materials.vramBytes;
        st.lastUploadMs = impl->uploadMs;
        st.terrainPass = passStats(&impl->terrainTime);
        st.rockPass = passStats(&impl->rockTime);
        st.waterPass = passStats(&impl->waterTime);
        st.totalVramBytes = st.terrain.vramBytes + st.water.vramBytes + st.rocks.vramBytes + impl->materials.vramBytes;
        if (impl->minimapTex) {
            st.totalVramBytes += (size_t)impl->minimapRes * impl->minimapRes * 4u;
        }
    }
    return st;
}
