/**
 * @file
 * @brief JSON-lines logging on top of spdlog, in a single header.
 *
 * A json_logger wraps an spdlog::logger so that every call made through it writes one JSON object per line, with typed
 * fields that the caller adds per call or binds to a child logger. It works with spdlog v1.x (1.12 or newer) and v2.x,
 * and keeps spdlog's sinks, levels and async support.
 *
 * @code
 * jspdlog::json_logger logger("app", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
 * logger.info({"user_id", 42}, "hello {}", "world");
 * // {"timestamp":"2026-05-13T08:00:00.000+02:00","logger":"app","level":"info","process":1234,"thread":5678,
 * //  "user_id":42,"message":"hello world"}
 * @endcode
 *
 * Field values keep their JSON type: numbers, booleans, strings and null, but also std::optional, enums, durations and
 * time points, ranges, maps, tuples and variants, nested to any depth. Your own types join in through a
 * jspdlog_serialize function next to the type, or a specialization of jspdlog::serializer.
 *
 * A json_formatter renders the whole line from the spdlog message: the header fields, the call site when asked for, the
 * fields and the message. A json_logger hands it the fields and the message in a payload that starts with a random,
 * process-wide token, and the formatter recognizes that token. Any other payload, such as a message logged directly on
 * the underlying spdlog::logger, by a macro or by third-party code, is escaped into the message field. So every line
 * that a json_formatter writes is one valid JSON object, whoever logged it, and valid UTF-8: invalid bytes in any
 * string follow JSPDLOG_UTF8_POLICY, which replaces them by default. install_json_formatter gives the same guarantee to
 * loggers and sinks that jspdlog does not wrap. A sink that writes the raw payload instead of the formatted line shows
 * the payload protocol, which is not JSON. Logging calls do not throw: a formatting error goes to the spdlog error
 * handler, as in spdlog.
 *
 * @copyright Copyright (c) 2026 Jan Moravec. Distributed under the MIT License, see LICENSE.
 */

#pragma once

/** @brief Major version of jspdlog. */
#define JSPDLOG_VERSION_MAJOR 0
/** @brief Minor version of jspdlog. */
#define JSPDLOG_VERSION_MINOR 5
/** @brief Patch version of jspdlog. */
#define JSPDLOG_VERSION_PATCH 0
/** @brief The version as one number, major * 10000 + minor * 100 + patch, for #if comparisons. */
#define JSPDLOG_VERSION ((JSPDLOG_VERSION_MAJOR * 10000) + (JSPDLOG_VERSION_MINOR * 100) + JSPDLOG_VERSION_PATCH)

#include <spdlog/version.h>

#if SPDLOG_VER_MAJOR >= 2
#define JSPDLOG_STD_FORMAT 0
#include <fmt/format.h>
/** @brief 1 when building against spdlog v2.x, 0 for v1.x. */
#define JSPDLOG_SPDLOG_V2 1
#else
#if SPDLOG_VERSION < 11200
#error "jspdlog requires spdlog >= 1.12 (or v2.x)"
#endif
#ifdef SPDLOG_USE_STD_FORMAT
// spdlog v1 formats with std::format, which needs C++20. spdlog v2 has no such mode.
#if __cplusplus < 202002L && !(defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#error "jspdlog with SPDLOG_USE_STD_FORMAT needs C++20 for <format>"
#endif
#define JSPDLOG_STD_FORMAT 1
#else
#define JSPDLOG_STD_FORMAT 0
#endif
#include <spdlog/fmt/fmt.h>
#define JSPDLOG_SPDLOG_V2 0
#endif

#include <spdlog/common.h>
#include <spdlog/details/os.h>
#include <spdlog/formatter.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>

#if JSPDLOG_STD_FORMAT && !defined(__cpp_lib_format)
#error "jspdlog with SPDLOG_USE_STD_FORMAT needs a standard library with <format>"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ratio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <pthread.h>
#endif

#if defined(__GNUC__) && __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define JSPDLOG_DETAIL_HAS_CXXABI 1
#else
#define JSPDLOG_DETAIL_HAS_CXXABI 0
#endif

#ifndef JSPDLOG_ASSERT
#include <cassert>
/**
 * @brief Called with a condition that must hold and a message. Debug builds use it to find field keys that clash with
 * the keys of the line. Define it before including jspdlog to use your own handler, for example one that logs. With
 * NDEBUG the default expands to nothing and the condition is not evaluated.
 */
#define JSPDLOG_ASSERT(cond, msg) assert((cond) && (msg))
#endif

#if (defined(__cpp_exceptions) || defined(_CPPUNWIND)) && !defined(SPDLOG_NO_EXCEPTIONS)
/** @brief 1 when the code is built with C++ exceptions, so a nested exception can be inspected, 0 otherwise. */
#define JSPDLOG_HAS_EXCEPTIONS 1
#else
#define JSPDLOG_HAS_EXCEPTIONS 0
#endif

#if defined(__cpp_rtti) || defined(__GXX_RTTI) || defined(_CPPRTTI)
/** @brief 1 when run-time type information is available, so the dynamic type of an exception can be named. */
#define JSPDLOG_HAS_RTTI 1
#else
#define JSPDLOG_HAS_RTTI 0
#endif

#ifdef __cpp_consteval
/** @brief consteval where the compiler has it, else constexpr: a compile-time check in C++20, a run-time one in C++17.
 */
#define JSPDLOG_CONSTEVAL consteval
#else
#define JSPDLOG_CONSTEVAL constexpr
#endif

#if JSPDLOG_STD_FORMAT
/** @brief What a format-string constructor is declared with: {fmt}'s own switch, or consteval for std::format. */
#define JSPDLOG_FORMAT_CONSTEVAL JSPDLOG_CONSTEVAL
#else
#define JSPDLOG_FORMAT_CONSTEVAL FMT_CONSTEVAL
#endif

// The policy values are not 0: the preprocessor reads an unknown name such as escape as 0, and the check below must
// reject it instead of taking it for one of the policies.
/** @brief JSPDLOG_UTF8_POLICY value: invalid UTF-8 is replaced by U+FFFD, one per maximal invalid subsequence. */
#define JSPDLOG_UTF8_REPLACE 1
/** @brief JSPDLOG_UTF8_POLICY value: each invalid byte is written as a JSON u00XX escape of its value. */
#define JSPDLOG_UTF8_ESCAPE 2
/** @brief JSPDLOG_UTF8_POLICY value: invalid bytes are copied unchanged. The line is then not valid JSON for strict
 * parsers. */
#define JSPDLOG_UTF8_PASS 3

#ifndef JSPDLOG_UTF8_POLICY
/**
 * @brief What jspdlog does with invalid UTF-8 in any string it writes: keys, values, messages, logger names and call
 * sites. Define it to JSPDLOG_UTF8_ESCAPE or JSPDLOG_UTF8_PASS before including jspdlog, the same in every translation
 * unit, or set the CMake cache variable JSPDLOG_UTF8_POLICY. The default is JSPDLOG_UTF8_REPLACE. Unlike
 * JSPDLOG_ACTIVE_LEVEL, which only the statement macros read, inline functions of the header read this setting, so
 * translation units that disagree break the one-definition rule.
 */
#define JSPDLOG_UTF8_POLICY JSPDLOG_UTF8_REPLACE
#endif

#if JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_REPLACE && JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_ESCAPE &&                       \
    JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_PASS
#error "JSPDLOG_UTF8_POLICY must be JSPDLOG_UTF8_REPLACE, JSPDLOG_UTF8_ESCAPE or JSPDLOG_UTF8_PASS"
#endif

#ifndef JSPDLOG_ESCAPE_LINE_SEPARATORS
/**
 * @brief When nonzero, U+2028 and U+2029 are written as JSON u2028 and u2029 escapes. They are valid in JSON but end a
 * line in JavaScript source before ES2019, which matters for consumers that embed lines in scripts. Off by default.
 * Like JSPDLOG_UTF8_POLICY it must be the same in every translation unit.
 */
#define JSPDLOG_ESCAPE_LINE_SEPARATORS 0
#endif

namespace jspdlog
{

/**
 * @brief The log level type of the spdlog in use.
 *
 * Spelling a level as jspdlog::level::info works with both spdlog v1, where the type is the unscoped enum
 * spdlog::level::level_enum, and spdlog v2, where it is the scoped enum spdlog::level.
 */
#if JSPDLOG_SPDLOG_V2
using spdlog::level;
#else
using level = spdlog::level::level_enum;
#endif

/** @brief Implementation details. Not part of the public API. */
namespace detail
{
/** @brief The number of levels, from trace to off. */
inline constexpr std::size_t LEVEL_COUNT = 7;
/** @brief The OpenTelemetry severity number of each level's range, from trace to off. */
inline constexpr std::array<int, LEVEL_COUNT> DEFAULT_SEVERITY_NUMBERS = {1, 5, 9, 13, 17, 21, 0};
} // namespace detail

/** @brief How the timestamp is written. */
enum class timestamp_format
{
    /** @brief RFC 3339 string with milliseconds, "2026-11-23T08:00:00.123+01:00". The default. */
    rfc3339_millis,
    /** @brief RFC 3339 string with microseconds. */
    rfc3339_micros,
    /** @brief RFC 3339 string with nanoseconds. The digits below the clock's resolution are zero. */
    rfc3339_nanos,
    /** @brief Milliseconds since the Unix epoch, as a number. time_type does not apply. */
    epoch_millis,
};

/**
 * @brief Settings of a json_logger or a json_formatter.
 *
 * The key members control the header fields, which always appear in this order: timestamp, logger, level, severity,
 * process, thread, source. They are followed by the fields and then the message. A string renames a field and an empty
 * optional leaves it out. Keys are escaped, so any text is safe. With every optional field left out, a line holds only
 * the fields and the message.
 */
struct json_options
{
    /** @brief Key of the timestamp, written as chosen by timestamp_style. */
    std::optional<std::string> timestamp_key = std::string{"timestamp"};
    /** @brief Key of the logger name. */
    std::optional<std::string> logger_key = std::string{"logger"};
    /** @brief Key of the level name, as spdlog spells it (for example warning) unless level_names says otherwise. */
    std::optional<std::string> level_key = std::string{"level"};
    /**
     * @brief Names written under level_key, indexed by level from trace to off. An empty optional uses spdlog's names,
     * including names set with SPDLOG_LEVEL_NAMES.
     */
    std::optional<std::array<std::string, detail::LEVEL_COUNT>> level_names;
    /** @brief Key of a numeric severity, written right after the level. Off by default. */
    std::optional<std::string> severity_key;
    /**
     * @brief The numbers written under severity_key, indexed like level_names. The default is the OpenTelemetry
     * severity number of each level's range: TRACE 1, DEBUG 5, INFO 9, WARN 13, ERROR 17, FATAL 21; off is 0.
     */
    std::array<int, detail::LEVEL_COUNT> severity_numbers = detail::DEFAULT_SEVERITY_NUMBERS;
    /** @brief Key of the process id, written as a number. */
    std::optional<std::string> process_key = std::string{"process"};
    /** @brief Key of the thread id, written as a number. */
    std::optional<std::string> thread_key = std::string{"thread"};
    /**
     * @brief Key of the call site, written as an object with the file (the base name), line and function. Off by
     * default. json_logger calls record their call site. Lines without one, such as those logged through a plain spdlog
     * macro without source information, have no such field. With source_flat, setting it only switches the call site
     * on and its text is not used.
     */
    std::optional<std::string> source_key;
    /** @brief Key of the file inside the call site object, or of the top-level field when source_flat is set. */
    std::string source_file_key = "file";
    /** @brief Key of the line inside the call site object, or of the top-level field when source_flat is set. */
    std::string source_line_key = "line";
    /** @brief Key of the function inside the call site object, or of the top-level field when source_flat is set. */
    std::string source_function_key = "function";
    /**
     * @brief Write the call site as three fields of the line itself, under source_file_key, source_line_key and
     * source_function_key, instead of an object under source_key. Schemas such as ECS and OpenTelemetry want that.
     */
    bool source_flat = false;
    /** @brief Key of the message. It is not optional, because a message from outside jspdlog needs a place to go. */
    std::string message_key = "message";
    /** @brief Key of the exception passed first to a logging call. Used only by those call forms. */
    std::string error_key = "error";
    /** @brief Key of the exception's type name inside its object. Used only by the exception call forms. */
    std::string error_type_key = "type";
    /** @brief Key of the exception's message inside its object. Used only by the exception call forms. */
    std::string error_message_key = "message";

    /** @brief How the timestamp is written. */
    timestamp_format timestamp_style = timestamp_format::rfc3339_millis;

    /** @brief Local time with its UTC offset, or UTC ending in Z. Does not apply to epoch_millis. */
    spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local;

    /**
     * @brief Line terminator. An empty optional keeps spdlog's platform default, which is CR LF on Windows. Set a
     * single LF for portable JSON-lines files, or an empty string for sinks that frame messages themselves.
     */
    std::optional<std::string> eol;
};

/**
 * @brief A JSON value that is already serialized, written to the line unchanged.
 *
 * Use it to log arrays or objects produced by a JSON library. jspdlog does not check the text, neither as JSON nor as
 * UTF-8, so the caller must make sure it is valid JSON. The UTF-8 policy does not apply to it. An empty value is
 * written as null.
 *
 * @code
 * logger.info({"items", jspdlog::raw_json{items.dump()}}, "loaded");
 * @endcode
 */
struct raw_json
{
    /** @brief The serialized JSON text. */
    std::string value;
};

/** @brief Implementation details. Not part of the public API. */
namespace detail
{

/** @brief The result of checking one UTF-8 sequence. */
struct utf8_step
{
    /** @brief Whether the bytes form one valid code point. */
    bool valid;
    /** @brief Bytes consumed: the sequence, or the maximal invalid subpart (at least one byte). */
    std::size_t size;
};

/**
 * @brief Checks the sequence that starts at p, a byte of 0x80 or more, where end is the end of the text.
 *
 * An invalid sequence consumes its maximal subpart, as the Unicode standard recommends for U+FFFD substitution.
 */
inline utf8_step next_utf8(const unsigned char *p, const unsigned char *end) noexcept
{
    // A sequence is a lead byte in [first, last], then len - 1 continuation bytes. The second byte has the range
    // [lo, hi], which excludes overlong forms, surrogates and code points above U+10FFFF. The rows are the Unicode
    // table of well-formed byte sequences.
    struct lead_range
    {
        unsigned first;
        unsigned last;
        std::size_t len;
        unsigned lo;
        unsigned hi;
    };
    static constexpr unsigned CONTINUATION_MIN = 0x80U;
    static constexpr unsigned CONTINUATION_MAX = 0xBFU;
    static constexpr lead_range LEADS[] = {
        {0xC2U, 0xDFU, 2, 0x80U, 0xBFU},
        {0xE0U, 0xE0U, 3, 0xA0U, 0xBFU},
        {0xE1U, 0xECU, 3, 0x80U, 0xBFU},
        {0xEDU, 0xEDU, 3, 0x80U, 0x9FU},
        {0xEEU, 0xEFU, 3, 0x80U, 0xBFU},
        {0xF0U, 0xF0U, 4, 0x90U, 0xBFU},
        {0xF1U, 0xF3U, 4, 0x80U, 0xBFU},
        {0xF4U, 0xF4U, 4, 0x80U, 0x8FU},
    };
    const unsigned b0 = p[0];
    const lead_range *lead = nullptr;
    for (const lead_range &row : LEADS)
    {
        if (b0 >= row.first && b0 <= row.last)
        {
            lead = &row;
            break;
        }
    }
    if (lead == nullptr)
    {
        return {false, 1}; // 80 to C1 and F5 to FF never start a sequence.
    }
    for (std::size_t i = 1; i < lead->len; ++i)
    {
        if (p + i == end)
        {
            return {false, i};
        }
        const unsigned b = p[i];
        const unsigned min = i == 1 ? lead->lo : CONTINUATION_MIN;
        const unsigned max = i == 1 ? lead->hi : CONTINUATION_MAX;
        if (b < min || b > max)
        {
            return {false, i};
        }
    }
    return {true, lead->len};
}

/**
 * @brief Appends text to a buffer as a quoted JSON string.
 *
 * Quotes, backslashes and control characters are escaped. Bytes of 0x80 or more are checked as UTF-8, and invalid ones
 * follow JSPDLOG_UTF8_POLICY: replaced, escaped or copied. The buffer can be a std::string or an spdlog memory buffer.
 */
template <typename Buffer>
void append_json_quoted(Buffer &out, std::string_view s)
{
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    static constexpr unsigned FIRST_PRINTABLE = 0x20U; // Bytes below this are control characters.
    static constexpr unsigned FIRST_NON_ASCII = 0x80U;
    static constexpr unsigned NIBBLE_BITS = 4U;
    static constexpr unsigned NIBBLE_MASK = 0xFU;
    // Whether bytes of 0x80 or more need a look: the pass policy copies them unless line separators are escaped.
    static constexpr bool CHECK_UTF8 = JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_PASS || JSPDLOG_ESCAPE_LINE_SEPARATORS != 0;
    static constexpr std::size_t SEPARATOR_SIZE = 3; // U+2028 and U+2029 are E2 80 A8 and E2 80 A9.
    static constexpr unsigned SEPARATOR_LEAD = 0xE2U;
    static constexpr unsigned SEPARATOR_SECOND = 0x80U;
    static constexpr unsigned LINE_SEPARATOR_LAST = 0xA8U;
    static constexpr unsigned PARAGRAPH_SEPARATOR_LAST = 0xA9U;
    out.push_back('"');
    const char *run = s.data(); // Start of the bytes not yet copied.
    const char *const end = s.data() + s.size();
    [[maybe_unused]] const auto put = [&out](std::string_view text) {
        out.append(text.data(), text.data() + text.size());
    };
    [[maybe_unused]] const auto put_u00 = [&out](unsigned c) {
        out.push_back('\\');
        out.push_back('u');
        out.push_back('0');
        out.push_back('0');
        out.push_back(HEX_DIGITS[c >> NIBBLE_BITS]);
        out.push_back(HEX_DIGITS[c & NIBBLE_MASK]);
    };
    for (const char *p = run; p != end; ++p)
    {
        const unsigned c = static_cast<unsigned char>(*p);
        if (c >= FIRST_NON_ASCII)
        {
            if constexpr (CHECK_UTF8)
            {
                const auto *const up = reinterpret_cast<const unsigned char *>(p);
                const utf8_step step = next_utf8(up, reinterpret_cast<const unsigned char *>(end));
                const bool separator = JSPDLOG_ESCAPE_LINE_SEPARATORS != 0 && step.valid &&
                                       step.size == SEPARATOR_SIZE && up[0] == SEPARATOR_LEAD &&
                                       up[1] == SEPARATOR_SECOND &&
                                       (up[2] == LINE_SEPARATOR_LAST || up[2] == PARAGRAPH_SEPARATOR_LAST);
                if (separator)
                {
                    out.append(run, p);
                    put(up[2] == LINE_SEPARATOR_LAST ? std::string_view{"\\u2028"} : std::string_view{"\\u2029"});
                    run = p + step.size;
                }
                else if (!step.valid && JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_PASS)
                {
                    out.append(run, p);
                    if constexpr (JSPDLOG_UTF8_POLICY == JSPDLOG_UTF8_REPLACE)
                    {
                        put(std::string_view{"\xEF\xBF\xBD"});
                    }
                    else
                    {
                        for (std::size_t i = 0; i < step.size; ++i)
                        {
                            put_u00(up[i]);
                        }
                    }
                    run = p + step.size;
                }
                p += step.size - 1; // Valid sequences, and bytes that pass, stay in the copied run.
            }
            continue;
        }
        if (c >= FIRST_PRINTABLE && c != '"' && c != '\\')
        {
            continue;
        }
        out.append(run, p);
        run = p + 1;
        out.push_back('\\');
        switch (c)
        {
        case '"':
            out.push_back('"');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '\b':
            out.push_back('b');
            break;
        case '\f':
            out.push_back('f');
            break;
        case '\n':
            out.push_back('n');
            break;
        case '\r':
            out.push_back('r');
            break;
        case '\t':
            out.push_back('t');
            break;
        default:
            out.push_back('u');
            out.push_back('0');
            out.push_back('0');
            out.push_back(HEX_DIGITS[c >> NIBBLE_BITS]);
            out.push_back(HEX_DIGITS[c & NIBBLE_MASK]);
            break;
        }
    }
    out.append(run, end);
    out.push_back('"');
}

/**
 * @brief Whether a string needs more than quotes around it to become a JSON string: a control character, a quote, a
 * backslash or a byte of 0x80 or more. It can run in a constant expression, which the key type relies on.
 */
constexpr bool needs_escape(std::string_view s) noexcept
{
    constexpr unsigned FIRST_PRINTABLE = 0x20U; // Bytes below this are control characters.
    constexpr unsigned FIRST_NON_ASCII = 0x80U;
    // A loop, because std::any_of is not constexpr before C++20.
    for (const char ch : s) // NOLINT(readability-use-anyofallof)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (c < FIRST_PRINTABLE || c == '"' || c == '\\' || c >= FIRST_NON_ASCII)
        {
            return true;
        }
    }
    return false;
}

/** @brief Whether a key needs no escaping, so it can be copied between quotes. Scans the key once. */
template <typename K>
constexpr bool is_plain_key(const K &key) noexcept
{
    return !needs_escape(std::string_view(key));
}

/** @brief Appends text to a buffer unchanged. The buffer can be a std::string or an spdlog memory buffer. */
template <typename Buffer>
void append(Buffer &out, std::string_view s)
{
    out.append(s.data(), s.data() + s.size());
}

/** @brief Appends an integer in decimal. */
template <typename Buffer, typename T>
void append_int(Buffer &out, T value)
{
    static_assert(std::is_integral_v<T>, "append_int: expected an integer");
#if JSPDLOG_STD_FORMAT
    std::array<char, 24> digits{}; // enough for the 20 digits and the sign of any 64-bit integer
    const auto written = [&](auto number) {
        const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), number);
        out.append(digits.data(), result.ptr);
    };
    if constexpr (std::is_signed_v<T>)
    {
        written(static_cast<long long>(value));
    }
    else
    {
        written(static_cast<unsigned long long>(value));
    }
