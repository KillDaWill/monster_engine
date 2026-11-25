/**
 * @file RockVisualCache.c
 * @brief Extracción de mallas SDF cerradas por variante geológica y renderizado instanciado GPU.
 */
#define GL_GLEXT_PROTOTYPES
#include "RockVisualCache.h"
#include "environment_glsl.inc"
#include "SceneLighting.h"
#include "Quaternion.h"

#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct RockGpuVertex {
    float pos[3];
    float normal[3];
    float shade; /* Variación mineral / oclusión local [0.55..1.15]. */
} RockGpuVertex;

typedef struct RockGpuInstance {
    float pos[3];
    float scale[3];
    float quat[4];
    float tint[3];
} RockGpuInstance;

typedef struct RockVariantMesh {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLuint instVbo;
    uint32_t indexCount;
    uint32_t instanceCap;
    uint32_t *sourceIndices;     /* Índices a world->geology.rocks[] de esta variante. */
    uint32_t sourceCount;
    RockGpuInstance *scratchInst;
    size_t vramBytes;
} RockVariantMesh;

typedef struct RockVisualCacheImpl {
    GLuint program, depthProgram;
    bool depthPass;
    GLint uCamPosLoc;
    GLint uDetailLoc;
    TerrainMaterialBindings materialBindings;

    RockVariantMesh *variants;
    uint32_t variantCount;
    const WorldStaticData *boundWorld;
    uint32_t worldEpoch;
    uint64_t worldFingerprint;

    RockRenderStats stats;
} RockVisualCacheImpl;

static const char *kRockVertSrc =
    "#version 330 compatibility\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in float aShade;\n"
    "layout(location=3) in vec3 iPos;\n"
    "layout(location=4) in vec3 iScale;\n"
    "layout(location=5) in vec4 iQuat;\n"
    "layout(location=6) in vec3 iTint;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vWorldNormal;\n"
    "out vec3 vTint;\n"
    "out float vShade;\n"
    "vec3 qrot(vec4 q, vec3 v) {\n"
    "    vec3 t = 2.0 * cross(q.xyz, v);\n"
    "    return v + q.w * t + cross(q.xyz, t);\n"
    "}\n"
    "void main() {\n"
    "    vec3 scaled = aPos * iScale;\n"
    "    vec3 wp = iPos + qrot(iQuat, scaled);\n"
    "    vec3 invS = 1.0 / max(abs(iScale), vec3(0.05));\n"
    "    vec3 wn = normalize(qrot(iQuat, aNormal * invS));\n"
    "    vWorldPos = wp;\n"
    "    vWorldNormal = wn;\n"
    "    vTint = iTint;\n"
    "    vShade = aShade;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(wp, 1.0);\n"
    "}\n";

static const char *kRockFragSrc =
    "uniform vec3 uCamPos;\n"
    "uniform bool uRockDetail;\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vWorldNormal;\n"
    "in vec3 vTint;\n"
    "in float vShade;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(vWorldNormal);\n"
    "    GroundMaterial rock = GroundMaterial(materialMean(2), vec3(0), vec2(.75,.94));\n"
    "    if (uHasMaterials) rock = rockMaterial(vWorldPos, n, dFdx(vWorldPos), dFdy(vWorldPos), uRockDetail, sceneExtras.x > .5);\n"
    "    vec3 color = rock.color * mix(vec3(.95), vTint * 1.75, .16);\n"
    "    vec3 mappedNormal = materialNormal(n, rock.gradient * .62);\n"
    "    vec3 lit = illuminateGround(color, vWorldPos, mappedNormal, n, sceneViewDirection(vWorldPos), rock.masks.x, rock.masks.y * mix(1.0,vShade,.38));\n"
    "    FragColor = vec4(sceneObjectOutput(lit, vWorldPos), 1.0);\n"
    "}\n";

