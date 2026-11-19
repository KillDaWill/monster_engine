#define _POSIX_C_SOURCE 200809L
#include "interface/UiCommands.h"
#include "interface/UiCreatureVisualizer.h"
#include "interface/UiDesktop.h"
#include "interface/UiTerminal.h"
#include "interface/UiText.h"
#include "interface/UiFilesView.h"
#include "../demos/interface_layout.h"
#include "interface/UiRecipeCatalog.h"
#include "interface/UiRecipeRoutes.h"
#include "interface/UiShell.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void InterfaceTest_CheckRecipe(const UiCreatureEngine *, UiVfsId first, UiVfsId second);
void InterfaceTest_CheckViewport(const UiCreatureEngine *, int renders, int width, int height);

static UiCommandResult Custom(void *state, const char *args, char *out, size_t size)
{
    ++*(int *)state;
    snprintf(out, size, "[%s]", args);
    return UI_COMMAND_OK;
}

static void Expect(const UiCommands *c, const char *line, UiCommandResult status, const char *expected)
{
    char out[UI_LOG_BYTES];
    assert(UiCommands_Execute(c,line,out,sizeof(out)) == status);
    assert(!strcmp(out,expected));
}

static void Commands(void)
{
    UiCommands c = {0};
    int calls = 0;
    assert(UiCommands_Register(&c,"print",UiCommands_Print,NULL));
    assert(!UiCommands_Register(&c,"print",Custom,&calls));
    assert(!UiCommands_Register(&c,"two words",Custom,&calls));
    assert(!UiCommands_Register(&c,"empty",NULL,NULL));
    assert(UiCommands_Register(&c,"custom",Custom,&calls));
    assert(UiCommands_Find(&c,"custom")->state == &calls);
    assert(!UiCommands_Find(&c,"unknown"));
    Expect(&c,"",UI_COMMAND_EMPTY,"");
    Expect(&c,"print",UI_COMMAND_OK,"");
    Expect(&c," print    Hola  señor  ",UI_COMMAND_OK,"Hola  señor  ");
    Expect(&c,"print \"  ¡Qué tal!  \"",UI_COMMAND_OK,"  ¡Qué tal!  ");
    Expect(&c,"print \"Dijo \\\"sí\\\" y \\\\.\" ",UI_COMMAND_OK,"Dijo \"sí\" y \\.");
    Expect(&c,"print \"\"",UI_COMMAND_OK,"");
    Expect(&c,"print \"open",UI_COMMAND_ERROR,"ERROR: Expected one double-quoted argument");
    Expect(&c,"print \"one\" two",UI_COMMAND_ERROR,"ERROR: Expected one double-quoted argument");
    Expect(&c,"unknown",UI_COMMAND_ERROR,"ERROR: Unknown command 'unknown'");
    Expect(&c,"custom payload",UI_COMMAND_OK,"[payload]");
    assert(calls == 1);
}

static void Editing(const UiCommands *commands)
{
    UiTerminal t;
    UiTerminal_Init(&t,commands);
    UiTerminal_Insert(&t,"Añ🌒z");
    assert(t.cursor == 8);
    UiTerminal_Key(&t,UI_LEFT);
    UiTerminal_Key(&t,UI_BACKSPACE);
    assert(t.cursor == 3 && !strcmp(t.line,"Añz"));
    UiTerminal_Key(&t,UI_HOME);
    UiTerminal_Key(&t,UI_LEFT);
    UiTerminal_Key(&t,UI_BACKSPACE);
    assert(t.cursor == 0);
    UiTerminal_Key(&t,UI_RIGHT);
    UiTerminal_Key(&t,UI_DELETE);
    assert(!strcmp(t.line,"Az"));
    UiTerminal_Key(&t,UI_END);
    UiTerminal_Key(&t,UI_RIGHT);
    UiTerminal_Key(&t,UI_DELETE);
    assert(t.cursor == 2 && !strcmp(t.line,"Az"));
    UiTerminal_Insert(&t,"\r\nñ\t\xff");
    assert(!strcmp(t.line,"Az ñ �"));

    UiTerminal_Init(&t,commands);
    char nearLimit[UI_COMMAND_BYTES-1];
    memset(nearLimit,'a',sizeof(nearLimit)-1);
    nearLimit[sizeof(nearLimit)-1] = 0;
    UiTerminal_Insert(&t,nearLimit);
    UiTerminal_Insert(&t,"á");
    assert(t.cursor == UI_COMMAND_BYTES-2);
    UiTerminal_Insert(&t,"xmore");
    assert(t.cursor == UI_COMMAND_BYTES-1);

    UiTerminal_Init(&t,commands);
    for (int i=0; i<UI_HISTORY_LIMIT+5; ++i) {
        char line[32];
        snprintf(line,sizeof(line),"print %d",i);
        UiTerminal_Insert(&t,line);
        UiTerminal_Key(&t,UI_ENTER);
    }
    assert(t.historyCount == UI_HISTORY_LIMIT);
    UiTerminal_Insert(&t,"draft á");
    UiTerminal_Key(&t,UI_UP);
    assert(!strcmp(t.line,"print 68"));
    for (int i=0;i<UI_HISTORY_LIMIT+1;++i) UiTerminal_Key(&t,UI_UP);
    assert(!strcmp(t.line,"print 5"));
    for (int i=0;i<UI_HISTORY_LIMIT+1;++i) UiTerminal_Key(&t,UI_DOWN);
    assert(!strcmp(t.line,"draft á"));
    assert(t.cursor == strlen(t.line));
}

typedef struct { char rows[4][32]; int count; } RowCapture;
static void CaptureRow(void *state, int row, UiLogKind kind, const char *text, size_t bytes, bool cont)
{
    RowCapture *capture = state;
    (void)kind;
    assert(row < 4 && bytes < 32);
    (void)cont;
    memcpy(capture->rows[row],text,bytes);
    capture->rows[row][bytes] = 0;
    ++capture->count;
}

static void Scrolling(const UiCommands *commands)
{
    UiTerminal t;
    UiTerminal_Init(&t,commands);
    size_t advance;
    assert(UiText_Wrap("hello world",5,&advance) == 5 && advance == 6);
    assert(UiText_Wrap("abcdef",3,&advance) == 3 && advance == 3);
    size_t malformed = 0;
    assert(UiText_Next("\xf4\x90\x80\x80",&malformed) == 0xfffd && malformed == 1);
    UiTerminal_Layout(&t,5,4);
    UiTerminal_Append(&t,UI_LOG_OUTPUT,"ññ abcd");
    UiTerminal_Append(&t,UI_LOG_OUTPUT,"");
    RowCapture capture = {0};
    UiTerminal_VisitRows(&t,CaptureRow,&capture);
    assert(capture.count == 3 && !strcmp(capture.rows[0],"ññ "));
    assert(!strcmp(capture.rows[1],"abcd") && !strcmp(capture.rows[2],""));
    UiTerminal_Layout(&t,80,3);
    for (int i=0;i<12;++i) UiTerminal_Append(&t,UI_LOG_OUTPUT,"line");
    assert(UiTerminal_FirstRow(&t) == 11);
    UiTerminal_Scroll(&t,4);
    assert(!t.following && UiTerminal_FirstRow(&t) == 7);
    UiTerminal_Append(&t,UI_LOG_OUTPUT,"new");
    assert(UiTerminal_FirstRow(&t) == 7);
    UiTerminal_Layout(&t,2,3);
    assert(UiTerminal_FirstRow(&t) > 7);
    UiTerminal_Scroll(&t,INT_MIN);
    assert(t.following && UiTerminal_FirstRow(&t) == t.totalRows-3);
    UiTerminal_Scroll(&t,INT_MAX);
    for (int i=0;i<UI_LOG_LIMIT+17;++i) UiTerminal_Append(&t,UI_LOG_OUTPUT,"y");
    assert(t.logCount == UI_LOG_LIMIT && !t.following && UiTerminal_FirstRow(&t) == 0);
}

