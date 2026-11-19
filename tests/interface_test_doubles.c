#include "interface/UiRenderer.h"
#include "../src/interface/UiCreatureEngine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct UiCreatureEngine {
    const UiVfsNode *first, *second;
};

void InterfaceTest_CheckRecipe(const UiCreatureEngine *engine, UiVfsId first, UiVfsId second)
{
    assert(engine && engine->first->id == first);
    assert((engine->second ? engine->second->id : 0) == second);
}

bool UiCreatureEngine_Request(UiCreatureEngine **owner, const char *directory,
                              const UiVfsNode *first, const UiVfsNode *second,
                              char *error, size_t capacity)
{
    (void)directory;
    if (first->recipeId == 9999) {
        snprintf(error,capacity,"ERROR: Test recipe failed to load");
        return false;
    }
    if (!*owner) *owner = malloc(sizeof(**owner));
    if (!*owner) abort();
    **owner = (UiCreatureEngine){first,second};
    return true;
}

void UiCreatureEngine_Destroy(UiCreatureEngine *engine) { free(engine); }
bool UiCreatureEngine_NeedsFrame(const UiCreatureEngine *engine) { return engine != NULL; }
unsigned UiCreatureEngine_Texture(const UiCreatureEngine *engine) { return engine != NULL; }
const char *UiCreatureEngine_Render(UiCreatureEngine *engine, int w, int h, double dt)
{
    (void)engine; (void)w; (void)h; (void)dt;
    return NULL;
}

/* Las pruebas del escritorio ejecutan los callbacks de entrada sin dibujar. */
void UiRenderer_Rect(UiRenderer *r, UiRect b, UiColor color)
{
    (void)r; (void)b; (void)color;
    abort();
}

void UiRenderer_Outline(UiRenderer *r, UiRect b, UiColor color) { UiRenderer_Rect(r,b,color); }
void UiRenderer_Gradient(UiRenderer *r, UiRect b, UiColor top, UiColor bottom)
{
    (void)bottom;
    UiRenderer_Rect(r,b,top);
}

void UiRenderer_Line(UiRenderer *r, float x, float y, float endX, float endY,
                     float thickness, UiColor color)
{
    (void)r; (void)x; (void)y; (void)endX; (void)endY; (void)thickness; (void)color;
    abort();
}

void UiRenderer_Quad(UiRenderer *r, unsigned texture, const UiPoint points[4], UiColor color)
{
    (void)r; (void)texture; (void)points; (void)color;
    abort();
}

bool UiRenderer_PushClip(UiRenderer *r, UiRect b)
{
    (void)r; (void)b;
    abort();
}

void UiRenderer_PopClip(UiRenderer *r)
{
    (void)r;
    abort();
}

void UiRenderer_Text(UiRenderer *r, int x, int y, int size, UiColor color, const char *text)
{
    (void)r; (void)x; (void)y; (void)size; (void)color; (void)text;
    abort();
}
