// SPDX-License-Identifier: MIT
//
// PROOF OF CONCEPT - not part of the installed library.
//
// A sketch of the proposed jspdlog design:
//
//   * A custom spdlog::formatter renders the whole JSON line from spdlog's
//     log_msg (no pinned pattern). It reads the logger name at format time,
//     emits source location only when present, and escapes messages that did
//     not come through jspdlog instead of corrupting the line.
//   * No macros: the call site (file/line/function) is captured by default
//     arguments (__builtin_FILE/LINE/FUNCTION, available in C++17 on GCC,
//     Clang and MSVC).
//   * No global state: nothing here touches spdlog's registry, and the token
//     that marks jspdlog payloads is generated per logger.
//   * Context for inner code: json_properties keep insertion order, and child
//     loggers share their parent's immutable, already-serialized context.
//
// How pre-serialized properties reach the formatter: json_logger prefixes the
// payload with a random 16-byte token shared only with its own formatter. The
// formatter splices payloads that carry the token in verbatim and escapes all
// others, so a foreign message cannot inject raw JSON. The token travels in
// the payload, which spdlog copies for async logging.

#pragma once

#include <jspdlog/jspdlog.h> // level alias, raw_json, detail::append_json_quoted, spdlog/fmt includes

#include <chrono>
#include <ctime>
#include <random>
#include <vector>

namespace jspdlog::poc
{

using jspdlog::level;
using jspdlog::raw_json;

namespace detail
{

using jspdlog::detail::append_json_quoted;

template <typename Buffer>
void append(Buffer &out, std::string_view s)
{
    out.append(s.data(), s.data() + s.size());
}

template <typename T>
struct type_identity
{
    using type = T;
};

// Call site, filled in by default arguments at the caller.
struct site
{
    const char *file;
    int line;
    const char *function;

    [[nodiscard]] spdlog::source_loc loc() const
    {
        using line_t = decltype(spdlog::source_loc{}.line);
        return spdlog::source_loc{file, static_cast<line_t>(line), function};
    }
};

#define JSPDLOG_POC_SITE_DEFAULT_                                                                                      \
    jspdlog::poc::detail::site                                                                                         \
    {                                                                                                                  \
        __builtin_FILE(), __builtin_LINE(), __builtin_FUNCTION()                                                       \
    }

inline std::string random_token()
{
    std::random_device rd;
    std::string token(16, '\0');
    for (auto &c : token)
    {
        c = static_cast<char>(rd() & 0xFF);
    }
    return token;
}

inline level msg_level(const spdlog::details::log_msg &msg)
{
#if JSPDLOG_SPDLOG_V2
    return msg.log_level;
#else
    return msg.level;
#endif
}

inline std::string_view level_name(level lvl)
{
#if JSPDLOG_SPDLOG_V2
    return spdlog::to_string_view(lvl);
#else
    const auto &sv = spdlog::level::to_string_view(lvl);
    return {sv.data(), sv.size()};
#endif
}

} // namespace detail

// A format string plus the call site, captured without macros:
// `logger.info("x={}", x)` records where it was called from.
template <typename... Args>
struct format_here
{
    spdlog::format_string_t<Args...> fmt;
    detail::site where;

    template <typename S, std::enable_if_t<std::is_convertible_v<const S &, spdlog::fmt_lib::string_view>, int> = 0>
    FMT_CONSTEVAL format_here(const S &s, detail::site w = JSPDLOG_POC_SITE_DEFAULT_)
        : fmt(s), where(w)
    {
    }
};

template <typename... Args>
using format_here_t = format_here<typename detail::type_identity<Args>::type...>;

// ---------------------------------------------------------------------------
// json_properties: typed key/value pairs in insertion order.
// ---------------------------------------------------------------------------
class json_properties
{
public:
    json_properties() = default;

