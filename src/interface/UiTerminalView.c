#include "interface/UiTerminalView.h"
#include "interface/UiTheme.h"
#include "interface/UiText.h"
#include <stdio.h>
#include <string.h>

enum { FONT = 15, OUTPUT_ROW = 21, GUTTER = 27, PAD = 12, HEADER = 32 };

UiRect UiTerminalView_InputRect(UiRect c)
{
    return (UiRect){c.x+PAD,c.y+c.h-60,c.w-2*PAD,34};
}

static UiRect OutputRect(UiRect c)
{
    UiRect input = UiTerminalView_InputRect(c);
    return (UiRect){c.x+PAD,c.y+HEADER,c.w-2*PAD,input.y-c.y-HEADER-10};
}

static int Max(int a, int b) { return a > b ? a : b; }

static void Layout(UiTerminal *t, UiRect output)
{
    UiTerminal_Layout(t, (output.w-GUTTER-15)/UiRenderer_Cell(FONT), output.h/OUTPUT_ROW);
}

typedef struct { char text[UI_COMMAND_BYTES+10]; int inset; } Prompt;

static Prompt MakePrompt(const UiTerminal *t, UiRect input)
{
    Prompt prompt = {.text=">",.inset=GUTTER};
    char path[UI_COMMAND_BYTES];
    if (!t->location || !t->location(t->locationState,path,sizeof(path))) return prompt;
    int cell = UiRenderer_Cell(FONT);
    int pathColumns = Max(4,(input.w-25)/cell-9-11);
    size_t bytes = strlen(path), start = 0;
    if (UiText_Count(path,bytes) > (size_t)pathColumns) {
        start = bytes;
        for (int i=0; i<pathColumns-3; ++i) start = UiText_Previous(path,start);
    }
    snprintf(prompt.text,sizeof(prompt.text),"nexus:%s%s $",start ? "..." : "",path+start);
    prompt.inset = 10+UiRenderer_Measure(prompt.text,strlen(prompt.text),FONT)+cell;
    return prompt;
}

UiRect UiTerminalView_TextRect(const UiTerminal *t, UiRect content)
{
    UiRect input = UiTerminalView_InputRect(content);
    int inset = MakePrompt(t,input).inset;
    return (UiRect){input.x+inset,input.y+2,Max(1,input.w-inset-6),input.h-4};
}

static void InputViewport(UiTerminal *t, UiRect input, int inset)
{
    int cursor = (int)UiText_Count(t->line, t->cursor) + t->compositionCursor;
    int columns = Max(1, (input.w-inset-9)/UiRenderer_Cell(FONT));
    if (cursor < t->inputFirstColumn) t->inputFirstColumn = cursor;
    if (cursor >= t->inputFirstColumn + columns) t->inputFirstColumn = cursor-columns+1;
    int count = (int)UiText_Count(t->line, strlen(t->line))
              + (int)UiText_Count(t->composition, strlen(t->composition));
    if (t->inputFirstColumn > Max(0, count-columns+1))
        t->inputFirstColumn = Max(0, count-columns+1);
}

typedef struct { UiRenderer *renderer; UiRect output; } RowView;

static void DrawRow(void *data, int row, UiLogKind kind, const char *text,
                    size_t length, bool continued)
{
    RowView *view = data;
    UiRenderer *r = view->renderer;
    int x = view->output.x, y = view->output.y+row*OUTPUT_ROW;
    static const UiColor colors[] = {UI_MUTED, UI_ACCENT, UI_PAPER, UI_ERROR};
    if (kind == UI_LOG_COMMAND)
        UiRenderer_Rect(r, (UiRect){x+1,y,view->output.w-12,OUTPUT_ROW}, 0x153546b0);
    if (!continued) {
        const char *marker = kind == UI_LOG_COMMAND ? ">" : kind == UI_LOG_ERROR ? "!" : "";
        UiRenderer_Text(r, x+9, y+1, FONT, colors[kind], marker);
        if (kind == UI_LOG_SYSTEM) UiRenderer_Rect(r, (UiRect){x+12,y+10,4,4}, 0x58abaaff);
    }
    UiRenderer_TextN(r, x+GUTTER, y+1, FONT, colors[kind], text, length);
}

static void DrawInput(UiTerminal *t, UiRenderer *r, UiRect b, Prompt prompt, bool active, bool caret)
{
    UiRenderer_Gradient(r, b, active ? 0x123240ff : 0x142632ff, 0x091a26ff);
    UiRenderer_Outline(r, b, active ? 0x4c9aa0ff : UI_EDGE);
    UiRenderer_Rect(r, (UiRect){b.x+1,b.y+1,2,b.h-2}, active ? UI_ACCENT : UI_MUTED);
    UiRenderer_Text(r, b.x+10, b.y+7, FONT, UI_ACCENT, prompt.text);
    if (!UiRenderer_PushClip(r, (UiRect){b.x+prompt.inset,b.y+2,b.w-prompt.inset-6,b.h-4})) return;

    int cell = UiRenderer_Cell(FONT);
    int x = b.x+prompt.inset-cell*t->inputFirstColumn, y = b.y+7;
    int prefix = UiRenderer_Measure(t->line, t->cursor, FONT);
    UiRenderer_TextN(r,x,y,FONT,UI_PAPER,t->line,t->cursor);
    x += prefix;
    int compositionWidth = UiRenderer_Measure(t->composition, strlen(t->composition), FONT);
    if (compositionWidth) {
        if (t->compositionLength)
            UiRenderer_Rect(r, (UiRect){x+cell*t->compositionCursor,y,
                cell*t->compositionLength,19}, 0x34636cff);
        UiRenderer_Text(r,x,y,FONT,UI_ACCENT,t->composition);
        UiRenderer_Rect(r,(UiRect){x,y+20,compositionWidth,1},UI_ACCENT);
    }
    UiRenderer_Text(r,x+compositionWidth,y,FONT,UI_PAPER,t->line+t->cursor);
    if (caret || (active && compositionWidth))
        UiRenderer_Rect(r,(UiRect){x+cell*t->compositionCursor,y+3,2,16},UI_ACCENT);
    UiRenderer_PopClip(r);
}

