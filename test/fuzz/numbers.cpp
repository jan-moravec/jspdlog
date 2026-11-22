// Target 6: numbers. Every integer width, float, double and long double, from raw bit patterns.
#include "fuzz_support.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace
{

// The number as jspdlog wrote it: the text after "v": in {"v":...}.
std::string number_text(const std::string &json)
{
    static constexpr std::string_view PREFIX = "{\"v\":";
    FUZZ_CHECK(json.size() > PREFIX.size() + 1);
    FUZZ_CHECK(json.compare(0, PREFIX.size(), PREFIX) == 0);
    FUZZ_CHECK(json.back() == '}');
    return json.substr(PREFIX.size(), json.size() - PREFIX.size() - 1);
}

template <typename T>
std::string insert_one(T value)
{
    jspdlog::fields f;
    f.insert("v", value);
    return f.to_json();
}

template <typename T>
void check_integer(fuzz::reader &r)
{
    const T value = r.value<T>();
    const std::string json = insert_one(value);
    const std::string text = number_text(json);
    FUZZ_CHECK(fuzz::is_json_number_grammar(text));
    FUZZ_CHECK(text.find_first_of(".eE") == std::string::npos); // an integer stays an integer
    const auto parsed = nlohmann::json::parse(json);
    if constexpr (std::is_signed_v<T>)
    {
        FUZZ_CHECK(parsed["v"].is_number_integer());
        FUZZ_CHECK(parsed["v"].get<std::int64_t>() == static_cast<std::int64_t>(value));
    }
    else
    {
        FUZZ_CHECK(parsed["v"].is_number_unsigned());
        FUZZ_CHECK(parsed["v"].get<std::uint64_t>() == static_cast<std::uint64_t>(value));
    }
}

// Equal values with the same sign, so -0.0 and 0.0 differ. Only called with finite values.
template <typename T>
bool same_bits(T a, T b)
{
    return a == b && std::signbit(a) == std::signbit(b);
}

template <typename T>
T parse_text(const std::string &text);
template <>
float parse_text<float>(const std::string &text)
{
    return std::strtof(text.c_str(), nullptr);
}
template <>
double parse_text<double>(const std::string &text)
{
    return std::strtod(text.c_str(), nullptr);
}

// float and double: fmt prints the shortest text that reads back as the same value.
template <typename T>
void check_binary_float(T value)
{
    const std::string json = insert_one(value);
    const std::string text = number_text(json);
    const auto parsed = nlohmann::json::parse(json);
    if (!std::isfinite(value))
    {
        FUZZ_CHECK(text == "null");
        FUZZ_CHECK(parsed["v"].is_null());
        return;
    }
    FUZZ_CHECK(fuzz::is_json_number_grammar(text));
    FUZZ_CHECK(parsed["v"].is_number_float()); // a whole float keeps its ".0"
    FUZZ_CHECK(same_bits(parse_text<T>(text), value));
}

// long double: grammar always, value to the precision of the widest conversion fmt may use.
void check_long_double(long double value)
{
    const std::string json = insert_one(value);
    const std::string text = number_text(json);
    if (!std::isfinite(value))
    {
        FUZZ_CHECK(text == "null");
        return;
    }
    FUZZ_CHECK(fuzz::is_json_number_grammar(text));
    FUZZ_CHECK(text.find_first_of(".eE") != std::string::npos); // still a float for consumers that infer types
    const long double back = std::strtold(text.c_str(), nullptr);
    FUZZ_CHECK(std::isfinite(back));
    FUZZ_CHECK(std::fabs(back - value) <= std::fabs(value) * 2.5e-16L);
    // nlohmann reads doubles only. Outside the range of double the number is valid JSON that many parsers cannot read.
    const long double magnitude = std::fabs(value);
    if (magnitude <= static_cast<long double>(std::numeric_limits<double>::max()) &&
        (magnitude >= static_cast<long double>(std::numeric_limits<double>::min()) || value == 0.0L))
    {
        FUZZ_CHECK(nlohmann::json::parse(json)["v"].is_number_float());
    }
}

// A long double from a mantissa and an exponent, so no input produces an invalid x87 encoding.
long double make_long_double(fuzz::reader &r)
{
    const auto mantissa = r.value<std::uint64_t>();
    const auto exponent = static_cast<int>(r.value<std::uint16_t>() % 32800U) - 16400;
    const bool negative = (r.byte() & 1U) != 0;
    const long double v = std::ldexp(static_cast<long double>(mantissa), exponent);
    return negative ? -v : v;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer requires this name
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    fuzz::reader r(fuzz::as_text(data, size));
    check_integer<std::int8_t>(r);
    check_integer<std::int16_t>(r);
    check_integer<std::int32_t>(r);
    check_integer<std::int64_t>(r);
    check_integer<std::uint8_t>(r);
    check_integer<std::uint16_t>(r);
    check_integer<std::uint32_t>(r);
    check_integer<std::uint64_t>(r);
    check_binary_float<float>(r.value<float>());
    check_binary_float<double>(r.value<double>());
    check_long_double(make_long_double(r));
    return 0;
}
