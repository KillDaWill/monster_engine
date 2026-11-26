/**
 * @file TerrainRenderer.c
 * @brief Renderizado chunked 32x32 con 4 niveles LOD, costura sin T-junctions,
 *        culling de frustum y shader GLSL 330 de splatting multi-material + depuración.
 */
#define GL_GLEXT_PROTOTYPES
#include "TerrainRenderer.h"
#include "TerrainLodIndices.h"
#include "environment_glsl.inc"
#include "SceneLighting.h"
#include "TerrainGenerator.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TERRAIN_CHUNK_CELLS 32u
#define TERRAIN_LOD_LEVELS 4u

typedef struct TerrainGpuVertex {
    float pos[3];
    float normal[3];
    float matWeights[4]; /* grass, drySoil, rock, wetMud */
    float aux[4];        /* altNorm, slopeRad, waterDepth, fertility */
    float flow[2];       /* flowX, flowZ */
} TerrainGpuVertex;

typedef struct TerrainChunk {
    uint32_t cx, cz;
    uint32_t x0, z0;
    uint32_t cellsX, cellsZ;
    AABB3D bounds;
    Vector3 center;
    float radius;
    float minWetDepth;
    float error[TERRAIN_LOD_LEVELS];
    uint8_t wetEdges, dryEdges;
    bool shoreline;
    bool pinned;
    uint32_t currentLod, colorLod;
    GLuint vao;
    GLuint vbo;
    uint32_t vertexCount;
    /* EBO dinámico sólo si es un borde no cuadrado; chunks estándar 32x32 comparten EBOs globales! */
    bool standardSize;
    GLuint customEbo[16];
    uint32_t customIndexCount[16];
} TerrainChunk;

/* 16 combinaciones de costura de borde (bits: 0=North/-Z, 1=South/+Z, 2=West/-X, 3=East/+X) por nivel LOD. */
typedef struct SharedLodIndices {
    GLuint ebo[TERRAIN_LOD_LEVELS][16];
    uint32_t count[TERRAIN_LOD_LEVELS][16];
} SharedLodIndices;

typedef struct TerrainRendererImpl {
    GLuint program, depthProgram;
    bool depthPass;
    bool programFailed;
    GLint uDebugModeLoc;
    GLint uCamPosLoc;
    GLint uNavLimitsLoc;
    GLint uHeightRangeLoc;
    GLint materialTierLoc;
    TerrainMaterialBindings materialBindings;
    GLuint landscape, contacts;
    GLint contactsLoc, contactsMappingLoc;
    Vector3 contactsMapping;
    GLint landscapeLoc, landscapeMappingLoc;
    Vector3 landscapeMapping;

    uint64_t worldEpoch;
    uint64_t worldFingerprint;
    uint32_t gridChunksX, gridChunksZ;
    float minH, maxH;

    TerrainChunk *chunks;
    uint32_t chunkCount;
    SharedLodIndices sharedEbo;
    bool sharedEboReady;

    TerrainDebugMode debugMode;
    float lodQualityScale;
    bool wireframe;
    bool forceLod0;
    bool continuousWater;
    TerrainRenderStats stats;
} TerrainRendererImpl;

const char *TerrainDebugMode_Name(TerrainDebugMode mode) {
    switch (mode) {
    case TERRAIN_DEBUG_NATURAL:     return "NATURAL";
    case TERRAIN_DEBUG_ELEVATION:   return "ELEVATION";
    case TERRAIN_DEBUG_SLOPE:       return "SLOPE";
    case TERRAIN_DEBUG_HYDROLOGY:   return "HYDROLOGY";
    case TERRAIN_DEBUG_WATER_DEPTH: return "WATER_DEPTH";
    case TERRAIN_DEBUG_BIOME:       return "BIOME_FERTILITY";
    case TERRAIN_DEBUG_NAVIGATION:  return "NAVIGATION";
    case TERRAIN_DEBUG_COLLISION:   return "COLLISION_LOD";
    case TERRAIN_DEBUG_ALBEDO:     return "ALBEDO";
    case TERRAIN_DEBUG_NORMAL:     return "NORMAL";
    case TERRAIN_DEBUG_ROUGHNESS:  return "ROUGHNESS";
    case TERRAIN_DEBUG_WEIGHTS:    return "MATERIAL_WEIGHTS";
    case TERRAIN_DEBUG_SHADOW:     return "DIRECT_VISIBILITY";
    default:                       return "NATURAL";
    }
}

bool TerrainDebugMode_Parse(const char *name, TerrainDebugMode *mode) {
    const char *names[] = {"natural", "elevation", "slope", "hydrology", "depth", "biome",
                           "navigation", "collision", "albedo", "normal", "roughness", "weights", "shadow"};
    for (int i = 0; i < TERRAIN_DEBUG_MODE_COUNT; ++i) {
        if (strcmp(name, names[i]) == 0) {
            *mode = (TerrainDebugMode)i;
            return true;
        }
    }
    return false;
}

static GLuint compileGlShaderParts(GLenum type, const char *const *parts, GLsizei count) {
    GLuint s = glCreateShader(type);
    if (!s) return 0u;
    glShaderSource(s, count, parts, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[TerrainRenderer] Shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0u;
    }
    return s;
}

