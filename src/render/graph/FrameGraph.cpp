#include "FrameGraph.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace FrameGraph {

namespace {

std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

std::string VersionName(const Resource& resource, std::uint32_t version) {
    return resource.name + "#" + std::to_string(version);
}

const char* FormatName(rhi::TextureFormat format) {
    switch (format) {
        case rhi::TextureFormat::Unknown: return "unknown";
        case rhi::TextureFormat::Rgba8: return "rgba8";
        case rhi::TextureFormat::Rgba16F: return "rgba16f";
        case rhi::TextureFormat::Depth24: return "depth24";
        case rhi::TextureFormat::Depth32F: return "depth32f";
        case rhi::TextureFormat::Buffer: return "buffer";
    }
    return "unknown";
}

} // namespace

PassBuilder& PassBuilder::Read(ResourceHandle handle) {
    _graph->AddRead(_pass, handle);
    return *this;
}

ResourceHandle PassBuilder::Write(ResourceHandle handle) {
    return _graph->AddWrite(_pass, handle, true);
}

ResourceHandle PassBuilder::Overwrite(ResourceHandle handle) {
    return _graph->AddWrite(_pass, handle, false);
}

PassBuilder& PassBuilder::SideEffect() {
    _graph->_passes[_pass].sideEffect = true;
    return *this;
}

void Graph::Reset() {
    _passes.clear();
    _resources.clear();
    _order.clear();
    _errors.clear();
    _physicalCount = 0;
}

ResourceHandle Graph::Import(std::string name, const rhi::TextureDesc& desc, ImportFlags flags) {
    Resource resource;
    resource.name = std::move(name);
    resource.desc = desc;
    resource.imported = true;
    resource.output = flags.output;
    _resources.push_back(std::move(resource));
    return {static_cast<std::uint32_t>(_resources.size() - 1), 0};
}

ResourceHandle Graph::Create(std::string name, const rhi::TextureDesc& desc) {
    Resource resource;
    resource.name = std::move(name);
    resource.desc = desc;
    _resources.push_back(std::move(resource));
    return {static_cast<std::uint32_t>(_resources.size() - 1), 0};
}

PassBuilder Graph::AddPass(PassDesc desc, ExecuteFn execute) {
    Pass pass;
    pass.desc = std::move(desc);
    pass.execute = std::move(execute);
    _passes.push_back(std::move(pass));
    return PassBuilder(*this, static_cast<PassId>(_passes.size() - 1));
}

void Graph::Error(std::string message) {
    _errors.push_back(std::move(message));
}

void Graph::AddRead(PassId pass, ResourceHandle handle) {
    if (!handle.Valid() || handle.index >= _resources.size()) {
        Error(_passes[pass].desc.name + " reads an invalid handle");
        return;
    }
    const Resource& resource = _resources[handle.index];
    if (handle.version >= resource.versions) {
        Error(_passes[pass].desc.name + " reads " + VersionName(resource, handle.version) + ", which nothing wrote");
        return;
    }
    if (!resource.imported && handle.version == 0) {
        Error(_passes[pass].desc.name + " reads transient " + resource.name + " before anything wrote it");
        return;
    }
    _passes[pass].reads.push_back({handle, true});
}

ResourceHandle Graph::AddWrite(PassId pass, ResourceHandle handle, bool load) {
    if (!handle.Valid() || handle.index >= _resources.size()) {
        Error(_passes[pass].desc.name + " writes an invalid handle");
        return {};
    }
    Resource& resource = _resources[handle.index];
    if (handle.version + 1 != resource.versions) {
        Error(_passes[pass].desc.name + " writes " + VersionName(resource, handle.version) +
              ", which is not the latest version (" + std::to_string(resource.versions - 1) + ")");
        return {};
    }
    if (load && !resource.imported && handle.version == 0) {
        Error(_passes[pass].desc.name + " draws onto transient " + resource.name +
              " before anything wrote it (Overwrite it instead)");
        return {};
    }
    const ResourceHandle next{handle.index, resource.versions++};
    _passes[pass].writes.push_back({next, load});
    return next;
}

