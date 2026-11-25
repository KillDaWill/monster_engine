/**
 * @file TerrainRenderer.c
 * @brief Renderizado chunked 32x32 con 4 niveles LOD, costura sin T-junctions,
 *        culling de frustum y shader GLSL 330 de splatting multi-material + depuración.
 */
#define GL_GLEXT_PROTOTYPES
#include "TerrainRenderer.h"
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
    bool hasWater;
    uint32_t currentLod;
    GLuint vao;
    GLuint vbo;
    uint32_t vertexCount;
    /* EBO dinámico sólo si es un borde no cuadrado; chunks estándar 32x32 comparten EBOs globales! */
    bool standardSize;
    GLuint customEbo;
    uint32_t customIndexCount;
} TerrainChunk;

/* 16 combinaciones de costura de borde (bits: 0=North/-Z, 1=South/+Z, 2=West/-X, 3=East/+X) por nivel LOD. */
typedef struct SharedLodIndices {
    GLuint ebo[TERRAIN_LOD_LEVELS][16];
    uint32_t count[TERRAIN_LOD_LEVELS][16];
} SharedLodIndices;

typedef struct TerrainRendererImpl {
    GLuint program;
    bool programFailed;
    GLint uDebugModeLoc;
    GLint uCamPosLoc;
    GLint uNavLimitsLoc;
    GLint uHeightRangeLoc;

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
    default:                        return "NATURAL";
    }
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
        "void main() {\n"
        "  vWorldPos = aPos;\n"
        "  vWorldNormal = normalize(aNormal);\n"
        "  vMatWeights = aMatWeights;\n"
        "  vAux = aAux;\n"
        "  vFlow = aFlow;\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);\n"
        "}\n";

    const char *fsPart1 =
        "#version 330 compatibility\n"
        "uniform int uDebugMode;\n"
        "uniform vec3 uCamPos;\n"
        "uniform vec2 uNavLimits;\n"   /* x=maxSlopeRad, y=maxWadeDepth */
        "uniform vec2 uHeightRange;\n" /* x=minH, y=maxH */
        "in vec3 vWorldPos;\n"
        "in vec3 vWorldNormal;\n"
        "in vec4 vMatWeights;\n"
        "in vec4 vAux;\n"
        "in vec2 vFlow;\n"
        "out vec4 fragColor;\n"
        "float hash21(vec2 p) {\n"
        "  p = fract(p * vec2(123.34, 456.21));\n"
        "  p += dot(p, p + 45.32);\n"
        "  return fract(p.x * p.y);\n"
        "}\n"
        "float vnoise(vec2 p) {\n"
        "  vec2 i = floor(p); vec2 f = fract(p);\n"
        "  vec2 u = f * f * (3.0 - 2.0 * f);\n"
        "  return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),\n"
        "             mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x), u.y);\n"
        "}\n"
        "float fbm(vec2 p) {\n"
        "  float s = 0.0, a = 0.5;\n"
        "  for (int i = 0; i < 3; ++i) { s += a * vnoise(p); p *= 2.07; a *= 0.5; }\n"
        "  return s;\n"
        "}\n"
        "void main() {\n"
        "  vec3 n = normalize(vWorldNormal);\n"
        "  vec3 sunDir = normalize(vec3(-0.48, 0.76, 0.44));\n"
        "  float ndl = max(dot(n, sunDir), 0.0);\n"
        "  float hemi = 0.5 + 0.5 * n.y;\n"
        "  float altNorm = vAux.x;\n"
        "  float slopeRad = vAux.y;\n"
        "  float wDepth = vAux.z;\n"
        "  float fert = vAux.w;\n"
        "  float dist = length(uCamPos - vWorldPos);\n"
        "  if (uDebugMode == 1) {\n"
        "    vec3 lo = vec3(0.14, 0.36, 0.24), mid = vec3(0.68, 0.66, 0.32), hi = vec3(0.86, 0.84, 0.82);\n"
        "    vec3 col = altNorm < 0.5 ? mix(lo, mid, altNorm * 2.0) : mix(mid, hi, (altNorm - 0.5) * 2.0);\n"
        "    float contour = abs(fract(vWorldPos.y * 0.5) - 0.5);\n"
        "    col *= mix(0.65, 1.0, smoothstep(0.02, 0.08, contour));\n"
        "    fragColor = vec4(col * (0.45 + 0.55 * ndl), 1.0); return;\n"
        "  } else if (uDebugMode == 2) {\n"
        "    float t = clamp(slopeRad / 0.85, 0.0, 1.0);\n"
        "    vec3 col = t < 0.5 ? mix(vec3(0.18, 0.72, 0.30), vec3(0.92, 0.80, 0.20), t * 2.0)\n"
        "                       : mix(vec3(0.92, 0.80, 0.20), vec3(0.90, 0.16, 0.14), (t - 0.5) * 2.0);\n"
        "    fragColor = vec4(col * (0.45 + 0.55 * ndl), 1.0); return;\n"
        "  } else if (uDebugMode == 3) {\n"
        "    float fl = length(vFlow);\n"
        "    vec3 col = wDepth > 0.01 ? mix(vec3(0.10, 0.45, 0.85), vec3(0.15, 0.92, 0.96), clamp(fl, 0.0, 1.0))\n"
        "                             : vec3(0.22 + 0.12 * ndl, 0.26 + 0.14 * ndl, 0.24 + 0.12 * ndl);\n"
        "    fragColor = vec4(col, 1.0); return;\n"
        "  } else if (uDebugMode == 4) {\n"
        "    vec3 col = wDepth > 0.01 ? mix(vec3(0.28, 0.82, 0.88), vec3(0.04, 0.14, 0.48), clamp(wDepth * 0.5, 0.0, 1.0))\n"
        "                             : vec3(0.32, 0.36, 0.32) * (0.45 + 0.55 * ndl);\n"
        "    fragColor = vec4(col, 1.0); return;\n"
        "  } else if (uDebugMode == 5) {\n"
        "    vec3 col = mix(vec3(0.56, 0.46, 0.32), vec3(0.16, 0.68, 0.24), fert);\n"
        "    if (wDepth > 0.01) col = vec3(0.14, 0.38, 0.56);\n"
        "    fragColor = vec4(col * (0.45 + 0.55 * ndl), 1.0); return;\n"
        "  } else if (uDebugMode == 6) {\n"
        "    bool steep = slopeRad > uNavLimits.x;\n"
        "    bool deep = wDepth > uNavLimits.y;\n"
        "    vec3 col = deep ? vec3(0.12, 0.22, 0.72) : (steep ? vec3(0.84, 0.18, 0.16) :\n"
        "               (wDepth > 0.04 ? vec3(0.22, 0.74, 0.80) : vec3(0.22, 0.66, 0.30)));\n"
        "    fragColor = vec4(col * (0.45 + 0.55 * ndl), 1.0); return;\n"
        "  }\n";
    const char *fsPart2 =
        "  /* Modo Natural: splatting continuo de 4 materiales con detalle procedural y triplanar en roca. */\n"
        "  float detailFade = 1.0 - smoothstep(40.0, 220.0, dist);\n"
        "  float macro = fbm(vWorldPos.xz * 0.065);\n"
        "  float micro = detailFade > 0.01 ? fbm(vWorldPos.xz * 0.65) : 0.5;\n"
        "  /* Triplanar para estratos/grietas en acantilados rocosos sin estiramiento UV. */\n"
        "  vec3 blend = abs(n); blend = pow(blend, vec3(4.0)); blend /= max(dot(blend, vec3(1.0)), 1e-4);\n"
        "  float strataX = fbm(vec2(vWorldPos.z * 0.22, vWorldPos.y * 0.85));\n"
        "  float strataZ = fbm(vec2(vWorldPos.x * 0.22, vWorldPos.y * 0.85));\n"
        "  float strataY = fbm(vWorldPos.xz * 0.32);\n"
        "  float rockTex = strataX * blend.x + strataY * blend.y + strataZ * blend.z;\n"
        "  vec3 grassCol = mix(vec3(0.20, 0.36, 0.15), vec3(0.32, 0.46, 0.19), macro * 0.75 + micro * 0.25);\n"
        "  grassCol = mix(grassCol, vec3(0.16, 0.31, 0.13), fert * 0.35);\n"
        "  vec3 dryCol   = mix(vec3(0.45, 0.38, 0.25), vec3(0.56, 0.48, 0.33), macro * 0.6 + micro * 0.4);\n"
        "  vec3 rockCol  = mix(vec3(0.30, 0.31, 0.33), vec3(0.52, 0.50, 0.46), rockTex);\n"
        "  vec3 mudCol   = mix(vec3(0.28, 0.26, 0.21), vec3(0.44, 0.40, 0.31), macro * 0.6 + micro * 0.4);\n"
        "  if (wDepth > 0.01) mudCol *= mix(0.86, 0.58, clamp(wDepth * 0.6, 0.0, 1.0));\n"
        "  vec4 w = vMatWeights;\n"
        "  vec3 albedo = grassCol * w.x + dryCol * w.y + rockCol * w.z + mudCol * w.w;\n"
        "  /* Perturbación sutil de iluminación micro-relieve sin alterar geometría física. */\n"
        "  float microShade = mix(1.0, 0.88 + 0.24 * micro, detailFade * (0.45 + 0.45 * w.z));\n"
        "  vec3 skyFill = mix(vec3(0.16, 0.19, 0.17), vec3(0.28, 0.35, 0.44), hemi);\n"
        "  vec3 sunCol  = vec3(1.05, 0.99, 0.88) * ndl * 0.84;\n"
        "  vec3 lit = albedo * (skyFill + sunCol) * microShade;\n"
        "  /* Niebla atmosférica suave para lectura espacial en valles grandes. */\n"
        "  float fog = smoothstep(95.0, 460.0, dist) * 0.45;\n"
        "  vec3 fogCol = vec3(0.56, 0.66, 0.74);\n"
        "  fragColor = vec4(mix(lit, fogCol, fog), 1.0);\n"
        "}\n";

    const char *vsParts[1] = { vs };
    const char *fsParts[2] = { fsPart1, fsPart2 };
    GLuint v = compileGlShaderParts(GL_VERTEX_SHADER, vsParts, 1);
    GLuint f = compileGlShaderParts(GL_FRAGMENT_SHADER, fsParts, 2);
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
    impl->uDebugModeLoc = glGetUniformLocation(p, "uDebugMode");
    impl->uCamPosLoc = glGetUniformLocation(p, "uCamPos");
    impl->uNavLimitsLoc = glGetUniformLocation(p, "uNavLimits");
    impl->uHeightRangeLoc = glGetUniformLocation(p, "uHeightRange");
    return true;
}

