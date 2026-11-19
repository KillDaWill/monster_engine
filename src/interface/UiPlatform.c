#include "interface/UiPlatform.h"
#include "interface/UiRenderer.h"
#include "interface/UiTheme.h"
#include <SDL2/SDL.h>
#include <stdio.h>

struct UiPlatform {
    SDL_Window *window;
    SDL_GLContext gl;
    UiRenderer *renderer;
    UiApp app;
    UiWindowId textOwner;
    int width, height;
    bool running, nativeFocus, redraw, mouseCaptured;
    Uint64 blinkStart;
};

static void Release(UiPlatform *p)
{
    if (p->mouseCaptured) SDL_CaptureMouse(SDL_FALSE);
    p->mouseCaptured = false;
    UiDesktop_Release(p->app.desktop);
}

static void SyncTextInput(UiPlatform *p)
{
    const UiWindow *w = UiDesktop_Find(p->app.desktop,p->app.desktop->focus);
    UiWindowId owner = p->nativeFocus && UiWindow_IsInteractive(w) && w->acceptsText ? w->id : 0;
    if (owner != p->textOwner) {
        if (p->textOwner) SDL_StopTextInput();
        if (owner) SDL_StartTextInput();
        p->textOwner = owner;
    }
    if (owner && p->app.textRect) {
        UiRect b = p->app.textRect(p->app.state,owner);
        SDL_SetTextInputRect(&(SDL_Rect){b.x,b.y,b.w,b.h});
    }
}

static void Dispatch(UiPlatform *p, UiEvent e)
{
    if (p->nativeFocus) UiDesktop_Dispatch(p->app.desktop, &e);
}

static void Key(UiPlatform *p, SDL_KeyboardEvent e)
{
    SDL_Keycode code = e.keysym.sym;
    unsigned mods = e.keysym.mod;
    if ((mods & KMOD_CTRL) && (code == SDLK_v || code == SDLK_TAB)) {
        if (e.repeat) return;
        if (code == SDLK_TAB) UiDesktop_Cycle(p->app.desktop, (mods & KMOD_SHIFT) != 0);
        else if (p->textOwner) {
            char *text = SDL_GetClipboardText();
            if (text) {
#if SDL_VERSION_ATLEAST(2,0,22)
                SDL_ClearComposition();
#endif
                Dispatch(p,(UiEvent){.type=UI_PASTE,.text=text});
                SDL_free(text);
            }
        }
        return;
    }
    UiKey key;
    switch (code) {
    case SDLK_LEFT: key = UI_LEFT; break;
    case SDLK_RIGHT: key = UI_RIGHT; break;
    case SDLK_UP: key = UI_UP; break;
    case SDLK_DOWN: key = UI_DOWN; break;
    case SDLK_HOME: key = UI_HOME; break;
    case SDLK_END: key = UI_END; break;
    case SDLK_DELETE: key = UI_DELETE; break;
    case SDLK_BACKSPACE: key = UI_BACKSPACE; break;
    case SDLK_PAGEUP: key = UI_PAGE_UP; break;
    case SDLK_PAGEDOWN: key = UI_PAGE_DOWN; break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (e.repeat) return;
        key = UI_ENTER;
        break;
    default: return;
    }
    Dispatch(p,(UiEvent){.type=UI_KEY,.key=key});
}

static void WindowEvent(UiPlatform *p, SDL_WindowEvent e)
{
    switch (e.event) {
    case SDL_WINDOWEVENT_FOCUS_LOST:
        Dispatch(p,(UiEvent){.type=UI_BLUR});
        p->nativeFocus = false;
        Release(p);
        break;
    case SDL_WINDOWEVENT_FOCUS_GAINED: p->nativeFocus = true; break;
    case SDL_WINDOWEVENT_MINIMIZED: Release(p); break;
    case SDL_WINDOWEVENT_SIZE_CHANGED:
        Release(p);
        p->width = e.data1;
        p->height = e.data2;
        p->app.resize(p->app.state,p->width,p->height);
        break;
    case SDL_WINDOWEVENT_CLOSE: p->running = false; break;
    }
}

