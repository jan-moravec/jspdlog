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
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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
     * Lines without a call site, such as those from json_logger calls, have no such field.
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

/** @brief std::remove_cvref_t for C++17. */
template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

/**
 * @brief Whether a type can be logged as a message on its own.
 *
 * Anything {fmt} can format qualifies, except character arrays and pointers other than char pointers. String literals
 * go to the format-string overloads instead, so their placeholders are checked at compile time. Other pointers would
 * print as addresses.
 */
template <typename T>
inline constexpr bool is_loggable_value_v =
    spdlog::fmt_lib::is_formattable<T>::value && !std::is_array_v<remove_cvref_t<T>> &&
    (!std::is_pointer_v<remove_cvref_t<T>> ||
     std::is_same_v<std::remove_cv_t<std::remove_pointer_t<remove_cvref_t<T>>>, char>);

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
 * json_logger::with_properties. Values keep their JSON type, so numbers and booleans stay filterable as such. Keys are
 * written sorted byte by byte, which is alphabetical for ASCII keys. Inserting a key that exists replaces its value,
 * and when two sets are merged the right-hand side wins.
 *
 * Values are serialized as soon as the set is built, so a braced list passed to a disabled level still costs its
 * serialization. Guard expensive fields with json_logger::should_log.
 *
 * A key equal to one of the logger's header keys or to message produces a duplicate key in the line. Avoiding that is
 * up to the caller.
 */
class json_properties
{
public:
    /** @brief Creates an empty set. Add fields with insert or merge. */
    json_properties() = default;

    /**
     * @brief Creates fields from alternating keys and values. Keys may be anything convertible to std::string or
     * std::string_view.
     */
    template <typename K, typename V, typename... Rest>
    json_properties(K &&key, V &&value, Rest &&...rest)
    {
        static_assert(sizeof...(Rest) % 2 == 0, "json_properties: expected key/value pairs");
        insert_pairs_(std::forward<K>(key), std::forward<V>(value), std::forward<Rest>(rest)...);
    }

    /** @brief Inserts null. */
    void insert(std::string key, std::nullptr_t) { set_(std::move(key), "null"); }

    /** @brief Inserts a string. A std::string converts to this overload. */
    void insert(std::string key, std::string_view value)
    {
        std::string encoded;
        encoded.reserve(value.size() + 2);
        detail::append_json_quoted(encoded, value);
        set_(std::move(key), std::move(encoded));
    }

    /** @brief Inserts a C string, or null for a null pointer. */
    void insert(std::string key, const char *value)
    {
        if (value == nullptr)
        {
            set_(std::move(key), "null");
        }
        else
        {
            insert(std::move(key), std::string_view{value});
        }
    }

    /** @brief Inserts true or false. */
    void insert(std::string key, bool value) { set_(std::move(key), value ? "true" : "false"); }

    /**
     * @brief Inserts a one-character string. Signed and unsigned char, and so int8_t and uint8_t, are numbers instead.
     */
    void insert(std::string key, char value) { insert(std::move(key), std::string_view{&value, 1}); }

    /** @brief Inserts an integer of any width as a JSON number. */
    template <
        typename T,
        std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>, int> = 0>
    void insert(std::string key, T value)
    {
        set_(std::move(key), std::to_string(value));
    }

    /**
     * @brief Inserts a floating-point number.
     *
     * A whole number keeps a trailing .0, so consumers that infer types from the text still see a float. NaN and
     * infinity become null, because JSON has no token for them.
     */
    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    void insert(std::string key, T value)
    {
        if (!std::isfinite(value))
        {
            set_(std::move(key), "null");
            return;
        }
        std::string s = spdlog::fmt_lib::format("{}", value);
        if (s.find_first_of(".eE") == std::string::npos)
        {
            s += ".0";
        }
        set_(std::move(key), std::move(s));
    }

    /** @brief Inserts pre-serialized JSON unchanged, or null when it is empty. */
    void insert(std::string key, raw_json value)
    {
        set_(std::move(key), value.value.empty() ? std::string{"null"} : std::move(value.value));
    }

    /** @brief Inserts other fields as a nested JSON object. */
    void insert(std::string key, const json_properties &value) { set_(std::move(key), value.to_json()); }

    /**
     * @brief Wide characters are rejected, because encoding them as UTF-8 is the caller's choice. Convert to a UTF-8
     * std::string first.
     */
    void insert(std::string, wchar_t) = delete;
    /** @brief Rejected like wchar_t. Convert to a UTF-8 std::string first. */
    void insert(std::string, char16_t) = delete;
    /** @brief Rejected like wchar_t. Convert to a UTF-8 std::string first. */
    void insert(std::string, char32_t) = delete;
#ifdef __cpp_char8_t
    /** @brief Rejected, because a single UTF-8 code unit is not always a character. Use a std::string instead. */
    void insert(std::string, char8_t) = delete;
#endif
    /**
     * @brief Pointers other than C strings are rejected. Without this overload they would silently convert to bool.
     */
    template <typename T>
    void insert(std::string, const T *) = delete;