    template <typename K, typename V, typename... Rest>
    json_properties(K &&key, V &&value, Rest &&...rest)
    {
        static_assert(sizeof...(Rest) % 2 == 0, "json_properties: expected key/value pairs");
        insert_pairs_(std::forward<K>(key), std::forward<V>(value), std::forward<Rest>(rest)...);
    }

    void insert(std::string_view key, std::nullptr_t) { set_(key, "null"); }
    void insert(std::string_view key, std::string_view v)
    {
        std::string json;
        detail::append_json_quoted(json, v);
        set_(key, json);
    }
    void insert(std::string_view key, const char *v) { v ? insert(key, std::string_view{v}) : set_(key, "null"); }
    void insert(std::string_view key, bool v) { set_(key, v ? "true" : "false"); }
    void insert(std::string_view key, char v) { insert(key, std::string_view{&v, 1}); }
    template <
        typename T,
        std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>, int> = 0>
    void insert(std::string_view key, T v)
    {
        set_(key, spdlog::fmt_lib::format("{}", +v)); // unary + keeps int8_t numeric
    }
    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    void insert(std::string_view key, T v)
    {
        if (!std::isfinite(v))
        {
            set_(key, "null");
            return;
        }
        std::string s = spdlog::fmt_lib::format("{}", v);
        if (s.find_first_of(".eE") == std::string::npos)
        {
            s += ".0";
        }
        set_(key, s);
    }
    void insert(std::string_view key, const raw_json &v) { set_(key, v.value.empty() ? "null" : v.value); }
    void insert(std::string_view key, const json_properties &nested) { set_(key, nested.to_json()); }
    void insert(std::string_view, wchar_t) = delete;
    void insert(std::string_view, char16_t) = delete;
    void insert(std::string_view, char32_t) = delete;
    template <typename T>
    void insert(std::string_view, const T *) = delete;

    // `other` wins on collisions; replaced keys keep their position.
    void merge(const json_properties &other)
    {
        for (const auto &e : other.entries_)
        {
            set_entry_(e);
        }
    }

    friend json_properties operator+(json_properties lhs, const json_properties &rhs)
    {
        lhs.merge(rhs);
        return lhs;
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool contains(std::string_view key) const noexcept { return find_(key) != nullptr; }
    void clear() noexcept { entries_.clear(); }

    [[nodiscard]] std::string to_json() const
    {
        std::string out;
        append_to(out);
        if (out.empty())
        {
            return "{}";
        }
        out.front() = '{';
        out.push_back('}');
        return out;
    }

    // Append `,"k":v,...` (each entry is stored pre-serialized like this).
    template <typename Buffer>
    void append_to(Buffer &out) const
    {
        for (const auto &e : entries_)
        {
            detail::append(out, e.text);
        }
    }

    // Append entries whose key is not in `except`.
    template <typename Buffer>
    void append_to(Buffer &out, const json_properties &except) const
    {
        for (const auto &e : entries_)
        {
            if (!except.contains(e.key))
            {
                detail::append(out, e.text);
            }
        }
    }

private:
    struct entry
    {
        std::string key;  // raw key, for lookups
        std::string text; // `,"key":value`
    };

    template <typename K, typename V, typename... Rest>
    void insert_pairs_(K &&key, V &&value, Rest &&...rest)
    {
        insert(std::string_view{key}, std::forward<V>(value));
        if constexpr (sizeof...(Rest) > 0)
        {
            insert_pairs_(std::forward<Rest>(rest)...);
        }
    }

    const entry *find_(std::string_view key) const noexcept
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

    void set_(std::string_view key, std::string_view json)
    {
        entry e{std::string{key}, {}};
        e.text.reserve(key.size() + json.size() + 4);
        e.text.push_back(',');
        detail::append_json_quoted(e.text, key);
        e.text.push_back(':');
        detail::append(e.text, json);
        set_entry_(std::move(e));
    }

    void set_entry_(entry e)
    {
        if (auto *existing = const_cast<entry *>(find_(e.key)))
        {
            existing->text = std::move(e.text);
        }
        else
        {
            entries_.push_back(std::move(e));
        }
    }

    std::vector<entry> entries_;
};

// ---------------------------------------------------------------------------
// json_formatter
// ---------------------------------------------------------------------------
struct formatter_options
{
    std::optional<std::string> timestamp_key = std::string{"timestamp"};
    std::optional<std::string> logger_key = std::string{"logger"};
    std::optional<std::string> level_key = std::string{"level"};
    std::optional<std::string> process_key = std::string{"process"};
    std::optional<std::string> thread_key = std::string{"thread"};
    // {"file":...,"line":...,"function":...}, emitted when the record has one.
    std::optional<std::string> source_key = std::string{"source"};
    std::string message_key = "message";
    spdlog::pattern_time_type time_type = spdlog::pattern_time_type::local;
    std::optional<std::string> eol;
};

class json_formatter final : public spdlog::formatter
{
public:
    // `token` marks payloads that json_logger pre-serialized. An empty token
    // (the default) treats every payload as a plain message, which is what
    // you want when installing the formatter on a logger you don't wrap.
    explicit json_formatter(formatter_options options = {}, std::string token = {})
        : opts_(std::move(options)),
          token_(std::move(token)),
          eol_(opts_.eol.value_or(spdlog::details::os::default_eol)),
          pid_text_(spdlog::fmt_lib::format_int(spdlog::details::os::pid()).str())
    {
        auto key = [](const std::optional<std::string> &k) {
            std::string out;
            if (k)
            {
                detail::append_json_quoted(out, *k);
                out.push_back(':');
            }
            return out;
        };
        timestamp_key_ = key(opts_.timestamp_key);
        logger_key_ = key(opts_.logger_key);
        level_key_ = key(opts_.level_key);
        process_key_ = key(opts_.process_key);
        thread_key_ = key(opts_.thread_key);
        source_key_ = key(opts_.source_key);
        message_key_ = key(opts_.message_key);
    }

