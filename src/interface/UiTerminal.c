#include "interface/UiTerminal.h"
#include "interface/UiText.h"
#include <string.h>

static int Max(int a, int b) { return a > b ? a : b; }

void UiTerminal_Init(UiTerminal *t, const UiCommands *commands)
{
    *t = (UiTerminal){.commands = commands, .following = true, .columns = 80, .visibleRows = 1};
}

static int Rows(const char *text, int columns)
{
    int count = 0;
    do {
        size_t advance;
        UiText_Wrap(text, columns, &advance);
        text += advance;
        ++count;
    } while (*text);
    return count;
}

void UiTerminal_Append(UiTerminal *t, UiLogKind kind, const char *text)
{
    if (t->logCount == UI_LOG_LIMIT) {
        t->totalRows -= t->log[t->logStart % UI_LOG_LIMIT].rows;
        ++t->logStart;
        --t->logCount;
        if (t->anchorEntry < t->logStart) {
            t->anchorEntry = t->logStart;
            t->anchorByte = 0;
        }
    }
    UiLogEntry *entry = &t->log[(t->logStart + t->logCount++) % UI_LOG_LIMIT];
    size_t end = 0, next = 0;
    while (UiText_Next(text, &next) && next < sizeof(entry->text)) end = next;
    memcpy(entry->text, text, end);
    entry->text[end] = '\0';
    entry->rows = Rows(entry->text, t->columns);
    entry->kind = kind;
    t->totalRows += entry->rows;
}

void UiTerminal_Insert(UiTerminal *t, const char *text)
{
    size_t length = strlen(t->line), offset = 0;
    while (text[offset]) {
        size_t start = offset;
        uint32_t cp = UiText_Next(text, &offset);
        /* Pegar varias líneas las convierte en una sola entrada editable. */
        bool separator = cp == '\n' || cp == '\r' || cp == '\t';
        if (cp == '\n' && start && text[start - 1] == '\r') continue;
        if (!separator && (cp < 32 || cp == 127)) continue;
        const char *bytes = separator ? " " : text + start;
        size_t count = separator ? 1 : offset - start;
        if (cp == 0xfffd && count == 1) {
            bytes = "\xef\xbf\xbd";
            count = 3;
        }
        if (length + count >= sizeof(t->line)) break;
        memmove(t->line + t->cursor + count, t->line + t->cursor, length - t->cursor + 1);
        memcpy(t->line + t->cursor, bytes, count);
        length += count;
        t->cursor += count;
    }
}

static void Recall(UiTerminal *t, int direction)
{
    int next = t->historyIndex + direction;
    if (next < 0 || next > t->historyCount) return;
    if (t->historyIndex == t->historyCount) strcpy(t->draft, t->line);
    t->historyIndex = next;
    strcpy(t->line, next == t->historyCount ? t->draft :
           t->history[(t->historyStart + next) % UI_HISTORY_LIMIT]);
    t->cursor = strlen(t->line);
}

static void Submit(UiTerminal *t)
{
    if (!t->line[strspn(t->line, " \t")]) return;
    if (t->historyCount == UI_HISTORY_LIMIT) {
        t->historyStart = (t->historyStart + 1) % UI_HISTORY_LIMIT;
        --t->historyCount;
    }
    strcpy(t->history[(t->historyStart + t->historyCount++) % UI_HISTORY_LIMIT], t->line);
    t->historyIndex = t->historyCount;
    t->draft[0] = '\0';
    UiTerminal_Append(t, UI_LOG_COMMAND, t->line);
    char output[UI_LOG_BYTES];
    UiCommandResult result = UiCommands_Execute(t->commands, t->line, output, sizeof(output));
    if (result != UI_COMMAND_EMPTY)
        UiTerminal_Append(t, result == UI_COMMAND_ERROR ? UI_LOG_ERROR : UI_LOG_OUTPUT, output);
    t->line[0] = '\0';
    t->cursor = 0;
}

