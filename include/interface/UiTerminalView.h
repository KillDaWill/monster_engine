#ifndef UI_TERMINAL_VIEW_H
#define UI_TERMINAL_VIEW_H

#include "UiTerminal.h"

void UiTerminalView_Draw(void *, UiRenderer *, UiRect, bool active, bool caret);
void UiTerminalView_Input(void *, UiRect, const UiEvent *);
UiRect UiTerminalView_InputRect(UiRect content);
#endif
