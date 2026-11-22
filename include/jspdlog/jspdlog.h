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
 * A json_formatter renders the whole line from the spdlog message: the header fields, the call site when asked for, the
 * fields and the message. A json_logger hands it the fields and the message in a payload that starts with a random,
 * process-wide token, and the formatter recognizes that token. Any other payload, such as a message logged directly on
 * the underlying spdlog::logger, by a macro or by third-party code, is escaped into the message field. So every line
 * that a json_formatter writes is one valid JSON object, whoever logged it. install_json_formatter gives the same
 * guarantee to loggers and sinks that jspdlog does not wrap. A sink that writes the raw payload instead of the
 * formatted line shows the payload protocol, which is not JSON. Logging calls do not throw: a formatting error goes to
 * the spdlog error handler, as in spdlog.
 *
 * @copyright Copyright (c) 2026 Jan Moravec. Distributed under the MIT License, see LICENSE.
 */

#pragma once

/** @brief Major version of jspdlog. */
#define JSPDLOG_VERSION_MAJOR 0
/** @brief Minor version of jspdlog. */
#define JSPDLOG_VERSION_MINOR 1
/** @brief Patch version of jspdlog. */
#define JSPDLOG_VERSION_PATCH 0
/** @brief The version as one number, major * 10000 + minor * 100 + patch, for #if comparisons. */
#define JSPDLOG_VERSION ((JSPDLOG_VERSION_MAJOR * 10000) + (JSPDLOG_VERSION_MINOR * 100) + JSPDLOG_VERSION_PATCH)

#include <spdlog/version.h>

#if SPDLOG_VER_MAJOR >= 2
#include <fmt/format.h>
/** @brief 1 when building against spdlog v2.x, 0 for v1.x. */
#define JSPDLOG_SPDLOG_V2 1
#else
#if SPDLOG_VERSION < 11200
#error "jspdlog requires spdlog >= 1.12 (or v2.x)"
#endif
#ifdef SPDLOG_USE_STD_FORMAT
#error "jspdlog does not support SPDLOG_USE_STD_FORMAT yet; build spdlog with {fmt}"
#endif
#include <spdlog/fmt/fmt.h>
#define JSPDLOG_SPDLOG_V2 0
#endif

#include <spdlog/common.h>
#include <spdlog/details/os.h>
#include <spdlog/formatter.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef JSPDLOG_ASSERT
#include <cassert>
/**
 * @brief Called with a condition that must hold and a message. Debug builds use it to find field keys that clash with
 * the keys of the line. Define it before including jspdlog to use your own handler, for example one that logs. With
 * NDEBUG the default expands to nothing and the condition is not evaluated.
 */
#define JSPDLOG_ASSERT(cond, msg) assert((cond) && (msg))
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

/**
 * @brief Settings of a json_logger or a json_formatter.
 *
 * The key members control the header fields, which always appear in this order: timestamp, logger, level, process,
 * thread, source. They are followed by the fields and then the message. A string renames a field and an empty optional
 * leaves it out. Keys are escaped, so any text is safe. With every optional field left out, a line holds only the
 * fields and the message.
 */
struct json_options
{
    /** @brief Key of the RFC 3339 timestamp with milliseconds. */
    std::optional<std::string> timestamp_key = std::string{"timestamp"};
    /** @brief Key of the logger name. */
    std::optional<std::string> logger_key = std::string{"logger"};
    /** @brief Key of the level name, as spdlog spells it (for example warning). */
    std::optional<std::string> level_key = std::string{"level"};
    /** @brief Key of the process id, written as a number. */
    std::optional<std::string> process_key = std::string{"process"};
    /** @brief Key of the thread id, written as a number. */
    std::optional<std::string> thread_key = std::string{"thread"};
    /**
     * @brief Key of the call site, written as an object with file (the base name), line and function. Off by default.
     * json_logger calls record their call site. Lines without one, such as those logged through a plain spdlog macro
     * without source information, have no such field.
     */
    std::optional<std::string> source_key;
    /** @brief Key of the message. It is not optional, because a message from outside jspdlog needs a place to go. */
    std::string message_key = "message";

