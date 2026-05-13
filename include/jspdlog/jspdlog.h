// SPDX-License-Identifier: MIT
//
// jspdlog - JSON-line logging on top of spdlog v2.
//
// Single-header. Drop-in. Wraps any spdlog::logger so that every log call
// emits exactly one valid JSON object per line. The JSON line pattern is set
// at construction time and cannot be overridden by the user, so jspdlog
// guarantees the output is always valid JSON.
//
// Quick start:
//
//   #include <spdlog/sinks/stdout_color_sinks.h>
//   #include <jspdlog/jspdlog.h>
//
//   jspdlog::json_logger logger(
//       "app",
//       std::make_shared<spdlog::sinks::stdout_color_sink_mt>()
//   );
//   logger.set_level(spdlog::level::trace);
//   logger.info(jspdlog::json_properties{"user_id", 42}, "hello {}", "world");
//
// Example output:
//   {"timestamp":"2026-05-13T08:00:00.000+02:00","logger":"app",
//    "level":"info","process":1234,"thread":5678,
//    "user_id":42,"message":"hello world"}

#pragma once

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>

// fmt is required transitively by spdlog v2 (it always uses external fmt).
// We include <fmt/format.h> directly because <spdlog/common.h> only pulls in
// the base/xchar parts of fmt, not the full string-returning `fmt::format`
// helpers we use below. All call sites route through `spdlog::fmt_lib`
// (defined in spdlog/common.h as a namespace alias for `fmt`) so the
// abstraction matches spdlog's own naming and adapts automatically if v2
// ever grows a std::format mode.
#include <fmt/format.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jspdlog
{

// ============================================================================
// detail: internal helpers (pattern constant + JSON string escaper).
// ============================================================================
namespace detail
{

// Append a JSON-quoted, escaped form of `s` to `out`. Handles the seven
// mandatory escapes plus \u00XX for other control bytes < 0x20. Bytes >= 0x20
// pass through unchanged; jspdlog treats input as opaque UTF-8 and does not
// re-encode it.
inline void append_json_quoted(std::string &out, std::string_view s)
{
    static constexpr char hex_digits[] = "0123456789abcdef";
    out.push_back('"');
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                out += "\\u00";
                out.push_back(hex_digits[(c >> 4) & 0xF]);
                out.push_back(hex_digits[c & 0xF]);
            }
            else
            {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

// Build the spdlog pattern that turns each log call into one JSON object.
// The logger name is escaped and baked in at construction time rather than
// interpolated via %n at format time, so names containing quotes, backslashes
// or control characters still produce a structurally valid JSON line. The
// trailing %v is where json_logger writes the property fragment (and the
// optional ,"message":"...") and the closing brace is part of the pattern
// itself, which is what guarantees the line is always valid JSON.
inline std::string make_json_pattern(std::string_view name)
{
    std::string out;
    out.reserve(120 + name.size());
    out += R"({"timestamp":"%Y-%m-%dT%H:%M:%S.%e%z","logger":)";
    append_json_quoted(out, name);
    out += R"(,"level":"%l","process":%P,"thread":%t%v})";
    return out;
}

} // namespace detail

// ============================================================================
// raw_json: a tagged wrapper for already-serialized JSON values.
// ============================================================================
//
// jspdlog itself has no JSON parser or serializer beyond the tiny escaper
// above. To insert JSON arrays, objects, or values produced by any JSON
// library, wrap the already-stringified form:
//
//   nlohmann::json arr = {1, 2, 3};
//   logger.info(jspdlog::json_properties{"items", jspdlog::raw_json{arr.dump()}}, "ok");
//
// The contents are inserted verbatim. jspdlog does no validation; the caller
// is responsible for ensuring `value` is valid JSON.
struct raw_json
{
    std::string value;
};

// ============================================================================
// json_properties: typed key/value pairs serialized eagerly into JSON text.
// ============================================================================
//
// Each value is converted to its JSON form at insert() time and stored as a
// std::string fragment. At log time, to_string() simply concatenates the
// fragments, which keeps the per-log-call hot path allocation-light.
class json_properties
{
public:
    json_properties() = default;

    template <typename T>
    json_properties(const std::string &key, const T &value)
    {
        insert(key, value);
    }

    template <typename T, typename... Args>
    json_properties(const std::string &key, const T &value, Args &&...args)
    {
        static_assert(sizeof...(args) % 2 == 0, "json_properties requires key/value pairs");
        insert(key, value);
        insert_pairs_(std::forward<Args>(args)...);
    }

    // --- Insert overloads -----------------------------------------------------
    // Each typed overload serializes the value into a small std::string. The
    // pointer template recurses through any depth of pointer-to-pointer.

    void insert(const std::string &key, std::nullptr_t)
    {
        members_[key] = "null";
    }

    void insert(const std::string &key, const char *value)
    {
        if (value != nullptr)
        {
            insert(key, std::string_view{value});
        }
        else
        {
            members_[key] = "null";
        }
    }

    void insert(const std::string &key, std::string_view value)
    {
        std::string encoded;
        encoded.reserve(value.size() + 2);
        detail::append_json_quoted(encoded, value);
        members_[key] = std::move(encoded);
    }

    void insert(const std::string &key, const std::string &value)
    {
        insert(key, std::string_view{value});
    }

    void insert(const std::string &key, bool value)
    {
        members_[key] = value ? "true" : "false";
    }

    // One template covers every integral type that isn't bool, including the
    // narrow ones (short, signed/unsigned char, int16_t, ...) and the wide
    // ones (long long, std::size_t, ...). bool is excluded so its dedicated
    // true/false overload still wins on overload resolution.
    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    void insert(const std::string &key, T value)
    {
        members_[key] = std::to_string(value);
    }

    // JSON has no NaN/Infinity tokens. Following the JavaScript JSON.stringify
    // convention, non-finite floats are serialized as `null`.
    void insert(const std::string &key, float value)
    {
        members_[key] = std::isfinite(value) ? spdlog::fmt_lib::format("{}", value) : "null";
    }
    void insert(const std::string &key, double value)
    {
        members_[key] = std::isfinite(value) ? spdlog::fmt_lib::format("{}", value) : "null";
    }

    // Empty raw_json content would produce ",\"key\":" followed by `,` or `}`
    // (invalid JSON). Fall back to `null` so the line stays parseable. The
    // caller is still responsible for the validity of non-empty content.
    void insert(const std::string &key, const raw_json &value)
    {
        members_[key] = value.value.empty() ? "null" : value.value;
    }
    void insert(const std::string &key, raw_json &&value)
    {
        members_[key] = value.value.empty() ? "null" : std::move(value.value);
    }

    // Pointer overload: null pointers become "null"; otherwise dereference
    // and dispatch to the matching typed overload. Recurses through
    // pointer-to-pointer chains of any depth.
    template <typename T>
    void insert(const std::string &key, const T *value)
    {
        if (value != nullptr)
        {
            insert(key, *value);
        }
        else
        {
            members_[key] = "null";
        }
    }

    // --- Merging --------------------------------------------------------------

    void merge(const json_properties &other)
    {
        for (const auto &[key, value] : other.members_)
        {
            members_.insert_or_assign(key, value);
        }
    }

    void merge(json_properties &&other)
    {
        // Take ownership of `other`'s storage first, then merge back anything
        // we already had under keys that don't collide. Duplicates keep
        // `other`'s value (which is now in `members_`), matching the
        // copy-merge semantics above.
        std::swap(members_, other.members_);
        members_.merge(other.members_);
    }

    // --- Inspection -----------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept
    {
        return members_.empty();
    }

    // Returns the serialized fragment ready to be appended after the spdlog
    // header. Always begins with a comma when non-empty, e.g.
    // `,"key1":1,"key2":"two"`.
    [[nodiscard]] std::string to_string() const
    {
        std::string out;
        for (const auto &[key, value] : members_)
        {
            out.push_back(',');
            detail::append_json_quoted(out, key);
            out.push_back(':');
            out += value;
        }
        return out;
    }

private:
    template <typename First, typename Second, typename... Rest>
    void insert_pairs_(const First &first, const Second &second, Rest &&...rest)
    {
        static_assert(std::is_convertible_v<First, std::string>, "key must be convertible to std::string");
        insert(first, second);
        if constexpr (sizeof...(rest) > 0)
        {
            insert_pairs_(std::forward<Rest>(rest)...);
        }
    }

    // Values are stored as already-serialized JSON fragments (no surrounding
    // key or commas). The map keeps keys sorted so that to_string() output
    // is deterministic for identical inputs, which makes regression tests
    // straightforward.
    std::map<std::string, std::string> members_;
};

// --- json_properties operators (composition) ---------------------------------
//
// The four corner cases of lvalue/rvalue lhs/rhs all collapse to "merge rhs
// into lhs", so rhs values always win on key collisions. By-value lhs lets
// the compiler reuse the caller's storage when lhs is an rvalue and is also
// unambiguous in overload resolution on MSVC (which previously needed a
// templated workaround).

inline json_properties operator+(json_properties lhs, const json_properties &rhs)
{
    lhs.merge(rhs);
    return lhs;
}

inline json_properties operator+(json_properties lhs, json_properties &&rhs)
{
    lhs.merge(std::move(rhs));
    return lhs;
}

// ============================================================================
// json_logger: thin wrapper around spdlog::logger that pins the JSON pattern.
// ============================================================================
//
// The user never gets to set the spdlog pattern. Every public constructor
// funnels through apply_pattern_() so the logger always emits valid JSON.
class json_logger
{
public:
    // --- Construction ---------------------------------------------------------
    //
    // The user supplies a name and any spdlog sink(s). jspdlog builds and
    // owns the spdlog::logger, applying the pinned JSON pattern.

    json_logger(std::string name, spdlog::sink_ptr sink)
        : logger_(std::make_shared<spdlog::logger>(std::move(name), std::move(sink)))
    {
        apply_pattern_();
    }

    json_logger(std::string name, spdlog::sinks_init_list sinks)
        : logger_(std::make_shared<spdlog::logger>(std::move(name), sinks))
    {
        apply_pattern_();
    }

    template <typename It>
    json_logger(std::string name, It sinks_begin, It sinks_end)
        : logger_(std::make_shared<spdlog::logger>(std::move(name), sinks_begin, sinks_end))
    {
        apply_pattern_();
    }

    // Escape hatch: take ownership of an existing spdlog::logger. The JSON
    // pattern is re-applied so the result is still guaranteed-valid JSON.
    [[nodiscard]] static json_logger adopt(std::shared_ptr<spdlog::logger> logger)
    {
        return json_logger(std::move(logger));
    }

    json_logger(const json_logger &) = default;
    json_logger(json_logger &&) noexcept = default;
    json_logger &operator=(const json_logger &) = default;
    json_logger &operator=(json_logger &&) noexcept = default;
    ~json_logger() = default;

    // --- Property binding -----------------------------------------------------

    // Returns a child logger that shares the underlying spdlog::logger (and
    // therefore its sinks, level, error handler), but carries an additional
    // set of bound properties. Existing keys are overridden by `props`.
    [[nodiscard]] json_logger with_properties(json_properties props) const
    {
        json_logger copy = *this;
        copy.properties_.merge(std::move(props));
        copy.cached_properties_ = copy.properties_.to_string();
        return copy;
    }

    // Forward spdlog runtime errors from this logger to another json_logger.
    // The forwarded message is logged at warn level and tagged with a
    // "source" property identifying which logger errored. Useful for
    // capturing "failed to write to sink X" etc. as structured records.
    void set_internal_logger(json_logger internal)
    {
        json_properties source_prop;
        source_prop.insert("source", logger_->name());
        internal.properties_.merge(std::move(source_prop));
        internal.cached_properties_ = internal.properties_.to_string();

        internal_logger_ = std::make_shared<json_logger>(std::move(internal));
        std::weak_ptr<json_logger> weak = internal_logger_;
        logger_->set_error_handler([weak](const std::string &msg) {
            if (auto p = weak.lock())
            {
                p->warn("{}", msg);
            }
        });
    }

    // --- Logging API ----------------------------------------------------------
    //
    // Every level (trace/debug/info/warn/error/critical) has the same four
    // overloads, all of which short-circuit the formatting work when the log
    // level is filtered out:
    //
    //   logger.info(fmt, args...);              // formatted message
    //   logger.info(value);                     // any fmt-formattable value
    //   logger.info(props, fmt, args...);       // properties + formatted message
    //   logger.info(props, value);              // properties + value
    //   logger.info(props);                     // properties only, no message
    //
    // The "properties only" path is reached because the non-template
    // `log_(level, const json_properties&)` overload below wins over the
    // template `log_<T>(level, const T&)` via overload resolution rules.

#define JSPDLOG_DEFINE_LEVEL_API(name, lvl)                                                                            \
    template <typename... Args>                                                                                        \
    void name(spdlog::format_string_t<Args...> fmt, Args &&...args)                                                    \
    {                                                                                                                  \
        log_(lvl, fmt, std::forward<Args>(args)...);                                                                   \
    }                                                                                                                  \
    template <typename T>                                                                                              \
    void name(const T &msg)                                                                                            \
    {                                                                                                                  \
        log_(lvl, msg);                                                                                                \
    }                                                                                                                  \
    template <typename... Args>                                                                                        \
    void name(const json_properties &props, spdlog::format_string_t<Args...> fmt, Args &&...args)                      \
    {                                                                                                                  \
        log_(lvl, props, fmt, std::forward<Args>(args)...);                                                            \
    }                                                                                                                  \
    template <typename T>                                                                                              \
    void name(const json_properties &props, const T &msg)                                                              \
    {                                                                                                                  \
        log_(lvl, props, msg);                                                                                         \
    }

    JSPDLOG_DEFINE_LEVEL_API(trace, spdlog::level::trace)
    JSPDLOG_DEFINE_LEVEL_API(debug, spdlog::level::debug)
    JSPDLOG_DEFINE_LEVEL_API(info, spdlog::level::info)
    JSPDLOG_DEFINE_LEVEL_API(warn, spdlog::level::warn)
    JSPDLOG_DEFINE_LEVEL_API(error, spdlog::level::err)
    JSPDLOG_DEFINE_LEVEL_API(critical, spdlog::level::critical)

#undef JSPDLOG_DEFINE_LEVEL_API

    // --- spdlog passthroughs --------------------------------------------------

    [[nodiscard]] const std::string &name() const
    {
        return logger_->name();
    }

    [[nodiscard]] spdlog::level level() const
    {
        return logger_->log_level();
    }

    void set_level(spdlog::level level)
    {
        logger_->set_level(level);
    }

    void flush()
    {
        logger_->flush();
    }

    void flush_on(spdlog::level level)
    {
        logger_->flush_on(level);
    }

    // Escape hatch for any spdlog::logger configuration we don't expose
    // directly (extra sinks, custom error handler, etc.). Do NOT call
    // set_pattern() on this; doing so will break the JSON output.
    [[nodiscard]] const std::shared_ptr<spdlog::logger> &spdlog_logger() const noexcept
    {
        return logger_;
    }

private:
    explicit json_logger(std::shared_ptr<spdlog::logger> logger) : logger_(std::move(logger))
    {
        apply_pattern_();
    }

    void apply_pattern_()
    {
        logger_->set_pattern(detail::make_json_pattern(logger_->name()));
    }

    // --- log_ dispatch --------------------------------------------------------

    // Properties-only path. Selected over the template log_<T> by overload
    // resolution when T == json_properties. Emits, e.g.,
    //   ...,"thread":1234,"k":"v"}
    // (the closing brace comes from the spdlog pattern).
    void log_(spdlog::level lvl, const json_properties &props)
    {
        if (!logger_->should_log(lvl))
        {
            return;
        }
        // Fast path for the common "no bound properties" case: avoid copying
        // properties_ and re-running the map merge just to produce the same
        // fragment.
        const std::string fragment = properties_.empty()
                                         ? props.to_string()
                                         : (properties_ + props).to_string();
        logger_->log(lvl, fragment);
    }

    template <typename... Args>
    void log_(spdlog::level lvl, spdlog::format_string_t<Args...> fmt, Args &&...args)
    {
        if (!logger_->should_log(lvl))
        {
            return;
        }
        log_message_(lvl, spdlog::fmt_lib::format(fmt, std::forward<Args>(args)...));
    }

    template <typename T>
    void log_(spdlog::level lvl, const T &msg)
    {
        if (!logger_->should_log(lvl))
        {
            return;
        }
        log_message_(lvl, spdlog::fmt_lib::format("{}", msg));
    }

    template <typename... Args>
    void log_(spdlog::level lvl, const json_properties &props, spdlog::format_string_t<Args...> fmt,
              Args &&...args)
    {
        if (!logger_->should_log(lvl))
        {
            return;
        }
        log_message_(lvl, props, spdlog::fmt_lib::format(fmt, std::forward<Args>(args)...));
    }

    template <typename T>
    void log_(spdlog::level lvl, const json_properties &props, const T &msg)
    {
        if (!logger_->should_log(lvl))
        {
            return;
        }
        log_message_(lvl, props, spdlog::fmt_lib::format("{}", msg));
    }

    // --- log_ leaf functions (build the JSON fragment and hand to spdlog) ----

    void log_message_(spdlog::level lvl, std::string msg)
    {
        std::string out;
        out.reserve(cached_properties_.size() + msg.size() + 16);
        out += cached_properties_;
        out += R"(,"message":)";
        detail::append_json_quoted(out, msg);
        logger_->log(lvl, out);
    }

    void log_message_(spdlog::level lvl, const json_properties &props, std::string msg)
    {
        // Same fast path as log_(level, props): if no properties are bound,
        // concatenating the per-call fragment is sufficient.
        std::string out = properties_.empty()
                              ? props.to_string()
                              : (properties_ + props).to_string();
        out.reserve(out.size() + msg.size() + 16);
        out += R"(,"message":)";
        detail::append_json_quoted(out, msg);
        logger_->log(lvl, out);
    }

    std::shared_ptr<spdlog::logger> logger_;
    json_properties properties_;
    std::string cached_properties_;
    std::shared_ptr<json_logger> internal_logger_;
};

} // namespace jspdlog
