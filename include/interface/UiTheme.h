#ifndef UI_THEME_H
#define UI_THEME_H

#include "UiRenderer.h"

#define UI_INK      0x060f1affu
#define UI_PAPER    0xcbe4ebffu
#define UI_MUTED    0x82a5baffu
#define UI_ACCENT   0x70ede3ffu
#define UI_EDGE     0x31596dffu
#define UI_ERROR    0xffad9affu

void UiTheme_TerminalIcon(UiRenderer *, int x, int y, UiColor);
void UiTheme_DrawWindows(const UiDesktop *, UiRenderer *, bool nativeFocus, bool caretVisible);
#endif
