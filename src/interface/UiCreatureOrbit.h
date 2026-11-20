#ifndef UI_CREATURE_ORBIT_H
#define UI_CREATURE_ORBIT_H

#include "interface/UiDesktop.h"
#include "AABB.h"
#include "RenderInterfaces.h"

#define UI_ORBIT_DEFAULT_YAW .72f
#define UI_ORBIT_DEFAULT_PITCH .22f
#define UI_ORBIT_DEFAULT_ZOOM .78f
#define UI_ORBIT_MIN_ZOOM .55f
#define UI_ORBIT_MAX_ZOOM 1.85f

typedef struct {
    float yaw, pitch, zoom;
    int pointerX, pointerY;
    bool dragging, dirty;
} UiCreatureOrbit;

UiCreatureOrbit UiCreatureOrbit_Default(void);
void UiCreatureOrbit_Input(UiCreatureOrbit *, const UiEvent *);
ICamera UiCreatureOrbit_Camera(const UiCreatureOrbit *, AABB3D bounds, float aspect);

/* Ajusta la proyección al volumen visible de la vista previa de CO. */
ICamera UiCreatureOrbit_PreviewCamera(const UiCreatureOrbit *, AABB3D bounds, float aspect);

#endif
