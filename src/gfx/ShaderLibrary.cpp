#include "ShaderLibrary.h"
#include <stdexcept>

ShaderLibrary::Entry& ShaderLibrary::Submit(const ShaderSpec& spec) {
    const auto found = _entries.find(spec);
    if (found != _entries.end()) {
        return found->second;
    }
    Entry& entry = _entries[spec];
    try {
        entry.shader = std::make_unique<Shader>(Shader::Submit(spec));
    } catch (const std::exception& e) {
        entry.error = e.what(); // already printed by Shader
    }
    return entry;
}

Shader* ShaderLibrary::Finish(Entry& entry) {
    if (!entry.shader) {
        return nullptr;
    }
    try {
        entry.shader->Finish();
    } catch (const std::exception& e) {
        entry.error = e.what();
        entry.shader.reset();
        return nullptr;
    }
    return entry.shader.get();
}

void ShaderLibrary::Prefetch(const ShaderSpec& spec) {
    Submit(spec);
}

Shader& ShaderLibrary::Get(const ShaderSpec& spec) {
    Entry& entry = Submit(spec);
    if (Shader* shader = Finish(entry)) {
        return *shader;
    }
    throw std::runtime_error("shader " + spec.Describe() + " is unavailable: " + entry.error);
}

Shader* ShaderLibrary::TryGet(const ShaderSpec& spec) {
    return Finish(Submit(spec));
}

Shader* ShaderLibrary::GetIfReady(const ShaderSpec& spec) {
    Entry& entry = Submit(spec);
    if (!entry.shader || !entry.shader->IsReady()) {
        return nullptr;
    }
    return Finish(entry);
}