bool Graph::Compile() {
    _order.clear();
    _physicalCount = 0;
    if (!_errors.empty()) {
        return false;
    }
    const std::size_t passCount = _passes.size();
    // Who wrote each version, and who reads it.
    std::vector<std::vector<PassId>> producer(_resources.size());
    std::vector<std::vector<std::vector<PassId>>> readers(_resources.size());
    for (std::size_t r = 0; r < _resources.size(); ++r) {
        producer[r].assign(_resources[r].versions, kInvalid);
        readers[r].resize(_resources[r].versions);
    }
    for (PassId p = 0; p < passCount; ++p) {
        for (const Access& write : _passes[p].writes) {
            producer[write.handle.index][write.handle.version] = p;
        }
        for (const Access& read : _passes[p].reads) {
            readers[read.handle.index][read.handle.version].push_back(p);
        }
    }

    // Cull: everything a root needs, transitively.
    std::vector<bool> alive(passCount, false);
    std::vector<PassId> work;
    for (PassId p = 0; p < passCount; ++p) {
        Pass& pass = _passes[p];
        bool root = pass.sideEffect;
        for (const Access& write : pass.writes) {
            root = root || _resources[write.handle.index].output;
        }
        if (root) {
            alive[p] = true;
            work.push_back(p);
        }
    }
    const auto need = [&](std::uint32_t resource, std::uint32_t version) {
        if (version == 0) {
            return;
        }
        const PassId writer = producer[resource][version];
        if (writer != kInvalid && !alive[writer]) {
            alive[writer] = true;
            work.push_back(writer);
        }
    };
    while (!work.empty()) {
        const PassId p = work.back();
        work.pop_back();
        for (const Access& read : _passes[p].reads) {
            need(read.handle.index, read.handle.version);
        }
        for (const Access& write : _passes[p].writes) {
            if (write.load) {
                need(write.handle.index, write.handle.version - 1);
            }
        }
    }

    // Order: Kahn's algorithm, always taking the earliest-declared ready pass.
    std::vector<std::vector<PassId>> successors(passCount);
    std::vector<std::uint32_t> indegree(passCount, 0);
    const auto edge = [&](PassId from, PassId to) {
        if (from == kInvalid || to == kInvalid || from == to || !alive[from] || !alive[to]) {
            return;
        }
        successors[from].push_back(to);
        ++indegree[to];
    };
    for (std::size_t r = 0; r < _resources.size(); ++r) {
        for (std::uint32_t v = 0; v < _resources[r].versions; ++v) {
            const PassId writer = producer[r][v];
            for (const PassId reader : readers[r][v]) {
                edge(writer, reader); // read after write
            }
            if (v + 1 < _resources[r].versions) {
                const PassId next = producer[r][v + 1];
                edge(writer, next); // write after write
                for (const PassId reader : readers[r][v]) {
                    edge(reader, next); // write after read
                }
            }
        }
    }
    std::priority_queue<PassId, std::vector<PassId>, std::greater<>> ready;
    std::size_t aliveCount = 0;
    for (PassId p = 0; p < passCount; ++p) {
        _passes[p].culled = !alive[p];
        _passes[p].order = kInvalid;
        if (alive[p]) {
            ++aliveCount;
            if (indegree[p] == 0) {
                ready.push(p);
            }
        }
    }
    while (!ready.empty()) {
        const PassId p = ready.top();
        ready.pop();
        _passes[p].order = static_cast<std::uint32_t>(_order.size());
        _order.push_back(p);
        for (const PassId next : successors[p]) {
            if (--indegree[next] == 0) {
                ready.push(next);
            }
        }
    }
    if (_order.size() != aliveCount) {
        Error("the passes' resource accesses form a cycle");
        _order.clear();
        return false;
    }

    // Lifetimes of transients over the surviving passes, then greedy aliasing by desc.
    for (Resource& resource : _resources) {
        resource.firstUse = kInvalid;
        resource.lastUse = kInvalid;
        resource.physical = kInvalid;
    }
    for (std::uint32_t position = 0; position < _order.size(); ++position) {
        const Pass& pass = _passes[_order[position]];
        const auto touch = [&](const Access& access) {
            Resource& resource = _resources[access.handle.index];
            if (resource.imported) {
                return;
            }
            resource.firstUse = std::min(resource.firstUse, position);
            resource.lastUse = resource.lastUse == kInvalid ? position : std::max(resource.lastUse, position);
        };
        std::for_each(pass.reads.begin(), pass.reads.end(), touch);
        std::for_each(pass.writes.begin(), pass.writes.end(), touch);
    }
    std::vector<std::uint32_t> transients;
    for (std::uint32_t r = 0; r < _resources.size(); ++r) {
        if (!_resources[r].imported && _resources[r].firstUse != kInvalid) {
            transients.push_back(r);
        }
    }
    std::stable_sort(transients.begin(), transients.end(),
                     [&](std::uint32_t a, std::uint32_t b) { return _resources[a].firstUse < _resources[b].firstUse; });
    struct Slot {
        rhi::TextureDesc desc;
        std::uint32_t lastUse;
    };
    std::vector<Slot> slots;
    for (const std::uint32_t r : transients) {
        Resource& resource = _resources[r];
        std::uint32_t chosen = kInvalid;
        for (std::uint32_t s = 0; s < slots.size(); ++s) {
            if (slots[s].desc == resource.desc && slots[s].lastUse < resource.firstUse) {
                chosen = s;
                break;
            }
        }
        if (chosen == kInvalid) {
            chosen = static_cast<std::uint32_t>(slots.size());
            slots.push_back({resource.desc, resource.lastUse});
        } else {
            slots[chosen].lastUse = resource.lastUse;
        }
        resource.physical = chosen;
    }
    _physicalCount = static_cast<std::uint32_t>(slots.size());
    return true;
}

void Graph::Execute(rhi::CommandList& commands) const {
    for (const PassId id : _order) {
        const Pass& pass = _passes[id];
        rhi::PassInfo info;
        info.name = pass.desc.name.c_str();
        info.kind = pass.desc.kind.c_str();
        info.timerSlot = pass.desc.timerSlot;
        info.stage = pass.desc.stage;
        commands.BeginPass(info);
        if (pass.execute) {
            pass.execute(commands);
        }
        commands.EndPass();
    }
}