    void format(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest) override
    {
        bool first = true;
        auto field = [&](const std::string &key) {
            if (!first)
            {
                dest.push_back(',');
            }
            first = false;
            detail::append(dest, key);
        };

        dest.push_back('{');
        if (opts_.timestamp_key)
        {
            field(timestamp_key_);
            append_timestamp_(msg, dest);
        }
        if (opts_.logger_key)
        {
            field(logger_key_);
            detail::append_json_quoted(dest, {msg.logger_name.data(), msg.logger_name.size()});
        }
        if (opts_.level_key)
        {
            field(level_key_);
            detail::append_json_quoted(dest, detail::level_name(detail::msg_level(msg)));
        }
        if (opts_.process_key)
        {
            field(process_key_);
            detail::append(dest, pid_text_);
        }
        if (opts_.thread_key)
        {
            field(thread_key_);
            append_int_(dest, msg.thread_id);
        }
        if (opts_.source_key && !msg.source.empty())
        {
            field(source_key_);
            detail::append(dest, R"({"file":)");
            detail::append_json_quoted(dest, msg.source.filename);
            detail::append(dest, R"(,"line":)");
            append_int_(dest, msg.source.line);
            detail::append(dest, R"(,"function":)");
            detail::append_json_quoted(dest, msg.source.funcname ? msg.source.funcname : "");
            dest.push_back('}');
        }

        const std::string_view payload{msg.payload.data(), msg.payload.size()};
        if (!token_.empty() && payload.substr(0, token_.size()) == token_)
        {
            std::string_view fragment = payload.substr(token_.size()); // `,"k":v,...`
            if (first && !fragment.empty())
            {
                fragment.remove_prefix(1);
            }
            detail::append(dest, fragment);
        }
        else
        {
            field(message_key_);
            detail::append_json_quoted(dest, payload);
        }
        dest.push_back('}');
        detail::append(dest, eol_);
    }

