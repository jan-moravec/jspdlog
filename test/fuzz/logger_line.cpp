// Target 3: logger_line. Fuzzes everything a user configures: the logger name, every header key, the message key, the
// time type, the line terminator and the message. Keys and names containing percent signs, quotes and control bytes
// must never change the shape of the line.
#include "fuzz_support.h"

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

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer requires this name
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    fuzz::reader r(fuzz::as_text(data, size));

    const std::uint8_t flags = r.byte();
    const auto eol_choice = static_cast<std::uint8_t>(r.byte() % 3U);
    const std::string name = r.string(16);

    // Two equal keys would write a duplicate key, which is the caller's problem, so make them distinct as written: two
    // different invalid keys can be replaced by the same text.
    std::set<std::string> used;
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
    const std::string message{r.rest()};

    jspdlog::json_options options;
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
        if (k->enabled)
        {
            expected_keys.push_back(fuzz::written_text(k->key));
        }
    }
    expected_keys.push_back(fuzz::written_text(message_key.key));
    FUZZ_CHECK(fuzz::keys_of(parsed) == expected_keys);

    if (timestamp.enabled)
    {
        const auto &ts = parsed.at(fuzz::written_text(timestamp.key));
        FUZZ_CHECK(ts.is_string());
        const std::string text = ts.get<std::string>();
        FUZZ_CHECK(text.size() >= 20 && text[4] == '-' && text[7] == '-' && text[10] == 'T' && text[13] == ':');
        if (utc)
        {
            FUZZ_CHECK(text.back() == 'Z');
        }
    }
    if (logger.enabled)
    {
        FUZZ_CHECK(parsed.at(fuzz::written_text(logger.key)) == ojson(fuzz::written_text(name)));
    }
    if (level.enabled)
    {
        FUZZ_CHECK(parsed.at(fuzz::written_text(level.key)) == ojson("info"));
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
        const auto &src = parsed.at(fuzz::written_text(source.key));
        FUZZ_CHECK(src.is_object());
        FUZZ_CHECK((fuzz::keys_of(src) == std::vector<std::string>{"file", "line", "function"}));
        FUZZ_CHECK(src.at("file").get<std::string>().find_first_of("/\\") == std::string::npos);
        FUZZ_CHECK(src.at("line").is_number_integer());
    }
    FUZZ_CHECK(parsed.at(fuzz::written_text(message_key.key)) == ojson(fuzz::written_text(message)));
    return 0;
}