    /** @brief Local time with its UTC offset, or UTC ending in Z. */
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
 * Use it to log arrays or objects produced by a JSON library. jspdlog does not check the text, so the caller must make
 * sure it is valid JSON. An empty value is written as null.
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

/**
 * @brief Appends text to a buffer as a quoted JSON string.
 *
 * Quotes, backslashes and control characters are escaped. Other bytes are copied unchanged, so the text should be
 * UTF-8. The buffer can be a std::string or an spdlog memory buffer.
 */
template <typename Buffer>
void append_json_quoted(Buffer &out, std::string_view s)
{
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    static constexpr unsigned FIRST_PRINTABLE = 0x20U; // Bytes below this are control characters.
    static constexpr unsigned NIBBLE_BITS = 4U;
    static constexpr unsigned NIBBLE_MASK = 0xFU;
    out.push_back('"');
    const char *run = s.data(); // Start of the bytes not yet copied.
    const char *const end = s.data() + s.size();
    for (const char *p = run; p != end; ++p)
    {
        const unsigned c = static_cast<unsigned char>(*p);
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

/** @brief Formats a jspdlog::detail::deferred_payload by running its write function. */
template <typename Write>
struct fmt::formatter<jspdlog::detail::deferred_payload<Write>>
{
    /** @brief Accepts only an empty format specification. */
    constexpr auto parse(fmt::format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

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
 * The line holds the header fields chosen in json_options, the call site if source_key is set, the fields of a
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
            detail::append_json_quoted(dest, detail::level_name(detail::msg_level(msg)));
        }
        if (opts_.process_key)
        {
            key(process_key_);
            detail::append_int(dest, spdlog::details::os::pid());
        }
        if (opts_.thread_key)
        {
            key(thread_key_);
            detail::append_int(dest, msg.thread_id);
        }
        if (opts_.source_key && !msg.source.empty())
        {
            key(source_key_);
            append_source_(msg.source, dest);
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
        process_key_ = prepare(opts_.process_key);
        thread_key_ = prepare(opts_.thread_key);
        source_key_ = prepare(opts_.source_key);
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

    /** @brief Appends the quoted RFC 3339 timestamp with milliseconds, rebuilding the cached part once per second. */
    void append_timestamp_(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest)
    {
        static constexpr int YEAR_BASE = 1900;
        static constexpr int CENTURY = 100;
        static constexpr int MINUTES_PER_HOUR = 60;
        static constexpr int MILLIS_HUNDREDS = 100;
        const auto since_epoch = msg.time.time_since_epoch();
        // floor keeps the milliseconds in 0 to 999 for times before the epoch too.
        const auto whole = std::chrono::floor<std::chrono::seconds>(since_epoch);
        const auto millis =
            static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch - whole).count());
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
        dest.push_back(static_cast<char>('0' + (millis / MILLIS_HUNDREDS)));
        append_2_(dest, millis % MILLIS_HUNDREDS);
        detail::append(dest, zone_);
    }

    /**
     * @brief Appends the call site as an object, with the base name of the file.
     *
     * Nothing is cached, because a file name pointer may be reused for different text once a dynamic call site ends.
     */
    static void append_source_(const spdlog::source_loc &src, spdlog::memory_buf_t &dest)
    {
        std::string_view file{src.filename != nullptr ? src.filename : ""};
        if (const auto slash = file.find_last_of("/\\"); slash != std::string_view::npos)
        {
            file.remove_prefix(slash + 1);
        }
        detail::append(dest, std::string_view{R"({"file":)"});
        detail::append_json_quoted(dest, file);
        detail::append(dest, std::string_view{R"(,"line":)"});
        detail::append_int(dest, src.line);
        detail::append(dest, std::string_view{R"(,"function":)"});
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
    std::string timestamp_key_, logger_key_, level_key_, process_key_, thread_key_, source_key_;
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
 * @brief Typed key/value fields for a log line, serialized when they are inserted.
 *
 * Pass them per call, usually as a braced list such as {"user_id", 42, "ok", true}, or bind them to a child logger with
 * json_logger::with. Values keep their JSON type, so numbers and booleans stay filterable as such. Fields keep the
 * order in which their keys were first inserted. Inserting a key that exists replaces its value and keeps its position,
 * and when two sets are merged the right-hand side wins.
 *
 * Fields are meant to be small: a lookup scans the keys, which beats a map for the handful of keys a log line carries.
 * Values are serialized as soon as the set is built, so a braced list passed to a disabled level still costs its
 * serialization. Guard expensive fields with json_logger::should_log.
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
        insert_pairs_(std::forward<K>(key), std::forward<V>(value), std::forward<Rest>(rest)...);
    }

    /**
     * @brief Inserts null. The key is copied at once, so it may be a temporary; the same holds for every insert.
     */
    void insert(std::string_view key, std::nullptr_t) { set_(key, "null"); }

    /** @brief Inserts a string. A std::string converts to this overload. */
    void insert(std::string_view key, std::string_view value)
    {
        std::string encoded;
        encoded.reserve(value.size() + 2);
        detail::append_json_quoted(encoded, value);
        set_(key, encoded);
    }

    /** @brief Inserts a C string, or null for a null pointer. */
    void insert(std::string_view key, const char *value)
    {
        if (value == nullptr)
        {
            set_(key, "null");
        }
        else
        {
            insert(key, std::string_view{value});
        }
    }

    /** @brief Inserts true or false. */
    void insert(std::string_view key, bool value) { set_(key, value ? "true" : "false"); }

    /**
     * @brief Inserts a one-character string. Signed and unsigned char, and so int8_t and uint8_t, are numbers instead.
     */
    void insert(std::string_view key, char value) { insert(key, std::string_view{&value, 1}); }

    /** @brief Inserts an integer of any width as a JSON number. */
    template <
        typename T,
        std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>, int> = 0>
    void insert(std::string_view key, T value)
    {
        std::string text;
        detail::append_int(text, value);
        set_(key, text);
    }

    /**
     * @brief Inserts a floating-point number.
     *
     * A whole number keeps a trailing .0, so consumers that infer types from the text still see a float. NaN and
     * infinity become null, because JSON has no token for them.
     */
    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    void insert(std::string_view key, T value)
    {
        if (!std::isfinite(value))
        {
            set_(key, "null");
            return;
        }
        std::string s = spdlog::fmt_lib::format("{}", value);
        if (s.find_first_of(".eE") == std::string::npos)
        {
            s += ".0";
        }
        set_(key, s);
    }

    /** @brief Inserts pre-serialized JSON unchanged, or null when it is empty. */
    void insert(std::string_view key, const raw_json &value)
    {
        set_(key, value.value.empty() ? std::string_view{"null"} : std::string_view{value.value});
    }

    /** @brief Inserts other fields as a nested JSON object. */
    void insert(std::string_view key, const fields &value) { set_(key, value.to_json()); }

    /**
     * @brief Wide characters are rejected, because encoding them as UTF-8 is the caller's choice. Convert to a UTF-8
     * std::string first.
     */
    void insert(std::string_view, wchar_t) = delete;
    /** @brief Rejected like wchar_t. Convert to a UTF-8 std::string first. */
    void insert(std::string_view, char16_t) = delete;
    /** @brief Rejected like wchar_t. Convert to a UTF-8 std::string first. */
    void insert(std::string_view, char32_t) = delete;
#ifdef __cpp_char8_t
    /** @brief Rejected, because a single UTF-8 code unit is not always a character. Use a std::string instead. */
    void insert(std::string_view, char8_t) = delete;
#endif
    /**
     * @brief Pointers other than C strings are rejected. Without this overload they would silently convert to bool.
     */
    template <typename T>
    void insert(std::string_view, const T *) = delete;

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
        for (const auto &e : other.entries_)
        {
            put_(e.key, std::string{e.text});
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
    [[nodiscard]] bool contains(std::string_view key) const noexcept { return find_(key) != nullptr; }
    /** @brief Removes every field. */
    void clear() noexcept { entries_.clear(); }

    /** @brief Returns the fields as a JSON object in insertion order. An empty set gives {}. */
    [[nodiscard]] std::string to_json() const
    {
        std::string out = fragment_();
        if (out.empty())
        {
            return "{}";
        }
        out.front() = '{';
        out.push_back('}');
        return out;
    }

private:
    friend class json_logger;

    /** @brief One field: the raw key for lookups and the serialized text ready to append. */
    struct entry
    {
        /** @brief The key as given, for lookups and merges. */
        std::string key;
        /** @brief A comma, the quoted key, a colon and the value. */
        std::string text;
    };

    template <typename K, typename V, typename... Rest>
    void insert_pairs_(K &&key, V &&value, Rest &&...rest)
    {
        static_assert(
            std::is_convertible_v<std::decay_t<K>, std::string_view>,
            "fields: key must be convertible to std::string_view"
        );
        insert(std::string_view(key), std::forward<V>(value));
        if constexpr (sizeof...(Rest) > 0)
        {
            insert_pairs_(std::forward<Rest>(rest)...);
        }
    }

    /** @brief The entry with this key, or null. */
    [[nodiscard]] const entry *find_(std::string_view key) const noexcept
    {
        for (const auto &e : entries_)
        {
            if (e.key == key)
            {
                return &e;
            }
        }
        return nullptr;
    }

    /** @brief Stores a field whose text is ready. A key that exists keeps its position. */
    void put_(std::string_view key, std::string text)
    {
        for (auto &e : entries_)
        {
            if (e.key == key)
            {
                e.text = std::move(text);
                return;
            }
        }
        entries_.push_back(entry{std::string{key}, std::move(text)});
    }

    /** @brief Stores a value that is already serialized. */
    void set_(std::string_view key, std::string_view value)
    {
        std::string text;
        text.reserve(key.size() + value.size() + 4);
        text.push_back(',');
        detail::append_json_quoted(text, key);
        text.push_back(':');
        detail::append(text, value);
        put_(key, std::move(text));
    }

    /** @brief Every entry text joined, so each field starts with a comma. */
    [[nodiscard]] std::string fragment_() const
    {
        std::string out;
        for (const auto &e : entries_)
        {
            out += e.text;
        }
        return out;
    }

    /** @brief The fields in insertion order. */
    std::vector<entry> entries_;
};

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

    /** @brief Collects the enabled header keys, the source key and the message key. */
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
        add(options.process_key);
        add(options.thread_key);
        add(options.source_key);
        keys_.push_back(options.message_key);
    }

    /** @brief Whether any key of the fields is one of the reserved keys. Only top-level keys are compared. */
    [[nodiscard]] bool clashes_with(const fields &f) const noexcept
    {
        return std::any_of(keys_.begin(), keys_.end(), [&f](const std::string &key) { return f.contains(key); });
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

/** @brief The type of fmt::runtime(...) in the {fmt} version in use. */
using runtime_format_string = decltype(spdlog::fmt_lib::runtime(""));

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

/** @brief Whether a single argument is neither a string nor a format string, so it cannot be a message. */
template <typename T>
inline constexpr bool is_rejected_message_v =
    !is_runtime_string_v<T> && !std::is_array_v<T> && !std::is_same_v<T, runtime_format_string>;

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
    FMT_CONSTEVAL basic_format_here(const S &s, detail::site w = detail::site())
        : fmt_(s), where_(w)
    {
    }

    /** @brief Converts fmt::runtime(...), capturing the call site of the logging function. */
    // Implicit on purpose: a string literal or fmt::runtime must convert at the call.
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor,misc-explicit-constructor)
    basic_format_here(detail::runtime_format_string r, detail::site w = detail::site())
        : fmt_(r), where_(w)
    {
    }

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
 * @endcode
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
        ctx->fragment = ctx->values.fragment_();
        json_logger child = *this;
        child.context_ = std::move(ctx);
        return child;
    }

