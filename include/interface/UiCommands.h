#ifndef UI_COMMANDS_H
#define UI_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

enum { UI_COMMAND_BYTES = 1024, UI_COMMAND_LIMIT = 24, UI_COMMAND_NAME_BYTES = 32 };
typedef enum { UI_COMMAND_OK, UI_COMMAND_ERROR, UI_COMMAND_EMPTY } UiCommandResult;
/* El argumento pertenece al llamador. El handler puede escribir una respuesta terminada en NUL. */
typedef UiCommandResult (*UiCommandHandler)(void *, const char *, char *, size_t);
typedef struct {
    char name[UI_COMMAND_NAME_BYTES];
    UiCommandHandler handler;
    void *state;
} UiCommand;
typedef struct { UiCommand entries[UI_COMMAND_LIMIT]; size_t count; } UiCommands;

bool UiCommands_Register(UiCommands *, const char *, UiCommandHandler, void *);
const UiCommand *UiCommands_Find(const UiCommands *, const char *);
UiCommandResult UiCommands_Execute(const UiCommands *, const char *, char *, size_t);
UiCommandResult UiCommands_Print(void *, const char *, char *, size_t);
#endif
