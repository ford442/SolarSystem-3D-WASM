#ifndef SOLARSYSTEM_RENDER_GRAPH_FRAMEGRAPH_H
#define SOLARSYSTEM_RENDER_GRAPH_FRAMEGRAPH_H
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * A frame as data (docs/ARCHITECTURE.md §7.5). GL-free and unit-tested
 * (tests/test_frame_graph.cpp); render/graph/FramePlan declares this app's frame with it.
 *
 * Passes declare which virtual resources they read and write. Every write makes a new
 * version of the resource, so "the shadow map component 3 drew" and "the shadow map
 * component 4 drew" are different things a pass can depend on. Compile() then:
 *
 *  - culls: a pass survives if it has a side effect, writes a resource marked `output` (the
 *    backbuffer), or produces a version some surviving pass reads. Nothing else runs, so a
 *    producer whose consumer was never declared (the solar-wind advection with the wind off)
 *    disappears without a check of its own.
 *  - orders: a topological sort over read-after-write, write-after-write and
 *    write-after-read edges that keeps declaration order wherever it is free to.
 *  - places transients: each `Create`d resource lives from its first to its last surviving
 *    use; two with equal descs and disjoint lifetimes share a physical slot (aliasing).
 *
 * Execute() runs the survivors in order, bracketing each with CommandList::BeginPass/EndPass,
 * which is where the GL backend opens the pass's GPU timer scope.
 */
namespace FrameGraph {

constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

/** A version of a virtual resource. Version 0 is its content before the frame's first write. */
struct ResourceHandle {
    std::uint32_t index = kInvalid;
    std::uint32_t version = 0;
    bool Valid() const { return index != kInvalid; }
};

struct ImportFlags {
    /** Its final content leaves the frame (the backbuffer): writing it keeps a pass alive. */
    bool output = false;
};

using PassId = std::uint32_t;
using ExecuteFn = std::function<void(rhi::CommandList&)>;

class Graph;

/** Declares one pass's resource accesses; returned by Graph::AddPass. */
class PassBuilder {
public:
    PassBuilder(Graph& graph, PassId pass) : _graph(&graph), _pass(pass) {}
    /** Reads `handle` (that exact version). */
    PassBuilder& Read(ResourceHandle handle);
    /**
     * Draws onto `handle` (load + store: depends on the version before it) and returns the
     * version it leaves. `handle` must be the latest version.
     */
    ResourceHandle Write(ResourceHandle handle);
    /** Clears or fully overwrites `handle` (no dependency on the previous content). */
    ResourceHandle Overwrite(ResourceHandle handle);
    /** Never culled: work whose effect is outside the graph (uploads, a callback to the page). */
    PassBuilder& SideEffect();
    PassId Id() const { return _pass; }

private:
    Graph* _graph;
    PassId _pass;
};

struct PassDesc {
    std::string name;   // unique in the frame, e.g. "scene:Earth/0"
    std::string kind;   // what timers and the HUD sum over, e.g. "scene"
    int stage = -1;     // FrameProfile::Stage it is billed to (-1 none)
    int timerSlot = -1; // set by whoever registered `kind` with the pass timers
};

struct Access {
    ResourceHandle handle;
    bool load = true; // Write (true) or Overwrite (false); ignored for reads
};

struct Pass {
    PassDesc desc;
    ExecuteFn execute;
    std::vector<Access> reads;
    std::vector<Access> writes;
    bool sideEffect = false;
    // Compile() results.
    bool culled = false;
    std::uint32_t order = kInvalid; // position in the compiled order
};

struct Resource {
    std::string name;
    rhi::TextureDesc desc;
    bool imported = false;
    bool output = false;
    std::uint32_t versions = 1; // version 0 exists from the start
    // Compile() results for transients (imported resources keep kInvalid).
    std::uint32_t firstUse = kInvalid; // compiled order index
    std::uint32_t lastUse = kInvalid;
    std::uint32_t physical = kInvalid; // aliasing slot
};

class Graph {
public:
    /** Forget every pass and resource; keeps capacity, so a graph rebuilt per frame does not allocate. */
    void Reset();

    /** A resource owned outside the graph (the backbuffer, the shadow map, particle buffers). */
    ResourceHandle Import(std::string name, const rhi::TextureDesc& desc, ImportFlags flags = {});
    /** A transient the graph places (and may alias with another of the same desc). */
    ResourceHandle Create(std::string name, const rhi::TextureDesc& desc);

    PassBuilder AddPass(PassDesc desc, ExecuteFn execute);

    /** Cull, order and place transients. False (and Errors() says why) on an invalid graph. */
    bool Compile();
    /** Run the compiled passes in order. Compile() first. */
    void Execute(rhi::CommandList& commands) const;

    const std::vector<Pass>& Passes() const { return _passes; }
    const std::vector<Resource>& Resources() const { return _resources; }
    /** Surviving passes in execution order. */
    const std::vector<PassId>& Order() const { return _order; }
    std::size_t CulledCount() const { return _passes.size() - _order.size(); }
    /** Physical slots the transients need after aliasing. */
    std::uint32_t PhysicalCount() const { return _physicalCount; }
    /** The physical slot of a transient resource (kInvalid if it was never used). */
    std::uint32_t PhysicalOf(ResourceHandle handle) const;
    const std::vector<std::string>& Errors() const { return _errors; }

    /** A surviving pass with this name, else nullptr. */
    const Pass* FindLive(const std::string& name) const;
    /** Surviving passes of `kind`. */
    std::size_t CountLive(const std::string& kind) const;

    /** Graphviz: passes as boxes (culled ones dashed), resource versions as ellipses. */
    std::string ToDot() const;
    /** {"passes":[…],"resources":[…],"physical":n} for the console and tooling. */
    std::string ToJson() const;
    /** Pass names of the compiled order joined with ',': cheap identity to dump only on change. */
    std::string Signature() const;

private:
    friend class PassBuilder;
    void AddRead(PassId pass, ResourceHandle handle);
    ResourceHandle AddWrite(PassId pass, ResourceHandle handle, bool load);
    void Error(std::string message);

    std::vector<Pass> _passes;
    std::vector<Resource> _resources;
    std::vector<PassId> _order;
    std::uint32_t _physicalCount = 0;
    std::vector<std::string> _errors;
};

} // namespace FrameGraph

#endif // SOLARSYSTEM_RENDER_GRAPH_FRAMEGRAPH_H