static inline uint32_t vIdx(uint32_t x, uint32_t z, uint32_t rowStride) {
    return z * rowStride + x;
}

/* Genera índices sin grietas para un chunk 32x32 en el nivel LOD dado y máscara de vecinos más gruesos:
 * stitchMask bit0: North(z=0), bit1: South(z=N), bit2: West(x=0), bit3: East(x=N).
 * Cuando un borde tiene vecino 1 nivel más grueso (paso 2*step), colapsa los vértices intermedios
 * impares del borde hacia los pares para eliminar completamente cualquier T-junction / grieta. */
static void buildStitchedIndices(uint32_t cellsX, uint32_t cellsZ, uint32_t lod, uint32_t stitchMask,
                                 uint32_t **outIndices, uint32_t *outCount) {
    uint32_t step = 1u << lod;
    if (step > cellsX) step = cellsX;
    if (step > cellsZ) step = cellsZ;
    uint32_t rowStride = cellsX + 1u;
    uint32_t maxTris = (cellsX / step + 2u) * (cellsZ / step + 2u) * 2u;
    uint32_t *idx = malloc((size_t)maxTris * 3u * sizeof(uint32_t));
    uint32_t n = 0u;
    if (!idx) { *outIndices = NULL; *outCount = 0u; return; }

    bool stitchN = (stitchMask & 1u) != 0u;
    bool stitchS = (stitchMask & 2u) != 0u;
    bool stitchW = (stitchMask & 4u) != 0u;
    bool stitchE = (stitchMask & 8u) != 0u;
    uint32_t coarseStep = step * 2u;

    for (uint32_t z = 0; z < cellsZ; z += step) {
        uint32_t z1 = z + step;
        if (z1 > cellsZ) z1 = cellsZ;
        for (uint32_t x = 0; x < cellsX; x += step) {
            uint32_t x1 = x + step;
            if (x1 > cellsX) x1 = cellsX;

            uint32_t x00 = x, z00 = z;
            uint32_t x10 = x1, z10 = z;
            uint32_t x01 = x, z01 = z1;
            uint32_t x11 = x1, z11 = z1;

            /* Ajustar vértices intermedios en bordes que colindan con un chunk de LOD más grueso. */
            if (stitchN && z == 0u) {
                if ((x00 % coarseStep) != 0u) x00 -= step;
                if ((x10 % coarseStep) != 0u) x10 += step;
                if (x10 > cellsX) x10 = cellsX;
            }
            if (stitchS && z1 == cellsZ) {
                if ((x01 % coarseStep) != 0u) x01 -= step;
                if ((x11 % coarseStep) != 0u) x11 += step;
                if (x11 > cellsX) x11 = cellsX;
            }
            if (stitchW && x == 0u) {
                if ((z00 % coarseStep) != 0u) z00 -= step;
                if ((z01 % coarseStep) != 0u) z01 += step;
                if (z01 > cellsZ) z01 = cellsZ;
            }
            if (stitchE && x1 == cellsX) {
                if ((z10 % coarseStep) != 0u) z10 -= step;
                if ((z11 % coarseStep) != 0u) z11 += step;
                if (z11 > cellsZ) z11 = cellsZ;
            }

            uint32_t i00 = vIdx(x00, z00, rowStride);
            uint32_t i10 = vIdx(x10, z10, rowStride);
            uint32_t i01 = vIdx(x01, z01, rowStride);
            uint32_t i11 = vIdx(x11, z11, rowStride);

            /* Misma diagonal autoritativa que TerrainHeightfield: T0=(v00,v11,v10), T1=(v00,v01,v11). */
            if (i00 != i11 && i11 != i10 && i00 != i10) {
                idx[n++] = i00; idx[n++] = i11; idx[n++] = i10;
            }
            if (i00 != i01 && i01 != i11 && i00 != i11) {
                idx[n++] = i00; idx[n++] = i01; idx[n++] = i11;
            }
        }
    }
    *outIndices = idx;
    *outCount = n;
}

