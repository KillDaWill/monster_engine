#define GL_GLEXT_PROTOTYPES
#include "EnvironmentDetailRenderer.h"
#include "OpenGLRenderer.h"
#include "PrimitiveMesh.h"
#include "TerrainGenerator.h"
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { CONTACT_SIZE = 512, DETAIL_LIMIT = 2600, DETAIL_TYPES = 3 };
enum { MEADOW_CLUMP, SHORE_REED, SMALL_STONE };

typedef struct DecorationCell {
    OpenGLMeshInstance instance;
    uint8_t type; // cero significa vacío
} DecorationCell;

typedef struct DetailState {
    GLuint contacts;
    uint8_t *pixels;
    DecorationCell *cells;
    unsigned columns;
    float spacing;
    Mesh meshes[DETAIL_TYPES];
    OpenGLMeshInstance batch[DETAIL_LIMIT];
} DetailState;

static uint32_t hashCell(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)z * 0xd8163841u ^ seed;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    return h ^ (h >> 16);
}

static float roll(uint32_t h, unsigned shift) { return (float)((h >> shift) & 255u) / 255.0f; }

static bool makeLeaves(Mesh *mesh, bool reed) {
    int leaves = reed ? 5 : 8;
    for (int blade = 0; blade < leaves; ++blade) {
        uint32_t rng = hashCell(blade, leaves, 7347u);
        float angle = (float)blade * 2.3999632f;
        float height = (reed ? .59f : .22f) * (.55f + .70f * roll(rng, 0));
        float bend = height * (reed ? .28f : .57f) * (.5f + roll(rng, 8));
        float width = height * (reed ? .060f : .095f);
        Vector3 out = {cosf(angle), 0, sinf(angle)}, side = {-out.z, 0, out.x};
        Vector3 root = Vec3_Scale(out, .024f + .048f * roll(rng, 16));
        MeshIndex indices[6];
        for (int row = 0; row < 3; ++row) {
            const float t = .5f * row;
            for (int edge = 0; edge < 2; ++edge) {
                Vector3 p = Vec3_Add(root, Vec3_Scale(out, bend * t * t));
                p = Vec3_Add(p, Vec3_Scale(side, width * (1.01f - t) * (edge == 0 ? -1 : 1)));
                p.y = height * t;
                Color color = {86 + 20 * row, 103 + 13 * row, 50 + 6 * row, 255};
                Vector3 normal = Vec3_Normalize(Vec3_Add(Vec3_Scale(out, -.7f), Vec3_Create(0, .8f, 0)));
                if (!Mesh_AddVertex(mesh, (MeshVertex){.position = p, .normal = normal, .color = color},
                                    &indices[2 * row + edge])) return false;
            }
        }
        for (int j = 0; j < 4; j += 2) {
            if (!Mesh_AddTriangle(mesh, indices[j], indices[j+2], indices[j+1]) ||
                !Mesh_AddTriangle(mesh, indices[j+1], indices[j+2], indices[j+3])) return false;
        }
    }
    mesh->doubleSided = true;
    return true;
}

bool EnvironmentDetailRenderer_Init(EnvironmentDetailRenderer *details) {
    DetailState *state = calloc(1, sizeof(*state));
    details->impl = state;
    if (!state) return false;
    for (int i = 0; i < DETAIL_TYPES; ++i) state->meshes[i] = Mesh_Create();
    if (!makeLeaves(&state->meshes[MEADOW_CLUMP], false) ||
        !makeLeaves(&state->meshes[SHORE_REED], true) ||
        !PrimitiveMesh_GenerateEllipsoid(&state->meshes[SMALL_STONE],
            (Transform3D){.position={0,.032f,0},.scale={.095f,.052f,.070f}}, 7, 4,
            Color_FromRGB(131,125,112))) {
        EnvironmentDetailRenderer_Destroy(details);
        return false;
    }
    return true;
}

void EnvironmentDetailRenderer_SyncWorld(EnvironmentDetailRenderer *details) {
    DetailState *state = details->impl;
    if (!state) return;
    free(state->cells);
    state->cells = NULL;
    state->columns = 0;
}