static bool initTerrainShader(TerrainRendererImpl *impl) {
    if (impl->program) return true;
    if (impl->programFailed) return false;

    const char *vs =
        "#version 330 compatibility\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec3 aNormal;\n"
        "layout(location=2) in vec4 aMatWeights;\n"
        "layout(location=3) in vec4 aAux;\n"
        "layout(location=4) in vec2 aFlow;\n"
        "out vec3 vWorldPos;\n"
        "out vec3 vWorldNormal;\n"
        "out vec4 vMatWeights;\n"
        "out vec4 vAux;\n"
        "out vec2 vFlow;\n"
        "flat out vec4 vParallelView;\n"
        "void main() {\n"
        "  vWorldPos = aPos;\n"
        "  vWorldNormal = normalize(aNormal);\n"
        "  vMatWeights = aMatWeights;\n"
        "  vAux = aAux;\n"
        "  vFlow = aFlow;\n"
        "  vParallelView = vec4(gl_ModelViewMatrix[0][2], gl_ModelViewMatrix[1][2],\n"
        "                      gl_ModelViewMatrix[2][2], gl_ProjectionMatrix[3][3]);\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);\n"
        "}\n";

    const char *fsPart1 =
        "uniform int uDebugMode;\n"
        "uniform vec3 uCamPos;\n"
        "uniform vec2 uNavLimits;\n"   /* x=maxSlopeRad, y=maxWadeDepth */
        "uniform vec2 uHeightRange;\n" /* x=minH, y=maxH */
        "in vec3 vWorldPos;\n"
        "in vec3 vWorldNormal;\n"
        "in vec4 vMatWeights;\n"
        "in vec4 vAux;\n"
        "in vec2 vFlow;\n"
        "flat in vec4 vParallelView;\n"
        "out vec4 fragColor;\n"
        ;
    const char *mainPart1 =
        "void main() {\n"
        "  vec3 n = normalize(vWorldNormal);\n"
        "  vec3 sunDir = normalize(vec3(-0.48, 0.76, 0.44));\n"
        "  float ndl = max(dot(n, sunDir), 0.0);\n"
        "  float altNorm = vAux.x;\n"
        "  float slopeRad = vAux.y;\n"
        "  float wDepth = vAux.z;\n"
        "  float fert = vAux.w;\n"
        "  if (uDebugMode == 1) {\n"
        "    vec3 lo = vec3(0.14, 0.36, 0.24), mid = vec3(0.68, 0.66, 0.32), hi = vec3(0.86, 0.84, 0.82);\n"
        "    vec3 col = altNorm < 0.5 ? mix(lo, mid, altNorm * 2.0) : mix(mid, hi, (altNorm - 0.5) * 2.0);\n"
        "    float contour = abs(fract(vWorldPos.y * 0.5) - 0.5);\n"
        "    col *= mix(0.65, 1.0, smoothstep(0.02, 0.08, contour));\n"
        "    fragColor = vec4(sceneDiagnostic(col * (0.45 + 0.55 * ndl)), 1.0); return;\n"
        "  } else if (uDebugMode == 2) {\n"
        "    float t = clamp(slopeRad / 0.85, 0.0, 1.0);\n"
        "    vec3 col = t < 0.5 ? mix(vec3(0.18, 0.72, 0.30), vec3(0.92, 0.80, 0.20), t * 2.0)\n"
        "                       : mix(vec3(0.92, 0.80, 0.20), vec3(0.90, 0.16, 0.14), (t - 0.5) * 2.0);\n"
        "    fragColor = vec4(sceneDiagnostic(col * (0.45 + 0.55 * ndl)), 1.0); return;\n"
        "  } else if (uDebugMode == 3) {\n"
        "    float fl = length(vFlow);\n"
        "    vec3 col = wDepth > 0.01 ? mix(vec3(0.10, 0.45, 0.85), vec3(0.15, 0.92, 0.96), clamp(fl, 0.0, 1.0))\n"
        "                             : vec3(0.22 + 0.12 * ndl, 0.26 + 0.14 * ndl, 0.24 + 0.12 * ndl);\n"
        "    fragColor = vec4(sceneDiagnostic(col), 1.0); return;\n"
        "  } else if (uDebugMode == 4) {\n"
        "    vec3 col = wDepth > 0.01 ? mix(vec3(0.28, 0.82, 0.88), vec3(0.04, 0.14, 0.48), clamp(wDepth * 0.5, 0.0, 1.0))\n"
        "                             : vec3(0.32, 0.36, 0.32) * (0.45 + 0.55 * ndl);\n"
        "    fragColor = vec4(sceneDiagnostic(col), 1.0); return;\n"
        "  } else if (uDebugMode == 5) {\n"
        "    vec3 col = mix(vec3(0.56, 0.46, 0.32), vec3(0.16, 0.68, 0.24), fert);\n"
        "    if (wDepth > 0.01) col = vec3(0.14, 0.38, 0.56);\n"
        "    fragColor = vec4(sceneDiagnostic(col * (0.45 + 0.55 * ndl)), 1.0); return;\n"
        "  } else if (uDebugMode == 6) {\n"
        "    bool steep = slopeRad > uNavLimits.x;\n"
        "    bool deep = wDepth > uNavLimits.y;\n"
        "    vec3 col = deep ? vec3(0.12, 0.22, 0.72) : (steep ? vec3(0.84, 0.18, 0.16) :\n"
        "               (wDepth > 0.04 ? vec3(0.22, 0.74, 0.80) : vec3(0.22, 0.66, 0.30)));\n"
        "    fragColor = vec4(sceneDiagnostic(col * (0.45 + 0.55 * ndl)), 1.0); return;\n"
        "  }\n";
    const char *mainPart2 =
        "  vec3 viewDir = normalize(vParallelView.w > 0.5 ? vParallelView.xyz : uCamPos - vWorldPos);\n"
        "  vec3 lit = terrainPalette(vWorldPos, n, viewDir, vMatWeights, wDepth,\n"
        "                            dFdx(vWorldPos), dFdy(vWorldPos), uDebugMode);\n"
        "  fragColor = vec4(lit, 1.0);\n"
        "}\n";

    const char *vsParts[] = {vs};
    const char *fsParts[] = {"#version 330 compatibility\n", fsPart1, ENV_SCENE_LIGHTING, ENV_SCENE_MATERIAL_LIGHTING, ENV_TERRAIN_SHADOW, ENV_MATERIALS, ENV_PROJECTION,
                             ENV_TERRAIN_FINISH, ENV_TERRAIN_NATURAL, mainPart1, mainPart2};
    GLuint v = compileGlShaderParts(GL_VERTEX_SHADER, vsParts, 1);
    GLuint f = compileGlShaderParts(GL_FRAGMENT_SHADER, fsParts, (GLsizei)(sizeof(fsParts) / sizeof(fsParts[0])));
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
    impl->depthProgram = SceneLighting_LinkDepth(v);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        impl->programFailed = true;
        return false;
    }
    SceneLighting_LinkProgram(p);
    impl->program = p;
    impl->materialBindings = TerrainMaterials_GetBindings(p);
    impl->contactsLoc = glGetUniformLocation(p, "uContactMap");
    impl->contactsMappingLoc = glGetUniformLocation(p, "uContactMapping");
    impl->landscapeLoc = glGetUniformLocation(p, "uLandscape");
    impl->landscapeMappingLoc = glGetUniformLocation(p, "uLandscapeMapping");
    impl->materialTierLoc = glGetUniformLocation(p, "uMaterialTier");
    impl->uDebugModeLoc = glGetUniformLocation(p, "uDebugMode");
    impl->uCamPosLoc = glGetUniformLocation(p, "uCamPos");
    impl->uNavLimitsLoc = glGetUniformLocation(p, "uNavLimits");
    impl->uHeightRangeLoc = glGetUniformLocation(p, "uHeightRange");
    return true;
}

