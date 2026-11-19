#include "interface/UiCreatureVisualizer.h"
#include "interface/UiTheme.h"
#include "interface/UiRecipeRoutes.h"
#include "UiCreatureEngine.h"
#include <stdio.h>

static void Close(void *state)
{
    UiCreatureVisualizer *app = state;
    app->window = 0;
    app->visibleLastFrame = false;
    UiCreatureEngine_Destroy(app->engine);
    app->engine = NULL;
}

static void Draw(void *state, UiRenderer *renderer, UiRect content, bool active, bool caret)
{
    UiCreatureVisualizer *app = state;
    (void)active;
    (void)caret;
    unsigned texture = UiCreatureEngine_Texture(app->engine);
    if (!texture) {
        UiRenderer_Rect(renderer,content,0x000000ff);
        return;
    }
    int x = content.x, y = content.y, right = x+content.w, bottom = y+content.h;
    UiRenderer_Quad(renderer,texture,(UiPoint[]){{x,y,0,1},{right,y,1,1},
        {right,bottom,1,0},{x,bottom,0,0}},0xffffffffu);
}

void UiCreatureVisualizer_Icon(UiRenderer *r, int x, int y, uint32_t color)
{
    UiRenderer_Line(r,x,y+4,x+6,y,1,color);
    UiRenderer_Line(r,x+6,y,x+13,y+4,1,color);
    UiRenderer_Line(r,x+13,y+4,x+6,y+9,1,color);
    UiRenderer_Line(r,x+6,y+9,x,y+4,1,color);
    UiRenderer_Rect(r,(UiRect){x+5,y+3,3,3},color);
}

bool UiCreatureVisualizer_Open(UiCreatureVisualizer *app)
{
    if (UiDesktop_Find(app->desktop,app->window)) {
        UiDesktop_Activate(app->desktop,app->window);
        return true;
    }
    UiRect area = app->desktop->area;
    app->window = UiDesktop_Add(app->desktop,(UiWindow){
        .rect={area.x+25,area.y+26,490,405},.minWidth=300,.minHeight=200,
        .title="CreatureVisualizer",.draw=Draw,.drawIcon=UiCreatureVisualizer_Icon,
        .onClose=Close,.state=app
    });
    return app->window != 0;
}

UiCommandResult UiCreatureVisualizer_Command(void *state, const char *args, char *out, size_t capacity)
{
    if (*args) snprintf(out,capacity,"CreatureVisualizer: Expected no arguments");
    else if (UiCreatureVisualizer_Open(state)) return UI_COMMAND_SILENT;
    else snprintf(out,capacity,"CreatureVisualizer: Desktop window limit reached");
    return UI_COMMAND_ERROR;
}

UiCommandResult UiCreatureVisualizer_CloseCommand(void *state, const char *args, char *out, size_t capacity)
{
    UiCreatureVisualizer *app = state;
    if (*args) {
        snprintf(out,capacity,"CreatureVisualizer.close: Expected no arguments");
        return UI_COMMAND_ERROR;
    }
    if (UiDesktop_Close(app->desktop,app->window)) return UI_COMMAND_SILENT;
    snprintf(out,capacity,"CreatureVisualizer is already closed.");
    return UI_COMMAND_OK;
}

void UiCreatureVisualizer_Close(UiCreatureVisualizer *app)
{
    UiDesktop_Close(app->desktop,app->window);
}

static const UiWindow *Visible(const UiCreatureVisualizer *app)
{
    const UiWindow *window = UiDesktop_Find(app->desktop,app->window);
    return window && !window->hidden ? window : NULL;
}

bool UiCreatureVisualizer_NeedsFrame(const UiCreatureVisualizer *app)
{
    return Visible(app) && UiCreatureEngine_NeedsFrame(app->engine);
}

const char *UiCreatureVisualizer_Prepass(UiCreatureVisualizer *app, int w, int h,
                                         int dw, int dh, double dt)
{
    const UiWindow *window = Visible(app);
    bool resumed = !app->visibleLastFrame;
    app->visibleLastFrame = window != NULL;
    if (!window || !app->engine) return NULL;
    UiRect area = UiWindow_Content(window->rect);
    UiRect pixels = UiRect_Drawable(area,w,h,dw,dh);
    return UiCreatureEngine_Render(app->engine,pixels.w,pixels.h,resumed ? 0.0 : dt);
}

static bool RequestView(UiCreatureVisualizer *app, const UiVfsNode *first,
                        const UiVfsNode *second, char *out, size_t capacity)
{
    if (!UiDesktop_Find(app->desktop,app->window) && app->desktop->count == UI_MAX_WINDOWS) {
        snprintf(out,capacity,"ERROR: Desktop window limit reached");
        return false;
    }
    if (!UiCreatureEngine_Request(&app->engine,app->executableDirectory,first,second,out,capacity))
        return false;
    if (UiCreatureVisualizer_Open(app)) return true;
    UiCreatureEngine_Destroy(app->engine);
    app->engine = NULL;
    snprintf(out,capacity,"ERROR: Cannot open CreatureVisualizer");
    return false;
}

bool UiCreatureVisualizer_ViewRecipe(UiCreatureVisualizer *app, UiVfsId recipe,
                                     char *error, size_t capacity)
{
    const UiVfsNode *node = UiVfs_Get(app->shell->fs,recipe);
    if (!UiVfs_IsRecipe(node)) {
        snprintf(error,capacity,"ERROR: Expected a recipe file");
        return false;
    }
    return RequestView(app,node,NULL,error,capacity);
}

static UiCommandResult View(UiCreatureVisualizer *app, const char *route, bool pair,
                            char *out, size_t capacity)
{
    const UiVfsNode *first, *second;
    const char *error = UiRecipeRoutes_Resolve(app->shell->fs,app->shell->cwd,route,pair,&first,&second);
    if (error) {
        snprintf(out,capacity,"ERROR: %s",error);
        return UI_COMMAND_ERROR;
    }
    return RequestView(app,first,second,out,capacity) ? UI_COMMAND_SILENT : UI_COMMAND_ERROR;
}

static UiCommandResult Single(void *state, const char *route, char *out, size_t capacity)
{
    return View(state,route,false,out,capacity);
}

static UiCommandResult Interpolation(void *state, const char *route, char *out, size_t capacity)
{
    return View(state,route,true,out,capacity);
}

bool UiCreatureVisualizer_Register(UiCreatureVisualizer *app, UiCommands *commands)
{
    return UiCommands_Register(commands,"CreatureVisualizer",UiCreatureVisualizer_Command,app) &&
        UiCommands_Register(commands,"CreatureVisualizer.close",UiCreatureVisualizer_CloseCommand,app) &&
        UiCommands_RegisterPrefix(commands,"CreatureVisualizer.view.",Single,app) &&
        UiCommands_RegisterPrefix(commands,"CreatureVisualizer.view.interpolation.",Interpolation,app) &&
        UiCommands_Register(commands,"CV",UiCreatureVisualizer_Command,app) &&
        UiCommands_Register(commands,"CV.close",UiCreatureVisualizer_CloseCommand,app) &&
        UiCommands_RegisterPrefix(commands,"CV.view.",Single,app) &&
        UiCommands_RegisterPrefix(commands,"CV.view.interpolation.",Interpolation,app);
}
