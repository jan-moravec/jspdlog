// Shared helpers for the fuzz targets: oracle checks, a byte reader and a capture sink.
//
// The helpers are independent of jspdlog's implementation on purpose. They decode and parse the output with their own
// code and nlohmann/json, so a bug in the header cannot hide behind the same bug in the check.

#pragma once

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/base_sink.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fuzz
{

using ojson = nlohmann::ordered_json;

/** Prints where an oracle check failed and aborts, so libFuzzer and the replay driver both report it. */
[[noreturn]] inline void fail(const char *expression, const char *file, int line)
{
    std::fprintf(stderr, "FUZZ_CHECK failed: %s (%s:%d)\n", expression, file, line);
    std::fflush(stderr);
    std::abort();
}

/** Aborts unless the condition holds. */
inline void check(bool condition, const char *expression, const char *file, int line)
{
    if (!condition)
    {
        fail(expression, file, line);
    }
}

} // namespace fuzz

#define FUZZ_CHECK(condition) ::fuzz::check((condition), #condition, __FILE__, __LINE__)

namespace fuzz
{

/** Views the fuzzer's input bytes as text. */
inline std::string_view as_text(const std::uint8_t *data, std::size_t size)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): uint8_t and char alias, this only changes the view
    return std::string_view{reinterpret_cast<const char *>(data), size};
}

/**
 * Reads typed values from the input, like LLVM's FuzzedDataProvider, which is not available to GCC and MSVC replay
 * builds. Reading past the end yields zeros, so every input is valid.
 */
class reader
{
public:
    explicit reader(std::string_view input)
        : input_(input)
    {
    }

    /** The next byte, or 0 at the end. */
    std::uint8_t byte()
    {
        if (pos_ >= input_.size())
        {
            return 0;
        }
        return static_cast<std::uint8_t>(input_[pos_++]);
    }

    /** The next sizeof(T) bytes as a T, in the host byte order. Missing bytes are zeros. */
    template <typename T>
    T value()
    {
        static_assert(std::is_trivially_copyable_v<T>, "reader::value needs a trivially copyable type");
        T out{};
        auto *dst = reinterpret_cast<unsigned char *>(&out); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        for (std::size_t i = 0; i < sizeof(T); ++i)
        {
            dst[i] = byte();
        }
        return out;
    }

    /** A string of up to max_size bytes: one length byte, then that many bytes (fewer at the end of the input). */
    std::string string(std::size_t max_size)
    {
        const std::size_t want = static_cast<std::size_t>(byte()) % (max_size + 1);
        const std::size_t n = std::min(want, remaining());
        std::string out{input_.substr(pos_, n)};
        pos_ += n;
        return out;
    }

    /** Everything that has not been read yet. */
    std::string_view rest()
    {
        const std::string_view out = input_.substr(pos_);
        pos_ = input_.size();
        return out;
    }

    /** The number of unread bytes. */
    [[nodiscard]] std::size_t remaining() const { return input_.size() - pos_; }

private:
    std::string_view input_;
    std::size_t pos_ = 0;
};