typedef struct { int events[UI_POINTER_RELEASE+1], closes; } WindowState;

static void WindowClose(void *state)
{
    ++((WindowState *)state)->closes;
}
static void WindowInput(void *state, UiRect c, const UiEvent *event)
{
    assert(c.w > 0 && c.h > 0);
    ++((WindowState *)state)->events[event->type];
}

static void Desktop(void)
{
    UiRect left = UiRect_Drawable((UiRect){1,3,9,8},100,40,150,80);
    UiRect right = UiRect_Drawable((UiRect){10,3,3,8},100,40,150,80);
    assert(left.x == 2 && left.y == 6 && left.h == 16);
    assert(left.x+left.w == right.x && right.x+right.w == 20);
    UiDesktop d = {0};
    WindowState one = {0}, two = {0};
    UiDesktop_SetArea(&d,(UiRect){10,20,600,400});
    UiWindow w = {.rect={30,40,300,200},.minWidth=180,.minHeight=130,
                  .acceptsText=true,.input=WindowInput,.state=&one};
    UiWindowId first = UiDesktop_Add(&d,w);
    w.rect.x = 200;
    w.state = &two;
    UiWindowId second = UiDesktop_Add(&d,w);
    assert(first && second != first && !d.focus && d.pendingFocus == second);
    UiDesktop_Update(&d,0.4);
    assert(d.focus == second && d.windows[1].id == second && !one.events[UI_BLUR]);
    UiDesktop_Press(&d,250,140);
    assert(two.events[UI_PRESS] == 1);
    UiDesktop_Press(&d,50,140);
    assert(d.focus == first && d.windows[1].id == first);
    UiDesktop_Dispatch(&d,&(UiEvent){.type=UI_TEXT,.text="test"});
    assert(one.events[UI_TEXT] == 1 && !two.events[UI_TEXT]);
    UiDesktop_Press(&d,550,90);
    assert(!d.focus);
    UiDesktop_Cycle(&d,false);
    assert(d.focus == first);
    UiDesktop_Cycle(&d,false);
    assert(d.focus == second);
    UiDesktop_Cycle(&d,true);
    assert(d.focus == first);
    UiDesktop_Press(&d,45,45);
    assert(d.captured == first);
    UiDesktop_Move(&d,-300,-300);
    UiRect placed = UiDesktop_Find(&d,first)->rect;
    assert(placed.x == 10 && placed.y == 20);
    UiDesktop_Move(&d,900,900);
    placed = UiDesktop_Find(&d,first)->rect;
    assert(placed.x == 310 && placed.y == 220);
    UiDesktop_Release(&d);
    UiDesktop_Press(&d,608,419);
    assert(d.captured == first && d.resizing);
    UiDesktop_Move(&d,400,220);
    placed = UiDesktop_Find(&d,first)->rect;
    assert(placed.w == 180 && placed.h == 130);
    UiDesktop_Release(&d);
    UiRect button = UiWindow_MinimizeButton(placed);
    UiDesktop_Press(&d,button.x+1,button.y+1);
    assert(d.focus == second && UiDesktop_Find(&d,first)->hidden);
    UiDesktop_Activate(&d,first);
    assert(d.focus == first && !UiDesktop_Find(&d,first)->hidden);
    UiDesktop_SetArea(&d,(UiRect){0,0,150,100});
    placed = UiDesktop_Find(&d,first)->rect;
    assert(placed.x == 0 && placed.y == 0 && placed.w == 150 && placed.h == 100);
    UiDesktop_Remove(&d,first);
    UiDesktop_Remove(&d,second);
    assert(!d.count && !d.focus && !d.captured);
}

static UiWindowPresentation Presentation(const UiDesktop *d, UiWindowId id)
{
    const UiWindow *w = UiDesktop_Find(d,id);
    assert(w);
    UiWindowPresentation p = UiWindow_Presentation(w);
    assert(p.frame.w > 0 && p.frame.h > 0 && p.frame.x >= w->rect.x && p.frame.y == w->rect.y);
    assert(p.frame.x+p.frame.w <= w->rect.x+w->rect.w && p.frame.h <= w->rect.h);
    assert(p.contentClip.w >= 0 && p.contentClip.h >= 0);
    if (p.contentClip.h && p.contentClip.w) {
        UiRect content = UiWindow_Content(w->rect);
        assert(p.contentClip.x == content.x && p.contentClip.y == content.y);
        assert(p.contentClip.w == content.w && p.contentClip.h <= content.h);
        assert(p.contentClip.y+p.contentClip.h < p.frame.y+p.frame.h);
    }
    return p;
}

