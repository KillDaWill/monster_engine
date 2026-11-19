#include "interface/UiTheme.h"

void UiTheme_TerminalIcon(UiRenderer *r, int x, int y, UiColor c)
{
    UiRenderer_Line(r, x, y, x+4, y+4, 1.5f, c);
    UiRenderer_Line(r, x+4, y+4, x, y+8, 1.5f, c);
    UiRenderer_Rect(r, (UiRect){x+7,y+7,6,1}, c);
}

static UiColor WithOpacity(UiColor color, float opacity)
{
    return (color & 0xffffff00u) | (unsigned)((color & 0xffu)*opacity+0.5f);
}

static void DrawHeader(const UiWindow *w, UiRenderer *r, UiRect b, bool active, float opacity)
{
    UiColor accent = WithOpacity(active ? UI_ACCENT : UI_MUTED,opacity);
    UiColor edge = WithOpacity(UI_EDGE,opacity);
    UiRenderer_Gradient(r, (UiRect){b.x+1,b.y+1,b.w-2,UI_TITLE_HEIGHT-1},
                        WithOpacity(active ? 0x204556ff : 0x1a303fff,opacity),
                        WithOpacity(0x0e2231ff,opacity));
    UiRenderer_Rect(r, (UiRect){b.x+1,b.y+1,60,2}, accent);
    UiRenderer_Rect(r, (UiRect){b.x+1,b.y+UI_TITLE_HEIGHT-1,b.w-2,1}, edge);
    UiRenderer_Outline(r, (UiRect){b.x+10,b.y+8,22,20}, edge);
    if (w->drawIcon) w->drawIcon(r, b.x+14, b.y+14, accent);
    else UiRenderer_Outline(r, (UiRect){b.x+17,b.y+14,8,8}, accent);

    UiRect minimize = UiWindow_MinimizeButton(b);
    UiRect close = UiWindow_CloseButton(b);
    UiRect title = {b.x+41,b.y+3,(w->onClose ? close.x : minimize.x)-b.x-48,UI_TITLE_HEIGHT-6};
    if (UiRenderer_PushClip(r, title)) {
        UiRenderer_Text(r, title.x, b.y+8, 16, WithOpacity(UI_PAPER,opacity), w->title ? w->title : "");
        UiRenderer_PopClip(r);
    }
    UiRenderer_Rect(r, minimize, WithOpacity(0x102330ff,opacity));
    UiRenderer_Outline(r, minimize, edge);
    UiRenderer_Rect(r, (UiRect){minimize.x+9,minimize.y+13,10,1}, accent);

    if (w->onClose) {
        UiColor cross = WithOpacity(active ? UI_ERROR : UI_MUTED,opacity);
        UiRenderer_Rect(r, close, WithOpacity(active ? 0x352831ff : 0x102330ff,opacity));
        UiRenderer_Outline(r, close, WithOpacity(active ? 0x784d55ff : UI_EDGE,opacity));
        UiRenderer_Line(r,close.x+10,close.y+8,close.x+18,close.y+16,1.25f,cross);
        UiRenderer_Line(r,close.x+18,close.y+8,close.x+10,close.y+16,1.25f,cross);
    }
}

void UiTheme_DrawWindows(const UiDesktop *d, UiRenderer *r, bool nativeFocus, bool caret)
{
    for (int i = 0; i < d->count; ++i) {
        const UiWindow *w = &d->windows[i];
        if (w->hidden) continue;
        UiWindowPresentation p = UiWindow_Presentation(w);
        UiRect b = p.frame;
        UiRect lower = {b.x,b.y+b.h-1,b.w,1};
        UiColor trace = 0x70ede3b0;
        if (p.linesOnly) {
            if (w->motion == UI_WINDOW_CLOSING && w->progress < 0.12)
                trace = WithOpacity(trace,(float)(w->progress/0.12));
            UiRenderer_Rect(r,(UiRect){b.x,b.y,b.w,1},trace);
            if (b.h > 1) UiRenderer_Rect(r,lower,trace);
            continue;
        }

        bool ready = UiWindow_IsInteractive(w);
        bool active = nativeFocus && (d->focus == w->id || d->pendingFocus == w->id);
        float header = p.unfold >= 0.2f ? 1 : p.unfold*5;
        for (int shadow = 10; p.unfold > 0 && shadow > 0; shadow -= 2)
            UiRenderer_Rect(r,(UiRect){b.x-shadow,b.y+4,b.w+2*shadow,b.h+shadow},
                            WithOpacity(0x02080c19,p.unfold));
        if (header > 0) {
            UiRenderer_Rect(r,b,WithOpacity(0x0a1926f8,header));
            bool clipHeader = b.w < 80 || b.h < UI_TITLE_HEIGHT;
            if (!clipHeader || UiRenderer_PushClip(r,b)) {
                DrawHeader(w,r,b,active,header);
                if (clipHeader) UiRenderer_PopClip(r);
            }
        }
        if (ready) {
            UiRenderer_Outline(r,b,active ? 0x477e8fff : UI_EDGE);
        } else {
            UiColor side = WithOpacity(active ? 0x477e8fff : UI_EDGE,header);
            UiRenderer_Rect(r,(UiRect){b.x,b.y+1,1,b.h-2},side);
            if (b.w > 1) UiRenderer_Rect(r,(UiRect){b.x+b.w-1,b.y+1,1,b.h-2},side);
            UiColor edge = active ? 0x477e8fff : UI_EDGE;
            UiRenderer_Rect(r,(UiRect){b.x,b.y,b.w,1},WithOpacity(edge,p.unfold));
            if (b.h > 1) UiRenderer_Rect(r,lower,WithOpacity(edge,p.unfold));
            if (p.unfold < 1) {
                UiColor highlight = WithOpacity(trace,1-p.unfold);
                UiRenderer_Rect(r,(UiRect){b.x,b.y,b.w,1},highlight);
                if (b.h > 1) UiRenderer_Rect(r,lower,highlight);
            }
        }

        if (w->draw && p.contentClip.w > 0 && p.contentClip.h > 0 &&
            UiRenderer_PushClip(r,p.contentClip)) {
            w->draw(w->state,r,UiWindow_Content(w->rect),active,ready && active && caret);
            UiRenderer_PopClip(r);
        }
        if (ready && b.w >= 16 && b.h >= 16) {
            UiRect grip = UiWindow_ResizeGrip(b);
            UiColor accent = active ? UI_ACCENT : UI_MUTED;
            for (int j = 3; j <= 7; j += 2)
                UiRenderer_Line(r,grip.x+13-j,grip.y+13,grip.x+13,grip.y+13-j,1,accent);
        }
    }
}
