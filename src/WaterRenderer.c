/**
 * @file WaterRenderer.c
 * @brief Malla estática de regiones húmedas físicas y shader GLSL 330 con color por profundidad,
 *        Fresnel, normales animadas alineadas con el flujo del río y espuma sutil de orilla.
 */
#define GL_GLEXT_PROTOTYPES
#include "WaterRenderer.h"
#include "environment_glsl.inc"
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
    float optics[4];
    float normal[3];
    uint32_t bodyId;
} WaterGpuVertex;

typedef struct WaterRendererImpl {
    GLuint program;
    bool programFailed;
    GLint uCamPosLoc;
    GLint uTimeLoc;
    GLint uQualityLoc;
    GLint uContourLoc;
    TerrainMaterialBindings materialBindings;

    GLuint vao, vbo, ebo;
    uint32_t indexCount, lineCount;
    uint64_t worldEpoch;
    uint64_t worldFingerprint;
    int qualityTier;
    WaterRenderStats stats;
} WaterRendererImpl;

static GLuint compileWaterShader(GLenum type, const char *const *src, GLsizei count) {
    GLuint s = glCreateShader(type);
    if (!s) return 0u;
    glShaderSource(s, count, src, NULL);
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
        "layout(location=3) in vec4 aOptics;\n"
        "layout(location=4) in vec3 aNormal;\n"
        "out vec3 vWorldPos;\n"
        "out float vDepth;\n"
        "out vec2 vFlow;\n"
        "out vec3 vSurfaceNormal;\n"
        "flat out vec4 vOptics;\n"
        "flat out vec4 vParallelView;\n"
        "void main() {\n"
        "  vWorldPos = aPos;\n"
        "  vDepth = aDepth;\n"
        "  vFlow = aFlow;\n"
        "  vSurfaceNormal = aNormal;\n"
        "  vOptics = aOptics;\n"
        "  vParallelView = vec4(gl_ModelViewMatrix[0][2], gl_ModelViewMatrix[1][2],\n"
        "                      gl_ModelViewMatrix[2][2], gl_ProjectionMatrix[3][3]);\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);\n"
        "}\n";

    const char *vertexParts[] = {vs};
    const char *fragParts[] = {"#version 330 compatibility\n", ENV_MATERIALS, ENV_WATER};
    GLuint v = compileWaterShader(GL_VERTEX_SHADER, vertexParts, 1);
    GLuint f = compileWaterShader(GL_FRAGMENT_SHADER, fragParts, 3);
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
    impl->uContourLoc = glGetUniformLocation(p, "uContour");
    impl->materialBindings = TerrainMaterials_GetBindings(p);
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
    impl->lineCount = 0u;
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

typedef struct WaterBuild {
    WaterGpuVertex *vertices;
    uint32_t *indices, *lines;
    uint32_t vertexCount, indexCount, lineCount;
    uint32_t vertexCap, indexCap, lineCap;
    uint32_t *table;
    uint32_t tableCap;
} WaterBuild;

static uint32_t waterVertexHash(const WaterGpuVertex *v) {
    uint32_t x, z;
    memcpy(&x, &v->pos[0], sizeof(x));
    memcpy(&z, &v->pos[2], sizeof(z));
    uint32_t hash = x * 0x9e3779b9u ^ z * 0x85ebca6bu ^ v->bodyId * 0xc2b2ae35u;
    return hash ^ (hash >> 16);
}

static uint32_t waterVertexSlot(const WaterBuild *m, const WaterGpuVertex *v) {
    uint32_t slot = waterVertexHash(v) & (m->tableCap - 1u);
    while (m->table[slot]) {
        const WaterGpuVertex *other = &m->vertices[m->table[slot] - 1u];
        if (other->bodyId == v->bodyId && other->pos[0] == v->pos[0] && other->pos[2] == v->pos[2])
            break;
        slot = (slot + 1u) & (m->tableCap - 1u);
    }
    return slot;
}

static bool reserveWaterVertices(WaterBuild *m) {
    if (m->vertexCount >= m->vertexCap) {
        uint32_t cap = m->vertexCap ? m->vertexCap * 2u : 256u;
        WaterGpuVertex *p = realloc(m->vertices, (size_t)cap * sizeof(*p));
        if (!p) return false;
        m->vertices = p;
        m->vertexCap = cap;
    }
    if (m->vertexCount >= m->tableCap / 2u) {
        uint32_t cap = m->tableCap ? m->tableCap * 2u : 512u;
        uint32_t *table = calloc(cap, sizeof(*table));
        if (!table) return false;
        free(m->table);
        m->table = table;
        m->tableCap = cap;
        for (uint32_t i = 0; i < m->vertexCount; ++i)
            m->table[waterVertexSlot(m, &m->vertices[i])] = i + 1u;
    }
    return true;
}

static bool appendWaterIndices(uint32_t **data, uint32_t *count, uint32_t *capacity,
                               const uint32_t *values, unsigned n) {
    if (*count + n > *capacity) {
        uint32_t cap = *capacity ? *capacity * 2u : 512u;
        uint32_t *p = realloc(*data, (size_t)cap * sizeof(*p));
        if (!p) return false;
        *data = p;
        *capacity = cap;
    }
    memcpy(*data + *count, values, n * sizeof(*values));
    *count += n;
    return true;
}

static void waterOptics(const WorldWaterBody *body, float absorption[4]) {
    /* Los perfiles de presentación no modifican la sustancia ni su contabilidad. */
    typedef struct WaterOptics { const char *id; float sigma[3]; } WaterOptics;
    static const WaterOptics profiles[] = {
        {"clean_water", {1.04f, 0.54f, 0.31f}},
        {"contaminated_water", {1.58f, 0.72f, 0.69f}},
    };
    const WaterOptics *profile = &profiles[0];
    for (size_t i = 1; body && i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        if (strcmp(profiles[i].id, body->substanceId) == 0) profile = &profiles[i];
    }
    memcpy(absorption, profile->sigma, sizeof(profile->sigma));
    absorption[3] = body && body->type == WATER_BODY_RIVER ? 1.0f : 0.0f;
}

static bool appendWaterPatch(WaterBuild *m, const TerrainHeightfield *tf, const WorldHydrology *hy,
                             const WorldWaterPatch *p) {
    WorldWaterVertex polygon[WORLD_WATER_PATCH_VERTICES];
    unsigned count = WorldHydrology_ClipWaterPatch(tf, p, polygon);
    if (!count) return true;
    uint32_t ids[WORLD_WATER_PATCH_VERTICES];
    float optics[4];
    waterOptics(WorldHydrology_GetBody(hy, p->bodyId), optics);
    for (unsigned i = 0; i < count; ++i) {
        const WorldWaterVertex *src = &polygon[i];
        WaterGpuVertex gpu = {
            .pos = {src->position.x, src->position.y, src->position.z},
            .depth = src->depth,
            .flow = {src->flowX, src->flowZ},
            .bodyId = p->bodyId,
        };
        memcpy(gpu.optics, optics, sizeof(optics));
        if (!reserveWaterVertices(m)) return false;
        uint32_t slot = waterVertexSlot(m, &gpu);
        if (!m->table[slot]) {
            m->vertices[m->vertexCount++] = gpu;
            m->table[slot] = m->vertexCount;
        }
        ids[i] = m->table[slot] - 1u;
    }
    for (unsigned i = 2; i < count; ++i) {
        Vector3 a = Vec3_Sub(polygon[i - 1u].position, polygon[0].position);
        Vector3 b = Vec3_Sub(polygon[i].position, polygon[0].position);
        if (a.z * b.x - a.x * b.z <= tf->spacing * tf->spacing * 1e-9f) continue;
        uint32_t tri[] = {ids[0], ids[i - 1u], ids[i]};
        if (!appendWaterIndices(&m->indices, &m->indexCount, &m->indexCap, tri, 3)) return false;
    }
    for (unsigned i = 0; i < count; ++i) {
        unsigned j = (i + 1u) % count;
        if (!(polygon[i].boundary & polygon[j].boundary) || ids[i] == ids[j]) continue;
        uint32_t line[] = {ids[i], ids[j]};
        if (!appendWaterIndices(&m->lines, &m->lineCount, &m->lineCap, line, 2)) return false;
    }
    return true;
}

static bool buildContinuousWater(const TerrainHeightfield *tf, const WorldHydrology *hy, WaterBuild *m) {
    for (uint32_t z = 0; z + 1u < tf->resolution; ++z) {
        for (uint32_t x = 0; x + 1u < tf->resolution; ++x) {
            for (unsigned t = 0; t < 2; ++t) {
                WorldWaterPatch patches[3];
                unsigned count = WorldHydrology_GetWaterPatches(hy, tf, x, z, t, patches);
                for (unsigned k = 0; k < count; ++k) {
                    if (!appendWaterPatch(m, tf, hy, &patches[k])) return false;
                }
            }
        }
    }
    return true;
}

/* Constructor de compatibilidad para la superficie racional histórica v1. */
static bool buildLegacyWater(const TerrainHeightfield *tf, const WorldHydrology *hy, WaterBuild *mesh) {
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

                    WaterGpuVertex *v = &verts[vCount];
                    v->pos[0] = tf->originX + (float)gx * tf->spacing;
                    v->pos[1] = surfY;
                    v->pos[2] = tf->originZ + (float)gz * tf->spacing;
                    v->depth = depth;
                    v->flow[0] = hy->flowX ? hy->flowX[gi] : 0.0f;
                    v->flow[1] = hy->flowZ ? hy->flowZ[gi] : 0.0f;
                    waterOptics(wb, v->optics);
                    v->bodyId = bid ? bid : refBody;
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

    mesh->vertices = verts;
    mesh->indices = indices;
    mesh->vertexCount = vCount;
    mesh->indexCount = iCount;
    return true;
}

static void buildWaterNormals(WaterBuild *mesh) {
    for (uint32_t i = 0; i < mesh->vertexCount; ++i)
        memset(mesh->vertices[i].normal, 0, sizeof(mesh->vertices[i].normal));
    for (uint32_t i = 0; i < mesh->indexCount; i += 3u) {
        const float *a = mesh->vertices[mesh->indices[i]].pos;
        const float *b = mesh->vertices[mesh->indices[i + 1u]].pos;
        const float *c = mesh->vertices[mesh->indices[i + 2u]].pos;
        float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        for (unsigned k = 0; k < 3; ++k) {
            float *normal = mesh->vertices[mesh->indices[i + k]].normal;
            normal[0] += nx;
            normal[1] += ny;
            normal[2] += nz;
        }
    }
    /* Cada normal promedia sólo triángulos reales que comparten vértice y
     * cuerpo en v2. La posición y la pendiente física de cada cara se conservan. */
    for (uint32_t i = 0; i < mesh->vertexCount; ++i) {
        float *n = mesh->vertices[i].normal;
        float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length >= 1e-10f) {
            for (unsigned k = 0; k < 3; ++k) n[k] /= length;
        } else {
            n[1] = 1.0f;
        }
    }
}

