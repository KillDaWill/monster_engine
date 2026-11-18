#include "interface/UiDesktop.h"
#include <string.h>

static int Max(int a, int b) { return a > b ? a : b; }
static int Clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }

bool UiRect_Contains(UiRect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

UiRect UiWindow_Content(UiRect r)
{
    return (UiRect){r.x + UI_FRAME, r.y + UI_TITLE_HEIGHT,
                   Max(0, r.w - 2 * UI_FRAME), Max(0, r.h - UI_TITLE_HEIGHT - UI_FRAME)};
}

UiRect UiWindow_MinimizeButton(UiRect r)
{
    return (UiRect){r.x + r.w - 36, r.y + 6, 28, 24};
}

UiRect UiWindow_ResizeGrip(UiRect r)
{
    return (UiRect){r.x + r.w - 16, r.y + r.h - 16, 16, 16};
}

static int Index(const UiDesktop *d, UiWindowId id)
{
    if (id)
        for (int i = 0; i < d->count; ++i)
            if (d->windows[i].id == id) return i;
    return -1;
}

const UiWindow *UiDesktop_Find(const UiDesktop *d, UiWindowId id)
{
    int i = Index(d, id);
    return i < 0 ? NULL : &d->windows[i];
}

static void ClampWindow(UiWindow *w, UiRect area)
{
    w->rect.w = Clamp(w->rect.w, Clamp(w->minWidth, 1, area.w), area.w);
    w->rect.h = Clamp(w->rect.h, Clamp(w->minHeight, 1, area.h), area.h);
    w->rect.x = Clamp(w->rect.x, area.x, area.x + area.w - w->rect.w);
    w->rect.y = Clamp(w->rect.y, area.y, area.y + area.h - w->rect.h);
}

void UiDesktop_SetArea(UiDesktop *d, UiRect area)
{
    area.w = Max(1, area.w);
    area.h = Max(1, area.h);
    if (d->captured) UiDesktop_Release(d);
    d->area = area;
    for (int i = 0; i < d->count; ++i) ClampWindow(&d->windows[i], area);
}

UiWindowId UiDesktop_Add(UiDesktop *d, UiWindow w)
{
    if (d->count == UI_MAX_WINDOWS) return 0;
    do { w.id = ++d->nextId; } while (!w.id || Index(d, w.id) >= 0);
    ClampWindow(&w, d->area);
    d->windows[d->count++] = w;
    if (!w.hidden) UiDesktop_Activate(d, w.id);
    return w.id;
}

void UiDesktop_Activate(UiDesktop *d, UiWindowId id)
{
    int i = Index(d, id);
    if (id && i < 0) return;
    if (d->focus != id) {
        const UiWindow *old = UiDesktop_Find(d, d->focus);
        if (old && old->input)
            old->input(old->state, UiWindow_Content(old->rect), &(UiEvent){.type = UI_BLUR});
    }
    d->focus = id;
    if (i < 0) return;
    UiWindow w = d->windows[i];
    w.hidden = false;
    memmove(&d->windows[i], &d->windows[i + 1], (d->count - i - 1) * sizeof(w));
    d->windows[d->count - 1] = w;
}

static void FocusTop(UiDesktop *d)
{
    for (int i = d->count - 1; i >= 0; --i) {
        if (!d->windows[i].hidden) {
            UiDesktop_Activate(d, d->windows[i].id);
            return;
        }
    }
}

void UiDesktop_Hide(UiDesktop *d, UiWindowId id)
{
    int i = Index(d, id);
    if (i < 0) return;
    d->windows[i].hidden = true;
    if (d->captured == id) UiDesktop_Release(d);
    if (d->focus == id) {
        UiDesktop_Activate(d, 0);
        FocusTop(d);
    }
}

void UiDesktop_Remove(UiDesktop *d, UiWindowId id)
{
    UiDesktop_Hide(d, id);
    int i = Index(d, id);
    if (i < 0) return;
    --d->count;
    memmove(&d->windows[i], &d->windows[i + 1], (d->count - i) * sizeof(UiWindow));
}

void UiDesktop_Cycle(UiDesktop *d, bool reverse)
{
    /* Los IDs conservan el orden de creación aunque cambie el orden visual. */
    UiWindowId next = 0, wrap = 0;
    for (int i = 0; i < d->count; ++i) {
        UiWindow w = d->windows[i];
        if (w.hidden) continue;
        if (!wrap || (reverse ? w.id > wrap : w.id < wrap)) wrap = w.id;
        bool candidate = reverse ? w.id < d->focus : w.id > d->focus;
        if (candidate && (!next || (reverse ? w.id > next : w.id < next))) next = w.id;
    }
    UiDesktop_Activate(d, next ? next : wrap);
}

static UiWindowId Hit(const UiDesktop *d, int x, int y)
{
    for (int i = d->count - 1; i >= 0; --i)
        if (!d->windows[i].hidden && UiRect_Contains(d->windows[i].rect, x, y))
            return d->windows[i].id;
    return 0;
}

void UiDesktop_Press(UiDesktop *d, int x, int y)
{
    UiDesktop_PressWithClicks(d,x,y,1);
}

void UiDesktop_PressWithClicks(UiDesktop *d, int x, int y, int clicks)
{
    if (d->captured) return;
    UiWindowId id = Hit(d, x, y);
    UiDesktop_Activate(d, id);
    const UiWindow *w = UiDesktop_Find(d, id);
    if (!w) return;
    if (UiRect_Contains(UiWindow_MinimizeButton(w->rect), x, y)) {
        UiDesktop_Hide(d, id);
        return;
    }
    bool resize = UiRect_Contains(UiWindow_ResizeGrip(w->rect), x, y);
    if (resize || y < w->rect.y + UI_TITLE_HEIGHT) {
        d->captured = id;
        d->resizing = resize;
        d->origin = w->rect;
        d->startX = x;
        d->startY = y;
        return;
    }
    UiDesktop_Dispatch(d, &(UiEvent){.type = UI_PRESS, .x = x, .y = y, .clicks = clicks});
}

void UiDesktop_Move(UiDesktop *d, int x, int y)
{
    int i = Index(d, d->captured);
    if (i < 0) return;
    UiWindow *w = &d->windows[i];
    UiRect r = d->origin;
    int dx = x - d->startX, dy = y - d->startY;
    if (d->resizing) {
        int maxW = d->area.x + d->area.w - r.x;
        int maxH = d->area.y + d->area.h - r.y;
        r.w = Clamp(r.w + dx, Clamp(w->minWidth, 1, maxW), maxW);
        r.h = Clamp(r.h + dy, Clamp(w->minHeight, 1, maxH), maxH);
    } else {
        r.x += dx;
        r.y += dy;
    }
    w->rect = r;
    ClampWindow(w, d->area);
}

void UiDesktop_Release(UiDesktop *d) { d->captured = 0; }

void UiDesktop_Dispatch(UiDesktop *d, const UiEvent *event)
{
    UiWindowId id = event->type == UI_WHEEL ? Hit(d, event->x, event->y) : d->focus;
    const UiWindow *w = UiDesktop_Find(d, id);
    if (!w || !w->input) return;
    UiRect content = UiWindow_Content(w->rect);
    if ((event->type == UI_PRESS || event->type == UI_WHEEL) &&
        !UiRect_Contains(content, event->x, event->y)) return;
    if ((event->type == UI_TEXT || event->type == UI_COMPOSE || event->type == UI_PASTE) &&
        !w->acceptsText) return;
    w->input(w->state, content, event);
}
