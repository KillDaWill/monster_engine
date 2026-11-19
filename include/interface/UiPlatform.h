#ifndef UI_PLATFORM_H
#define UI_PLATFORM_H

#include "UiDesktop.h"
#include <stddef.h>

typedef struct UiPlatform UiPlatform;
typedef struct {
    void *state;
    UiDesktop *desktop;
    void (*resize)(void *, int width, int height);
    bool (*desktopPress)(void *, int x, int y);
    void (*draw)(void *, UiRenderer *, int width, int height);
    UiRect (*textRect)(void *, UiWindowId);
    bool (*needsFrame)(void *);
    void (*prepass)(void *, int width, int height, int drawableW, int drawableH, double dt);
    void (*cleanup)(void *); /* Con el contexto activo, después de cerrar todas las ventanas. */
} UiApp;

/* Ejecuta el ciclo SDL/GL. La aplicación conserva la propiedad de su estado. */
/* Directorio del ejecutable con separador final, independiente del cwd. */
bool UiPlatform_ExecutableDirectory(char *, size_t);
int UiPlatform_Run(const char *title, UiApp app);
#endif