void UiTerminalView_Draw(void *state, UiRenderer *r, UiRect c, bool active, bool caret)
{
    UiTerminal *t = state;
    UiRect output = OutputRect(c), input = UiTerminalView_InputRect(c);
    Layout(t, output);
    Prompt prompt = MakePrompt(t,input);
    InputViewport(t,input,prompt.inset);
    UiRenderer_Rect(r, c, UI_INK);
    UiRenderer_Outline(r, c, 0x284352ff);

    UiRenderer_Text(r,c.x+PAD,c.y+6,12,UI_MUTED,"SESSION LOG");
    const char *status = t->following ? "LIVE" : "SCROLLED";
    int statusX = c.x+c.w-PAD-UiRenderer_Measure(status, strlen(status), 12);
    UiRenderer_Rect(r,(UiRect){statusX-11,c.y+14,4,4},t->following ? UI_ACCENT : 0xf2c17eff);
    UiRenderer_Text(r,statusX,c.y+6,12,t->following ? UI_ACCENT : 0xf2c17eff,status);
    UiRenderer_Rect(r,(UiRect){c.x+PAD,c.y+26,c.w-2*PAD,1},0x284352ff);

    int rowsH = t->visibleRows * OUTPUT_ROW;
    if (UiRenderer_PushClip(r, (UiRect){output.x,output.y,output.w,rowsH})) {
        UiRenderer_Rect(r,(UiRect){output.x+20,output.y,1,rowsH},0x1e3849ff);
        UiTerminal_VisitRows(t, DrawRow, &(RowView){r,output});
        UiRenderer_PopClip(r);
    }
    if (t->totalRows > t->visibleRows) {
        int x = output.x+output.w-4, size = Max(8,rowsH*t->visibleRows/t->totalRows);
        int y = (rowsH-size)*UiTerminal_FirstRow(t)/(t->totalRows-t->visibleRows);
        UiRenderer_Rect(r,(UiRect){x,output.y,2,rowsH},0x203c4cff);
        UiRenderer_Rect(r,(UiRect){x,output.y+y,2,size},0x64adbaff);
    }
    DrawInput(t, r, input, prompt, active, caret);
    UiRenderer_Text(r,c.x+PAD,c.y+c.h-22,11,UI_MUTED,"ENTER Run   /   UP DOWN History");
    char count[32];
    snprintf(count,sizeof(count),"%zu / %d B",strlen(t->line),UI_COMMAND_BYTES-1);
    UiRenderer_Text(r,c.x+c.w-PAD-UiRenderer_Measure(count,strlen(count),11),
                    c.y+c.h-22,11,UI_MUTED,count);
}

static void ClearComposition(UiTerminal *t)
{
    t->composition[0] = '\0';
    t->compositionCursor = t->compositionLength = 0;
}

static void SetComposition(UiTerminal *t, const UiEvent *e)
{
    size_t end = 0, next = 0;
    while (UiText_Next(e->text,&next) && next < sizeof(t->composition)) end = next;
    memcpy(t->composition,e->text,end);
    t->composition[end] = '\0';
    int count = (int)UiText_Count(t->composition,end);
    int cursor = Max(0,e->compositionCursor);
    t->compositionCursor = cursor < count ? cursor : count;
    int selection = Max(0,e->compositionLength);
    int remaining = count-t->compositionCursor;
    t->compositionLength = selection < remaining ? selection : remaining;
}

void UiTerminalView_Input(void *state, UiRect c, const UiEvent *e)
{
    UiTerminal *t = state;
    UiRect input = UiTerminalView_InputRect(c), output = OutputRect(c);
    Layout(t,output);
    switch (e->type) {
    case UI_PRESS:
        if (!UiRect_Contains(input,e->x,e->y) || t->composition[0]) break;
        int inset = MakePrompt(t,input).inset;
        InputViewport(t,input,inset);
        int cell = UiRenderer_Cell(FONT);
        int target = t->inputFirstColumn+Max(0,e->x-input.x-inset+cell/2)/cell;
        t->cursor = 0;
        while (target-- > 0 && UiText_Next(t->line,&t->cursor)) {}
        break;
    case UI_WHEEL:
        if (UiRect_Contains(output,e->x,e->y)) UiTerminal_Scroll(t,e->wheel*3);
        break;
    case UI_KEY:
        if (!t->composition[0] || e->key == UI_PAGE_UP || e->key == UI_PAGE_DOWN)
            UiTerminal_Key(t,e->key);
        break;
    case UI_TEXT:
    case UI_PASTE:
        ClearComposition(t);
        UiTerminal_Insert(t,e->text);
        break;
    case UI_COMPOSE: SetComposition(t,e); break;
    case UI_BLUR: ClearComposition(t); break;
    case UI_POINTER_MOVE:
    case UI_POINTER_RELEASE: break;
    }
}
