#include "PlanetManifestLoader.h"
#include "../PlanetSystemManifest.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace PlanetManifestLoader {
namespace {

struct ParsedSystem {
    std::string name;
    std::string init;
    glm::vec3 proxyOffset{0.f};
    float activationRadius = 0.f;
    std::vector<std::string> required;
    std::vector<std::string> optional;
    std::vector<std::string> optionalHighRes;
};

struct ParsedManifest {
    int version = 0;
    std::vector<ParsedSystem> systems;
};

class JsonReader {
public:
    explicit JsonReader(std::string source) : _source(std::move(source)) {}

    bool parseManifest(ParsedManifest& out, std::string& error) {
        if (!consume('{')) {
            error = "Expected object at root";
            return false;
        }

        while (true) {
            skipWhitespace();
            if (consume('}')) break;

            std::string key;
            if (!parseString(key) || !consume(':')) {
                error = "Expected key in root object";
                return false;
            }

            if (key == "version") {
                double value = 0.0;
                if (!parseNumber(value)) {
                    error = "Invalid version";
                    return false;
                }
                out.version = static_cast<int>(value);
            } else if (key == "systems") {
                if (!parseSystems(out.systems, error)) return false;
            } else if (!skipValue()) {
                error = "Failed to skip unknown root key: " + key;
                return false;
            }

            skipWhitespace();
            if (consume('}')) break;
            if (!consume(',')) {
                error = "Expected ',' or '}' in root object";
                return false;
            }
        }

        return true;
    }

private:
    std::string _source;
    size_t _pos = 0;

    char peek() const {
        return _pos < _source.size() ? _source[_pos] : '\0';
    }

    void skipWhitespace() {
        while (_pos < _source.size() && std::isspace(static_cast<unsigned char>(_source[_pos]))) {
            ++_pos;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (_pos < _source.size() && _source[_pos] == expected) {
            ++_pos;
            return true;
        }
        return false;
    }

    bool parseString(std::string& out) {
        skipWhitespace();
        if (peek() != '"') return false;
        ++_pos;

        out.clear();
        while (_pos < _source.size()) {
            const char ch = _source[_pos++];
            if (ch == '"') return true;
            if (ch == '\\' && _pos < _source.size()) {
                const char escaped = _source[_pos++];
                switch (escaped) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(escaped); break;
                }
                continue;
            }
            out.push_back(ch);
        }
        return false;
    }