    [[nodiscard]] std::unique_ptr<spdlog::formatter> clone() const override
    {
        return std::make_unique<json_formatter>(opts_, token_);
    }

private:
    template <typename Int>
    static void append_int_(spdlog::memory_buf_t &dest, Int value)
    {
        const spdlog::fmt_lib::format_int text(value);
        dest.append(text.data(), text.data() + text.size());
    }

    // RFC 3339 with milliseconds; the date/time part is cached per second.
    void append_timestamp_(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest)
    {
        using namespace std::chrono;
        const auto secs = time_point_cast<seconds>(msg.time);
        const auto millis = static_cast<int>(duration_cast<milliseconds>(msg.time - secs).count());
        const std::time_t tt = system_clock::to_time_t(secs);
        if (tt != cached_second_)
        {
            const bool utc = opts_.time_type == spdlog::pattern_time_type::utc;
            const std::tm tm = utc ? spdlog::details::os::gmtime(tt) : spdlog::details::os::localtime(tt);
            cached_.clear();
            spdlog::fmt_lib::format_to(
                std::back_inserter(cached_),
                "\"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.",
                tm.tm_year + 1900,
                tm.tm_mon + 1,
                tm.tm_mday,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec
            );
            zone_.clear();
            if (utc)
            {
                zone_ = "Z\"";
            }
            else
            {
                const int offset = spdlog::details::os::utc_minutes_offset(tm);
                const int abs_offset = offset < 0 ? -offset : offset;
                zone_ = spdlog::fmt_lib::format(
                    "{}{:02}:{:02}\"", offset < 0 ? '-' : '+', abs_offset / 60, abs_offset % 60
                );
            }
            cached_second_ = tt;
        }
        dest.append(cached_.data(), cached_.data() + cached_.size());
        dest.push_back(static_cast<char>('0' + millis / 100));
        dest.push_back(static_cast<char>('0' + millis / 10 % 10));
        dest.push_back(static_cast<char>('0' + millis % 10));
        detail::append(dest, zone_);
    }

    formatter_options opts_;
    std::string token_;
    std::string eol_;
    std::string pid_text_;
    std::string timestamp_key_, logger_key_, level_key_, process_key_, thread_key_, source_key_, message_key_;
    std::time_t cached_second_ = -1;
    spdlog::memory_buf_t cached_;
    std::string zone_;
};

// ---------------------------------------------------------------------------
// json_logger
// ---------------------------------------------------------------------------
class json_logger
{
public:
    json_logger(std::string name, spdlog::sink_ptr sink, const formatter_options &options = {})
        : json_logger(std::make_shared<spdlog::logger>(std::move(name), std::move(sink)), options)
    {
    }

    // Wrap a logger you built (e.g. an async logger with your own thread pool).
    [[nodiscard]] static json_logger
    adopt(std::shared_ptr<spdlog::logger> logger, const formatter_options &options = {})
    {
        return json_logger(std::move(logger), options);
    }

    // A child logger. Its context is the parent's plus `props` (props win);
    // the parent is unchanged. The context is immutable and shared by all
    // copies of the child.
    [[nodiscard]] json_logger with_properties(const json_properties &props) const
    {
        json_logger child = *this;
        auto ctx = std::make_shared<bound_context>();
        if (context_)
        {
            ctx->props = context_->props;
        }
        ctx->props.merge(props);
        ctx->props.append_to(ctx->fragment);
        child.context_ = std::move(ctx);
        return child;
    }

    // Call shapes (also available as log(level, ...)):
    //   info("fmt {}", args...)
    //   info(props, "fmt {}", args...)
    //   info(props)
    // Each records its call site through a defaulted argument.
    template <typename... Args>
    void log(level lvl, format_here_t<Args...> fmt, Args &&...args)
    {
        log(lvl, empty_(), std::move(fmt), std::forward<Args>(args)...);
    }