static GLuint compileShader(GLenum type, const char *const *src, GLsizei count) {
    GLuint sh = glCreateShader(type);
    if (!sh) return 0;
    glShaderSource(sh, count, src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char logBuf[512];
        glGetShaderInfoLog(sh, sizeof(logBuf), NULL, logBuf);
        fprintf(stderr, "[RockVisualCache] Error compilando shader: %s\n", logBuf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static bool ensureShader(RockVisualCacheImpl *impl) {
    if (impl->program != 0) return true;
    const char *vsParts[] = {kRockVertSrc};
    const char *fsParts[] = {"#version 330 compatibility\n", ENV_SCENE_LIGHTING, ENV_MATERIALS, ENV_PROJECTION, kRockFragSrc};
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsParts, 1);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsParts, 5);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    impl->depthProgram = SceneLighting_LinkDepth(vs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char logBuf[512];
        glGetProgramInfoLog(prog, sizeof(logBuf), NULL, logBuf);
        fprintf(stderr, "[RockVisualCache] Error enlazando shader: %s\n", logBuf);
        glDeleteProgram(prog);
        return false;
    }
    SceneLighting_LinkProgram(prog);
    impl->program = prog;
    impl->materialBindings = TerrainMaterials_GetBindings(prog);
    impl->uDetailLoc = glGetUniformLocation(prog, "uRockDetail");
    impl->uCamPosLoc = glGetUniformLocation(prog, "uCamPos");
    return true;
}

static Vector3 vec3NormSafe(Vector3 v) {
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l < 1e-6f) return (Vector3){0.0f, 1.0f, 0.0f};
    return (Vector3){v.x / l, v.y / l, v.z / l};
}

static Vector3 sdfGradient(const WorldRockVariant *v, Vector3 p) {
    const float e = 0.015f;
    float dx = WorldGeology_EvaluateVariantSDF(v, (Vector3){p.x + e, p.y, p.z}) -
               WorldGeology_EvaluateVariantSDF(v, (Vector3){p.x - e, p.y, p.z});
    float dy = WorldGeology_EvaluateVariantSDF(v, (Vector3){p.x, p.y + e, p.z}) -
               WorldGeology_EvaluateVariantSDF(v, (Vector3){p.x, p.y - e, p.z});
    float dz = WorldGeology_EvaluateVariantSDF(v, (Vector3){p.x, p.y, p.z + e}) -
               WorldGeology_EvaluateVariantSDF(v, (Vector3){p.x, p.y, p.z - e});
    return vec3NormSafe((Vector3){dx, dy, dz});
}

/* Subdivisión de icosaedro unitario para malla cerrada determinista (nivel 2 = 162 verts, 320 tris). */
typedef struct EdgeKey {
    uint32_t a, b;
    uint32_t mid;
} EdgeKey;

static uint32_t getMidpoint(uint32_t a, uint32_t b, Vector3 *verts, uint32_t *vCount,
                            EdgeKey *edges, uint32_t *eCount) {
    uint32_t lo = (a < b) ? a : b;
    uint32_t hi = (a < b) ? b : a;
    for (uint32_t i = 0; i < *eCount; ++i) {
        if (edges[i].a == lo && edges[i].b == hi) return edges[i].mid;
    }
    Vector3 m = {
        0.5f * (verts[lo].x + verts[hi].x),
        0.5f * (verts[lo].y + verts[hi].y),
        0.5f * (verts[lo].z + verts[hi].z)
    };
    uint32_t idx = (*vCount)++;
    verts[idx] = vec3NormSafe(m);
    edges[*eCount].a = lo;
    edges[*eCount].b = hi;
    edges[*eCount].mid = idx;
    (*eCount)++;
    return idx;
}