    /** @brief Adds every field of other. Where both have a key, the value from other wins. */
    void merge(json_properties other)
    {
        for (auto &[key, value] : other.members_)
        {
            members_.insert_or_assign(key, std::move(value));
        }
    }

    /** @brief Returns the merge of both sets. Where both have a key, the value from rhs wins. */
    friend json_properties operator+(json_properties lhs, json_properties rhs)
    {
        lhs.merge(std::move(rhs));
        return lhs;
    }

    /** @brief Whether there are no fields. */
    [[nodiscard]] bool empty() const noexcept { return members_.empty(); }
    /** @brief The number of distinct keys. */
    [[nodiscard]] std::size_t size() const noexcept { return members_.size(); }
    /** @brief Removes every field. */
    void clear() noexcept { members_.clear(); }

    /** @brief Returns the fields as a JSON object. An empty set gives {}. */
    [[nodiscard]] std::string to_json() const
    {
        std::string out;
        append_merged_(out, *this, json_properties{});
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

    template <typename K, typename V, typename... Rest>
    void insert_pairs_(K &&key, V &&value, Rest &&...rest)
    {
        if constexpr (std::is_convertible_v<std::decay_t<K>, std::string>)
        {
            insert(std::string(std::forward<K>(key)), std::forward<V>(value));
        }
        else
        {
            static_assert(
                std::is_convertible_v<std::decay_t<K>, std::string_view>,
                "json_properties: key must be convertible to std::string or std::string_view"
            );
            insert(std::string(std::string_view(key)), std::forward<V>(value));
        }
        if constexpr (sizeof...(Rest) > 0)
        {
            insert_pairs_(std::forward<Rest>(rest)...);
        }
    }

    /** @brief Stores a value that is already serialized. */
    void set_(std::string key, std::string json) { members_.insert_or_assign(std::move(key), std::move(json)); }

    /**
     * @brief Appends a comma, the quoted key, a colon and the value for every field of lhs merged with rhs.
     *
     * Where both have a key, the value from rhs wins. Both maps are sorted, so one pass over each is enough.
     */
    template <typename Buffer>
    static void append_merged_(Buffer &out, const json_properties &lhs, const json_properties &rhs)
    {
        const auto emit = [&out](const std::string &key, const std::string &value) {
            out.push_back(',');
            detail::append_json_quoted(out, key);
            out.push_back(':');
            out.append(value.data(), value.data() + value.size());
        };
        auto l = lhs.members_.begin();
        auto r = rhs.members_.begin();
        while (l != lhs.members_.end() || r != rhs.members_.end())
        {
            if (r == rhs.members_.end() || (l != lhs.members_.end() && l->first < r->first))
            {
                emit(l->first, l->second);
                ++l;
            }
            else
            {
                if (l != lhs.members_.end() && l->first == r->first)
                {
                    ++l; // The rhs value replaces this one.
                }
                emit(r->first, r->second);
                ++r;
            }
        }
    }

    /** @brief Maps each key to its serialized JSON value. */
    std::map<std::string, std::string, std::less<>> members_;
};

/**
 * @brief A logger that writes one JSON object per line.
 *
 * Copies share the underlying spdlog::logger, so its level, sinks and error handler apply to every copy. Bound fields
 * belong to each copy and are copied with it: with_properties returns a child that adds fields to every line it writes,
 * which is how inner code adds context without affecting its caller.
 *
 * The logging calls are const and only read the json_logger, so inner code can take a const reference, and several
 * threads can log through one json_logger at once. Whether their lines are written safely is up to the sinks, as in
 * spdlog: use the _mt sinks when logging from several threads.
 *
 * Each level has five call forms, also available through log with the level as the first argument:
 *
 * @code
 * logger.info("loaded {} items", n);    // formatted message
 * logger.info(text);                    // a value as the message
 * logger.info({"items", n});            // fields only, no message
 * logger.info({"items", n}, "loaded");  // fields and a formatted message
 * logger.info({"items", n}, text);      // fields and a value as the message
 * @endcode
 *
 * The message is always a JSON string. Put values that should keep their type in fields.
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
     * @brief Returns a child logger that adds props to every line it writes.
     *
     * The child shares this logger's spdlog::logger and keeps this logger's bound fields. Where both have a key, the
     * value from props wins. This logger is not changed.
     */
    [[nodiscard]] json_logger with_properties(json_properties props) const
    {
        json_logger child = *this;
        child.properties_.merge(std::move(props));
        child.bound_.clear();
        json_properties::append_merged_(child.bound_, child.properties_, empty_properties_());
        return child;
    }

    /** @brief The fields bound to this logger by with_properties. */
    [[nodiscard]] const json_properties &properties() const noexcept { return properties_; }

