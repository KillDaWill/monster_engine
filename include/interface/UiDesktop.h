#ifndef UI_DESKTOP_H
#define UI_DESKTOP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { int x, y, w, h; } UiRect;
typedef uint32_t UiWindowId;
typedef struct UiRenderer UiRenderer;

enum { UI_MAX_WINDOWS = 16, UI_TITLE_HEIGHT = 36, UI_FRAME = 7 };

typedef enum {
    UI_PRESS, UI_WHEEL, UI_KEY, UI_TEXT, UI_COMPOSE, UI_PASTE, UI_BLUR, UI_POINTER_MOVE, UI_POINTER_RELEASE
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
    float preciseWheel;
} UiEvent;

typedef void (*UiWindowClose)(void *);
typedef void (*UiWindowDraw)(void *, UiRenderer *, UiRect, bool, bool);
typedef void (*UiWindowIcon)(UiRenderer *, int x, int y, uint32_t color);
typedef void (*UiWindowInput)(void *, UiRect, const UiEvent *);

typedef enum { UI_WINDOW_READY, UI_WINDOW_OPENING, UI_WINDOW_CLOSING } UiWindowMotion;

typedef struct {
    UiWindowId id;
    UiRect rect;
    int minWidth, minHeight;
    bool hidden, acceptsText;
    const char *title;
    UiWindowDraw draw;
    UiWindowIcon drawIcon;
    UiWindowInput input;
    UiWindowClose onClose;
    void *state;
    /* Propiedad del escritorio. rect conserva las dimensiones de destino. */
    UiWindowMotion motion;
    double progress;
    bool capturesContentPointer; /**< Captura opcional tras UI_PRESS dentro del contenido. */
} UiWindow;

typedef struct {
    UiRect frame, contentClip;
    bool linesOnly;
    float unfold;
} UiWindowPresentation;

typedef struct {
    /* Desde el fondo hasta el primer plano. Las referencias al arreglo son transitorias. */
    UiWindow windows[UI_MAX_WINDOWS];
    int count;
    UiWindowId nextId, focus, pendingFocus, captured;
    UiRect area, origin;
    int startX, startY;
    bool resizing, contentDrag;
} UiDesktop;

bool UiRect_Contains(UiRect r, int x, int y);
/* Convierte ambos bordes al framebuffer para mantener las uniones fraccionarias. */
UiRect UiRect_Drawable(UiRect, int width, int height, int drawableW, int drawableH);
UiRect UiWindow_Content(UiRect frame);
UiRect UiWindow_MinimizeButton(UiRect frame);
UiRect UiWindow_CloseButton(UiRect frame);
UiRect UiWindow_ResizeGrip(UiRect frame);
/* Geometría lógica de dibujo. El layout del contenido usa UiWindow_Content(rect). */
UiWindowPresentation UiWindow_Presentation(const UiWindow *);
bool UiWindow_IsInteractive(const UiWindow *);
void UiDesktop_SetArea(UiDesktop *desktop, UiRect area);
/* Copia la descripción e inicia la apertura si es visible. title y state viven hasta retirar la ventana. */
UiWindowId UiDesktop_Add(UiDesktop *desktop, UiWindow window);
/* Solicita el cierre. Una ventana oculta se libera de inmediato. */
bool UiDesktop_Close(UiDesktop *desktop, UiWindowId id);
/* El cierre invoca onClose tras retirar el ID del escritorio. */
bool UiDesktop_CloseImmediate(UiDesktop *desktop, UiWindowId id);
/* Finaliza todas las ventanas. Los callbacks de cierre no deben crear ventanas durante la salida. */
void UiDesktop_CloseAll(UiDesktop *desktop);
/* Avanza las transiciones con segundos monotónicos. Puede ejecutar callbacks de cierre. */
void UiDesktop_Update(UiDesktop *desktop, double dt);
bool UiDesktop_HasActiveAnimations(const UiDesktop *desktop);
/* Retira un marco sin invocar onClose. */
void UiDesktop_Remove(UiDesktop *desktop, UiWindowId id);
const UiWindow *UiDesktop_Find(const UiDesktop *desktop, UiWindowId id);
/* Restaura la ventana y revierte un cierre en curso. El foco espera a que termine de abrir. */
void UiDesktop_Activate(UiDesktop *desktop, UiWindowId id);
void UiDesktop_Hide(UiDesktop *desktop, UiWindowId id);
void UiDesktop_Cycle(UiDesktop *desktop, bool reverse);
void UiDesktop_Press(UiDesktop *desktop, int x, int y);
void UiDesktop_PressWithClicks(UiDesktop *desktop, int x, int y, int clicks);
void UiDesktop_Move(UiDesktop *desktop, int x, int y);
void UiDesktop_Release(UiDesktop *desktop);
void UiDesktop_Dispatch(UiDesktop *desktop, const UiEvent *event);

#endif