bool WaterRenderer_SyncWorld(WaterRenderer *wr, const WorldStaticData *world) {
    if (!wr || !wr->impl) return false;
    WaterRendererImpl *impl = (WaterRendererImpl *)wr->impl;
    if (!world || world->isFlat || !world->hydrology.enabled || world->hydrology.totalWetCells == 0u) {
        freeWaterMesh(impl);
        return true;
    }
    if (impl->worldEpoch == world->epochId && impl->worldFingerprint == world->dataFingerprint) {
        return true;
    }
    if (!initWaterShader(impl)) return false;
    const TerrainHeightfield *tf = &world->terrain;
    const WorldHydrology *hy = &world->hydrology;
    WaterBuild mesh = {0};
    bool ok = hy->surfaceBodyId ? buildContinuousWater(tf, hy, &mesh) : buildLegacyWater(tf, hy, &mesh);
    free(mesh.table);
    if (!ok) {
        free(mesh.vertices); free(mesh.indices); free(mesh.lines);
        return false;
    }
    if (!mesh.indexCount) {
        free(mesh.vertices); free(mesh.indices); free(mesh.lines);
        freeWaterMesh(impl);
        impl->worldEpoch = world->epochId;
        impl->worldFingerprint = world->dataFingerprint;
        return true;
    }
    buildWaterNormals(&mesh);
    GLuint vao, vbo, ebo;

    GLint prevVao = 0, prevBuf = 0, prevEbo = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    size_t vBytes = (size_t)mesh.vertexCount * sizeof(WaterGpuVertex);
    size_t iBytes = (size_t)mesh.indexCount * sizeof(uint32_t);
    size_t lineBytes = (size_t)mesh.lineCount * sizeof(uint32_t);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vBytes, mesh.vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(iBytes + lineBytes), NULL, GL_STATIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)iBytes, mesh.indices);
    if (lineBytes) glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)iBytes, (GLsizeiptr)lineBytes, mesh.lines);

    GLint vboSize = 0, eboSize = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboSize);
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &eboSize);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, depth));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, flow));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, optics));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(WaterGpuVertex), (const void *)offsetof(WaterGpuVertex, normal));

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);

    free(mesh.vertices); free(mesh.indices); free(mesh.lines);
    if (vboSize != (GLint)vBytes || eboSize != (GLint)(iBytes + lineBytes)) {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
        fprintf(stderr, "[WaterRenderer] Fallo al subir la superficie estática.\n");
        return false;
    }
    freeWaterMesh(impl);
    impl->vao = vao;
    impl->vbo = vbo;
    impl->ebo = ebo;
    impl->indexCount = mesh.indexCount;
    impl->lineCount = mesh.lineCount;
    impl->worldEpoch = world->epochId;
    impl->worldFingerprint = world->dataFingerprint;
    impl->stats.triangles = impl->indexCount / 3u;
    impl->stats.vramBytes = vBytes + iBytes + lineBytes;
    return true;
}

