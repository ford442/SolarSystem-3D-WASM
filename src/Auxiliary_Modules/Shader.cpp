#include "Shader.h"
#include "GlCapabilities.h"
#include "../gfx/FrameUniforms.h"
#include <algorithm>
#include <cstring>
#include <set>

#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

namespace {

// Shader sources are authored once as GLSL ES 3.00 (the only dialect WebGL 2 accepts) and
// assembled by ShaderSource: the native build rewrites `#version 300 es` to
// `#version 460 core`, the shared common/preamble.glsl is inserted and
// `#include "common/…"` lines are expanded. See docs/ARCHITECTURE.md §9.1.
ShaderSource::Result AssembleShader(const std::string& path, ShaderSource::Stage stage,
                                    const std::vector<std::string>& defines,
                                    ShaderSource::Dialect dialect = ShaderSource::NativeDialect()) {
    ShaderSource::Result result =
        ShaderSource::Preprocess(path, stage, dialect, ShaderSource::ReadFileFromDisk, defines);
    if (!result.ok) {
        std::cerr << "ERROR::SHADER::PREPROCESS_FAILED: " << result.error << std::endl;
        throw std::runtime_error("ERROR::SHADER::PREPROCESS_FAILED: " + result.error);
    }
    return result;
}

std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void CompileStage(GlShaderStage& stage, GLenum type, const ShaderSource::Result& source) {
    const char* code = source.code.c_str();
    stage = GlShaderStage::Create(type);
    glShaderSource(stage.get(), 1, &code, nullptr);
    glCompileShader(stage.get());
}

} // namespace

ShaderSpec::ShaderSpec(std::string vertexPath, std::string fragmentPath, std::vector<std::string> permutationDefines,
                       std::string geometryPath)
    : vertex(std::move(vertexPath)), fragment(std::move(fragmentPath)), geometry(std::move(geometryPath)),
      defines(std::move(permutationDefines)) {
    std::sort(defines.begin(), defines.end());
    defines.erase(std::unique(defines.begin(), defines.end()), defines.end());
}

std::string ShaderSpec::Describe() const {
    std::string description = BaseName(vertex) + " + " + BaseName(fragment);
    if (!geometry.empty()) {
        description += " + " + BaseName(geometry);
    }
    if (!defines.empty()) {
        description += " [";
        for (size_t i = 0; i < defines.size(); ++i) {
            description += (i > 0 ? " " : "") + defines[i];
        }
        description += "]";
    }
    return description;
}

struct Shader::Pending {
    ShaderSource::Result vertexSource, fragmentSource, geometrySource;
    GlShaderStage vertex, fragment, geometry;
};

std::vector<Shader*>& Shader::Registry() {
    // Leaked on purpose: a Shader may outlive static destruction order.
    static auto* registry = new std::vector<Shader*>();
    return *registry;
}

Shader::Shader() {
    Registry().push_back(this);
}

Shader::Shader(ShaderSpec spec) : Shader() {
    _spec = std::move(spec);
    SubmitStages();
    Finish();
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath, std::vector<std::string> defines)
    : Shader(ShaderSpec(vertexPath, fragmentPath, std::move(defines))) {}

#ifndef __EMSCRIPTEN__
Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath)
    : Shader(ShaderSpec(vertexPath, fragmentPath, {}, geometryPath)) {}
#endif

Shader Shader::Submit(ShaderSpec spec) {
    Shader shader;
    shader._spec = std::move(spec);
    shader.SubmitStages();
    return shader;
}

Shader::Shader(Shader&& other) noexcept : Shader() {
    *this = std::move(other);
}

Shader& Shader::operator=(Shader&& other) noexcept = default;

Shader::~Shader() {
    std::vector<Shader*>& registry = Registry();
    registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
}