    bool parseNumber(double& out) {
        skipWhitespace();
        const size_t start = _pos;
        if (peek() == '-') ++_pos;
        while (_pos < _source.size() && std::isdigit(static_cast<unsigned char>(_source[_pos]))) ++_pos;
        if (peek() == '.') {
            ++_pos;
            while (_pos < _source.size() && std::isdigit(static_cast<unsigned char>(_source[_pos]))) ++_pos;
        }
        if (start == _pos) return false;

        try {
            out = std::stod(_source.substr(start, _pos - start));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool parseVec3(glm::vec3& out) {
        if (!consume('[')) return false;
        double x = 0.0, y = 0.0, z = 0.0;
        if (!parseNumber(x) || !consume(',') || !parseNumber(y) || !consume(',') || !parseNumber(z) || !consume(']')) {
            return false;
        }
        out = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return true;
    }

    bool parseStringArray(std::vector<std::string>& out) {
        if (!consume('[')) return false;
        out.clear();
        skipWhitespace();
        if (consume(']')) return true;

        while (true) {
            std::string value;
            if (!parseString(value)) return false;
            out.push_back(std::move(value));
            skipWhitespace();
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
    }

    bool skipValue() {
        skipWhitespace();
        const char ch = peek();
        if (ch == '"') {
            std::string ignored;
            return parseString(ignored);
        }
        if (ch == '{') {
            if (!consume('{')) return false;
            while (true) {
                skipWhitespace();
                if (consume('}')) return true;
                std::string key;
                if (!parseString(key) || !consume(':') || !skipValue()) return false;
                skipWhitespace();
                if (consume('}')) return true;
                if (!consume(',')) return false;
            }
        }
        if (ch == '[') {
            if (!consume('[')) return false;
            while (true) {
                skipWhitespace();
                if (consume(']')) return true;
                if (!skipValue()) return false;
                skipWhitespace();
                if (consume(']')) return true;
                if (!consume(',')) return false;
            }
        }
        if (ch == 't' || ch == 'f' || ch == 'n') {
            while (_pos < _source.size() && std::isalpha(static_cast<unsigned char>(_source[_pos]))) ++_pos;
            return true;
        }
        double ignored = 0.0;
        return parseNumber(ignored);
    }

    bool parseSystems(std::vector<ParsedSystem>& out, std::string& error) {
        if (!consume('[')) {
            error = "Expected systems array";
            return false;
        }

        skipWhitespace();
        if (consume(']')) return true;

        while (true) {
            ParsedSystem system;
            if (!parseSystem(system, error)) return false;
            out.push_back(std::move(system));

            skipWhitespace();
            if (consume(']')) return true;
            if (!consume(',')) {
                error = "Expected ',' or ']' in systems array";
                return false;
            }
        }
    }

    bool parseSystem(ParsedSystem& out, std::string& error) {
        if (!consume('{')) {
            error = "Expected system object";
            return false;
        }

        while (true) {
            skipWhitespace();
            if (consume('}')) return true;

            std::string key;
            if (!parseString(key) || !consume(':')) {
                error = "Expected key in system object";
                return false;
            }

            if (key == "name") {
                if (!parseString(out.name)) {
                    error = "Invalid system name";
                    return false;
                }
            } else if (key == "init") {
                if (!parseString(out.init)) {
                    error = "Invalid init tag";
                    return false;
                }
            } else if (key == "proxyPosition") {
                if (!parseVec3(out.proxyOffset)) {
                    error = "Invalid proxyPosition for " + out.name;
                    return false;
                }
            } else if (key == "activationRadius") {
                double radius = 0.0;
                if (!parseNumber(radius)) {
                    error = "Invalid activationRadius for " + out.name;
                    return false;
                }
                out.activationRadius = static_cast<float>(radius);
            } else if (key == "required") {
                if (!parseStringArray(out.required)) {
                    error = "Invalid required array for " + out.name;
                    return false;
                }
            } else if (key == "optional") {
                if (!parseStringArray(out.optional)) {
                    error = "Invalid optional array for " + out.name;
                    return false;
                }
            } else if (key == "optionalHighRes") {
                if (!parseStringArray(out.optionalHighRes)) {
                    error = "Invalid optionalHighRes array for " + out.name;
                    return false;
                }
            } else if (!skipValue()) {
                error = "Failed to skip unknown system key: " + key;
                return false;
            }

            skipWhitespace();
            if (consume('}')) return true;
            if (!consume(',')) {
                error = "Expected ',' or '}' in system object";
                return false;
            }
        }
    }
};

std::string ReadFileToString(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

std::vector<PlanetSystemManifest> LoadManifests(
    const std::string& path,
    const glm::vec3 sunPosition,
    const InitFuncFactory& makeInitFunc,
    int& outVersion,
    std::string& errorOut) {
    outVersion = 0;
    errorOut.clear();

    const std::string json = ReadFileToString(path);
    if (json.empty()) {
        errorOut = "Manifest file missing or empty: " + path;
        return {};
    }

    ParsedManifest parsed;
    JsonReader reader(json);
    if (!reader.parseManifest(parsed, errorOut)) {
        if (errorOut.empty()) errorOut = "Failed to parse manifest JSON";
        return {};
    }

    if (parsed.systems.empty()) {
        errorOut = "Manifest contains no systems";
        return {};
    }

    outVersion = parsed.version;

    std::vector<PlanetSystemManifest> manifests;
    manifests.reserve(parsed.systems.size());

    for (const ParsedSystem& system : parsed.systems) {
        if (system.name.empty() || system.init.empty()) {
            errorOut = "System entry missing name or init tag";
            return {};
        }
        if (system.required.empty()) {
            errorOut = "System " + system.name + " has no required assets";
            return {};
        }
        if (system.activationRadius <= 0.f) {
            errorOut = "System " + system.name + " has invalid activationRadius";
            return {};
        }

        std::function<void()> initFunc = makeInitFunc(system.init);
        if (!initFunc) {
            errorOut = "Unknown init tag '" + system.init + "' for system " + system.name;
            return {};
        }

        PlanetSystemManifest manifest;
        manifest.name = system.name;
        manifest.proxyPosition = sunPosition + system.proxyOffset;
        manifest.activationRadius = system.activationRadius;
        manifest.assetPaths = system.required;
        manifest.optionalAssetPaths = system.optional;
        manifest.optionalHighResAssetPaths = system.optionalHighRes;
        manifest.initFunc = std::move(initFunc);
        manifests.push_back(std::move(manifest));
    }

    return manifests;
}

} // namespace PlanetManifestLoader
