#include "CreatureNavigation.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NAV_MAX_DIM 200
#define NAV_MAX_CELLS (NAV_MAX_DIM * NAV_MAX_DIM)
#define NAV_MAX_EXPANSIONS 4096

typedef struct NavStaticCache {
    unsigned revision;
    float halfSize;
    float cellSize;
    int dim;
    unsigned cells;
    float landClearance[NAV_MAX_CELLS];
    float swimClearance[NAV_MAX_CELLS];
} NavStaticCache;

/* Buffers scratch reutilizables por hilo: cero malloc/memset por búsqueda A*. */
typedef struct NavThreadScratch {
    uint32_t gen;
    uint32_t visitGen[NAV_MAX_CELLS];
    uint8_t state[NAV_MAX_CELLS]; /* 1 = abierto, 2 = cerrado */
    float g[NAV_MAX_CELLS];
    int parent[NAV_MAX_CELLS];
    int heap[NAV_MAX_CELLS];
    float heapF[NAV_MAX_CELLS];
    int heapPos[NAV_MAX_CELLS];
} NavThreadScratch;

static _Thread_local NavThreadScratch g_navScratch;

static int computeGridDim(float halfSize) {
    /* Resolución ~1.0 u.m. en arenas medianas/grandes, acotada en [32, NAV_MAX_DIM]. */
    float span = 2.0f * halfSize;
    int dim = (int)ceilf(span / 1.0f);
    if (dim < 48) dim = 48;
    if (dim > NAV_MAX_DIM) dim = NAV_MAX_DIM;
    return dim;
}

static inline Vector3 cellCenter(int i, int dim, float half, float step) {
    return Vec3_Create(-half + ((float)(i % dim) + 0.5f) * step, 0.0f,
                       -half + ((float)(i / dim) + 0.5f) * step);
}

static inline int pointToCell(Vector3 p, int dim, float half, float invStep) {
    int x = (int)floorf((p.x + half) * invStep);
    int z = (int)floorf((p.z + half) * invStep);
    return (x < 0 || z < 0 || x >= dim || z >= dim) ? -1 : (z * dim + x);
}

static inline void heapSwap(NavThreadScratch *sc, int a, int b) {
    int na = sc->heap[a], nb = sc->heap[b];
    float fa = sc->heapF[a], fb = sc->heapF[b];
    sc->heap[a] = nb; sc->heap[b] = na;
    sc->heapF[a] = fb; sc->heapF[b] = fa;
    sc->heapPos[nb] = a;
    sc->heapPos[na] = b;
}

static inline bool heapLess(const NavThreadScratch *sc, int a, int b) {
    if (fabsf(sc->heapF[a] - sc->heapF[b]) > 1e-5f) return sc->heapF[a] < sc->heapF[b];
    /* Desempate determinista por menor índice de celda. */
    return sc->heap[a] < sc->heap[b];
}

static void heapSiftUp(NavThreadScratch *sc, int idx) {
    while (idx > 0) {
        int p = (idx - 1) >> 1;
        if (!heapLess(sc, idx, p)) break;
        heapSwap(sc, idx, p);
        idx = p;
    }
}

static void heapSiftDown(NavThreadScratch *sc, int idx, int size) {
    for (;;) {
        int l = (idx << 1) + 1;
        int r = l + 1;
        int best = idx;
        if (l < size && heapLess(sc, l, best)) best = l;
        if (r < size && heapLess(sc, r, best)) best = r;
        if (best == idx) break;
        heapSwap(sc, idx, best);
        idx = best;
    }
}

static void heapPushOrDecrease(NavThreadScratch *sc, int *heapSize, int node, float f) {
    if (sc->visitGen[node] == sc->gen && sc->state[node] == 1) {
        int pos = sc->heapPos[node];
        sc->heapF[pos] = f;
        heapSiftUp(sc, pos);
        return;
    }
    int pos = (*heapSize)++;
    sc->heap[pos] = node;
    sc->heapF[pos] = f;
    sc->heapPos[node] = pos;
    sc->state[node] = 1;
    heapSiftUp(sc, pos);
}