static void WindowAnimations(void)
{
    UiDesktop d = {0};
    WindowState back = {0}, front = {0};
    UiRect target = {40,30,300,220};
    UiWindow w = {.rect=target,.input=WindowInput,.acceptsText=true,.onClose=WindowClose,.state=&back};
    UiDesktop_SetArea(&d,(UiRect){0,0,600,400});
    UiWindowId first = UiDesktop_Add(&d,w);
    UiDesktop_Update(&d,0.05);
    w.state = &front;
    UiWindowId second = UiDesktop_Add(&d,w);
    assert(!d.focus && d.pendingFocus == second && UiDesktop_HasActiveAnimations(&d));
    assert(UiDesktop_Find(&d,second)->motion == UI_WINDOW_OPENING);
    UiWindowPresentation p = Presentation(&d,second);
    assert(p.linesOnly && p.frame.w <= 8 && p.frame.h == UI_TITLE_HEIGHT);
    assert(!p.contentClip.h);
    UiDesktop_Dispatch(&d,&(UiEvent){.type=UI_TEXT,.text="invisible"});
    UiDesktop_Press(&d,target.x+target.w/2,target.y);
    assert(!d.captured && !front.events[UI_TEXT] && !front.events[UI_PRESS]);

    for (int i=0; i<13; ++i) {
        UiDesktop_Update(&d,0.01);
        UiWindowPresentation next = Presentation(&d,second);
        assert(next.linesOnly && next.frame.w >= p.frame.w && next.frame.h == UI_TITLE_HEIGHT);
        assert(!next.contentClip.h);
        p = next;
    }
    UiDesktop_Update(&d,0.011);
    p = Presentation(&d,second);
    assert(!p.linesOnly && p.frame.w == target.w && !p.contentClip.h);
    for (int i=0; i<22; ++i) {
        UiDesktop_Update(&d,0.01);
        UiWindowPresentation next = Presentation(&d,second);
        assert(next.frame.w == target.w && next.frame.h >= p.frame.h);
        assert(next.contentClip.h >= p.contentClip.h);
        const UiRect placed = UiDesktop_Find(&d,second)->rect;
        assert(placed.x == target.x && placed.y == target.y && placed.w == target.w && placed.h == target.h);
        p = next;
    }
    assert(p.frame.h == target.h && p.contentClip.h == UiWindow_Content(target).h);
    assert(!UiDesktop_HasActiveAnimations(&d) && d.focus == second);
    UiDesktop_Press(&d,target.x+10,target.y+10);
    assert(d.captured == second);
    assert(UiDesktop_Close(&d,second) && UiDesktop_Close(&d,second));
    assert(d.focus == first && !d.captured && front.events[UI_BLUR] == 1 && !front.closes);
    assert(d.windows[d.count-1].id == second);
    UiDesktop_Cycle(&d,false);
    assert(d.focus == first);
    UiDesktop_Activate(&d,0);
    UiDesktop_Press(&d,target.x+50,target.y+80);
    assert(!d.focus && !back.events[UI_PRESS] && !front.events[UI_PRESS]);
    UiDesktop_Activate(&d,first);
    UiDesktop_Dispatch(&d,&(UiEvent){.type=UI_PASTE,.text="still editing"});
    assert(back.events[UI_PASTE] == 1 && !front.events[UI_PASTE]);

    for (int i=0; i<17; ++i) {
        UiDesktop_Update(&d,0.01);
        UiWindowPresentation next = Presentation(&d,second);
        assert(!next.linesOnly && next.frame.w == target.w && next.frame.h <= p.frame.h);
        assert(next.contentClip.h <= p.contentClip.h && !front.closes);
        p = next;
    }
    UiDesktop_Update(&d,0.02);
    p = Presentation(&d,second);
    assert(p.linesOnly && p.frame.w < target.w && !p.contentClip.h);
    UiDesktop_Update(&d,0.10);
    assert(Presentation(&d,second).frame.w < p.frame.w && !front.closes);
    UiDesktop_Update(&d,0.02);
    assert(!UiDesktop_Find(&d,second) && front.closes == 1 && d.focus == first);
    assert(!UiDesktop_Close(&d,second) && !UiDesktop_HasActiveAnimations(&d));
    UiDesktop_CloseAll(&d);
    assert(!d.count && !d.focus && back.closes == 1 && front.closes == 1);
}

static void InterruptedAnimations(void)
{
    UiDesktop d = {0};
    WindowState state = {0};
    UiWindow w = {.rect={38,25,180,140},.onClose=WindowClose,.state=&state};
    UiDesktop_SetArea(&d,(UiRect){0,0,600,400});
    for (int i=0; i<6; ++i) {
        UiWindowId id = UiDesktop_Add(&d,w);
        UiDesktop_Update(&d,i%2 ? 0.22 : 0.07);
        UiWindowPresentation before = Presentation(&d,id);
        assert(UiDesktop_Close(&d,id) && !d.pendingFocus);
        UiWindowPresentation after = Presentation(&d,id);
        assert(before.frame.w == after.frame.w && before.frame.h == after.frame.h);
        UiDesktop_Update(&d,0.025);
        before = Presentation(&d,id);
        UiDesktop_Activate(&d,id);
        after = Presentation(&d,id);
        assert(before.frame.w == after.frame.w && before.frame.h == after.frame.h);
        assert(d.count == 1 && state.closes == i);
        UiDesktop_Update(&d,0.4);
        assert(d.focus == id && UiWindow_IsInteractive(UiDesktop_Find(&d,id)));
        assert(UiDesktop_Close(&d,id));
        UiDesktop_Update(&d,0.4);
        assert(!d.count && state.closes == i+1);
    }

    UiWindowId tiny = UiDesktop_Add(&d,w);
    UiDesktop_Update(&d,0.08);
    UiDesktop_SetArea(&d,(UiRect){1,2,5,3});
    assert(Presentation(&d,tiny).frame.h == 3);
    UiDesktop_Update(&d,0.15);
    UiWindowPresentation p = Presentation(&d,tiny);
    assert(p.frame.w == 5 && p.frame.h == 3 && !p.contentClip.h && !p.contentClip.w);
    UiDesktop_Hide(&d,tiny);
    assert(!UiDesktop_HasActiveAnimations(&d) && !d.pendingFocus);
    UiDesktop_Activate(&d,tiny);
    assert(UiWindow_IsInteractive(UiDesktop_Find(&d,tiny)) && d.focus == tiny);
    UiDesktop_Hide(&d,tiny);
    assert(UiDesktop_Close(&d,tiny) && !d.count && state.closes == 7);

    UiWindowId closing = UiDesktop_Add(&d,w);
    UiDesktop_Update(&d,0.1);
    assert(UiDesktop_Close(&d,closing));
    assert(UiDesktop_Add(&d,w) && UiDesktop_HasActiveAnimations(&d));
    UiDesktop_CloseAll(&d);
    assert(!d.count && !d.focus && !d.pendingFocus && !UiDesktop_HasActiveAnimations(&d));
    assert(state.closes == 9);

    UiWindowId remaining = UiDesktop_Add(&d,w);
    UiWindowId interrupted = UiDesktop_Add(&d,w);
    assert(d.pendingFocus == interrupted && UiDesktop_Close(&d,interrupted));
    assert(d.pendingFocus == remaining);
    UiDesktop_Update(&d,0.4);
    assert(d.focus == remaining && state.closes == 10);
    UiDesktop_CloseAll(&d);
    assert(state.closes == 11);
}

static void FixturePath(char *path, size_t size, const char *from, const char *filename)
{
    int bytes = snprintf(path,size,"%s/%s",from,filename);
    assert(bytes > 0 && (size_t)bytes < size);
}

static void Fixture(const char *directory, const char *name, const char *contents)
{
    char path[512];
    FixturePath(path,sizeof(path),directory,name);
    FILE *file = fopen(path,"wb");
    assert(file && fputs(contents,file) >= 0 && fclose(file) == 0);
}