    /** @brief Writes a line with fields only, without a message. */
    void log(level lvl, const json_properties &props) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, props, detail::KIND_FIELDS, [](spdlog::memory_buf_t & /*out*/) {});
        }
    }

    /** @brief Writes a formatted message. The format string is checked at compile time. */
    template <typename... Args>
    void log(level lvl, spdlog::format_string_t<Args...> fmt, Args &&...args) const
    {
        log(lvl, empty_properties_(), fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes a value formatted with {fmt} as the message, for example a runtime string. */
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>
    void log(level lvl, const T &msg) const
    {
        log(lvl, empty_properties_(), msg);
    }

    /** @brief Writes fields and a formatted message. */
    template <typename... Args>
    void log(level lvl, const json_properties &props, spdlog::format_string_t<Args...> fmt, Args &&...args) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, props, detail::KIND_MESSAGE, [&](spdlog::memory_buf_t &out) {
                spdlog::memory_buf_t msg;
                spdlog::fmt_lib::format_to(std::back_inserter(msg), fmt, std::forward<Args>(args)...);
                append_message_(out, msg);
            });
        }
    }

    /** @brief Writes fields and a value formatted with {fmt} as the message. */
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>
    void log(level lvl, const json_properties &props, const T &msg) const
    {
        if (should_log(lvl))
        {
            emit_(lvl, props, detail::KIND_MESSAGE, [&msg](spdlog::memory_buf_t &out) {
                spdlog::memory_buf_t buf;
                spdlog::fmt_lib::format_to(std::back_inserter(buf), "{}", msg);
                append_message_(out, buf);
            });
        }
    }

// Defines the five call forms for one level. Undefined right after use.
#define JSPDLOG_DETAIL_LEVEL_FUNCTIONS(name, lvl)                                                                      \
    template <typename... Args>                                                                                        \
    void name(spdlog::format_string_t<Args...> fmt, Args &&...args) const                                              \
    {                                                                                                                  \
        log(lvl, fmt, std::forward<Args>(args)...);                                                                    \
    }                                                                                                                  \
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>                                   \
    void name(const T &msg) const                                                                                      \
    {                                                                                                                  \
        log(lvl, msg);                                                                                                 \
    }                                                                                                                  \
    void name(const json_properties &props) const                                                                      \
    {                                                                                                                  \
        log(lvl, props);                                                                                               \
    }                                                                                                                  \
    template <typename... Args>                                                                                        \
    void name(const json_properties &props, spdlog::format_string_t<Args...> fmt, Args &&...args) const                \
    {                                                                                                                  \
        log(lvl, props, fmt, std::forward<Args>(args)...);                                                             \
    }                                                                                                                  \
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>                                   \
    void name(const json_properties &props, const T &msg) const                                                        \
    {                                                                                                                  \
        log(lvl, props, msg);                                                                                          \
    }

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
    /** @brief Installs a json_formatter on a logger that is known not to be null. */
    json_logger(std::shared_ptr<spdlog::logger> logger, const json_options &options)
        : logger_(std::move(logger)), token_(detail::process_token())
    {
        logger_->set_formatter(std::unique_ptr<spdlog::formatter>(new json_formatter(options, token_)));
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
    static const json_properties &empty_properties_()
    {
        static const json_properties EMPTY;
        return EMPTY;
    }

    /** @brief Appends the message as a quoted JSON string. The formatter adds the key. */
    static void append_message_(spdlog::memory_buf_t &out, const spdlog::memory_buf_t &msg)
    {
        detail::append_json_quoted(out, std::string_view{msg.data(), msg.size()});
    }

    /**
     * @brief Logs a payload: the token, the kind, the size of the fields, the bound and per-call fields and then what
     * write_message appends. spdlog runs the serialization while it formats the line, inside its error handling.
     *
     * The fields are an already escaped list, each entry starting with a comma. The message is appended as a quoted
     * JSON string, so it is escaped once however many sinks the logger has.
     */
    template <typename WriteMessage>
    void emit_(level lvl, const json_properties &props, char kind, const WriteMessage &write_message) const
    {
        const auto write = [&](spdlog::memory_buf_t &out) {
            detail::append(out, std::string_view{token_.data(), token_.size()});
            out.push_back(kind);
            const std::size_t size_at = out.size();
            detail::append(out, std::string_view{"00000000", detail::SIZE_DIGITS});
            const std::size_t fields_at = out.size();
            if (props.empty())
            {
                detail::append(out, bound_);
            }
            else
            {
                json_properties::append_merged_(out, properties_, props);
            }
            detail::patch_hex_size(out, size_at, out.size() - fields_at);
            write_message(out);
        };
        logger_->log(lvl, "{}", detail::deferred_payload<decltype(write)>{&write});
    }

    /** @brief The spdlog::logger shared by every copy. */
    std::shared_ptr<spdlog::logger> logger_;
    /** @brief The token that starts every payload, the same as in the formatter this logger installed. */
    detail::payload_token token_;
    /** @brief Fields bound by with_properties. */
    json_properties properties_;
    /** @brief The bound fields serialized once, ready to append to every line. */
    std::string bound_;
};

} // namespace jspdlog
