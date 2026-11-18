#include "interface/UiTheme.h"

void UiTheme_TerminalIcon(UiRenderer *r, int x, int y, UiColor c)
{
    UiRenderer_Line(r, x, y, x+4, y+4, 1.5f, c);
    UiRenderer_Line(r, x+4, y+4, x, y+8, 1.5f, c);
    UiRenderer_Rect(r, (UiRect){x+7,y+7,6,1}, c);
}

void UiTheme_DrawWindows(const UiDesktop *d, UiRenderer *r, bool nativeFocus, bool caret)
{
    for (int i = 0; i < d->count; ++i) {
        const UiWindow *w = &d->windows[i];
        if (w->hidden) continue;
        bool active = nativeFocus && d->focus == w->id;
        UiColor accent = active ? UI_ACCENT : UI_MUTED;
        UiRect b = w->rect;
        for (int shadow = 10; shadow > 0; shadow -= 2)
            UiRenderer_Rect(r, (UiRect){b.x-shadow,b.y+4,b.w+2*shadow,b.h+shadow}, 0x02080c19);
        UiRenderer_Rect(r, b, 0x0a1926f8);
        UiRenderer_Gradient(r, (UiRect){b.x+1,b.y+1,b.w-2,UI_TITLE_HEIGHT-1},
                            active ? 0x204556ff : 0x1a303fff, 0x0e2231ff);
        UiRenderer_Outline(r, b, active ? 0x477e8fff : UI_EDGE);
        UiRenderer_Rect(r, (UiRect){b.x+1,b.y+1,60,2}, accent);
        UiRenderer_Rect(r, (UiRect){b.x+1,b.y+UI_TITLE_HEIGHT-1,b.w-2,1}, UI_EDGE);
        UiRenderer_Outline(r, (UiRect){b.x+10,b.y+8,22,20}, UI_EDGE);
        if (w->drawIcon) w->drawIcon(r, b.x+14, b.y+14, accent);
        else UiRenderer_Outline(r, (UiRect){b.x+17,b.y+14,8,8}, accent);

        UiRect minimize = UiWindow_MinimizeButton(b);
        UiRect title = {b.x+41,b.y+3,minimize.x-b.x-48,UI_TITLE_HEIGHT-6};
        if (UiRenderer_PushClip(r, title)) {
            UiRenderer_Text(r, title.x, b.y+8, 16, UI_PAPER, w->title ? w->title : "");
            UiRenderer_PopClip(r);
        }
        UiRenderer_Rect(r, minimize, 0x102330ff);
        UiRenderer_Outline(r, minimize, UI_EDGE);
        UiRenderer_Rect(r, (UiRect){minimize.x+9,minimize.y+13,10,1}, accent);

        UiRect content = UiWindow_Content(b);
        if (w->draw && UiRenderer_PushClip(r, content)) {
            w->draw(w->state, r, content, active, active && caret);
            UiRenderer_PopClip(r);
        }
        UiRect grip = UiWindow_ResizeGrip(b);
        for (int j = 4; j <= 10; j += 3)
            UiRenderer_Line(r, grip.x+13-j, grip.y+13, grip.x+13, grip.y+13-j, 1, accent);
    }
}
