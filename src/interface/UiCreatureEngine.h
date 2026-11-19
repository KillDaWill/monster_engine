#ifndef UI_CREATURE_ENGINE_H
#define UI_CREATURE_ENGINE_H

#include "interface/UiCreatureVisualizer.h"

bool UiCreatureEngine_Request(UiCreatureEngine **, const char *directory, const UiVfsNode *first,
                              const UiVfsNode *second, char *error, size_t);
void UiCreatureEngine_Destroy(UiCreatureEngine *);
bool UiCreatureEngine_NeedsFrame(const UiCreatureEngine *);
const char *UiCreatureEngine_Render(UiCreatureEngine *, int width, int height, double dt);
unsigned UiCreatureEngine_Texture(const UiCreatureEngine *);
#endif