static inline uint32_t vIdx(uint32_t x, uint32_t z, uint32_t rowStride) {
    return z * rowStride + x;
}

static bool initSharedEbos(TerrainRendererImpl *impl) {
    if (impl->sharedEboReady) return true;
    GLint previous = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
    bool ok = true;
    for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVELS && ok; ++lod) {
        for (uint32_t mask = 0; mask < 16u && ok; ++mask) {
            uint32_t *idx = NULL, count = 0u;
            TerrainLodIndices_Build(TERRAIN_CHUNK_CELLS, TERRAIN_CHUNK_CELLS, lod, mask, &idx, &count);
            if (!idx) { ok = false; break; }
            glGenBuffers(1, &impl->sharedEbo.ebo[lod][mask]);
            glBindBuffer(GL_ARRAY_BUFFER, impl->sharedEbo.ebo[lod][mask]);
            GLint bytes = (GLint)(count * sizeof(uint32_t)), uploaded = 0;
            glBufferData(GL_ARRAY_BUFFER, bytes, idx, GL_STATIC_DRAW);
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &uploaded);
            ok = uploaded == bytes;
            impl->sharedEbo.count[lod][mask] = count;
            free(idx);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous);
    if (!ok) {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVELS; ++lod)
            glDeleteBuffers(16, impl->sharedEbo.ebo[lod]);
        impl->sharedEbo = (SharedLodIndices){0};
    }
    impl->sharedEboReady = ok;
    return ok;
}

static void freeChunks(TerrainRendererImpl *impl) {
    if (!impl) return;
    glDeleteTextures(1, &impl->landscape);
    impl->landscape = 0;
    if (!impl->chunks) return;
    for (uint32_t i = 0; i < impl->chunkCount; ++i) {
        if (impl->chunks[i].vao) glDeleteVertexArrays(1, &impl->chunks[i].vao);
        if (impl->chunks[i].vbo) glDeleteBuffers(1, &impl->chunks[i].vbo);
        if (impl->chunks[i].customEbo[0]) glDeleteBuffers(16, impl->chunks[i].customEbo);
    }
    free(impl->chunks);
    impl->chunks = NULL;
    impl->chunkCount = 0u;
    impl->gridChunksX = 0u;
    impl->gridChunksZ = 0u;
    impl->worldEpoch = 0u;
    impl->worldFingerprint = 0u;
}

bool TerrainRenderer_Init(TerrainRenderer *tr) {
    if (!tr) return false;
    memset(tr, 0, sizeof(*tr));
    TerrainRendererImpl *impl = calloc(1, sizeof(*impl));
    if (!impl) return false;
    impl->debugMode = TERRAIN_DEBUG_NATURAL;
    impl->lodQualityScale = 1.0f;
    tr->impl = impl;
    return true;
}

void TerrainRenderer_Destroy(TerrainRenderer *tr) {
    if (!tr || !tr->impl) return;
    TerrainRendererImpl *impl = (TerrainRendererImpl *)tr->impl;
    freeChunks(impl);
    if (impl->sharedEboReady) {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVELS; ++lod) {
            for (uint32_t mask = 0; mask < 16u; ++mask) {
                if (impl->sharedEbo.ebo[lod][mask]) glDeleteBuffers(1, &impl->sharedEbo.ebo[lod][mask]);
            }
        }
        impl->sharedEboReady = false;
    }
    if (impl->program) glDeleteProgram(impl->program);
    if (impl->depthProgram) glDeleteProgram(impl->depthProgram);
    free(impl);
    tr->impl = NULL;
}

