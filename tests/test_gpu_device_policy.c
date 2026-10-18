/** @file test_gpu_device_policy.c
 * @brief Política de GPU en equipos híbridos, monoadaptador y sin controlador utilizable.
 */
#include "GpuDevicePolicy.h"
#include "test_utils.h"
void run_gpu_device_policy_tests(void) {
    GpuDeviceCapability devices[]={{true,false,512ull<<20},{true,true,4ull<<30},{true,true,8ull<<30}};
    TEST_ASSERT(GpuDevicePolicy_Select(devices,2,0)==1,"Elegir la dedicada del portátil híbrido");
    TEST_ASSERT(GpuDevicePolicy_Select(devices,3,0)==2,"Desempatar dedicadas por memoria conocida");
    devices[1].usable=devices[2].usable=false;
    TEST_ASSERT(GpuDevicePolicy_Select(devices,3,0)==0,"Usar integrada cuando no hay dedicada utilizable");
    TEST_ASSERT(GpuDevicePolicy_Select(devices,1,0)==0,"Conservar equipos con una sola GPU");
    devices[1]=(GpuDeviceCapability){true,true,0};
    TEST_ASSERT(GpuDevicePolicy_Select(devices,2,0)==1,"Memoria desconocida no invalida una dedicada");
    devices[0]=(GpuDeviceCapability){true,true,0};
    TEST_ASSERT(GpuDevicePolicy_Select(devices,2,0)==0,"Empate conserva el adaptador predeterminado");
    TEST_ASSERT(GpuDevicePolicy_Select(NULL,0,0)==SIZE_MAX,"Inventario ausente");
    devices[0].usable=devices[1].usable=false;
    TEST_ASSERT(GpuDevicePolicy_Select(devices,2,0)==SIZE_MAX,"No seleccionar controladores ausentes");
}