    template <typename... Args>
    void log(level lvl, const json_properties &props, format_here_t<Args...> fmt, Args &&...args)
    {
        if (logger_->should_log(lvl))
        {
            spdlog::memory_buf_t msg;
            spdlog::fmt_lib::format_to(std::back_inserter(msg), fmt.fmt, std::forward<Args>(args)...);
            emit_(fmt.where, lvl, props, std::string_view{msg.data(), msg.size()}, true);
        }
    }

    void log(level lvl, const json_properties &props, detail::site where = JSPDLOG_POC_SITE_DEFAULT_)
    {
        if (logger_->should_log(lvl))
        {
            emit_(where, lvl, props, {}, false);
        }
    }

#define JSPDLOG_POC_LEVEL_(name, lvl)                                                                                  \
    template <typename... Args>                                                                                        \
    void name(format_here_t<Args...> fmt, Args &&...args)                                                              \
    {                                                                                                                  \
        log(lvl, empty_(), std::move(fmt), std::forward<Args>(args)...);                                               \
    }                                                                                                                  \
    template <typename... Args>                                                                                        \
    void name(const json_properties &props, format_here_t<Args...> fmt, Args &&...args)                                \
    {                                                                                                                  \
        log(lvl, props, std::move(fmt), std::forward<Args>(args)...);                                                  \
    }                                                                                                                  \
    void name(const json_properties &props, detail::site where = JSPDLOG_POC_SITE_DEFAULT_)                            \
    {                                                                                                                  \
        log(lvl, props, where);                                                                                        \
    }

    JSPDLOG_POC_LEVEL_(trace, level::trace)
    JSPDLOG_POC_LEVEL_(debug, level::debug)
    JSPDLOG_POC_LEVEL_(info, level::info)
    JSPDLOG_POC_LEVEL_(warn, level::warn)
    JSPDLOG_POC_LEVEL_(error, level::err)
    JSPDLOG_POC_LEVEL_(critical, level::critical)
#undef JSPDLOG_POC_LEVEL_

    void set_level(level lvl) { logger_->set_level(lvl); }
    [[nodiscard]] bool should_log(level lvl) const noexcept { return logger_->should_log(lvl); }
    void flush() { logger_->flush(); }
    [[nodiscard]] const std::shared_ptr<spdlog::logger> &spdlog_logger() const noexcept { return logger_; }

private:
    struct bound_context
    {
        json_properties props;
        std::string fragment; // props serialized as `,"k":v,...`
    };

    json_logger(std::shared_ptr<spdlog::logger> logger, const formatter_options &options)
        : logger_(std::move(logger)), token_(detail::random_token())
    {
        detail::append_json_quoted(message_key_, options.message_key);
        message_key_.insert(0, 1, ',');
        message_key_.push_back(':');
        logger_->set_formatter(std::make_unique<json_formatter>(options, token_));
    }

    static const json_properties &empty_()
    {
        static const json_properties empty; // immutable constant, not shared state
        return empty;
    }

    void emit_(const detail::site &where, level lvl, const json_properties &props, std::string_view msg, bool has_msg)
    {
        spdlog::memory_buf_t out;
        detail::append(out, token_);
        if (context_)
        {
            if (props.empty())
            {
                detail::append(out, context_->fragment);
            }
            else
            {
                context_->props.append_to(out, props); // skip keys the call overrides
            }
        }
        props.append_to(out);
        if (has_msg)
        {
            detail::append(out, message_key_);
            detail::append_json_quoted(out, msg);
        }
        logger_->log(where.loc(), lvl, spdlog::string_view_t{out.data(), out.size()});
    }

    std::shared_ptr<spdlog::logger> logger_;
    std::string token_;
    std::string message_key_; // `,"message":`
    std::shared_ptr<const bound_context> context_;
};

// Make a logger you don't wrap (for example one a library logs to) emit JSON
// lines; every message becomes {"message": "..."}.
inline void install_json_formatter(spdlog::logger &logger, const formatter_options &options = {})
{
    logger.set_formatter(std::make_unique<json_formatter>(options));
}

} // namespace jspdlog::poc