static void Event(UiPlatform *p, SDL_Event e)
{
    UiDesktop *d = p->app.desktop;
    p->redraw = true;
    if (e.type != SDL_MOUSEMOTION) p->blinkStart = SDL_GetTicks64();
    switch (e.type) {
    case SDL_QUIT: p->running = false; break;
    case SDL_WINDOWEVENT: WindowEvent(p,e.window); break;
    case SDL_KEYDOWN: Key(p,e.key); break;
    case SDL_TEXTINPUT: Dispatch(p,(UiEvent){.type=UI_TEXT,.text=e.text.text}); break;
    case SDL_TEXTEDITING:
        Dispatch(p,(UiEvent){.type=UI_COMPOSE,.text=e.edit.text,
            .compositionCursor=e.edit.start,.compositionLength=e.edit.length});
        break;
#if SDL_VERSION_ATLEAST(2,0,22)
    case SDL_TEXTEDITING_EXT:
        if (e.editExt.text) {
            Dispatch(p,(UiEvent){.type=UI_COMPOSE,.text=e.editExt.text,
                .compositionCursor=e.editExt.start,.compositionLength=e.editExt.length});
            SDL_free(e.editExt.text);
        }
        break;
#endif
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT && !d->captured &&
            (!p->app.desktopPress || !p->app.desktopPress(p->app.state,e.button.x,e.button.y))) {
            UiDesktop_PressWithClicks(d,e.button.x,e.button.y,e.button.clicks);
            if (d->captured) p->mouseCaptured = SDL_CaptureMouse(SDL_TRUE) == 0;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT) Release(p);
        break;
    case SDL_MOUSEMOTION:
        if (d->captured && !(e.motion.state & SDL_BUTTON_LMASK)) Release(p);
        UiDesktop_Move(d,e.motion.x,e.motion.y);
        break;
    case SDL_MOUSEWHEEL: {
        int x, y;
#if SDL_VERSION_ATLEAST(2,26,0)
        x = e.wheel.mouseX;
        y = e.wheel.mouseY;
#else
        SDL_GetMouseState(&x,&y);
#endif
        int dy = e.wheel.y;
        if (dy > 100) dy = 100;
        if (dy < -100) dy = -100;
        float precise = (float)dy;
#if SDL_VERSION_ATLEAST(2,0,18)
        precise = SDL_clamp(e.wheel.preciseY,-100.0f,100.0f);
#endif
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { dy = -dy; precise = -precise; }
        Dispatch(p,(UiEvent){.type=UI_WHEEL,.x=x,.y=y,.wheel=dy,.preciseWheel=precise});
        break;
    }
    default: break;
    }
    if (p->mouseCaptured && !d->captured) Release(p);
    SyncTextInput(p);
}