static int heapPop(NavThreadScratch *sc, int *heapSize) {
    int top = sc->heap[0];
    int last = --(*heapSize);
    if (last > 0) {
        sc->heap[0] = sc->heap[last];
        sc->heapF[0] = sc->heapF[last];
        sc->heapPos[sc->heap[0]] = 0;
        heapSiftDown(sc, 0, last);
    }
    sc->state[top] = 2;
    return top;
}

bool CreatureNavigation_EnsureCache(AIWorld *w) {
    if (!w || !isfinite(w->halfSize) || w->halfSize <= 0.0f) return false;
    NavStaticCache *cache = (NavStaticCache *)w->navCache;
    if (cache && cache->revision == w->revision && fabsf(cache->halfSize - w->halfSize) < 1e-4f) {
        return true;
    }
    if (!cache) {
        cache = malloc(sizeof(*cache));
        if (!cache) return false;
        w->navCache = cache;
    }
    int dim = computeGridDim(w->halfSize);
    float step = (2.0f * w->halfSize) / (float)dim;
    unsigned cells = (unsigned)(dim * dim);
    cache->revision = w->revision;
    cache->halfSize = w->halfSize;
    cache->cellSize = step;
    cache->dim = dim;
    cache->cells = cells;

    /* Recolectar únicamente bloqueadores estáticos (obstáculos + agua). */
    typedef struct StaticDisc { float x, z, r; bool isWater; } StaticDisc;
    StaticDisc stackDiscs[256];
    StaticDisc *discs = stackDiscs;
    unsigned discCap = 256u, discCount = 0u;
    for (unsigned i = 0; i < w->capacity; ++i) {
        const AIWorldObject *o = AIWorld_At(w, i);
        if (!o || !o->active || o->walkable || o->type == AI_CREATURE) continue;
        if (discCount == discCap) {
            unsigned nextCap = discCap * 2u;
            StaticDisc *grown = malloc(nextCap * sizeof(*grown));
            if (!grown) break;
            memcpy(grown, discs, discCount * sizeof(*discs));
            if (discs != stackDiscs) free(discs);
            discs = grown;
            discCap = nextCap;
        }
        discs[discCount++] = (StaticDisc){o->position.x, o->position.z, o->radius, o->type == AI_WATER};
    }

    for (unsigned idx = 0; idx < cells; ++idx) {
        Vector3 c = cellCenter((int)idx, dim, w->halfSize, step);
        float wallClear = fminf(w->halfSize - fabsf(c.x), w->halfSize - fabsf(c.z));
        float landMin = wallClear;
        float swimMin = wallClear;
        for (unsigned d = 0; d < discCount; ++d) {
            float dx = c.x - discs[d].x;
            float dz = c.z - discs[d].z;
            float dist = sqrtf(dx * dx + dz * dz) - discs[d].r;
            if (dist < landMin) landMin = dist;
            if (!discs[d].isWater && dist < swimMin) swimMin = dist;
        }
        cache->landClearance[idx] = landMin;
        cache->swimClearance[idx] = swimMin;
    }
    if (discs != stackDiscs) free(discs);
    return true;
}

static inline bool cellIsWalkable(const AIWorld *w, const NavStaticCache *cache,
                                  int idx, int dim, float half, float step,
                                  float r, bool swim) {
    if (cache) {
        float clr = swim ? cache->swimClearance[idx] : cache->landClearance[idx];
        return clr >= r + 1e-4f;
    }
    return AIWorld_Walkable(w, cellCenter(idx, dim, half, step), r, swim);
}