void Shader::SubmitStages() {
#ifdef __EMSCRIPTEN__
    // Defence in depth behind the missing 3-arg constructor: nothing in the web build can
    // reach a geometry stage, because WebGL 2 does not have one.
    if (!_spec.geometry.empty()) {
        throw std::runtime_error("geometry shaders are not available in WebGL 2: " + _spec.geometry);
    }
#endif
    auto pending = std::make_unique<Pending>();
    pending->vertexSource = AssembleShader(_spec.vertex, ShaderSource::Stage::Vertex, _spec.defines);
    pending->fragmentSource = AssembleShader(_spec.fragment, ShaderSource::Stage::Fragment, _spec.defines);
    if (!_spec.geometry.empty()) {
        pending->geometrySource = AssembleShader(_spec.geometry, ShaderSource::Stage::Geometry, _spec.defines);
    }

    _sourceFiles.clear();
    for (const ShaderSource::Result* source : {&pending->vertexSource, &pending->fragmentSource, &pending->geometrySource}) {
        for (const std::string& name : source->sourceNames) {
            const bool known = std::any_of(_sourceFiles.begin(), _sourceFiles.end(),
                                           [&](const auto& file) { return file.first == name; });
            if (!known) {
                _sourceFiles.emplace_back(name, ShaderSource::FileModificationTime(name).value_or(-1));
            }
        }
    }

    // Compile and link without asking for any status: a status query is what makes the
    // driver wait, so it is left to Finish() (KHR_parallel_shader_compile).
    CompileStage(pending->vertex, GL_VERTEX_SHADER, pending->vertexSource);
    CompileStage(pending->fragment, GL_FRAGMENT_SHADER, pending->fragmentSource);
#ifndef __EMSCRIPTEN__
    if (!_spec.geometry.empty()) {
        CompileStage(pending->geometry, GL_GEOMETRY_SHADER, pending->geometrySource);
    }
#endif
    _program = GlProgram::Create();
    const GLuint program = _program.get();
    glAttachShader(program, pending->vertex.get());
    glAttachShader(program, pending->fragment.get());
    if (pending->geometry) {
        glAttachShader(program, pending->geometry.get());
    }
    glLinkProgram(program);
    _pending = std::move(pending);
}

bool Shader::IsReady() const {
    if (!_pending || !GetGlCapabilities().parallelShaderCompile) {
        return true;
    }
    GLint done = GL_FALSE;
    glGetProgramiv(_program.get(), GL_COMPLETION_STATUS_KHR, &done);
    return done == GL_TRUE;
}

void Shader::Finish() {
    if (!_pending) {
        return;
    }
    // Taken out first, so a throw below leaves a finished (if unusable) Shader.
    const std::unique_ptr<Pending> pending = std::move(_pending);
    CheckCompileErrors(pending->vertex.get(), ShaderType::VertexShader, _spec.vertex, &pending->vertexSource);
    CheckCompileErrors(pending->fragment.get(), ShaderType::FragmentShader, _spec.fragment, &pending->fragmentSource);
    if (pending->geometry) {
        CheckCompileErrors(pending->geometry.get(), ShaderType::GeometryShader, _spec.geometry,
                           &pending->geometrySource);
    }
    const GLuint program = _program.get();
    CheckCompileErrors(program, ShaderType::ShaderProgram, _spec.Describe());

    glDetachShader(program, pending->vertex.get());
    glDetachShader(program, pending->fragment.get());
    if (pending->geometry) {
        glDetachShader(program, pending->geometry.get());
    }

    std::vector<ShaderSource::SamplerBinding> samplerBindings = pending->vertexSource.samplerBindings;
    for (const ShaderSource::Result* source : {&pending->fragmentSource, &pending->geometrySource}) {
        samplerBindings.insert(samplerBindings.end(), source->samplerBindings.begin(), source->samplerBindings.end());
    }
    Introspect(samplerBindings);
}

void Shader::Introspect(const std::vector<ShaderSource::SamplerBinding>& samplerBindings) {
    const GLuint program = _program.get();
    GLint count = 0;
    GLint maxLength = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    std::vector<char> nameBuffer(static_cast<size_t>(std::max(maxLength, 1)));
    _slots.clear();
    _uniformIndex.clear();
    for (GLint i = 0; i < count; ++i) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, static_cast<GLuint>(i), static_cast<GLsizei>(nameBuffer.size()), &length, &size,
                           &type, nameBuffer.data());
        std::string name(nameBuffer.data(), static_cast<size_t>(length));
        const GLint location = glGetUniformLocation(program, name.c_str());
        if (location < 0) {
            continue; // a uniform-block member
        }
        const auto slot = static_cast<unsigned int>(_slots.size());
        _slots.push_back(UniformSlot{location, type});
        if (name.size() > 3 && name.compare(name.size() - 3, 3, "[0]") == 0) {
            _uniformIndex.emplace_back(name.substr(0, name.size() - 3), slot);
        }
        _uniformIndex.emplace_back(std::move(name), slot);
    }
    std::sort(_uniformIndex.begin(), _uniformIndex.end());

    // SetInt binds the program for the call and restores the current one.
    for (const ShaderSource::SamplerBinding& binding : samplerBindings) {
        SetInt(binding.uniform, binding.unit);
    }

    const GLuint frameBlock = glGetUniformBlockIndex(program, "FrameUBO");
    _usesFrameUniforms = frameBlock != GL_INVALID_INDEX;
    if (_usesFrameUniforms) {
        glUniformBlockBinding(program, frameBlock, FrameUniformBuffer::kBinding);
    }
}