bool WaterRenderer_Render(WaterRenderer *wr, const ICamera *camera,
                          const TerrainMaterials *materials, float timeSeconds, bool showContour) {
    if (!wr || !wr->impl || !camera) return false;
    WaterRendererImpl *impl = (WaterRendererImpl *)wr->impl;
    impl->stats.drawCalls = 0u;
    if (!impl->vao || impl->indexCount == 0u || !impl->program) return true;

    GLint prevProg = 0, prevVao = 0, prevEbo = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevEbo);
    GLint blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha, equationRGB, equationAlpha, depthFunc;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(impl->program);
    TerrainMaterialState materialState = TerrainMaterials_Bind(materials, &impl->materialBindings);
    glUniform1i(impl->uContourLoc, GL_FALSE);
    if (impl->uCamPosLoc >= 0) glUniform3f(impl->uCamPosLoc, camera->position.x, camera->position.y, camera->position.z);
    if (impl->uTimeLoc >= 0) glUniform1f(impl->uTimeLoc, fmodf(timeSeconds, 80.0f));
    if (impl->uQualityLoc >= 0) glUniform1i(impl->uQualityLoc, impl->qualityTier);

    glBindVertexArray(impl->vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl->ebo);
    glDrawElements(GL_TRIANGLES, (GLsizei)impl->indexCount, GL_UNSIGNED_INT, 0);
    impl->stats.drawCalls = 1u;
    if (showContour && impl->lineCount) {
        glUniform1i(impl->uContourLoc, GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glDrawElements(GL_LINES, (GLsizei)impl->lineCount, GL_UNSIGNED_INT,
                       (const void *)((size_t)impl->indexCount * sizeof(uint32_t)));
        ++impl->stats.drawCalls;
    }

    /* Restaurar estado OpenGL completo para pasadas posteriores (HUD/overlays). */
    glDepthMask(prevDepthMask);
    glDepthFunc((GLenum)depthFunc);
    if (!prevDepth) glDisable(GL_DEPTH_TEST);
    glBlendEquationSeparate((GLenum)equationRGB, (GLenum)equationAlpha);
    glBlendFuncSeparate((GLenum)blendSrcRGB, (GLenum)blendDstRGB,
                        (GLenum)blendSrcAlpha, (GLenum)blendDstAlpha);
    if (!prevBlend) glDisable(GL_BLEND);
    if (prevCull) glEnable(GL_CULL_FACE);
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)prevEbo);
    TerrainMaterials_Restore(&materialState);
    glUseProgram((GLuint)prevProg);
    return true;
}

WaterRenderStats WaterRenderer_GetStats(const WaterRenderer *wr) {
    if (!wr || !wr->impl) return (WaterRenderStats){0};
    return ((const WaterRendererImpl *)wr->impl)->stats;
}
