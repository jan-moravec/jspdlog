// Target 3: logger_line. Fuzzes everything a user configures: the logger name, every header key, the message key, the
// time type, the line terminator and the message, on top of the defaults or of one of the schema presets, which add
// their level names, timestamp style and flat call site. Keys and names containing percent signs, quotes and control
// bytes must never change the shape of the line. The message is also read as a traceparent header: one that parses
// must be written back by trace_fields with the same hex. The logger name is too short to hold one. With the top flag
// bit set, a time anywhere in the range of the clock is formatted too, and its timestamp compared with the model.
#include "fuzz_support.h"

#include <algorithm>
#include <chrono>
#include <set>

namespace
{

using fuzz::ojson;

struct header_key
{
    bool enabled = false;
    std::string key;
};

// The terminators worth testing: none, LF and CR LF.
std::string_view eol_for(std::uint8_t choice)
{
    if (choice == 0)
    {
        return "";
    }
    return choice == 1 ? "\n" : "\r\n";
}

// A text that parses as a traceparent is exactly one, and trace_fields gives its parts back unchanged.
void check_traceparent(std::string_view text)
{
    const std::optional<jspdlog::trace_context> ctx = jspdlog::parse_traceparent(text);
    if (!ctx)
    {
        return;
    }
    FUZZ_CHECK(ctx->valid());
    FUZZ_CHECK(text.size() == 55 && text.substr(0, 3) == "00-" && text[35] == '-' && text[52] == '-');
    const std::string expected = R"({"trace_id":")" + std::string{text.substr(3, 32)} + R"(","span_id":")" +
                                 std::string{text.substr(36, 16)} + R"(","trace_flags":")" +
                                 std::string{text.substr(53, 2)} + R"("})";
    FUZZ_CHECK(jspdlog::trace_fields(*ctx).to_json() == expected);
}

// A message logged with a time of the caller's choosing, as spdlog allows, has a valid timestamp: epoch milliseconds,
// or the RFC 3339 string of the model, or null for a year that RFC 3339 cannot write.
void check_time(
    const jspdlog::json_options &options, std::string_view timestamp_key, std::string_view eol, std::int64_t wanted
)
{
    using clock = spdlog::log_clock;
    // Both ends are rounded toward zero, so every second between them converts to the clock without overflow.
    const std::int64_t lowest = std::chrono::duration_cast<std::chrono::seconds>(clock::duration::min()).count();
    const std::int64_t highest = std::chrono::duration_cast<std::chrono::seconds>(clock::duration::max()).count();
    const std::int64_t seconds = std::clamp(wanted, lowest, highest);

    jspdlog::json_formatter formatter(options);
    spdlog::details::log_msg msg("name", jspdlog::level::info, "x");
    msg.time = clock::time_point{std::chrono::duration_cast<clock::duration>(std::chrono::seconds(seconds))};
    spdlog::memory_buf_t buf;
    formatter.format(msg, buf);
    const std::string_view line{buf.data(), buf.size()};
    // The header keys are the only strings that can be invalid UTF-8 here.
    if (!fuzz::parseable(timestamp_key) || !fuzz::parseable(options.message_key) ||
        !fuzz::parseable(options.logger_key.value_or("")) || !fuzz::parseable(options.level_key.value_or("")) ||
        !fuzz::parseable(options.process_key.value_or("")) || !fuzz::parseable(options.thread_key.value_or("")))
    {
        fuzz::check_shape(line, eol);
        return;
    }
    const ojson parsed = fuzz::parse_line(line, eol);
    const ojson &ts = parsed.at(fuzz::written_text(timestamp_key));

    if (options.timestamp_style == jspdlog::timestamp_format::epoch_millis)
    {
        FUZZ_CHECK(ts == ojson(seconds * 1000));
        return;
    }
    const std::optional<std::string> utc = fuzz::rfc3339_utc_ms(seconds * 1000);
    if (options.time_type == spdlog::pattern_time_type::utc)
    {
        if (!utc)
        {
            FUZZ_CHECK(ts.is_null());
            return;
        }
        const std::string text = ts.get<std::string>();
        // The model has milliseconds; the date and time before the fraction are the same in every style.
        FUZZ_CHECK(text.substr(0, 20) == utc->substr(0, 20));
        FUZZ_CHECK(text.find_first_not_of('0', 20) == text.size() - 1 && text.back() == 'Z');
        return;
    }
    // Local time can cross the first or the last year by the zone offset, so only its form is checked.
    if (ts.is_null())
    {
        return;
    }
    const std::string text = ts.get<std::string>();
    FUZZ_CHECK(text.size() >= 24 && text[4] == '-' && text[7] == '-' && text[10] == 'T' && text[13] == ':');
    FUZZ_CHECK(text.find_first_not_of("0123456789-+:.TZ") == std::string::npos);
}

