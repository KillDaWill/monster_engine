#include "interface/UiRenderer.h"
#include "interface/UiText.h"
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { ATLAS = 512, BATCH_VERTICES = 16380, CLIP_LIMIT = 12 };
typedef struct { UiPoint point; GLubyte rgba[4]; } UiVertex;

struct UiRenderer {
    GLuint atlas, texture;
    int count, width, height, drawableW, drawableH, depth;
    UiRect clips[CLIP_LIMIT];
    UiVertex vertices[BATCH_VERTICES];
};

static const unsigned char encodedAtlas[] = {
#include "../../assets/interface/fonts/InterfaceAtlas.inc"
};

UiRenderer *UiRenderer_Create(void)
{
    UiRenderer *r = calloc(1, sizeof(*r));
    unsigned char *pixels = malloc(ATLAS * ATLAS);
    if (!r || !pixels) {
        free(r);
        free(pixels);
        return NULL;
    }
    size_t offset = 0;
    for (size_t i = 0; i < sizeof(encodedAtlas); ++i) {
        unsigned run = 1 + (encodedAtlas[i] & 15);
        if (offset + run > ATLAS * ATLAS) break;
        memset(pixels + offset, (encodedAtlas[i] >> 4) * 17, run);
        offset += run;
    }
    if (offset == ATLAS * ATLAS) {
        glGenTextures(1, &r->atlas);
        glBindTexture(GL_TEXTURE_2D, r->atlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA8, ATLAS, ATLAS, 0,
                     GL_ALPHA, GL_UNSIGNED_BYTE, pixels);
    }
    free(pixels);
    if (!r->atlas || glGetError() != GL_NO_ERROR) {
        UiRenderer_Destroy(r);
        return NULL;
    }
    return r;
}

void UiRenderer_Destroy(UiRenderer *r)
{
    if (!r) return;
    if (r->atlas) glDeleteTextures(1, &r->atlas);
    free(r);
}

static void Flush(UiRenderer *r)
{
    if (!r->count) return;
    glBindTexture(GL_TEXTURE_2D, r->texture);
    glDrawArrays(GL_TRIANGLES, 0, r->count);
    r->count = 0;
}