static void measureChunkError(TerrainChunk *ch, const TerrainGpuVertex *vertices) {
    if (!ch->standardSize) return;
    uint32_t stride = ch->cellsX + 1u;
    float spacing = vertices[1].pos[0] - vertices[0].pos[0];
    for (uint32_t lod = 1; lod < TERRAIN_LOD_LEVELS; ++lod) {
        uint32_t step = 1u << lod;
        float error = 0.0f;
        for (uint32_t z = 0; z <= ch->cellsZ; ++z) {
            uint32_t z0 = (z < ch->cellsZ) ? z - z % step : z - step;
            float v = (float)(z - z0) / (float)step;
            for (uint32_t x = 0; x <= ch->cellsX; ++x) {
                uint32_t x0 = (x < ch->cellsX) ? x - x % step : x - step;
                float u = (float)(x - x0) / (float)step;
                const TerrainGpuVertex *a = &vertices[vIdx(x0, z0, stride)];
                const TerrainGpuVertex *b = &vertices[u >= v ? vIdx(x0 + step, z0, stride) : vIdx(x0, z0 + step, stride)];
                const TerrainGpuVertex *c = &vertices[vIdx(x0 + step, z0 + step, stride)];
                const TerrainGpuVertex *sample = &vertices[vIdx(x, z, stride)];
                float wa = 1.0f - fmaxf(u, v), wb = fabsf(u - v), wc = fminf(u, v);
                float h = wa * a->pos[1] + wb * b->pos[1] + wc * c->pos[1];
                float normalError = fabsf(sample->normal[1] -
                                          (wa * a->normal[1] + wb * b->normal[1] + wc * c->normal[1]));
                error = fmaxf(error, fabsf(sample->pos[1] - h) + 0.24f * spacing * normalError);
            }
        }
        ch->error[lod] = fmaxf(ch->error[lod - 1u], error);
    }
}

static void pinShoreNeighbors(TerrainRendererImpl *impl) {
    uint32_t width = impl->gridChunksX;
    for (uint32_t i = 0; i < impl->chunkCount; ++i) {
        TerrainChunk *ch = &impl->chunks[i];
        if (!ch->shoreline) continue;
        ch->pinned = true;
        uint8_t edgeShore = ch->wetEdges & ch->dryEdges;
        if (ch->cz && (edgeShore & 1u)) impl->chunks[i - width].pinned = true;
        if (ch->cz + 1u < impl->gridChunksZ && (edgeShore & 2u)) impl->chunks[i + width].pinned = true;
        if (ch->cx && (edgeShore & 4u)) impl->chunks[i - 1u].pinned = true;
        if (ch->cx + 1u < width && (edgeShore & 8u)) impl->chunks[i + 1u].pinned = true;
    }
}

enum { LANDSCAPE_SIZE = 128, LANDSCAPE_UNIT = 3, CONTACT_UNIT = 4 };

static size_t buildLandscape(TerrainRendererImpl *impl, const WorldStaticData *world) {
    const TerrainHeightfield *tf = &world->terrain;
    const WorldBiomeField *biome = &world->biome;
    uint8_t *pixels = malloc(LANDSCAPE_SIZE * LANDSCAPE_SIZE * 4);
    if (!pixels) return 0;
    for (int j = 0; j < LANDSCAPE_SIZE; ++j) {
        for (int i = 0; i < LANDSCAPE_SIZE; ++i) {
            float fx = ((float)i + .5f) / LANDSCAPE_SIZE, fz = ((float)j + .5f) / LANDSCAPE_SIZE;
            int ix = (int)roundf(fx * (tf->resolution - 1)), iz = (int)roundf(fz * (tf->resolution - 1));
            float grass = 0, moisture = 0, shore = 0;
            for (int z = -2; z <= 2; ++z) {
                int zz = (int)fminf(tf->resolution - 1, fmaxf(0, iz + z));
                for (int x = -2; x <= 2; ++x) {
                    int xx = (int)fminf(tf->resolution - 1, fmaxf(0, ix + x));
                    size_t k = (size_t)zz * tf->resolution + xx;
                    grass += biome->fertility[k] * .56f + biome->moisture[k] * .44f;
                    moisture += biome->moisture[k];
                    shore += fmaxf(0.0f, 1.0f - biome->waterDistance[k] / 4.6f);
                }
            }
            float wx = tf->originX + tf->span * fx, wz = tf->originZ + tf->span * fz;
            float patch = TerrainNoise_Fbm2D(wx * .047f, wz * .047f,
                                             world->masterSeed ^ 0x168d49b3u, 2, .32f, 2.07f);
            size_t out = ((size_t)j * LANDSCAPE_SIZE + i) * 4;
            pixels[out] = (uint8_t)roundf(fminf(1, grass / 25) * 255);
            pixels[out + 1] = (uint8_t)roundf(fmaxf(0, 1.0f - moisture / 25) * 255);
            pixels[out + 2] = (uint8_t)roundf(shore * (255.0f / 25));
            pixels[out + 3] = (uint8_t)roundf(fminf(1, fmaxf(0, .50f + patch * .94f)) * 255);
        }
    }
    GLint binding, unpack;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glGenTextures(1, &impl->landscape);
    glBindTexture(GL_TEXTURE_2D, impl->landscape);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, LANDSCAPE_SIZE, LANDSCAPE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, (GLuint)binding);
    glPopClientAttrib();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack);
    free(pixels);
    impl->landscapeMapping = Vec3_Create(tf->originX, tf->originZ, 1.0f / tf->span);
    return (LANDSCAPE_SIZE * LANDSCAPE_SIZE * 4 - 1) * 4 / 3;
}

