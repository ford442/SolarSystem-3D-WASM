#include "StarCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace StarCatalog {
namespace {

/** Position just past the `[` / `{` that follows `"key":`, or npos. */
size_t openAfterKey(const std::string& json, const char* key, char open) {
    const std::string needle = std::string("\"") + key + "\"";
    const size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return std::string::npos;
    }
    const size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return std::string::npos;
    }
    const size_t bracket = json.find_first_not_of(" \t\r\n", colon + 1);
    if (bracket == std::string::npos || json[bracket] != open) {
        return std::string::npos;
    }
    return bracket + 1;
}

void skipSpace(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
        ++i;
    }
}

} // namespace

bool Parse(const std::string& json, Catalog& out, std::string& errorOut) {
    out = {};
    errorOut.clear();

    // The generator writes a fixed shape, so this is a small scanner rather than a JSON library
    // (the repo has none). It still rejects anything that does not match that shape.
    const size_t fieldsOpen = openAfterKey(json, "fields", '[');
    if (fieldsOpen == std::string::npos) {
        errorOut = "missing \"fields\" array";
        return false;
    }
    const size_t fieldsClose = json.find(']', fieldsOpen);
    const std::string fields = fieldsClose == std::string::npos ? "" : json.substr(fieldsOpen, fieldsClose - fieldsOpen);
    const size_t ra = fields.find("raDeg"), dec = fields.find("decDeg"), vmag = fields.find("vmag"), bv = fields.find("bv");
    if (ra == std::string::npos || dec == std::string::npos || vmag == std::string::npos || bv == std::string::npos ||
        !(ra < dec && dec < vmag && vmag < bv)) {
        errorOut = "unexpected \"fields\" (want raDeg, decDeg, vmag, bv)";
        return false;
    }

    size_t i = openAfterKey(json, "data", '[');
    if (i == std::string::npos) {
        errorOut = "missing \"data\" array";
        return false;
    }
    std::vector<float> values;
    values.reserve(20000);
    while (true) {
        skipSpace(json, i);
        if (i >= json.size()) {
            errorOut = "unterminated \"data\" array";
            return false;
        }
        if (json[i] == ']') {
            break;
        }
        char* end = nullptr;
        const double value = std::strtod(json.c_str() + i, &end);
        if (end == json.c_str() + i || !std::isfinite(value)) {
            errorOut = "bad number in \"data\"";
            return false;
        }
        values.push_back(static_cast<float>(value));
        i = static_cast<size_t>(end - json.c_str());
        skipSpace(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
        }
    }
    if (values.size() % 4 != 0) {
        errorOut = "\"data\" is not a whole number of 4-tuples";
        return false;
    }

    out.stars.reserve(values.size() / 4);
    for (size_t k = 0; k + 3 < values.size(); k += 4) {
        Star star;
        star.raDeg = values[k];
        star.decDeg = values[k + 1];
        star.vmag = values[k + 2];
        star.bv = values[k + 3];
        if (star.raDeg < 0.0f || star.raDeg >= 360.0f || star.decDeg < -90.0f || star.decDeg > 90.0f) {
            out = {};
            errorOut = "star coordinates out of range";
            return false;
        }
        out.stars.push_back(star);
    }

    // Names are optional: {"0":"Sirius","1":"Canopus",...}
    size_t n = openAfterKey(json, "names", '{');
    if (n != std::string::npos) {
        while (true) {
            skipSpace(json, n);
            if (n >= json.size() || json[n] == '}') {
                break;
            }
            if (json[n] != '"') {
                out = {};
                errorOut = "bad \"names\" object";
                return false;
            }
            const size_t keyEnd = json.find('"', n + 1);
            const size_t colon = keyEnd == std::string::npos ? keyEnd : json.find(':', keyEnd);
            const size_t valueOpen = colon == std::string::npos ? colon : json.find('"', colon);
            const size_t valueEnd = valueOpen == std::string::npos ? valueOpen : json.find('"', valueOpen + 1);
            if (valueEnd == std::string::npos) {
                out = {};
                errorOut = "bad \"names\" object";
                return false;
            }
            const int index = std::atoi(json.substr(n + 1, keyEnd - n - 1).c_str());
            if (index >= 0 && index < static_cast<int>(out.stars.size())) {
                out.names[index] = json.substr(valueOpen + 1, valueEnd - valueOpen - 1);
            }
            n = valueEnd + 1;
            skipSpace(json, n);
            if (n < json.size() && json[n] == ',') {
                ++n;
            }
        }
    }

    // The tiers draw a prefix, so an unsorted file would silently drop bright stars.
    for (size_t k = 1; k < out.stars.size(); ++k) {
        if (out.stars[k].vmag < out.stars[k - 1].vmag) {
            out = {};
            errorOut = "stars are not sorted by magnitude";
            return false;
        }
    }
    return true;
}

bool LoadFromFile(const std::string& path, Catalog& out, std::string& errorOut) {
    std::ifstream in(path);
    if (!in) {
        out = {};
        errorOut = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return Parse(ss.str(), out, errorOut);
}

void ColorFromBV(float bv, float rgbOut[3]) {
    // Ballesteros (2012) B-V → temperature, then Tanner Helland's blackbody → RGB fit.
    const double clamped = std::clamp(static_cast<double>(bv), -0.4, 2.0);
    const double kelvin = 4600.0 * (1.0 / (0.92 * clamped + 1.7) + 1.0 / (0.92 * clamped + 0.62));
    const double t = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;

    double r, g, b;
    if (t <= 66.0) {
        r = 255.0;
        g = 99.4708025861 * std::log(t) - 161.1195681661;
    } else {
        r = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
        g = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
    }
    if (t >= 66.0) {
        b = 255.0;
    } else if (t <= 19.0) {
        b = 0.0;
    } else {
        b = 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
    }
    const double rgb[3] = {std::clamp(r, 0.0, 255.0) / 255.0, std::clamp(g, 0.0, 255.0) / 255.0,
                           std::clamp(b, 0.0, 255.0) / 255.0};
    // Real star colours are subtle to the eye; keep half the saturation so a field of them
    // reads as stars rather than confetti.
    for (int c = 0; c < 3; ++c) {
        rgbOut[c] = static_cast<float>(0.5 + 0.5 * rgb[c]);
    }
}

} // namespace StarCatalog