void UiRenderer_Begin(UiRenderer *r, int w, int h, int dw, int dh)
{
    r->width = w;
    r->height = h;
    r->drawableW = dw;
    r->drawableH = dh;
    r->depth = 0;
    r->count = 0;
    glViewport(0, 0, dw, dh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(UiVertex), &r->vertices[0].point.x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(UiVertex), &r->vertices[0].point.u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(UiVertex), r->vertices[0].rgba);
    glClearColor(0.014f, 0.04f, 0.065f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void UiRenderer_End(UiRenderer *r)
{
    Flush(r);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

static void Quad(UiRenderer *r, GLuint texture, const UiPoint p[4], UiColor top, UiColor bottom)
{
    if (r->texture != texture || r->count + 6 > BATCH_VERTICES) Flush(r);
    r->texture = texture;
    const int indices[] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        int j = indices[i];
        UiColor c = j < 2 ? top : bottom;
        r->vertices[r->count++] = (UiVertex){p[j], {c >> 24, c >> 16, c >> 8, c}};
    }
}

void UiRenderer_Quad(UiRenderer *r, unsigned texture, const UiPoint p[4], UiColor color)
{
    Quad(r, texture, p, color, color);
}

void UiRenderer_Gradient(UiRenderer *r, UiRect b, UiColor top, UiColor bottom)
{
    const float texel = 510.0f / ATLAS;
    Quad(r, r->atlas, (UiPoint[]){{b.x,b.y,texel,texel},{b.x+b.w,b.y,texel,texel},
        {b.x+b.w,b.y+b.h,texel,texel},{b.x,b.y+b.h,texel,texel}}, top, bottom);
}

void UiRenderer_Rect(UiRenderer *r, UiRect b, UiColor color)
{
    if (b.w > 0 && b.h > 0) UiRenderer_Gradient(r, b, color, color);
}

void UiRenderer_Outline(UiRenderer *r, UiRect b, UiColor c)
{
    UiRenderer_Rect(r, (UiRect){b.x,b.y,b.w,1}, c);
    UiRenderer_Rect(r, (UiRect){b.x,b.y+b.h-1,b.w,1}, c);
    UiRenderer_Rect(r, (UiRect){b.x,b.y+1,1,b.h-2}, c);
    UiRenderer_Rect(r, (UiRect){b.x+b.w-1,b.y+1,1,b.h-2}, c);
}

void UiRenderer_Line(UiRenderer *r, float x, float y, float endX, float endY,
                     float thickness, UiColor color)
{
    float dx = endX - x, dy = endY - y, length = hypotf(dx, dy);
    if (length == 0) return;
    float nx = -dy * thickness / (2 * length), ny = dx * thickness / (2 * length);
    const float t = 510.0f / ATLAS;
    UiRenderer_Quad(r, r->atlas, (UiPoint[]){{x+nx,y+ny,t,t},{endX+nx,endY+ny,t,t},
        {endX-nx,endY-ny,t,t},{x-nx,y-ny,t,t}}, color);
}

static UiRect Intersect(UiRect a, UiRect b)
{
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int right = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return (UiRect){x, y, right > x ? right-x : 0, bottom > y ? bottom-y : 0};
}

static void ApplyClip(UiRenderer *r)
{
    if (!r->depth) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    UiRect b = r->clips[r->depth - 1];
    float sx = (float)r->drawableW / r->width, sy = (float)r->drawableH / r->height;
    int x = lroundf(b.x * sx), right = lroundf((b.x + b.w) * sx);
    int y = lroundf((r->height - b.y - b.h) * sy), top = lroundf((r->height - b.y) * sy);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, right - x, top - y);
}

bool UiRenderer_PushClip(UiRenderer *r, UiRect b)
{
    if (r->depth == CLIP_LIMIT) return false;
    Flush(r);
    UiRect parent = r->depth ? r->clips[r->depth-1] : (UiRect){0,0,r->width,r->height};
    r->clips[r->depth++] = Intersect(parent, b);
    ApplyClip(r);
    return true;
}

void UiRenderer_PopClip(UiRenderer *r)
{
    if (!r->depth) return;
    Flush(r);
    --r->depth;
    ApplyClip(r);
}

int UiRenderer_Cell(int fontSize) { return (fontSize * 3 + 2) / 5; }

int UiRenderer_Measure(const char *str, size_t bytes, int size)
{
    return (int)UiText_Count(str, bytes) * UiRenderer_Cell(size);
}

void UiRenderer_TextN(UiRenderer *r, int x, int y, int size, UiColor color,
                      const char *str, size_t bytes)
{
    size_t offset = 0;
    int advance = UiRenderer_Cell(size);
    float height = size * (38.0f / 32);
    for (; offset < bytes; x += advance) {
        uint32_t cp = UiText_Next(str, &offset);
        if (!cp) break;
        if (cp == ' ' || cp == 160) continue;
        unsigned i = cp >= 33 && cp <= 126 ? cp - 32 :
                     cp >= 161 && cp <= 383 ? cp - 65 : 319;
        float u = (i % 25 * 20 + 0.5f) / ATLAS, v = (i / 25 * 38 + 0.5f) / ATLAS;
        float u1 = u + 19.0f / ATLAS, v1 = v + 37.0f / ATLAS;
        UiRenderer_Quad(r, r->atlas, (UiPoint[]){{x,y,u,v},{x+advance,y,u1,v},
            {x+advance,y+height,u1,v1},{x,y+height,u,v1}}, color);
    }
}

void UiRenderer_Text(UiRenderer *r, int x, int y, int size, UiColor c, const char *str)
{
    UiRenderer_TextN(r, x, y, size, c, str, strlen(str));
}
