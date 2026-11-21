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
 * The header fields are part of an spdlog pattern that ends with the message flag and the closing brace. jspdlog passes
 * spdlog an already escaped list of fields as the message, so every line written through jspdlog is a complete JSON
 * object. A message logged directly on the underlying spdlog::logger bypasses that escaping.
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
#define JSPDLOG_VERSION (JSPDLOG_VERSION_MAJOR * 10000 + JSPDLOG_VERSION_MINOR * 100 + JSPDLOG_VERSION_PATCH)

#include <spdlog/version.h>

#if SPDLOG_VER_MAJOR >= 2
#include <fmt/format.h>
/** @brief 1 when building against spdlog v2.x, 0 for v1.x. */
#define JSPDLOG_SPDLOG_V2 1
#else
#if SPDLOG_VERSION < 11200
#error "jspdlog requires spdlog >= 1.12 (or v2.x)"
#endif
#if defined(SPDLOG_USE_STD_FORMAT)
#error "jspdlog does not support SPDLOG_USE_STD_FORMAT yet; build spdlog with {fmt}"
#endif
#include <spdlog/fmt/fmt.h>
#define JSPDLOG_SPDLOG_V2 0
#endif

#include <spdlog/common.h>
#include <spdlog/details/os.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>

#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
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
using level = spdlog::level;
#else
using level = spdlog::level::level_enum;
#endif

/**
 * @brief Settings fixed when a json_logger is created.
 *
 * The key members control the header fields, which always appear in this order: timestamp, logger, level, process,
 * thread. A string renames the field and an empty optional leaves it out. Keys are escaped, so any text is safe. With
 * every field left out, a line holds only the fields and the message.
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
    static constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    const char *run = s.data(); // Start of the bytes not yet copied.
    const char *const end = s.data() + s.size();
    for (const char *p = run; p != end; ++p)
    {
        const auto c = static_cast<unsigned char>(*p);
        if (c >= 0x20 && c != '"' && c != '\\')
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
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
            break;
        }
    }
    out.append(run, end);
    out.push_back('"');
}

/**
 * @brief Appends text as a quoted JSON string that spdlog's pattern parser keeps literally.
 *
 * Percent signs are doubled so that a logger name or key containing them cannot turn into a pattern flag.
 */
inline void append_pattern_literal(std::string &out, std::string_view s)
{
    std::string quoted;
    append_json_quoted(quoted, s);
    for (char c : quoted)
    {
        out.push_back(c);
        if (c == '%')
        {
            out.push_back('%');
        }
    }
}

/**
 * @brief Builds the spdlog pattern for a logger: the header fields followed by the message flag and a closing brace.
 *
 * @return Whether the pattern has at least one header field, in which case the message part must start with a comma.
 */
inline bool make_json_pattern(std::string &out, std::string_view logger_name, const json_options &opts)
{
    out = "{";
    bool any = false;
    auto key = [&](const std::string &k) {
        if (any)
        {
            out.push_back(',');
        }
        append_pattern_literal(out, k);
        out.push_back(':');
        any = true;
    };
    if (opts.timestamp_key)
    {
        key(*opts.timestamp_key);
        // UTC ends in a literal Z because spdlog before 1.13 prints the local offset in UTC mode.
        out += opts.time_type == spdlog::pattern_time_type::utc ? R"("%Y-%m-%dT%H:%M:%S.%eZ")"
                                                                : R"("%Y-%m-%dT%H:%M:%S.%e%z")";
    }
    if (opts.logger_key)
    {
        key(*opts.logger_key);
        append_pattern_literal(out, logger_name);
    }
    if (opts.level_key)
    {
        key(*opts.level_key);
        out += R"("%l")";
    }
    if (opts.process_key)
    {
        key(*opts.process_key);
        out += "%P";
    }
    if (opts.thread_key)
    {
        key(*opts.thread_key);
        out += "%t";
    }
    out += "%v}";
    return any;
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

} // namespace detail

