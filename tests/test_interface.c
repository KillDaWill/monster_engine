#define _POSIX_C_SOURCE 200809L
#include "interface/UiCommands.h"
#include "interface/UiDesktop.h"
#include "interface/UiTerminal.h"
#include "interface/UiText.h"
#include "interface/UiFilesApp.h"
#include "interface/UiRecipeCatalog.h"
#include "interface/UiShell.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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

typedef struct { int events[UI_BLUR+1]; } WindowState;
static void WindowInput(void *state, UiRect c, const UiEvent *event)
{
    assert(c.w > 0 && c.h > 0);
    ++((WindowState *)state)->events[event->type];
}

static void Desktop(void)
{
    UiDesktop d = {0};
    WindowState one = {0}, two = {0};
    UiDesktop_SetArea(&d,(UiRect){10,20,600,400});
    UiWindow w = {.rect={30,40,300,200},.minWidth=180,.minHeight=130,
                  .acceptsText=true,.input=WindowInput,.state=&one};
    UiWindowId first = UiDesktop_Add(&d,w);
    w.rect.x = 200;
    w.state = &two;
    UiWindowId second = UiDesktop_Add(&d,w);
    assert(first && second != first && d.focus == second);
    assert(d.windows[1].id == second && one.events[UI_BLUR] == 1);
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
    assert(filesId && desktop.count == 2 && desktop.focus == filesId);
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
    assert(files.previewUnavailable && files.directory == folder);
    UiFilesApp_MoveSelection(&files,1,1);
    assert(files.selected == sub && files.firstRow == 1 && !files.previewUnavailable);
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

    UiDesktop_Remove(&desktop,filesId);
    while (desktop.count < UI_MAX_WINDOWS) assert(UiDesktop_Add(&desktop,launch.presentation));
    Expect(&commands,"Files",UI_COMMAND_ERROR,"Files: Desktop window limit reached");
    UiVfs_Destroy(fs);
}

int main(void)
{
    Commands();
    UiCommands c = {0};
    assert(UiCommands_Register(&c,"print",UiCommands_Print,NULL));
    Editing(&c);
    Scrolling(&c);
    Desktop();
    RecipeCatalog();
    ShellAndFiles();
    puts("Interface tests passed");
    return 0;
}