bool TerrainRenderer_SyncWorld(TerrainRenderer *tr, const WorldStaticData *world) {
    if (!tr || !tr->impl) return false;
    TerrainRendererImpl *impl = (TerrainRendererImpl *)tr->impl;
    if (!world || world->isFlat) {
        freeChunks(impl);
        return true;
    }
    if (impl->worldEpoch == world->epochId && impl->worldFingerprint == world->dataFingerprint &&
        impl->chunks != NULL) {
        return true;
    }

    if (!initTerrainShader(impl) || !initSharedEbos(impl)) return false;
    TerrainRendererImpl *published = impl;
    TerrainRendererImpl staged = *published;
    staged.chunks = NULL;
    staged.landscape = 0;
    staged.chunkCount = 0;
    impl = &staged;

    const TerrainHeightfield *tf = &world->terrain;
    const WorldHydrology *hy = &world->hydrology;
    const WorldBiomeField *bf = &world->biome;
    uint32_t totalCells = tf->resolution - 1u;
    uint32_t chunksX = (totalCells + TERRAIN_CHUNK_CELLS - 1u) / TERRAIN_CHUNK_CELLS;
    uint32_t chunksZ = (totalCells + TERRAIN_CHUNK_CELLS - 1u) / TERRAIN_CHUNK_CELLS;
    uint32_t totalChunks = chunksX * chunksZ;

    impl->chunks = calloc(totalChunks, sizeof(TerrainChunk));
    if (!impl->chunks) return false;
    impl->chunkCount = totalChunks;
    impl->gridChunksX = chunksX;
    impl->gridChunksZ = chunksZ;
    impl->continuousWater = hy->surfaceBodyId != NULL;
    impl->minH = tf->minHeight;
    impl->maxH = tf->maxHeight;
    impl->worldEpoch = world->epochId;
    impl->worldFingerprint = world->dataFingerprint;

    float altRange = fmaxf(1.0f, tf->maxHeight - tf->minHeight);
    size_t maxChunkVerts = (TERRAIN_CHUNK_CELLS + 1u) * (TERRAIN_CHUNK_CELLS + 1u);
    TerrainGpuVertex *vBuf = malloc(maxChunkVerts * sizeof(TerrainGpuVertex));
    if (!vBuf) { freeChunks(impl); return false; }

    size_t vramBytes = 0u;
    uint64_t uploads = 0u;
    GLint prevVao = 0, prevBuf = 0, prevEbo = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);

    bool uploadOk = true;
    for (uint32_t cz = 0; cz < chunksZ && uploadOk; ++cz) {
        uint32_t z0 = cz * TERRAIN_CHUNK_CELLS;
        uint32_t cellsZ = (z0 + TERRAIN_CHUNK_CELLS <= totalCells) ? TERRAIN_CHUNK_CELLS : (totalCells - z0);
        for (uint32_t cx = 0; cx < chunksX && uploadOk; ++cx) {
            uint32_t x0 = cx * TERRAIN_CHUNK_CELLS;
            uint32_t cellsX = (x0 + TERRAIN_CHUNK_CELLS <= totalCells) ? TERRAIN_CHUNK_CELLS : (totalCells - x0);
            TerrainChunk *ch = &impl->chunks[cz * chunksX + cx];
            ch->cx = cx; ch->cz = cz;
            ch->x0 = x0; ch->z0 = z0;
            ch->cellsX = cellsX; ch->cellsZ = cellsZ;
            ch->standardSize = (cellsX == TERRAIN_CHUNK_CELLS && cellsZ == TERRAIN_CHUNK_CELLS);
            ch->bounds = AABB_Empty();
            ch->minWetDepth = FLT_MAX;
            uint32_t wetCount = 0u;
            uint16_t wetBody = 0u;

            uint32_t vCount = 0u;
            for (uint32_t lz = 0; lz <= cellsZ; ++lz) {
                uint32_t gz = z0 + lz;
                float wz = tf->originZ + (float)gz * tf->spacing;
                for (uint32_t lx = 0; lx <= cellsX; ++lx) {
                    uint32_t gx = x0 + lx;
                    float wx = tf->originX + (float)gx * tf->spacing;
                    size_t gi = (size_t)gz * tf->resolution + gx;
                    float h = tf->heights[gi];
                    Vector3 n = tf->smoothNormals[gi];

                    TerrainGpuVertex *v = &vBuf[vCount++];
                    v->pos[0] = wx; v->pos[1] = h; v->pos[2] = wz;
                    v->normal[0] = n.x; v->normal[1] = n.y; v->normal[2] = n.z;
                    if (bf->materialRGBA) {
                        const uint8_t *m = &bf->materialRGBA[gi * 4u];
                        v->matWeights[0] = (float)m[0] / 255.0f;
                        v->matWeights[1] = (float)m[1] / 255.0f;
                        v->matWeights[2] = (float)m[2] / 255.0f;
                        v->matWeights[3] = (float)m[3] / 255.0f;
                    } else {
                        v->matWeights[0] = 1.0f; v->matWeights[1] = v->matWeights[2] = v->matWeights[3] = 0.0f;
                    }
                    float depth = (hy->waterDepth) ? hy->waterDepth[gi] : 0.0f;
                    if (depth > WORLD_WATER_DEPTH_EPSILON) {
                        ++wetCount;
                        ch->minWetDepth = fminf(depth, ch->minWetDepth);
                        uint16_t body = hy->waterBodyId[gi];
                        ch->shoreline |= wetBody != 0u && wetBody != body;
                        wetBody = body;
                    }
                    uint8_t edges = (lz <= 1u ? 1u : 0u) | (lz + 1u >= cellsZ ? 2u : 0u) |
                                    (lx <= 1u ? 4u : 0u) | (lx + 1u >= cellsX ? 8u : 0u);
                    if (depth > WORLD_WATER_DEPTH_EPSILON) ch->wetEdges |= edges;
                    if (depth < 0.08f) {
                        ch->dryEdges |= edges;
                    }
                    v->aux[0] = (h - tf->minHeight) / altRange;
                    v->aux[1] = atanf(tf->slopeTan[gi]);
                    v->aux[2] = depth;
                    v->aux[3] = bf->fertility ? bf->fertility[gi] : 0.5f;
                    v->flow[0] = hy->flowX ? hy->flowX[gi] : 0.0f;
                    v->flow[1] = hy->flowZ ? hy->flowZ[gi] : 0.0f;
                    AABB_ExpandPoint(&ch->bounds, Vec3_Create(wx, h, wz));
                }
            }
            ch->center = Vec3_Scale(Vec3_Add(ch->bounds.start, ch->bounds.end), 0.5f);
            ch->radius = Vec3_Length(Vec3_Sub(ch->bounds.end, ch->bounds.start)) * 0.5f;
            ch->vertexCount = vCount;
            ch->shoreline |= wetCount > 0u && wetCount < vCount;
            measureChunkError(ch, vBuf);

            glGenVertexArrays(1, &ch->vao);
            glGenBuffers(1, &ch->vbo);
            glBindVertexArray(ch->vao);
            glBindBuffer(GL_ARRAY_BUFFER, ch->vbo);
            size_t vBytes = (size_t)vCount * sizeof(TerrainGpuVertex);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vBytes, vBuf, GL_STATIC_DRAW);
            GLint uploaded = 0;
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &uploaded);
            if (uploaded != (GLint)vBytes) { uploadOk = false; break; }
            vramBytes += vBytes;
            ++uploads;

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainGpuVertex), (const void *)offsetof(TerrainGpuVertex, pos));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainGpuVertex), (const void *)offsetof(TerrainGpuVertex, normal));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainGpuVertex), (const void *)offsetof(TerrainGpuVertex, matWeights));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(TerrainGpuVertex), (const void *)offsetof(TerrainGpuVertex, aux));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainGpuVertex), (const void *)offsetof(TerrainGpuVertex, flow));

            if (!ch->standardSize) {
                glGenBuffers(16, ch->customEbo);
                for (uint32_t mask = 0; mask < 16; ++mask) {
                    uint32_t *cIdx = NULL, cCnt = 0u;
                    TerrainLodIndices_Build(cellsX, cellsZ, 0u, mask, &cIdx, &cCnt);
                    if (!cIdx) { uploadOk = false; break; }
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ch->customEbo[mask]);
                    GLint bytes = (GLint)(cCnt * sizeof(uint32_t));
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, cIdx, GL_STATIC_DRAW);
                    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &uploaded);
                    uploadOk = bytes == uploaded;
                    ch->customIndexCount[mask] = cCnt;
                    vramBytes += (size_t)cCnt * sizeof(uint32_t);
                    free(cIdx);
                    if (!uploadOk) break;
                }
            }

        }
    }

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);
    free(vBuf);
    if (!uploadOk) {
        freeChunks(impl);
        fprintf(stderr, "[TerrainRenderer] Fallo al subir chunks del terreno.\n");
        return false;
    }
    pinShoreNeighbors(impl);
    for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVELS; ++lod)
        for (uint32_t mask = 0; mask < 16; ++mask)
            vramBytes += (size_t)impl->sharedEbo.count[lod][mask] * sizeof(uint32_t);

    impl->stats.totalChunks = totalChunks;
    impl->stats.gpuUploads += uploads;
    impl->stats.vramBytes = vramBytes + buildLandscape(impl, world);
    freeChunks(published);
    *published = staged;
    return true;
}