std::uint32_t Graph::PhysicalOf(ResourceHandle handle) const {
    return handle.index < _resources.size() ? _resources[handle.index].physical : kInvalid;
}

const Pass* Graph::FindLive(const std::string& name) const {
    for (const PassId id : _order) {
        if (_passes[id].desc.name == name) {
            return &_passes[id];
        }
    }
    return nullptr;
}

std::size_t Graph::CountLive(const std::string& kind) const {
    return static_cast<std::size_t>(
        std::count_if(_order.begin(), _order.end(), [&](PassId id) { return _passes[id].desc.kind == kind; }));
}

std::string Graph::Signature() const {
    std::string signature;
    for (const PassId id : _order) {
        if (!signature.empty()) {
            signature += ',';
        }
        signature += _passes[id].desc.name;
    }
    return signature;
}

std::string Graph::ToDot() const {
    std::string dot = "digraph FrameGraph {\n  rankdir=LR;\n  node [fontname=\"Helvetica\", fontsize=10];\n";
    for (PassId p = 0; p < _passes.size(); ++p) {
        const Pass& pass = _passes[p];
        std::string label = pass.desc.name;
        if (!pass.culled) {
            label = std::to_string(pass.order) + ": " + label;
        }
        dot += "  p" + std::to_string(p) + " [shape=box, label=" + Quote(label) +
               (pass.culled ? ", style=dashed, fontcolor=gray, color=gray" : "") + "];\n";
    }
    for (std::uint32_t r = 0; r < _resources.size(); ++r) {
        const Resource& resource = _resources[r];
        for (std::uint32_t v = 0; v < resource.versions; ++v) {
            dot += "  r" + std::to_string(r) + "_" + std::to_string(v) +
                   " [shape=ellipse, label=" + Quote(VersionName(resource, v)) +
                   (resource.imported ? "" : ", style=filled, fillcolor=lightyellow") +
                   (resource.output ? ", peripheries=2" : "") + "];\n";
        }
    }
    for (PassId p = 0; p < _passes.size(); ++p) {
        for (const Access& read : _passes[p].reads) {
            dot += "  r" + std::to_string(read.handle.index) + "_" + std::to_string(read.handle.version) + " -> p" +
                   std::to_string(p) + ";\n";
        }
        for (const Access& write : _passes[p].writes) {
            dot += "  p" + std::to_string(p) + " -> r" + std::to_string(write.handle.index) + "_" +
                   std::to_string(write.handle.version) + (write.load ? "" : " [style=bold]") + ";\n";
        }
    }
    dot += "}\n";
    return dot;
}

std::string Graph::ToJson() const {
    std::string json = "{\"passes\":[";
    for (PassId p = 0; p < _passes.size(); ++p) {
        const Pass& pass = _passes[p];
        if (p) {
            json += ',';
        }
        json += "{\"name\":" + Quote(pass.desc.name) + ",\"kind\":" + Quote(pass.desc.kind) +
                ",\"stage\":" + std::to_string(pass.desc.stage) + ",\"culled\":" + (pass.culled ? "true" : "false");
        if (!pass.culled) {
            json += ",\"order\":" + std::to_string(pass.order);
        }
        if (pass.sideEffect) {
            json += ",\"sideEffect\":true";
        }
        json += ",\"reads\":[";
        for (std::size_t i = 0; i < pass.reads.size(); ++i) {
            json += (i ? "," : "") +
                    Quote(VersionName(_resources[pass.reads[i].handle.index], pass.reads[i].handle.version));
        }
        json += "],\"writes\":[";
        for (std::size_t i = 0; i < pass.writes.size(); ++i) {
            json += (i ? "," : "") +
                    Quote(VersionName(_resources[pass.writes[i].handle.index], pass.writes[i].handle.version));
        }
        json += "]}";
    }
    json += "],\"resources\":[";
    for (std::uint32_t r = 0; r < _resources.size(); ++r) {
        const Resource& resource = _resources[r];
        if (r) {
            json += ',';
        }
        json += "{\"name\":" + Quote(resource.name) + ",\"format\":\"" + FormatName(resource.desc.format) +
                "\",\"width\":" + std::to_string(resource.desc.width) +
                ",\"height\":" + std::to_string(resource.desc.height) +
                ",\"imported\":" + (resource.imported ? "true" : "false") +
                ",\"output\":" + (resource.output ? "true" : "false") +
                ",\"versions\":" + std::to_string(resource.versions);
        if (resource.physical != kInvalid) {
            json += ",\"physical\":" + std::to_string(resource.physical) +
                    ",\"firstUse\":" + std::to_string(resource.firstUse) +
                    ",\"lastUse\":" + std::to_string(resource.lastUse);
        }
        json += '}';
    }
    json += "],\"physical\":" + std::to_string(_physicalCount) + ",\"culled\":" + std::to_string(CulledCount()) + "}";
    return json;
}

} // namespace FrameGraph