// The options to start from: the defaults, or one of the presets.
jspdlog::json_options base_options(std::uint8_t choice)
{
    switch (choice % 5U)
    {
    case 1:
        return jspdlog::presets::ecs();
    case 2:
        return jspdlog::presets::gcp();
    case 3:
        return jspdlog::presets::datadog();
    case 4:
        return jspdlog::presets::otel();
    default:
        return {};
    }
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer requires this name
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    fuzz::reader r(fuzz::as_text(data, size));

    const std::uint8_t flags = r.byte();
    jspdlog::json_options options = base_options(r.byte());
    const auto eol_choice = static_cast<std::uint8_t>(r.byte() % 3U);
    const std::string name = r.string(16);

    // Two equal keys would write a duplicate key, which is the caller's problem, so make them distinct as written: two
    // different invalid keys can be replaced by the same text.
    std::set<std::string> used;
    if (options.source_flat)
    {
        used.insert(options.source_file_key);
        used.insert(options.source_line_key);
        used.insert(options.source_function_key);
    }
    const auto next_key = [&](bool enabled) {
        std::string key = r.string(8);
        if (enabled)
        {
            while (!used.insert(fuzz::written_text(key)).second)
            {
                key += '~';
            }
        }
        return header_key{enabled, std::move(key)};
    };
    const header_key timestamp = next_key((flags & 0x01U) != 0);
    const header_key logger = next_key((flags & 0x02U) != 0);
    const header_key level = next_key((flags & 0x04U) != 0);
    const header_key process = next_key((flags & 0x08U) != 0);
    const header_key thread = next_key((flags & 0x10U) != 0);
    const header_key source = next_key((flags & 0x20U) != 0);
    const header_key message_key = next_key(true);
    const bool utc = (flags & 0x40U) != 0;
    const std::string_view eol = eol_for(eol_choice);
    const bool timed = (flags & 0x80U) != 0;
    const std::int64_t seconds = timed ? r.value<std::int64_t>() : 0;
    const std::string message{r.rest()};
    check_traceparent(message);

    const auto optional_key = [](const header_key &k) {
        return k.enabled ? std::optional<std::string>{k.key} : std::nullopt;
    };
    options.timestamp_key = optional_key(timestamp);
    options.logger_key = optional_key(logger);
    options.level_key = optional_key(level);
    options.process_key = optional_key(process);
    options.thread_key = optional_key(thread);
    options.source_key = optional_key(source);
    options.message_key = message_key.key;
    options.time_type = utc ? spdlog::pattern_time_type::utc : spdlog::pattern_time_type::local;
    options.eol = std::string{eol};
    options.severity_key.reset(); // a preset's severity field is not one of the fuzzed keys
    if (timed && timestamp.enabled)
    {
        check_time(options, timestamp.key, eol, seconds);
    }

    const fuzz::harness h(name, options);
    h.logger.info(message);

    FUZZ_CHECK(h.sink->lines.size() == 1);
    const std::string &line = h.sink->lines.front();

    // Every line parses, unless the build copies invalid UTF-8, which nlohmann rejects: then only the shape is checked.
    bool utf8 = fuzz::parseable(name) && fuzz::parseable(message);
    for (const header_key *k : {&timestamp, &logger, &level, &process, &thread, &source, &message_key})
    {
        utf8 = utf8 && fuzz::parseable(k->key);
    }
    if (!utf8)
    {
        fuzz::check_shape(line, eol);
        return 0;
    }

    const ojson parsed = fuzz::parse_line(line, eol);

    // Every enabled key is present, in the documented order, and nothing else is.
    std::vector<std::string> expected_keys;
    for (const header_key *k : {&timestamp, &logger, &level, &process, &thread, &source})
    {
        if (!k->enabled)
        {
            continue;
        }
        if (k == &source && options.source_flat)
        {
            expected_keys.push_back(options.source_file_key);
            expected_keys.push_back(options.source_line_key);
            expected_keys.push_back(options.source_function_key);
            continue;
        }
        expected_keys.push_back(fuzz::written_text(k->key));
    }
    expected_keys.push_back(fuzz::written_text(message_key.key));
    FUZZ_CHECK(fuzz::keys_of(parsed) == expected_keys);

    if (timestamp.enabled)
    {
        const auto &ts = parsed.at(fuzz::written_text(timestamp.key));
        if (options.timestamp_style == jspdlog::timestamp_format::epoch_millis)
        {
            FUZZ_CHECK(ts.is_number_integer());
        }
        else
        {
            FUZZ_CHECK(ts.is_string());
            const std::string text = ts.get<std::string>();
            FUZZ_CHECK(text.size() >= 20 && text[4] == '-' && text[7] == '-' && text[10] == 'T' && text[13] == ':');
            if (utc)
            {
                FUZZ_CHECK(text.back() == 'Z');
            }
        }
    }
    if (logger.enabled)
    {
        FUZZ_CHECK(parsed.at(fuzz::written_text(logger.key)) == ojson(fuzz::written_text(name)));
    }
    if (level.enabled)
    {
        const std::string info_name = options.level_names ? (*options.level_names)[2] : std::string{"info"};
        FUZZ_CHECK(parsed.at(fuzz::written_text(level.key)) == ojson(info_name));
    }
    if (process.enabled)
    {
        FUZZ_CHECK(parsed.at(fuzz::written_text(process.key)).is_number_integer());
    }
    if (thread.enabled)
    {
        FUZZ_CHECK(parsed.at(fuzz::written_text(thread.key)).is_number_integer());
    }
    if (source.enabled)
    {
        // The members of the call site, in an object under the source key or as fields of the line.
        const ojson &src = options.source_flat ? parsed : parsed.at(fuzz::written_text(source.key));
        FUZZ_CHECK(src.is_object());
        if (!options.source_flat)
        {
            FUZZ_CHECK((fuzz::keys_of(src) == std::vector<std::string>{"file", "line", "function"}));
        }
        const std::string file_key = options.source_flat ? options.source_file_key : "file";
        const std::string line_key = options.source_flat ? options.source_line_key : "line";
        FUZZ_CHECK(src.at(file_key).get<std::string>().find_first_of("/\\") == std::string::npos);
        FUZZ_CHECK(src.at(line_key).is_number_integer());
    }
    FUZZ_CHECK(parsed.at(fuzz::written_text(message_key.key)) == ojson(fuzz::written_text(message)));
    return 0;
}
