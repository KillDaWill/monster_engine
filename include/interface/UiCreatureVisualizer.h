#ifndef UI_CREATURE_VISUALIZER_H
#define UI_CREATURE_VISUALIZER_H

#include "UiShell.h"
#include "UiDesktop.h"

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
void UiCreatureVisualizer_Close(UiCreatureVisualizer *);
bool UiCreatureVisualizer_NeedsFrame(const UiCreatureVisualizer *);
const char *UiCreatureVisualizer_Prepass(UiCreatureVisualizer *, int width, int height,
                                         int drawableW, int drawableH, double dt);
bool UiCreatureVisualizer_Register(UiCreatureVisualizer *, UiCommands *);
void UiCreatureVisualizer_Icon(UiRenderer *, int, int, uint32_t);
UiCommandResult UiCreatureVisualizer_Command(void *, const char *, char *, size_t);
UiCommandResult UiCreatureVisualizer_CloseCommand(void *, const char *, char *, size_t);

#endif
