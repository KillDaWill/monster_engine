#ifndef UI_OFFSPRING_APP_H
#define UI_OFFSPRING_APP_H

#include "CreatureOffspring.h"
#include "UiCreatureVisualizer.h"
#include "UiTerminal.h"

typedef struct UiOffspringJob UiOffspringJob;
typedef struct {
    UiDesktop *desktop;
    UiVfs *fs;
    UiVfsId folder;
    UiTerminal *terminal;
    UiCreatureVisualizer *visualizer;
    UiWindowId window;
    CreatureOffspringBatch *batch;
    UiOffspringJob *job;
    UiVfsId saved[CREATURE_OFFSPRING_MAX];
    size_t selected, scroll;
    uint32_t seedNonce;
    bool error;
    char status[256];
} UiOffspringApp;

bool UiOffspringApp_Open(UiOffspringApp *);
bool UiOffspringApp_Register(UiOffspringApp *, UiCommands *);
bool UiOffspringApp_NeedsFrame(const UiOffspringApp *);
void UiOffspringApp_Update(UiOffspringApp *);
void UiOffspringApp_Icon(UiRenderer *, int, int, uint32_t);

#endif
