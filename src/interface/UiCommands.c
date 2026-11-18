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

bool UiCommands_Register(UiCommands *registry, const char *name,
                         UiCommandHandler handler, void *state)
{
    if (!name || !*name || !handler || strlen(name) >= UI_COMMAND_NAME_BYTES ||
        strpbrk(name, " \t\r\n") || registry->count == UI_COMMAND_LIMIT ||
        UiCommands_Find(registry, name)) return false;
    UiCommand *entry = &registry->entries[registry->count++];
    strcpy(entry->name, name);
    entry->handler = handler;
    entry->state = state;
    return true;
}

UiCommandResult UiCommands_Execute(const UiCommands *registry, const char *line,
                                    char *out, size_t capacity)
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
    }
    if (!entry) {
        snprintf(out, capacity, "ERROR: Unknown command '%.*s'", (int)length, line);
        return UI_COMMAND_ERROR;
    }
    line += length;
    while (Separator(*line)) ++line;
    UiCommandResult result = entry->handler(entry->state, line, out, capacity);
    out[capacity - 1] = '\0';
    return result;
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
