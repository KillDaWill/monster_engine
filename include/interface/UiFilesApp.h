#ifndef UI_FILES_APP_H
#define UI_FILES_APP_H

#include "UiDesktop.h"
#include "UiCommands.h"
#include "UiVfs.h"

enum { UI_FILES_HISTORY = 64 };
typedef struct {
    const UiVfs *fs;
    UiVfsId directory, selected;
    UiVfsId back[UI_FILES_HISTORY];
    int backCount, firstRow;
    bool previewUnavailable;
    UiWindowId window;
} UiFilesApp;

typedef struct {
    UiFilesApp *files;
    UiDesktop *desktop;
    UiWindow presentation;
} UiFilesLaunch;

/* El árbol y el estado de la aplicación sobreviven a su ventana. */
void UiFilesApp_Init(UiFilesApp *, const UiVfs *);
bool UiFilesApp_Open(UiFilesLaunch *);
UiCommandResult UiFilesApp_Command(void *, const char *, char *, size_t);
UiCommandResult UiFilesApp_CloseCommand(void *, const char *, char *, size_t);
bool UiFilesApp_Go(UiFilesApp *, UiVfsId);
bool UiFilesApp_Back(UiFilesApp *);
bool UiFilesApp_Up(UiFilesApp *);
void UiFilesApp_Select(UiFilesApp *, UiVfsId);
void UiFilesApp_Activate(UiFilesApp *);
void UiFilesApp_MoveSelection(UiFilesApp *, int direction, int visibleRows);
void UiFilesApp_Scroll(UiFilesApp *, int delta, int visibleRows);
int UiFilesApp_Count(const UiFilesApp *);
UiVfsId UiFilesApp_Item(const UiFilesApp *, int row);
#endif
