#include "interface/UiPlatform.h"
#include "interface/UiTerminalView.h"
#include "interface/UiTheme.h"
#include <math.h>
#include <stdlib.h>

enum { TOP = 54, BOTTOM = 56, MARGIN = 26 };

typedef struct {
    UiDesktop desktop;
    UiCommands commands;
    UiTerminal terminal;
    UiWindowId terminalId;
    int height;
} InterfaceDemo;

static UiRect Dock(int height) { return (UiRect){MARGIN,height-BOTTOM+10,168,36}; }

static void Resize(void *state, int w, int h)
{
    InterfaceDemo *demo = state;
    demo->height = h;
    UiDesktop_SetArea(&demo->desktop,(UiRect){MARGIN,TOP+14,w-2*MARGIN,h-TOP-BOTTOM-28});
    if (!demo->terminalId) {
        int windowW = w >= 960 ? 676 : w-2*MARGIN;
        int windowH = h >= 620 ? 442 : h-TOP-BOTTOM-28;
        demo->terminalId = UiDesktop_Add(&demo->desktop,(UiWindow){
            .rect={w-60-windowW,(h-windowH)/2,windowW,windowH},
            .minWidth=340,.minHeight=235,.title="Command Terminal",.acceptsText=true,
            .draw=UiTerminalView_Draw,.drawIcon=UiTheme_TerminalIcon,.input=UiTerminalView_Input,.state=&demo->terminal
        });
    }
}

static bool DesktopPress(void *state, int x, int y)
{
    InterfaceDemo *demo = state;
    if (!UiRect_Contains(Dock(demo->height),x,y)) return false;
    UiDesktop_Activate(&demo->desktop,demo->terminalId);
    return true;
}

static UiRect TextRect(void *state, UiWindowId id)
{
    InterfaceDemo *demo = state;
    const UiWindow *window = UiDesktop_Find(&demo->desktop,id);
    return window ? UiTerminalView_InputRect(UiWindow_Content(window->rect)) : (UiRect){0};
}

static void Arc(UiRenderer *r, float x, float y, float radius, float start, float end, UiColor c)
{
    float px=x+radius*cosf(start), py=y+radius*sinf(start);
    for (int i=1; i<=40; ++i) {
        float a=start+(end-start)*i/40;
        float nx=x+radius*cosf(a), ny=y+radius*sinf(a);
        UiRenderer_Line(r,px,py,nx,ny,1,c);
        px=nx;
        py=ny;
    }
}

static void DesktopMark(UiRenderer *r, int w, int h)
{
    int x = (w-736+MARGIN)/2, y = h/2-26;
    if (x > 285) x = 285;
    if (!UiRenderer_PushClip(r,(UiRect){MARGIN,TOP+1,2*x-2*MARGIN,h-TOP-BOTTOM-1})) return;
    UiRenderer_Line(r,x-145,y,x+145,y,1,0x416f8166);
    UiRenderer_Line(r,x,y-134,x,y+143,1,0x416f8166);
    Arc(r,x,y,99,0,6.2831853f,0x487d9088);
    Arc(r,x,y,70,0,6.2831853f,0x45788e65);
    Arc(r,x,y,108,-1.37f,0.26f,0x59b5bfaa);
    Arc(r,x,y,108,1.76f,3.40f,0x59b5bfaa);
    for (int i=0; i<48; ++i) {
        float a=i*6.2831853f/48, dx=cosf(a), dy=sinf(a);
        float inner=i%6==0 ? 84 : 92;
        UiRenderer_Line(r,x+dx*inner,y+dy*inner,x+dx*98,y+dy*98,1,0x52869a99);
    }
    UiRenderer_Line(r,x,y-41,x+35,y+20,2,0x75d8dfc5);
    UiRenderer_Line(r,x+35,y+20,x-35,y+20,2,0x75d8dfc5);
    UiRenderer_Line(r,x-35,y+20,x,y-41,2,0x75d8dfc5);
    UiRenderer_Line(r,x-17,y-10,x+17,y-10,1,0x74d0d6cc);
    UiRenderer_Line(r,x-17,y-10,x,y+20,1,0x74d0d6cc);
    UiRenderer_Line(r,x+17,y-10,x,y+20,1,0x74d0d6cc);
    UiRenderer_Rect(r,(UiRect){x-2,y-2,4,4},UI_ACCENT);
    UiRenderer_PopClip(r);

    if (h >= 600) {
        const char label[] = "Command Desktop";
        int width = UiRenderer_Measure(label,sizeof(label)-1,30);
        UiRenderer_Text(r,x-width/2,y+134,30,0xc8ecf1ff,label);
        UiRenderer_Rect(r,(UiRect){x-width/2,y+179,width,1},0x457387a4);
        UiRenderer_Rect(r,(UiRect){x-16,y+179,32,1},UI_ACCENT);
    }
}

