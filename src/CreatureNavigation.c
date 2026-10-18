#include "CreatureNavigation.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#define GRID 48
#define CELLS (GRID * GRID)
static Vector3 center(int i, float half) {
    float step = 2 * half / GRID;
    return Vec3_Create(-half + (i % GRID + .5f) * step, 0, -half + (i / GRID + .5f) * step);
}
static int cell(Vector3 p, float half) {
    int x = (int)((p.x + half) / (2 * half) * GRID), z = (int)((p.z + half) / (2 * half) * GRID);
    return x < 0 || z < 0 || x >= GRID || z >= GRID ? -1 : z * GRID + x;
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
    if (AIWorld_SegmentWalkable(w, from, goal, r, swim)) {
        n->points[n->count++] = goal;
        return true;
    }
    float g[CELLS];
    int parent[CELLS];
    unsigned char state[CELLS] = {0}, valid[CELLS];
    int start = cell(from, w->halfSize), end = cell(goal, w->halfSize);
    if (start < 0 || end < 0 || !AIWorld_Walkable(w, goal, r, swim))
        goto failed;
    for (int i = 0; i < CELLS; ++i) {
        g[i] = FLT_MAX;
        parent[i] = -1;
        valid[i] = AIWorld_Walkable(w, center(i, w->halfSize), r, swim);
    }
    if (!valid[start] || !valid[end] ||
        !AIWorld_SegmentWalkable(w, from, center(start, w->halfSize), r, swim) ||
        !AIWorld_SegmentWalkable(w, center(end, w->halfSize), goal, r, swim))
        goto failed;
    g[start] = 0;
    state[start] = 1;
    for (int iter = 0; iter < CELLS; ++iter) {
        int best = -1;
        float score = FLT_MAX;
        for (int i = 0; i < CELLS; ++i)
            if (state[i] == 1) {
                float f = g[i] + abs(i % GRID - end % GRID) + abs(i / GRID - end / GRID);
                if (f < score) {
                    score = f;
                    best = i;
                }
            }
        if (best < 0)
            goto failed;
        if (best == end) {
            int reverse[CREATURE_PATH_CAPACITY], count = 0;
            for (int i = end; i != start; i = parent[i]) {
                if (i < 0 || count >= CREATURE_PATH_CAPACITY - 1)
                    goto failed;
                reverse[count++] = i;
            }
            Vector3 previous = from;
            for (int j = count - 1; j >= 0;) {
                int far = 0;
                for (int k = 0; k <= j; ++k)
                    if (AIWorld_SegmentWalkable(w, previous, center(reverse[k], w->halfSize), r,
                                                swim)) {
                        far = k;
                        break;
                    }
                previous = center(reverse[far], w->halfSize);
                n->points[n->count++] = previous;
                j = far - 1;
            }
            n->points[n->count++] = goal;
            return true;
        }
        state[best] = 2;
        int x = best % GRID, z = best / GRID;
        int neighbors[] = {x > 0 ? best - 1 : -1, x < GRID - 1 ? best + 1 : -1,
                           z > 0 ? best - GRID : -1, z < GRID - 1 ? best + GRID : -1};
        for (int j = 0; j < 4; ++j) {
            int k = neighbors[j];
            if (k < 0 || !valid[k] || state[k] == 2 ||
                !AIWorld_SegmentWalkable(w, center(best, w->halfSize), center(k, w->halfSize), r,
                                         swim))
                continue;
            if (g[best] + 1 < g[k]) {
                g[k] = g[best] + 1;
                parent[k] = best;
                state[k] = 1;
            }
        }
    }
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
