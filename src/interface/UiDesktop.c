#include "interface/UiDesktop.h"
#include <string.h>

static int Max(int a, int b) { return a > b ? a : b; }
static int Clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }

/* El recorrido común a ambos sentidos reserva [0, 1] para el ancho y [1, 2] para la altura. */
static const double openExpand = 0.140, openReveal = 0.220;
static const double closeContract = 0.120, closeCollapse = 0.180;

static double Advance(double progress, double dt, double horizontal, double vertical)
{
    double elapsed = progress <= 1 ? progress*horizontal : horizontal+(progress-1)*vertical;
    elapsed += dt;
    if (elapsed <= 0) return 0;
    if (elapsed <= horizontal) return elapsed/horizontal;
    return elapsed >= horizontal+vertical ? 2 : 1+(elapsed-horizontal)/vertical;
}

bool UiRect_Contains(UiRect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int PixelEdge(int coordinate, int logical, int drawable)
{
    int64_t scaled = (int64_t)coordinate*drawable;
    return (int)((scaled+(scaled < 0 ? -logical/2 : logical/2))/logical);
}

UiRect UiRect_Drawable(UiRect r, int w, int h, int dw, int dh)
{
    if (w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return (UiRect){0};
    int x = PixelEdge(r.x,w,dw), y = PixelEdge(r.y,h,dh);
    return (UiRect){x,y,PixelEdge(r.x+r.w,w,dw)-x,PixelEdge(r.y+r.h,h,dh)-y};
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

UiRect UiWindow_CloseButton(UiRect r)
{
    return (UiRect){r.x + r.w - 70, r.y + 6, 28, 24};
}

UiRect UiWindow_ResizeGrip(UiRect r)
{
    return (UiRect){r.x + r.w - 16, r.y + r.h - 16, 16, 16};
}

UiWindowPresentation UiWindow_Presentation(const UiWindow *w)
{
    UiRect frame = w->rect;
    UiRect content = UiWindow_Content(frame);
    if (w->motion == UI_WINDOW_READY) return (UiWindowPresentation){frame,content,false,1};

    int collapsed = Clamp(UI_TITLE_HEIGHT,1,frame.h);
    if (w->progress < 1) {
        double remaining = 1-w->progress;
        int seed = Clamp(8,1,frame.w);
        int width = seed+(int)((frame.w-seed)*(1-remaining*remaining)+0.5);
        frame.x += (frame.w-width)/2;
        frame.w = width;
        frame.h = collapsed;
        return (UiWindowPresentation){frame,{0},true,0};
    }
    double t = w->progress-1;
    double reveal = t*t*(3-2*t);
    frame.h = collapsed+(int)((frame.h-collapsed)*reveal+0.5);
    content.h = Clamp(frame.y+frame.h-1-content.y,0,content.h);
    return (UiWindowPresentation){frame,content,false,(float)reveal};
}

bool UiWindow_IsInteractive(const UiWindow *w)
{
    return w && !w->hidden && w->motion == UI_WINDOW_READY;
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
    w.motion = w.hidden ? UI_WINDOW_READY : UI_WINDOW_OPENING;
    w.progress = w.hidden ? 2 : 0;
    d->windows[d->count++] = w;
    if (!w.hidden) UiDesktop_Activate(d, w.id);
    return w.id;
}

static void Focus(UiDesktop *d, UiWindowId id)
{
    if (d->focus != id) {
        if (d->contentDrag) UiDesktop_Release(d);
        const UiWindow *old = UiDesktop_Find(d, d->focus);
        if (old && old->input)
            old->input(old->state, UiWindow_Content(old->rect), &(UiEvent){.type = UI_BLUR});
    }
    d->focus = id;
}

void UiDesktop_Activate(UiDesktop *d, UiWindowId id)
{
    int i = Index(d, id);
    if (id && i < 0) return;
    if (!id) {
        d->pendingFocus = 0;
        Focus(d,0);
        return;
    }
    UiWindow w = d->windows[i];
    w.hidden = false;
    if (w.motion == UI_WINDOW_CLOSING)
        w.motion = w.progress >= 2 ? UI_WINDOW_READY : UI_WINDOW_OPENING;
    bool ready = w.motion == UI_WINDOW_READY;
    d->pendingFocus = ready ? 0 : id;
    memmove(&d->windows[i], &d->windows[i + 1], (d->count - i - 1) * sizeof(w));
    d->windows[d->count - 1] = w;
    if (ready) Focus(d,id);
}

static void Deactivate(UiDesktop *d, UiWindowId id)
{
    if (d->captured == id) UiDesktop_Release(d);
    bool pending = d->pendingFocus == id;
    if (pending) d->pendingFocus = 0;
    if (d->focus != id && (!pending || d->focus)) return;
    UiWindowId opening = d->pendingFocus;
    for (int i = d->count - 1; i >= 0; --i) {
        const UiWindow *w = &d->windows[i];
        if (UiWindow_IsInteractive(w)) {
            Focus(d,w->id);
            return;
        }
        if (!opening && !w->hidden && w->motion == UI_WINDOW_OPENING) opening = w->id;
    }
    d->pendingFocus = opening;
    Focus(d,0);
}

void UiDesktop_Hide(UiDesktop *d, UiWindowId id)
{
    int i = Index(d, id);
    if (i < 0 || d->windows[i].motion == UI_WINDOW_CLOSING) return;
    d->windows[i].hidden = true;
    d->windows[i].motion = UI_WINDOW_READY;
    d->windows[i].progress = 2;
    Deactivate(d,id);
}

void UiDesktop_Remove(UiDesktop *d, UiWindowId id)
{
    int i = Index(d, id);
    if (i < 0) return;
    d->windows[i].hidden = true;
    Deactivate(d,id);
    --d->count;
    memmove(&d->windows[i], &d->windows[i + 1], (d->count - i) * sizeof(UiWindow));
}

bool UiDesktop_Close(UiDesktop *d, UiWindowId id)
{
    int i = Index(d,id);
    if (i < 0 || !d->windows[i].onClose) return false;
    if (d->windows[i].hidden) return UiDesktop_CloseImmediate(d,id);
    d->windows[i].motion = UI_WINDOW_CLOSING;
    Deactivate(d,id);
    return true;
}

bool UiDesktop_CloseImmediate(UiDesktop *d, UiWindowId id)
{
    const UiWindow *w = UiDesktop_Find(d,id);
    if (!w || !w->onClose) return false;
    UiWindowClose cleanup = w->onClose;
    void *state = w->state;
    UiDesktop_Remove(d,id);
    cleanup(state);
    return true;
}

void UiDesktop_CloseAll(UiDesktop *d)
{
    Focus(d,0);
    UiDesktop_Release(d);
    d->pendingFocus = 0;
    while (d->count) {
        UiWindowId id = d->windows[d->count-1].id;
        if (!UiDesktop_CloseImmediate(d,id)) UiDesktop_Remove(d,id);
    }
}

bool UiDesktop_HasActiveAnimations(const UiDesktop *d)
{
    for (int i = 0; i < d->count; ++i)
        if (d->windows[i].motion != UI_WINDOW_READY) return true;
    return false;
}

void UiDesktop_Update(UiDesktop *d, double dt)
{
    if (!(dt > 0)) return;
    UiWindowId finished[UI_MAX_WINDOWS];
    int count = 0;
    for (int i = 0; i < d->count; ++i) {
        UiWindow *w = &d->windows[i];
        if (w->motion == UI_WINDOW_OPENING) {
            w->progress = Advance(w->progress,dt,openExpand,openReveal);
            if (w->progress >= 2) w->motion = UI_WINDOW_READY;
        } else if (w->motion == UI_WINDOW_CLOSING) {
            w->progress = Advance(w->progress,-dt,closeContract,closeCollapse);
            if (w->progress <= 0) finished[count++] = w->id;
        }
    }
    if (UiWindow_IsInteractive(UiDesktop_Find(d,d->pendingFocus))) {
        Focus(d,d->pendingFocus);
        d->pendingFocus = 0;
    }
    for (int i = 0; i < count; ++i) {
        const UiWindow *w = UiDesktop_Find(d,finished[i]);
        if (w && w->motion == UI_WINDOW_CLOSING) UiDesktop_CloseImmediate(d,finished[i]);
    }
}

void UiDesktop_Cycle(UiDesktop *d, bool reverse)
{
    /* Los IDs conservan el orden de creación aunque cambie el orden visual. */
    UiWindowId next = 0, wrap = 0;
    for (int i = 0; i < d->count; ++i) {
        UiWindow w = d->windows[i];
        if (!UiWindow_IsInteractive(&w)) continue;
        if (!wrap || (reverse ? w.id > wrap : w.id < wrap)) wrap = w.id;
        bool candidate = reverse ? w.id < d->focus : w.id > d->focus;
        if (candidate && (!next || (reverse ? w.id > next : w.id < next))) next = w.id;
    }
    UiWindowId selected = next ? next : wrap;
    if (selected != d->focus) UiDesktop_Activate(d,selected);
    else if (selected) d->pendingFocus = 0;
}

static UiWindowId Hit(const UiDesktop *d, int x, int y)
{
    for (int i = d->count - 1; i >= 0; --i) {
        const UiWindow *w = &d->windows[i];
        if (w->hidden || !UiRect_Contains(w->rect,x,y)) continue;
        UiWindowPresentation p = UiWindow_Presentation(w);
        if (UiRect_Contains(p.frame,x,y) && (!p.linesOnly || y == p.frame.y ||
            y == p.frame.y+p.frame.h-1)) return w->id;
    }
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
    if (id && !UiWindow_IsInteractive(UiDesktop_Find(d,id))) return;
    UiDesktop_Activate(d, id);
    const UiWindow *w = UiDesktop_Find(d, id);
    if (!w) return;
    if (UiRect_Contains(UiWindow_MinimizeButton(w->rect), x, y)) {
        UiDesktop_Hide(d, id);
        return;
    }
    if (w->onClose && UiRect_Contains(UiWindow_CloseButton(w->rect), x, y)) {
        UiDesktop_Close(d,id);
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
    if (w->capturesContentPointer && w->input && UiRect_Contains(UiWindow_Content(w->rect),x,y)) {
        d->captured = id;
        d->contentDrag = true;
    }
    UiDesktop_Dispatch(d, &(UiEvent){.type = UI_PRESS, .x = x, .y = y, .clicks = clicks});
}

void UiDesktop_Move(UiDesktop *d, int x, int y)
{
    int i = Index(d, d->captured);
    if (i < 0) return;
    UiWindow *w = &d->windows[i];
    if (d->contentDrag) {
        w->input(w->state, UiWindow_Content(w->rect),
                 &(UiEvent){.type=UI_POINTER_MOVE,.x=x,.y=y});
        return;
    }
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

void UiDesktop_Release(UiDesktop *d)
{
    const UiWindow *w = d->contentDrag ? UiDesktop_Find(d,d->captured) : NULL;
    d->captured = 0;
    d->contentDrag = false;
    if (w && w->input)
        w->input(w->state, UiWindow_Content(w->rect), &(UiEvent){.type=UI_POINTER_RELEASE});
}

void UiDesktop_Dispatch(UiDesktop *d, const UiEvent *event)
{
    UiWindowId id = event->type == UI_WHEEL ? Hit(d, event->x, event->y) : d->focus;
    const UiWindow *w = UiDesktop_Find(d, id);
    if (!UiWindow_IsInteractive(w) || !w->input) return;
    UiRect content = UiWindow_Content(w->rect);
    if ((event->type == UI_PRESS || event->type == UI_WHEEL) &&
        !UiRect_Contains(content, event->x, event->y)) return;
    if ((event->type == UI_TEXT || event->type == UI_COMPOSE || event->type == UI_PASTE) &&
        !w->acceptsText) return;
    w->input(w->state, content, event);
}
