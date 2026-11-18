#ifndef UI_DESKTOP_H
#define UI_DESKTOP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { int x, y, w, h; } UiRect;
typedef uint32_t UiWindowId;
typedef struct UiRenderer UiRenderer;

enum { UI_MAX_WINDOWS = 16, UI_TITLE_HEIGHT = 36, UI_FRAME = 7 };

typedef enum {
    UI_PRESS, UI_WHEEL, UI_KEY, UI_TEXT, UI_COMPOSE, UI_PASTE, UI_BLUR
} UiEventType;
typedef enum {
    UI_LEFT, UI_RIGHT, UI_UP, UI_DOWN, UI_HOME, UI_END, UI_DELETE,
    UI_BACKSPACE, UI_ENTER, UI_PAGE_UP, UI_PAGE_DOWN
} UiKey;
typedef struct {
    UiEventType type;
    int x, y, wheel, clicks;
    UiKey key;
    const char *text;
    int compositionCursor, compositionLength;
} UiEvent;

typedef void (*UiWindowDraw)(void *, UiRenderer *, UiRect, bool, bool);
typedef void (*UiWindowIcon)(UiRenderer *, int x, int y, uint32_t color);
typedef void (*UiWindowInput)(void *, UiRect, const UiEvent *);

typedef struct {
    UiWindowId id;
    UiRect rect;
    int minWidth, minHeight;
    bool hidden, acceptsText;
    const char *title;
    UiWindowDraw draw;
    UiWindowIcon drawIcon;
    UiWindowInput input;
    void *state;
} UiWindow;

typedef struct {
    /* Desde el fondo hasta el primer plano. Las referencias al arreglo son transitorias. */
    UiWindow windows[UI_MAX_WINDOWS];
    int count;
    UiWindowId nextId, focus, captured;
    UiRect area, origin;
    int startX, startY;
    bool resizing;
} UiDesktop;

bool UiRect_Contains(UiRect r, int x, int y);
UiRect UiWindow_Content(UiRect frame);
UiRect UiWindow_MinimizeButton(UiRect frame);
UiRect UiWindow_ResizeGrip(UiRect frame);
void UiDesktop_SetArea(UiDesktop *desktop, UiRect area);
/* Copia la descripción. La aplicación conserva la propiedad de title y state. */
UiWindowId UiDesktop_Add(UiDesktop *desktop, UiWindow window);
void UiDesktop_Remove(UiDesktop *desktop, UiWindowId id);
const UiWindow *UiDesktop_Find(const UiDesktop *desktop, UiWindowId id);
void UiDesktop_Activate(UiDesktop *desktop, UiWindowId id);
void UiDesktop_Hide(UiDesktop *desktop, UiWindowId id);
void UiDesktop_Cycle(UiDesktop *desktop, bool reverse);
void UiDesktop_Press(UiDesktop *desktop, int x, int y);
void UiDesktop_PressWithClicks(UiDesktop *desktop, int x, int y, int clicks);
void UiDesktop_Move(UiDesktop *desktop, int x, int y);
void UiDesktop_Release(UiDesktop *desktop);
void UiDesktop_Dispatch(UiDesktop *desktop, const UiEvent *event);

#endif