void EnvironmentDetailRenderer_Destroy(EnvironmentDetailRenderer *details) {
    DetailState *state = details->impl;
    if (!state) return;
    EnvironmentDetailRenderer_SyncWorld(details);
    glDeleteTextures(1, &state->contacts);
    free(state->pixels);
    for (int i = 0; i < DETAIL_TYPES; ++i) Mesh_Free(&state->meshes[i]);
    free(state);
    details->impl = NULL;
}

static bool buildDecoration(DetailState *state, const WorldStaticData *world) {
    const TerrainHeightfield *tf = &world->terrain;
    const WorldBiomeField *biome = &world->biome;
    unsigned columns = (unsigned)fminf(384, ceilf(tf->span / 1.05f));
    state->cells = calloc((size_t)columns * columns, sizeof(*state->cells));
    if (!state->cells) return false;
    state->columns = columns;
    state->spacing = tf->span / columns;
    for (unsigned z = 0; z < columns; ++z) {
        for (unsigned x = 0; x < columns; ++x) {
            uint32_t rng = hashCell((int)x, (int)z, world->masterSeed ^ 0x69b62f1bu);
            if (roll(rng, 0) > .76f) continue;
            float px = tf->originX + (x + .12f + .76f * roll(rng, 8)) * state->spacing;
            float pz = tf->originZ + (z + .12f + .76f * roll(rng, 16)) * state->spacing;
            unsigned ix = (unsigned)roundf((px - tf->originX) / tf->spacing);
            unsigned iz = (unsigned)roundf((pz - tf->originZ) / tf->spacing);
            size_t i = (size_t)iz * tf->resolution + ix;
            float patch = TerrainNoise_Fbm2D(px * .11f, pz * .11f, world->masterSeed, 2, .5f, 2.17f);
            float moisture = biome->moisture[i], shore = biome->waterDistance[i];
            const uint8_t *weights = biome->materialRGBA + i * 4;
            if (weights[2] > 155 || tf->slopeTan[i] > .56f) continue;
            int type;
            if (shore < 1.75f && moisture > .55f && patch > .04f && roll(rng, 24) < .12f)
                type = SHORE_REED;
            else if (weights[0] > 61 && patch + .16f * moisture > -.022f &&
                     roll(rng, 24) < .38f + patch * .94f)
                type = MEADOW_CLUMP;
            else if (roll(rng, 24) < .042f && weights[0] < 168)
                type = SMALL_STONE;
            else continue;
            SurfaceHit hit;
            if (!Terrain_SampleGround(tf, px, pz, &hit) ||
                WorldGeology_CollidesDisc(&world->geology, hit.position, .20f)) continue;
            if (shore < tf->spacing * 2.2f) {
                WaterSample water;
                if (WorldHydrology_SampleWater(&world->hydrology, tf, px, pz, &water) &&
                    water.wet && water.depth > (type == SHORE_REED ? .085f : .007f)) continue;
            }
            DecorationCell *cell = state->cells + (size_t)z * columns + x;
            float hue = .5f * roll(rng, 16) + .5f * moisture;
            Color tint = type == SMALL_STONE ? Color_FromRGB(128+18*hue, 121+15*hue, 111+10*hue) :
                Color_FromRGB(126-39*hue, 113+22*hue, 64-7*hue);
            tint.a = 176;
            cell->instance = (OpenGLMeshInstance){
                .position = hit.position, .yaw = 6.2831853f * roll(rng, 0),
                .scale = .70f + .75f * roll(rng, 8), .scaleY = .68f + .70f * roll(rng, 16),
                .tint = tint
            };
            cell->type = (uint8_t)(type + 1);
        }
    }
    return true;
}

static float cameraViewHeight(const ICamera *camera) {
    return camera->fov <= 0 ? -camera->fov :
        2 * Vec3_Distance(camera->position, camera->target) * tanf(camera->fov * .00872664626f);
}

