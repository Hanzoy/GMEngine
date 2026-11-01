#pragma once
#include <cstdint>

// A build-time ceiling, not a reservation or a streaming/cache budget. The
// compiler, synchronous loader and background scene loader must agree.
#ifndef GM_RENDER_RESOURCE_BUDGET_MIB
#define GM_RENDER_RESOURCE_BUDGET_MIB 128
#endif
namespace gm {
inline constexpr uint64_t RenderResourceBudget = uint64_t(GM_RENDER_RESOURCE_BUDGET_MIB) * 1024 * 1024;
inline constexpr uint64_t SceneBinaryBudget = RenderResourceBudget + 64ull * 1024 * 1024;
static_assert(GM_RENDER_RESOURCE_BUDGET_MIB >= 1 && GM_RENDER_RESOURCE_BUDGET_MIB <= 2048);
static_assert(SceneBinaryBudget + 12 < UINT32_MAX);
inline constexpr bool renderPayloadFits(uint64_t used, uint64_t added) {
    return used <= RenderResourceBudget && added <= RenderResourceBudget - used;
}
} // namespace gm