static void initSharedEbos(TerrainRendererImpl *impl) {
    if (impl->sharedEboReady) return;
    for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVELS; ++lod) {
        for (uint32_t mask = 0; mask < 16u; ++mask) {
            uint32_t *idx = NULL;
            uint32_t count = 0u;
            buildStitchedIndices(TERRAIN_CHUNK_CELLS, TERRAIN_CHUNK_CELLS, lod, mask, &idx, &count);
            glGenBuffers(1, &impl->sharedEbo.ebo[lod][mask]);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl->sharedEbo.ebo[lod][mask]);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(uint32_t)), idx, GL_STATIC_DRAW);
            impl->sharedEbo.count[lod][mask] = count;
            free(idx);
        }
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    impl->sharedEboReady = true;
}

static void freeChunks(TerrainRendererImpl *impl) {
    if (!impl || !impl->chunks) return;
    for (uint32_t i = 0; i < impl->chunkCount; ++i) {
        if (impl->chunks[i].vao) glDeleteVertexArrays(1, &impl->chunks[i].vao);
        if (impl->chunks[i].vbo) glDeleteBuffers(1, &impl->chunks[i].vbo);
        if (impl->chunks[i].customEbo) glDeleteBuffers(1, &impl->chunks[i].customEbo);
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
    free(impl);
    tr->impl = NULL;
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

    if (!initTerrainShader(impl)) return false;
    initSharedEbos(impl);
    freeChunks(impl);

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

    for (uint32_t cz = 0; cz < chunksZ; ++cz) {
        uint32_t z0 = cz * TERRAIN_CHUNK_CELLS;
        uint32_t cellsZ = (z0 + TERRAIN_CHUNK_CELLS <= totalCells) ? TERRAIN_CHUNK_CELLS : (totalCells - z0);
        for (uint32_t cx = 0; cx < chunksX; ++cx) {
            uint32_t x0 = cx * TERRAIN_CHUNK_CELLS;
            uint32_t cellsX = (x0 + TERRAIN_CHUNK_CELLS <= totalCells) ? TERRAIN_CHUNK_CELLS : (totalCells - x0);
            TerrainChunk *ch = &impl->chunks[cz * chunksX + cx];
            ch->cx = cx; ch->cz = cz;
            ch->x0 = x0; ch->z0 = z0;
            ch->cellsX = cellsX; ch->cellsZ = cellsZ;
            ch->standardSize = (cellsX == TERRAIN_CHUNK_CELLS && cellsZ == TERRAIN_CHUNK_CELLS);
            ch->bounds = AABB_Empty();
            ch->hasWater = false;

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
                    if (depth > 0.01f) ch->hasWater = true;
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

            glGenVertexArrays(1, &ch->vao);
            glGenBuffers(1, &ch->vbo);
            glBindVertexArray(ch->vao);
            glBindBuffer(GL_ARRAY_BUFFER, ch->vbo);
            size_t vBytes = (size_t)vCount * sizeof(TerrainGpuVertex);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vBytes, vBuf, GL_STATIC_DRAW);
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
                uint32_t *cIdx = NULL;
                uint32_t cCnt = 0u;
                buildStitchedIndices(cellsX, cellsZ, 0u, 0u, &cIdx, &cCnt);
                glGenBuffers(1, &ch->customEbo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ch->customEbo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(cCnt * sizeof(uint32_t)), cIdx, GL_STATIC_DRAW);
                ch->customIndexCount = cCnt;
                vramBytes += (size_t)cCnt * sizeof(uint32_t);
                free(cIdx);
            }
        }
    }

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);
    free(vBuf);

    impl->stats.totalChunks = totalChunks;
    impl->stats.gpuUploads += uploads;
    impl->stats.vramBytes = vramBytes;
    return true;
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

