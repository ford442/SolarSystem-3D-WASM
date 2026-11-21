#ifndef SOLARSYSTEM_CORE_JSON_H
#define SOLARSYSTEM_CORE_JSON_H

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * A small JSON DOM: enough to read the generated star-system catalogs
 * (resource/systems/generated/<id>.runtime.json) without a third-party dependency.
 *
 * Numbers are doubles; strings are UTF-8 with \uXXXX escapes decoded (surrogate pairs too).
 * Lookups on the wrong type or a missing key return a shared null value, so a reader can
 * chain `value["orbit"]["keplerian"]["aKm"].AsNumber(0.0)` and get its default on a gap.
 */
namespace Json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value MakeBool(bool b);
    static Value MakeNumber(double n);
    static Value MakeString(std::string s);
    static Value MakeArray(std::vector<Value> items);
    static Value MakeObject(std::map<std::string, Value, std::less<>> members);

    Type GetType() const { return _type; }
    bool IsNull() const { return _type == Type::Null; }
    bool IsBool() const { return _type == Type::Bool; }
    bool IsNumber() const { return _type == Type::Number; }
    bool IsString() const { return _type == Type::String; }
    bool IsArray() const { return _type == Type::Array; }
    bool IsObject() const { return _type == Type::Object; }

    bool AsBool(bool fallback = false) const { return _type == Type::Bool ? _bool : fallback; }
    double AsNumber(double fallback = 0.0) const { return _type == Type::Number ? _number : fallback; }
    const std::string& AsString() const;
    std::string AsString(std::string_view fallback) const;

    /** Array length or object member count; 0 otherwise. */
    std::size_t Size() const;
    /** Array element, or the null value when out of range or not an array. */
    const Value& operator[](std::size_t index) const;
    /** Object member, or the null value when missing or not an object. */
    const Value& operator[](std::string_view key) const;
    bool Has(std::string_view key) const;

    const std::vector<Value>& Items() const;
    const std::map<std::string, Value, std::less<>>& Members() const;

private:
    Type _type = Type::Null;
    bool _bool = false;
    double _number = 0.0;
    std::string _string;
    // Shared so a Value copies cheaply; the DOM is immutable once parsed.
    std::shared_ptr<const std::vector<Value>> _array;
    std::shared_ptr<const std::map<std::string, Value, std::less<>>> _object;
};

/** Parse `text`; on failure returns false and sets `error` (with the byte offset). */
bool Parse(std::string_view text, Value& out, std::string& error);

/** Read and parse a file; false (and `error`) if it cannot be opened or parsed. */
bool ParseFile(const std::string& path, Value& out, std::string& error);

} // namespace Json

#endif // SOLARSYSTEM_CORE_JSON_H
