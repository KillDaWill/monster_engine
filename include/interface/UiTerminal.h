#ifndef UI_TERMINAL_H
#define UI_TERMINAL_H

#include "UiCommands.h"
#include "UiDesktop.h"

enum { UI_HISTORY_LIMIT = 64, UI_LOG_LIMIT = 256, UI_LOG_BYTES = UI_COMMAND_REPLY_BYTES };

typedef enum { UI_LOG_SYSTEM, UI_LOG_COMMAND, UI_LOG_OUTPUT, UI_LOG_ERROR } UiLogKind;
typedef struct {
    char text[UI_LOG_BYTES];
    int rows;
    UiLogKind kind;
} UiLogEntry;
typedef bool (*UiTerminalLocation)(const void *, char *, size_t);
typedef struct {
    const UiCommands *commands;
    UiTerminalLocation location;
    const void *locationState;
    char line[UI_COMMAND_BYTES], draft[UI_COMMAND_BYTES];
    char history[UI_HISTORY_LIMIT][UI_COMMAND_BYTES];
    size_t cursor;
    int historyStart, historyCount, historyIndex;
    UiLogEntry log[UI_LOG_LIMIT];
    uint64_t logStart;
    int logCount, columns, visibleRows, totalRows;
    bool following;
    uint64_t anchorEntry;
    size_t anchorByte;
    char composition[UI_COMMAND_BYTES];
    int compositionCursor, compositionLength;
    int inputFirstColumn;
} UiTerminal;

typedef void (*UiTerminalRow)(void *, int row, UiLogKind, const char *, size_t, bool continuation);

/* El registro vive al menos tanto como el terminal. El estado no posee memoria dinámica. */
void UiTerminal_Init(UiTerminal *, const UiCommands *);
/* Borra la salida y su ancla sin cambiar la edición ni el historial. */
void UiTerminal_ClearLog(UiTerminal *);
UiCommandResult UiTerminal_ClearCommand(void *, const char *, char *, size_t);
void UiTerminal_Append(UiTerminal *, UiLogKind, const char *);
void UiTerminal_Insert(UiTerminal *, const char *);
void UiTerminal_Key(UiTerminal *, UiKey);
void UiTerminal_Layout(UiTerminal *, int columns, int visibleRows);
void UiTerminal_Scroll(UiTerminal *, int rowsTowardTop);
int UiTerminal_FirstRow(const UiTerminal *);
void UiTerminal_VisitRows(const UiTerminal *, UiTerminalRow, void *);
#endif