void EnvironmentDetailRenderer_QueueDecoration(EnvironmentDetailRenderer *details,
    const WorldStaticData *world, Renderer3D *renderer, const ICamera *camera, int tier) {
    DetailState *state = details->impl;
    float viewHeight = cameraViewHeight(camera);
    if (!state || tier <= 0 || viewHeight > 74.0f) return;
    if (!state->cells && !buildDecoration(state, world)) return;
    const TerrainHeightfield *tf = &world->terrain;
    float range = tier > 1 ? 38.0f : 29.0f;
    Vector3 center = camera->fov <= 0 ? camera->target : Vec3_Lerp(camera->position, camera->target, .52f);
    Vector3 ahead = Vec3_Normalize(Vec3_Sub(camera->target, camera->position));
    Vector3 right = Vec3_Normalize(Vec3_Cross(ahead, camera->up));
    Vector3 up = Vec3_Cross(right, ahead);
    int x0 = (int)fmaxf(0, floorf((center.x - range - tf->originX) / state->spacing));
    int z0 = (int)fmaxf(0, floorf((center.z - range - tf->originZ) / state->spacing));
    int x1 = (int)fminf(state->columns, ceilf((center.x + range - tf->originX) / state->spacing));
    int z1 = (int)fminf(state->columns, ceilf((center.z + range - tf->originZ) / state->spacing));
    for (int type = 0; type < DETAIL_TYPES; ++type) {
        size_t count = 0, limit = type == MEADOW_CLUMP ? 2100 : 250;
        for (int z = z0; z < z1 && count < limit; ++z) {
            for (int x = x0; x < x1 && count < limit; ++x) {
                const DecorationCell *cell = state->cells + (size_t)z * state->columns + x;
                if (cell->type != type + 1) continue;
                Vector3 p = Vec3_Sub(cell->instance.position, center);
                float fade = fmaxf(0, fminf(1, (1.0f - (p.x*p.x+p.z*p.z)/(range*range)) * 2.5f));
                fade *= fminf(1, (74 - viewHeight) / 24);
                if (fade < .025f) continue;
                Vector3 offset = Vec3_Sub(cell->instance.position, camera->position);
                float depth = Vec3_Dot(offset, ahead);
                if (depth < camera->nearPlane - 1 || depth > camera->farPlane + 1) continue;
                float half = camera->fov > 0 ? depth * tanf(camera->fov * .00872664626f) : viewHeight * .5f;
                if (fabsf(Vec3_Dot(offset, right)) > half * camera->aspectRatio + 1 ||
                    fabsf(Vec3_Dot(offset, up)) > half + 1) continue;
                OpenGLMeshInstance instance = cell->instance;
                instance.scale *= fade;
                instance.scaleY *= fade;
                state->batch[count++] = instance;
            }
        }
        if (count) {
            // Las hierbas subpíxel quedan representadas por el material del suelo.
            OpenGLRenderer_SceneCastShadow(renderer, type != SMALL_STONE && viewHeight < 30.0f);
            OpenGLRenderer_RenderMeshInstanced(renderer, &state->meshes[type], state->batch, count, true);
        }
    }
    OpenGLRenderer_SceneCastShadow(renderer, true);
}

static void writeContact(uint8_t *pixels, Vector3 mapping, Vector3 p, float rx, float rz,
                         float yaw, float strength) {
    float unitToPixel = mapping.z * CONTACT_SIZE;
    float x = (p.x - mapping.x) * unitToPixel - .5f;
    float z = (p.z - mapping.y) * unitToPixel - .5f;
    float radius = fmaxf(rx, rz) * unitToPixel;
    float c = cosf(yaw), s = sinf(yaw);
    float u = 1.0f / fmaxf(.78f, rx * unitToPixel);
    float v = 1.0f / fmaxf(.78f, rz * unitToPixel);
    int x0 = (int)fmaxf(0, x - radius - 1), x1 = (int)fminf(CONTACT_SIZE - 1, x + radius + 1);
    int z0 = (int)fmaxf(0, z - radius - 1), z1 = (int)fminf(CONTACT_SIZE - 1, z + radius + 1);
    for (int iz = z0; iz <= z1; ++iz) {
        for (int ix = x0; ix <= x1; ++ix) {
            float ax = ix - x, az = iz - z;
            float qx = (ax * c - az * s) * u, qz = (ax * s + az * c) * v;
            float falloff = fmaxf(0, 1 - qx*qx - qz*qz);
            uint8_t shade = (uint8_t)(255 * strength * falloff * falloff);
            uint8_t *pixel = &pixels[iz * CONTACT_SIZE + ix];
            if (shade > *pixel) *pixel = shade;
        }
    }
}