#else
    if constexpr (std::is_signed_v<T>)
    {
        const spdlog::fmt_lib::format_int text(static_cast<long long>(value));
        out.append(text.data(), text.data() + text.size());
    }
    else
    {
        const spdlog::fmt_lib::format_int text(static_cast<unsigned long long>(value));
        out.append(text.data(), text.data() + text.size());
    }
#endif
}

/** @brief Views spdlog's string view type as a std::string_view. */
template <typename View>
std::string_view view(const View &v) noexcept
{
    return std::string_view{v.data(), v.size()};
}

/** @brief The level of a message, whose member is named differently in spdlog v1 and v2. */
inline level msg_level(const spdlog::details::log_msg &msg) noexcept
{
#if JSPDLOG_SPDLOG_V2
    return msg.log_level;
#else
    return msg.level;
#endif
}

/** @brief The name of a level as spdlog spells it, including names customized with SPDLOG_LEVEL_NAMES. */
inline std::string_view level_name(level lvl) noexcept
{
#if JSPDLOG_SPDLOG_V2
    return spdlog::to_string_view(lvl);
#else
    return view(spdlog::level::to_string_view(lvl));
#endif
}

/** @brief The value of a lowercase hexadecimal digit, or -1 for any other character. */
inline int hex_value(char c) noexcept
{
    static constexpr int FIRST_LETTER_VALUE = 10;
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + FIRST_LETTER_VALUE;
    }
    return -1;
}

/** @brief Size of the token that starts every payload written by a json_logger. */
inline constexpr std::size_t TOKEN_SIZE = 32;
/** @brief Number of hexadecimal digits that give the size of the fields in a payload. */
inline constexpr std::size_t SIZE_DIGITS = 8;
/** @brief Size of the payload up to the first byte of the fields: token, kind and size. */
inline constexpr std::size_t PAYLOAD_HEADER_SIZE = TOKEN_SIZE + 1 + SIZE_DIGITS;
/** @brief Payload kind: fields only. */
inline constexpr char KIND_FIELDS = 'F';
/** @brief Payload kind: fields followed by a message. */
inline constexpr char KIND_MESSAGE = 'M';
/** @brief Largest fields size the payload can hold. */
inline constexpr std::size_t MAX_FIELDS_SIZE = 0xffffffffU;

/** @brief The token that marks a payload as written by a json_logger. */
using payload_token = std::array<char, TOKEN_SIZE>;

/**
 * @brief Returns the token shared by every json_logger and json_formatter of the program.
 *
 * It is random per run and never changes after the first call. A message from outside jspdlog can only be mistaken for
 * a payload if it starts with the token.
 */
inline const payload_token &process_token()
{
    static const payload_token TOKEN = [] {
        static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        static constexpr std::size_t PREFIX_SIZE = 8;
        static constexpr unsigned ALPHABET_MASK = 63U;
        static constexpr std::size_t RANDOM_WORDS = 8;
        std::array<std::uint32_t, RANDOM_WORDS + 2> seed{};
        try
        {
            std::random_device rd;
            for (std::size_t i = 0; i < RANDOM_WORDS; ++i)
            {
                seed[i] = rd();
            }
        }
        catch (...) // NOLINT(bugprone-empty-catch): no entropy source, the values below still make the token unique
        {
        }
        // Mixed in so that the token differs per run even when random_device is deterministic.
        const int local = 0;
        seed[RANDOM_WORDS] = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        seed[RANDOM_WORDS + 1] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&local));
        std::seed_seq seq(seed.begin(), seed.end());
        std::mt19937_64 gen(seq);
        payload_token t{};
        std::memcpy(t.data(), "jspdlog:", PREFIX_SIZE);
        for (std::size_t i = PREFIX_SIZE; i < TOKEN_SIZE; ++i)
        {
            t[i] = ALPHABET[gen() & ALPHABET_MASK];
        }
        return t;
    }();
    return TOKEN;
}

/**
 * @brief Returns the id of the current process, as written under json_options::process_key.
 *
 * On POSIX systems getpid is a system call, which would cost more than the rest of a short line, so the id is read once
 * and cached. A handler registered with pthread_atfork clears the cache in the child after fork, so a forked child
 * writes its own id. A child created without the fork function of the C library, for example by a raw clone system
 * call, would keep the parent's id. If the handler cannot be registered, the id is read on every call. On Windows the
 * call is cheap and is not cached.
 */
inline std::size_t process_id() noexcept
{
#ifdef _WIN32
    return static_cast<std::size_t>(spdlog::details::os::pid());
#else
    static std::atomic<std::size_t> cached{0};
    static const bool CACHEABLE =
        ::pthread_atfork(nullptr, nullptr, [] { cached.store(0, std::memory_order_relaxed); }) == 0;
    if (!CACHEABLE)
    {
        return static_cast<std::size_t>(spdlog::details::os::pid());
    }
    std::size_t pid = cached.load(std::memory_order_relaxed);
    if (pid == 0)
    {
        pid = static_cast<std::size_t>(spdlog::details::os::pid());
        cached.store(pid, std::memory_order_relaxed);
    }
    return pid;
#endif
}

/** @brief What the formatter found in a payload. */
struct parsed_payload
{
    /** @brief Whether the payload was written by a json_logger with the expected token. */
    bool ours = false;
    /** @brief The fields, each entry starting with a comma. May be empty. */
    std::string_view fields;
    /** @brief The message as a quoted JSON string, or empty when the line has no message. */
    std::string_view message;
};

/**
 * @brief Splits a payload into fields and message, or reports that it is not a payload of this process.
 *
 * Anything that does not match the protocol exactly is reported as foreign, so a damaged payload ends up escaped in the
 * message instead of breaking the line.
 */
inline parsed_payload parse_payload(std::string_view p, const payload_token &token) noexcept
{
    if (p.size() < PAYLOAD_HEADER_SIZE || std::memcmp(p.data(), token.data(), TOKEN_SIZE) != 0)
    {
        return {};
    }
    static constexpr unsigned NIBBLE_BITS = 4U;
    const char kind = p[TOKEN_SIZE];
    std::size_t n = 0;
    for (std::size_t i = 0; i < SIZE_DIGITS; ++i)
    {
        const int d = hex_value(p[TOKEN_SIZE + 1 + i]);
        if (d < 0)
        {
            return {};
        }
        n = (n << NIBBLE_BITS) | static_cast<std::size_t>(d);
    }
    const std::string_view rest = p.substr(PAYLOAD_HEADER_SIZE);
    const bool kind_ok = (kind == KIND_FIELDS && n == rest.size()) || (kind == KIND_MESSAGE && n < rest.size());
    if (!kind_ok)
    {
        return {};
    }
    const std::string_view fields = rest.substr(0, n);
    const std::string_view message = rest.substr(n);
    const bool fields_ok = fields.empty() || fields.front() == ',';
    const bool message_ok = message.empty() || (message.size() >= 2 && message.front() == '"' && message.back() == '"');
    if (!fields_ok || !message_ok)
    {
        return {};
    }
    return {true, fields, message};
}

/**
 * @brief Writes the size of the fields into the placeholder that precedes them in a payload.
 *
 * Throws std::length_error when the fields do not fit the fixed-width size.
 */
inline void patch_hex_size(spdlog::memory_buf_t &out, std::size_t at, std::size_t size)
{
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    static constexpr unsigned NIBBLE_BITS = 4U;
    static constexpr unsigned NIBBLE_MASK = 0xFU;
    if (size > MAX_FIELDS_SIZE)
    {
        throw std::length_error("jspdlog: fields larger than 4 GiB");
    }
    for (std::size_t i = 0; i < SIZE_DIGITS; ++i)
    {
        const unsigned shift = NIBBLE_BITS * static_cast<unsigned>(SIZE_DIGITS - 1 - i);
        out.data()[at + i] = HEX_DIGITS[(size >> shift) & NIBBLE_MASK];
    }
}

/**
 * @brief Appends the start of a payload: the token, the kind, the size of the fields and the fields that write_fields
 * appends. The caller then appends the message. Used by json_logger and by the error handler, so both write the same
 * protocol.
 */
template <typename WriteFields>
void write_payload(spdlog::memory_buf_t &out, const payload_token &token, char kind, const WriteFields &write_fields)
{
    append(out, std::string_view{token.data(), token.size()});
    out.push_back(kind);
    const std::size_t size_at = out.size();
    append(out, std::string_view{"00000000", SIZE_DIGITS});
    const std::size_t fields_at = out.size();
    write_fields(out);
    patch_hex_size(out, size_at, out.size() - fields_at);
}

/**
 * @brief Hands spdlog the function that writes a line's fields and message.
 *
 * json_logger logs it with the format string "{}", so the user's message is formatted while spdlog formats the line.
 * An exception thrown by a user formatter then reaches the logger's error handler, as it would for a plain spdlog call,
 * and the logging call does not throw.
 */
template <typename Write>
struct deferred_payload
{
    /** @brief Appends the fields and message to a buffer. */
    const Write *write;
};

} // namespace detail
} // namespace jspdlog

#if JSPDLOG_STD_FORMAT
/** @brief Formats a jspdlog::detail::deferred_payload by running its write function. */
template <typename Write>
struct std::formatter<jspdlog::detail::deferred_payload<Write>, char>
{
    /** @brief Accepts only an empty format specification. */
    constexpr auto parse(std::format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.begin(); }
#else
/** @brief Formats a jspdlog::detail::deferred_payload by running its write function. */
template <typename Write>
struct fmt::formatter<jspdlog::detail::deferred_payload<Write>>
{
    /** @brief Accepts only an empty format specification. */
    constexpr auto parse(fmt::format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.begin(); }
#endif

    /** @brief Runs the write function and copies its output. */
    template <typename Context>
    auto format(const jspdlog::detail::deferred_payload<Write> &payload, Context &ctx) const -> decltype(ctx.out())
    {
        spdlog::memory_buf_t buf;
        (*payload.write)(buf);
        return std::copy(buf.begin(), buf.end(), ctx.out());
    }
};

namespace jspdlog
{

class json_logger;

/**
 * @brief An spdlog formatter that renders every message as one JSON object.
 *
 * The line holds the header fields chosen in json_options, the call site if source_key is set (as an object, or as
 * three fields with source_flat), the fields of a
 * json_logger call and the message. A payload written by a json_logger is split into fields and message. Any other
 * payload is escaped whole into the message field, so the line is valid JSON whoever logged it.
 *
 * Like spdlog's own formatters it keeps a timestamp cache and is called under the sink's mutex, so give each sink its
 * own instance. Use install_json_formatter to install it, or call format from a callback sink.
 *
 * @code
 * auto formatter = std::make_shared<jspdlog::json_formatter>(options);
 * auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>([formatter](const spdlog::details::log_msg &msg) {
 *     spdlog::memory_buf_t line;
 *     formatter->format(msg, line);
 *     send(std::string_view{line.data(), line.size()});
 * });
 * @endcode
 */
class json_formatter final : public spdlog::formatter
{
public:
    /** @brief Creates a formatter for the given options. */
    explicit json_formatter(json_options options = {})
        : json_formatter(std::move(options), detail::process_token())
    {
    }

    /** @brief Appends one line, ending in the configured line terminator, to dest. */
    void format(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest) override
    {
        bool first = true;
        const auto key = [&](const std::string &quoted_key_colon) {
            if (!first)
            {
                dest.push_back(',');
            }
            first = false;
            detail::append(dest, quoted_key_colon);
        };

        dest.push_back('{');
        if (opts_.timestamp_key)
        {
            key(timestamp_key_);
            append_timestamp_(msg, dest);
        }
        if (opts_.logger_key)
        {
            key(logger_key_);
            detail::append_json_quoted(dest, detail::view(msg.logger_name));
        }
        if (opts_.level_key)
        {
            key(level_key_);
            detail::append(dest, level_texts_[level_index_(msg)]);
        }
        if (opts_.severity_key)
        {
            key(severity_key_);
            detail::append(dest, severity_texts_[level_index_(msg)]);
        }
        if (opts_.process_key)
        {
            key(process_key_);
            detail::append_int(dest, detail::process_id());
        }
        if (opts_.thread_key)
        {
            key(thread_key_);
            detail::append_int(dest, msg.thread_id);
        }
        if (opts_.source_key && !msg.source.empty())
        {
            if (opts_.source_flat)
            {
                const std::string_view file = source_file_(msg.source);
                key(source_file_key_);
                detail::append_json_quoted(dest, file);
                key(source_line_key_);
                detail::append_int(dest, msg.source.line);
                key(source_function_key_);
                detail::append_json_quoted(
                    dest, std::string_view{msg.source.funcname != nullptr ? msg.source.funcname : ""}
                );
            }
            else
            {
                key(source_key_);
                append_source_(msg.source, dest);
            }
        }

        const std::string_view payload = detail::view(msg.payload);
        const detail::parsed_payload parsed = detail::parse_payload(payload, token_);
        if (parsed.ours)
        {
            std::string_view fields = parsed.fields;
            if (first && !fields.empty())
            {
                fields.remove_prefix(1); // The first entry needs no comma.
                first = false;
            }
            detail::append(dest, fields);
            if (!parsed.message.empty())
            {
                key(message_key_);
                detail::append(dest, parsed.message);
            }
        }
        else
        {
            key(message_key_);
            detail::append_json_quoted(dest, payload);
        }
        dest.push_back('}');
        detail::append(dest, eol_);
    }

    /** @brief Returns a copy with the same options and token and an empty timestamp cache. */
    [[nodiscard]] std::unique_ptr<spdlog::formatter> clone() const override
    {
        return std::unique_ptr<spdlog::formatter>(new json_formatter(opts_, token_));
    }

private:
    friend class json_logger;

    /** @brief Creates a formatter that recognizes payloads with the given token, and escapes the keys once. */
    json_formatter(json_options options, const detail::payload_token &token)
        : opts_(std::move(options)),
          token_(token),
          eol_(opts_.eol.value_or(spdlog::details::os::default_eol)),
          message_key_(quote_key_(opts_.message_key))
    {
        const auto prepare = [](const std::optional<std::string> &key) {
            return key ? quote_key_(*key) : std::string{};
        };
        timestamp_key_ = prepare(opts_.timestamp_key);
        logger_key_ = prepare(opts_.logger_key);
        level_key_ = prepare(opts_.level_key);
        severity_key_ = prepare(opts_.severity_key);
        process_key_ = prepare(opts_.process_key);
        thread_key_ = prepare(opts_.thread_key);
        source_key_ = prepare(opts_.source_key);
        source_file_key_ = quote_key_(opts_.source_file_key);
        source_line_key_ = quote_key_(opts_.source_line_key);
        source_function_key_ = quote_key_(opts_.source_function_key);
        for (std::size_t i = 0; i < LEVEL_COUNT; ++i)
        {
            const std::string_view name = opts_.level_names
                                              ? std::string_view{(*opts_.level_names)[i]}
                                              : detail::level_name(static_cast<level>(static_cast<int>(i)));
            detail::append_json_quoted(level_texts_[i], name);
            severity_texts_[i] = std::to_string(opts_.severity_numbers[i]);
        }
    }

    /** @brief The number of levels, from trace to off. */
    static constexpr std::size_t LEVEL_COUNT = detail::LEVEL_COUNT;
    /** @brief The digits of the fraction of a second in each RFC 3339 style. */
    static constexpr int MILLI_DIGITS = 3;
    static constexpr int MICRO_DIGITS = 6;
    static constexpr int NANO_DIGITS = 9;
    static_assert(static_cast<std::size_t>(level::off) == LEVEL_COUNT - 1, "level::off must be the last level");

