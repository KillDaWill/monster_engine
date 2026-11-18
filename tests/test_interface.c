#include "interface/UiCommands.h"
#include "interface/UiDesktop.h"
#include "interface/UiTerminal.h"
#include "interface/UiText.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
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

int main(void)
{
    Commands();
    UiCommands c = {0};
    assert(UiCommands_Register(&c,"print",UiCommands_Print,NULL));
    Editing(&c);
    Scrolling(&c);
    Desktop();
    puts("Interface tests passed");
    return 0;
}