void TerrainRenderer_SetContacts(TerrainRenderer *tr, unsigned texture, Vector3 mapping) {
    if (!tr || !tr->impl) return;
    TerrainRendererImpl *impl = tr->impl;
    impl->contacts = texture;
    impl->contactsMapping = mapping;
}

void TerrainRenderer_SetDebugMode(TerrainRenderer *tr, TerrainDebugMode mode) {
    if (!tr || !tr->impl) return;
    TerrainRendererImpl *impl = (TerrainRendererImpl *)tr->impl;
    impl->debugMode = (mode < TERRAIN_DEBUG_MODE_COUNT) ? mode : TERRAIN_DEBUG_NATURAL;
}

TerrainDebugMode TerrainRenderer_GetDebugMode(const TerrainRenderer *tr) {
    if (!tr || !tr->impl) return TERRAIN_DEBUG_NATURAL;
    return ((const TerrainRendererImpl *)tr->impl)->debugMode;
}

void TerrainRenderer_SetQuality(TerrainRenderer *tr, float lodQualityScale, bool wireframe) {
    if (!tr || !tr->impl) return;
    TerrainRendererImpl *impl = (TerrainRendererImpl *)tr->impl;
    impl->lodQualityScale = fmaxf(0.4f, fminf(2.0f, lodQualityScale));
    impl->wireframe = wireframe;
}

void TerrainRenderer_ForceLod0(TerrainRenderer *tr, bool enabled) {
    if (tr && tr->impl) ((TerrainRendererImpl *)tr->impl)->forceLod0 = enabled;
}

typedef struct TerrainFrustum {
    float p[6][4];
} TerrainFrustum;

static void extractTerrainFrustum(TerrainFrustum *f, const float proj[16]) {
    float mv[16], m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            m[col * 4 + row] =
                proj[0 * 4 + row] * mv[col * 4 + 0] +
                proj[1 * 4 + row] * mv[col * 4 + 1] +
                proj[2 * 4 + row] * mv[col * 4 + 2] +
                proj[3 * 4 + row] * mv[col * 4 + 3];
        }
    }
    const int sign[6] = {1, -1, 1, -1, 1, -1};
    const int rowIdx[6] = {0, 0, 1, 1, 2, 2};
    for (int i = 0; i < 6; ++i) {
        int r = rowIdx[i];
        float s = (float)sign[i];
        float a = m[3]  + s * m[r];
        float b = m[7]  + s * m[4 + r];
        float c = m[11] + s * m[8 + r];
        float d = m[15] + s * m[12 + r];
        float inv = 1.0f / fmaxf(1e-6f, sqrtf(a * a + b * b + c * c));
        f->p[i][0] = a * inv;
        f->p[i][1] = b * inv;
        f->p[i][2] = c * inv;
        f->p[i][3] = d * inv;
    }
}