    /** @brief The index of the level of a message in the level tables, which is the level's number. */
    static std::size_t level_index_(const spdlog::details::log_msg &msg) noexcept
    {
        const auto index = static_cast<std::size_t>(detail::msg_level(msg));
        return index < LEVEL_COUNT ? index : LEVEL_COUNT - 1;
    }

    /** @brief Returns the key as a quoted JSON string followed by a colon. */
    static std::string quote_key_(const std::string &key)
    {
        std::string out;
        detail::append_json_quoted(out, key);
        out.push_back(':');
        return out;
    }

    /** @brief Appends two decimal digits of a value from 0 to 99. */
    template <typename Buffer>
    static void append_2_(Buffer &out, int value)
    {
        static constexpr int BASE = 10;
        out.push_back(static_cast<char>('0' + (value / BASE)));
        out.push_back(static_cast<char>('0' + (value % BASE)));
    }

    /** @brief Appends exactly digits decimal digits of value, with leading zeros. */
    template <typename Buffer>
    static void append_digits_(Buffer &out, std::int64_t value, int digits)
    {
        static constexpr std::int64_t BASE = 10;
        std::array<char, NANO_DIGITS> text{}; // The fraction of a second has at most nine digits.
        for (int i = digits; i > 0; --i)
        {
            text[static_cast<std::size_t>(i - 1)] = static_cast<char>('0' + (value % BASE));
            value /= BASE;
        }
        out.append(text.data(), text.data() + digits);
    }

    /**
     * @brief Appends the timestamp as chosen by timestamp_style.
     *
     * An RFC 3339 string rebuilds its cached date and time part once per second.
     */
    void append_timestamp_(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest)
    {
        static constexpr int YEAR_BASE = 1900;
        static constexpr int CENTURY = 100;
        static constexpr int MINUTES_PER_HOUR = 60;
        static constexpr std::int64_t NANOS_PER_MILLI = 1'000'000;
        static constexpr std::int64_t NANOS_PER_MICRO = 1'000;
        const auto since_epoch = msg.time.time_since_epoch();
        if (opts_.timestamp_style == timestamp_format::epoch_millis)
        {
            detail::append_int(dest, std::chrono::floor<std::chrono::milliseconds>(since_epoch).count());
            return;
        }
        // floor keeps the fraction in 0 to 999999999 nanoseconds for times before the epoch too.
        const auto whole = std::chrono::floor<std::chrono::seconds>(since_epoch);
        const std::int64_t nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch - whole).count();
        const std::time_t second = spdlog::log_clock::to_time_t(spdlog::log_clock::time_point{whole});
        if (!cached_second_ || second != *cached_second_)
        {
            const bool utc = opts_.time_type == spdlog::pattern_time_type::utc;
            const std::tm tm = utc ? spdlog::details::os::gmtime(second) : spdlog::details::os::localtime(second);
            cached_prefix_.clear();
            cached_prefix_.push_back('"');
            append_2_(cached_prefix_, (tm.tm_year + YEAR_BASE) / CENTURY);
            append_2_(cached_prefix_, (tm.tm_year + YEAR_BASE) % CENTURY);
            cached_prefix_.push_back('-');
            append_2_(cached_prefix_, tm.tm_mon + 1);
            cached_prefix_.push_back('-');
            append_2_(cached_prefix_, tm.tm_mday);
            cached_prefix_.push_back('T');
            append_2_(cached_prefix_, tm.tm_hour);
            cached_prefix_.push_back(':');
            append_2_(cached_prefix_, tm.tm_min);
            cached_prefix_.push_back(':');
            append_2_(cached_prefix_, tm.tm_sec);
            cached_prefix_.push_back('.');
            zone_.clear();
            if (utc)
            {
                zone_ = "Z\"";
            }
            else
            {
                const int offset = spdlog::details::os::utc_minutes_offset(tm);
                const int magnitude = offset < 0 ? -offset : offset;
                zone_.push_back(offset < 0 ? '-' : '+');
                append_2_(zone_, magnitude / MINUTES_PER_HOUR);
                zone_.push_back(':');
                append_2_(zone_, magnitude % MINUTES_PER_HOUR);
                zone_.push_back('"');
            }
            cached_second_ = second;
        }
        detail::append(dest, cached_prefix_);
        switch (opts_.timestamp_style)
        {
        case timestamp_format::rfc3339_micros:
            append_digits_(dest, nanos / NANOS_PER_MICRO, MICRO_DIGITS);
            break;
        case timestamp_format::rfc3339_nanos:
            append_digits_(dest, nanos, NANO_DIGITS);
            break;
        case timestamp_format::rfc3339_millis:
        case timestamp_format::epoch_millis: // Returned above.
            append_digits_(dest, nanos / NANOS_PER_MILLI, MILLI_DIGITS);
            break;
        }
        detail::append(dest, zone_);
    }

    /** @brief The base name of the file of a call site. */
    static std::string_view source_file_(const spdlog::source_loc &src) noexcept
    {
        std::string_view file{src.filename != nullptr ? src.filename : ""};
        if (const auto slash = file.find_last_of("/\\"); slash != std::string_view::npos)
        {
            file.remove_prefix(slash + 1);
        }
        return file;
    }

    /**
     * @brief Appends the call site as an object, with the base name of the file.
     *
     * Nothing is cached, because a file name pointer may be reused for different text once a dynamic call site ends.
     */
    void append_source_(const spdlog::source_loc &src, spdlog::memory_buf_t &dest) const
    {
        dest.push_back('{');
        detail::append(dest, source_file_key_);
        detail::append_json_quoted(dest, source_file_(src));
        dest.push_back(',');
        detail::append(dest, source_line_key_);
        detail::append_int(dest, src.line);
        dest.push_back(',');
        detail::append(dest, source_function_key_);
        detail::append_json_quoted(dest, std::string_view{src.funcname != nullptr ? src.funcname : ""});
        dest.push_back('}');
    }

    /** @brief The options the formatter was created with. */
    json_options opts_;
    /** @brief The token that marks payloads written by a json_logger. */
    detail::payload_token token_;
    /** @brief The line terminator. */
    std::string eol_;
    /** @brief The message key, quoted and followed by a colon. */
    std::string message_key_;
    /** @brief The header keys, quoted and followed by a colon. Empty for keys that are left out. */
    std::string timestamp_key_, logger_key_, level_key_, severity_key_, process_key_, thread_key_, source_key_;
    /** @brief The call site keys, quoted and followed by a colon. */
    std::string source_file_key_, source_line_key_, source_function_key_;
    /** @brief The level names as quoted JSON strings, indexed by level. */
    std::array<std::string, LEVEL_COUNT> level_texts_;
    /** @brief The severity numbers in decimal, indexed by level. */
    std::array<std::string, LEVEL_COUNT> severity_texts_;
    /** @brief The second of the cached timestamp part, empty before the first line. */
    std::optional<std::time_t> cached_second_;
    /** @brief The quoted date and time up to the decimal point. */
    std::string cached_prefix_;
    /** @brief The UTC offset, or Z, with the closing quote. */
    std::string zone_;
};

/**
 * @brief Makes a logger that jspdlog does not wrap write JSON lines.
 *
 * Plain messages become {"message":"..."} with the header fields of the options. Use it on the default logger or on
 * third-party loggers. It replaces the formatter of every sink of the logger.
 */
inline void install_json_formatter(spdlog::logger &logger, const json_options &options = {})
{
    logger.set_formatter(std::make_unique<json_formatter>(options));
}

/** @brief The same for one sink, for example a sink added to a json_logger's spdlog::logger after construction. */
inline void install_json_formatter(spdlog::sinks::sink &sink, const json_options &options = {})
{
    sink.set_formatter(std::make_unique<json_formatter>(options));
}

/**
 * @brief Ready-made json_options for the field names of common log ingestion schemas.
 *
 * A preset is a function that returns a filled json_options, so change any member afterwards. Each one leaves the call
 * site off: set source_key to any text, or to the key the schema names, to turn it on. Exceptions logged through the
 * exception call forms go under the key the schema uses for them. Bind schema fields that are not per call, such as
 * ecs.version, with json_logger::with.
 *
 * @code
 * auto options = jspdlog::presets::ecs();
 * options.source_key = "source"; // also write the call site
 * jspdlog::json_logger logger("app", sink, options);
 * @endcode
 */
namespace presets
{

/** @brief The table of level names of json_options, from trace to off. */
using level_name_table = std::array<std::string, jspdlog::detail::LEVEL_COUNT>;

/**
 * @brief Elastic Common Schema: @timestamp in UTC, log.level, log.logger, process.pid, process.thread.id, the call
 * site as log.origin.file.name, log.origin.file.line and log.origin.function, and exceptions under error with type and
 * message, which are error.type and error.message once Elasticsearch expands the dotted keys. ECS requires ecs.version
 * in every event; bind it with json_logger::with, since only you know which version your index templates use.
 */
inline json_options ecs()
{
    json_options o;
    o.timestamp_key = "@timestamp";
    o.time_type = spdlog::pattern_time_type::utc;
    o.level_key = "log.level";
    o.level_names = level_name_table{{"trace", "debug", "info", "warn", "error", "critical", "off"}};
    o.logger_key = "log.logger";
    o.process_key = "process.pid";
    o.thread_key = "process.thread.id";
    o.source_flat = true;
    o.source_file_key = "log.origin.file.name";
    o.source_line_key = "log.origin.file.line";
    o.source_function_key = "log.origin.function";
    o.eol = "\n";
    return o;
}

/**
 * @brief Google Cloud Logging structured logs, as read from stdout by Cloud Run, GKE and the Ops Agent: severity, time
 * as an RFC 3339 string with nanoseconds in UTC, and the message. The logger, process and thread fields stay, as
 * fields of the JSON payload. The call site is off. To write it where Cloud Logging
 * looks, set source_key to logging.googleapis.com/sourceLocation: its file, line and function members already have
 * the names of that field.
 */
inline json_options gcp()
{
    json_options o;
    o.timestamp_key = "time";
    o.time_type = spdlog::pattern_time_type::utc;
    o.timestamp_style = timestamp_format::rfc3339_nanos;
    o.level_key = "severity";
    o.level_names = level_name_table{{"DEBUG", "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL", "DEFAULT"}};
    o.eol = "\n";
    return o;
}

/**
 * @brief Datadog log attributes: the status that Datadog remaps, the date as epoch milliseconds, logger.name and
 * logger.thread_name, and exceptions under error with kind and message.
 */
inline json_options datadog()
{
    json_options o;
    o.timestamp_key = "timestamp";
    o.timestamp_style = timestamp_format::epoch_millis;
    o.level_key = "status";
    o.level_names = level_name_table{{"trace", "debug", "info", "warn", "error", "critical", "off"}};
    o.logger_key = "logger.name";
    o.thread_key = "logger.thread_name";
    o.process_key = std::nullopt;
    o.error_type_key = "kind";
    o.eol = "\n";
    return o;
}

/**
 * @brief The OpenTelemetry log data model as flat JSON, for a collector's filelog receiver: timestamp, severity_text,
 * severity_number and body, with the call site as code.file.path (the base name), code.line.number and
 * code.function.name, process.pid, thread.id and exceptions under exception with type and message.
 */
inline json_options otel()
{
    json_options o;
    o.timestamp_key = "timestamp";
    o.time_type = spdlog::pattern_time_type::utc;
    o.timestamp_style = timestamp_format::rfc3339_nanos;
    o.level_key = "severity_text";
    o.level_names = level_name_table{{"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"}};
    o.severity_key = "severity_number";
    o.message_key = "body";
    o.logger_key = "logger";
    o.process_key = "process.pid";
    o.thread_key = "thread.id";
    o.source_flat = true;
    o.source_file_key = "code.file.path";
    o.source_line_key = "code.line.number";
    o.source_function_key = "code.function.name";
    o.error_key = "exception";
    o.eol = "\n";
    return o;
}

} // namespace presets

class fields;

/** @brief Implementation details. Not part of the public API. */
namespace detail
{
struct writer_access;

/**
 * @brief Called by a key that needs escaping. It is not a constant expression, so in C++20 the key does not compile;
 * in C++17 the key is checked when it is created and this throws std::invalid_argument.
 */
[[noreturn]] inline void key_needs_escape()
{
#if JSPDLOG_HAS_EXCEPTIONS
    throw std::invalid_argument(
        "jspdlog: a key needs escaping: it holds a control character, a quote, a backslash or "
        "a byte of 0x80 or more"
    );
#else
    std::abort();
#endif
}

struct key_access;
} // namespace detail

/**
 * @brief A field key checked when the program is compiled, so inserting it copies the key between quotes without a
 * scan. A key with a control character, a quote, a backslash or a byte of 0x80 or more does not compile in C++20; in
 * C++17 the check runs when the key is created and throws std::invalid_argument. Build one with the _key literal or
 * with key{"name"}.
 *
 * @code
 * using namespace jspdlog::literals;
 * logger.info({"user_id"_key, 42}, "logged in");
 * @endcode
 */
class key
{
public:
    /** @brief Checks a string literal. Explicit, so that a plain literal stays a std::string_view. */
    template <std::size_t N>
    explicit JSPDLOG_CONSTEVAL key(const char (&s)[N])
        : text_(s, N - 1)
    {
        check_();
    }

    /**
     * @brief A writable char array is not a key: its contents can change after the check and its size counts every
     * byte, not the text. Pass its text as a std::string_view key instead.
     */
    template <std::size_t N>
    explicit key(char (&s)[N]) = delete;

    /** @brief The key text. */
    [[nodiscard]] constexpr std::string_view view() const noexcept { return text_; }

    /** @brief Lets a key go wherever a string key is taken. */
    // Implicit on purpose: a key is a string key that is known to need no escaping.
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor,misc-explicit-constructor)
    constexpr operator std::string_view() const noexcept { return text_; }

private:
    friend struct detail::key_access;

    /** @brief Checks the text of a literal operator. */
    JSPDLOG_CONSTEVAL key(const char *s, std::size_t n)
        : text_(s, n)
    {
        check_();
    }

    /** @brief Fails to compile, or throws, when the key needs escaping. */
    constexpr void check_() const
    {
        if (detail::needs_escape(text_))
        {
            detail::key_needs_escape();
        }
    }

    /** @brief The key text. */
    std::string_view text_;
};

namespace detail
{
/** @brief Gives the literal operator the private constructor of key. */
struct key_access
{
    /** @brief Creates a key from the characters of a user-defined literal. */
    static JSPDLOG_CONSTEVAL key make(const char *s, std::size_t n) { return {s, n}; }
};

/** @brief A key type is known to need no escaping. */
constexpr bool is_plain_key(const key & /*key*/) noexcept
{
    return true;
}
} // namespace detail

namespace literals
{
/** @brief "user_id"_key is a key checked when the program is compiled. */
JSPDLOG_CONSTEVAL key operator""_key(const char *s, std::size_t n)
{
    return detail::key_access::make(s, n);
}
} // namespace literals

namespace detail
{
/**
 * @brief Where a json_writer appends: a std::string, or spdlog's own memory buffer when json_logger writes a line.
 *
 * json_logger writes per-call values straight into spdlog's buffer, with no intermediate string and one copy fewer per
 * value. Each append branches on which target is set; both are written the same way, so the output is identical.
 */
class output_ref
{
public:
    /** @brief Appends to a string. */
    explicit output_ref(std::string &s) noexcept
        : string_(&s)
    {
    }
    /** @brief Appends to a buffer. */
    explicit output_ref(
        spdlog::memory_buf_t &b, int /*tag: tells the buffer from the string when they are one type*/
    ) noexcept
        : buffer_(&b)
    {
    }
    /** @brief Appends one character. */
    void push_back(char c)
    {
        if (buffer_ != nullptr)
        {
            buffer_->push_back(c);
        }
        else
        {
            string_->push_back(c);
        }
    }
    /** @brief Appends the characters in [first, last). */
    void append(const char *first, const char *last)
    {
        if (buffer_ != nullptr)
        {
            buffer_->append(first, last);
        }
        else
        {
            string_->append(first, last);
        }
    }

private:
    /** @brief The string, or null when writing to a buffer. */
    std::string *string_ = nullptr;
    /** @brief The buffer, or null when writing to a string. */
    spdlog::memory_buf_t *buffer_ = nullptr;
};
} // namespace detail

/**
 * @brief Specialize to give a type you do not own a JSON form. The primary template has no write member.
 *
 * Use it for types from other libraries or the standard library. For your own types, a jspdlog_serialize function next
 * to the type is shorter. The second parameter lets one specialization cover a family of types.
 *
 * @code
 * template <>
 * struct jspdlog::serializer<QString>
 * {
 *     static void write(jspdlog::json_writer &w, const QString &s) { w.value(s.toStdString()); }
 * };
 * @endcode
 */
template <typename T, typename Enable = void>
struct serializer
{
};

/**
 * @brief Writes one JSON value into a buffer. Used by fields for every value and passed to user serializers.
 *
 * value writes anything that has a JSON form:
 *
 * - bool, integers, floating-point numbers (NaN and infinity are null), nullptr and a null C string (null), std::string
 *   and anything that converts to std::string_view, char (a one-character string), raw_json and fields.
 * - std::optional (the value, or null when empty), std::variant (the active alternative, null when valueless) and
 *   std::monostate (null).
 * - Enums: the name when {fmt} has a formatter for the enum (a fmt::formatter specialization, or format_as in {fmt} 10
 *   and newer, which may also return a number), otherwise the number.
 * - std::chrono::duration: milliseconds. A duration with an integer representation and a period that is a whole number
 *   of milliseconds is written as an integer, any other duration as a floating-point number, so the JSON type depends
 *   on the C++ type only. Put the unit in the key, for example elapsed_ms.
 * - std::chrono::system_clock time points of any duration: an RFC 3339 UTC string with milliseconds, such as
 *   2026-11-23T08:00:00.123Z. Years outside 0000 to 9999 are written as null. Other clocks are rejected.
 * - Ranges (std::vector, std::array, std::list, std::set, std::span, C arrays and anything with begin and end): arrays.
 * - Maps whose key converts to std::string_view: objects, in the iteration order of the map. A std::multimap can write
 *   a key twice.
 * - std::pair and std::tuple, and maps with other keys (as an array of [key, value] pairs): arrays.
 * - Exceptions (anything derived from std::exception) and std::exception_ptr: an object with the type name and the
 *   message, and the nested causes as a chain under cause. A null std::exception_ptr is null.
 * - Your own types, through jspdlog_serialize or serializer. They win over the exception form.
 *
 * Everything is recursive, so a std::map of std::optional of std::vector of your own type works. Wide characters,
 * pointers other than C strings and types without a JSON form are rejected at compile time. std::filesystem::path is
 * one of them: convert it with string() or u8string().
 *
 * Commas are inserted automatically. Inside an object call key before each value, inside an array call value for each
 * element. A serializer must write exactly one value. The writer does not validate structure in release builds, like
 * raw_json; debug builds check what is cheap to check through JSPDLOG_ASSERT.
 *
 * @code
 * namespace shop
 * {
 * inline void jspdlog_serialize(jspdlog::json_writer &w, const money &m)
 * {
 *     w.begin_object();
 *     w.member("cents", m.cents);
 *     w.member("currency", m.currency);
 *     w.end_object();
 * }
 * } // namespace shop
 * @endcode
 */