static void Background(UiRenderer *r, int w, int h)
{
    UiRenderer_Gradient(r,(UiRect){0,0,w,h},0x10374aff,0x07131fff);
    for (int x=MARGIN; x<w-MARGIN; x+=24) {
        UiColor color = (x-MARGIN)%120 ? 0x6797ac17 : 0x6797ac36;
        UiRenderer_Rect(r,(UiRect){x,TOP,1,h-TOP-BOTTOM},color);
    }
    for (int y=TOP; y<h-BOTTOM; y+=24) {
        UiColor color = (y-TOP)%120 ? 0x6797ac17 : 0x6797ac36;
        UiRenderer_Rect(r,(UiRect){MARGIN,y,w-2*MARGIN,1},color);
    }
    UiRenderer_Outline(r,(UiRect){MARGIN,TOP+14,w-2*MARGIN,h-TOP-BOTTOM-28},0x507f9354);
    for (int x=MARGIN+120; x<w-MARGIN; x+=120) {
        UiRenderer_Rect(r,(UiRect){x,TOP+14,1,5},0x6299ac70);
        UiRenderer_Rect(r,(UiRect){x,h-BOTTOM-19,1,5},0x6299ac70);
    }
    for (int side=0; side<2; ++side) {
        int x = side ? w-MARGIN-19 : MARGIN, y = side ? h-BOTTOM-16 : TOP+14;
        UiRenderer_Rect(r,(UiRect){x,y,19,2},0x6badbb99);
        UiRenderer_Rect(r,(UiRect){side ? x+17 : x,side ? y-13 : y,2,15},0x6badbb99);
    }
    if (w >= 1080 && h >= 500) DesktopMark(r,w,h);
}

static void TopBar(UiRenderer *r, int w)
{
    UiRenderer_Gradient(r,(UiRect){0,0,w,TOP},0x112b3bff,0x091b29ff);
    UiRenderer_Rect(r,(UiRect){0,TOP-1,w,1},UI_EDGE);
    UiRenderer_Rect(r,(UiRect){MARGIN,TOP-1,150,1},UI_ACCENT);
    UiRenderer_Outline(r,(UiRect){MARGIN,12,30,30},UI_EDGE);
    UiRenderer_Line(r,MARGIN+7,34,MARGIN+15,19,1.5f,UI_ACCENT);
    UiRenderer_Line(r,MARGIN+15,19,MARGIN+23,34,1.5f,UI_ACCENT);
    UiRenderer_Line(r,MARGIN+7,34,MARGIN+23,34,1.5f,UI_ACCENT);
    UiRenderer_Text(r,68,12,18,UI_PAPER,"N E X U S");
    UiRenderer_Text(r,68,32,11,UI_MUTED,"INTERFACE 01");
    UiRenderer_Rect(r,(UiRect){w-158,25,5,5},UI_ACCENT);
    UiRenderer_Text(r,w-142,18,14,UI_ACCENT,"SYSTEM ONLINE");
}

static void Draw(void *state, UiRenderer *r, int w, int h)
{
    InterfaceDemo *demo = state;
    Background(r,w,h);
    TopBar(r,w);
    UiRenderer_Gradient(r,(UiRect){0,h-BOTTOM,w,BOTTOM},0x0b1e2cff,0x091722ff);
    UiRenderer_Rect(r,(UiRect){0,h-BOTTOM,w,1},UI_EDGE);
    UiRect b = Dock(h);
    const UiWindow *window = UiDesktop_Find(&demo->desktop,demo->terminalId);
    bool active = demo->desktop.focus == demo->terminalId;
    UiRenderer_Gradient(r,b,active ? 0x1a4351ff : 0x122f3eff,0x0c2230ff);
    UiRenderer_Outline(r,b,active ? 0x508791ff : UI_EDGE);
    UiRenderer_Rect(r,(UiRect){b.x,b.y,2,b.h},active ? UI_ACCENT : UI_EDGE);
    UiTheme_TerminalIcon(r,b.x+13,b.y+13,active ? UI_ACCENT : UI_MUTED);
    UiRenderer_Text(r,b.x+41,b.y+8,15,UI_PAPER,"Terminal");
    UiRenderer_Rect(r,(UiRect){b.x+b.w-15,b.y+16,5,5},window && !window->hidden ? UI_ACCENT : UI_EDGE);
    UiRenderer_Text(r,w-177,b.y+11,12,UI_MUTED,"CTRL TAB  Focus");
}

int main(void)
{
    InterfaceDemo *demo = calloc(1,sizeof(*demo));
    if (!demo) return 1;
    UiCommands_Register(&demo->commands,"print",UiCommands_Print,NULL);
    UiTerminal_Init(&demo->terminal,&demo->commands);
    UiTerminal_Append(&demo->terminal,UI_LOG_SYSTEM,"Session initialized.");
    UiTerminal_Append(&demo->terminal,UI_LOG_SYSTEM,"Terminal ready. Try print Hello world");
    int result = UiPlatform_Run("Nexus | Command Desktop",(UiApp){
        .state=demo,.desktop=&demo->desktop,.resize=Resize,.desktopPress=DesktopPress,
        .draw=Draw,.textRect=TextRect
    });
    free(demo);
    return result;
}