static bool buildUnitIcosphere(int subdivs, Vector3 **outDirs, uint32_t *outVCount,
                               uint32_t **outIdx, uint32_t *outICount) {
    /* Nivel 2: 162 vértices, 320 triángulos (960 índices). */
    const float t = (1.0f + sqrtf(5.0f)) * 0.5f;
    static const float rawV[12][3] = {
        {-1,  1,  0}, { 1,  1,  0}, {-1, -1,  0}, { 1, -1,  0},
        { 0, -1,  1}, { 0,  1,  1}, { 0, -1, -1}, { 0,  1, -1},
        { 1,  0, -1}, { 1,  0,  1}, {-1,  0, -1}, {-1,  0,  1}
    };
    static const uint32_t rawF[20][3] = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1}
    };

    uint32_t maxV = 12u;
    uint32_t maxT = 20u;
    for (int s = 0; s < subdivs; ++s) {
        maxT *= 4u;
        maxV = maxT / 2u + 2u;
    }

    Vector3 *verts = (Vector3 *)malloc((size_t)(maxV + 8u) * sizeof(Vector3));
    uint32_t *idxA = (uint32_t *)malloc((size_t)maxT * 3u * sizeof(uint32_t));
    uint32_t *idxB = (uint32_t *)malloc((size_t)maxT * 3u * sizeof(uint32_t));
    EdgeKey *edges = (EdgeKey *)malloc((size_t)(maxT * 3u) * sizeof(EdgeKey));
    if (!verts || !idxA || !idxB || !edges) {
        free(verts); free(idxA); free(idxB); free(edges);
        return false;
    }

    for (int i = 0; i < 12; ++i) {
        float vx = rawV[i][0];
        float vy = rawV[i][1];
        float vz = rawV[i][2];
        if (i < 4) vy *= t;
        else if (i < 8) vz *= t;
        else vx *= t;
        verts[i] = vec3NormSafe((Vector3){vx, vy, vz});
    }
    uint32_t vCount = 12u;
    uint32_t tCount = 20u;
    for (uint32_t i = 0; i < 20u; ++i) {
        idxA[i * 3u + 0u] = rawF[i][0];
        idxA[i * 3u + 1u] = rawF[i][1];
        idxA[i * 3u + 2u] = rawF[i][2];
    }

    for (int s = 0; s < subdivs; ++s) {
        uint32_t eCount = 0u;
        uint32_t nt = 0u;
        for (uint32_t f = 0; f < tCount; ++f) {
            uint32_t a = idxA[f * 3u + 0u];
            uint32_t b = idxA[f * 3u + 1u];
            uint32_t c = idxA[f * 3u + 2u];
            uint32_t ab = getMidpoint(a, b, verts, &vCount, edges, &eCount);
            uint32_t bc = getMidpoint(b, c, verts, &vCount, edges, &eCount);
            uint32_t ca = getMidpoint(c, a, verts, &vCount, edges, &eCount);
            idxB[nt * 3u + 0u] = a;  idxB[nt * 3u + 1u] = ab; idxB[nt * 3u + 2u] = ca; nt++;
            idxB[nt * 3u + 0u] = b;  idxB[nt * 3u + 1u] = bc; idxB[nt * 3u + 2u] = ab; nt++;
            idxB[nt * 3u + 0u] = c;  idxB[nt * 3u + 1u] = ca; idxB[nt * 3u + 2u] = bc; nt++;
            idxB[nt * 3u + 0u] = ab; idxB[nt * 3u + 1u] = bc; idxB[nt * 3u + 2u] = ca; nt++;
        }
        uint32_t *tmp = idxA;
        idxA = idxB;
        idxB = tmp;
        tCount = nt;
    }

    free(idxB);
    free(edges);
    *outDirs = verts;
    *outVCount = vCount;
    *outIdx = idxA;
    *outICount = tCount * 3u;
    return true;
}

