#include "TimestampPairs.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rhi {

int TimestampPairs::Reserve(int slot) {
    if (slot < 0 || _slots.size() >= _capacity) {
        return -1;
    }
    _slots.push_back(slot);
    return static_cast<int>(_slots.size() - 1);
}

void TimestampPairs::Resolve(const std::uint64_t* ticks, std::size_t tickCount, std::vector<double>& msPerSlot) const {
    for (std::size_t pair = 0; pair < _slots.size() && 2 * pair + 1 < tickCount; ++pair) {
        const std::uint64_t begin = ticks[2 * pair];
        const std::uint64_t end = ticks[2 * pair + 1];
        if (end <= begin) {
            continue;
        }
        const auto slot = static_cast<std::size_t>(_slots[pair]);
        if (msPerSlot.size() <= slot) {
            msPerSlot.resize(slot + 1, 0.0);
        }
        msPerSlot[slot] += static_cast<double>(end - begin) / 1.0e6;
    }
}

} // namespace rhi
