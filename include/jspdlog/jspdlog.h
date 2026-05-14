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
// Example output (the timezone offset uses spdlog's %z flag and may render
// as either "+02:00" or "+0200" depending on the platform):
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

#include <atomic>
#include <cmath>
#include <functional>
#include <iterator>
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
//
// Two layers of escaping are needed for the logger name:
//   1. JSON-escape the bytes so the resulting field value is valid JSON.
//   2. Double every `%` so spdlog's pattern parser treats them as literals
//      rather than format-flag prefixes (otherwise a name like "50%off"
//      would inject whatever `%o` happens to expand to, breaking the
//      structurally-valid-JSON guarantee).
inline std::string make_json_pattern(std::string_view name)
{
    std::string quoted_name;
    quoted_name.reserve(name.size() + 2);
    append_json_quoted(quoted_name, name);

    std::string out;
    out.reserve(120 + quoted_name.size());
    out += R"({"timestamp":"%Y-%m-%dT%H:%M:%S.%e%z","logger":)";
    for (char c : quoted_name)
    {
        out.push_back(c);
        if (c == '%')
        {
            out.push_back('%');
        }
    }
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

    // The default copy/move ops are correct; we spell them out so the move
    // ops are visibly noexcept (which they are under any reasonable stdlib
    // implementation with std::allocator and std::less) for callers that
    // want to wrap json_properties in containers that care.
    json_properties(const json_properties &) = default;
    json_properties(json_properties &&) noexcept = default;
    json_properties &operator=(const json_properties &) = default;
    json_properties &operator=(json_properties &&) noexcept = default;
    ~json_properties() = default;

    // Single variadic constructor for both `{key, value}` and
    // `{key1, value1, key2, value2, ...}`. Replaces the historical pair of
    // (single-pair, variadic) overloads that relied on template partial-
    // ordering to disambiguate the two-argument call site; with the rules
    // collapsed there is no chance of an ambiguous selection on any
    // conformant compiler. Keys are anything implicitly convertible to
    // either `std::string` (string literals, `std::string`, ...) or
    // `std::string_view`; the latter is converted with an explicit
    // `std::string{view}` step so callers can pass a `string_view` here
    // without first wrapping it themselves.
    template <typename K, typename V, typename... Rest>
    json_properties(K &&key, V &&value, Rest &&...rest)
    {
        static_assert(sizeof...(rest) % 2 == 0, "json_properties requires key/value pairs");
        insert(make_key_(std::forward<K>(key)), std::forward<V>(value));
        if constexpr (sizeof...(rest) > 0)
        {
            insert_pairs_(std::forward<Rest>(rest)...);
        }
    }

    // --- Insert overloads -----------------------------------------------------
    // Each typed overload serializes the value into a small std::string. The
    // pointer template recurses through any depth of pointer-to-pointer.
    //
    // The key is taken by value so the temporary `std::string` that callers
    // typically pass (constructed implicitly from a string literal) can be
    // moved straight into `members_` instead of being copied through a const
    // reference.

    void insert(std::string key, std::nullptr_t) { members_.insert_or_assign(std::move(key), "null"); }

    void insert(std::string key, const char *value)
    {
        if (value != nullptr)
        {
            insert(std::move(key), std::string_view{value});
        }
        else
        {
            members_.insert_or_assign(std::move(key), "null");
        }
    }

    void insert(std::string key, std::string_view value)
    {
        std::string encoded;
        encoded.reserve(value.size() + 2);
        detail::append_json_quoted(encoded, value);
        members_.insert_or_assign(std::move(key), std::move(encoded));
    }

    void insert(std::string key, const std::string &value) { insert(std::move(key), std::string_view{value}); }

    void insert(std::string key, bool value)
    {
        members_.insert_or_assign(std::move(key), value ? "true" : "false");
    }

    // Plain `char` is treated as a single-character JSON string rather than
    // a small integer, which is what users almost always want. signed char
    // and unsigned char keep the integer behavior because they're the
    // canonical 8-bit integer types (int8_t, uint8_t).
    void insert(std::string key, char value) { insert(std::move(key), std::string_view{&value, 1}); }

    // Wide character types deliberately don't compile: encoding a single
    // wchar_t/char16_t/char32_t to UTF-8 requires a unicode encoder, which
    // jspdlog intentionally doesn't bundle. Callers should convert to a
    // UTF-8 std::string themselves and pass that.
    void insert(std::string, wchar_t) = delete;
    void insert(std::string, char16_t) = delete;
    void insert(std::string, char32_t) = delete;

    // One template covers every remaining integral type, including the narrow
    // ones (short, signed/unsigned char, int16_t, ...) and the wide ones
    // (long long, std::size_t, ...). bool and char are excluded so their
    // dedicated overloads win on overload resolution.
    template <
        typename T,
        std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>, int> = 0>
    void insert(std::string key, T value)
    {
        members_.insert_or_assign(std::move(key), std::to_string(value));
    }

    // JSON has no NaN/Infinity tokens. Following the JavaScript JSON.stringify
    // convention, non-finite floats are serialized as `null`.
    //
    // For finite values we route through `format_finite_float_` so that integer-
    // valued floats like `1.0` still serialize with a trailing `.0`. fmt's
    // shortest-round-trip default would otherwise emit them as `1`, which most
    // JSON consumers then parse as an integer -- giving the producer's choice
    // of `float`/`double` no observable effect downstream. Forcing the decimal
    // point keeps the JSON type stable across the range of float values.
    void insert(std::string key, float value)
    {
        members_.insert_or_assign(std::move(key), std::isfinite(value) ? format_finite_float_(value) : "null");
    }
    void insert(std::string key, double value)
    {
        members_.insert_or_assign(std::move(key), std::isfinite(value) ? format_finite_float_(value) : "null");
    }

    // Empty raw_json content would produce ",\"key\":" followed by `,` or `}`
    // (invalid JSON). Fall back to `null` so the line stays parseable. The
    // caller is still responsible for the validity of non-empty content.
    void insert(std::string key, const raw_json &value)
    {
        members_.insert_or_assign(std::move(key), value.value.empty() ? std::string{"null"} : value.value);
    }
    void insert(std::string key, raw_json &&value)
    {
        members_.insert_or_assign(
            std::move(key), value.value.empty() ? std::string{"null"} : std::move(value.value)
        );
    }

    // Pointer overload: null pointers become "null"; otherwise dereference
    // and dispatch to the matching typed overload. Recurses through
    // pointer-to-pointer chains of any depth.
    template <typename T>
    void insert(std::string key, const T *value)
    {
        if (value != nullptr)
        {
            insert(std::move(key), *value);
        }
        else
        {
            members_.insert_or_assign(std::move(key), "null");
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
        // The swap puts `other`'s incoming entries into `members_` and leaves
        // our previous entries in `other.members_` -- i.e. `members_` now
        // holds the side that should win on collisions. We then merge our
        // previous entries back in: std::map::merge leaves colliding entries
        // behind in the source, so any of "our" keys that already exist in
        // `members_` (where `other`'s value lives) stay in `other.members_`
        // and are quietly dropped, which is exactly the rhs-wins semantics.
        // Finally we clear the leftovers so the moved-from object is left in
        // the conventional empty-but-valid state.
        std::swap(members_, other.members_);
        members_.merge(other.members_);
        other.members_.clear();
    }

    // --- Inspection -----------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return members_.empty(); }

    // Returns the serialized fragment ready to be appended after the spdlog
    // header. Always begins with a comma when non-empty, e.g.
    // `,"key1":1,"key2":"two"`.
    [[nodiscard]] std::string to_string() const
    {
        // Pre-size: each entry contributes at least key.size() + value.size()
        // + 4 chars (the leading comma, the two quotes around the key, and
        // the colon -- i.e. `,"":`). Keys that need JSON-escaping push the
        // actual size higher, but for the common ASCII-only case this avoids
        // re-allocating across the loop.
        std::size_t expected = 0;
        for (const auto &[key, value] : members_)
        {
            expected += key.size() + value.size() + 4;
        }
        std::string out;
        out.reserve(expected);
        for (const auto &[key, value] : members_)
        {
            append_entry_(out, key, value);
        }
        return out;
    }

    // Hot-path helper: serialize the merge of `*this` and `rhs` directly into
    // `out`, with rhs winning on key collisions, without materializing an
    // intermediate `json_properties`. Walks the two already-sorted maps in
    // lockstep, so the cost is one linear pass over each side rather than a
    // full map copy plus a second pass to serialize. Used by `json_logger`
    // when both bound and per-call properties are present; exposed publicly
    // so it can be unit-tested independently of the logger.
    void append_merged_to(std::string &out, const json_properties &rhs) const
    {
        // Same +4 reasoning as `to_string()`: `,"":` per entry, exact for
        // ASCII keys that don't need escaping. We over-reserve slightly when
        // the two sides have colliding keys (the rhs entry is emitted once,
        // not twice) which is harmless.
        std::size_t expected = 0;
        for (const auto &[key, value] : members_)
        {
            expected += key.size() + value.size() + 4;
        }
        for (const auto &[key, value] : rhs.members_)
        {
            expected += key.size() + value.size() + 4;
        }
        out.reserve(out.size() + expected);

        auto lit = members_.begin();
        const auto lend = members_.end();
        auto rit = rhs.members_.begin();
        const auto rend = rhs.members_.end();
        while (lit != lend && rit != rend)
        {
            if (lit->first < rit->first)
            {
                append_entry_(out, lit->first, lit->second);
                ++lit;
            }
            else if (rit->first < lit->first)
            {
                append_entry_(out, rit->first, rit->second);
                ++rit;
            }
            else
            {
                append_entry_(out, rit->first, rit->second);
                ++lit;
                ++rit;
            }
        }
        for (; lit != lend; ++lit)
        {
            append_entry_(out, lit->first, lit->second);
        }
        for (; rit != rend; ++rit)
        {
            append_entry_(out, rit->first, rit->second);
        }
    }

private:
    template <typename First, typename Second, typename... Rest>
    void insert_pairs_(First &&first, Second &&second, Rest &&...rest)
    {
        insert(make_key_(std::forward<First>(first)), std::forward<Second>(second));
        if constexpr (sizeof...(rest) > 0)
        {
            insert_pairs_(std::forward<Rest>(rest)...);
        }
    }

    // Converts any string-like key to `std::string`. Accepts both implicit
    // `std::string` conversions (string literals, `std::string`) and
    // `std::string_view` conversions (which are not implicit to `std::string`
    // and would otherwise hit the `insert` overload set as an
    // unconvertible-key error). Decays the input so a forwarding reference
    // such as `const char (&)[N]` is checked as `const char *`.
    template <typename K>
    static std::string make_key_(K &&key)
    {
        using DK = std::decay_t<K>;
        if constexpr (std::is_convertible_v<DK, std::string>)
        {
            return std::string(std::forward<K>(key));
        }
        else
        {
            static_assert(
                std::is_convertible_v<DK, std::string_view>,
                "json_properties key must be convertible to std::string or std::string_view"
            );
            return std::string{std::string_view{std::forward<K>(key)}};
        }
    }

    // Forces a trailing decimal point on integer-valued floats so JSON
    // consumers see a stable number type for the field. fmt's `{}` format
    // emits the shortest round-trip representation, so `1.0` would otherwise
    // serialize as `1`, which most parsers then read back as an integer.
    template <typename Float>
    static std::string format_finite_float_(Float value)
    {
        std::string s = spdlog::fmt_lib::format("{}", value);
        // If fmt already emitted a fractional or exponent marker the value is
        // already unambiguously a float; otherwise append `.0` to make it so.
        const bool has_float_marker = s.find_first_of(".eE") != std::string::npos;
        if (!has_float_marker)
        {
            s += ".0";
        }
        return s;
    }

    static void append_entry_(std::string &out, const std::string &key, const std::string &value)
    {
        out.push_back(',');
        detail::append_json_quoted(out, key);
        out.push_back(':');
        out += value;
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

    // Constrains the iterator template so a stray call with an iterator over
    // some unrelated type fails at the constructor signature with a clear
    // message, rather than producing a confusing instantiation error inside
    // spdlog's own constructor body.
    template <
        typename It,
        std::enable_if_t<
            std::is_constructible_v<spdlog::sink_ptr, typename std::iterator_traits<It>::reference>,
            int> = 0>
    json_logger(std::string name, It sinks_begin, It sinks_end)
        : logger_(std::make_shared<spdlog::logger>(std::move(name), sinks_begin, sinks_end))
    {
        apply_pattern_();
    }

    // Wrap an already-built spdlog::logger (e.g. one configured elsewhere
    // with a custom error handler, registered in the spdlog registry, or
    // assembled by another framework). The JSON pattern is re-applied so the
    // result is still guaranteed-valid JSON, regardless of whatever pattern
    // was previously set.
    //
    // The optional `time_type` lets callers preserve the adopted logger's
    // timestamp mode through pattern re-application: spdlog implicitly resets
    // the pattern-time mode to `local` on every `set_pattern()` call, so an
    // adopted logger configured for UTC would otherwise silently switch to
    // local. Passing `spdlog::pattern_time_type::utc` here is equivalent to
    // calling `set_pattern_time(utc)` on the returned logger immediately.
    [[nodiscard]] static json_logger adopt(
        std::shared_ptr<spdlog::logger> logger,
        spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local
    )
    {
        return json_logger(std::move(logger), time_type);
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
    //
    // Sharing the spdlog::logger means *bound properties are isolated per
    // child*, but configuration mutations are not: calling set_level(),
    // flush(), or flush_on() on the child reaches through to the same
    // underlying spdlog::logger as the parent and so affects every
    // json_logger derived from the same root. If you need truly independent
    // configuration, construct a new json_logger.
    //
    // Two ref-qualified overloads: the lvalue version copies *this so the
    // caller's logger is unchanged; the rvalue version mutates *this in
    // place and returns it by move, which keeps chained construction like
    // `make_logger().with_properties(...).with_properties(...)` allocation-
    // light (no second copy of `properties_`).
    [[nodiscard]] json_logger with_properties(json_properties props) const &
    {
        json_logger copy = *this;
        copy.properties_.merge(std::move(props));
        copy.cached_properties_ = copy.properties_.to_string();
        return copy;
    }

    [[nodiscard]] json_logger with_properties(json_properties props) &&
    {
        properties_.merge(std::move(props));
        cached_properties_ = properties_.to_string();
        return std::move(*this);
    }

    // --- Error handling -------------------------------------------------------
    //
    // Two public entry points are intentionally enough:
    //   * `silence_errors()` drops spdlog runtime errors entirely.
    //   * the free function `forward_errors_to(source, destination)` (below)
    //     surfaces them as JSON warn lines on another logger.
    // Lower-level installation of a raw callback is intentionally not part
    // of the public API; callers that need it can reach through
    // `spdlog_logger()->set_error_handler(...)` directly.

    // Replace the spdlog runtime-error handler with a no-op. Subsequent
    // runtime errors from sinks (e.g. a disk-full exception caught by
    // spdlog) are dropped silently. spdlog v2 no-ops when the handler slot
    // is empty rather than reinstating the built-in stderr writer, so this
    // really is a permanent silence until `forward_errors_to()` (or an
    // escape-hatch `spdlog_logger()->set_error_handler(...)` call) replaces
    // the handler again.
    void silence_errors() { logger_->set_error_handler({}); }

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

    [[nodiscard]] const std::string &name() const noexcept { return logger_->name(); }

    // Named to match spdlog::logger::log_level() (and to avoid shadowing the
    // unqualified `spdlog::level` type inside the class body).
    [[nodiscard]] spdlog::level log_level() const noexcept { return logger_->log_level(); }

    void set_level(spdlog::level lvl) { logger_->set_level(lvl); }

    void flush() { logger_->flush(); }

    void flush_on(spdlog::level lvl) { logger_->flush_on(lvl); }

    // Switch between local and UTC timestamps without losing the pinned
    // JSON pattern. Internally this re-applies the same JSON pattern that
    // the constructor installs, but with the requested pattern_time_type;
    // spdlog otherwise resets the time mode to local on every set_pattern()
    // call, which is why exposing this separately from set_level() / flush()
    // is necessary. The common use case is right after adopt(), to restore
    // a UTC mode that the adopted logger originally had.
    void set_pattern_time(spdlog::pattern_time_type time_type)
    {
        logger_->set_pattern(detail::make_json_pattern(logger_->name()), time_type);
    }

    // Escape hatch for any spdlog::logger configuration we don't expose
    // directly (extra sinks, custom error handler, etc.). The "structurally
    // impossible to emit invalid JSON" guarantee assumes nobody calls
    // set_pattern() on the returned logger -- doing so will break it.
    [[nodiscard]] const std::shared_ptr<spdlog::logger> &spdlog_logger() const noexcept { return logger_; }

private:
    // Used by `adopt()`. The time_type defaults to `local` so the public
    // sink/sinks_init_list/iterator constructors -- which delegate to
    // `apply_pattern_()` with no argument -- stay on spdlog's default time
    // mode, while `adopt()` can opt into UTC up front.
    explicit json_logger(
        std::shared_ptr<spdlog::logger> logger,
        spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local
    )
        : logger_(std::move(logger))
    {
        apply_pattern_(time_type);
    }

    // Installs the pinned JSON pattern on the underlying spdlog logger.
    // Note: spdlog's `set_pattern()` implicitly resets the pattern-time mode
    // to whatever is passed (defaulting to local), which is why both
    // `adopt(logger, time_type)` and `set_pattern_time()` thread an explicit
    // mode through this function -- without that, a caller switching to UTC
    // would silently revert on the next pattern reapplication.
    void apply_pattern_(spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local)
    {
        logger_->set_pattern(detail::make_json_pattern(logger_->name()), time_type);
    }

    // Install a handler for spdlog runtime errors. Pass an empty
    // std::function to clear the handler entirely; once cleared, subsequent
    // runtime errors are dropped silently (spdlog v2 no-ops when the handler
    // slot is empty rather than reinstating the built-in stderr writer).
    // The handler runs on the thread that triggered the error and must be
    // safe to call concurrently if the logger is shared across threads.
    //
    // This is the implementation primitive behind `silence_errors()` and the
    // free function `forward_errors_to()`; it's intentionally private so
    // those two named helpers remain the only public way to configure error
    // handling. Code that genuinely needs a raw callback can still install
    // one through `spdlog_logger()->set_error_handler(...)`.
    void set_error_handler_(std::function<void(std::string_view)> handler)
    {
        if (!handler)
        {
            logger_->set_error_handler({});
            return;
        }
        logger_->set_error_handler([h = std::move(handler)](const std::string &msg) { h(msg); });
    }

    friend void forward_errors_to(json_logger &source, json_logger destination);

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
        // Fast paths: avoid copying maps when we already have a precomputed
        // fragment (cached_properties_) or when one side is empty. Only the
        // both-non-empty branch needs the actual merge to honor "rhs wins"
        // semantics on key collisions; that branch routes through
        // json_properties::append_merged_to so we never materialize an
        // intermediate map -- it emits the merged JSON fragment straight
        // into `fragment_storage`. The `fragment_storage` / `fragment`
        // split mirrors log_message_(level, props, msg) -- on the
        // "bound-properties only" path we can hand cached_properties_ to
        // spdlog as a view rather than copying it into a fresh std::string.
        std::string fragment_storage;
        std::string_view fragment;
        if (props.empty())
        {
            fragment = cached_properties_;
        }
        else if (properties_.empty())
        {
            fragment_storage = props.to_string();
            fragment = fragment_storage;
        }
        else
        {
            properties_.append_merged_to(fragment_storage, props);
            fragment = fragment_storage;
        }
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
    void log_(spdlog::level lvl, const json_properties &props, spdlog::format_string_t<Args...> fmt, Args &&...args)
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
        static constexpr std::string_view message_sep = R"(,"message":)";
        std::string out;
        out.reserve(cached_properties_.size() + message_sep.size() + msg.size() + 2);
        out += cached_properties_;
        out += message_sep;
        detail::append_json_quoted(out, msg);
        logger_->log(lvl, out);
    }

    void log_message_(spdlog::level lvl, const json_properties &props, std::string msg)
    {
        // Mirrors the fast paths in log_(level, props): the merge-and-
        // serialize is only needed when both sides carry properties, and
        // even then we route through append_merged_to so no intermediate
        // json_properties is materialized. We build the fragment first,
        // then reserve once before appending the ",\"message\":..." tail.
        static constexpr std::string_view message_sep = R"(,"message":)";

        std::string fragment_storage;
        std::string_view fragment;
        if (props.empty())
        {
            fragment = cached_properties_;
        }
        else if (properties_.empty())
        {
            fragment_storage = props.to_string();
            fragment = fragment_storage;
        }
        else
        {
            properties_.append_merged_to(fragment_storage, props);
            fragment = fragment_storage;
        }

        std::string out;
        out.reserve(fragment.size() + message_sep.size() + msg.size() + 2);
        out.append(fragment.data(), fragment.size());
        out += message_sep;
        detail::append_json_quoted(out, msg);
        logger_->log(lvl, out);
    }

    std::shared_ptr<spdlog::logger> logger_;
    json_properties properties_;
    std::string cached_properties_;
};

// ============================================================================
// forward_errors_to: turn spdlog runtime errors into structured warn lines on
// another json_logger. The primary public way to react to sink failures.
// ============================================================================
//
// Captures `source`'s name and a copy of `destination` at call time. The copy
// is intentional: subsequent with_properties() calls on the original
// destination do not affect the forwarder. Errors emitted by `source` after
// this call surface as
//   destination.warn(json_properties{"source", source.name()}, "{}", msg)
//
// Exceptions thrown from the destination's own logging path are swallowed.
// An error handler that throws would re-enter spdlog's error machinery and
// risk infinite recursion (or, depending on the sink, deadlocking against
// the destination's own mutex). Silently dropping the secondary failure is
// the conservative choice when the user has already opted into "best-effort
// error reporting".
//
// A second protection covers the case where `destination` shares a sink with
// `source` (or actually is `source`): if writing the forwarded warn line
// triggers another sink failure, spdlog will catch that exception and re-
// invoke this same error handler. The shared `in_handler` flag breaks the
// recursion at the second entry without losing the first message. The flag
// is per-handler (not global), so independent forwarder chains don't
// interfere with each other.
inline void forward_errors_to(json_logger &source, json_logger destination)
{
    auto name = source.name();
    auto in_handler = std::make_shared<std::atomic<bool>>(false);
    source.set_error_handler_([name = std::move(name),
                               dest = std::move(destination),
                               in_handler = std::move(in_handler)](std::string_view msg) mutable {
        bool expected = false;
        if (!in_handler->compare_exchange_strong(expected, true))
        {
            return;
        }
        try
        {
            dest.warn(json_properties{"source", name}, "{}", msg);
        }
        catch (...)
        {
            // Intentionally swallowed; see comment above.
        }
        in_handler->store(false);
    });
}

} // namespace jspdlog