Shader::UniformSlot* Shader::FindSlot(std::string_view name) const {
    const auto it = std::lower_bound(_uniformIndex.begin(), _uniformIndex.end(), name,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    return it != _uniformIndex.end() && it->first == name ? &_slots[it->second] : nullptr;
}

template <class T>
bool Shader::Store(UniformSlot& slot, const T& value) {
    static_assert(sizeof(T) <= sizeof(UniformSlot::value), "uniform value larger than a mat4");
    if (slot.size == sizeof(T) && std::memcmp(slot.value, &value, sizeof(T)) == 0) {
        return false;
    }
    std::memcpy(slot.value, &value, sizeof(T));
    slot.size = static_cast<unsigned char>(sizeof(T));
    return true;
}

void Shader::Use() const {
    if (_pending) {
        // Everything is normally finished before it draws (ShaderLibrary::Get); this keeps a
        // stray Submit()ted Shader correct instead of drawing with an unchecked program.
        const_cast<Shader*>(this)->Finish();
    }
    glUseProgram(_program.get());
    s_currentProgram = _program.get();
}

template <class F>
void Shader::WithProgramBound(F&& issue) const {
    const GLuint current = s_currentProgram;
    const bool foreign = current != _program.get();
    if (foreign) {
        glUseProgram(_program.get());
    }
    ++s_uniformCalls;
    issue();
    if (foreign) {
        glUseProgram(current);
    }
}

// Every setter: find the slot, skip an unchanged value, and only then count and issue the
// call. glUniform* writes to the *current* program, so a setter on a Shader that is not the
// one last Use()d binds its own program for the call and puts the current one back; the
// value then lands where the caller meant it, and the slot cache stays true.
#define SOLARSYSTEM_SHADER_SET(stored, call)   \
    UniformSlot* slot = FindSlot(name);        \
    if (slot && Store(*slot, stored)) {        \
        const GLint location = slot->location; \
        WithProgramBound([&] { call; });       \
    }

void Shader::SetBool(std::string_view name, bool value) const {
    // Stored as the int GL keeps, so SetBool(true) and SetInt(1) agree.
    const int asInt = value ? 1 : 0;
    SOLARSYSTEM_SHADER_SET(asInt, glUniform1i(location, asInt))
}

void Shader::SetInt(std::string_view name, int value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform1i(location, value))
}

void Shader::SetFloat(std::string_view name, float value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform1f(location, value))
}

void Shader::SetVec2(std::string_view name, const glm::vec2& value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform2fv(location, 1, &value[0]))
}

void Shader::SetVec2(std::string_view name, float x, float y) const {
    SetVec2(name, glm::vec2(x, y));
}

void Shader::SetVec3(std::string_view name, const glm::vec3& value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform3fv(location, 1, &value[0]))
}

void Shader::SetVec3(std::string_view name, float x, float y, float z) const {
    SetVec3(name, glm::vec3(x, y, z));
}

void Shader::SetVec4(std::string_view name, const glm::vec4& value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform4fv(location, 1, &value[0]))
}

void Shader::SetMat3(std::string_view name, const glm::mat3& mat) const {
    SOLARSYSTEM_SHADER_SET(mat, glUniformMatrix3fv(location, 1, GL_FALSE, &mat[0][0]))
}

void Shader::SetMat4(std::string_view name, const glm::mat4& mat) const {
    SOLARSYSTEM_SHADER_SET(mat, glUniformMatrix4fv(location, 1, GL_FALSE, &mat[0][0]))
}