static UiVfsId Recipes(UiVfs *fs)
{
    return UiVfs_Add(fs,UiVfs_Root(fs),"recepies",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
}

static void RecipeCatalog(void)
{
    char directory[] = "/tmp/nexus-catalog-XXXXXX";
    assert(mkdtemp(directory));
    Fixture(directory,"catalog.txt","# fixture\r\nrenamed.creature.json\r\n"
        "cerdo.creature.json\nrenamed.creature.json\nsecond.creature.json\n"
        "../external.creature.json\npath/thing.creature.json\nmissing.creature.json\n"
        "bad.creature.json\nlinked.creature.json\n");
    Fixture(directory,"renamed.creature.json",
        "{\"recipe\":{\"metadata\":[null,true,false,-0.2e+4,{\"id\":9}],\"name\":\"Renamed\",\"id\":3},\"version\":1}");
    Fixture(directory,"cerdo.creature.json","{\"version\":1,\"recipe\":{\"id\":1007,\"name\":\"Cerdo\"}}");
    Fixture(directory,"second.creature.json","{\"version\":1,\"recipe\":{\"id\":3,\"name\":\"Duplicate\"}}");
    Fixture(directory,"bad.creature.json","{\"version\":1,\"recipe\":{\"id\":2,\"name\":\"Malformed\",}}");
    char link[512];
    FixturePath(link,sizeof(link),directory,"linked.creature.json");
    assert(symlink("renamed.creature.json",link) == 0);

    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId folder = Recipes(fs);
    assert(folder);
    UiRecipeCatalogReport report = UiRecipeCatalog_Load(fs,folder,directory);
    assert(report.manifestAvailable && report.external == 2 && report.builtin == 3);
    assert(report.rejected == 5 && report.duplicates == 2);
    const char *expected[] = {"renamed.creature.json","cerdo.creature.json","Lagarto.recipe",
                              "Perro.recipe","Velkar.recipe"};
    UiVfsId id = UiVfs_FirstChild(fs,folder);
    for (size_t i=0; i<sizeof(expected)/sizeof(expected[0]); ++i) {
        const UiVfsNode *node = UiVfs_Get(fs,id);
        assert(node && node->type == UI_VFS_FILE && node->parent == folder && node->readOnly);
        assert(!strcmp(node->name,expected[i]));
        assert(node->source == (i<2 ? UI_VFS_EXTERNAL_RECIPE : UI_VFS_BUILTIN_RECIPE));
        assert(i || node->recipeId == 3);
        id = UiVfs_NextSibling(fs,id);
    }
    assert(!id && !UiVfs_FindChild(fs,folder,"Brumacerno.recipe"));
    UiVfs_Destroy(fs);

    fs = UiVfs_Create();
    folder = Recipes(fs);
    report = UiRecipeCatalog_LoadProject(fs,folder,directory);
    assert(!report.manifestAvailable && report.builtin == 4);
    UiVfs_Destroy(fs);

    const char *cleanup[] = {"catalog.txt","renamed.creature.json","cerdo.creature.json",
                             "second.creature.json","bad.creature.json","linked.creature.json"};
    for (size_t i=0; i<sizeof(cleanup)/sizeof(cleanup[0]); ++i) {
        char path[512];
        FixturePath(path,sizeof(path),directory,cleanup[i]);
        assert(unlink(path) == 0);
    }
    fs = UiVfs_Create();
    folder = Recipes(fs);
    report = UiRecipeCatalog_Load(fs,folder,directory);
    assert(!report.manifestAvailable && !report.external && report.builtin == 4);
    assert(UiVfs_FindChild(fs,folder,"Brumacerno.recipe"));
    UiVfs_Destroy(fs);
    assert(rmdir(directory) == 0);
}

static void Resolve(const UiVfs *fs, UiVfsId base, const char *path,
                    UiVfsResult expected, UiVfsId target)
{
    UiVfsId found = 9999;
    assert(UiVfs_Resolve(fs,base,path,&found) == expected && found == target);
}

static void SubmitLine(UiTerminal *terminal, const char *line)
{
    UiTerminal_Insert(terminal,line);
    UiTerminal_Key(terminal,UI_ENTER);
    assert(!terminal->line[0]);
}

static const UiLogEntry *Log(const UiTerminal *terminal, int i)
{
    return &terminal->log[(terminal->logStart+i)%UI_LOG_LIMIT];
}

static void ShellAndFiles(void)
{
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId root = UiVfs_Root(fs), folder = Recipes(fs);
    UiVfsId file = UiVfs_Add(fs,folder,"cerdo.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,1007);
    UiVfsId sub = UiVfs_Add(fs,folder,"folder",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0);
    assert(root && folder && file && sub);
    const UiVfsNode *top = UiVfs_Get(fs,root);
    assert(top->parent == root && top->type == UI_VFS_DIRECTORY && top->readOnly);
    assert(!UiVfs_Add(fs,file,"child",UI_VFS_FILE,UI_VFS_VIRTUAL,0));
    assert(!UiVfs_Add(fs,root,"recepies",UI_VFS_DIRECTORY,UI_VFS_VIRTUAL,0));
    assert(!UiVfs_Add(fs,root,"../escape",UI_VFS_FILE,UI_VFS_VIRTUAL,0));
    Resolve(fs,root,"/",UI_VFS_OK,root);
    Resolve(fs,root,"recepies",UI_VFS_OK,folder);
    Resolve(fs,root,"./recepies",UI_VFS_OK,folder);
    Resolve(fs,folder,"/recepies",UI_VFS_OK,folder);
    Resolve(fs,folder,".",UI_VFS_OK,folder);
    Resolve(fs,folder,"../recepies",UI_VFS_OK,folder);
    Resolve(fs,folder,"../../../../",UI_VFS_OK,root);
    Resolve(fs,folder,"//recepies///folder/..",UI_VFS_OK,folder);
    Resolve(fs,folder,"notfound",UI_VFS_MISSING,0);
    Resolve(fs,root,"/etc/passwd",UI_VFS_MISSING,0);
    Resolve(fs,folder,"cerdo.creature.json",UI_VFS_OK,file);
    Resolve(fs,folder,"cerdo.creature.json/..",UI_VFS_NOT_DIRECTORY,0);
    Resolve(fs,folder,"cerdo.creature.json/",UI_VFS_NOT_DIRECTORY,0);
    char path[UI_VFS_PATH_BYTES];
    assert(UiVfs_Path(fs,file,path,sizeof(path)) && !strcmp(path,"/recepies/cerdo.creature.json"));
    assert(!UiVfs_Path(fs,file,path,3));

    UiShell shell;
    UiShell_Init(&shell,fs);
    UiCommands commands = {0};
    assert(UiShell_Register(&shell,&commands));
    assert(!UiShell_Register(&shell,&commands));
    assert(UiCommands_Register(&commands,"print",UiCommands_Print,NULL));
    Expect(&commands,"pwd",UI_COMMAND_OK,"/");
    Expect(&commands,"ls",UI_COMMAND_OK,"recepies/");
    Expect(&commands,"cd recepies",UI_COMMAND_SILENT,"");
    Expect(&commands,"pwd",UI_COMMAND_OK,"/recepies");
    Expect(&commands,"ls .",UI_COMMAND_OK,"cerdo.creature.json\nfolder/");
    Expect(&commands,"ls /recepies",UI_COMMAND_OK,"cerdo.creature.json\nfolder/");
    Expect(&commands,"ls -l",UI_COMMAND_ERROR,"ls: -l: Options are not supported");
    Expect(&commands,"ls cerdo.creature.json",UI_COMMAND_ERROR,"ls: cerdo.creature.json: Not a directory");
    Expect(&commands,"cd nonexistent",UI_COMMAND_ERROR,"cd: nonexistent: No such directory");
    Expect(&commands,"cd cerdo.creature.json",UI_COMMAND_ERROR,"cd: cerdo.creature.json: Not a directory");
    assert(shell.cwd == folder);
    Expect(&commands,"cd ..",UI_COMMAND_SILENT,"");
    Expect(&commands,"cd ..",UI_COMMAND_SILENT,"");
    Expect(&commands,"pwd",UI_COMMAND_OK,"/");
    Expect(&commands,"cd /recepies",UI_COMMAND_SILENT,"");
    Expect(&commands,"cd",UI_COMMAND_SILENT,"");
    assert(shell.cwd == root);

    UiDesktop desktop = {0};
    UiDesktop_SetArea(&desktop,(UiRect){0,0,600,400});
    WindowState terminalWindow = {0};
    UiWindowId terminalId = UiDesktop_Add(&desktop,(UiWindow){
        .rect={250,30,300,200},.acceptsText=true,.input=WindowInput,.state=&terminalWindow
    });
    UiDesktop_Update(&desktop,0.4);
    UiFilesApp files;
    UiFilesApp_Init(&files,fs);
    UiFilesLaunch launch = {.files=&files,.desktop=&desktop,
        .presentation={.rect={20,10,400,250},.minWidth=300,.minHeight=210,.title="Files"}};
    assert(UiCommands_Register(&commands,"Files",UiFilesApp_Command,&launch));
    Expect(&commands,"Files x",UI_COMMAND_ERROR,"Files: Expected no arguments");
    UiTerminal terminal;
    UiTerminal_Init(&terminal,&commands);
    UiTerminal_Layout(&terminal,50,12);
    SubmitLine(&terminal,"cd /recepies");
    assert(terminal.logCount == 1 && shell.cwd == folder);
    SubmitLine(&terminal,"ls");
    assert(terminal.logCount == 4 && terminal.totalRows == 4);
    assert(Log(&terminal,2)->kind == UI_LOG_OUTPUT && !strcmp(Log(&terminal,2)->text,"cerdo.creature.json"));
    assert(!strcmp(Log(&terminal,3)->text,"folder/"));
    SubmitLine(&terminal,"Files");
    UiWindowId filesId = files.window;
    assert(filesId && desktop.count == 2 && desktop.focus == terminalId);
    UiDesktop_Update(&desktop,0.4);
    assert(desktop.focus == filesId);
    assert(terminal.logCount == 5 && !strcmp(terminal.history[2],"Files"));
    assert(terminalWindow.events[UI_BLUR] == 1);
    assert(files.directory == root && files.fs == shell.fs && shell.cwd == folder);
    assert(UiFilesApp_Item(&files,0) == folder);
    UiFilesApp_Select(&files,folder);
    UiFilesApp_Activate(&files);
    assert(files.directory == folder && UiFilesApp_Count(&files) == 2);
    assert(UiFilesApp_Item(&files,0) == file && UiFilesApp_Item(&files,1) == sub);
    UiFilesApp_Select(&files,file);
    UiFilesApp_Activate(&files);
    assert(files.activationError[0] && files.directory == folder);
    UiFilesApp_MoveSelection(&files,1,1);
    assert(files.selected == sub && files.firstRow == 1 && !files.activationError[0]);
    assert(UiFilesApp_Up(&files) && files.directory == root);
    assert(UiFilesApp_Back(&files) && files.directory == folder);
    assert(!UiFilesApp_Go(&files,file) && shell.cwd == folder);
    Expect(&commands,"Files",UI_COMMAND_SILENT,"");
    assert(files.window == filesId && desktop.count == 2);
    UiDesktop_Hide(&desktop,filesId);
    assert(desktop.focus == terminalId && UiDesktop_Find(&desktop,filesId)->hidden);
    Expect(&commands,"Files",UI_COMMAND_SILENT,"");
    assert(files.window == filesId && desktop.focus == filesId && !UiDesktop_Find(&desktop,filesId)->hidden);
    UiDesktop_Cycle(&desktop,false);
    assert(desktop.focus == terminalId && files.directory == folder);
    SubmitLine(&terminal,"print Hello world");
    assert(terminal.logCount == 7 && !strcmp(Log(&terminal,6)->text,"Hello world"));
    SubmitLine(&terminal,"print");
    assert(Log(&terminal,8)->kind == UI_LOG_OUTPUT && !Log(&terminal,8)->text[0]);
    UiDesktop_SetArea(&desktop,(UiRect){0,0,340,225});
    const UiWindow *window = UiDesktop_Find(&desktop,filesId);
    assert(window->rect.w == 340 && window->rect.h == 225);
    UiRect content = UiWindow_Content(window->rect);
    assert(content.w < window->rect.w && content.h < window->rect.h);

    /* Un listado mayor que el búfer de respuesta mantiene una entrada por archivo. */
    for (int i=0; i<100; ++i) {
        char name[64];
        snprintf(name,sizeof(name),"catalog-test-fixture-%03d.creature.json",i);
        assert(UiVfs_Add(fs,folder,name,UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,2000+i));
    }
    int before = terminal.logCount;
    SubmitLine(&terminal,"ls");
    assert(terminal.logCount == before+103);
    int viewStart = UiTerminal_FirstRow(&terminal);
    UiTerminal_Scroll(&terminal,8);
    assert(!terminal.following && UiTerminal_FirstRow(&terminal) == viewStart-8);
    UiTerminal_Layout(&terminal,12,8);
    assert(!terminal.following && terminal.totalRows > terminal.logCount);
    assert(!strcmp(Log(&terminal,terminal.logCount-1)->text,"catalog-test-fixture-099.creature.json"));

    assert(UiCommands_Register(&commands,"Files.close",UiFilesApp_CloseCommand,&launch));
    UiDesktop_Hide(&desktop,filesId);
    Expect(&commands,"Files.close",UI_COMMAND_SILENT,"");
    assert(!files.window && files.directory == root && desktop.focus == terminalId && desktop.count == 1);
    Expect(&commands,"Files.close",UI_COMMAND_OK,"Files is already closed.");
    Expect(&commands,"Files",UI_COMMAND_SILENT,"");
    assert(files.window && files.window != filesId && files.directory == root);
    UiDesktop_Update(&desktop,0.4);
    UiRect close = UiWindow_CloseButton(UiDesktop_Find(&desktop,files.window)->rect);
    UiDesktop_Press(&desktop,close.x+1,close.y+1);
    assert(files.window && UiDesktop_Find(&desktop,files.window)->motion == UI_WINDOW_CLOSING);
    UiDesktop_Update(&desktop,0.4);
    assert(!files.window && !desktop.captured && desktop.focus == terminalId);
    assert(!UiDesktop_Close(&desktop,terminalId));
    Expect(&commands,"Files",UI_COMMAND_SILENT,"");
    UiWindowId reopened = files.window;
    UiDesktop_Update(&desktop,0.4);
    const UiWindow *pressed = UiDesktop_Find(&desktop,reopened);
    UiDesktop_Press(&desktop,pressed->rect.x+8,pressed->rect.y+8);
    assert(desktop.captured == reopened);
    assert(UiDesktop_Close(&desktop,reopened));
    assert(!desktop.captured && files.window == reopened && desktop.focus == terminalId);
    UiDesktop_Update(&desktop,0.4);
    assert(!files.window);
    while (desktop.count < UI_MAX_WINDOWS) assert(UiDesktop_Add(&desktop,launch.presentation));
    Expect(&commands,"Files",UI_COMMAND_ERROR,"Files: Desktop window limit reached");
    UiVfs_Destroy(fs);
}

static void RecipeRoutes(void)
{
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId root = UiVfs_Root(fs), folder = Recipes(fs);
    UiVfsId dog = UiVfs_Add(fs,folder,"Perro.recipe",UI_VFS_FILE,UI_VFS_BUILTIN_RECIPE,2);
    UiVfsId elephant = UiVfs_Add(fs,folder,"elefante.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,1008);
    UiVfsId pig = UiVfs_Add(fs,folder,"cerdo.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,1007);
    const UiVfsNode *a, *b;
    assert(!UiRecipeRoutes_Resolve(fs,root,"\"/recepies/Perro.recipe\"",false,&a,&b));
    assert(a->id == dog && !b);
    assert(!UiRecipeRoutes_Resolve(fs,folder,"\"./elefante.creature.json\"",false,&a,&b));
    assert(a->id == elephant);
    assert(!UiRecipeRoutes_Resolve(fs,root,
        "\"/recepies/cerdo.creature.json\".\"/recepies/elefante.creature.json\"",true,&a,&b));
    assert(a->id == pig && b->id == elephant);
    assert(!UiRecipeRoutes_Resolve(fs,folder,"\"cerdo.creature.json\".\"Perro.recipe\"",true,&a,&b));
    assert(a->id == pig && b->id == dog);
    static const struct { const char *route, *error; bool pair; } invalid[] = {
        {"", "Expected a double-quoted recipe path", false},
        {"/recepies/Perro.recipe", "Expected a double-quoted recipe path", false},
        {"cerdo.creature.json.Perro.recipe", "Expected a double-quoted recipe path", true},
        {"\"/recepies/Perro.recipe", "Missing closing double quote in recipe path", false},
        {"\"\"", "Recipe path must not be empty", false},
        {"\"Perro.recipe\"junk", "Unexpected text after recipe path", false},
        {"\"Perro.recipe\"", "Expected a period between two double-quoted recipe paths", true},
        {"\"Perro.recipe\"\"cerdo.creature.json\"", "Expected a period between two double-quoted recipe paths", true},
        {"\"Perro.recipe\".", "Expected a double-quoted recipe path", true},
        {"\"Perro.recipe\".\"cerdo.creature.json", "Missing closing double quote in recipe path", true},
        {"\"Perro.recipe\".\"\"", "Recipe path must not be empty", true},
        {"\"Perro.recipe\".\"cerdo.creature.json\"junk", "Unexpected text after recipe path", true},
        {"\"Perro.recipe\".\"missing.recipe\"", "Second recipe not found", true},
        {"\"/recepies\"", "Expected a recipe file", false},
        {"\"/etc/passwd\"", "Recipe not found", false},
        {"\"../../etc/passwd\"", "Recipe not found", false}
    };
    for (size_t i=0; i<sizeof(invalid)/sizeof(*invalid); ++i) {
        const char *error = UiRecipeRoutes_Resolve(fs,folder,invalid[i].route,invalid[i].pair,&a,&b);
        assert(error && !strcmp(error,invalid[i].error) && !a && !b);
    }

    char oversized[UI_VFS_PATH_BYTES+3];
    memset(oversized,'a',sizeof(oversized));
    oversized[0] = oversized[sizeof(oversized)-2] = '"';
    oversized[sizeof(oversized)-1] = '\0';
    assert(!strcmp(UiRecipeRoutes_Resolve(fs,folder,oversized,false,&a,&b),"Recipe path too long"));
    for (const char **name=(const char *[]){"a","b.c","a.b","c",NULL}; *name; ++name)
        assert(UiVfs_Add(fs,folder,*name,UI_VFS_FILE,UI_VFS_BUILTIN_RECIPE,1));
    assert(!UiRecipeRoutes_Resolve(fs,folder,"\"a\".\"b.c\"",true,&a,&b) && !strcmp(b->name,"b.c"));
    assert(!UiRecipeRoutes_Resolve(fs,folder,"\"a.b\".\"c\"",true,&a,&b) && !strcmp(a->name,"a.b"));
    UiVfs_Destroy(fs);

    UiCommands commands = {0};
    int calls = 0;
    assert(UiCommands_RegisterPrefix(&commands,"CreatureVisualizer.view.",Custom,&calls));
    assert(UiCommands_RegisterPrefix(&commands,"CreatureVisualizer.view.interpolation.",Custom,&calls));
    assert(UiCommands_Register(&commands,"CreatureVisualizer.close",Custom,&calls));
    assert(UiCommands_Register(&commands,"print",UiCommands_Print,NULL));
    Expect(&commands,"CreatureVisualizer.view./recepies/Perro.recipe",UI_COMMAND_OK,"[/recepies/Perro.recipe]");
    Expect(&commands,"CreatureVisualizer.view.interpolation.abc.def",UI_COMMAND_OK,"[abc.def]");
    Expect(&commands,"CreatureVisualizer.view.",UI_COMMAND_OK,"[]");
    Expect(&commands,"CreatureVisualizer.close",UI_COMMAND_OK,"[]");
    Expect(&commands,"print CreatureVisualizer.view.xyz",UI_COMMAND_OK,"CreatureVisualizer.view.xyz");
    assert(calls == 4);
}

typedef struct {
    UiCreatureVisualizer *visualizer;
    UiVfsId recipe;
    int calls;
} RecipeActivation;

static bool OpenRecipe(void *state, UiVfsId id, char *error, size_t capacity)
{
    RecipeActivation *activation = state;
    activation->recipe = id;
    ++activation->calls;
    return UiCreatureVisualizer_ViewRecipe(activation->visualizer,id,error,capacity);
}

static void VisualizerAndFiles(void)
{
    UiVfs *fs = UiVfs_Create();
    assert(fs);
    UiVfsId folder = Recipes(fs);
    UiVfsId pig = UiVfs_Add(fs,folder,"cerdo.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,1007);
    UiVfsId elephant = UiVfs_Add(fs,folder,"elefante.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,1008);
    UiVfsId broken = UiVfs_Add(fs,folder,"broken.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,9999);
    UiVfsId note = UiVfs_Add(fs,folder,"notes.txt",UI_VFS_FILE,UI_VFS_VIRTUAL,0);
    assert(pig && elephant && broken && note);
    UiVfsId dog = UiVfs_Add(fs,folder,"Perro.recipe",UI_VFS_FILE,UI_VFS_BUILTIN_RECIPE,2);
    UiVfsId rhino = UiVfs_Add(fs,folder,"rinoceronte.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,1009);
    UiVfsId rat = UiVfs_Add(fs,folder,"rata.creature.json",UI_VFS_FILE,UI_VFS_EXTERNAL_RECIPE,1010);
    assert(dog && rhino && rat);
    UiShell shell;
    UiShell_Init(&shell,fs);
    UiDesktop desktop = {0};
    UiDesktop_SetArea(&desktop,WorkspaceRect(1280,720));
    UiCreatureVisualizer cv = {.desktop=&desktop,.shell=&shell};
    UiCommands commands = {0};
    assert(UiCreatureVisualizer_Register(&cv,&commands));
    assert(!UiCreatureVisualizer_Register(&cv,&commands));
    assert(UiShell_Register(&shell,&commands));

    Expect(&commands,"CV",UI_COMMAND_SILENT,"");
    UiWindowId id = cv.window;
    assert(id && !desktop.focus && desktop.pendingFocus == id && desktop.count == 1 && !cv.engine);
    UiDesktop_Hide(&desktop,id);
    Expect(&commands,"CreatureVisualizer",UI_COMMAND_SILENT,"");
    assert(cv.window == id && !UiDesktop_Find(&desktop,id)->hidden && desktop.focus == id);
    Expect(&commands,"CV.view.\"/recepies/Perro.recipe\"",UI_COMMAND_SILENT,"");
    UiCreatureEngine *engine = cv.engine;
    assert(engine && cv.window == id && desktop.count == 1);
    InterfaceTest_CheckRecipe(engine,dog,0);
    Expect(&commands,"cd recepies",UI_COMMAND_SILENT,"");
    Expect(&commands,"CreatureVisualizer.view.\"cerdo.creature.json\"",UI_COMMAND_SILENT,"");
    assert(cv.engine == engine);
    InterfaceTest_CheckRecipe(engine,pig,0);
    Expect(&commands,"CV.view.interpolation.\"/recepies/cerdo.creature.json\".\"/recepies/elefante.creature.json\"",
        UI_COMMAND_SILENT,"");
    InterfaceTest_CheckRecipe(engine,pig,elephant);
    Expect(&commands,"CreatureVisualizer.view.interpolation.\"elefante.creature.json\".\"Perro.recipe\"",
        UI_COMMAND_SILENT,"");
    InterfaceTest_CheckRecipe(engine,elephant,dog);
    assert(cv.window == id && cv.engine == engine && desktop.count == 1);
    Expect(&commands,"CV.view./recepies/Perro.recipe",UI_COMMAND_ERROR,
        "ERROR: Expected a double-quoted recipe path");
    Expect(&commands,"CreatureVisualizer.view.interpolation.cerdo.creature.json.Perro.recipe",UI_COMMAND_ERROR,
        "ERROR: Expected a double-quoted recipe path");
    Expect(&commands,"CV.close",UI_COMMAND_SILENT,"");
    assert(cv.window == id && cv.engine == engine && !UiCreatureVisualizer_NeedsFrame(&cv));
    Expect(&commands,"CV.close",UI_COMMAND_SILENT,"");
    UiDesktop_Update(&desktop,0.10);
    Expect(&commands,"CV",UI_COMMAND_SILENT,"");
    assert(cv.window == id && cv.engine == engine);
    UiDesktop_Update(&desktop,0.4);
    Expect(&commands,"CV.close",UI_COMMAND_SILENT,"");
    UiDesktop_Update(&desktop,0.4);
    assert(!cv.window && !cv.engine && !desktop.count);

    UiFilesApp files;
    UiFilesApp_Init(&files,fs);
    RecipeActivation activation = {.visualizer=&cv};
    files.openRecipe = OpenRecipe;
    files.recipeContext = &activation;
    UiFilesLaunch launch = {.files=&files,.desktop=&desktop,
        .presentation={.rect={600,100,480,405},.input=UiFilesView_Input}};
    assert(UiFilesApp_Open(&launch));
    UiWindowId filesId = files.window;
    UiDesktop_Update(&desktop,0.4);
    UiRect content = UiWindow_Content(UiDesktop_Find(&desktop,filesId)->rect);
    int x = content.x+150, firstRow = content.y+80;
    UiDesktop_PressWithClicks(&desktop,x,firstRow,1);
    assert(files.selected == folder && files.directory == UiVfs_Root(fs) && !activation.calls);
    UiDesktop_PressWithClicks(&desktop,x,firstRow,2);
    assert(files.directory == folder && !activation.calls);
    UiDesktop_PressWithClicks(&desktop,x,firstRow,1);
    assert(files.selected == pig && !activation.calls && !cv.window);
    UiDesktop_PressWithClicks(&desktop,x,firstRow+25,2);
    assert(files.selected == elephant && !activation.calls && !cv.window);
    UiDesktop_PressWithClicks(&desktop,x,firstRow+25,2);
    assert(activation.recipe == elephant && activation.calls == 1 && desktop.focus == filesId);
    assert(!UiCreatureVisualizer_NeedsFrame(&cv));
    UiCreatureVisualizer_Prepass(&cv,1280,720,2560,1440,0.016);
    InterfaceTest_CheckViewport(cv.engine,0,0,0);
    UiDesktop_Update(&desktop,0.4);
    assert(desktop.focus == cv.window && UiCreatureVisualizer_NeedsFrame(&cv));
    UiCreatureVisualizer_Prepass(&cv,1280,720,2560,1440,0.016);
    InterfaceTest_CheckViewport(cv.engine,0,0,0);
    UiCreatureVisualizer_Prepass(&cv,1280,720,2560,1440,0.016);
    InterfaceTest_CheckViewport(cv.engine,1,952,724);
    assert(files.window == filesId && !UiDesktop_Find(&desktop,filesId)->hidden && desktop.count == 2);

    id = cv.window;
    engine = cv.engine;
    UiDesktop_Hide(&desktop,id);
    UiDesktop_PressWithClicks(&desktop,x,firstRow,1);
    UiDesktop_Dispatch(&desktop,&(UiEvent){.type=UI_KEY,.key=UI_ENTER});
    assert(activation.recipe == pig && activation.calls == 2 && !files.activationError[0]);
    assert(cv.window == id && cv.engine == engine && desktop.focus == id && desktop.count == 2);
    assert(!UiDesktop_Find(&desktop,id)->hidden);
    InterfaceTest_CheckRecipe(engine,pig,0);

    UiDesktop_PressWithClicks(&desktop,x,firstRow+50,1);
    UiDesktop_PressWithClicks(&desktop,x,firstRow+50,2);
    assert(activation.recipe == broken && activation.calls == 3 && files.selected == broken);
    assert(!strcmp(files.activationError,"ERROR: Test recipe failed to load"));
    assert(desktop.focus == filesId && cv.engine == engine);
    InterfaceTest_CheckRecipe(engine,pig,0);
    UiDesktop_PressWithClicks(&desktop,x,firstRow+75,1);
    UiDesktop_PressWithClicks(&desktop,x,firstRow+75,2);
    assert(activation.calls == 3 && files.selected == note && files.activationError[0]);

    const UiVfsId wildlife[]={rhino,rat};
    for(int i=0;i<2;++i) {
        UiDesktop_Activate(&desktop,filesId);
        UiDesktop_PressWithClicks(&desktop,x,firstRow+125+i*25,1);
        UiDesktop_PressWithClicks(&desktop,x,firstRow+125+i*25,2);
        assert(activation.recipe==wildlife[i] && activation.calls==4+i);
        assert(desktop.focus==cv.window && cv.engine==engine);
        InterfaceTest_CheckRecipe(engine,wildlife[i],0);
    }

    Expect(&commands,"CreatureVisualizer.close",UI_COMMAND_SILENT,"");
    UiDesktop_Update(&desktop,0.1);
    UiCreatureVisualizer_Prepass(&cv,1280,720,2560,1440,0.016);
    InterfaceTest_CheckViewport(cv.engine,1,952,724);
    UiDesktop_Update(&desktop,0.4);
    assert(!cv.window && !cv.engine && desktop.count == 1 && desktop.focus == filesId);
    assert(UiDesktop_Close(&desktop,filesId));
    UiDesktop_Update(&desktop,0.4);
    assert(UiFilesApp_Open(&launch));
    assert(files.directory == UiVfs_Root(fs) && files.openRecipe == OpenRecipe && files.recipeContext == &activation);
    UiVfs_Destroy(fs);
}

static void Clear(void)
{
    UiVfs *fs = UiVfs_Create();
    assert(fs && Recipes(fs));
    UiShell shell;
    UiShell_Init(&shell,fs);
    UiCommands commands = {0};
    UiTerminal terminal;
    UiTerminal_Init(&terminal,&commands);
    terminal.location = UiShell_Location;
    terminal.locationState = &shell;
    assert(UiShell_Register(&shell,&commands));
    assert(UiCommands_Register(&commands,"clear",UiTerminal_ClearCommand,&terminal));
    UiTerminal_Layout(&terminal,12,5);
    SubmitLine(&terminal,"cd recepies");
    for (int i=0; i<UI_LOG_LIMIT+5; ++i)
        UiTerminal_Append(&terminal,UI_LOG_OUTPUT,"Enough text to wrap an entry");
    UiTerminal_Scroll(&terminal,11);
    assert(!terminal.following && terminal.logStart && terminal.anchorEntry && terminal.anchorByte);
    SubmitLine(&terminal,"clear unexpected");
    assert(terminal.logCount == UI_LOG_LIMIT);
    assert(Log(&terminal,terminal.logCount-1)->kind == UI_LOG_ERROR);
    assert(!strcmp(Log(&terminal,terminal.logCount-1)->text,"clear: Expected no arguments"));

    SubmitLine(&terminal,"clear");
    assert(!terminal.logStart && !terminal.logCount && !terminal.totalRows &&
        !terminal.anchorEntry && !terminal.anchorByte && terminal.following && !terminal.cursor);
    assert(!UiTerminal_FirstRow(&terminal));
    RowCapture rows = {0};
    UiTerminal_VisitRows(&terminal,CaptureRow,&rows);
    assert(!rows.count);
    char location[UI_VFS_PATH_BYTES];
    assert(terminal.location(terminal.locationState,location,sizeof(location)));
    assert(!strcmp(location,"/recepies"));
    UiTerminal_Key(&terminal,UI_UP);
    assert(!strcmp(terminal.line,"clear"));
    UiTerminal_Key(&terminal,UI_UP);
    assert(!strcmp(terminal.line,"clear unexpected"));
    UiTerminal_Key(&terminal,UI_DOWN);
    UiTerminal_Key(&terminal,UI_DOWN);
    assert(!terminal.line[0]);
    SubmitLine(&terminal,"pwd");
    assert(terminal.logCount == 2 && !strcmp(Log(&terminal,1)->text,"/recepies"));
    UiVfs_Destroy(fs);
}

static void ContentDrag(void)
{
    UiDesktop desktop = {0};
    WindowState back = {0}, front = {0};
    UiDesktop_SetArea(&desktop,(UiRect){0,0,800,600});
    UiWindowId other = UiDesktop_Add(&desktop,(UiWindow){
        .rect={400,30,250,190},.input=WindowInput,.state=&back
    });
    UiWindowId viewer = UiDesktop_Add(&desktop,(UiWindow){
        .rect={30,30,300,240},.input=WindowInput,.state=&front,
        .onClose=WindowClose,.capturesContentPointer=true
    });
    UiDesktop_Update(&desktop,.4);
    UiDesktop_Press(&desktop,130,120);
    assert(desktop.captured == viewer && desktop.contentDrag && front.events[UI_PRESS] == 1);
    UiDesktop_Move(&desktop,-100,610);
    assert(front.events[UI_POINTER_MOVE] == 1 && UiDesktop_Find(&desktop,viewer)->rect.x == 30);
    UiDesktop_Dispatch(&desktop,&(UiEvent){.type=UI_WHEEL,.x=450,.y=130,.wheel=1});
    assert(back.events[UI_WHEEL] == 1 && front.events[UI_WHEEL] == 0);
    UiDesktop_Release(&desktop);
    UiDesktop_Move(&desktop,180,155);
    assert(front.events[UI_POINTER_RELEASE] == 1 && front.events[UI_POINTER_MOVE] == 1);
    UiDesktop_Press(&desktop,130,120);
    UiDesktop_Activate(&desktop,other);
    assert(!desktop.captured && front.events[UI_POINTER_RELEASE] == 2);
    UiDesktop_Press(&desktop,130,120);
    UiDesktop_Hide(&desktop,viewer);
    assert(!desktop.captured && front.events[UI_POINTER_RELEASE] == 3);
    UiDesktop_Activate(&desktop,viewer);
    UiDesktop_Press(&desktop,130,120);
    assert(UiDesktop_Close(&desktop,viewer));
    assert(!desktop.captured && front.events[UI_POINTER_RELEASE] == 4);
    UiDesktop_Update(&desktop,.4);
    assert(front.closes == 1 && !UiDesktop_Find(&desktop,viewer));
}

static void Workspace(void)
{
    static const int sizes[][2] = {{1280,720},{1024,768},{800,600},{560,400}};
    static const int extents[][2] = {{1228,582},{972,630},{748,462},{508,262}};
    UiDesktop desktop = {0};
    for (size_t i=0; i<sizeof(sizes)/sizeof(*sizes); ++i) {
        int w = sizes[i][0], h = sizes[i][1];
        UiDesktop_SetArea(&desktop,WorkspaceRect(w,h));
        UiRect area = desktop.area, grid = WorkspaceInterior(area);
        assert(area.x == 26 && area.y == 68);
        assert(area.w == extents[i][0] && area.h == extents[i][1]);
        assert(grid.x == area.x+1 && grid.y == area.y+1);
        assert(grid.x+grid.w == area.x+area.w-1 && grid.y+grid.h == area.y+area.h-1);
        UiRect windowPx = UiRect_Drawable(area,w,h,w*3/2,h*3/2);
        UiRect gridPx = UiRect_Drawable(grid,w,h,w*3/2,h*3/2);
        assert(gridPx.x > windowPx.x && gridPx.y > windowPx.y);
        assert(gridPx.x+gridPx.w < windowPx.x+windowPx.w);
        assert(gridPx.y+gridPx.h < windowPx.y+windowPx.h);
    }
}

int main(void)
{
    Commands();
    UiCommands c = {0};
    assert(UiCommands_Register(&c,"print",UiCommands_Print,NULL));
    Editing(&c);
    Scrolling(&c);
    Desktop();
    ContentDrag();
    WindowAnimations();
    InterruptedAnimations();
    RecipeCatalog();
    ShellAndFiles();
    RecipeRoutes();
    VisualizerAndFiles();
    Clear();
    Workspace();
    puts("Interface tests passed");
    return 0;
}
