#ifndef UI_CREATURE_ENGINE_H
#define UI_CREATURE_ENGINE_H

#include "interface/UiCreatureVisualizer.h"

bool UiCreatureEngine_Request(UiCreatureEngine **, const char *directory, const UiVfsNode *first,
                              const UiVfsNode *second, char *error, size_t);
void UiCreatureEngine_Destroy(UiCreatureEngine *);
void UiCreatureEngine_OrbitInput(UiCreatureEngine *, const UiEvent *);
bool UiCreatureEngine_NeedsFrame(const UiCreatureEngine *);
const char *UiCreatureEngine_Render(UiCreatureEngine *, int width, int height, double dt);
typedef struct { unsigned texture; int width, height; } UiCreatureFrame;
UiCreatureFrame UiCreatureEngine_Front(const UiCreatureEngine *);
#endif
