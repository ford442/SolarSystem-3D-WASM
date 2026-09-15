#include "MissionCatalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>
#include <glm/geometric.hpp>

namespace MissionCatalog {
namespace {

class JsonReader {
public:
    explicit JsonReader(std::string source) : _source(std::move(source)) {}

    bool parseCatalog(Catalog& out, std::string& error) {
        if (!consume('{')) {
            error = "Expected object at root";
            return false;
        }
        while (true) {
            skipWhitespace();
            if (consume('}')) {
                break;
            }
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
            } else if (key == "missions") {
                if (!parseMissions(out.missions, error)) {
                    return false;
                }
            } else if (!skipValue()) {
                error = "Failed to skip unknown root key: " + key;
                return false;
            }
            skipWhitespace();
            if (consume('}')) {
                break;
            }
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
        if (peek() != '"') {
            return false;
        }
        ++_pos;
        out.clear();
        while (_pos < _source.size()) {
            const char ch = _source[_pos++];
            if (ch == '"') {
                return true;
            }
            if (ch == '\\' && _pos < _source.size()) {
                out.push_back(_source[_pos++]);
                continue;
            }
            out.push_back(ch);
        }
        return false;
    }

    bool parseNumber(double& out) {
        skipWhitespace();
        const size_t start = _pos;
        if (peek() == '-') {
            ++_pos;
        }
        while (_pos < _source.size() && std::isdigit(static_cast<unsigned char>(_source[_pos]))) {
            ++_pos;
        }
        if (peek() == '.') {
            ++_pos;
            while (_pos < _source.size() && std::isdigit(static_cast<unsigned char>(_source[_pos]))) {
                ++_pos;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++_pos;
            if (peek() == '+' || peek() == '-') {
                ++_pos;
            }
            while (_pos < _source.size() && std::isdigit(static_cast<unsigned char>(_source[_pos]))) {
                ++_pos;
            }
        }
        if (start == _pos) {
            return false;
        }
        try {
            out = std::stod(_source.substr(start, _pos - start));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool parseColor(glm::vec3& out) {
        if (!consume('[')) {
            return false;
        }
        double r = 1.0, g = 1.0, b = 1.0;
        if (!parseNumber(r) || !consume(',') || !parseNumber(g) || !consume(',') || !parseNumber(b) ||
            !consume(']')) {
            return false;
        }
        out = glm::vec3(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
        return true;
    }

    bool parseSample(Sample& out) {
        if (!consume('[')) {
            return false;
        }
        double jd = 0.0, x = 0.0, y = 0.0, z = 0.0;
        if (!parseNumber(jd) || !consume(',') || !parseNumber(x) || !consume(',') || !parseNumber(y) ||
            !consume(',') || !parseNumber(z) || !consume(']')) {
            return false;
        }
        out.julianDate = jd;
        out.au = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return true;
    }

    bool parseSamples(std::vector<Sample>& out, std::string& error) {
        if (!consume('[')) {
            error = "Expected samples array";
            return false;
        }
        while (true) {
            skipWhitespace();
            if (consume(']')) {
                break;
            }
            Sample sample;
            if (!parseSample(sample)) {
                error = "Invalid sample [jd,x,y,z]";
                return false;
            }
            out.push_back(sample);
            skipWhitespace();
            if (consume(']')) {
                break;
            }
            if (!consume(',')) {
                error = "Expected ',' or ']' in samples";
                return false;
            }
        }
        return true;
    }

    bool parseMission(Mission& out, std::string& error) {
        if (!consume('{')) {
            error = "Expected mission object";
            return false;
        }
        while (true) {
            skipWhitespace();
            if (consume('}')) {
                break;
            }
            std::string key;
            if (!parseString(key) || !consume(':')) {
                error = "Expected key in mission object";
                return false;
            }
            if (key == "id") {
                if (!parseString(out.id)) {
                    error = "Invalid mission id";
                    return false;
                }
            } else if (key == "name") {
                if (!parseString(out.name)) {
                    error = "Invalid mission name";
                    return false;
                }
            } else if (key == "color") {
                if (!parseColor(out.color)) {
                    error = "Invalid mission color";
                    return false;
                }
            } else if (key == "samples") {
                if (!parseSamples(out.samples, error)) {
                    return false;
                }
            } else if (!skipValue()) {
                error = "Failed to skip unknown mission key: " + key;
                return false;
            }
            skipWhitespace();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                error = "Expected ',' or '}' in mission object";
                return false;
            }
        }
        if (out.id.empty()) {
            error = "Mission missing id";
            return false;
        }
        if (out.name.empty()) {
            out.name = out.id;
        }
        return true;
    }

    bool parseMissions(std::vector<Mission>& out, std::string& error) {
        if (!consume('[')) {
            error = "Expected missions array";
            return false;
        }
        while (true) {
            skipWhitespace();
            if (consume(']')) {
                break;
            }
            Mission mission;
            if (!parseMission(mission, error)) {
                return false;
            }
            out.push_back(std::move(mission));
            skipWhitespace();
            if (consume(']')) {
                break;
            }
            if (!consume(',')) {
                error = "Expected ',' or ']' in missions";
                return false;
            }
        }
        return true;
    }

    bool skipValue() {
        skipWhitespace();
        const char ch = peek();
        if (ch == '"') {
            std::string tmp;
            return parseString(tmp);
        }
        if (ch == '{') {
            ++_pos;
            int depth = 1;
            while (_pos < _source.size() && depth > 0) {
                const char c = _source[_pos++];
                if (c == '"') {
                    while (_pos < _source.size()) {
                        const char s = _source[_pos++];
                        if (s == '\\' && _pos < _source.size()) {
                            ++_pos;
                            continue;
                        }
                        if (s == '"') {
                            break;
                        }
                    }
                } else if (c == '{') {
                    ++depth;
                } else if (c == '}') {
                    --depth;
                }
            }
            return depth == 0;
        }
        if (ch == '[') {
            ++_pos;
            int depth = 1;
            while (_pos < _source.size() && depth > 0) {
                const char c = _source[_pos++];
                if (c == '"') {
                    while (_pos < _source.size()) {
                        const char s = _source[_pos++];
                        if (s == '\\' && _pos < _source.size()) {
                            ++_pos;
                            continue;
                        }
                        if (s == '"') {
                            break;
                        }
                    }
                } else if (c == '[') {
                    ++depth;
                } else if (c == ']') {
                    --depth;
                }
            }
            return depth == 0;
        }
        if (ch == 't' || ch == 'f' || ch == 'n') {
            while (_pos < _source.size() && std::isalpha(static_cast<unsigned char>(_source[_pos]))) {
                ++_pos;
            }
            return true;
        }
        double tmp = 0.0;
        return parseNumber(tmp);
    }
};

} // namespace

bool LoadFromString(const std::string& source, Catalog& out, std::string& errorOut) {
    out = Catalog{};
    JsonReader reader(source);
    if (!reader.parseCatalog(out, errorOut)) {
        out = Catalog{};
        return false;
    }
    return true;
}

bool LoadFromFile(const std::string& path, Catalog& out, std::string& errorOut) {
    std::ifstream in(path);
    if (!in) {
        errorOut = "Could not open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return LoadFromString(ss.str(), out, errorOut);
}

int IndexById(const Catalog& catalog, const std::string& id) {
    for (int i = 0; i < static_cast<int>(catalog.missions.size()); ++i) {
        if (catalog.missions[static_cast<size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

bool InterpolateAu(const Mission& mission, double julianDate, glm::vec3& outAu) {
    if (mission.samples.empty()) {
        return false;
    }
    if (julianDate <= mission.samples.front().julianDate) {
        outAu = mission.samples.front().au;
        return true;
    }
    if (julianDate >= mission.samples.back().julianDate) {
        outAu = mission.samples.back().au;
        return true;
    }
    auto it = std::lower_bound(
        mission.samples.begin(), mission.samples.end(), julianDate,
        [](const Sample& sample, double jd) { return sample.julianDate < jd; });
    if (it == mission.samples.begin()) {
        outAu = it->au;
        return true;
    }
    const Sample& b = *it;
    const Sample& a = *(it - 1);
    const double span = b.julianDate - a.julianDate;
    const float t = span > 1.0e-9 ? static_cast<float>((julianDate - a.julianDate) / span) : 0.0f;
    outAu = a.au + t * (b.au - a.au);
    return true;
}

int SampleStrideForQuality(int qualityPreset) {
    if (qualityPreset <= 0) {
        return 4;
    }
    if (qualityPreset == 1) {
        return 2;
    }
    return 1;
}

std::vector<glm::vec3> DownsampledAu(const Mission& mission, int qualityPreset) {
    std::vector<glm::vec3> out;
    if (mission.samples.empty()) {
        return out;
    }
    const int stride = std::max(1, SampleStrideForQuality(qualityPreset));
    out.reserve((mission.samples.size() / static_cast<size_t>(stride)) + 2);
    for (size_t i = 0; i < mission.samples.size(); i += static_cast<size_t>(stride)) {
        out.push_back(mission.samples[i].au);
    }
    const glm::vec3& last = mission.samples.back().au;
    if (out.empty() || glm::length(out.back() - last) > 1.0e-6f) {
        out.push_back(last);
    }
    return out;
}

} // namespace MissionCatalog