typedef struct TerrainFrustum {
    float p[6][4];
} TerrainFrustum;

static void extractTerrainFrustum(TerrainFrustum *f) {
    float proj[16], mv[16], m[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
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
                            float maxSlopeNavRadians, float maxWadeNavDepth) {
    if (!tr || !tr->impl || !camera) return false;
    TerrainRendererImpl *impl = (TerrainRendererImpl *)tr->impl;
    if (!impl->chunks || impl->chunkCount == 0u || !impl->program) return false;

    /* 1) Selección de LOD por distancia + histéresis + protección de detalle en orillas/ríos. */
    float qScale = impl->lodQualityScale;
    float d1 = 48.0f * qScale;
    float d2 = 110.0f * qScale;
    float d3 = 210.0f * qScale;

    for (uint32_t i = 0; i < impl->chunkCount; ++i) {
        TerrainChunk *ch = &impl->chunks[i];
        if (!ch->standardSize) { ch->currentLod = 0u; continue; }
        float dist = Vec3_Distance(camera->position, ch->center) - ch->radius * 0.6f;
        if (dist < 0.0f) dist = 0.0f;
        /* Chunks con río/lago conservan mayor resolución para no cubrir cauces ni orillas. */
        if (ch->hasWater) dist *= 0.62f;

        uint32_t prev = ch->currentLod;
        uint32_t next = (dist < d1) ? 0u : (dist < d2) ? 1u : (dist < d3) ? 2u : 3u;
        /* Histéresis del 10% para evitar popping en el límite de distancia. */
        if (next > prev) {
            float thresh = (prev == 0u) ? d1 : (prev == 1u) ? d2 : d3;
            if (dist < thresh * 1.10f) next = prev;
        } else if (next < prev) {
            float thresh = (next == 0u) ? d1 : (next == 1u) ? d2 : d3;
            if (dist > thresh * 0.90f) next = prev;
        }
        ch->currentLod = next;
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

    /* 3) Guardar estado OpenGL y dibujar chunks visibles dentro del frustum. */
    GLint prevProg = 0, prevVao = 0, prevEbo = 0;
    GLint prevPoly[2] = {GL_FILL, GL_FILL};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);
    glGetIntegerv(GL_POLYGON_MODE, prevPoly);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    if (impl->wireframe || impl->debugMode == TERRAIN_DEBUG_COLLISION) {
        glPolygonMode(GL_FRONT_AND_BACK, impl->wireframe ? GL_LINE : GL_FILL);
    }

    glUseProgram(impl->program);
    if (impl->uDebugModeLoc >= 0) glUniform1i(impl->uDebugModeLoc, (GLint)impl->debugMode);
    if (impl->uCamPosLoc >= 0) glUniform3f(impl->uCamPosLoc, camera->position.x, camera->position.y, camera->position.z);
    if (impl->uNavLimitsLoc >= 0) glUniform2f(impl->uNavLimitsLoc, maxSlopeNavRadians, maxWadeNavDepth);
    if (impl->uHeightRangeLoc >= 0) glUniform2f(impl->uHeightRangeLoc, impl->minH, impl->maxH);

    impl->stats.renderedChunks = 0u;
    impl->stats.culledChunks = 0u;
    impl->stats.renderedTriangles = 0u;
    impl->stats.drawCalls = 0u;
    memset(impl->stats.lodCounts, 0, sizeof(impl->stats.lodCounts));

    TerrainFrustum frustum;
    extractTerrainFrustum(&frustum);

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
            if (ch->standardSize) {
                uint32_t mask = 0u;
                if (cz > 0u && impl->chunks[(cz - 1u) * cxN + cx].currentLod > lod) mask |= 1u;
                if (cz + 1u < czN && impl->chunks[(cz + 1u) * cxN + cx].currentLod > lod) mask |= 2u;
                if (cx > 0u && impl->chunks[cz * cxN + (cx - 1u)].currentLod > lod) mask |= 4u;
                if (cx + 1u < cxN && impl->chunks[cz * cxN + (cx + 1u)].currentLod > lod) mask |= 8u;
                ebo = impl->sharedEbo.ebo[lod][mask];
                idxCount = impl->sharedEbo.count[lod][mask];
            } else {
                ebo = ch->customEbo;
                idxCount = ch->customIndexCount;
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
    glPolygonMode(GL_FRONT_AND_BACK, (GLenum)prevPoly[0]);
    if (!prevCull) glDisable(GL_CULL_FACE);
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);
    glUseProgram((GLuint)prevProg);
    return true;
}

TerrainRenderStats TerrainRenderer_GetStats(const TerrainRenderer *tr) {
    if (!tr || !tr->impl) return (TerrainRenderStats){0};
    return ((const TerrainRendererImpl *)tr->impl)->stats;
}