class json_writer
{
public:
    /** @brief Creates a writer that appends to out. The buffer must outlive the writer. */
    explicit json_writer(std::string &out) noexcept
        : out_(out)
    {
    }

    /** @brief Writes any supported value, including containers and types with a serializer. */
    template <typename T>
    void value(const T &v);

    /** @brief Writes pre-serialized JSON unchanged, or null when it is empty. */
    void raw(std::string_view json) { scalar_(json.empty() ? std::string_view{"null"} : json); }

    /** @brief Opens an object. Call key and value alternately, then end_object. */
    void begin_object() { open_('{'); }
    /** @brief Writes a key inside an object. The next call must write its value. */
    void key(std::string_view k)
    {
        JSPDLOG_ASSERT(depth_ > 0 && !after_key_, "jspdlog: key outside a container, or two keys in a row");
        separate_();
        detail::append_json_quoted(out_, k);
        out_.push_back(':');
        after_key_ = true;
    }
    /** @brief Closes the innermost object. */
    void end_object() { close_('}'); }
    /** @brief Opens an array. Call value for each element, then end_array. */
    void begin_array() { open_('['); }
    /** @brief Closes the innermost array. */
    void end_array() { close_(']'); }

    /** @brief Writes key(k) followed by value(v). */
    template <typename T>
    void member(std::string_view k, const T &v)
    {
        key(k);
        value(v);
    }

private:
    friend struct detail::writer_access;
    friend class fields;

    /** @brief Creates a writer that appends to spdlog's buffer. Used by json_logger through writer_access. */
    json_writer(spdlog::memory_buf_t &out, int tag) noexcept
        : out_(out, tag)
    {
    }

    /** @brief Writes a comma when a value precedes this one in the same container. */
    void separate_()
    {
        JSPDLOG_ASSERT(!need_comma_ || depth_ > 0, "jspdlog: a serializer wrote more than one value");
        if (need_comma_)
        {
            out_.push_back(',');
        }
        need_comma_ = false;
    }

    /** @brief Marks that a value was completed. */
    void done_() noexcept
    {
        need_comma_ = true;
        after_key_ = false;
    }

    /** @brief Writes text that is already valid JSON as one value. */
    void scalar_(std::string_view text)
    {
        separate_();
        detail::append(out_, text);
        done_();
    }

    /** @brief Writes a quoted, escaped string. */
    void string_(std::string_view text)
    {
        separate_();
        detail::append_json_quoted(out_, text);
        done_();
    }

    /** @brief Writes an integer in decimal. */
    template <typename T>
    void integer_(T number)
    {
        separate_();
        detail::append_int(out_, number);
        done_();
    }

    /**
     * @brief Writes a floating-point number. A whole number keeps a trailing .0, so consumers that infer types from the
     * text still see a float. NaN and infinity become null, because JSON has no token for them.
     */
    template <typename T>
    void floating_(T number)
    {
        if (!std::isfinite(number))
        {
            scalar_("null");
            return;
        }
        std::string text = spdlog::fmt_lib::format("{}", number);
        if (text.find_first_of(".eE") == std::string::npos)
        {
            text += ".0";
        }
        scalar_(text);
    }

    /** @brief Opens an object or an array. */
    void open_(char bracket)
    {
        separate_();
        out_.push_back(bracket);
        ++depth_;
        need_comma_ = false;
        after_key_ = false;
    }

    /** @brief Closes an object or an array. */
    void close_(char bracket)
    {
        JSPDLOG_ASSERT(depth_ > 0 && !after_key_, "jspdlog: closing without an open container, or after a key");
        --depth_;
        out_.push_back(bracket);
        done_();
    }

    /** @brief Whether exactly one value was written and nothing is left open. */
    [[nodiscard]] bool complete_() const noexcept { return depth_ == 0 && !after_key_ && need_comma_; }

    /** @brief The string or buffer written to. */
    detail::output_ref out_;
    /** @brief Whether the next value or key needs a comma before it. */
    bool need_comma_ = false;
    /** @brief The number of open objects and arrays. Stored in every build, so the layout does not depend on NDEBUG. */
    int depth_ = 0;
    /** @brief Whether a key was written and its value is still missing. */
    bool after_key_ = false;
};

/**
 * @brief Typed key/value fields for a log line, serialized when they are inserted.
 *
 * Pass them per call, usually as a braced list such as {"user_id", 42, "ok", true}, or bind them to a child logger with
 * json_logger::with. Values keep their JSON type, so numbers and booleans stay filterable as such. Fields keep the
 * order in which their keys were first inserted. Inserting a key that exists replaces its value and keeps its position,
 * and when two sets are merged the right-hand side wins.
 *
 * Fields are meant to be small: a lookup scans the keys, which beats a map for the handful of keys a log line carries.
 * The serialized text of all fields lives in one string, so building a set costs two allocations however many fields
 * it has. A braced list passed to a logging call is not a fields object: it is serialized only when the line is
 * written.
 *
 * A key equal to one of the logger's header keys, to its source key or to its message key produces a duplicate key in
 * the line. Avoiding that is up to the caller; debug builds check it through JSPDLOG_ASSERT.
 */
class fields
{
public:
    /** @brief Creates an empty set. Add fields with insert or merge. */
    fields() = default;

    /**
     * @brief Creates fields from alternating keys and values. Keys may be anything convertible to std::string_view.
     */
    template <typename K, typename V, typename... Rest>
    fields(K &&key, V &&value, Rest &&...rest)
    {
        static_assert(sizeof...(Rest) % 2 == 0, "fields: expected key/value pairs");
        entries_.reserve((sizeof...(Rest) / 2) + 1);
        text_.reserve(estimate_size_(key, value, rest...));
        insert_pairs_(std::forward<K>(key), std::forward<V>(value), std::forward<Rest>(rest)...);
    }

    /**
     * @brief Inserts any value that has a JSON form: see json_writer for the list. The key is copied at once, so it may
     * be a temporary; a key that exists gets the new value at its old position.
     *
     * A type without a JSON form, a wide character and a pointer other than a C string are rejected at compile time.
     * Give your own types a form with jspdlog_serialize or a specialization of serializer. Throws std::length_error
     * when the fields would exceed 4 GiB.
     */
    template <typename T>
    void insert(std::string_view key, const T &value)
    {
        put_value_(key, !detail::needs_escape(key), value);
    }

    /** @brief Inserts a value under a key that was checked when the program was compiled, so the key is not scanned. */
    template <typename T>
    void insert(const jspdlog::key &k, const T &value)
    {
        put_value_(k.view(), true, value);
    }

    /**
     * @brief Adds every field of other. Where both have a key, the value from other wins and the key keeps its
     * position.
     */
    void merge(const fields &other)
    {
        if (&other == this)
        {
            return;
        }
        for (const entry &e : other.entries_)
        {
            const std::string_view text{other.text_.data() + e.begin, e.size};
            put_text_(other.key_of_(e), e.key_in_text, text);
        }
    }

    /** @brief Returns the merge of both sets. Where both have a key, the value from rhs wins. */
    friend fields operator+(fields lhs, const fields &rhs)
    {
        lhs.merge(rhs);
        return lhs;
    }

    /** @brief Whether there are no fields. */
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    /** @brief The number of distinct keys. */
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    /** @brief Whether a key is present. */
    [[nodiscard]] bool contains(std::string_view key) const noexcept { return index_of_(key) != entries_.size(); }
    /** @brief Removes every field. */
    void clear() noexcept
    {
        text_.clear();
        entries_.clear();
        keys_.clear();
    }

    /** @brief Returns the fields as a JSON object in insertion order. An empty set gives {}. */
    [[nodiscard]] std::string to_json() const
    {
        if (text_.empty())
        {
            return "{}";
        }
        std::string out = text_;
        out.front() = '{';
        out.push_back('}');
        return out;
    }

private:
    friend class json_logger;

    /** @brief Where one field lies in text_. */
    struct entry
    {
        /** @brief Offset of the comma that starts the field. */
        std::uint32_t begin;
        /** @brief Size of the whole field, from the comma to the end of the value. */
        std::uint32_t size;
        /** @brief Offset of the raw key: in text_ when the key needed no escaping, else in keys_. */
        std::uint32_t key_begin;
        /** @brief Size of the raw key. */
        std::uint32_t key_size;
        /** @brief Whether the raw key is the quoted key inside text_, as opposed to a copy in keys_. */
        bool key_in_text;
    };

    /** @brief Cuts text_ back to the size it had, unless released, so a field that fails half-written leaves no trace.
     */
    class truncate_guard
    {
    public:
        /** @brief Remembers the current size of s. */
        explicit truncate_guard(std::string &s) noexcept
            : s_(&s), size_(s.size())
        {
        }
        truncate_guard(const truncate_guard &) = delete;
        truncate_guard &operator=(const truncate_guard &) = delete;
        /** @brief Cuts the string back unless release was called. */
        ~truncate_guard()
        {
            if (s_ != nullptr)
            {
                s_->resize(size_);
            }
        }
        /** @brief Keeps what was appended. */
        void release() noexcept { s_ = nullptr; }

    private:
        std::string *s_;
        std::size_t size_;
    };

    /** @brief A rough size of the text of the pairs of the variadic constructor, so one allocation usually suffices. */
    template <typename K, typename V, typename... Rest>
    static std::size_t estimate_size_(const K &key, const V & /*value*/, const Rest &...rest) noexcept
    {
        static constexpr std::size_t PUNCTUATION_AND_VALUE = 16; // comma, quotes, colon and a short value
        std::size_t size = PUNCTUATION_AND_VALUE;
        if constexpr (std::is_convertible_v<const K &, std::string_view>)
        {
            size += std::string_view(key).size();
        }
        if constexpr (sizeof...(Rest) > 0)
        {
            size += estimate_size_(rest...);
        }
        return size;
    }

    template <typename K, typename V, typename... Rest>
    void insert_pairs_(K &&key, V &&value, Rest &&...rest)
    {
        static_assert(
            std::is_convertible_v<std::decay_t<K>, std::string_view>,
            "fields: key must be convertible to std::string_view"
        );
        put_value_(std::string_view(key), detail::is_plain_key(key), std::forward<V>(value));
        if constexpr (sizeof...(Rest) > 0)
        {
            insert_pairs_(std::forward<Rest>(rest)...);
        }
    }

    /** @brief The raw key of an entry. */
    [[nodiscard]] std::string_view key_of_(const entry &e) const noexcept
    {
        const char *base = e.key_in_text ? text_.data() : keys_.data();
        return {base + e.key_begin, e.key_size};
    }

    /** @brief The text of an entry: a comma, the quoted key, a colon and the value. */
    [[nodiscard]] std::string_view text_of_(std::size_t i) const noexcept
    {
        return {text_.data() + entries_[i].begin, entries_[i].size};
    }

    /** @brief The index of the entry with this raw key, or the number of entries when there is none. */
    [[nodiscard]] std::size_t index_of_(std::string_view key) const noexcept
    {
        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            if (key_of_(entries_[i]) == key)
            {
                return i;
            }
        }
        return entries_.size();
    }

    /** @brief Writes a comma, the quoted key, a colon and the value to a string. A plain key is copied unscanned. */
    template <typename T>
    static void write_field_(std::string &out, std::string_view key, bool plain_key, const T &value)
    {
        out.push_back(',');
        if (plain_key)
        {
            out.push_back('"');
            out.append(key.data(), key.size());
            out.push_back('"');
        }
        else
        {
            detail::append_json_quoted(out, key);
        }
        out.push_back(':');
        json_writer w(out);
        w.value(value);
        JSPDLOG_ASSERT(w.complete_(), "jspdlog: a serializer left an object or array open, or wrote no value");
    }

    /** @brief Stores a field from a value. A key that exists keeps its position. */
    template <typename T>
    void put_value_(std::string_view key, bool plain_key, const T &value)
    {
        const std::size_t i = index_of_(key);
        if (i != entries_.size())
        {
            std::string text;
            write_field_(text, key, plain_key, value);
            replace_(i, text);
            return;
        }
        truncate_guard guard(text_);
        const std::size_t begin = text_.size();
        write_field_(text_, key, plain_key, value);
        add_entry_(begin, key, plain_key);
        guard.release();
    }

    /** @brief Stores a field whose text is ready. A key that exists keeps its position. */
    void put_text_(std::string_view key, bool plain_key, std::string_view text)
    {
        const std::size_t i = index_of_(key);
        if (i != entries_.size())
        {
            replace_(i, text);
            return;
        }
        truncate_guard guard(text_);
        const std::size_t begin = text_.size();
        text_.append(text.data(), text.size());
        add_entry_(begin, key, plain_key);
        guard.release();
    }

    /** @brief Records the field that starts at begin and ends at the end of text_. */
    void add_entry_(std::size_t begin, std::string_view key, bool plain_key)
    {
        if (text_.size() > detail::MAX_FIELDS_SIZE)
        {
            throw std::length_error("jspdlog: fields larger than 4 GiB");
        }
        entry e{};
        e.begin = static_cast<std::uint32_t>(begin);
        e.size = static_cast<std::uint32_t>(text_.size() - begin);
        e.key_size = static_cast<std::uint32_t>(key.size());
        e.key_in_text = plain_key;
        if (plain_key)
        {
            // The quoted key follows the comma and the opening quote, so the raw key is a view into text_.
            e.key_begin = e.begin + 2;
            entries_.push_back(e);
            return;
        }
        e.key_begin = static_cast<std::uint32_t>(keys_.size());
        keys_.append(key.data(), key.size());
        push_with_key_(e);
    }

    /** @brief Adds an entry whose key was just appended to keys_, taking the key back out when adding fails. */
    void push_with_key_(const entry &e)
    {
#if JSPDLOG_HAS_EXCEPTIONS
        try
        {
            entries_.push_back(e);
        }
        catch (...)
        {
            keys_.resize(e.key_begin);
            throw;
        }
#else
        entries_.push_back(e);
#endif
    }

    /** @brief Replaces the text of entry i, moving the later fields. */
    void replace_(std::size_t i, std::string_view text)
    {
        entry &e = entries_[i];
        const std::size_t old_size = e.size;
        if (text_.size() - old_size + text.size() > detail::MAX_FIELDS_SIZE)
        {
            throw std::length_error("jspdlog: fields larger than 4 GiB");
        }
        text_.replace(e.begin, old_size, text.data(), text.size());
        e.size = static_cast<std::uint32_t>(text.size());
        const auto delta = static_cast<std::int64_t>(text.size()) - static_cast<std::int64_t>(old_size);
        for (std::size_t j = i + 1; j < entries_.size(); ++j)
        {
            entries_[j].begin = static_cast<std::uint32_t>(static_cast<std::int64_t>(entries_[j].begin) + delta);
            if (entries_[j].key_in_text)
            {
                entries_[j].key_begin =
                    static_cast<std::uint32_t>(static_cast<std::int64_t>(entries_[j].key_begin) + delta);
            }
        }
    }

    /** @brief Every field, each starting with a comma, in insertion order. This is also the serialized form. */
    std::string text_;
    /** @brief Where each field lies in text_, in the same order. */
    std::vector<entry> entries_;
    /** @brief Raw keys that needed escaping, so lookups compare raw keys. Usually empty. */
    std::string keys_;
};

