#ifndef UI_PLATFORM_H
#define UI_PLATFORM_H

#include "UiDesktop.h"

typedef struct UiPlatform UiPlatform;
typedef struct {
    void *state;
    UiDesktop *desktop;
    void (*resize)(void *, int width, int height);
    bool (*desktopPress)(void *, int x, int y);
    void (*draw)(void *, UiRenderer *, int width, int height);
    UiRect (*textRect)(void *, UiWindowId);
} UiApp;

/* Ejecuta el ciclo SDL/GL. La aplicación conserva la propiedad de su estado. */
int UiPlatform_Run(const char *title, UiApp app);
#endif