static bool buildVariantMesh(const WorldRockVariant *variant, RockVariantMesh *outMesh) {
    Vector3 *dirs = NULL;
    uint32_t vCount = 0u;
    uint32_t *indices = NULL;
    uint32_t iCount = 0u;
    if (!buildUnitIcosphere(2, &dirs, &vCount, &indices, &iCount)) return false;

    RockGpuVertex *verts = (RockGpuVertex *)calloc(vCount, sizeof(RockGpuVertex));
    if (!verts) {
        free(dirs);
        free(indices);
        return false;
    }

    /* Proyectar cada dirección radial sobre la isosuperficie cero de WorldGeology_EvaluateVariantSDF. */
    float maxR = fmaxf(variant->conservativeRadiusXZ,
                       fmaxf(fabsf(variant->conservativeMinY), fabsf(variant->conservativeMaxY))) * 1.35f + 0.4f;
    for (uint32_t i = 0; i < vCount; ++i) {
        Vector3 d = dirs[i];
        float lo = 0.02f;
        float hi = maxR;
        /* Encontrar intervalo [lo, hi] donde SDF cruza 0 (interior < 0, exterior > 0). */
        float sLo = WorldGeology_EvaluateVariantSDF(variant, (Vector3){d.x * lo, d.y * lo, d.z * lo});
        if (sLo > 0.0f) {
            /* Si el origen quedase fuera en un clúster asimétrico, buscar el mínimo a lo largo del rayo. */
            float bestT = lo;
            float bestS = sLo;
            for (int k = 1; k <= 16; ++k) {
                float t = (maxR * (float)k) / 16.0f;
                float s = WorldGeology_EvaluateVariantSDF(variant, (Vector3){d.x * t, d.y * t, d.z * t});
                if (s < bestS) { bestS = s; bestT = t; }
            }
            lo = bestT;
        }
        /* Bisección radial de 18 pasos (precisión < 1e-5). */
        for (int it = 0; it < 18; ++it) {
            float mid = 0.5f * (lo + hi);
            float sm = WorldGeology_EvaluateVariantSDF(variant, (Vector3){d.x * mid, d.y * mid, d.z * mid});
            if (sm <= 0.0f) lo = mid;
            else hi = mid;
        }
        float r = 0.5f * (lo + hi);
        Vector3 p = {d.x * r, d.y * r, d.z * r};

        /* 2 pasos suaves de proyección por gradiente para agudizar planos de fractura sin doblar caras. */
        for (int step = 0; step < 2; ++step) {
            float sd = WorldGeology_EvaluateVariantSDF(variant, p);
            if (fabsf(sd) < 1e-4f) break;
            Vector3 g = sdfGradient(variant, p);
            float clampStep = fmaxf(-0.08f, fminf(0.08f, sd));
            p.x -= g.x * clampStep;
            p.y -= g.y * clampStep;
            p.z -= g.z * clampStep;
        }

        Vector3 n = sdfGradient(variant, p);
        /* Oclusión aproximada por distancia al centro y curvatura respecto a la normal radial. */
        float convex = d.x * n.x + d.y * n.y + d.z * n.z;
        float shade = 0.78f + 0.24f * fmaxf(0.0f, convex) + 0.08f * n.y;
        if (shade < 0.58f) shade = 0.58f;
        if (shade > 1.12f) shade = 1.12f;

        verts[i].pos[0] = p.x;
        verts[i].pos[1] = p.y;
        verts[i].pos[2] = p.z;
        verts[i].normal[0] = n.x;
        verts[i].normal[1] = n.y;
        verts[i].normal[2] = n.z;
        verts[i].shade = shade;
    }

    /* En rocas fracturadas, mezclar ligeramente la normal geométrica de triángulos para aristas nítidas. */
    if (variant->family == ROCK_FAMILY_FRACTURED || variant->family == ROCK_FAMILY_BEDROCK) {
        Vector3 *accN = (Vector3 *)calloc(vCount, sizeof(Vector3));
        if (accN) {
            for (uint32_t t = 0; t < iCount; t += 3u) {
                uint32_t i0 = indices[t], i1 = indices[t + 1u], i2 = indices[t + 2u];
                Vector3 a = {verts[i0].pos[0], verts[i0].pos[1], verts[i0].pos[2]};
                Vector3 b = {verts[i1].pos[0], verts[i1].pos[1], verts[i1].pos[2]};
                Vector3 c = {verts[i2].pos[0], verts[i2].pos[1], verts[i2].pos[2]};
                Vector3 e1 = {b.x - a.x, b.y - a.y, b.z - a.z};
                Vector3 e2 = {c.x - a.x, c.y - a.y, c.z - a.z};
                Vector3 fn = {
                    e1.y * e2.z - e1.z * e2.y,
                    e1.z * e2.x - e1.x * e2.z,
                    e1.x * e2.y - e1.y * e2.x
                };
                accN[i0].x += fn.x; accN[i0].y += fn.y; accN[i0].z += fn.z;
                accN[i1].x += fn.x; accN[i1].y += fn.y; accN[i1].z += fn.z;
                accN[i2].x += fn.x; accN[i2].y += fn.y; accN[i2].z += fn.z;
            }
            for (uint32_t i = 0; i < vCount; ++i) {
                Vector3 gn = vec3NormSafe(accN[i]);
                Vector3 mixN = vec3NormSafe((Vector3){
                    verts[i].normal[0] * 0.45f + gn.x * 0.55f,
                    verts[i].normal[1] * 0.45f + gn.y * 0.55f,
                    verts[i].normal[2] * 0.45f + gn.z * 0.55f
                });
                verts[i].normal[0] = mixN.x;
                verts[i].normal[1] = mixN.y;
                verts[i].normal[2] = mixN.z;
            }
            free(accN);
        }
    }

    GLint prevVao = 0, prevBuf = 0, prevEbo = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);

    glGenVertexArrays(1, &outMesh->vao);
    glGenBuffers(1, &outMesh->vbo);
    glGenBuffers(1, &outMesh->ebo);
    glGenBuffers(1, &outMesh->instVbo);

    glBindVertexArray(outMesh->vao);
    glBindBuffer(GL_ARRAY_BUFFER, outMesh->vbo);
    size_t vBytes = (size_t)vCount * sizeof(RockGpuVertex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vBytes, verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, outMesh->ebo);
    size_t iBytes = (size_t)iCount * sizeof(uint32_t);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)iBytes, indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RockGpuVertex), (const void *)offsetof(RockGpuVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RockGpuVertex), (const void *)offsetof(RockGpuVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(RockGpuVertex), (const void *)offsetof(RockGpuVertex, shade));

    /* Atributos por instancia (divisor = 1). */
    glBindBuffer(GL_ARRAY_BUFFER, outMesh->instVbo);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(RockGpuInstance), (const void *)offsetof(RockGpuInstance, pos));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(RockGpuInstance), (const void *)offsetof(RockGpuInstance, scale));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(RockGpuInstance), (const void *)offsetof(RockGpuInstance, quat));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(RockGpuInstance), (const void *)offsetof(RockGpuInstance, tint));
    glVertexAttribDivisor(6, 1);

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);

    outMesh->indexCount = iCount;
    outMesh->vramBytes = vBytes + iBytes;

    free(verts);
    free(dirs);
    free(indices);
    return true;
}

