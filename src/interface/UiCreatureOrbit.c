#include "UiCreatureOrbit.h"
#include <math.h>

UiCreatureOrbit UiCreatureOrbit_Default(void)
{
    return (UiCreatureOrbit){
        .yaw=UI_ORBIT_DEFAULT_YAW,.pitch=UI_ORBIT_DEFAULT_PITCH,.zoom=UI_ORBIT_DEFAULT_ZOOM
    };
}

void UiCreatureOrbit_Input(UiCreatureOrbit *orbit, const UiEvent *event)
{
    float yaw = orbit->yaw, pitch = orbit->pitch, zoom = orbit->zoom;
    switch (event->type) {
    case UI_PRESS:
        orbit->dragging = event->clicks < 2;
        orbit->pointerX = event->x;
        orbit->pointerY = event->y;
        if (!orbit->dragging) {
            yaw = UI_ORBIT_DEFAULT_YAW;
            pitch = UI_ORBIT_DEFAULT_PITCH;
            zoom = UI_ORBIT_DEFAULT_ZOOM;
        }
        break;
    case UI_POINTER_MOVE:
        if (!orbit->dragging) return;
        yaw = remainderf(yaw - .012f*((float)event->x-orbit->pointerX),6.28318531f);
        pitch = fminf(1.42f,fmaxf(-1.42f,pitch + .010f*((float)event->y-orbit->pointerY)));
        orbit->pointerX = event->x;
        orbit->pointerY = event->y;
        break;
    case UI_WHEEL: {
        float steps = event->preciseWheel != 0 ? event->preciseWheel : (float)event->wheel;
        if (!isfinite(steps)) return;
        zoom = fminf(UI_ORBIT_MAX_ZOOM,fmaxf(UI_ORBIT_MIN_ZOOM,zoom*expf(-.12f*steps)));
        break;
    }
    case UI_POINTER_RELEASE:
    case UI_BLUR:
        orbit->dragging = false;
        return;
    default:
        return;
    }
    orbit->dirty |= yaw != orbit->yaw || pitch != orbit->pitch || zoom != orbit->zoom;
    orbit->yaw = yaw;
    orbit->pitch = pitch;
    orbit->zoom = zoom;
}

ICamera UiCreatureOrbit_Camera(const UiCreatureOrbit *orbit, AABB3D bounds, float aspect)
{
    Vector3 target = Vec3_Scale(Vec3_Add(bounds.start,bounds.end),.5f);
    float radius = fmaxf(.01f,.5f*Vec3_Length(AABB_Size(bounds)));
    float cp = cosf(orbit->pitch);
    Vector3 outward = Vec3_Create(sinf(orbit->yaw)*cp,sinf(orbit->pitch),cosf(orbit->yaw)*cp);
    /* La esfera fija el encuadre para toda la órbita y también para ventanas estrechas. */
    float zoom = (orbit->zoom-UI_ORBIT_MIN_ZOOM)/(UI_ORBIT_MAX_ZOOM-UI_ORBIT_MIN_ZOOM);
    float height = 2.08f*radius*(1.0f+1.35f*zoom)/fminf(aspect,1.0f);
    return (ICamera){
        .position=Vec3_Add(target,Vec3_Scale(outward,2.0f*radius)),.target=target,
        .up={0,1,0},.fov=-height,.aspectRatio=aspect,.nearPlane=.05f*radius,.farPlane=4.0f*radius
    };
}

ICamera UiCreatureOrbit_PreviewCamera(const UiCreatureOrbit *orbit, AABB3D bounds, float aspect)
{
    /* Conserva el encuadre esférico invariante bajo rotación para que el
     * tamaño aparente no varíe al girar la criatura. */
    return UiCreatureOrbit_Camera(orbit,bounds,aspect);
}
