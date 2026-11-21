#include "Json.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Json {
namespace {

const Value& NullValue() {
    static const Value null;
    return null;
}

const std::string& EmptyString() {
    static const std::string empty;
    return empty;
}

void AppendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : _text(text) {}

    bool ParseDocument(Value& out, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(out, 0)) {
            error = _error + " at byte " + std::to_string(_pos);
            return false;
        }
        SkipWhitespace();
        if (_pos != _text.size()) {
            error = "trailing characters at byte " + std::to_string(_pos);
            return false;
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;

    std::string_view _text;
    std::size_t _pos = 0;
    std::string _error;

    bool Fail(const char* message) {
        _error = message;
        return false;
    }

    void SkipWhitespace() {
        while (_pos < _text.size()) {
            const char c = _text[_pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            ++_pos;
        }
    }

    bool Literal(std::string_view word) {
        if (_text.substr(_pos, word.size()) != word) {
            return false;
        }
        _pos += word.size();
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return Fail("nesting too deep");
        }
        if (_pos >= _text.size()) {
            return Fail("unexpected end of input");
        }
        const char c = _text[_pos];
        if (c == '{') {
            return ParseObject(out, depth);
        }
        if (c == '[') {
            return ParseArray(out, depth);
        }
        if (c == '"') {
            std::string s;
            if (!ParseString(s)) {
                return false;
            }
            out = Value::MakeString(std::move(s));
            return true;
        }
        if (Literal("true")) {
            out = Value::MakeBool(true);
            return true;
        }
        if (Literal("false")) {
            out = Value::MakeBool(false);
            return true;
        }
        if (Literal("null")) {
            out = Value();
            return true;
        }
        return ParseNumber(out);
    }

    bool ParseNumber(Value& out) {
        const std::size_t start = _pos;
        if (_pos < _text.size() && (_text[_pos] == '-' || _text[_pos] == '+')) {
            ++_pos;
        }
        bool digits = false;
        while (_pos < _text.size()) {
            const char c = _text[_pos];
            if ((c >= '0' && c <= '9')) {
                digits = true;
            } else if (c != '.' && c != 'e' && c != 'E' && c != '-' && c != '+') {
                break;
            }
            ++_pos;
        }
        if (!digits) {
            _pos = start;
            return Fail("expected a value");
        }
        const std::string token(_text.substr(start, _pos - start));
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) {
            _pos = start;
            return Fail("malformed number");
        }
        out = Value::MakeNumber(value);
        return true;
    }

    bool ParseHex4(uint32_t& out) {
        if (_pos + 4 > _text.size()) {
            return Fail("truncated \\u escape");
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = _text[_pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') {
                out |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return Fail("bad hex digit in \\u escape");
            }
        }
        return true;
    }

    bool ParseString(std::string& out) {
        ++_pos; // opening quote
        while (true) {
            if (_pos >= _text.size()) {
                return Fail("unterminated string");
            }
            const char c = _text[_pos++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return Fail("control character in string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (_pos >= _text.size()) {
                return Fail("unterminated escape");
            }
            const char e = _text[_pos++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!ParseHex4(cp)) {
                        return false;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (!Literal("\\u")) {
                            return Fail("unpaired surrogate");
                        }
                        uint32_t low = 0;
                        if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            return Fail("bad low surrogate");
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return Fail("unknown escape");
            }
        }
    }

    bool ParseArray(Value& out, int depth) {
        ++_pos; // [
        std::vector<Value> items;
        SkipWhitespace();
        if (_pos < _text.size() && _text[_pos] == ']') {
            ++_pos;
            out = Value::MakeArray(std::move(items));
            return true;
        }
        while (true) {
            SkipWhitespace();
            Value item;
            if (!ParseValue(item, depth + 1)) {
                return false;
            }
            items.push_back(std::move(item));
            SkipWhitespace();
            if (_pos >= _text.size()) {
                return Fail("unterminated array");
            }
            const char c = _text[_pos++];
            if (c == ']') {
                break;
            }
            if (c != ',') {
                return Fail("expected ',' or ']'");
            }
        }
        out = Value::MakeArray(std::move(items));
        return true;
    }

    bool ParseObject(Value& out, int depth) {
        ++_pos; // {
        std::map<std::string, Value, std::less<>> members;
        SkipWhitespace();
        if (_pos < _text.size() && _text[_pos] == '}') {
            ++_pos;
            out = Value::MakeObject(std::move(members));
            return true;
        }
        while (true) {
            SkipWhitespace();
            if (_pos >= _text.size() || _text[_pos] != '"') {
                return Fail("expected a key");
            }
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (_pos >= _text.size() || _text[_pos] != ':') {
                return Fail("expected ':'");
            }
            ++_pos;
            SkipWhitespace();
            Value value;
            if (!ParseValue(value, depth + 1)) {
                return false;
            }
            members[std::move(key)] = std::move(value);
            SkipWhitespace();
            if (_pos >= _text.size()) {
                return Fail("unterminated object");
            }
            const char c = _text[_pos++];
            if (c == '}') {
                break;
            }
            if (c != ',') {
                return Fail("expected ',' or '}'");
            }
        }
        out = Value::MakeObject(std::move(members));
        return true;
    }
};

} // namespace

Value Value::MakeBool(bool b) {
    Value v;
    v._type = Type::Bool;
    v._bool = b;
    return v;
}

Value Value::MakeNumber(double n) {
    Value v;
    v._type = Type::Number;
    v._number = n;
    return v;
}

Value Value::MakeString(std::string s) {
    Value v;
    v._type = Type::String;
    v._string = std::move(s);
    return v;
}

Value Value::MakeArray(std::vector<Value> items) {
    Value v;
    v._type = Type::Array;
    v._array = std::make_shared<const std::vector<Value>>(std::move(items));
    return v;
}

Value Value::MakeObject(std::map<std::string, Value, std::less<>> members) {
    Value v;
    v._type = Type::Object;
    v._object = std::make_shared<const std::map<std::string, Value, std::less<>>>(std::move(members));
    return v;
}

const std::string& Value::AsString() const {
    return _type == Type::String ? _string : EmptyString();
}

std::string Value::AsString(std::string_view fallback) const {
    return _type == Type::String ? _string : std::string(fallback);
}

std::size_t Value::Size() const {
    if (_type == Type::Array) {
        return _array->size();
    }
    if (_type == Type::Object) {
        return _object->size();
    }
    return 0;
}

const Value& Value::operator[](std::size_t index) const {
    if (_type != Type::Array || index >= _array->size()) {
        return NullValue();
    }
    return (*_array)[index];
}

const Value& Value::operator[](std::string_view key) const {
    if (_type != Type::Object) {
        return NullValue();
    }
    const auto it = _object->find(key);
    return it == _object->end() ? NullValue() : it->second;
}

bool Value::Has(std::string_view key) const {
    return _type == Type::Object && _object->find(key) != _object->end();
}

const std::vector<Value>& Value::Items() const {
    static const std::vector<Value> empty;
    return _type == Type::Array ? *_array : empty;
}

const std::map<std::string, Value, std::less<>>& Value::Members() const {
    static const std::map<std::string, Value, std::less<>> empty;
    return _type == Type::Object ? *_object : empty;
}

bool Parse(std::string_view text, Value& out, std::string& error) {
    // A UTF-8 byte-order mark is not JSON, but editors write one.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    Parser parser(text);
    return parser.ParseDocument(out, error);
}

bool ParseFile(const std::string& path, Value& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!Parse(buffer.str(), out, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // namespace Json