static void freeVariants(RockVisualCacheImpl *impl) {
    if (!impl || !impl->variants) return;
    for (uint32_t i = 0; i < impl->variantCount; ++i) {
        RockVariantMesh *vm = &impl->variants[i];
        if (vm->vao) glDeleteVertexArrays(1, &vm->vao);
        if (vm->vbo) glDeleteBuffers(1, &vm->vbo);
        if (vm->ebo) glDeleteBuffers(1, &vm->ebo);
        if (vm->instVbo) glDeleteBuffers(1, &vm->instVbo);
        free(vm->sourceIndices);
        free(vm->scratchInst);
    }
    free(impl->variants);
    impl->variants = NULL;
    impl->variantCount = 0u;
    impl->boundWorld = NULL;
}

bool RockVisualCache_Init(RockVisualCache *cache) {
    if (!cache) return false;
    memset(cache, 0, sizeof(*cache));
    RockVisualCacheImpl *impl = (RockVisualCacheImpl *)calloc(1, sizeof(RockVisualCacheImpl));
    if (!impl) return false;
    cache->impl = impl;
    return true;
}

void RockVisualCache_Destroy(RockVisualCache *cache) {
    if (!cache || !cache->impl) return;
    RockVisualCacheImpl *impl = (RockVisualCacheImpl *)cache->impl;
    freeVariants(impl);
    if (impl->program) glDeleteProgram(impl->program);
    if (impl->depthProgram) glDeleteProgram(impl->depthProgram);
    free(impl);
    cache->impl = NULL;
}