namespace detail
{

/** @brief Always false, but depends on its arguments, so a static_assert can use it in a discarded branch. */
template <typename... T>
inline constexpr bool dependent_false_v = false;

/** @brief Whether a type is a character type that jspdlog does not encode. */
template <typename T>
inline constexpr bool is_rejected_char_v =
    std::is_same_v<T, wchar_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>
#ifdef __cpp_char8_t
    || std::is_same_v<T, char8_t>
#endif
    ;

/** @brief Whether a type is an array of char, such as a string literal or a buffer. */
template <typename T>
inline constexpr bool is_char_array_v =
    std::is_array_v<T> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>;

/** @brief Whether a type is a std::optional. */
template <typename T>
inline constexpr bool is_optional_v = false;
template <typename T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

/** @brief Whether a type is a std::variant. */
template <typename T>
inline constexpr bool is_variant_v = false;
template <typename... Ts>
inline constexpr bool is_variant_v<std::variant<Ts...>> = true;

/** @brief Whether a type is a std::chrono::duration. */
template <typename T>
inline constexpr bool is_duration_v = false;
template <typename Rep, typename Period>
inline constexpr bool is_duration_v<std::chrono::duration<Rep, Period>> = true;

/** @brief Whether a type is a std::chrono::time_point of any clock. */
template <typename T>
inline constexpr bool is_time_point_v = false;
template <typename Clock, typename Duration>
inline constexpr bool is_time_point_v<std::chrono::time_point<Clock, Duration>> = true;

/** @brief Whether a type is a std::pair or a std::tuple. A std::array is a range, not a tuple, here. */
template <typename T>
inline constexpr bool is_tuple_like_v = false;
template <typename A, typename B>
inline constexpr bool is_tuple_like_v<std::pair<A, B>> = true;
template <typename... Ts>
inline constexpr bool is_tuple_like_v<std::tuple<Ts...>> = true;

/** @brief Detects begin and end, found through ADL like a range-based for does. */
namespace range_adl
{
using std::begin;
using std::end;

template <typename T, typename = void>
struct has_begin_end : std::false_type
{
};

template <typename T>
struct has_begin_end<
    T,
    std::void_t<decltype(begin(std::declval<const T &>())), decltype(end(std::declval<const T &>()))>> : std::true_type
{
};

template <typename T>
using element_t = std::remove_cv_t<std::remove_reference_t<decltype(*begin(std::declval<const T &>()))>>;
} // namespace range_adl

/** @brief Whether a type has begin and end, and is not a range of itself, like std::filesystem::path. */
template <typename T, typename = void>
inline constexpr bool is_range_v = false;
template <typename T>
inline constexpr bool is_range_v<T, std::enable_if_t<range_adl::has_begin_end<T>::value>> =
    !std::is_same_v<range_adl::element_t<T>, T>;

/** @brief Whether a type is a map whose keys convert to std::string_view. */
template <typename T, typename = void>
inline constexpr bool is_string_keyed_map_v = false;
template <typename T>
inline constexpr bool is_string_keyed_map_v<
    T,
    std::void_t<typename T::key_type, typename T::mapped_type, std::enable_if_t<is_range_v<T>>>> =
    std::is_convertible_v<const typename T::key_type &, std::string_view>;

/** @brief Whether serializer<T> has a write function for a json_writer. */
template <typename T, typename = void>
inline constexpr bool has_serializer_v = false;
template <typename T>
inline constexpr bool has_serializer_v<
    T,
    std::void_t<decltype(serializer<T>::write(std::declval<json_writer &>(), std::declval<const T &>()))>> = true;

/**
 * @brief Finds jspdlog_serialize by argument-dependent lookup, in the namespace of the type.
 *
 * jspdlog declares no jspdlog_serialize itself. A deleted declaration here would keep ordinary lookup from finding one
 * in the global namespace, but GCC 10 and 11 then skip argument-dependent lookup too, so there is none.
 */
namespace serialize_adl
{
/** @brief Chosen when jspdlog_serialize(json_writer &, const T &) is callable. */
template <typename T>
auto detect(int)
    -> decltype(jspdlog_serialize(std::declval<json_writer &>(), std::declval<const T &>()), std::true_type{});
/** @brief Chosen otherwise. */
template <typename T>
std::false_type detect(...);

/** @brief Calls the jspdlog_serialize that argument-dependent lookup finds. */
template <typename T>
void call(json_writer &w, const T &v)
{
    jspdlog_serialize(w, v);
}
} // namespace serialize_adl

/** @brief Whether jspdlog_serialize(json_writer &, const T &) is found by argument-dependent lookup. */
template <typename T>
inline constexpr bool has_adl_serialize_v = decltype(serialize_adl::detect<T>(0))::value;

/** @brief Gives the functions below the private members of json_writer. */
struct writer_access
{
    /** @brief Writes text that is already valid JSON. */
    static void scalar(json_writer &w, std::string_view text) { w.scalar_(text); }
    /** @brief Writes a string. */
    static void string(json_writer &w, std::string_view text) { w.string_(text); }
    /** @brief Writes an integer. */
    template <typename T>
    static void integer(json_writer &w, T number)
    {
        w.integer_(number);
    }
    /** @brief Writes a floating-point number. */
    template <typename T>
    static void floating(json_writer &w, T number)
    {
        w.floating_(number);
    }
    /** @brief Writes fields as an object. */
    static void object(json_writer &w, const fields &f) { w.scalar_(f.to_json()); }
    /** @brief Whether exactly one value was written and nothing is left open. */
    static bool complete(const json_writer &w) noexcept { return w.complete_(); }
    /** @brief Creates a writer that appends to spdlog's buffer. */
    static json_writer to_buffer(spdlog::memory_buf_t &out) noexcept { return {out, 0}; }
};

/** @brief Multiplies by a positive factor, stopping at the limits of the type instead of overflowing. */
template <typename T>
constexpr T saturating_mul(T a, T factor) noexcept
{
    if (a == 0)
    {
        return 0;
    }
    if constexpr (std::is_signed_v<T>)
    {
        if (a < 0)
        {
            return a < std::numeric_limits<T>::min() / factor ? std::numeric_limits<T>::min() : a * factor;
        }
    }
    return a > std::numeric_limits<T>::max() / factor ? std::numeric_limits<T>::max() : a * factor;
}

/** @brief Whether an enum has a formatter, as opposed to {fmt} 9 formatting every unscoped enum as an int. */
template <typename E>
inline constexpr bool enum_has_formatter_v = std::is_default_constructible_v<spdlog::fmt_lib::formatter<E, char>>;

/** @brief Detects format_as for an enum by overload resolution, like serialize_adl does for jspdlog_serialize. */
namespace format_as_adl
{
/** @brief Chosen when format_as(E) is found; its type is what format_as returns. */
template <typename E>
auto detect(int) -> decltype(format_as(std::declval<E>()));
/** @brief Chosen otherwise. */
template <typename E>
void detect(...);
} // namespace format_as_adl

#if FMT_VERSION >= 100000
/** @brief What format_as returns for an enum, or void when there is none. */
template <typename E>
using format_as_result_t = decltype(format_as_adl::detect<E>(0));
#else
/** @brief void: {fmt} 9 has no format_as. */
template <typename E>
using format_as_result_t = void;
#endif

/**
 * @brief Writes an enum as its name when {fmt} has a formatter or format_as for it, otherwise as its number. A
 * format_as that returns an integer gives a number.
 */
template <typename E>
void write_enum(json_writer &w, E e)
{
    using format_as_result = format_as_result_t<E>;
    if constexpr (std::is_integral_v<format_as_result>)
    {
        writer_access::integer(w, format_as(e));
    }
    else if constexpr (!std::is_void_v<format_as_result> || enum_has_formatter_v<E>)
    {
        writer_access::string(w, spdlog::fmt_lib::format("{}", e));
    }
    else
    {
        writer_access::integer(w, static_cast<std::underlying_type_t<E>>(e));
    }
}

/**
 * @brief Writes a duration in milliseconds: an integer when the representation is an integer and a tick is a whole
 * number of milliseconds, a floating-point number otherwise. The JSON type depends on the C++ type only. A huge integer
 * duration stops at the limits of long long instead of wrapping.
 */
template <typename Rep, typename Period>
void write_duration(json_writer &w, std::chrono::duration<Rep, Period> d)
{
    using ms_ratio = std::ratio_divide<Period, std::milli>; // how many milliseconds one tick is
    if constexpr (std::is_integral_v<Rep> && ms_ratio::den == 1)
    {
        using wide = std::conditional_t<std::is_signed_v<Rep>, long long, unsigned long long>;
        writer_access::integer(w, saturating_mul(static_cast<wide>(d.count()), static_cast<wide>(ms_ratio::num)));
    }
    else
    {
        writer_access::floating(w, std::chrono::duration<double, std::milli>(d).count());
    }
}

/**
 * @brief Year, month and day of a count of days since 1970-01-01, proleptic Gregorian.
 *
 * This is Howard Hinnant's civil_from_days. It needs no time zone database and no gmtime, so it is thread-safe and
 * works for any year. The constants are those of the published algorithm.
 */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
constexpr void civil_from_days(long long z, long long &y, unsigned &m, unsigned &d) noexcept
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - (era * 146097));
    const unsigned yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    const unsigned doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    const unsigned mp = ((5 * doy) + 2) / 153;
    d = doy - (((153 * mp) + 2) / 5) + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<long long>(yoe) + (era * 400) + (m <= 2 ? 1 : 0);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/** @brief The most milliseconds floor_milliseconds returns, far outside the years 0 to 9999. */
inline constexpr long long MAX_TIME_POINT_MS = std::numeric_limits<long long>::max() / 2;

/** @brief Whole milliseconds of a duration, rounded toward negative infinity, stopping at MAX_TIME_POINT_MS. */
template <typename Rep, typename Period>
long long floor_milliseconds(std::chrono::duration<Rep, Period> d) noexcept
{
    using ms_ratio = std::ratio_divide<Period, std::milli>;
    if constexpr (std::is_integral_v<Rep>)
    {
        if constexpr (ms_ratio::den == 1)
        {
            using wide = std::conditional_t<std::is_signed_v<Rep>, long long, unsigned long long>;
            const wide ms = saturating_mul(static_cast<wide>(d.count()), static_cast<wide>(ms_ratio::num));
            if constexpr (std::is_signed_v<Rep>)
            {
                return ms;
            }
            else
            {
                return ms > static_cast<unsigned long long>(MAX_TIME_POINT_MS) ? MAX_TIME_POINT_MS
                                                                               : static_cast<long long>(ms);
            }
        }
        else
        {
            return static_cast<long long>(std::chrono::floor<std::chrono::milliseconds>(d).count());
        }
    }
    else
    {
        const double ms = std::floor(std::chrono::duration<double, std::milli>(d).count());
        constexpr auto LIMIT = static_cast<double>(MAX_TIME_POINT_MS);
        if (ms <= -LIMIT)
        {
            return -MAX_TIME_POINT_MS;
        }
        if (ms < LIMIT)
        {
            return static_cast<long long>(ms);
        }
        return MAX_TIME_POINT_MS; // also NaN, which compares false with everything
    }
}

/** @brief Builds an RFC 3339 timestamp in a fixed buffer, one zero-padded number and separator at a time. */
class rfc3339_buffer
{
public:
    /** @brief Appends a number zero-padded to width digits, then a separator. */
    void put(long long number, int width, char separator) noexcept
    {
        static constexpr long long BASE = 10;
        for (int i = width - 1; i >= 0; --i)
        {
            text_[size_ + static_cast<std::size_t>(i)] = static_cast<char>('0' + (number % BASE));
            number /= BASE;
        }
        size_ += static_cast<std::size_t>(width);
        text_[size_++] = separator;
    }

    /** @brief The text written so far. */
    [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), size_}; }

private:
    /** @brief The length of YYYY-MM-DDTHH:MM:SS.mmmZ. */
    static constexpr std::size_t LENGTH = 24;
    /** @brief The characters written so far. */
    std::array<char, LENGTH> text_{};
    /** @brief The number of characters written. */
    std::size_t size_ = 0;
};

/**
 * @brief Writes a time point as an RFC 3339 UTC string with milliseconds, rounded toward negative infinity, so times
 * before 1970 get 0 to 999 milliseconds. Years outside 0000 to 9999 cannot be written in RFC 3339 and become null.
 */
template <typename Clock, typename Duration>
void write_time_point(json_writer &w, const std::chrono::time_point<Clock, Duration> &tp)
{
    static_assert(
        std::is_same_v<Clock, std::chrono::system_clock>,
        "jspdlog: only system_clock time points have a defined epoch; convert other clocks to system_clock first"
    );
    static constexpr long long MS_PER_SECOND = 1000;
    static constexpr long long SECONDS_PER_MINUTE = 60;
    static constexpr long long MINUTES_PER_HOUR = 60;
    static constexpr long long MS_PER_MINUTE = MS_PER_SECOND * SECONDS_PER_MINUTE;
    static constexpr long long MS_PER_HOUR = MS_PER_MINUTE * MINUTES_PER_HOUR;
    static constexpr long long HOURS_PER_DAY = 24;
    static constexpr long long MS_PER_DAY = MS_PER_HOUR * HOURS_PER_DAY;
    static constexpr long long LAST_YEAR = 9999;
    const long long ms = floor_milliseconds(tp.time_since_epoch());
    long long days = ms / MS_PER_DAY;
    long long in_day = ms % MS_PER_DAY;
    if (in_day < 0)
    {
        in_day += MS_PER_DAY;
        --days;
    }
    long long year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, year, month, day);
    if (year < 0 || year > LAST_YEAR)
    {
        writer_access::scalar(w, "null");
        return;
    }
    rfc3339_buffer text;
    text.put(year, 4, '-');
    text.put(month, 2, '-');
    text.put(day, 2, 'T');
    text.put(in_day / MS_PER_HOUR, 2, ':');
    text.put((in_day / MS_PER_MINUTE) % MINUTES_PER_HOUR, 2, ':');
    text.put((in_day / MS_PER_SECOND) % SECONDS_PER_MINUTE, 2, '.');
    text.put(in_day % MS_PER_SECOND, 3, 'Z');
    writer_access::string(w, text.view());
}

/** @brief Whether a range declares bool as its value type, as a std::vector of bool does. */
template <typename R, typename = void>
inline constexpr bool has_bool_value_type_v = false;
template <typename R>
inline constexpr bool has_bool_value_type_v<R, std::void_t<typename R::value_type>> =
    std::is_same_v<typename R::value_type, bool>;

/**
 * @brief Writes the elements of a range as an array. A range of bool may hand out proxy objects instead of bool, as a
 * std::vector of bool does in libc++, so its elements are converted to bool first.
 */
template <typename R>
void write_array(json_writer &w, const R &r)
{
    w.begin_array();
    for (const auto &e : r)
    {
        if constexpr (has_bool_value_type_v<R>)
        {
            const bool b = e; // a conversion, not a cast, so it is not useless when e is a bool already
            w.value(b);
        }
        else
        {
            w.value(e);
        }
    }
    w.end_array();
}

/** @brief Writes a map with string-like keys as an object. */
template <typename M>
void write_object(json_writer &w, const M &m)
{
    w.begin_object();
    for (const auto &entry : m)
    {
        w.key(std::string_view(entry.first));
        w.value(entry.second);
    }
    w.end_object();
}

/** @brief Writes a std::pair or std::tuple as an array. */
template <typename T>
void write_tuple(json_writer &w, const T &t)
{
    w.begin_array();
    std::apply([&w](const auto &...e) { (w.value(e), ...); }, t);
    w.end_array();
}

/** @brief Writes the active alternative of a variant, or null when it is valueless. */
template <typename... Ts>
void write_variant(json_writer &w, const std::variant<Ts...> &v)
{
    if (v.valueless_by_exception())
    {
        writer_access::scalar(w, "null");
        return;
    }
    std::visit([&w](const auto &alternative) { w.value(alternative); }, v);
}

/** @brief Views a char array up to its first null character, or to its end when it has none. */
template <typename A>
std::string_view char_array_view(const A &array) noexcept
{
    const char *const end = std::find(std::begin(array), std::end(array), char{0});
    return std::string_view{std::begin(array), static_cast<std::size_t>(end - std::begin(array))};
}

/** @brief The keys of an exception object. The value-type form uses the defaults; the call forms use json_options. */
struct exception_keys
{
    /** @brief Key of the type name. */
    std::string_view type = "type";
    /** @brief Key of the message. */
    std::string_view message = "message";
};

/** @brief How many nested causes are written at most. Deeper ones are left out. */
inline constexpr int MAX_EXCEPTION_DEPTH = 8;

/** @brief Removes a leading class or struct, which MSVC writes in front of a type name. */
inline void strip_class_keyword(std::string &name)
{
    for (const std::string_view keyword : {std::string_view{"class "}, std::string_view{"struct "}})
    {
        if (name.compare(0, keyword.size(), keyword) == 0)
        {
            name.erase(0, keyword.size());
            return;
        }
    }
}

/**
 * @brief Makes a type name the same on every compiler and standard library: no class or struct keyword, no libc++
 * inline namespace, and for the type that std::throw_with_nested throws, the user's type instead of the library's
 * wrapper template, such as std::_Nested_exception of T on libstdc++ and MSVC. A name that does not look like such a
 * wrapper stays as it is.
 */
inline std::string normalize_type_name(std::string name, bool nested)
{
    static constexpr std::string_view LIBCXX_NAMESPACE = "std::__1::";
    for (std::size_t at = name.find(LIBCXX_NAMESPACE); at != std::string::npos; at = name.find(LIBCXX_NAMESPACE, at))
    {
        name.replace(at, LIBCXX_NAMESPACE.size(), "std::");
    }
    strip_class_keyword(name);
    static constexpr std::string_view STD_NAMESPACE = "std::";
    if (nested && name.compare(0, STD_NAMESPACE.size(), STD_NAMESPACE) == 0)
    {
        const std::size_t open = name.find('<');
        const std::size_t close = name.rfind('>');
        if (open != std::string::npos && close != std::string::npos && close > open + 1)
        {
            name = name.substr(open + 1, close - open - 1);
            strip_class_keyword(name);
        }
    }
    return name;
}

/** @brief The name of the dynamic type of an exception, demangled where the compiler mangles it. */
// NOLINTNEXTLINE(modernize-use-string-view): only the build without RTTI returns a literal; the others build a name
inline std::string exception_type_name(const std::exception &ex)
{
#if JSPDLOG_HAS_RTTI
    const char *raw = typeid(ex).name();
#if JSPDLOG_DETAIL_HAS_CXXABI
    int status = 0;
    const std::unique_ptr<char, void (*)(void *)> demangled(
        abi::__cxa_demangle(raw, nullptr, nullptr, &status), &std::free
    );
    std::string name = status == 0 && demangled ? demangled.get() : raw;
#else
    std::string name = raw; // MSVC: "class std::runtime_error"
#endif
    return normalize_type_name(std::move(name), dynamic_cast<const std::nested_exception *>(&ex) != nullptr);
#else
    (void)ex;
    return "std::exception"; // no RTTI: the static type is all there is
#endif
}

/**
 * @brief Writes an exception as an object with its type and message, and its nested causes as a chain.
 *
 * A std::nested_exception that holds no cause, such as one thrown by std::throw_with_nested outside a handler, has no
 * cause member: std::rethrow_if_nested would call std::terminate for it.
 */
// NOLINTNEXTLINE(misc-no-recursion): the recursion stops at MAX_EXCEPTION_DEPTH
inline void write_exception(json_writer &w, const std::exception &ex, const exception_keys &keys, int depth = 0)
{
    w.begin_object();
    w.key(keys.type);
    writer_access::string(w, exception_type_name(ex));
    const char *what = ex.what();
    w.key(keys.message);
    writer_access::string(w, std::string_view{what != nullptr ? what : ""});
#if JSPDLOG_HAS_EXCEPTIONS && JSPDLOG_HAS_RTTI
    const auto *nested = dynamic_cast<const std::nested_exception *>(&ex);
    if (depth < MAX_EXCEPTION_DEPTH && nested != nullptr && nested->nested_ptr() != nullptr)
    {
        try
        {
            nested->rethrow_nested();
        }
        catch (const std::exception &cause)
        {
            w.key("cause");
            write_exception(w, cause, keys, depth + 1);
        }
        catch (...)
        {
            w.key("cause");
            w.begin_object();
            w.key(keys.type);
            writer_access::string(w, "unknown");
            w.end_object();
        }
    }
#else
    (void)depth;
#endif
    w.end_object();
}

/** @brief Writes the exception behind a pointer like write_exception, or null for a null pointer. */
inline void write_exception_ptr(json_writer &w, const std::exception_ptr &p, const exception_keys &keys)
{
    if (!p)
    {
        writer_access::scalar(w, "null");
        return;
    }
#if JSPDLOG_HAS_EXCEPTIONS
    try
    {
        std::rethrow_exception(p);
    }
    catch (const std::exception &ex)
    {
        write_exception(w, ex, keys);
        return;
    }
    catch (...) // NOLINT(bugprone-empty-catch): an exception of another type is written as unknown below
    {
    }
#endif
    w.begin_object();
    w.key(keys.type);
    writer_access::string(w, "unknown");
    w.end_object();
}

/** @brief The exception of an exception call form and the keys to write it with. Lives on the caller's stack. */
struct exception_arg
{
    /** @brief The exception. */
    const std::exception *ex = nullptr;
    /** @brief The keys of its object. */
    exception_keys keys;
};

/** @brief Writes the exception_arg behind a pointer. It has the type of a per-call reference's writer function. */
inline void write_exception_arg(json_writer &w, const void *p)
{
    const auto &arg = *static_cast<const exception_arg *>(p);
    write_exception(w, *arg.ex, arg.keys);
}