bool CreatureNavigation_Plan(CreatureNavigation *n, const AIWorld *w, Vector3 from, Vector3 goal,
                             float r, bool swim) {
    n->count = n->index = 0;
    n->progress = (NavigationProgress){0};
    n->goal = goal;
    n->clearance = r;
    n->swimming = swim;
    n->revision = w->revision;
    n->failed = false;
    n->lastExpandedNodes = 0u;

    /* Camino rápido en línea recta respaldado por la capa espacial estática. */
    if (AIWorld_SegmentWalkable(w, from, goal, r, swim)) {
        n->points[n->count++] = goal;
        return true;
    }

    const NavStaticCache *cache = (const NavStaticCache *)w->navCache;
    if (cache && (cache->revision != w->revision || fabsf(cache->halfSize - w->halfSize) > 1e-4f)) {
        cache = NULL;
    }
    int dim = cache ? cache->dim : computeGridDim(w->halfSize);
    float step = cache ? cache->cellSize : (2.0f * w->halfSize) / (float)dim;
    float invStep = 1.0f / step;

    int start = pointToCell(from, dim, w->halfSize, invStep);
    int end = pointToCell(goal, dim, w->halfSize, invStep);
    if (start < 0 || end < 0 || !AIWorld_Walkable(w, goal, r, swim)) goto failed;

    /* Si from o goal está pegado a una orilla walkable, el centro de su celda puede caer dentro del margen:
     * buscar la celda adyacente walkable más cercana con segmento libre hasta from/goal. */
    if (!cellIsWalkable(w, cache, start, dim, w->halfSize, step, r, swim) ||
        !AIWorld_SegmentWalkable(w, from, cellCenter(start, dim, w->halfSize, step), r, swim)) {
        int sx0 = start % dim, sz0 = start / dim, bestS = -1;
        float bestD2 = FLT_MAX;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = sx0 + dx, nz = sz0 + dz;
                if (nx < 0 || nz < 0 || nx >= dim || nz >= dim) continue;
                int ni = nz * dim + nx;
                if (!cellIsWalkable(w, cache, ni, dim, w->halfSize, step, r, swim)) continue;
                Vector3 c = cellCenter(ni, dim, w->halfSize, step);
                if (!AIWorld_SegmentWalkable(w, from, c, r, swim)) continue;
                float d2 = Vec3_LengthSq(Vec3_Sub(c, from));
                if (d2 < bestD2) { bestD2 = d2; bestS = ni; }
            }
        }
        if (bestS < 0) goto failed;
        start = bestS;
    }
    if (!cellIsWalkable(w, cache, end, dim, w->halfSize, step, r, swim) ||
        !AIWorld_SegmentWalkable(w, cellCenter(end, dim, w->halfSize, step), goal, r, swim)) {
        int ex0 = end % dim, ez0 = end / dim, bestE = -1;
        float bestD2 = FLT_MAX;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = ex0 + dx, nz = ez0 + dz;
                if (nx < 0 || nz < 0 || nx >= dim || nz >= dim) continue;
                int ni = nz * dim + nx;
                if (!cellIsWalkable(w, cache, ni, dim, w->halfSize, step, r, swim)) continue;
                Vector3 c = cellCenter(ni, dim, w->halfSize, step);
                if (!AIWorld_SegmentWalkable(w, c, goal, r, swim)) continue;
                float d2 = Vec3_LengthSq(Vec3_Sub(c, goal));
                if (d2 < bestD2) { bestD2 = d2; bestE = ni; }
            }
        }
        if (bestE < 0) goto failed;
        end = bestE;
    }

    NavThreadScratch *sc = &g_navScratch;
    if (++sc->gen == 0u) {
        memset(sc->visitGen, 0, sizeof(sc->visitGen));
        sc->gen = 1u;
    }

    /* Acotar ventana de búsqueda local para evitar explorar toda la arena de 200x200 en fallos. */
    int sx = start % dim, sz = start / dim;
    int ex = end % dim, ez = end / dim;
    int pad = 28;
    int minX = (sx < ex ? sx : ex) - pad; if (minX < 0) minX = 0;
    int maxX = (sx > ex ? sx : ex) + pad; if (maxX >= dim) maxX = dim - 1;
    int minZ = (sz < ez ? sz : ez) - pad; if (minZ < 0) minZ = 0;
    int maxZ = (sz > ez ? sz : ez) + pad; if (maxZ >= dim) maxZ = dim - 1;

    int heapSize = 0;
    sc->visitGen[start] = sc->gen;
    sc->g[start] = 0.0f;
    sc->parent[start] = -1;
    sc->state[start] = 0;
    float h0 = (float)(abs(sx - ex) + abs(sz - ez));
    heapPushOrDecrease(sc, &heapSize, start, h0);

    int expanded = 0;
    while (heapSize > 0 && expanded < NAV_MAX_EXPANSIONS) {
        int best = heapPop(sc, &heapSize);
        ++expanded;
        if (best == end) {
            n->lastExpandedNodes = (uint64_t)expanded;
            int reverse[CREATURE_PATH_CAPACITY], count = 0;
            for (int i = end; i != start; i = sc->parent[i]) {
                if (i < 0 || count >= CREATURE_PATH_CAPACITY - 1) goto failed;
                reverse[count++] = i;
            }
            Vector3 previous = from;
            for (int j = count - 1; j >= 0;) {
                int far = 0;
                for (int k = 0; k <= j; ++k) {
                    Vector3 ck = cellCenter(reverse[k], dim, w->halfSize, step);
                    if (AIWorld_SegmentWalkable(w, previous, ck, r, swim)) {
                        far = k;
                        break;
                    }
                }
                previous = cellCenter(reverse[far], dim, w->halfSize, step);
                if (n->count < CREATURE_PATH_CAPACITY - 1u) {
                    n->points[n->count++] = previous;
                }
                j = far - 1;
            }
            n->points[n->count++] = goal;
            return true;
        }
        int x = best % dim, z = best / dim;
        int neighbors[4] = {
            x > minX ? best - 1 : -1,
            x < maxX ? best + 1 : -1,
            z > minZ ? best - dim : -1,
            z < maxZ ? best + dim : -1
        };
        Vector3 cBest = cellCenter(best, dim, w->halfSize, step);
        float nextG = sc->g[best] + 1.0f;
        for (int j = 0; j < 4; ++j) {
            int k = neighbors[j];
            if (k < 0) continue;
            bool visited = (sc->visitGen[k] == sc->gen);
            if (visited && sc->state[k] == 2) continue;
            if (!cellIsWalkable(w, cache, k, dim, w->halfSize, step, r, swim)) continue;
            if (!visited || nextG < sc->g[k]) {
                Vector3 cK = cellCenter(k, dim, w->halfSize, step);
                if (!AIWorld_SegmentWalkable(w, cBest, cK, r, swim)) continue;
                if (!visited) {
                    sc->visitGen[k] = sc->gen;
                    sc->state[k] = 0;
                }
                sc->g[k] = nextG;
                sc->parent[k] = best;
                float h = (float)(abs(k % dim - ex) + abs(k / dim - ez));
                heapPushOrDecrease(sc, &heapSize, k, nextG + h);
            }
        }
    }
    n->lastExpandedNodes = (uint64_t)expanded;
failed:
    n->failed = true;
    ++n->failures;
    return false;
}

NavigationGoalStatus CreatureNavigation_Update(CreatureNavigation *n, Vector3 p, float yaw,
                                               float speed, float turn, float dt, Vector3 *out) {
    if (n->failed || !n->count)
        return NAVIGATION_STALLED;
    while (n->index + 1 < n->count && Vec3_Distance(p, n->points[n->index]) < .7f) {
        ++n->index;
        n->progress = (NavigationProgress){0};
    }
    *out = n->points[n->index];
    NavigationGoalStatus s =
        NavigationProgress_Update(&n->progress, p, *out, yaw, speed, turn, .12f, dt);
    if (s == NAVIGATION_ARRIVED && n->index + 1 < n->count) {
        ++n->index;
        n->progress = (NavigationProgress){0};
        *out = n->points[n->index];
        return NAVIGATION_ADVANCING;
    }
    return s;
}