#undef SOLARSYSTEM_SHADER_SET

void Shader::Apply(const UniformSlot& slot) const {
    const auto* floats = reinterpret_cast<const GLfloat*>(slot.value);
    const auto* ints = reinterpret_cast<const GLint*>(slot.value);
    const GLint location = slot.location;
    const auto issue = [&](size_t bytes, auto&& call) {
        if (slot.size == bytes) {
            WithProgramBound(call);
        }
    };
    switch (slot.type) {
        case GL_BOOL:
        case GL_INT: issue(sizeof(GLint), [&] { glUniform1iv(location, 1, ints); }); break;
        case GL_FLOAT: issue(sizeof(GLfloat), [&] { glUniform1fv(location, 1, floats); }); break;
        case GL_FLOAT_VEC2: issue(sizeof(glm::vec2), [&] { glUniform2fv(location, 1, floats); }); break;
        case GL_FLOAT_VEC3: issue(sizeof(glm::vec3), [&] { glUniform3fv(location, 1, floats); }); break;
        case GL_FLOAT_VEC4: issue(sizeof(glm::vec4), [&] { glUniform4fv(location, 1, floats); }); break;
        case GL_FLOAT_MAT3: issue(sizeof(glm::mat3), [&] { glUniformMatrix3fv(location, 1, GL_FALSE, floats); }); break;
        case GL_FLOAT_MAT4: issue(sizeof(glm::mat4), [&] { glUniformMatrix4fv(location, 1, GL_FALSE, floats); }); break;
        default: break; // samplers: Introspect already gave them their unit
    }
}

void Shader::RefreshSourceTimes() {
    for (auto& [path, mtime] : _sourceFiles) {
        mtime = ShaderSource::FileModificationTime(path).value_or(-1);
    }
}

bool Shader::SourcesChanged(std::vector<std::pair<std::string, long long>>& cache) const {
    for (const auto& file : _sourceFiles) {
        const std::string& path = file.first;
        auto it = std::find_if(cache.begin(), cache.end(), [&](const auto& entry) { return entry.first == path; });
        if (it == cache.end()) {
            cache.emplace_back(path, ShaderSource::FileModificationTime(path).value_or(-1));
            it = cache.end() - 1;
        }
        if (it->second != file.second) {
            return true;
        }
    }
    return false;
}

bool Shader::Reload() {
    Shader fresh;
    fresh._spec = _spec;
    try {
        fresh.SubmitStages();
        fresh.Finish();
    } catch (const std::exception&) {
        // Already printed. Remember these mtimes, so the next poll waits for another save.
        RefreshSourceTimes();
        std::cerr << "[Shader] hot reload of " << _spec.Describe() << " failed; keeping the previous program"
                  << std::endl;
        return false;
    }
    // Uniforms that were set once (at init, or on a toggle) would otherwise read 0 until the
    // next change; carry every value this Shader gave over to the same name and type.
    for (const auto& [name, oldIndex] : _uniformIndex) {
        const UniformSlot& old = _slots[oldIndex];
        UniformSlot* slot = old.size != 0 ? fresh.FindSlot(name) : nullptr;
        if (!slot || slot->type != old.type || slot->size != 0) {
            continue; // gone, retyped, or already set (a sampler, or an array's other name)
        }
        std::memcpy(slot->value, old.value, old.size);
        slot->size = old.size;
        fresh.Apply(*slot);
    }
    *this = std::move(fresh);
    return true;
}

int Shader::ReloadChanged() {
    std::vector<std::pair<std::string, long long>> stats;
    std::vector<Shader*> changed;
    for (Shader* shader : Registry()) {
        if (shader->_program && !shader->_pending && shader->SourcesChanged(stats)) {
            changed.push_back(shader);
        }
    }
    if (changed.empty()) {
        return 0;
    }
    // The program about to be replaced may be the current one; nothing may still name it.
    glUseProgram(0);
    s_currentProgram = 0;
    int reloaded = 0;
    for (Shader* shader : changed) { // Reload() registers a temporary, so not over Registry()
        if (shader->Reload()) {
            ++reloaded;
            std::cout << "[Shader] reloaded " << shader->_spec.Describe() << std::endl;
        }
    }
    return reloaded;
}