void UiTerminal_Key(UiTerminal *t, UiKey key)
{
    size_t next = t->cursor;
    switch (key) {
    case UI_LEFT: t->cursor = UiText_Previous(t->line, t->cursor); break;
    case UI_RIGHT: UiText_Next(t->line, &t->cursor); break;
    case UI_HOME: t->cursor = 0; break;
    case UI_END: t->cursor = strlen(t->line); break;
    case UI_BACKSPACE:
        t->cursor = UiText_Previous(t->line, t->cursor);
        memmove(t->line + t->cursor, t->line + next, strlen(t->line + next) + 1);
        break;
    case UI_DELETE:
        UiText_Next(t->line, &next);
        memmove(t->line + t->cursor, t->line + next, strlen(t->line + next) + 1);
        break;
    case UI_UP: Recall(t, -1); break;
    case UI_DOWN: Recall(t, 1); break;
    case UI_ENTER: Submit(t); break;
    case UI_PAGE_UP: UiTerminal_Scroll(t, Max(1, t->visibleRows - 1)); break;
    case UI_PAGE_DOWN: UiTerminal_Scroll(t, -Max(1, t->visibleRows - 1)); break;
    }
}

void UiTerminal_Layout(UiTerminal *t, int columns, int visibleRows)
{
    t->visibleRows = Max(1, visibleRows);
    columns = Max(1, columns);
    if (t->columns != columns) {
        t->columns = columns;
        t->totalRows = 0;
        for (int i = 0; i < t->logCount; ++i) {
            UiLogEntry *e = &t->log[(t->logStart + i) % UI_LOG_LIMIT];
            e->rows = Rows(e->text, columns);
            t->totalRows += e->rows;
        }
    }
    if (t->totalRows <= t->visibleRows) t->following = true;
}

int UiTerminal_FirstRow(const UiTerminal *t)
{
    int bottom = Max(0, t->totalRows - t->visibleRows);
    if (t->following) return bottom;
    int row = 0;
    for (int i = 0; i < t->logCount; ++i) {
        uint64_t id = t->logStart + i;
        const UiLogEntry *e = &t->log[id % UI_LOG_LIMIT];
        if (id < t->anchorEntry) {
            row += e->rows;
        } else {
            size_t byte = 0, step;
            while (byte < t->anchorByte && e->text[byte]) {
                UiText_Wrap(e->text + byte, t->columns, &step);
                if (byte + step > t->anchorByte) break;
                byte += step;
                ++row;
            }
            break;
        }
    }
    return row < bottom ? row : bottom;
}

void UiTerminal_Scroll(UiTerminal *t, int up)
{
    int bottom = Max(0, t->totalRows - t->visibleRows);
    int64_t target = (int64_t)UiTerminal_FirstRow(t) - up;
    t->following = target >= bottom;
    if (t->following) return;
    int row = target > 0 ? (int)target : 0;
    for (int i = 0; i < t->logCount; ++i) {
        uint64_t id = t->logStart + i;
        const UiLogEntry *e = &t->log[id % UI_LOG_LIMIT];
        if (row >= e->rows) {
            row -= e->rows;
            continue;
        }
        t->anchorEntry = id;
        t->anchorByte = 0;
        while (row-- > 0) {
            size_t step;
            UiText_Wrap(e->text + t->anchorByte, t->columns, &step);
            t->anchorByte += step;
        }
        break;
    }
}

void UiTerminal_VisitRows(const UiTerminal *t, UiTerminalRow visit, void *state)
{
    int first = UiTerminal_FirstRow(t), row = 0;
    for (int i = 0; i < t->logCount && row < first + t->visibleRows; ++i) {
        const UiLogEntry *e = &t->log[(t->logStart + i) % UI_LOG_LIMIT];
        if (row + e->rows <= first) {
            row += e->rows;
            continue;
        }
        const char *text = e->text;
        do {
            size_t step;
            size_t bytes = UiText_Wrap(text, t->columns, &step);
            if (row >= first)
                visit(state, row - first, e->kind, text, bytes, text != e->text);
            ++row;
            text += step;
        } while (*text && row < first + t->visibleRows);
    }
}
