/** @file GpuDevicePolicy.c
 * @brief Selección barata al arrancar; no introduce pruebas ni sondeos por fotograma.
 */
#include "GpuDevicePolicy.h"
size_t GpuDevicePolicy_Select(const GpuDeviceCapability* devices,size_t count,size_t defaultIndex) {
    if(!devices)return SIZE_MAX;
    size_t selected=defaultIndex<count&&devices[defaultIndex].usable?defaultIndex:SIZE_MAX;
    for(size_t i=0;i<count;++i) {
        if(!devices[i].usable)continue;
        if(selected==SIZE_MAX || (devices[i].dedicated&&!devices[selected].dedicated) ||
           (devices[i].dedicated==devices[selected].dedicated && devices[i].dedicatedBytes>0 &&
            devices[selected].dedicatedBytes>0 && devices[i].dedicatedBytes>devices[selected].dedicatedBytes))selected=i;
    }
    return selected;
}