std::vector<std::string> Shader::LiveSourceFiles() {
    std::set<std::string> files;
    for (const Shader* shader : Registry()) {
        for (const auto& file : shader->_sourceFiles) {
            files.insert(file.first);
        }
    }
    return {files.begin(), files.end()};
}

#ifndef __EMSCRIPTEN__
int Shader::SelfTestDirectory(const std::string& directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".vs" || extension == ".fs")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    // Both dialects: the native one, and GLSL ES 3.00 exactly as the web build assembles it.
    // Desktop drivers that expose ARB_ES3_compatibility (Mesa does) compile `#version 300 es`
    // too, so CI catches an ES-only error without a browser.
    const ShaderSource::Dialect dialects[] = {ShaderSource::Dialect::Desktop460, ShaderSource::Dialect::GlslEs300};
    int failures = 0;
    size_t compiled = 0;
    for (const ShaderSource::Dialect dialect : dialects) {
        const char* dialectName = dialect == ShaderSource::Dialect::Desktop460 ? "460 core" : "300 es";
        for (const auto& file : files) {
            const bool vertex = file.extension() == ".vs";
            const std::string path = directory + "/" + file.filename().string();
            const std::vector<std::string> permutations =
                ShaderSource::DeclaredPermutations(ShaderSource::ReadFileFromDisk(path).value_or(""));
            for (unsigned mask = 0; mask < (1u << permutations.size()); ++mask) {
                std::vector<std::string> defines;
                std::string label = path + " (" + dialectName;
                for (size_t bit = 0; bit < permutations.size(); ++bit) {
                    if (mask & (1u << bit)) {
                        defines.push_back(permutations[bit]);
                        label += " " + permutations[bit];
                    }
                }
                label += ")";
                try {
                    const ShaderSource::Result source = AssembleShader(
                        path, vertex ? ShaderSource::Stage::Vertex : ShaderSource::Stage::Fragment, defines, dialect);
                    GlShaderStage shader;
                    CompileStage(shader, vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER, source);
                    CheckCompileErrors(shader.get(), vertex ? ShaderType::VertexShader : ShaderType::FragmentShader,
                                       label, &source);
                    ++compiled;
                } catch (const std::exception&) {
                    ++failures; // already printed by AssembleShader / CheckCompileErrors
                }
            }
        }
    }
    std::cout << "[ShaderSelfTest] " << compiled << "/" << (compiled + static_cast<size_t>(failures))
              << " shader stages compiled (460 core + 300 es, every declared permutation)" << (failures == 0 ? " OK" : " - FAILURES") << std::endl;
    return failures;
}
#endif

void Shader::CheckCompileErrors(size_t shader, ShaderType type, const std::string& path,
                                const ShaderSource::Result* source) {
    int success;
    char infoLog[1024];
    if (type != ShaderType::ShaderProgram) {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER::COMPILATION_FAILED\nType: " << ShaderTypeToString(type)
                      << "\nPath: " << path << "\n" << infoLog;
            if (source) {
                // Errors are reported as <source string>:<line>; map the numbers back to
                // files, and dump the assembled text in case a driver numbers #line oddly.
                std::cerr << "Source strings:\n" << ShaderSource::DescribeSources(*source)
                          << "Assembled source:\n" << ShaderSource::NumberedListing(source->code);
            }
            std::cerr << "\n -- --------------------------------------------------- -- " << std::endl;
            throw std::runtime_error("ERROR::SHADER_COMPILATION_ERROR of type: " + ShaderTypeToString(type) + " " + path + "\n" + std::string(infoLog) + "\n -- --------------------------------------------------- -- ");
        }
    }
    else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER::PROGRAM_LINKING_FAILED\nType: " << ShaderTypeToString(type)
                      << "\nPath: " << path << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
            throw std::runtime_error("ERROR::PROGRAM_LINKING_ERROR of type: " + ShaderTypeToString(type) + "\n" + std::string(infoLog) + path + "\n -- --------------------------------------------------- --");
        }
    }
}

std::string Shader::ShaderTypeToString(ShaderType type) {
    switch(type) {
        case ShaderType::VertexShader: return "Vertex Shader";
        case ShaderType::FragmentShader: return "Fragment Shader";
        case ShaderType::GeometryShader: return "Geometry Shader";
        case ShaderType::ShaderProgram: return "Shader Program";
        default: return "";
    }
}
