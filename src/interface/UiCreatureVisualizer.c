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

static void Input(void *state, UiRect content, const UiEvent *event)
{
    UiCreatureVisualizer *app = state;
    (void)content;
    UiCreatureEngine_OrbitInput(app->engine,event);
}

static void Draw(void *state, UiRenderer *renderer, UiRect content, bool active, bool caret)
{
    UiCreatureVisualizer *app = state;
    (void)active;
    (void)caret;
    UiCreatureFrame frame = UiCreatureEngine_Front(app->engine);
    if (!frame.texture) {
        UiRenderer_Rect(renderer,content,0x000000ff);
        return;
    }
    /* Si el escritorio se redimensiona durante el cierre, la textura congelada conserva su aspecto. */
    int x = content.x, y = content.y, width = content.w, height = content.h;
    if ((int64_t)width*frame.height > (int64_t)height*frame.width) {
        width = (int)(((int64_t)height*frame.width+frame.height/2)/frame.height);
        x += (content.w-width)/2;
    } else {
        height = (int)(((int64_t)width*frame.height+frame.width/2)/frame.width);
        y += (content.h-height)/2;
    }
    if (width < content.w || height < content.h) UiRenderer_Rect(renderer,content,0x000000ff);
    int right = x+width, bottom = y+height;
    UiRenderer_Quad(renderer,frame.texture,(UiPoint[]){{x,y,0,1},{right,y,1,1},
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
        .input=Input,.capturesContentPointer=true,.onClose=Close,.state=app
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

static const UiWindow *Ready(const UiCreatureVisualizer *app)
{
    const UiWindow *window = UiDesktop_Find(app->desktop,app->window);
    return UiWindow_IsInteractive(window) ? window : NULL;
}

bool UiCreatureVisualizer_NeedsFrame(const UiCreatureVisualizer *app)
{
    return Ready(app) && UiCreatureEngine_NeedsFrame(app->engine);
}

const char *UiCreatureVisualizer_Prepass(UiCreatureVisualizer *app, int w, int h,
                                         int dw, int dh, double dt)
{
    const UiWindow *window = Ready(app);
    bool resumed = !app->visibleLastFrame;
    app->visibleLastFrame = window != NULL;
    if (!window || !app->engine) return NULL;
    /* Presenta el marco completo antes del primer pase, que puede compilar shaders. */
    if (resumed && !UiCreatureEngine_Front(app->engine).texture) return NULL;
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

bool UiCreatureVisualizer_ViewTemporary(UiCreatureVisualizer *app, const CreatureRecipe *recipe,
    unsigned candidate, char *error, size_t capacity)
{
    if (!UiDesktop_Find(app->desktop,app->window) && app->desktop->count == UI_MAX_WINDOWS) {
        snprintf(error,capacity,"ERROR: Desktop window limit reached");
        return false;
    }
    return UiCreatureEngine_RequestTemporary(&app->engine,recipe,candidate,error,capacity) &&
        UiCreatureVisualizer_Open(app);
}

void UiCreatureVisualizer_InvalidateTemporary(UiCreatureVisualizer *app, unsigned candidate)
{
    UiCreatureEngine_InvalidateTemporary(app->engine,candidate);
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
