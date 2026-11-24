#ifndef SOLARSYSTEM_GFX_RHI_TIMESTAMPPAIRS_H
#define SOLARSYSTEM_GFX_RHI_TIMESTAMPPAIRS_H
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * Per-pass GPU time from WebGPU timestamp queries (docs/ARCHITECTURE.md §7.9), GL-free.
 *
 * WebGPU timestamps a render pass at its beginning and end (GPURenderPassTimestampWrites), so
 * each render pass the planner opens takes one begin/end pair of a GPUQuerySet, billed to the
 * timer slot of the frame-graph pass it belongs to — the same slot numbering GpuTimer uses for
 * GL_TIME_ELAPSED (FrameProfile::kStageCount + PassInfo::timerSlot). A pass kind that opens
 * many render passes (one scene pass per component and view) is the sum of its pairs.
 */
namespace rhi {

class TimestampPairs {
public:
    explicit TimestampPairs(std::uint32_t capacity) : _capacity(capacity) {}

    /** Forget the pairs of the previous frame. */
    void BeginFrame() { _slots.clear(); }
    /** A pair for one render pass billed to `slot`: its index, or -1 if untimed or the set is full. */
    int Reserve(int slot);
    std::uint32_t Count() const { return static_cast<std::uint32_t>(_slots.size()); }
    std::uint32_t Capacity() const { return _capacity; }
    /** The slot pair `index` was reserved for. */
    int SlotOf(std::uint32_t index) const { return _slots[index]; }

    /**
     * Sum each slot's elapsed time. `ticks` holds 2 × Count() nanosecond timestamps (begin, end
     * per pair) as resolveQuerySet writes them. A pair whose end is before its begin (an
     * implementation may zero or clamp timestamps) adds nothing. `msPerSlot` grows to fit.
     */
    void Resolve(const std::uint64_t* ticks, std::size_t tickCount, std::vector<double>& msPerSlot) const;

private:
    std::uint32_t _capacity;
    std::vector<int> _slots;
};

} // namespace rhi

#endif // SOLARSYSTEM_GFX_RHI_TIMESTAMPPAIRS_H
