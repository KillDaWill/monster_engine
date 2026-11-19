#include "interface/UiCommands.h"
#include <stdio.h>
#include <string.h>

static bool Separator(char c) { return c == ' ' || c == '\t'; }

const UiCommand *UiCommands_Find(const UiCommands *registry, const char *name)
{
    for (size_t i = 0; i < registry->count; ++i)
        if (!strcmp(registry->entries[i].name, name)) return &registry->entries[i];
    return NULL;
}

static UiCommand *Register(UiCommands *registry, const char *name, void *state)
{
    if (!name || !*name || strlen(name) >= UI_COMMAND_NAME_BYTES ||
        strpbrk(name, " \t\r\n") || registry->count == UI_COMMAND_LIMIT ||
        UiCommands_Find(registry, name)) return NULL;
    UiCommand *entry = &registry->entries[registry->count++];
    *entry = (UiCommand){.state=state};
    strcpy(entry->name, name);
    return entry;
}

bool UiCommands_Register(UiCommands *registry, const char *name,
                         UiCommandHandler handler, void *state)
{
    UiCommand *entry = handler ? Register(registry,name,state) : NULL;
    if (!entry) return false;
    entry->handler = handler;
    return true;
}

bool UiCommands_RegisterPrefix(UiCommands *registry, const char *name,
                               UiCommandHandler handler, void *state)
{
    if (!name || !*name || name[strlen(name)-1] != '.' ||
        !UiCommands_Register(registry,name,handler,state)) return false;
    registry->entries[registry->count-1].prefix = true;
    return true;
}

bool UiCommands_RegisterLines(UiCommands *registry, const char *name,
                              UiCommandLineHandler handler, void *state)
{
    UiCommand *entry = handler ? Register(registry,name,state) : NULL;
    if (!entry) return false;
    entry->lines = handler;
    return true;
}

static UiCommandResult Execute(const UiCommands *registry, const char *line,
                                    char *out, size_t capacity, UiCommandOutput lines, bool emitReply)
{
    if (!capacity) return UI_COMMAND_ERROR;
    out[0] = '\0';
    while (Separator(*line)) ++line;
    if (!*line) return UI_COMMAND_EMPTY;
    size_t length = strcspn(line, " \t");
    char name[UI_COMMAND_NAME_BYTES];
    const UiCommand *entry = NULL;
    if (length < sizeof(name)) {
        memcpy(name, line, length);
        name[length] = '\0';
        entry = UiCommands_Find(registry, name);
        if (entry && entry->prefix) entry = NULL;
    }
    if (!entry) {
        size_t best = 0;
        for (size_t i=0; i<registry->count; ++i) {
            const UiCommand *candidate = &registry->entries[i];
            if (!candidate->prefix) continue;
            size_t bytes = strlen(candidate->name);
            if (bytes > best && !strncmp(line,candidate->name,bytes)) {
                best = bytes;
                entry = candidate;
            }
        }
        if (entry) length = best;
    }
    if (!entry) {
        snprintf(out, capacity, "ERROR: Unknown command '%.*s'", (int)length, line);
        if (emitReply && lines.write) lines.write(lines.state,UI_COMMAND_ERROR,out);
        return UI_COMMAND_ERROR;
    }
    line += length;
    if (!entry->prefix) while (Separator(*line)) ++line;
    if (entry->lines) return entry->lines(entry->state,line,lines);
    UiCommandResult result = entry->handler(entry->state, line, out, capacity);
    out[capacity - 1] = '\0';
    if (emitReply && lines.write && (result == UI_COMMAND_OK || result == UI_COMMAND_ERROR))
        lines.write(lines.state,result,out);
    return result;
}

typedef struct { char *text; size_t capacity, bytes; bool hasLine, full; } Buffer;

static void Collect(void *state, UiCommandResult kind, const char *text)
{
    (void)kind;
    Buffer *buffer = state;
    size_t bytes = strlen(text), separator = buffer->hasLine;
    if (buffer->full || bytes + separator >= buffer->capacity-buffer->bytes) {
        buffer->full = true;
        return;
    }
    if (separator) buffer->text[buffer->bytes++] = '\n';
    memcpy(buffer->text+buffer->bytes,text,bytes+1);
    buffer->bytes += bytes;
    buffer->hasLine = true;
}

UiCommandResult UiCommands_Execute(const UiCommands *registry, const char *line,
                                    char *out, size_t capacity)
{
    Buffer buffer = {.text=out,.capacity=capacity};
    if (!capacity) return UI_COMMAND_ERROR;
    UiCommandResult result = Execute(registry,line,out,capacity,
                                     (UiCommandOutput){Collect,&buffer},false);
    return buffer.full ? UI_COMMAND_ERROR : result;
}

UiCommandResult UiCommands_ExecuteLines(const UiCommands *registry, const char *line,
                                         UiCommandOutput lines)
{
    char buffer[UI_COMMAND_REPLY_BYTES];
    return Execute(registry,line,buffer,sizeof(buffer),lines,true);
}

UiCommandResult UiCommands_Print(void *state, const char *args, char *out, size_t capacity)
{
    (void)state;
    if (!capacity) return UI_COMMAND_ERROR;
    if (*args != '"') {
        snprintf(out, capacity, "%s", args);
        return UI_COMMAND_OK;
    }
    size_t length = 0;
    ++args;
    while (*args && *args != '"') {
        if (*args == '\\' && (args[1] == '"' || args[1] == '\\')) ++args;
        if (length + 1 < capacity) out[length++] = *args;
        ++args;
    }
    if (*args == '"') {
        ++args;
        while (Separator(*args)) ++args;
        if (!*args) {
            out[length] = '\0';
            return UI_COMMAND_OK;
        }
    }
    snprintf(out, capacity, "ERROR: Expected one double-quoted argument");
    return UI_COMMAND_ERROR;
}
