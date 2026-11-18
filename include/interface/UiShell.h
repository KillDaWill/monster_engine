#ifndef UI_SHELL_H
#define UI_SHELL_H

#include "UiCommands.h"
#include "UiVfs.h"

typedef struct { const UiVfs *fs; UiVfsId cwd; } UiShell;

/* La sesión toma prestado el árbol. El registro toma prestada la sesión. */
void UiShell_Init(UiShell *, const UiVfs *);
bool UiShell_Register(UiShell *, UiCommands *);
bool UiShell_Location(const void *, char *, size_t);
#endif
