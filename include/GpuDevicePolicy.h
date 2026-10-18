/** @file GpuDevicePolicy.h
 * @brief Preferencia de adaptador de renderizado a partir del inventario de plataforma.
 */
#ifndef GPU_DEVICE_POLICY_H
#define GPU_DEVICE_POLICY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/** @brief Capacidad declarada por el adaptador de plataforma, sin inicializar contextos. */
typedef struct GpuDeviceCapability {
    bool usable, dedicated;
    uint64_t dedicatedBytes; /**< Cero indica memoria desconocida, no ausencia de GPU. */
} GpuDeviceCapability;
/** @brief Prefiere hardware dedicado; desempata por memoria conocida y conserva el predeterminado.
 * @param defaultIndex Adaptador predeterminado; SIZE_MAX si se desconoce.
 * @return Índice del adaptador preferido o SIZE_MAX si ninguno está disponible. */
size_t GpuDevicePolicy_Select(const GpuDeviceCapability*,size_t count,size_t defaultIndex);
#endif