/** True when the bytes are valid UTF-8 (RFC 3629: no overlong forms, no surrogates, at most U+10FFFF). */
inline bool is_valid_utf8(std::string_view s)
{
    std::size_t i = 0;
    const auto at = [&](std::size_t k) { return static_cast<unsigned>(static_cast<unsigned char>(s[k])); };
    const auto continuation = [&](std::size_t k) { return k < s.size() && (at(k) & 0xC0U) == 0x80U; };
    while (i < s.size())
    {
        const unsigned c = at(i);
        if (c < 0x80U)
        {
            i += 1;
        }
        else if (c >= 0xC2U && c <= 0xDFU)
        {
            if (!continuation(i + 1))
            {
                return false;
            }
            i += 2;
        }
        else if (c >= 0xE0U && c <= 0xEFU)
        {
            if (!continuation(i + 1) || !continuation(i + 2))
            {
                return false;
            }
            const unsigned c1 = at(i + 1);
            if ((c == 0xE0U && c1 < 0xA0U) || (c == 0xEDU && c1 > 0x9FU)) // overlong, surrogate
            {
                return false;
            }
            i += 3;
        }
        else if (c >= 0xF0U && c <= 0xF4U)
        {
            if (!continuation(i + 1) || !continuation(i + 2) || !continuation(i + 3))
            {
                return false;
            }
            const unsigned c1 = at(i + 1);
            if ((c == 0xF0U && c1 < 0x90U) || (c == 0xF4U && c1 > 0x8FU)) // overlong, above U+10FFFF
            {
                return false;
            }
            i += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

namespace text_detail
{

inline int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

inline void append_utf8(std::string &out, unsigned cp)
{
    if (cp < 0x80U)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800U)
    {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
    else if (cp < 0x10000U)
    {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

} // namespace text_detail

/**
 * Decodes a JSON string literal byte by byte, for text that is not valid UTF-8 and so cannot go through nlohmann.
 * Returns nothing unless the literal is exactly one string: an opening quote, no raw control bytes, only the escapes
 * JSON allows, a closing quote and nothing after it. Bytes other than escapes are copied unchanged.
 */
inline std::optional<std::string> decode_json_string(std::string_view quoted)
{
    if (quoted.size() < 2 || quoted.front() != '"')
    {
        return std::nullopt;
    }
    std::string out;
    std::size_t i = 1;
    const auto read_u4 = [&](std::size_t at) -> int {
        if (at + 4 > quoted.size())
        {
            return -1;
        }
        int v = 0;
        for (std::size_t k = 0; k < 4; ++k)
        {
            const int d = text_detail::hex_digit(quoted[at + k]);
            if (d < 0)
            {
                return -1;
            }
            v = (v * 16) + d;
        }
        return v;
    };
    while (i < quoted.size())
    {
        const auto c = static_cast<unsigned char>(quoted[i]);
        if (c == '"')
        {
            return i + 1 == quoted.size() ? std::optional<std::string>{std::move(out)} : std::nullopt;
        }
        if (c < 0x20U)
        {
            return std::nullopt;
        }
        if (c != '\\')
        {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        if (i + 1 >= quoted.size())
        {
            return std::nullopt;
        }
        const char e = quoted[i + 1];
        i += 2;
        switch (e)
        {
        case '"':
            out.push_back('"');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '/':
            out.push_back('/');
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u':
        {
            const int hi = read_u4(i);
            if (hi < 0)
            {
                return std::nullopt;
            }
            i += 4;
            auto cp = static_cast<unsigned>(hi);
            if (cp >= 0xD800U && cp <= 0xDBFFU)
            {
                if (i + 1 >= quoted.size() || quoted[i] != '\\' || quoted[i + 1] != 'u')
                {
                    return std::nullopt;
                }
                const int lo = read_u4(i + 2);
                if (lo < 0xDC00 || lo > 0xDFFF)
                {
                    return std::nullopt;
                }
                i += 6;
                cp = 0x10000U + ((cp - 0xD800U) << 10U) + (static_cast<unsigned>(lo) - 0xDC00U);
            }
            else if (cp >= 0xDC00U && cp <= 0xDFFFU)
            {
                return std::nullopt;
            }
            text_detail::append_utf8(out, cp);
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return std::nullopt; // no closing quote
}

/** Decodes a JSON string literal, or aborts when it is not exactly one. */
inline std::string decode_json_string_or_abort(std::string_view quoted)
{
    auto decoded = decode_json_string(quoted);
    if (!decoded)
    {
        fail("decode_json_string(quoted)", __FILE__, __LINE__);
    }
    return std::move(*decoded);
}

/** True when the literal is exactly one JSON string. See decode_json_string. */
inline bool is_json_string_syntax(std::string_view quoted)
{
    return decode_json_string(quoted).has_value();
}

/** True when the text is one JSON number as RFC 8259 spells it. */
inline bool is_json_number_grammar(std::string_view s)
{
    std::size_t i = 0;
    const auto digits = [&] {
        const std::size_t start = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        {
            ++i;
        }
        return i - start;
    };
    if (i < s.size() && s[i] == '-')
    {
        ++i;
    }
    if (i < s.size() && s[i] == '0')
    {
        ++i;
    }
    else if (digits() == 0)
    {
        return false;
    }
    if (i < s.size() && s[i] == '.')
    {
        ++i;
        if (digits() == 0)
        {
            return false;
        }
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
    {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        {
            ++i;
        }
        if (digits() == 0)
        {
            return false;
        }
    }
    return i == s.size();
}

/** Removes the terminator, or aborts when the line does not end with it. */
inline std::string_view strip_eol(std::string_view line, std::string_view eol)
{
    FUZZ_CHECK(line.size() >= eol.size());
    FUZZ_CHECK(line.substr(line.size() - eol.size()) == eol);
    return line.substr(0, line.size() - eol.size());
}

/**
 * Checks the JSON-lines property without parsing: one braced object, and no control byte anywhere, so no raw newline
 * or carriage return can split the line. It holds for any input, including text that is not valid UTF-8.
 */
inline void check_shape(std::string_view line, std::string_view eol)
{
    const std::string_view body = strip_eol(line, eol);
    FUZZ_CHECK(body.size() >= 2);
    FUZZ_CHECK(body.front() == '{' && body.back() == '}');
    for (const char c : body)
    {
        FUZZ_CHECK(static_cast<unsigned char>(c) >= 0x20U);
    }
}

/**
 * Parses one line strictly. Aborts unless it is exactly one JSON object followed by the configured eol. With
 * json_lines set, which is the default, it also aborts on any control byte inside the object. Call it only when every
 * string in the line is valid UTF-8, because nlohmann rejects anything else.
 */
inline ojson parse_line(std::string_view line, std::string_view eol, bool json_lines = true)
{
    const std::string_view body = strip_eol(line, eol);
    if (json_lines)
    {
        check_shape(line, eol);
    }
    ojson parsed = ojson::parse(body.begin(), body.end(), nullptr, false);
    FUZZ_CHECK(parsed.is_object());
    return parsed;
}

/** The keys of an object in document order. */
inline std::vector<std::string> keys_of(const ojson &object)
{
    std::vector<std::string> keys;
    for (const auto &item : object.items())
    {
        keys.push_back(item.key());
    }
    return keys;
}

/** Collects every formatted line, including its terminator. */
class capture_sink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    std::vector<std::string> lines;

    [[nodiscard]] std::string last() const { return lines.empty() ? std::string{} : lines.back(); }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        lines.emplace_back(formatted.data(), formatted.size());
    }
    void flush_() override {}
};

/** Header keys that a fuzzed field key must not equal, because duplicate keys are the caller's problem. */
inline std::string avoid_reserved_key(std::string key, const std::vector<std::string> &reserved)
{
    while (std::find(reserved.begin(), reserved.end(), key) != reserved.end())
    {
        key += '_';
    }
    return key;
}

/** A json_logger on a capture sink. */
struct harness
{
    std::shared_ptr<capture_sink> sink = std::make_shared<capture_sink>();
    jspdlog::json_logger logger;

    harness(std::string name, const jspdlog::json_options &options)
        : logger(std::move(name), sink, options)
    {
        logger.set_level(jspdlog::level::trace);
    }
};

/** Options with only the deterministic header fields: logger and level. Every line ends in LF. */
inline jspdlog::json_options deterministic_options()
{
    jspdlog::json_options options;
    options.timestamp_key = std::nullopt;
    options.process_key = std::nullopt;
    options.thread_key = std::nullopt;
    options.eol = "\n";
    return options;
}

} // namespace fuzz
