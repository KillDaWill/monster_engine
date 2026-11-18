#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include "UiDesktop.h"
#include <stddef.h>

typedef uint32_t UiColor; /* RRGGBBAA */
typedef struct { float x, y, u, v; } UiPoint;

/* Requiere contexto OpenGL 2.1 activo durante la creación, dibujo y liberación. */
UiRenderer *UiRenderer_Create(void);
void UiRenderer_Destroy(UiRenderer *);
void UiRenderer_Begin(UiRenderer *, int width, int height, int drawableW, int drawableH);
void UiRenderer_End(UiRenderer *);
void UiRenderer_Rect(UiRenderer *, UiRect, UiColor);
void UiRenderer_Gradient(UiRenderer *, UiRect, UiColor top, UiColor bottom);
void UiRenderer_Outline(UiRenderer *, UiRect, UiColor);
void UiRenderer_Line(UiRenderer *, float x, float y, float endX, float endY, float thickness, UiColor);
void UiRenderer_Quad(UiRenderer *, unsigned texture, const UiPoint points[4], UiColor);
/* Pop sólo después de un Push exitoso. Los clips anidados se intersectan. */
bool UiRenderer_PushClip(UiRenderer *, UiRect);
void UiRenderer_PopClip(UiRenderer *);
int UiRenderer_Cell(int fontSize);
int UiRenderer_Measure(const char *, size_t bytes, int fontSize);
void UiRenderer_TextN(UiRenderer *, int x, int y, int fontSize, UiColor, const char *, size_t);
void UiRenderer_Text(UiRenderer *, int x, int y, int fontSize, UiColor, const char *);
#endif
