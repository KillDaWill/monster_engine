#ifndef UI_CREATURE_VISUALIZER_H
#define UI_CREATURE_VISUALIZER_H

#include "UiShell.h"
#include "UiDesktop.h"

typedef struct CreatureRecipe CreatureRecipe;
typedef struct UiCreatureEngine UiCreatureEngine;
typedef struct {
    UiDesktop *desktop;
    const UiShell *shell;
    const char *executableDirectory;
    UiWindowId window;
    UiCreatureEngine *engine;
    bool visibleLastFrame;
} UiCreatureVisualizer;

bool UiCreatureVisualizer_Open(UiCreatureVisualizer *);
/* Solicita una escena por ID del VFS y activa la ventana si se acepta. */
bool UiCreatureVisualizer_ViewRecipe(UiCreatureVisualizer *, UiVfsId, char *error, size_t capacity);
bool UiCreatureVisualizer_ViewTemporary(UiCreatureVisualizer *, const CreatureRecipe *,
    unsigned candidate, char *error, size_t capacity);
/* candidate=0 descarta todas las vistas temporales, incluida una solicitud pendiente. */
void UiCreatureVisualizer_InvalidateTemporary(UiCreatureVisualizer *, unsigned candidate);
void UiCreatureVisualizer_Close(UiCreatureVisualizer *);
bool UiCreatureVisualizer_NeedsFrame(const UiCreatureVisualizer *);
const char *UiCreatureVisualizer_Prepass(UiCreatureVisualizer *, int width, int height,
                                         int drawableW, int drawableH, double dt);
bool UiCreatureVisualizer_Register(UiCreatureVisualizer *, UiCommands *);
void UiCreatureVisualizer_Icon(UiRenderer *, int, int, uint32_t);
UiCommandResult UiCreatureVisualizer_Command(void *, const char *, char *, size_t);
UiCommandResult UiCreatureVisualizer_CloseCommand(void *, const char *, char *, size_t);

#endif