bool RockVisualCache_SyncWorld(RockVisualCache *cache, const WorldStaticData *world) {
    if (!cache || !cache->impl) return false;
    RockVisualCacheImpl *impl = (RockVisualCacheImpl *)cache->impl;
    if (!world || world->geology.variantCount == 0u || world->geology.rockCount == 0u) {
        freeVariants(impl);
        impl->worldEpoch = world ? world->epochId : 0u;
        impl->worldFingerprint = world ? world->dataFingerprint : 0u;
        memset(&impl->stats, 0, sizeof(impl->stats));
        return true;
    }

    if (impl->boundWorld == world &&
        impl->worldEpoch == world->epochId &&
        impl->worldFingerprint == world->dataFingerprint &&
        impl->variants != NULL) {
        return true;
    }

    if (!ensureShader(impl)) return false;
    freeVariants(impl);

    const WorldGeology *geo = &world->geology;
    impl->variants = (RockVariantMesh *)calloc(geo->variantCount, sizeof(RockVariantMesh));
    if (!impl->variants) return false;
    impl->variantCount = geo->variantCount;

    /* Contar instancias por variante. */
    for (uint32_t r = 0; r < geo->rockCount; ++r) {
        uint32_t vi = geo->rocks[r].variantIndex;
        if (vi < geo->variantCount) impl->variants[vi].sourceCount++;
    }

    size_t totalVram = 0u;
    for (uint32_t v = 0; v < geo->variantCount; ++v) {
        RockVariantMesh *vm = &impl->variants[v];
        if (vm->sourceCount == 0u) continue;
        if (!buildVariantMesh(&geo->variants[v], vm)) {
            freeVariants(impl);
            return false;
        }
        vm->sourceIndices = (uint32_t *)malloc((size_t)vm->sourceCount * sizeof(uint32_t));
        vm->scratchInst = (RockGpuInstance *)malloc((size_t)vm->sourceCount * sizeof(RockGpuInstance));
        if (!vm->sourceIndices || !vm->scratchInst) {
            freeVariants(impl);
            return false;
        }
        vm->instanceCap = vm->sourceCount;
        vm->sourceCount = 0u;

        GLint prevBuf = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
        glBindBuffer(GL_ARRAY_BUFFER, vm->instVbo);
        size_t instBytes = (size_t)vm->instanceCap * sizeof(RockGpuInstance);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)instBytes, NULL, GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);

        vm->vramBytes += instBytes;
        totalVram += vm->vramBytes;
    }

    for (uint32_t r = 0; r < geo->rockCount; ++r) {
        uint32_t vi = geo->rocks[r].variantIndex;
        if (vi < geo->variantCount && impl->variants[vi].sourceIndices) {
            impl->variants[vi].sourceIndices[impl->variants[vi].sourceCount++] = r;
        }
    }

    impl->boundWorld = world;
    impl->worldEpoch = world->epochId;
    impl->worldFingerprint = world->dataFingerprint;
    impl->stats.variantCount = geo->variantCount;
    impl->stats.totalInstances = geo->rockCount;
    impl->stats.vramBytes = totalVram;
    return true;
}

/* Culling por planos de frustum extraídos de MVP actual. */
typedef struct RockFrustum {
    float p[6][4];
} RockFrustum;

static void extractFrustumFromGL(RockFrustum *f) {
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

static bool sphereInFrustum(const RockFrustum *f, Vector3 c, float r) {
    for (int i = 0; i < 6; ++i) {
        if (f->p[i][0] * c.x + f->p[i][1] * c.y + f->p[i][2] * c.z + f->p[i][3] < -r) {
            return false;
        }
    }
    return true;
}

bool RockVisualCache_Render(RockVisualCache *cache, const ICamera *camera,
                            const TerrainMaterials *materials, bool materialDetail) {
    if (!cache || !cache->impl || !camera) return false;
    RockVisualCacheImpl *impl = (RockVisualCacheImpl *)cache->impl;
    impl->stats.renderedInstances = 0u;
    impl->stats.culledInstances = 0u;
    impl->stats.renderedTriangles = 0u;
    impl->stats.drawCalls = 0u;

    if (!impl->boundWorld || !impl->variants || !impl->program) return true;
    const WorldGeology *geo = &impl->boundWorld->geology;

    RockFrustum frustum;
    extractFrustumFromGL(&frustum);

    GLint prevProg = 0, prevVao = 0, prevBuf = 0, prevEbo = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);
    GLint cullMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glUseProgram(impl->depthPass ? impl->depthProgram : impl->program);
    TerrainMaterialState materialState = {0};
    if (!impl->depthPass) {
        materialState = TerrainMaterials_Bind(materials, &impl->materialBindings);
        glUniform1i(impl->uDetailLoc, materialDetail);
        if (impl->uCamPosLoc >= 0) {
            glUniform3f(impl->uCamPosLoc, camera->position.x, camera->position.y, camera->position.z);
        }
    }

    for (uint32_t v = 0; v < impl->variantCount; ++v) {
        RockVariantMesh *vm = &impl->variants[v];
        if (vm->sourceCount == 0u || !vm->vao) continue;

        uint32_t visCount = 0u;
        for (uint32_t i = 0; i < vm->sourceCount; ++i) {
            const WorldRockInstance *rk = &geo->rocks[vm->sourceIndices[i]];
            float boundR = fmaxf(rk->boundRadiusXZ, 0.5f * (rk->maxY - rk->minY)) * 1.25f + 0.5f;
            if (!sphereInFrustum(&frustum, rk->position, boundR)) {
                impl->stats.culledInstances++;
                continue;
            }
            RockGpuInstance *dst = &vm->scratchInst[visCount++];
            dst->pos[0] = rk->position.x;
            dst->pos[1] = rk->position.y;
            dst->pos[2] = rk->position.z;
            dst->scale[0] = rk->scale.x;
            dst->scale[1] = rk->scale.y;
            dst->scale[2] = rk->scale.z;
            dst->quat[0] = rk->orientation.x;
            dst->quat[1] = rk->orientation.y;
            dst->quat[2] = rk->orientation.z;
            dst->quat[3] = rk->orientation.w;
            dst->tint[0] = rk->tint[0];
            dst->tint[1] = rk->tint[1];
            dst->tint[2] = rk->tint[2];
        }

        if (visCount == 0u) continue;

        glBindBuffer(GL_ARRAY_BUFFER, vm->instVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((size_t)visCount * sizeof(RockGpuInstance)),
                        vm->scratchInst);

        glBindVertexArray(vm->vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vm->ebo);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)vm->indexCount, GL_UNSIGNED_INT, 0, (GLsizei)visCount);

        impl->stats.renderedInstances += visCount;
        impl->stats.renderedTriangles += (uint64_t)(vm->indexCount / 3u) * (uint64_t)visCount;
        impl->stats.drawCalls++;
    }

    if (!prevDepth) glDisable(GL_DEPTH_TEST);
    if (!prevCull) glDisable(GL_CULL_FACE);
    glCullFace((GLenum)cullMode);
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);
    if (!impl->depthPass) TerrainMaterials_Restore(&materialState);
    glUseProgram((GLuint)prevProg);
    return true;
}