unsigned EnvironmentDetailRenderer_Contacts(EnvironmentDetailRenderer *details,
    const WorldStaticData *world, const ICamera *camera, const EcosystemVisualDrawPacket *packets,
    size_t count, bool shadowed, Vector3 *mapping) {
    DetailState *state = details->impl;
    if (!state) return 0;
    const TerrainHeightfield *tf = &world->terrain;
    float span = fmaxf(16, fminf(tf->span + 4, cameraViewHeight(camera) * fmaxf(2, camera->aspectRatio * 1.8f)));
    float centerX = span >= tf->span ? tf->originX + tf->span * .5f : camera->target.x;
    float centerZ = span >= tf->span ? tf->originZ + tf->span * .5f : camera->target.z;
    *mapping = Vec3_Create(centerX - span * .5f, centerZ - span * .5f, 1.0f / span);
    if (!state->pixels) state->pixels = malloc(CONTACT_SIZE * CONTACT_SIZE);
    if (!state->pixels) return 0;
    memset(state->pixels, 0, CONTACT_SIZE * CONTACT_SIZE);
    for (uint32_t i = 0; i < world->geology.rockCount; ++i) {
        const WorldRockInstance *rock = &world->geology.rocks[i];
        float radius = fmaxf(.12f, rock->boundRadiusXZ * .86f);
        writeContact(state->pixels, *mapping, rock->position, radius, radius, 0, .29f);
    }
    for (size_t i = 0; i < count; ++i) {
        const EcosystemVisualDrawPacket *p = &packets[i];
        if (!p->ready) continue;
        if (p->supportCount) {
            for (unsigned j = 0; j < p->supportCount; ++j) {
                if (!(p->stanceMask & (1u << j))) continue;
                Vector3 foot = p->supportPoints[j];
                SurfaceHit hit;
                if (!Terrain_SampleGround(tf, foot.x, foot.z, &hit)) continue;
                float distance = fabsf(foot.y - hit.position.y);
                if (distance > .13f) continue;
                float radius = fminf(.25f, fmaxf(.10f, p->scale * .49f));
                writeContact(state->pixels, *mapping, foot, radius, radius * 1.26f, p->yaw,
                             (shadowed ? .43f : .65f) * (1.0f - distance / .14f));
            }
        } else {
            float width = fmaxf(.16f, p->meshRadius * p->scale * 1.28f);
            float length = fmaxf(.39f, p->meshLength * p->scale * .37f);
            writeContact(state->pixels, *mapping, p->presentedCenter, width, length, p->yaw,
                         shadowed ? .24f : .48f);
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
    if (!state->contacts) {
        glGenTextures(1, &state->contacts);
        glBindTexture(GL_TEXTURE_2D, state->contacts);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, CONTACT_SIZE, CONTACT_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    }
    glBindTexture(GL_TEXTURE_2D, state->contacts);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CONTACT_SIZE, CONTACT_SIZE, GL_RED, GL_UNSIGNED_BYTE, state->pixels);
    glBindTexture(GL_TEXTURE_2D, (GLuint)binding);
    glPopClientAttrib();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack);
    return state->contacts;
}

size_t EnvironmentDetailRenderer_VramBytes(const EnvironmentDetailRenderer *details) {
    const DetailState *state = details->impl;
    return state && state->contacts ? CONTACT_SIZE * CONTACT_SIZE : 0;
}