/**
 * @brief Whether write_value has a branch for the type, that is, whether it has a JSON form. Mirrors the branches of
 * write_value, so a type is lazy exactly when none of them takes it.
 */
template <typename T>
inline constexpr bool has_json_form_v =
    std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_pointer_v<T> || is_char_array_v<T> ||
    std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::monostate> || std::is_same_v<T, raw_json> ||
    std::is_same_v<T, fields> || has_serializer_v<T> || has_adl_serialize_v<T> ||
    std::is_base_of_v<std::exception, T> || std::is_same_v<T, std::exception_ptr> ||
    std::is_convertible_v<const T &, std::string_view> || is_optional_v<T> || is_variant_v<T> || is_duration_v<T> ||
    is_time_point_v<T> || is_tuple_like_v<T> || is_string_keyed_map_v<T> || is_range_v<T>;

/**
 * @brief Whether a value is lazy: a callable that takes no arguments and has no JSON form of its own. It is run when
 * the line is written, and what it returns is written as the value.
 */
template <typename T>
inline constexpr bool is_lazy_value_v = std::is_class_v<T> && std::is_invocable_v<const T &> && !has_json_form_v<T>;

/**
 * @brief Writes any value that has a JSON form. The order matters: built-in scalars first and not overridable, then the
 * customization points, then the categories of the standard library, so a user type that is also a range can choose its
 * own form.
 */
template <typename T>
void write_value(json_writer &w, const T &v)
{
    using value_type = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<value_type, std::nullptr_t> || std::is_same_v<value_type, std::monostate>)
    {
        writer_access::scalar(w, "null");
    }
    else if constexpr (std::is_same_v<value_type, bool>)
    {
        writer_access::scalar(w, v ? "true" : "false");
    }
    else if constexpr (is_rejected_char_v<value_type>)
    {
        static_assert(
            dependent_false_v<value_type>,
            "jspdlog: wchar_t, char8_t, char16_t and char32_t are rejected; convert to a UTF-8 std::string"
        );
    }
    else if constexpr (std::is_same_v<value_type, char>)
    {
        writer_access::string(w, std::string_view{&v, 1});
    }
    else if constexpr (std::is_integral_v<value_type>)
    {
        writer_access::integer(w, v);
    }
    else if constexpr (std::is_floating_point_v<value_type>)
    {
        writer_access::floating(w, v);
    }
    else if constexpr (std::is_same_v<value_type, const char *> || std::is_same_v<value_type, char *>)
    {
        if (v == nullptr)
        {
            writer_access::scalar(w, "null");
        }
        else
        {
            writer_access::string(w, std::string_view{v});
        }
    }
    else if constexpr (std::is_pointer_v<value_type>)
    {
        static_assert(
            dependent_false_v<value_type>, "jspdlog: pointers are rejected; they would otherwise convert to bool"
        );
    }
    else if constexpr (is_char_array_v<value_type>)
    {
        writer_access::string(w, char_array_view(v)); // a literal or a buffer, ends at the first null character
    }
    else if constexpr (std::is_same_v<value_type, raw_json>)
    {
        w.raw(v.value);
    }
    else if constexpr (std::is_same_v<value_type, fields>)
    {
        writer_access::object(w, v);
    }
    else if constexpr (has_serializer_v<value_type>)
    {
        serializer<value_type>::write(w, v);
    }
    else if constexpr (has_adl_serialize_v<value_type>)
    {
        serialize_adl::call(w, v);
    }
    else if constexpr (std::is_base_of_v<std::exception, value_type>)
    {
        write_exception(w, v, exception_keys{});
    }
    else if constexpr (std::is_same_v<value_type, std::exception_ptr>)
    {
        write_exception_ptr(w, v, exception_keys{});
    }
    else if constexpr (std::is_convertible_v<const value_type &, std::string_view>)
    {
        writer_access::string(w, std::string_view(v));
    }
    else if constexpr (is_optional_v<value_type>)
    {
        if (v)
        {
            w.value(*v);
        }
        else
        {
            writer_access::scalar(w, "null");
        }
    }
    else if constexpr (is_variant_v<value_type>)
    {
        write_variant(w, v);
    }
    else if constexpr (std::is_enum_v<value_type>)
    {
        write_enum(w, v);
    }
    else if constexpr (is_duration_v<value_type>)
    {
        write_duration(w, v);
    }
    else if constexpr (is_time_point_v<value_type>)
    {
        write_time_point(w, v);
    }
    else if constexpr (is_tuple_like_v<value_type>)
    {
        write_tuple(w, v);
    }
    else if constexpr (is_string_keyed_map_v<value_type>)
    {
        write_object(w, v);
    }
    else if constexpr (is_range_v<value_type>) // a map with other keys is a range of pairs, so it becomes [[key,
                                               // value], ...]
    {
        write_array(w, v);
    }
    else if constexpr (is_lazy_value_v<value_type>)
    {
        static_assert(
            dependent_false_v<value_type>,
            "jspdlog: a callable value is lazy and allowed only in the braced list of a logging call, not in a stored "
            "fields object or with()"
        );
    }
    else if constexpr (std::is_class_v<value_type> && std::is_invocable_v<value_type &>)
    {
        static_assert(
            dependent_false_v<value_type>, "jspdlog: a lazy value is called as const; remove mutable from the lambda"
        );
    }
    else
    {
        static_assert(
            dependent_false_v<value_type>,
            "jspdlog: no JSON form for this type. Define jspdlog_serialize(json_writer &, const T &) next to the type, "
            "or specialize jspdlog::serializer<T>. A std::filesystem::path is rejected: convert it with string() or "
            "u8string()"
        );
    }
}

} // namespace detail

template <typename T>
void json_writer::value(const T &v)
{
    detail::write_value(*this, v);
}

namespace detail
{

/**
 * @brief The keys a logger writes itself, to find field keys that would duplicate them.
 *
 * It is stored in every build, so the layout of json_logger does not depend on NDEBUG.
 */
class reserved_keys
{
public:
    /** @brief Creates an empty list. */
    reserved_keys() = default;

    /** @brief Collects the enabled header keys, the call site keys and the message key. */
    explicit reserved_keys(const json_options &options)
    {
        const auto add = [this](const std::optional<std::string> &key) {
            if (key)
            {
                keys_.push_back(*key);
            }
        };
        add(options.timestamp_key);
        add(options.logger_key);
        add(options.level_key);
        add(options.severity_key);
        add(options.process_key);
        add(options.thread_key);
        if (options.source_key && options.source_flat)
        {
            keys_.push_back(options.source_file_key);
            keys_.push_back(options.source_line_key);
            keys_.push_back(options.source_function_key);
        }
        else
        {
            add(options.source_key);
        }
        keys_.push_back(options.message_key);
    }

    /** @brief Whether any key of the fields is one of the reserved keys. Only top-level keys are compared. */
    [[nodiscard]] bool clashes_with(const fields &f) const noexcept
    {
        return std::any_of(keys_.begin(), keys_.end(), [&f](const std::string &key) { return f.contains(key); });
    }

    /** @brief Whether the key is one of the reserved keys. */
    [[nodiscard]] bool is_reserved(std::string_view key) const noexcept
    {
        return std::any_of(keys_.begin(), keys_.end(), [key](const std::string &reserved) { return reserved == key; });
    }

private:
    /** @brief The reserved keys. */
    std::vector<std::string> keys_;
};

/**
 * @brief A call site, filled in by default arguments at the caller.
 *
 * The builtins are default arguments of a constructor that is not a template. GCC evaluates a builtin in the default
 * argument of a function template where the template is written instead of where it is called, which a constructor
 * default argument avoids.
 */
struct site
{
    /** @brief Captures the place where the site is default-constructed, which is the call of a logging function. */
    explicit constexpr site(
        const char *f = __builtin_FILE(), int l = __builtin_LINE(), const char *fn = __builtin_FUNCTION()
    ) noexcept
        : file_(f), line_(l), function_(fn)
    {
    }

    /** @brief The source file, as the compiler spells it. */
    [[nodiscard]] constexpr const char *file() const noexcept { return file_; }
    /** @brief The line of the call. */
    [[nodiscard]] constexpr int line() const noexcept { return line_; }
    /** @brief The short name of the calling function, which depends on the compiler. */
    [[nodiscard]] constexpr const char *function() const noexcept { return function_; }

private:
    /** @brief The source file. */
    const char *file_;
    /** @brief The line of the call. */
    int line_;
    /** @brief The name of the calling function. */
    const char *function_;
};

/** @brief std::type_identity for C++17, used to keep a parameter out of template argument deduction. */
template <typename T>
struct type_identity
{
    /** @brief The type itself. */
    using type = T;
};

#if JSPDLOG_STD_FORMAT
#if defined(__cpp_lib_format) && __cpp_lib_format >= 202311L
#define JSPDLOG_HAS_RUNTIME_FORMAT 1
/** @brief The type of std::runtime_format(...). */
using runtime_format_string = decltype(std::runtime_format(std::string_view{}));
#else
#define JSPDLOG_HAS_RUNTIME_FORMAT 0
/** @brief std::format has no runtime format strings before C++26, so no argument has this type. */
struct runtime_format_string
{
    runtime_format_string() = delete;
};
#endif
#else
#define JSPDLOG_HAS_RUNTIME_FORMAT 1
/** @brief The type of fmt::runtime(...) in the {fmt} version in use. */
using runtime_format_string = decltype(spdlog::fmt_lib::runtime(""));
#endif

#if JSPDLOG_STD_FORMAT
/** @brief The text of a format string: std::format_string, or a plain std::string_view in older standard libraries. */
template <typename F>
std::string_view format_text(const F &f) noexcept
{
    if constexpr (std::is_convertible_v<const F &, std::string_view>)
    {
        return f;
    }
    else
    {
        return f.get();
    }
}
#endif

/**
 * @brief Whether a type is a writable char array, such as a buffer filled at run time. String literals are const
 * arrays, so they stay format strings.
 */
template <typename S>
inline constexpr bool is_char_buffer_v = std::is_array_v<S> && std::is_same_v<std::remove_extent_t<S>, char>;

/**
 * @brief Whether a type is a string a call may pass as a message: a std::string, std::string_view, C string or
 * writable char array.
 */
template <typename S>
inline constexpr bool is_runtime_string_v =
    std::is_same_v<S, std::string> || std::is_same_v<S, std::string_view> || std::is_same_v<S, const char *> ||
    std::is_same_v<S, char *> || is_char_buffer_v<S>;

/** @brief Enables an overload for runtime strings only. String literals go to the format-string overloads. */
template <typename S>
using if_runtime_string = std::enable_if_t<is_runtime_string_v<S>, int>;

/**
 * @brief Enables an overload for exceptions only. The exception parameter is a template, not a const std::exception
 * reference, because the standard library of MSVC gives std::exception a constructor that takes a const char pointer
 * and an int, so a braced list such as {"k", 1} would convert to one.
 */
template <typename E>
using if_exception = std::enable_if_t<std::is_base_of_v<std::exception, E>, int>;

/**
 * @brief Whether a single argument is neither a string, a format string nor a fields object, so it cannot be a message.
 *
 * A fields object is excluded because a call form with fields converts it to the per-call parameter, and a template
 * that matches the type exactly would win over that conversion. An exception is excluded for the same reason against
 * the exception call forms, which take a const std::exception reference.
 */
template <typename T>
inline constexpr bool is_rejected_message_v =
    !is_runtime_string_v<T> && !std::is_array_v<T> && !std::is_same_v<T, runtime_format_string> &&
    !std::is_same_v<T, fields> && !std::is_base_of_v<std::exception, T>;

/**
 * @brief Views a runtime string as a std::string_view. A null C string is an empty message, and a char array ends at
 * its first null character or at its end, whichever comes first.
 */
template <typename S>
std::string_view message_view(const S &s) noexcept
{
    if constexpr (std::is_pointer_v<S>)
    {
        return s != nullptr ? std::string_view{s} : std::string_view{};
    }
    else if constexpr (std::is_array_v<S>)
    {
        const char *const end = std::find(std::begin(s), std::end(s), '\0');
        return std::string_view{std::begin(s), static_cast<std::size_t>(end - std::begin(s))};
    }
    else
    {
        return std::string_view{s};
    }
}

/**
 * @brief The fields of one logging call.
 *
 * A braced list is kept as references to its keys and values, which live until the end of the full expression that
 * contains the call, and is serialized only when the line is written. So a call at a disabled level serializes nothing.
 * A fields object is referenced whole; it is already serialized. Not copyable, and never named in user code.
 */
class call_fields
{
public:
    /** @brief The most pairs a braced list may hold. A larger set is built as a fields object. */
    static constexpr std::size_t CAPACITY = 16;

    /** @brief Creates an empty set, for a call with an empty braced list. */
    call_fields() noexcept = default;

    /** @brief Refers to a fields object, which must outlive the call. */
    // Implicit on purpose: a fields object is passed to a logging call as it is.
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor,misc-explicit-constructor)
    call_fields(const fields &f) noexcept
        : named_(&f)
    {
    }

    /**
     * @brief Refers to the keys and values of a braced list. Keys may be anything convertible to std::string_view.
     *
     * Nothing is copied or moved, so the arguments must outlive the call, which temporaries in a braced list do.
     */
    template <typename K, typename V, typename... Rest>
    // Implicit on purpose: a braced list converts at the call.
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor,misc-explicit-constructor)
    call_fields(const K &key, const V &value, const Rest &...rest)
    {
        static_assert(sizeof...(Rest) % 2 == 0, "fields: expected key/value pairs");
        static_assert(
            (sizeof...(Rest) / 2) + 1 <= CAPACITY,
            "jspdlog: a braced list holds at most 16 fields; build a jspdlog::fields object for more"
        );
        add_(key, value, rest...);
    }

    call_fields(const call_fields &) = delete;
    call_fields &operator=(const call_fields &) = delete;
    ~call_fields() = default;

    /** @brief The fields object this refers to, or null for a braced list. */
    [[nodiscard]] const fields *named() const noexcept { return named_; }

    /** @brief The number of key and value references of a braced list. */
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /** @brief The key of a reference. */
    [[nodiscard]] std::string_view key(std::size_t i) const noexcept { return refs_[i].key; }

    /**
     * @brief One key and value of a braced list, or the exception of an exception call.
     *
     * The members have no initializers, so refs_ stays uninitialized and a call writes only the entries it uses.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
    struct ref
    {
        /** @brief The key, viewed in place. */
        std::string_view key;
        /** @brief The value, which the braced list owns. */
        const void *value;
        /** @brief Writes the whole field, with the type of the value restored: see write_item. */
        void (*write)(spdlog::memory_buf_t &, const ref &);
    };

    /** @brief The reference at index i. */
    [[nodiscard]] const ref &at(std::size_t i) const noexcept { return refs_[i]; }

    /**
     * @brief Appends a reference as a field: a comma, the quoted key, a colon and the serialized value. This runs the
     * serializer of the value, so it throws what the serializer throws.
     */
    static void append_ref(spdlog::memory_buf_t &out, const ref &r) { r.write(out, r); }

    /**
     * @brief The function a reference stores to write its field. Value writes the value from its pointer; Plain says
     * that the key needs no escaping, so it is copied between quotes without a scan. Whether the key is plain is part
     * of the function, so a reference needs no flag, which keeps a braced list small and cheap to build.
     */
    template <void (*Value)(json_writer &, const void *), bool Plain>
    static void write_item(spdlog::memory_buf_t &out, const ref &r)
    {
        out.push_back(',');
        if constexpr (Plain)
        {
            out.push_back('"');
            out.append(r.key.data(), r.key.data() + r.key.size());
            out.push_back('"');
        }
        else
        {
            append_json_quoted(out, r.key);
        }
        out.push_back(':');
        json_writer w = writer_access::to_buffer(out);
        Value(w, r.value);
        JSPDLOG_ASSERT(
            writer_access::complete(w), "jspdlog: a serializer left an object or array open, or wrote no value"
        );
    }

private:
#ifdef _MSC_VER
// A lazy value that always throws makes the code after the call unreachable in this function, which C4702 reports.
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
    /**
     * @brief Serializes the value behind a pointer of type V, or runs it first when it is a lazy value and writes what
     * it returns.
     */
    template <typename V>
    static void write_ref_(json_writer &w, const void *p)
    {
        const V &v = *static_cast<const V *>(p);
        if constexpr (is_lazy_value_v<V>)
        {
            using result = std::decay_t<std::invoke_result_t<const V &>>;
            constexpr bool WRITABLE = !std::is_void_v<result> && !is_lazy_value_v<result>;
            static_assert(WRITABLE, "jspdlog: a lazy value must return a value");
            if constexpr (WRITABLE)
            {
                w.value(v());
            }
        }
        else
        {
            w.value(v);
        }
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    /** @brief Records one pair and then the rest. */
    template <typename K, typename V, typename... Rest>
    void add_(const K &key, const V &value, const Rest &...rest)
    {
        static_assert(
            std::is_convertible_v<const K &, std::string_view>, "fields: key must be convertible to std::string_view"
        );
        refs_[size_++] = ref{
            std::string_view(key),
            static_cast<const void *>(std::addressof(value)),
            &write_item<&write_ref_<V>, std::is_same_v<K, ::jspdlog::key>>,
        };
        if constexpr (sizeof...(Rest) > 0)
        {
            add_(rest...);
        }
    }

    /** @brief The fields object, or null for a braced list. */
    const fields *named_ = nullptr;
    /** @brief The references of a braced list. Only the first size_ entries are written. */
    std::array<ref, CAPACITY> refs_; // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
    /** @brief The number of entries of refs_ in use. */
    std::size_t size_ = 0;
};

} // namespace detail

/**
 * @brief A format string together with the place where it was written.
 *
 * Logging functions take it instead of a plain format string, so the call site is captured by default arguments and
 * no macro is needed. The format string is checked at compile time where {fmt} can do so. Wrapping it in fmt::runtime
 * is accepted and checked at run time.
 */
template <typename... Args>
class basic_format_here
{
public:
    /** @brief Converts a string literal, capturing the call site of the logging function. */
    template <typename S, std::enable_if_t<std::is_convertible_v<const S &, spdlog::fmt_lib::string_view>, int> = 0>
    // Implicit on purpose: a string literal or fmt::runtime must convert at the call.
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor,misc-explicit-constructor)
    JSPDLOG_FORMAT_CONSTEVAL basic_format_here(const S &s, detail::site w = detail::site())
        : fmt_(s), where_(w)
    {
    }

#if JSPDLOG_HAS_RUNTIME_FORMAT
    /** @brief Converts fmt::runtime(...), capturing the call site of the logging function. */
    // Implicit on purpose: a string literal or fmt::runtime must convert at the call.
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor,misc-explicit-constructor)
    basic_format_here(detail::runtime_format_string r, detail::site w = detail::site())
        : fmt_(r), where_(w)
    {
    }