static void drawWireCylinder(Vector3 center, float radius, float halfH) {
    const int segs = 16;
    float y0 = center.y - halfH;
    float y1 = center.y + halfH;
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segs; ++i) {
        float a = (float)i * (2.0f * (float)M_PI / (float)segs);
        glVertex3f(center.x + cosf(a) * radius, y0, center.z + sinf(a) * radius);
    }
    glEnd();
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segs; ++i) {
        float a = (float)i * (2.0f * (float)M_PI / (float)segs);
        glVertex3f(center.x + cosf(a) * radius, y1, center.z + sinf(a) * radius);
    }
    glEnd();
    glBegin(GL_LINES);
    for (int i = 0; i < 4; ++i) {
        float a = (float)i * (0.5f * (float)M_PI);
        float vx = center.x + cosf(a) * radius;
        float vz = center.z + sinf(a) * radius;
        glVertex3f(vx, y0, vz);
        glVertex3f(vx, y1, vz);
    }
    glEnd();
}

void RockVisualCache_RenderCollisionDebug(const RockVisualCache *cache, const WorldStaticData *world,
                                          const ICamera *camera) {
    (void)cache;
    if (!world || !camera || world->geology.rockCount == 0u) return;

    GLint prevProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glUseProgram(0);
    GLboolean prevLight = glIsEnabled(GL_LIGHTING);
    glDisable(GL_LIGHTING);
    glLineWidth(1.6f);

    const WorldGeology *geo = &world->geology;
    for (uint32_t i = 0; i < geo->rockCount; ++i) {
        const WorldRockInstance *rk = &geo->rocks[i];
        if (!rk->collides) continue;
        float dx = rk->position.x - camera->position.x;
        float dz = rk->position.z - camera->position.z;
        if (dx * dx + dz * dz > 260.0f * 260.0f) continue;

        glColor3f(1.0f, 0.35f, 0.15f);
        for (uint8_t l = 0; l < rk->lobeCount; ++l) {
            drawWireCylinder(rk->lobes[l].center, rk->lobes[l].radiusXZ, rk->lobes[l].halfHeight);
        }
    }

    glLineWidth(1.0f);
    if (prevLight) glEnable(GL_LIGHTING);
    glUseProgram((GLuint)prevProg);
}

RockRenderStats RockVisualCache_GetStats(const RockVisualCache *cache) {
    if (!cache || !cache->impl) return (RockRenderStats){0};
    return ((const RockVisualCacheImpl *)cache->impl)->stats;
}

void RockVisualCache_RenderShadow(RockVisualCache *rocks, const ICamera *camera) {
    RockVisualCacheImpl *impl = rocks->impl;
    if (!impl || !impl->depthProgram) return;
    impl->depthPass = true;
    RockVisualCache_Render(rocks, camera, NULL, false);
    impl->depthPass = false;
}