/**
 * @brief Typed key/value fields for a log line, serialized when they are inserted.
 *
 * Pass them per call, usually as a braced list such as {"user_id", 42, "ok", true}, or bind them to a child logger with
 * json_logger::with_properties. Values keep their JSON type, so numbers and booleans stay filterable as such. Keys are
 * written in alphabetical order. Inserting a key that exists replaces its value, and when two sets are merged the
 * right-hand side wins.
 *
 * A key equal to one of the logger's header keys or to message produces a duplicate key in the line. Avoiding that is
 * up to the caller.
 */
class json_properties
{
public:
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
    void insert(std::string, char16_t) = delete;
    void insert(std::string, char32_t) = delete;
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
        auto emit = [&out](const std::string &key, const std::string &value) {
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
 * Copies are cheap and share the underlying spdlog::logger, so its level, sinks and error handler apply to every copy.
 * Bound fields belong to each copy: with_properties returns a child that adds fields to every line it writes, which is
 * how inner code adds context without affecting its caller.
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
 */
class json_logger
{
public:
    /** @brief Creates a logger writing to one sink. Throws std::invalid_argument if the sink is null. */
    json_logger(std::string name, spdlog::sink_ptr sink, json_options options = {})
        : json_logger(make_logger_(std::move(name), std::move(sink)), options)
    {
    }

    /** @brief Creates a logger writing to several sinks. Throws std::invalid_argument if any sink is null. */
    json_logger(std::string name, spdlog::sinks_init_list sinks, json_options options = {})
        : json_logger(std::move(name), sinks.begin(), sinks.end(), std::move(options))
    {
    }

    /** @brief Creates a logger writing to a range of sinks. Throws std::invalid_argument if any sink is null. */
    template <
        typename It,
        std::enable_if_t<std::is_constructible_v<spdlog::sink_ptr, typename std::iterator_traits<It>::reference>, int> =
            0>
    json_logger(std::string name, It first, It last, json_options options = {})
        : json_logger(make_logger_(std::move(name), first, last), options)
    {
    }

    /**
     * @brief Wraps an existing spdlog::logger, for example an async logger.
     *
     * The logger's formatter is replaced by jspdlog's, which affects everyone using that spdlog::logger. Its sinks,
     * level and error handler stay as they are. Throws std::invalid_argument if the logger is null.
     */
    [[nodiscard]] static json_logger adopt(std::shared_ptr<spdlog::logger> logger, const json_options &options = {})
    {
        if (!logger)
        {
            throw std::invalid_argument("jspdlog: adopt() requires a non-null logger");
        }
        return json_logger(std::move(logger), options);
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
    void log(level lvl, const json_properties &props)
    {
        if (should_log(lvl))
        {
            emit_(lvl, props, nullptr);
        }
    }

    /** @brief Writes a formatted message. The format string is checked at compile time. */
    template <typename... Args>
    void log(level lvl, spdlog::format_string_t<Args...> fmt, Args &&...args)
    {
        log(lvl, empty_properties_(), fmt, std::forward<Args>(args)...);
    }

    /** @brief Writes a value formatted with {fmt} as the message, for example a runtime string. */
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>
    void log(level lvl, const T &msg)
    {
        log(lvl, empty_properties_(), msg);
    }

    /** @brief Writes fields and a formatted message. */
    template <typename... Args>
    void log(level lvl, const json_properties &props, spdlog::format_string_t<Args...> fmt, Args &&...args)
    {
        if (should_log(lvl))
        {
            spdlog::memory_buf_t msg;
            spdlog::fmt_lib::format_to(std::back_inserter(msg), fmt, std::forward<Args>(args)...);
            emit_(lvl, props, &msg);
        }
    }

    /** @brief Writes fields and a value formatted with {fmt} as the message. */
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>
    void log(level lvl, const json_properties &props, const T &msg)
    {
        if (should_log(lvl))
        {
            spdlog::memory_buf_t buf;
            spdlog::fmt_lib::format_to(std::back_inserter(buf), "{}", msg);
            emit_(lvl, props, &buf);
        }
    }

// Defines the five call forms for one level. Undefined right after use.
#define JSPDLOG_LEVEL_FUNCTIONS_(name, lvl)                                                                            \
    template <typename... Args>                                                                                        \
    void name(spdlog::format_string_t<Args...> fmt, Args &&...args)                                                    \
    {                                                                                                                  \
        log(lvl, fmt, std::forward<Args>(args)...);                                                                    \
    }                                                                                                                  \
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>                                   \
    void name(const T &msg)                                                                                            \
    {                                                                                                                  \
        log(lvl, msg);                                                                                                 \
    }                                                                                                                  \
    void name(const json_properties &props)                                                                            \
    {                                                                                                                  \
        log(lvl, props);                                                                                               \
    }                                                                                                                  \
    template <typename... Args>                                                                                        \
    void name(const json_properties &props, spdlog::format_string_t<Args...> fmt, Args &&...args)                      \
    {                                                                                                                  \
        log(lvl, props, fmt, std::forward<Args>(args)...);                                                             \
    }                                                                                                                  \
    template <typename T, std::enable_if_t<detail::is_loggable_value_v<T>, int> = 0>                                   \
    void name(const json_properties &props, const T &msg)                                                              \
    {                                                                                                                  \
        log(lvl, props, msg);                                                                                          \
    }

    JSPDLOG_LEVEL_FUNCTIONS_(trace, level::trace)
    JSPDLOG_LEVEL_FUNCTIONS_(debug, level::debug)
    JSPDLOG_LEVEL_FUNCTIONS_(info, level::info)
    JSPDLOG_LEVEL_FUNCTIONS_(warn, level::warn)
    JSPDLOG_LEVEL_FUNCTIONS_(error, level::err)
    JSPDLOG_LEVEL_FUNCTIONS_(critical, level::critical)
#undef JSPDLOG_LEVEL_FUNCTIONS_

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
     * Setting a pattern or formatter on it, or logging through it directly, produces lines that are not valid JSON.
     */
    [[nodiscard]] const std::shared_ptr<spdlog::logger> &spdlog_logger() const noexcept { return logger_; }

private:
    /** @brief Installs the JSON pattern on a logger that is known not to be null. */
    json_logger(std::shared_ptr<spdlog::logger> logger, const json_options &options)
        : logger_(std::move(logger))
    {
        std::string pattern;
        leading_comma_ = detail::make_json_pattern(pattern, logger_->name(), options);
        logger_->set_formatter(
            std::make_unique<spdlog::pattern_formatter>(
                std::move(pattern), options.time_type, options.eol.value_or(spdlog::details::os::default_eol)
            )
        );
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
        static const json_properties empty;
        return empty;
    }

    /**
     * @brief Serializes the bound and per-call fields and the message, and passes them to spdlog as the message part
     * of the pattern. A null msg writes no message key.
     */
    void emit_(level lvl, const json_properties &props, const spdlog::memory_buf_t *msg)
    {
        spdlog::memory_buf_t out;
        if (props.empty())
        {
            out.append(bound_.data(), bound_.data() + bound_.size());
        }
        else
        {
            json_properties::append_merged_(out, properties_, props);
        }
        if (msg != nullptr)
        {
            static constexpr std::string_view message_sep = R"(,"message":)";
            out.append(message_sep.data(), message_sep.data() + message_sep.size());
            detail::append_json_quoted(out, std::string_view{msg->data(), msg->size()});
        }
        const std::size_t skip = (!leading_comma_ && out.size() > 0) ? 1 : 0;
        logger_->log(lvl, spdlog::string_view_t{out.data() + skip, out.size() - skip});
    }

    /** @brief The spdlog::logger shared by every copy. */
    std::shared_ptr<spdlog::logger> logger_;
    /** @brief Whether the pattern has header fields, so the message part must start with a comma. */
    bool leading_comma_ = true;
    /** @brief Fields bound by with_properties. */
    json_properties properties_;
    /** @brief The bound fields serialized once, ready to append to every line. */
    std::string bound_;
};

} // namespace jspdlog