#endif

    /** @brief The format string. */
    [[nodiscard]] spdlog::format_string_t<Args...> format() const noexcept { return fmt_; }
    /** @brief The call site. */
    [[nodiscard]] detail::site where() const noexcept { return where_; }

private:
    /** @brief The format string. */
    spdlog::format_string_t<Args...> fmt_;
    /** @brief The call site. */
    detail::site where_;
};

/** @brief The format-string parameter of a logging function, whose arguments are deduced from the other parameters. */
template <typename... Args>
using format_here_t = basic_format_here<typename detail::type_identity<Args>::type...>;

/**
 * @brief A logger that writes one JSON object per line.
 *
 * Copies share the underlying spdlog::logger, so its level, sinks and error handler apply to every copy. They also
 * share the bound fields, which are immutable: with returns a child that adds fields to every line it writes, which is
 * how inner code adds context without affecting its caller. Copying a json_logger copies two pointers.
 *
 * The logging calls are const and only read the json_logger, so inner code can take a const reference, and several
 * threads can log through one json_logger, its copies and its children at once. Whether their lines are written safely
 * is up to the sinks, as in spdlog: use the _mt sinks when logging from several threads.
 *
 * Each level has five call forms, also available through log with the level as the first argument:
 *
 * @code
 * logger.info("loaded {} items", n);    // formatted message
 * logger.info(text);                    // a runtime string as the message
 * logger.info({"items", n});            // fields only, no message
 * logger.info({"items", n}, "loaded");  // fields and a formatted message
 * logger.info({"items", n}, text);      // fields and a runtime string as the message
 * logger.info({"plan", [&] { return explain(q); }}, "planned");  // a lazy value
 * @endcode
 *
 * Each level also has six call forms that take a std::exception first, with the same endings: no message, a message
 * or a format string, and each with fields too. They write the exception as the first per-call field, under
 * json_options::error_key, as an object with its type name and message, and its nested causes under cause. The
 * exception is written only when the line is, so a call at a disabled level computes nothing. A per-call field with
 * the error key is a mistake that debug builds report through JSPDLOG_ASSERT; a bound field with that key is replaced
 * by the exception.
 *
 * @code
 * try { connect(); }
 * catch (const std::exception &ex) { logger.error(ex, {"host", host}, "connect failed"); }
 * @endcode
 *
 * A braced list is kept as references and serialized only when the line is written, so a call at a disabled level
 * serializes nothing and a braced list holds at most 16 pairs. A value in it may be a callable that takes no
 * arguments, which runs only when the line is written, in the calling thread, and whose result is the value. That
 * means whenever the logger's level lets the call through, even if each sink then drops the line because of a higher
 * level of its own. Lazy values are allowed only here, not in a fields object or with. The format arguments of a call
 * are evaluated before the call; the JSPDLOG_* macros at the end of this header skip them too when the level is off.
 *
 * The message is always a JSON string, and a single argument that is not a string is rejected at compile time: put
 * values that should keep their type in fields. A string literal is a format string checked at compile time; a
 * std::string, std::string_view, C string or writable char array is taken as it is. Convert other string types first.
 *
 * Every call records its file, line and function, and the formatter writes them when json_options::source_key is set.
 * The function is the short name the compiler gives, which differs between compilers.
 *
 * Lines list the header fields, the bound fields of outer and inner loggers, the fields of the call and then the
 * message. A key that appears again, in an inner logger or in a call, replaces the value and stays where it first
 * appeared.
 *
 * The constructors and adopt install a json_formatter on every sink of the spdlog::logger. A sink shared with another
 * json_logger works; the options of the logger created last apply to both. A sink added to the spdlog::logger later, or
 * shared with a plain spdlog logger, needs install_json_formatter to get JSON lines. Like spdlog, the logging calls do
 * not throw: an exception from a user formatter goes to the spdlog::logger's error handler and the line is dropped.
 *
 * A serializer that throws, or a lazy value that does, fails while the line is written: the exception goes to the
 * error handler and the line is dropped.
 *
 * A json_logger and the formatter it installs agree on the payload token, so they work together even when the logger
 * is used from another module than the one that installed the formatter. Mixing a formatter installed by one module
 * with a json_logger of another module on one sink is not supported on platforms where each module has its own token,
 * such as Windows DLLs: the line stays valid JSON, but the fields appear inside the message.
 *
 * Debug builds check that no field key equals a header key, the source key or the message key of this logger, through
 * JSPDLOG_ASSERT. The keys of the options this logger was created with are used, even when a shared sink writes with
 * the options of another logger.
 */
class json_logger
{
public:
    /** @brief Creates a logger writing to one sink. Throws std::invalid_argument if the sink is null. */
    json_logger(std::string name, spdlog::sink_ptr sink, const json_options &options = {})
        : json_logger(make_logger_(std::move(name), std::move(sink)), options)
    {
    }

    /** @brief Creates a logger writing to several sinks. Throws std::invalid_argument if any sink is null. */
    json_logger(std::string name, spdlog::sinks_init_list sinks, const json_options &options = {})
        : json_logger(std::move(name), sinks.begin(), sinks.end(), options)
    {
    }

    /** @brief Creates a logger writing to a range of sinks. Throws std::invalid_argument if any sink is null. */
    template <
        typename It,
        std::enable_if_t<std::is_constructible_v<spdlog::sink_ptr, typename std::iterator_traits<It>::reference>, int> =
            0>
    json_logger(std::string name, It first, It last, const json_options &options = {})
        : json_logger(make_logger_(std::move(name), first, last), options)
    {
    }

    /**
     * @brief Wraps an existing spdlog::logger, for example an async logger.
     *
     * The formatter of the logger's sinks is replaced by a json_formatter, which affects everyone using that
     * spdlog::logger or those sinks. Its level and error handler stay as they are. Throws std::invalid_argument if the
     * logger is null.
     */
    [[nodiscard]] static json_logger adopt(std::shared_ptr<spdlog::logger> logger, const json_options &options = {})
    {
        if (!logger)
        {
            throw std::invalid_argument("jspdlog: adopt() requires a non-null logger");
        }
        return {std::move(logger), options};
    }

    /**
     * @brief Returns a child logger that adds f to every line it writes.
     *
     * The child shares this logger's spdlog::logger and keeps this logger's bound fields. Where both have a key, the
     * value from f wins and the key keeps its position. This logger is not changed, and the child's fields are
     * immutable, so any number of threads can log through the child, this logger and their copies at once.
     */
    [[nodiscard]] json_logger with(const fields &f) const
    {
        JSPDLOG_ASSERT(
            !context_->reserved.clashes_with(f), "jspdlog: a field key equals a header, source or message key"
        );
        auto ctx = std::make_shared<context_data>(*context_);
        ctx->values.merge(f);
        json_logger child = *this;
        child.context_ = std::move(ctx);
        return child;
    }

    /** @brief The fields bound to this logger by with. */
    [[nodiscard]] const fields &context() const noexcept { return context_->values; }

    /** @brief Writes a line with fields only, without a message. */
    void log(level lvl, const detail::call_fields &f, detail::site where = detail::site()) const
    {
        log_fields_(lvl, nullptr, f, where);
    }

    /** @brief Writes a formatted message. The format string is checked at compile time. */
    template <typename... Args>
    void log(level lvl, format_here_t<Args...> fmt, Args &&...args) const
    {
        log_fmt_(lvl, nullptr, empty_call_fields_(), fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes a runtime string as the message. */
    template <typename S, detail::if_runtime_string<S> = 0>
    void log(level lvl, const S &msg, detail::site where = detail::site()) const
    {
        log_str_(lvl, nullptr, empty_call_fields_(), detail::message_view(msg), where);
    }

    /** @brief Writes fields and a formatted message. */
    template <typename... Args>
    void log(level lvl, const detail::call_fields &f, format_here_t<Args...> fmt, Args &&...args) const
    {
        log_fmt_(lvl, nullptr, f, fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes fields and a runtime string as the message. */
    template <typename S, detail::if_runtime_string<S> = 0>
    void log(level lvl, const detail::call_fields &f, const S &msg, detail::site where = detail::site()) const
    {
        log_str_(lvl, nullptr, f, detail::message_view(msg), where);
    }

    /** @brief Writes an exception and a formatted message. */
    template <typename E, detail::if_exception<E> = 0, typename... Args>
    void log(level lvl, const E &ex, format_here_t<Args...> fmt, Args &&...args) const
    {
        log_fmt_(lvl, &ex, empty_call_fields_(), fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes an exception and a runtime string as the message. */
    template <typename E, typename S, detail::if_exception<E> = 0, detail::if_runtime_string<S> = 0>
    void log(level lvl, const E &ex, const S &msg, detail::site where = detail::site()) const
    {
        log_str_(lvl, &ex, empty_call_fields_(), detail::message_view(msg), where);
    }

    /**
     * @brief Writes an exception under json_options::error_key, with fields if given and without a message. The fields
     * have a default and are not a separate overload, so a braced list cannot be taken for the call site.
     */
    template <typename E, detail::if_exception<E> = 0>
    void log(
        level lvl, const E &ex, const detail::call_fields &f = empty_call_fields_(), detail::site where = detail::site()
    ) const
    {
        log_fields_(lvl, &ex, f, where);
    }

    /** @brief Writes an exception, fields and a formatted message. */
    template <typename E, detail::if_exception<E> = 0, typename... Args>
    void log(level lvl, const E &ex, const detail::call_fields &f, format_here_t<Args...> fmt, Args &&...args) const
    {
        log_fmt_(lvl, &ex, f, fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes an exception, fields and a runtime string as the message. */
    template <typename E, typename S, detail::if_exception<E> = 0, detail::if_runtime_string<S> = 0>
    void
    log(level lvl, const E &ex, const detail::call_fields &f, const S &msg, detail::site where = detail::site()) const
    {
        log_str_(lvl, &ex, f, detail::message_view(msg), where);
    }

    /** @brief Rejects a single argument that is not a string. Put typed values in fields. */
    template <typename T, std::enable_if_t<detail::is_rejected_message_v<T>, int> = 0>
    void log(level lvl, const T &) const = delete;

// Defines the call forms for one level. Undefined right after use.
#define JSPDLOG_DETAIL_LEVEL_FUNCTIONS(name, lvl)                                                                      \
    template <typename... Args>                                                                                        \
    void name(format_here_t<Args...> fmt, Args &&...args) const                                                        \
    {                                                                                                                  \
        log_fmt_(lvl, nullptr, empty_call_fields_(), fmt, std::forward<Args>(args)...);                                \
    }                                                                                                                  \
    template <typename S, detail::if_runtime_string<S> = 0>                                                            \
    void name(const S &msg, detail::site where = detail::site()) const                                                 \
    {                                                                                                                  \
        log_str_(lvl, nullptr, empty_call_fields_(), detail::message_view(msg), where);                                \
    }                                                                                                                  \
    void name(const detail::call_fields &f, detail::site where = detail::site()) const                                 \
    {                                                                                                                  \
        log_fields_(lvl, nullptr, f, where);                                                                           \
    }                                                                                                                  \
    template <typename... Args>                                                                                        \
    void name(const detail::call_fields &f, format_here_t<Args...> fmt, Args &&...args) const                          \
    {                                                                                                                  \
        log_fmt_(lvl, nullptr, f, fmt, std::forward<Args>(args)...);                                                   \
    }                                                                                                                  \
    template <typename S, detail::if_runtime_string<S> = 0>                                                            \
    void name(const detail::call_fields &f, const S &msg, detail::site where = detail::site()) const                   \
    {                                                                                                                  \
        log_str_(lvl, nullptr, f, detail::message_view(msg), where);                                                   \
    }                                                                                                                  \
    template <typename E, detail::if_exception<E> = 0, typename... Args>                                               \
    void name(const E &ex, format_here_t<Args...> fmt, Args &&...args) const                                           \
    {                                                                                                                  \
        log_fmt_(lvl, &ex, empty_call_fields_(), fmt, std::forward<Args>(args)...);                                    \
    }                                                                                                                  \
    template <typename E, typename S, detail::if_exception<E> = 0, detail::if_runtime_string<S> = 0>                   \
    void name(const E &ex, const S &msg, detail::site where = detail::site()) const                                    \
    {                                                                                                                  \
        log_str_(lvl, &ex, empty_call_fields_(), detail::message_view(msg), where);                                    \
    }                                                                                                                  \
    template <typename E, detail::if_exception<E> = 0>                                                                 \
    void name(const E &ex, const detail::call_fields &f = empty_call_fields_(), detail::site where = detail::site())   \
        const                                                                                                          \
    {                                                                                                                  \
        log_fields_(lvl, &ex, f, where);                                                                               \
    }                                                                                                                  \
    template <typename E, detail::if_exception<E> = 0, typename... Args>                                               \
    void name(const E &ex, const detail::call_fields &f, format_here_t<Args...> fmt, Args &&...args) const             \
    {                                                                                                                  \
        log_fmt_(lvl, &ex, f, fmt, std::forward<Args>(args)...);                                                       \
    }                                                                                                                  \
    template <typename E, typename S, detail::if_exception<E> = 0, detail::if_runtime_string<S> = 0>                   \
    void name(const E &ex, const detail::call_fields &f, const S &msg, detail::site where = detail::site()) const      \
    {                                                                                                                  \
        log_str_(lvl, &ex, f, detail::message_view(msg), where);                                                       \
    }                                                                                                                  \
    template <typename T, std::enable_if_t<detail::is_rejected_message_v<T>, int> = 0>                                 \
    void name(const T &) const = delete;

    JSPDLOG_DETAIL_LEVEL_FUNCTIONS(trace, level::trace)
    JSPDLOG_DETAIL_LEVEL_FUNCTIONS(debug, level::debug)
    JSPDLOG_DETAIL_LEVEL_FUNCTIONS(info, level::info)
    JSPDLOG_DETAIL_LEVEL_FUNCTIONS(warn, level::warn)
    JSPDLOG_DETAIL_LEVEL_FUNCTIONS(error, level::err)
    JSPDLOG_DETAIL_LEVEL_FUNCTIONS(critical, level::critical)
#undef JSPDLOG_DETAIL_LEVEL_FUNCTIONS

    /** @brief The logger name, as given at construction or taken from the adopted spdlog::logger. */
    [[nodiscard]] const std::string &name() const noexcept { return logger_->name(); }

    /** @brief The level below which calls are dropped. Shared by every copy. */
    [[nodiscard]] level log_level() const noexcept
    {
#if JSPDLOG_SPDLOG_V2
        return logger_->log_level();
#else
        return logger_->level();
#endif
    }

    /** @brief Sets the level below which calls are dropped, for every copy. */
    void set_level(level lvl) { logger_->set_level(lvl); }
    /**
     * @brief Whether a call at this level would be written. Use it to skip building expensive fields or arguments.
     */
    [[nodiscard]] bool should_log(level lvl) const noexcept { return logger_->should_log(lvl); }
    /** @brief Flushes every sink. */
    void flush() { logger_->flush(); }
    /** @brief Flushes automatically after every line at this level or above. */
    void flush_on(level lvl) { logger_->flush_on(lvl); }

    /**
     * @brief The underlying spdlog::logger, for settings jspdlog does not wrap, such as the error handler.
     *
     * Logging through it directly is safe: the message is escaped into the message field. Setting a pattern or
     * formatter on it or on its sinks replaces the json_formatter, and the lines then show the payload protocol. Call
     * install_json_formatter on a sink added to it later.
     */
    [[nodiscard]] const std::shared_ptr<spdlog::logger> &spdlog_logger() const noexcept { return logger_; }

private:
    /** @brief What a logger and its copies share: the bound fields and what is derived from them. Immutable. */
    struct context_data
    {
        /** @brief The bound fields. */
        fields values;
        /** @brief The token that starts every payload, the same as in the formatter this logger installed. */
        detail::payload_token token{};
        /** @brief The keys the logger writes itself, for the debug check. */
        detail::reserved_keys reserved;
        /** @brief Where an exception of an exception call form goes: its key and the keys inside its object. */
        struct error_keys_data
        {
            /** @brief Key of the exception object. */
            std::string key;
            /** @brief Key of the type name inside it. */
            std::string type;
            /** @brief Key of the message inside it. */
            std::string message;
        } errors;
    };

    /** @brief Installs a json_formatter on a logger that is known not to be null. */
    json_logger(std::shared_ptr<spdlog::logger> logger, const json_options &options)
        : logger_(std::move(logger)), context_(make_context_(options))
    {
        logger_->set_formatter(std::unique_ptr<spdlog::formatter>(new json_formatter(options, context_->token)));
    }

    /** @brief Creates the empty context of a root logger. */
    static std::shared_ptr<const context_data> make_context_(const json_options &options)
    {
        auto ctx = std::make_shared<context_data>();
        ctx->token = detail::process_token();
        ctx->reserved = detail::reserved_keys(options);
        ctx->errors = {options.error_key, options.error_type_key, options.error_message_key};
        return ctx;
    }

    /** @brief Creates the spdlog::logger for one sink, rejecting a null sink. */
    static std::shared_ptr<spdlog::logger> make_logger_(std::string name, spdlog::sink_ptr sink)
    {
        if (!sink)
        {
            throw std::invalid_argument("jspdlog: sink must not be null");
        }
        return std::make_shared<spdlog::logger>(std::move(name), std::move(sink));
    }

    /**
     * @brief Creates the spdlog::logger for a range of sinks, rejecting null sinks. Single-pass ranges are not checked,
     * because checking would consume them.
     */
    template <typename It>
    static std::shared_ptr<spdlog::logger> make_logger_(std::string name, It first, It last)
    {
        using category = typename std::iterator_traits<It>::iterator_category;
        if constexpr (std::is_base_of_v<std::forward_iterator_tag, category>)
        {
            for (auto it = first; it != last; ++it)
            {
                if (!*it)
                {
                    throw std::invalid_argument("jspdlog: sink must not be null");
                }
            }
        }
        return std::make_shared<spdlog::logger>(std::move(name), first, last);
    }

    /** @brief An empty set of fields, shared by the call forms without fields. */
    static const detail::call_fields &empty_call_fields_()
    {
        static const detail::call_fields EMPTY{};
        return EMPTY;
    }

    /** @brief Appends the message as a quoted JSON string. The formatter adds the key. */
    static void append_message_(spdlog::memory_buf_t &out, const spdlog::memory_buf_t &msg)
    {
        detail::append_json_quoted(out, std::string_view{msg.data(), msg.size()});
    }

    /** @brief Writes a line with fields only. */
    void log_fields_(level lvl, const std::exception *ex, const detail::call_fields &f, detail::site where) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, ex, f, where, detail::KIND_FIELDS, [](spdlog::memory_buf_t & /*out*/) {});
        }
    }

    /** @brief Writes fields and a message formatted from a format string and its arguments. */
    template <typename... Args>
    void log_fmt_(
        level lvl, const std::exception *ex, const detail::call_fields &f, format_here_t<Args...> fmt, Args &&...args
    ) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, ex, f, fmt.where(), detail::KIND_MESSAGE, [&](spdlog::memory_buf_t &out) {
                spdlog::memory_buf_t msg;
#if JSPDLOG_STD_FORMAT
                std::vformat_to(
                    std::back_inserter(msg), detail::format_text(fmt.format()), std::make_format_args(args...)
                );
#else
                spdlog::fmt_lib::format_to(std::back_inserter(msg), fmt.format(), std::forward<Args>(args)...);
#endif
                append_message_(out, msg);
            });
        }
    }

    /** @brief Writes fields and a message taken as it is. */
    void log_str_(
        level lvl, const std::exception *ex, const detail::call_fields &f, std::string_view msg, detail::site where
    ) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, ex, f, where, detail::KIND_MESSAGE, [msg](spdlog::memory_buf_t &out) {
                detail::append_json_quoted(out, msg);
            });
        }
    }

    /**
     * @brief Appends the bound fields and the per-call fields, serializing each per-call value that is written.
     *
     * The per-call items are, in order, an optional leading reference (the exception of an exception call) and then
     * the entries of a fields object or the references of a braced list. A per-call item with the key of a bound field
     * replaces its value at the bound field's position, and a key that appears twice among the items takes the last
     * value at the first position. Only the last value of a repeated key is serialized, so an earlier one is never run.
     * The cost is quadratic in the number of items, at most call_fields::CAPACITY plus one.
     */
    void append_props_(
        spdlog::memory_buf_t &out, const detail::call_fields &props, const detail::call_fields::ref *leading
    ) const
    {
        const context_data &ctx = *context_;
        const fields *named = props.named();
        if (leading == nullptr && (named != nullptr ? named->empty() : props.size() == 0))
        {
            detail::append(out, ctx.values.text_);
            return;
        }
        const std::size_t first = leading != nullptr ? 1 : 0;
        const std::size_t count = first + (named != nullptr ? named->entries_.size() : props.size());
        const auto key_of = [&](std::size_t i) -> std::string_view {
            if (i < first)
            {
                return leading->key;
            }
            return named != nullptr ? named->key_of_(named->entries_[i - first]) : props.key(i - first);
        };
        // Inside the deferred writer, so a handler that throws reaches spdlog's error handler.
        for (std::size_t i = 0; i < count; ++i)
        {
            JSPDLOG_ASSERT(
                !ctx.reserved.is_reserved(key_of(i)), "jspdlog: a field key equals a header, source or message key"
            );
            JSPDLOG_ASSERT(
                leading == nullptr || i < first || key_of(i) != leading->key,
                "jspdlog: a field key equals the error key of an exception call"
            );
        }
        if (leading == nullptr && named != nullptr && ctx.values.empty())
        {
            // A fields object has distinct keys, so with nothing bound its text is the whole list.
            detail::append(out, named->text_);
            return;
        }
        const auto append_item = [&](std::size_t i) {
            if (i < first)
            {
                detail::call_fields::append_ref(out, *leading);
            }
            else if (named != nullptr)
            {
                detail::append(out, named->text_of_(i - first));
            }
            else
            {
                detail::call_fields::append_ref(out, props.at(i - first));
            }
        };
        const auto last_with_key = [&](std::string_view k) {
            for (std::size_t i = count; i > 0; --i)
            {
                if (key_of(i - 1) == k)
                {
                    return i - 1;
                }
            }
            return count;
        };
        const auto repeats_earlier = [&](std::size_t i) {
            for (std::size_t j = 0; j < i; ++j)
            {
                if (key_of(j) == key_of(i))
                {
                    return true;
                }
            }
            return false;
        };
        for (std::size_t b = 0; b < ctx.values.entries_.size(); ++b)
        {
            const std::size_t over = last_with_key(ctx.values.key_of_(ctx.values.entries_[b]));
            if (over == count)
            {
                detail::append(out, ctx.values.text_of_(b));
            }
            else
            {
                append_item(over);
            }
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!ctx.values.contains(key_of(i)) && !repeats_earlier(i))
            {
                append_item(last_with_key(key_of(i)));
            }
        }
    }

    /**
     * @brief Logs a payload: the token, the kind, the size of the fields, the bound and per-call fields and then what
     * write_message appends. spdlog runs the serialization while it formats the line, inside its error handling, so
     * the values of a braced list are serialized here and only when the line is written.
     *
     * The fields are an already escaped list, each entry starting with a comma. The exception of an exception call
     * comes first among the per-call fields. A per-call field with the key of a bound field replaces its value at the
     * bound field's position, and a key that appears twice in one braced list takes the last value at the first
     * position, as in a fields object. The message is appended as a quoted JSON
     * string, so it is escaped once however many sinks the logger has.
     */
    template <typename WriteMessage>
    void emit_(
        level lvl,
        const std::exception *ex,
        const detail::call_fields &props,
        detail::site where,
        char kind,
        const WriteMessage &write_message
    ) const
    {
        const context_data &ctx = *context_;
        const auto write = [&](spdlog::memory_buf_t &out) {
            detail::write_payload(out, ctx.token, kind, [&](spdlog::memory_buf_t &fields_out) {
                // The exception is held by reference and written here, when the line is, so a disabled level computes
                // nothing and an exception from writing it reaches the error handler.
                const detail::exception_arg arg{ex, {ctx.errors.type, ctx.errors.message}};
                const detail::call_fields::ref leading{
                    ctx.errors.key,
                    &arg,
                    &detail::call_fields::write_item<&detail::write_exception_arg, false>,
                };
                append_props_(fields_out, props, ex != nullptr ? &leading : nullptr);
            });
            write_message(out);
        };
        // The line type is int in spdlog v1 and unsigned in v2.
        using line_type = decltype(spdlog::source_loc{}.line);
        logger_->log(
            spdlog::source_loc{where.file(), static_cast<line_type>(where.line()), where.function()},
            lvl,
            "{}",
            detail::deferred_payload<decltype(write)>{&write}
        );
    }

    /** @brief The spdlog::logger shared by every copy. */
    std::shared_ptr<spdlog::logger> logger_;
    /** @brief The bound fields and what is derived from them, shared by every copy and never null. */
    std::shared_ptr<const context_data> context_;
};

