#include "GpuResourceRegistry.h"
#include "../core/Log.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

namespace GpuResources {
namespace {
std::uint32_t g_generation = 0;
}

std::size_t LiveHandleCount() {
    return detail::g_liveCount;
}

std::size_t RestorableCount() {
    std::size_t count = 0;
    for (const IGpuRestorable* node = detail::g_restorableHead; node; node = node->_next) {
        ++count;
    }
    return count;
}

std::size_t InvalidateAll() {
    std::size_t count = 0;
    while (GlHandleBase* node = detail::g_liveHead) {
        node->Release(); // unlinks and zeroes; no glDelete*
        ++count;
    }
    return count;
}

std::size_t RecreateAll() {
    // Snapshot first: a RestoreGpu may construct owners, which register themselves already
    // on the new context. (It must not destroy or move one: the snapshot would dangle.)
    std::array<std::vector<IGpuRestorable*>, kPhaseCount> phases;
    for (IGpuRestorable* node = detail::g_restorableHead; node; node = node->_next) {
        phases[static_cast<std::size_t>(node->_phase)].push_back(node);
    }
    std::size_t restored = 0;
    for (std::vector<IGpuRestorable*>& owners : phases) {
        // Oldest first (the list is newest-first): parents usually register before the
        // children they build.
        for (auto it = owners.rbegin(); it != owners.rend(); ++it) {
            try {
                (*it)->RestoreGpu();
                ++restored;
            } catch (const std::exception& error) {
                LOG_ERROR("GL", "restoring a GPU resource failed: %s", error.what());
            }
        }
    }
    ++g_generation;
    return restored;
}

std::uint32_t Generation() {
    return g_generation;
}

} // namespace GpuResources