static bool aabbInTerrainFrustum(const TerrainFrustum *f, AABB3D box) {
    for (int i = 0; i < 6; ++i) {
        float px = (f->p[i][0] >= 0.0f) ? box.end.x : box.start.x;
        float py = (f->p[i][1] >= 0.0f) ? box.end.y : box.start.y;
        float pz = (f->p[i][2] >= 0.0f) ? box.end.z : box.start.z;
        if (f->p[i][0] * px + f->p[i][1] * py + f->p[i][2] * pz + f->p[i][3] < -0.5f) {
            return false;
        }
    }
    return true;
}

bool TerrainRenderer_Render(TerrainRenderer *tr, const ICamera *camera,
                            const TerrainMaterials *materials, float maxSlopeNavRadians, float maxWadeNavDepth) {
    if (!tr || !tr->impl || !camera) return false;
    TerrainRendererImpl *impl = (TerrainRendererImpl *)tr->impl;
    if (!impl->chunks || impl->chunkCount == 0u || !impl->program) return false;

    /* Error geométrico proyectado en píxeles con la matriz y el viewport efectivos. */
    float proj[16];
    GLint viewport[4];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, viewport);
    float qScale = impl->lodQualityScale;
    float projectionScale = 0.5f * (float)viewport[3] * fabsf(proj[5]);
    bool perspective = fabsf(proj[15]) < 0.5f;
    Vector3 cameraForward = Vec3_Normalize(Vec3_Sub(camera->target, camera->position));
    for (uint32_t i = 0; i < impl->chunkCount; ++i) {
        TerrainChunk *ch = &impl->chunks[i];
        uint32_t prev = ch->colorLod;
        ch->currentLod = 0u;
        if (impl->forceLod0 || !ch->standardSize) continue;
        float zNear = Vec3_Dot(Vec3_Sub(ch->center, camera->position), cameraForward) - ch->radius;
        float pixelsPerUnit = perspective ? projectionScale / fmaxf(camera->nearPlane, zNear) : projectionScale;
        if (impl->depthPass) {
            for (uint32_t lod = TERRAIN_LOD_LEVELS - 1u; lod > 0u; --lod) {
                if (ch->error[lod] * pixelsPerUnit < 1.25f) {
                    ch->currentLod = lod;
                    break;
                }
            }
            continue;
        }
        bool protectWater = impl->continuousWater || pixelsPerUnit > 12.0f;
        if (ch->pinned && protectWater) continue;
        bool closeToContact = pixelsPerUnit > 24.0f &&
                              camera->target.x >= ch->bounds.start.x - 1.5f && camera->target.x <= ch->bounds.end.x + 1.5f &&
                              camera->target.z >= ch->bounds.start.z - 1.5f && camera->target.z <= ch->bounds.end.z + 1.5f;
        if (closeToContact) continue;
        for (uint32_t lod = TERRAIN_LOD_LEVELS - 1u; lod > 0u; --lod) {
            float threshold = (lod > prev ? 1.45f : 1.75f) / qScale;
            if (pixelsPerUnit < 9.0f) threshold *= protectWater ? 1.23f : 1.45f;
            if (ch->error[lod] * pixelsPerUnit <= threshold &&
                (!protectWater || ch->error[(lod < 3u) ? lod + 1u : lod] + 0.025f < ch->minWetDepth)) {
                ch->currentLod = lod;
                break;
            }
        }
    }

    /* 2) Relajación para garantizar diferencia máxima de 1 nivel LOD entre chunks adyacentes. */
    uint32_t cxN = impl->gridChunksX, czN = impl->gridChunksZ;
    for (int pass = 0; pass < 3; ++pass) {
        for (uint32_t cz = 0; cz < czN; ++cz) {
            for (uint32_t cx = 0; cx < cxN; ++cx) {
                TerrainChunk *ch = &impl->chunks[cz * cxN + cx];
                uint32_t lod = ch->currentLod;
                if (cx > 0u && impl->chunks[cz * cxN + (cx - 1u)].currentLod + 1u < lod)
                    lod = impl->chunks[cz * cxN + (cx - 1u)].currentLod + 1u;
                if (cx + 1u < cxN && impl->chunks[cz * cxN + (cx + 1u)].currentLod + 1u < lod)
                    lod = impl->chunks[cz * cxN + (cx + 1u)].currentLod + 1u;
                if (cz > 0u && impl->chunks[(cz - 1u) * cxN + cx].currentLod + 1u < lod)
                    lod = impl->chunks[(cz - 1u) * cxN + cx].currentLod + 1u;
                if (cz + 1u < czN && impl->chunks[(cz + 1u) * cxN + cx].currentLod + 1u < lod)
                    lod = impl->chunks[(cz + 1u) * cxN + cx].currentLod + 1u;
                ch->currentLod = lod;
            }
        }
    }

    if (!impl->depthPass)
        for (uint32_t i = 0; i < impl->chunkCount; ++i)
            impl->chunks[i].colorLod = impl->chunks[i].currentLod;

    /* 3) Guardar estado OpenGL y dibujar chunks visibles dentro del frustum. */
    GLint prevProg = 0, prevVao = 0, prevEbo = 0;
    GLint prevPoly[2] = {GL_FILL, GL_FILL};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);
    glGetIntegerv(GL_POLYGON_MODE, prevPoly);
    GLint cullMode, frontFace;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    glGetIntegerv(GL_FRONT_FACE, &frontFace);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    if (!impl->depthPass && (impl->wireframe || impl->debugMode == TERRAIN_DEBUG_COLLISION)) {
        glPolygonMode(GL_FRONT_AND_BACK, impl->wireframe ? GL_LINE : GL_FILL);
    }

    glUseProgram(impl->depthPass ? impl->depthProgram : impl->program);
    TerrainMaterialState materialState = {0};
    GLint previousLandscape = 0, previousContacts = 0;
    if (!impl->depthPass) {
        materialState = TerrainMaterials_Bind(materials, &impl->materialBindings);
        glActiveTexture(GL_TEXTURE0 + LANDSCAPE_UNIT);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousLandscape);
        glBindTexture(GL_TEXTURE_2D, impl->landscape);
        glUniform1i(impl->landscapeLoc, LANDSCAPE_UNIT);
        Vector3 mapping = impl->landscapeMapping;
        glUniform3f(impl->landscapeMappingLoc, mapping.x, mapping.y, mapping.z);
        glActiveTexture(GL_TEXTURE0 + CONTACT_UNIT);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousContacts);
        glBindTexture(GL_TEXTURE_2D, impl->contacts);
        glUniform1i(impl->contactsLoc, CONTACT_UNIT);
        mapping = impl->contactsMapping;
        glUniform3f(impl->contactsMappingLoc, mapping.x, mapping.y, impl->contacts ? mapping.z : 0);
        glUniform1i(impl->materialTierLoc, qScale < 0.85f ? 0 : (qScale < 1.2f ? 1 : 2));
        if (impl->uDebugModeLoc >= 0) glUniform1i(impl->uDebugModeLoc, (GLint)impl->debugMode);
        if (impl->uCamPosLoc >= 0) glUniform3f(impl->uCamPosLoc, camera->position.x, camera->position.y, camera->position.z);
        if (impl->uNavLimitsLoc >= 0) glUniform2f(impl->uNavLimitsLoc, maxSlopeNavRadians, maxWadeNavDepth);
        if (impl->uHeightRangeLoc >= 0) glUniform2f(impl->uHeightRangeLoc, impl->minH, impl->maxH);
    }

    impl->stats.renderedChunks = 0u;
    impl->stats.culledChunks = 0u;
    impl->stats.renderedTriangles = 0u;
    impl->stats.drawCalls = 0u;
    memset(impl->stats.lodCounts, 0, sizeof(impl->stats.lodCounts));

    TerrainFrustum frustum;
    extractTerrainFrustum(&frustum, proj);

    for (uint32_t cz = 0; cz < czN; ++cz) {
        for (uint32_t cx = 0; cx < cxN; ++cx) {
            TerrainChunk *ch = &impl->chunks[cz * cxN + cx];
            if (!aabbInTerrainFrustum(&frustum, ch->bounds)) {
                ++impl->stats.culledChunks;
                continue;
            }
            uint32_t lod = ch->currentLod;
            glBindVertexArray(ch->vao);
            GLuint ebo = 0u;
            uint32_t idxCount = 0u;
            uint32_t mask = 0u;
            if (cz > 0u && impl->chunks[(cz - 1u) * cxN + cx].currentLod > lod) mask |= 1u;
            if (cz + 1u < czN && impl->chunks[(cz + 1u) * cxN + cx].currentLod > lod) mask |= 2u;
            if (cx > 0u && impl->chunks[cz * cxN + (cx - 1u)].currentLod > lod) mask |= 4u;
            if (cx + 1u < cxN && impl->chunks[cz * cxN + (cx + 1u)].currentLod > lod) mask |= 8u;
            if (ch->standardSize) {
                ebo = impl->sharedEbo.ebo[lod][mask];
                idxCount = impl->sharedEbo.count[lod][mask];
            } else {
                ebo = ch->customEbo[mask];
                idxCount = ch->customIndexCount[mask];
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glDrawElements(GL_TRIANGLES, (GLsizei)idxCount, GL_UNSIGNED_INT, 0);
            ++impl->stats.renderedChunks;
            ++impl->stats.drawCalls;
            ++impl->stats.lodCounts[lod < TERRAIN_LOD_LEVELS ? lod : 0u];
            impl->stats.renderedTriangles += idxCount / 3u;
        }
    }

    /* Restaurar estado OpenGL limpiamente. */
    if (prevPoly[0] == prevPoly[1]) glPolygonMode(GL_FRONT_AND_BACK, (GLenum)prevPoly[0]);
    else {
        glPolygonMode(GL_FRONT, (GLenum)prevPoly[0]);
        glPolygonMode(GL_BACK, (GLenum)prevPoly[1]);
    }
    if (!prevCull) glDisable(GL_CULL_FACE);
    if (!prevDepth) glDisable(GL_DEPTH_TEST);
    glCullFace((GLenum)cullMode);
    glFrontFace((GLenum)frontFace);
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);
    if (!impl->depthPass) {
        glActiveTexture(GL_TEXTURE0 + LANDSCAPE_UNIT);
        glBindTexture(GL_TEXTURE_2D, (GLuint)previousLandscape);
        glActiveTexture(GL_TEXTURE0 + CONTACT_UNIT);
        glBindTexture(GL_TEXTURE_2D, (GLuint)previousContacts);
        TerrainMaterials_Restore(&materialState);
    }
    glUseProgram((GLuint)prevProg);
    return true;
}

TerrainRenderStats TerrainRenderer_GetStats(const TerrainRenderer *tr) {
    if (!tr || !tr->impl) return (TerrainRenderStats){0};
    return ((const TerrainRendererImpl *)tr->impl)->stats;
}

void TerrainRenderer_RenderShadow(TerrainRenderer *renderer, const ICamera *camera) {
    TerrainRendererImpl *impl = renderer->impl;
    if (!impl || !impl->depthProgram) return;
    impl->depthPass = true;
    TerrainRenderer_Render(renderer, camera, NULL, 0, 0);
    impl->depthPass = false;
}