/** @brief Where and how the error handler made by make_error_handler reports. */
struct error_report_options
{
    /** @brief Keys and time format of the report line. Its process and thread keys apply too. */
    json_options format{};
    /**
     * @brief A sink to write the report to. It must not be a sink of the logger that fails. Null writes to stream. The
     * sink writes the line with its own formatter, so install a json_formatter on it with install_json_formatter.
     */
    spdlog::sink_ptr sink;
    /** @brief Where the line goes when sink is null. When both are null, reports are discarded. */
    std::FILE *stream = stderr;
    /** @brief Reports closer together than this are counted and dropped, as spdlog's default handler does. */
    std::chrono::milliseconds min_interval = std::chrono::seconds{1};
};

/** @brief Implementation details. Not part of the public API. */
namespace detail
{
/** @brief What an error handler keeps and does, shared by every copy of the handler and guarded by its own mutex. */
class error_reporter
{
public:
    /** @brief Creates the state for one handler. */
    error_reporter(std::string logger_name, error_report_options options)
        : formatter_(options.format), name_(std::move(logger_name)), options_(std::move(options))
    {
    }

    /** @brief Writes one report, or counts it as dropped when the last one was too recent. */
    void report(const std::string &what)
    {
        const std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (reported_ && now - last_ < options_.min_interval)
        {
            ++dropped_;
            return;
        }
        reported_ = true;
        last_ = now;
        // This report counts as dropped until it is written, so one that throws is carried by the next report.
        const std::size_t earlier = dropped_++;
        spdlog::memory_buf_t payload;
        write_payload(payload, process_token(), KIND_MESSAGE, [&](spdlog::memory_buf_t &out) {
            if (earlier > 0)
            {
                append(out, std::string_view{",\"dropped\":"});
                append_int(out, earlier);
            }
        });
        append_json_quoted(payload, what);
        const spdlog::details::log_msg msg(
            spdlog::string_view_t(name_.data(), name_.size()),
            level::err,
            spdlog::string_view_t(payload.data(), payload.size())
        );
        if (options_.sink)
        {
            options_.sink->log(msg);
            dropped_ = 0;
            return;
        }
        if (options_.stream == nullptr)
        {
            dropped_ = 0;
            return;
        }
        spdlog::memory_buf_t line;
        formatter_.format(msg, line);
        if (line.size() == 0 || line.data()[line.size() - 1] != '\n')
        {
            line.push_back('\n');
        }
        // Nothing to do when the stream fails: there is nowhere left to report it.
        static_cast<void>(std::fwrite(line.data(), 1, line.size(), options_.stream));
        static_cast<void>(std::fflush(options_.stream));
        dropped_ = 0;
    }

private:
    /** @brief Serializes reports from several threads. It is not an spdlog mutex, so a report never waits on spdlog. */
    std::mutex mutex_;
    /** @brief Writes the line to a stream. */
    json_formatter formatter_;
    /** @brief The logger name of the report. */
    std::string name_;
    /** @brief The options the handler was made with. */
    error_report_options options_;
    /** @brief When the last report was written. */
    std::chrono::steady_clock::time_point last_;
    /** @brief Whether a report was written, so the first one is never dropped. */
    bool reported_ = false;
    /** @brief The reports dropped since the last one that was written. */
    std::size_t dropped_ = 0;
};
} // namespace detail

/**
 * @brief Returns an spdlog error handler that writes each error as one JSON line, never through a logger, so it cannot
 * recurse or deadlock when the failing sink fails again. It does not throw: if the report itself fails, it writes a
 * short plain-text line to stderr. Install it with spdlog::logger::set_error_handler, or in the config of an spdlog v2
 * async_sink.
 *
 * Reports closer together than min_interval are not written; the next one carries their number as dropped. The state
 * lives in the returned handler and its copies, which share it, so there is no global state. A thread-pool worker or
 * async_sink thread may call the handler, and a mutex of its own serializes them.
 *
 * @code
 * logger.spdlog_logger()->set_error_handler(jspdlog::make_error_handler(logger.name()));
 * // {"timestamp":"...","logger":"app","level":"error","dropped":3,"message":"Failed writing to file ..."}
 * @endcode
 */
inline spdlog::err_handler make_error_handler(std::string logger_name, error_report_options options = {})
{
    const auto state = std::make_shared<detail::error_reporter>(std::move(logger_name), std::move(options));
    return [state](const std::string &what) noexcept {
#if JSPDLOG_HAS_EXCEPTIONS
        try
        {
            state->report(what);
        }
        catch (...)
        {
            // Last resort: a failure while reporting must not leave the logging call, nor log again.
            static_cast<void>(std::fputs("jspdlog: the error handler failed\n", stderr));
        }
#else
        state->report(what);
#endif
    };
}

} // namespace jspdlog

#ifndef JSPDLOG_ACTIVE_LEVEL
/**
 * @brief The lowest level that the JSPDLOG_* macros keep, one of the SPDLOG_LEVEL_* values from trace (0) to off (6).
 *
 * A macro below it is removed at compile time: its arguments are type-checked but never run, and optimizing compilers
 * emit nothing for it. Define it before including jspdlog, for example as a compile definition of a release build.
 * It is independent of SPDLOG_ACTIVE_LEVEL, which jspdlog does not read, and only the macros read it, so translation
 * units with different values link together without violating the one definition rule.
 */
#define JSPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

// Unique local names, so a macro inside a lazy value of another macro does not shadow the outer one. __COUNTER__ gives
// each macro its own name, also when both are on one line, where GCC's -Wshadow would report a shared one. Clang
// versions with -Wc2y-extensions report __COUNTER__ under -Wpedantic, and a pragma in the macro cannot cover a nested
// macro, whose __COUNTER__ is expanded with the arguments of the outer one. They use __LINE__: Clang reports the
// shadowing of a local that the lambda does not capture only under -Wshadow-uncaptured-local, which -Wshadow does not
// include.
#if defined(__clang__) && defined(__has_warning)
#if __has_warning("-Wc2y-extensions")
#define JSPDLOG_DETAIL_UNIQUE __LINE__
#endif
#endif
#ifndef JSPDLOG_DETAIL_UNIQUE
#define JSPDLOG_DETAIL_UNIQUE __COUNTER__
#endif
#define JSPDLOG_DETAIL_CAT2(a, b) a##b
#define JSPDLOG_DETAIL_CAT(a, b) JSPDLOG_DETAIL_CAT2(a, b)

// MSVC reports a condition that is constant once the level is known (C4127), which is the point of these macros, and
// the code it guards as unreachable (C4702).
#ifdef _MSC_VER
#define JSPDLOG_DETAIL_BEGIN_CONSTANT_CONDITION __pragma(warning(push)) __pragma(warning(disable : 4127 4702))
#define JSPDLOG_DETAIL_END_CONSTANT_CONDITION __pragma(warning(pop))
#else
#define JSPDLOG_DETAIL_BEGIN_CONSTANT_CONDITION
#define JSPDLOG_DETAIL_END_CONSTANT_CONDITION
#endif

// A macro and not a function: a function whose body depends on JSPDLOG_ACTIVE_LEVEL would differ between translation
// units compiled with different values, which violates the one definition rule.
#if JSPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#define JSPDLOG_DETAIL_ENABLED(lvl) true
#else
#define JSPDLOG_DETAIL_ENABLED(lvl) (static_cast<int>(lvl) >= JSPDLOG_ACTIVE_LEVEL)
#endif

// The logger and the level are evaluated once. Nothing else in the call runs unless the level is on.
#define JSPDLOG_DETAIL_LOG(id, logger, lvl, ...)                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        JSPDLOG_DETAIL_BEGIN_CONSTANT_CONDITION                                                                        \
        const ::jspdlog::json_logger &JSPDLOG_DETAIL_CAT(jspdlog_logger_, id) = (logger);                              \
        const ::jspdlog::level JSPDLOG_DETAIL_CAT(jspdlog_level_, id) = (lvl);                                         \
        if (JSPDLOG_DETAIL_ENABLED(JSPDLOG_DETAIL_CAT(jspdlog_level_, id)) &&                                          \
            JSPDLOG_DETAIL_CAT(jspdlog_logger_, id).should_log(JSPDLOG_DETAIL_CAT(jspdlog_level_, id)))                \
        {                                                                                                              \
            JSPDLOG_DETAIL_CAT(jspdlog_logger_, id).log(JSPDLOG_DETAIL_CAT(jspdlog_level_, id), __VA_ARGS__);          \
        }                                                                                                              \
        JSPDLOG_DETAIL_END_CONSTANT_CONDITION                                                                          \
    } while (false)

// A level below JSPDLOG_ACTIVE_LEVEL: the call is type-checked and its arguments count as used, but it never runs.
// This is if (false) and not an empty statement, so a variable that only logging uses does not become unused.
#define JSPDLOG_DETAIL_REMOVED(logger, lvl, ...)                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        JSPDLOG_DETAIL_BEGIN_CONSTANT_CONDITION                                                                        \
        if (false)                                                                                                     \
        {                                                                                                              \
            (logger).log(lvl, __VA_ARGS__);                                                                            \
        }                                                                                                              \
        JSPDLOG_DETAIL_END_CONSTANT_CONDITION                                                                          \
    } while (false)

/**
 * @brief Logs through a json_logger at a level that may be a variable, evaluating nothing else when it is off.
 *
 * The arguments after the level are anything that json_logger::log takes: a format string and its arguments, a runtime
 * string, or a braced list of fields with or without a message. None of them is evaluated when the level is below the
 * logger's level or below JSPDLOG_ACTIVE_LEVEL, so use it for format arguments that are costly to build. The logger
 * and the level are evaluated once. The macro is a statement. The recorded source line is the line of the macro.
 *
 * @code
 * JSPDLOG_LOG(logger, level, {"plan", explain(q)}, "planned {}", describe(q));
 * @endcode
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_LOG(logger, lvl, ...) JSPDLOG_DETAIL_LOG(JSPDLOG_DETAIL_UNIQUE, logger, lvl, __VA_ARGS__)

#if JSPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
/** @brief Like JSPDLOG_LOG at the trace level, and removed at compile time when JSPDLOG_ACTIVE_LEVEL is above trace. */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_TRACE(logger, ...) JSPDLOG_LOG(logger, ::jspdlog::level::trace, __VA_ARGS__)
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_TRACE(logger, ...) JSPDLOG_DETAIL_REMOVED(logger, ::jspdlog::level::trace, __VA_ARGS__)
#endif

#if JSPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
/** @brief Like JSPDLOG_LOG at the debug level, and removed at compile time when JSPDLOG_ACTIVE_LEVEL is above debug. */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_DEBUG(logger, ...) JSPDLOG_LOG(logger, ::jspdlog::level::debug, __VA_ARGS__)
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_DEBUG(logger, ...) JSPDLOG_DETAIL_REMOVED(logger, ::jspdlog::level::debug, __VA_ARGS__)
#endif

#if JSPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
/** @brief Like JSPDLOG_LOG at the info level, and removed at compile time when JSPDLOG_ACTIVE_LEVEL is above info. */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_INFO(logger, ...) JSPDLOG_LOG(logger, ::jspdlog::level::info, __VA_ARGS__)
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_INFO(logger, ...) JSPDLOG_DETAIL_REMOVED(logger, ::jspdlog::level::info, __VA_ARGS__)
#endif

#if JSPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
/** @brief Like JSPDLOG_LOG at the warn level, and removed at compile time when JSPDLOG_ACTIVE_LEVEL is above warn. */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_WARN(logger, ...) JSPDLOG_LOG(logger, ::jspdlog::level::warn, __VA_ARGS__)
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_WARN(logger, ...) JSPDLOG_DETAIL_REMOVED(logger, ::jspdlog::level::warn, __VA_ARGS__)
#endif

#if JSPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
/** @brief Like JSPDLOG_LOG at the error level, and removed at compile time when JSPDLOG_ACTIVE_LEVEL is above error. */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_ERROR(logger, ...) JSPDLOG_LOG(logger, ::jspdlog::level::err, __VA_ARGS__)
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_ERROR(logger, ...) JSPDLOG_DETAIL_REMOVED(logger, ::jspdlog::level::err, __VA_ARGS__)
#endif

#if JSPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
/**
 * @brief Like JSPDLOG_LOG at the critical level, and removed at compile time when JSPDLOG_ACTIVE_LEVEL is above
 * critical.
 */
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_CRITICAL(logger, ...) JSPDLOG_LOG(logger, ::jspdlog::level::critical, __VA_ARGS__)
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the arguments must not be evaluated when the level is off
#define JSPDLOG_CRITICAL(logger, ...) JSPDLOG_DETAIL_REMOVED(logger, ::jspdlog::level::critical, __VA_ARGS__)
#endif
