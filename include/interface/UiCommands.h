#ifndef UI_COMMANDS_H
#define UI_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

enum { UI_COMMAND_BYTES = 1024, UI_COMMAND_LIMIT = 24, UI_COMMAND_NAME_BYTES = 32,
       UI_COMMAND_REPLY_BYTES = UI_COMMAND_BYTES + 32 };
typedef enum { UI_COMMAND_OK, UI_COMMAND_ERROR, UI_COMMAND_EMPTY, UI_COMMAND_SILENT } UiCommandResult;
/* El argumento pertenece al llamador. El handler puede escribir una respuesta terminada en NUL. */
typedef UiCommandResult (*UiCommandHandler)(void *, const char *, char *, size_t);
/* El receptor consume cada línea de forma síncrona; el handler conserva su memoria. */
typedef void (*UiCommandWriteLine)(void *, UiCommandResult, const char *);
typedef struct { UiCommandWriteLine write; void *state; } UiCommandOutput;
typedef UiCommandResult (*UiCommandLineHandler)(void *, const char *, UiCommandOutput);
typedef struct {
    char name[UI_COMMAND_NAME_BYTES];
    UiCommandHandler handler;
    UiCommandLineHandler lines;
    void *state;
} UiCommand;
typedef struct { UiCommand entries[UI_COMMAND_LIMIT]; size_t count; } UiCommands;

bool UiCommands_Register(UiCommands *, const char *, UiCommandHandler, void *);
bool UiCommands_RegisterLines(UiCommands *, const char *, UiCommandLineHandler, void *);
const UiCommand *UiCommands_Find(const UiCommands *, const char *);
UiCommandResult UiCommands_Execute(const UiCommands *, const char *, char *, size_t);
UiCommandResult UiCommands_ExecuteLines(const UiCommands *, const char *, UiCommandOutput);
UiCommandResult UiCommands_Print(void *, const char *, char *, size_t);
#endif
