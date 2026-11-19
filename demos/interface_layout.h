#ifndef NEXUS_INTERFACE_LAYOUT_H
#define NEXUS_INTERFACE_LAYOUT_H

#include "interface/UiDesktop.h"

enum { TOP = 54, BOTTOM = 56, MARGIN = 26 };

static inline UiRect WorkspaceRect(int width, int height)
{
    return (UiRect){MARGIN,TOP+14,width-2*MARGIN,height-TOP-BOTTOM-28};
}

static inline UiRect WorkspaceInterior(UiRect area)
{
    return (UiRect){area.x+1,area.y+1,area.w-2,area.h-2};
}

#endif
