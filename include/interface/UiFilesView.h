#ifndef UI_FILES_VIEW_H
#define UI_FILES_VIEW_H

#include "UiFilesApp.h"

void UiFilesView_Draw(void *, UiRenderer *, UiRect, bool active, bool caret);
void UiFilesView_Input(void *, UiRect, const UiEvent *);
void UiFilesView_Icon(UiRenderer *, int x, int y, uint32_t color);
#endif
