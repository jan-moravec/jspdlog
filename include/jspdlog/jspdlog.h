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
#include <cassert>
#include <cmath>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jspdlog
{

// ============================================================================
// json_pattern_options: choose which fixed header fields to emit and rename
// them.
// ============================================================================
//
// Each std::optional in this struct controls one entry of the JSON header
// emitted by every json_logger:
//   * a string value -- include the field with that key name;
//   * std::nullopt   -- omit the field entirely.
//
// The defaults reproduce the pre-customization output exactly:
//   {"timestamp":"...","logger":"...","level":"...","process":...,"thread":...}
// so call sites that don't pass options are unaffected. Field order is fixed
// (timestamp, logger, level, process, thread) regardless of which subset is
// selected.
//
// Only the *keys* are configurable; the value formats are not -- timestamps
// stay ISO-8601 (with the spdlog `%z` offset that may render as "+02:00" or
// "+0200" depending on platform), levels stay spdlog's level name strings,
// and process/thread stay numeric. Keys are JSON-escaped and spdlog-pattern-
// escaped exactly like the logger name, so any byte (including quotes,
// backslashes, control chars and `%`) is safe.
//
// Omitting *every* fixed field produces `{<properties-or-message>}` lines, or
// literally `{}` for an empty properties-only call -- still valid JSON.
//
// Caveats / gotchas (not enforced; jspdlog passes keys through verbatim):
//   * An empty key (`opts.timestamp = ""`) produces `{"":"..."}`. Technically
//     valid JSON, almost certainly a typo.
//   * Two fields renamed to the same key produce `{"x":"...","x":"..."}`.
//     RFC 8259 permits duplicate keys but most consumers pick one
//     arbitrarily, so this is rarely what you want.
//
// Example:
//   jspdlog::json_pattern_options opts;
//   opts.timestamp = "ts";          // rename
//   opts.process = std::nullopt;    // omit
//   jspdlog::json_logger logger("app", sink, opts);
struct json_pattern_options
{
    std::optional<std::string> timestamp = std::string{"timestamp"};
    std::optional<std::string> logger = std::string{"logger"};
    std::optional<std::string> level = std::string{"level"};
    std::optional<std::string> process = std::string{"process"};
    std::optional<std::string> thread = std::string{"thread"};
};

// ============================================================================
// detail: internal helpers (pattern builder + JSON string escaper).
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

// Append `s` to `out` so that the result is safe to embed as a literal piece
// of JSON inside an spdlog pattern. Two layers of escaping are needed:
//   1. JSON-escape the bytes so the resulting field is valid JSON.
//   2. Double every `%` so spdlog's pattern parser treats them as literals
//      rather than format-flag prefixes (otherwise a literal like "50%off"
//      would expand whatever `%o` produces, or worse a `%v` would splice
//      the message into the wrong place and break the
//      structurally-valid-JSON guarantee).
// Used for the logger name (a JSON value) and for every json_pattern_options
// key (a JSON key); both have the same "user-supplied literal goes through
// spdlog's pattern parser" risk profile.
inline void append_pattern_safe_quoted(std::string &out, std::string_view s)
{
    std::string quoted;
    quoted.reserve(s.size() + 2);
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

// Result of building the pinned spdlog pattern. `fragment_needs_leading_comma`
// is true when the pattern emits at least one fixed field; in that case the
// trailing `%v` fragment is preceded by the last field's value and so must
// begin with a comma. When no fixed fields are emitted (pattern is `{%v}`)
// the fragment must NOT begin with a comma -- json_logger uses this flag to
// strip the leading comma it would otherwise produce.
struct json_pattern_build
{
    std::string pattern;
    bool fragment_needs_leading_comma;
};

// Build the spdlog pattern that turns each log call into one JSON object.
// The logger name is escaped and baked in at construction time rather than
// interpolated via %n at format time, so names containing quotes, backslashes
// or control characters still produce a structurally valid JSON line. The
// trailing `%v` is where json_logger writes the property fragment (and the
// optional ,"message":"...") and the closing brace is part of the pattern
// itself, which is what guarantees the line is always valid JSON.
inline json_pattern_build make_json_pattern(std::string_view name, const json_pattern_options &opts)
{
    std::string out;
    out.reserve(160 + name.size());
    out.push_back('{');

    bool any_field = false;
    auto emit_separator = [&]() {
        if (any_field)
        {
            out.push_back(',');
        }
    };

    if (opts.timestamp.has_value())
    {
        emit_separator();
        append_pattern_safe_quoted(out, *opts.timestamp);
        out += R"(:"%Y-%m-%dT%H:%M:%S.%e%z")";
        any_field = true;
    }
    if (opts.logger.has_value())
    {
        emit_separator();
        append_pattern_safe_quoted(out, *opts.logger);
        out.push_back(':');
        append_pattern_safe_quoted(out, name);
        any_field = true;
    }
    if (opts.level.has_value())
    {
        emit_separator();
        append_pattern_safe_quoted(out, *opts.level);
        out += R"(:"%l")";
        any_field = true;
    }
    if (opts.process.has_value())
    {
        emit_separator();
        append_pattern_safe_quoted(out, *opts.process);
        out += ":%P";
        any_field = true;
    }
    if (opts.thread.has_value())
    {
        emit_separator();
        append_pattern_safe_quoted(out, *opts.thread);
        out += ":%t";
        any_field = true;
    }
    out += "%v}";
    return {std::move(out), /*fragment_needs_leading_comma=*/any_field};
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
    //
    // Note that `T = char` is *not* covered here -- the non-template
    // `insert(std::string, const char *)` above wins on overload resolution
    // for `char *` / `const char *`, so `insert("k", &my_char)` follows the C
    // convention of "pointer-to-char is a NUL-terminated string starting
    // here" and reads bytes until the first NUL. That can be UB if the byte
    // at `&my_char` is not actually followed by a NUL; pass `my_char`
    // (without `&`) if you want the single-character-string semantics, or
    // `static_cast<int>(my_char)` if you want the integer.
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

    json_logger(std::string name, spdlog::sink_ptr sink, json_pattern_options options = {})
        : logger_(std::make_shared<spdlog::logger>(std::move(name), std::move(sink)))
        , pattern_options_(std::move(options))
    {
        apply_pattern_();
    }

    json_logger(std::string name, spdlog::sinks_init_list sinks, json_pattern_options options = {})
        : logger_(std::make_shared<spdlog::logger>(std::move(name), sinks))
        , pattern_options_(std::move(options))
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
    json_logger(std::string name, It sinks_begin, It sinks_end, json_pattern_options options = {})
        : logger_(std::make_shared<spdlog::logger>(std::move(name), sinks_begin, sinks_end))
        , pattern_options_(std::move(options))
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
    // the pattern-time mode to whatever is passed to `set_pattern()` (default
    // `local`), so an adopted logger configured for UTC would otherwise
    // silently switch to local. Passing `spdlog::pattern_time_type::utc` here
    // is equivalent to calling `set_pattern_time(utc)` on the returned logger
    // immediately, except that the value is also persisted on the json_logger
    // so any future internal pattern reapplication keeps it.
    [[nodiscard]] static json_logger adopt(
        std::shared_ptr<spdlog::logger> logger,
        spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local
    )
    {
        return json_logger(std::move(logger), json_pattern_options{}, time_type);
    }

    // Same as the two-argument adopt(), but also threads a json_pattern_options
    // through. Spelled as a separate overload (rather than an extra default
    // argument) so existing call sites like `adopt(logger, utc)` continue to
    // resolve unambiguously, and so callers passing only options don't have
    // to spell out the pattern_time_type.
    [[nodiscard]] static json_logger adopt(
        std::shared_ptr<spdlog::logger> logger,
        json_pattern_options options,
        spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local
    )
    {
        return json_logger(std::move(logger), std::move(options), time_type);
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
    //   * `silence_errors()`         drops spdlog runtime errors entirely.
    //   * `forward_errors_to(dest)`  surfaces them as JSON warn lines on
    //                                another logger.
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

    // Forward spdlog runtime errors emitted by *this to `destination` as
    // structured JSON warn lines tagged with this logger's name. The errors
    // surface as
    //   destination.warn(json_properties{"source", this->name()}, "{}", msg)
    //
    // `destination` is taken by value and captured into the handler, so
    // subsequent `with_properties()` calls on the original `destination` do
    // not affect the forwarder.
    //
    // Exceptions thrown from `destination`'s own logging path are swallowed.
    // An error handler that throws would re-enter spdlog's error machinery
    // and risk infinite recursion (or, depending on the sink, deadlocking
    // against a sink mutex). Silently dropping the secondary failure is the
    // conservative choice when the user has already opted into "best-effort
    // error reporting".
    //
    // A second protection covers cyclic forwarder chains: if writing the
    // forwarded warn line itself triggers a sink failure on a logger that
    // forwards back into *this, the per-handler `in_handler` flag breaks the
    // recursion at the second entry without losing the first message. The
    // flag is per-installation (not global), so independent forwarder chains
    // don't interfere with each other.
    //
    // Precondition: `destination`'s underlying `spdlog::logger` must NOT be
    // the same object as this logger's. If it is, the forwarder would call
    // dest.warn() from inside spdlog's err_helper while that helper holds
    // a non-recursive mutex, deadlocking on the second entry. asserted in
    // debug builds; in release builds the deadlock would manifest as a hang
    // on the first sink failure. Sharing only sinks (with distinct
    // spdlog::loggers) is fine.
    void forward_errors_to(json_logger destination)
    {
        assert(
            logger_.get() != destination.logger_.get() &&
            "forward_errors_to: source and destination must not share a spdlog::logger "
            "(same shared_ptr would deadlock on spdlog's err_helper mutex)"
        );

        auto name = this->name();
        auto in_handler = std::make_shared<std::atomic<bool>>(false);
        set_error_handler_(
            [name = std::move(name),
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
            }
        );
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

    [[nodiscard]] const std::string &name() const noexcept { return logger_->name(); }

    // Named to match spdlog::logger::log_level() (and to avoid shadowing the
    // unqualified `spdlog::level` type inside the class body).
    [[nodiscard]] spdlog::level log_level() const noexcept { return logger_->log_level(); }

    void set_level(spdlog::level lvl) { logger_->set_level(lvl); }

    void flush() { logger_->flush(); }

    void flush_on(spdlog::level lvl) { logger_->flush_on(lvl); }

    // Switch between local and UTC timestamps without losing the pinned
    // JSON pattern. The mode is persisted on this json_logger, so any
    // subsequent internal pattern reapplication (today only `set_pattern_time`
    // itself triggers one, but that may grow) keeps it. spdlog's
    // `set_pattern()` otherwise resets the time mode to whatever you pass it
    // (defaulting to local), which is why exposing this separately from
    // set_level() / flush() is necessary.
    //
    // For loggers built via the public constructors the default is `local`
    // (matching spdlog's own default). `adopt(logger, time_type)` lets
    // callers override that up front for a foreign logger.
    void set_pattern_time(spdlog::pattern_time_type time_type)
    {
        pattern_time_ = time_type;
        apply_pattern_();
    }

    // Returns the timestamp mode currently in effect for the pinned JSON
    // pattern. Useful when wrapping a json_logger inside another component
    // that wants to know whether the timestamps are local or UTC without
    // having to track the value separately.
    [[nodiscard]] spdlog::pattern_time_type pattern_time() const noexcept { return pattern_time_; }

    // Escape hatch for any spdlog::logger configuration we don't expose
    // directly (extra sinks, custom error handler, etc.). The "structurally
    // impossible to emit invalid JSON" guarantee assumes nobody calls
    // set_pattern() on the returned logger -- doing so will break it.
    [[nodiscard]] const std::shared_ptr<spdlog::logger> &spdlog_logger() const noexcept { return logger_; }

private:
    // Used by `adopt()`. Persists `time_type` on the json_logger so any
    // future internal pattern reapplication keeps using it; the default
    // is `local`, matching spdlog's own default.
    explicit json_logger(
        std::shared_ptr<spdlog::logger> logger,
        json_pattern_options options,
        spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local
    )
        : logger_(std::move(logger))
        , pattern_options_(std::move(options))
        , pattern_time_(time_type)
    {
        apply_pattern_();
    }

    // Installs the pinned JSON pattern on the underlying spdlog logger and
    // refreshes the cached "does the %v fragment need a leading comma?" flag.
    // Always uses the persisted `pattern_time_` so a previously-configured
    // UTC mode survives any future pattern reapplication.
    void apply_pattern_()
    {
        auto built = detail::make_json_pattern(logger_->name(), pattern_options_);
        fragment_needs_leading_comma_ = built.fragment_needs_leading_comma;
        logger_->set_pattern(std::move(built.pattern), pattern_time_);
    }

    // Strip the leading comma from `fragment` when the pinned pattern emits
    // no fixed fields (i.e. it is just `{%v}`). With at least one fixed
    // field present, `%v` is preceded by the last field's value and the
    // fragment must begin with `,` to separate them; with none present, an
    // unstripped fragment would produce a line starting with `{,...`.
    [[nodiscard]] std::string_view trim_leading_comma_(std::string_view fragment) const noexcept
    {
        if (!fragment_needs_leading_comma_ && !fragment.empty() && fragment.front() == ',')
        {
            fragment.remove_prefix(1);
        }
        return fragment;
    }

    // Install a handler for spdlog runtime errors. Pass an empty
    // std::function to clear the handler entirely; once cleared, subsequent
    // runtime errors are dropped silently (spdlog v2 no-ops when the handler
    // slot is empty rather than reinstating the built-in stderr writer).
    // The handler runs on the thread that triggered the error and must be
    // safe to call concurrently if the logger is shared across threads.
    //
    // This is the implementation primitive behind the public `silence_errors()`
    // and `forward_errors_to(...)` members; it's intentionally private so
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
        logger_->log(lvl, trim_leading_comma_(fragment));
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
        logger_->log(lvl, trim_leading_comma_(out));
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
        logger_->log(lvl, trim_leading_comma_(out));
    }

    std::shared_ptr<spdlog::logger> logger_;
    json_properties properties_;
    std::string cached_properties_;
    json_pattern_options pattern_options_;
    // Persisted timestamp mode for the pinned JSON pattern. spdlog's
    // `set_pattern()` resets the time mode on every call, so we have to
    // re-pass `pattern_time_` from `apply_pattern_()` to keep the chosen
    // mode through any future reapplication.
    spdlog::pattern_time_type pattern_time_ = spdlog::pattern_time_type::local;
    // Cached from the last apply_pattern_(). True for the default options
    // (and any subset that keeps at least one fixed field); false only when
    // every fixed field is omitted, in which case log_*() must strip the
    // leading comma from the fragment it would otherwise emit.
    bool fragment_needs_leading_comma_ = true;
};

} // namespace jspdlog