static void Run(UiPlatform *p)
{
    p->running = p->redraw = true;
    p->nativeFocus = (SDL_GetWindowFlags(p->window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    SDL_StopTextInput();
    SyncTextInput(p);
    p->blinkStart = SDL_GetTicks64();
    bool lastCaret = false, wasNativeVisible = false;
    Uint64 nextFrame = 0, lastFrame = SDL_GetTicks64(), lastTick = lastFrame;
    SDL_Event e;
    bool pending = false;
    while (p->running) {
        Uint64 now = SDL_GetTicks64();
        bool nativeVisible = !(SDL_GetWindowFlags(p->window) & SDL_WINDOW_MINIMIZED);
        /* La espera devuelve un evento pendiente. Avanzamos primero las ventanas
           existentes para que las nuevas empiecen desde cero, incluso tras un reposo largo. */
        if (nativeVisible && wasNativeVisible && UiDesktop_HasActiveAnimations(p->app.desktop)) {
            UiWindowId focus = p->app.desktop->focus;
            Uint64 elapsed = now-lastTick;
            UiDesktop_Update(p->app.desktop,(elapsed > 100 ? 100 : elapsed)*0.001);
            p->redraw = true;
            if (focus != p->app.desktop->focus) SyncTextInput(p);
        }
        lastTick = now;
        if (pending) Event(p,e);
        /* Limita el drenaje de eventos para presentar también durante entrada continua. */
        for (int i = 0; i < 96 && SDL_PollEvent(&e); ++i) Event(p,e);
        if (!p->running) break;
        now = SDL_GetTicks64();
        nativeVisible = !(SDL_GetWindowFlags(p->window) & SDL_WINDOW_MINIMIZED);
        if (!nativeVisible) lastFrame = now;
        bool animated = nativeVisible && (UiDesktop_HasActiveAnimations(p->app.desktop) ||
            (p->app.needsFrame && p->app.needsFrame(p->app.state)));
        if (!animated) nextFrame = 0;
        Uint64 elapsed = now-p->blinkStart;
        bool caret = p->textOwner && elapsed/530%2 == 0;
        if (p->redraw || caret != lastCaret || (animated && now >= nextFrame)) {
            int dw, dh;
            SDL_GL_GetDrawableSize(p->window,&dw,&dh);
            if (dw > 0 && dh > 0 && p->width > 0 && p->height > 0 &&
                nativeVisible) {
                double dt = !wasNativeVisible ? 0.0 : now-lastFrame > 100 ? 0.1 : (now-lastFrame)*0.001;
                lastFrame = now;
                if (animated) nextFrame = now+16;
                if (p->app.prepass) p->app.prepass(p->app.state,p->width,p->height,dw,dh,dt);
                UiRenderer_Begin(p->renderer,p->width,p->height,dw,dh);
                p->app.draw(p->app.state,p->renderer,p->width,p->height);
                UiTheme_DrawWindows(p->app.desktop,p->renderer,p->nativeFocus,caret);
                UiRenderer_End(p->renderer);
                SDL_GL_SwapWindow(p->window);
            }
            p->redraw = false;
            lastCaret = caret;
        }
        wasNativeVisible = nativeVisible;
        int timeout = nativeVisible && p->textOwner ?
            530-(int)((SDL_GetTicks64()-p->blinkStart)%530) : -1;
        if (animated) {
            now = SDL_GetTicks64();
            int wake = nextFrame > now ? (int)(nextFrame-now) : 1;
            if (timeout < 0 || wake < timeout) timeout = wake;
        }
        pending = SDL_WaitEventTimeout(&e,timeout) == 1;
    }
    Release(p);
    SDL_StopTextInput();
}

static SDL_Window *CreateWindow(const char *title, int width, int height)
{
    return SDL_CreateWindow(title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,
                            SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
}

int UiPlatform_Run(const char *title, UiApp app)
{
#if SDL_VERSION_ATLEAST(2,0,22)
    SDL_SetHint(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT,"1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr,"SDL: %s\n",SDL_GetError());
        return 1;
    }
    UiPlatform p = {.app=app,.width=1280,.height=720};
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
    p.window = CreateWindow(title,p.width,p.height);
    if (p.window) p.gl = SDL_GL_CreateContext(p.window);
    if (!p.gl) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
        if (!p.window) p.window = CreateWindow(title,p.width,p.height);
        if (p.window) p.gl = SDL_GL_CreateContext(p.window);
    }
    if (p.gl) p.renderer = UiRenderer_Create();
    int result = p.renderer ? 0 : 1;
    if (!p.gl) fprintf(stderr,"SDL/OpenGL: %s\n",SDL_GetError());
    else if (!p.renderer) fprintf(stderr,"No se pudo inicializar el atlas OpenGL.\n");
    if (p.renderer) {
        SDL_SetWindowMinimumSize(p.window,560,400);
        SDL_GL_SetSwapInterval(1);
        SDL_GetWindowSize(p.window,&p.width,&p.height);
        app.resize(app.state,p.width,p.height);
        Run(&p);
        UiDesktop_CloseAll(app.desktop);
        if (app.cleanup) app.cleanup(app.state);
        UiRenderer_Destroy(p.renderer);
    }
    if (p.gl) SDL_GL_DeleteContext(p.gl);
    if (p.window) SDL_DestroyWindow(p.window);
    SDL_Quit();
    return result;
}

bool UiPlatform_ExecutableDirectory(char *out, size_t capacity)
{
    char *directory = SDL_GetBasePath();
    if (!directory) return false;
    size_t bytes = SDL_strlen(directory);
    bool fits = bytes < capacity;
    if (fits) SDL_memcpy(out,directory,bytes+1);
    SDL_free(directory);
    return fits;
}