    /** @brief The fields bound to this logger by with. */
    [[nodiscard]] const fields &context() const noexcept { return context_->values; }

    /** @brief Writes a line with fields only, without a message. */
    void log(level lvl, const fields &f, detail::site where = detail::site()) const { log_fields_(lvl, f, where); }

    /** @brief Writes a formatted message. The format string is checked at compile time. */
    template <typename... Args>
    void log(level lvl, format_here_t<Args...> fmt, Args &&...args) const
    {
        log_fmt_(lvl, empty_fields_(), fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes a runtime string as the message. */
    template <typename S, detail::if_runtime_string<S> = 0>
    void log(level lvl, const S &msg, detail::site where = detail::site()) const
    {
        log_str_(lvl, empty_fields_(), detail::message_view(msg), where);
    }

    /** @brief Writes fields and a formatted message. */
    template <typename... Args>
    void log(level lvl, const fields &f, format_here_t<Args...> fmt, Args &&...args) const
    {
        log_fmt_(lvl, f, fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes fields and a runtime string as the message. */
    template <typename S, detail::if_runtime_string<S> = 0>
    void log(level lvl, const fields &f, const S &msg, detail::site where = detail::site()) const
    {
        log_str_(lvl, f, detail::message_view(msg), where);
    }

    /** @brief Rejects a single argument that is not a string. Put typed values in fields. */
    template <typename T, std::enable_if_t<detail::is_rejected_message_v<T>, int> = 0>
    void log(level lvl, const T &) const = delete;

// Defines the call forms for one level. Undefined right after use.
#define JSPDLOG_DETAIL_LEVEL_FUNCTIONS(name, lvl)                                                                      \
    template <typename... Args>                                                                                        \
    void name(format_here_t<Args...> fmt, Args &&...args) const                                                        \
    {                                                                                                                  \
        log_fmt_(lvl, empty_fields_(), fmt, std::forward<Args>(args)...);                                              \
    }                                                                                                                  \
    template <typename S, detail::if_runtime_string<S> = 0>                                                            \
    void name(const S &msg, detail::site where = detail::site()) const                                                 \
    {                                                                                                                  \
        log_str_(lvl, empty_fields_(), detail::message_view(msg), where);                                              \
    }                                                                                                                  \
    void name(const fields &f, detail::site where = detail::site()) const                                              \
    {                                                                                                                  \
        log_fields_(lvl, f, where);                                                                                    \
    }                                                                                                                  \
    template <typename... Args>                                                                                        \
    void name(const fields &f, format_here_t<Args...> fmt, Args &&...args) const                                       \
    {                                                                                                                  \
        log_fmt_(lvl, f, fmt, std::forward<Args>(args)...);                                                            \
    }                                                                                                                  \
    template <typename S, detail::if_runtime_string<S> = 0>                                                            \
    void name(const fields &f, const S &msg, detail::site where = detail::site()) const                                \
    {                                                                                                                  \
        log_str_(lvl, f, detail::message_view(msg), where);                                                            \
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
        /** @brief The values serialized once, each field starting with a comma. */
        std::string fragment;
        /** @brief The token that starts every payload, the same as in the formatter this logger installed. */
        detail::payload_token token{};
        /** @brief The keys the logger writes itself, for the debug check. */
        detail::reserved_keys reserved;
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
    static const fields &empty_fields_()
    {
        static const fields EMPTY;
        return EMPTY;
    }

    /** @brief Appends the message as a quoted JSON string. The formatter adds the key. */
    static void append_message_(spdlog::memory_buf_t &out, const spdlog::memory_buf_t &msg)
    {
        detail::append_json_quoted(out, std::string_view{msg.data(), msg.size()});
    }

    /** @brief Writes a line with fields only. */
    void log_fields_(level lvl, const fields &f, detail::site where) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, f, where, detail::KIND_FIELDS, [](spdlog::memory_buf_t & /*out*/) {});
        }
    }

    /** @brief Writes fields and a message formatted from a format string and its arguments. */
    template <typename... Args>
    void log_fmt_(level lvl, const fields &f, format_here_t<Args...> fmt, Args &&...args) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, f, fmt.where(), detail::KIND_MESSAGE, [&](spdlog::memory_buf_t &out) {
                spdlog::memory_buf_t msg;
                spdlog::fmt_lib::format_to(std::back_inserter(msg), fmt.format(), std::forward<Args>(args)...);
                append_message_(out, msg);
            });
        }
    }

    /** @brief Writes fields and a message taken as it is. */
    void log_str_(level lvl, const fields &f, std::string_view msg, detail::site where) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, f, where, detail::KIND_MESSAGE, [msg](spdlog::memory_buf_t &out) {
                detail::append_json_quoted(out, msg);
            });
        }
    }

    /**
     * @brief Logs a payload: the token, the kind, the size of the fields, the bound and per-call fields and then what
     * write_message appends. spdlog runs the serialization while it formats the line, inside its error handling.
     *
     * The fields are an already escaped list, each entry starting with a comma. A per-call field with the key of a
     * bound field replaces its value at the bound field's position. The message is appended as a quoted JSON string,
     * so it is escaped once however many sinks the logger has.
     */
    template <typename WriteMessage>
    void emit_(level lvl, const fields &props, detail::site where, char kind, const WriteMessage &write_message) const
    {
        const context_data &ctx = *context_;
        const auto write = [&](spdlog::memory_buf_t &out) {
            if (!props.empty())
            {
                // Inside the deferred writer, so a handler that throws reaches spdlog's error handler.
                JSPDLOG_ASSERT(
                    !ctx.reserved.clashes_with(props), "jspdlog: a field key equals a header, source or message key"
                );
            }
            detail::append(out, std::string_view{ctx.token.data(), ctx.token.size()});
            out.push_back(kind);
            const std::size_t size_at = out.size();
            detail::append(out, std::string_view{"00000000", detail::SIZE_DIGITS});
            const std::size_t fields_at = out.size();
            if (props.empty())
            {
                detail::append(out, ctx.fragment);
            }
            else
            {
                for (const auto &e : ctx.values.entries_)
                {
                    const fields::entry *over = props.find_(e.key);
                    detail::append(out, over != nullptr ? over->text : e.text);
                }
                for (const auto &e : props.entries_)
                {
                    if (!ctx.values.contains(e.key))
                    {
                        detail::append(out, e.text);
                    }
                }
            }
            detail::patch_hex_size(out, size_at, out.size() - fields_at);
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

} // namespace jspdlog
