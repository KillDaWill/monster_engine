#include "interface/UiPlatform.h"
#include "interface/UiFilesView.h"
#include "interface/UiCreatureVisualizer.h"
#include "interface/UiRecipeCatalog.h"
#include "interface/UiShell.h"
#include "interface/UiTerminalView.h"
#include "interface/UiTheme.h"
#include "interface_layout.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    UiDesktop desktop;
    UiCommands commands;
    UiTerminal terminal;
    UiVfs *fs;
    UiShell shell;
    UiFilesApp files;
    UiFilesLaunch filesLaunch;
    UiCreatureVisualizer visualizer;
    UiWindowId terminalId;
    int height;
} InterfaceDemo;

static UiRect Dock(int height, int index)
{
    static const int x[] = {0,178,316}, widths[] = {168,128,182};
    return (UiRect){MARGIN+x[index],height-BOTTOM+10,widths[index],36};
}

static void Resize(void *state, int w, int h)
{
    InterfaceDemo *demo = state;
    demo->height = h;
    UiRect area = WorkspaceRect(w,h);
    UiDesktop_SetArea(&demo->desktop,area);
    if (!demo->terminalId) {
        int windowW = w >= 960 ? 676 : area.w;
        int windowH = h >= 620 ? 442 : area.h;
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
    if (UiRect_Contains(Dock(demo->height,0),x,y)) {
        UiDesktop_Activate(&demo->desktop,demo->terminalId);
        return true;
    }
    if (UiRect_Contains(Dock(demo->height,1),x,y)) {
        if (!UiFilesApp_Open(&demo->filesLaunch))
            UiTerminal_Append(&demo->terminal,UI_LOG_ERROR,"Files: Desktop window limit reached");
        return true;
    }
    if (UiRect_Contains(Dock(demo->height,2),x,y)) {
        if (!UiCreatureVisualizer_Open(&demo->visualizer))
            UiTerminal_Append(&demo->terminal,UI_LOG_ERROR,"CreatureVisualizer: Desktop window limit reached");
        return true;
    }
    return false;
}

static UiRect TextRect(void *state, UiWindowId id)
{
    InterfaceDemo *demo = state;
    const UiWindow *window = UiDesktop_Find(&demo->desktop,id);
    return window ? UiTerminalView_TextRect(&demo->terminal,UiWindow_Content(window->rect)) : (UiRect){0};
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

static void DesktopMark(UiRenderer *r, int w, int h, UiRect interior)
{
    int x = (w-736+MARGIN)/2, y = h/2-26;
    if (x > 285) x = 285;
    if (!UiRenderer_PushClip(r,interior)) return;
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

    if (h >= 600) {
        const char label[] = "Command Desktop";
        int width = UiRenderer_Measure(label,sizeof(label)-1,30);
        UiRenderer_Text(r,x-width/2,y+134,30,0xc8ecf1ff,label);
        UiRenderer_Rect(r,(UiRect){x-width/2,y+179,width,1},0x457387a4);
        UiRenderer_Rect(r,(UiRect){x-16,y+179,32,1},UI_ACCENT);
    }
    UiRenderer_PopClip(r);
}

static void Background(UiRenderer *r, int w, int h, UiRect area)
{
    UiRenderer_Gradient(r,(UiRect){0,0,w,h},0x10374aff,0x07131fff);
    UiRect interior = WorkspaceInterior(area);
    int right = area.x+area.w-1, bottom = area.y+area.h-1;
    for (int x=area.x+24; x<right; x+=24) {
        UiColor color = (x-area.x)%120 ? 0x6797ac17 : 0x6797ac36;
        UiRenderer_Rect(r,(UiRect){x,interior.y,1,interior.h},color);
    }
    for (int y=area.y+24; y<bottom; y+=24) {
        UiColor color = (y-area.y)%120 ? 0x6797ac17 : 0x6797ac36;
        UiRenderer_Rect(r,(UiRect){interior.x,y,interior.w,1},color);
    }
    if (w >= 1080 && h >= 500) DesktopMark(r,w,h,interior);
    UiRenderer_Outline(r,area,0x507f9354);
    for (int x=area.x+120; x<right; x+=120) {
        UiRenderer_Rect(r,(UiRect){x,area.y,1,5},0x6299ac70);
        UiRenderer_Rect(r,(UiRect){x,bottom-4,1,5},0x6299ac70);
    }
    for (int side=0; side<2; ++side) {
        int x = side ? right-18 : area.x, y = side ? bottom-1 : area.y;
        UiRenderer_Rect(r,(UiRect){x,y,19,2},0x6badbb99);
        UiRenderer_Rect(r,(UiRect){side ? x+17 : x,side ? y-13 : y,2,15},0x6badbb99);
    }
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

static void DockButton(const InterfaceDemo *demo, UiRenderer *r, UiRect b, UiWindowId id,
                       const char *label, UiWindowIcon icon)
{
    const UiWindow *window = UiDesktop_Find(&demo->desktop,id);
    bool active = id && demo->desktop.focus == id;
    UiRenderer_Gradient(r,b,active ? 0x1a4351ff : 0x122f3eff,0x0c2230ff);
    UiRenderer_Outline(r,b,active ? 0x508791ff : UI_EDGE);
    UiRenderer_Rect(r,(UiRect){b.x,b.y,2,b.h},active ? UI_ACCENT : UI_EDGE);
    icon(r,b.x+13,b.y+13,active ? UI_ACCENT : UI_MUTED);
    int size = UiRenderer_Measure(label,strlen(label),15) > b.w-61 ? 12 : 15;
    int labelX = b.x+(size == 12 ? 34 : 41);
    UiRenderer_Text(r,labelX,b.y+(size == 12 ? 11 : 8),size,UI_PAPER,label);
    UiRenderer_Rect(r,(UiRect){b.x+b.w-15,b.y+16,5,5},window && !window->hidden ? UI_ACCENT : UI_EDGE);
}

static void Draw(void *state, UiRenderer *r, int w, int h)
{
    InterfaceDemo *demo = state;
    Background(r,w,h,demo->desktop.area);
    TopBar(r,w);
    UiRenderer_Gradient(r,(UiRect){0,h-BOTTOM,w,BOTTOM},0x0b1e2cff,0x091722ff);
    UiRenderer_Rect(r,(UiRect){0,h-BOTTOM,w,1},UI_EDGE);
    DockButton(demo,r,Dock(h,0),demo->terminalId,"Terminal",UiTheme_TerminalIcon);
    DockButton(demo,r,Dock(h,1),demo->files.window,"Files",UiFilesView_Icon);
    DockButton(demo,r,Dock(h,2),demo->visualizer.window,"CreatureVisualizer",UiCreatureVisualizer_Icon);
    if (w >= 720) UiRenderer_Text(r,w-177,h-BOTTOM+21,12,UI_MUTED,"CTRL TAB  Focus");
}

static bool NeedsFrame(void *state)
{
    return UiCreatureVisualizer_NeedsFrame(&((InterfaceDemo *)state)->visualizer);
}

static void Prepass(void *state, int w, int h, int dw, int dh, double dt)
{
    InterfaceDemo *demo = state;
    const char *error = UiCreatureVisualizer_Prepass(&demo->visualizer,w,h,dw,dh,dt);
    if (error) UiTerminal_Append(&demo->terminal,UI_LOG_ERROR,error);
}

static bool OpenRecipe(void *state, UiVfsId recipe, char *error, size_t capacity)
{
    InterfaceDemo *demo = state;
    if (UiCreatureVisualizer_ViewRecipe(&demo->visualizer,recipe,error,capacity)) return true;
    UiTerminal_Append(&demo->terminal,UI_LOG_ERROR,error);
    return false;
}

int main(void)
{
    InterfaceDemo *demo = calloc(1,sizeof(*demo));
    if (!demo) return 1;
    demo->fs = UiVfs_Create();
    UiVfsId recipes = UiVfs_Add(demo->fs,UiVfs_Root(demo->fs),"recepies",
                               UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
    if (!recipes) {
        UiVfs_Destroy(demo->fs);
        free(demo);
        return 1;
    }
    char directory[4096];
    const char *base = UiPlatform_ExecutableDirectory(directory,sizeof(directory)) ? directory : NULL;
    UiRecipeCatalogReport report = UiRecipeCatalog_LoadProject(demo->fs,recipes,base);
    UiShell_Init(&demo->shell,demo->fs);
    UiFilesApp_Init(&demo->files,demo->fs);
    demo->files.openRecipe = OpenRecipe;
    demo->files.recipeContext = demo;
    demo->filesLaunch = (UiFilesLaunch){
        .files=&demo->files,.desktop=&demo->desktop,
        .presentation={
            .rect={MARGIN+25,TOP+40,480,405},.minWidth=360,.minHeight=224,
            .title="Files",.draw=UiFilesView_Draw,.input=UiFilesView_Input,.drawIcon=UiFilesView_Icon
        }
    };
    demo->visualizer = (UiCreatureVisualizer){.desktop=&demo->desktop,.shell=&demo->shell,.executableDirectory=base};
    UiTerminal_Init(&demo->terminal,&demo->commands);
    if (!UiCommands_Register(&demo->commands,"print",UiCommands_Print,NULL) ||
        !UiShell_Register(&demo->shell,&demo->commands) ||
        !UiCommands_Register(&demo->commands,"Files",UiFilesApp_Command,&demo->filesLaunch) ||
        !UiCommands_Register(&demo->commands,"Files.close",UiFilesApp_CloseCommand,&demo->filesLaunch) ||
        !UiCreatureVisualizer_Register(&demo->visualizer,&demo->commands) ||
        !UiCommands_Register(&demo->commands,"clear",UiTerminal_ClearCommand,&demo->terminal)) {
        fputs("Nexus: Cannot register desktop commands\n",stderr);
        UiVfs_Destroy(demo->fs);
        free(demo);
        return 1;
    }
    UiTerminal_Append(&demo->terminal,UI_LOG_SYSTEM,"Session initialized.");
    UiTerminal_Append(&demo->terminal,UI_LOG_SYSTEM,"Terminal ready. Try ls or Files");
    demo->terminal.location = UiShell_Location;
    demo->terminal.locationState = &demo->shell;
    if (!report.manifestAvailable) {
        UiTerminal_Append(&demo->terminal,UI_LOG_ERROR,"Recipe catalog unavailable. Showing built-in recipes.");
    } else if (report.rejected || report.duplicates) {
        char notice[96];
        snprintf(notice,sizeof(notice),"Recipe catalog: %u invalid or missing, %u duplicate entries skipped.",
                 report.rejected,report.duplicates);
        UiTerminal_Append(&demo->terminal,UI_LOG_ERROR,notice);
    }
    int result = UiPlatform_Run("Nexus | Command Desktop",(UiApp){
        .state=demo,.desktop=&demo->desktop,.resize=Resize,.desktopPress=DesktopPress,
        .draw=Draw,.textRect=TextRect,.needsFrame=NeedsFrame,.prepass=Prepass
    });
    UiVfs_Destroy(demo->fs);
    free(demo);
    return result;
}
