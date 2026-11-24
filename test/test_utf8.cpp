// The UTF-8 policy of the build, on every path that writes a string. This file is compiled for the default policy and,
// again, for each other policy and for line separator escaping, so the expected text is chosen by the macros.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using test_support::deterministic_options;
using test_support::fixture;

namespace
{

struct utf8_case
{
    std::string_view input;
    std::string_view replace; // Expected output under the replace policy, before JSON quoting rules apply.
    std::string_view escape;  // Expected output under the escape policy.
};

// U+FFFD in UTF-8, one per maximal invalid subpart.
#define FFFD "\xEF\xBF\xBD"

constexpr utf8_case CASES[] = {
    {"ascii", "ascii", "ascii"},
    {"\xC3\xA9", "\xC3\xA9", "\xC3\xA9"},                                      // valid, 2 bytes
    {"\xE2\x82\xAC", "\xE2\x82\xAC", "\xE2\x82\xAC"},                          // valid, 3 bytes
    {"\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80"},              // valid, 4 bytes
    {"a\xC3", "a" FFFD, "a\\u00c3"},                                           // truncated at the end
    {"a\xC3z", "a" FFFD "z", "a\\u00c3z"},                                     // truncated before ASCII
    {"\xE2\x82", FFFD, "\\u00e2\\u0082"},                                      // one maximal subpart
    {"\xE2\x82z", FFFD "z", "\\u00e2\\u0082z"},                                // the same, followed by ASCII
    {"\xF0\x9F\x98", FFFD, "\\u00f0\\u009f\\u0098"},                           // 4-byte start cut after 3 bytes
    {"\xF0\x80\x80", FFFD FFFD FFFD, "\\u00f0\\u0080\\u0080"},                 // 80 cannot follow F0
    {"\xED\xA0\x80", FFFD FFFD FFFD, "\\u00ed\\u00a0\\u0080"},                 // surrogate
    {"\xF4\x90\x80\x80", FFFD FFFD FFFD FFFD, "\\u00f4\\u0090\\u0080\\u0080"}, // above U+10FFFF
    {"\xC0\xAF", FFFD FFFD, "\\u00c0\\u00af"},                                 // overlong
    {"\xE0\x80\xAF", FFFD FFFD FFFD, "\\u00e0\\u0080\\u00af"},                 // overlong, 3 bytes
    {"\xC1\xBF", FFFD FFFD, "\\u00c1\\u00bf"},                                 // overlong
    {"\x80", FFFD, "\\u0080"},                                                 // lone continuation byte
    {"a\xBFz", "a" FFFD "z", "a\\u00bfz"},                                     // lone continuation byte in text
    {"\xFF", FFFD, "\\u00ff"},
    {"\xFE\xFF", FFFD FFFD, "\\u00fe\\u00ff"},
    {"\xF5\x80\x80\x80", FFFD FFFD FFFD FFFD, "\\u00f5\\u0080\\u0080\\u0080"}, // F5 never starts a sequence
    {"\xC3\xA9t\xC3\xA9 \xFF end", "\xC3\xA9t\xC3\xA9 " FFFD " end", "\xC3\xA9t\xC3\xA9 \\u00ff end"},
    {"\xC3\xA9\xC3", "\xC3\xA9" FFFD, "\xC3\xA9\\u00c3"}, // valid then truncated
};

#undef FFFD

// What the build writes for the input.
std::string expected_for(const utf8_case &c)
{
#if JSPDLOG_UTF8_POLICY == JSPDLOG_UTF8_REPLACE
    return std::string(c.replace);
#elif JSPDLOG_UTF8_POLICY == JSPDLOG_UTF8_ESCAPE
    return std::string(c.escape);
#else
    return std::string(c.input);
#endif
}

#ifdef JSPDLOG_TEST_NLOHMANN
// Whether the build promises valid JSON for every input.
constexpr bool LINES_ARE_JSON = JSPDLOG_UTF8_POLICY != JSPDLOG_UTF8_PASS;
#endif

// The line must be one object, and where the build promises valid JSON, the decoded string must equal the expected
// text.
void check_line(const std::string &line, const std::string &expected_text, const std::string &path)
{
    INFO(path << ": " << line);
    CHECK(line.find('\n') == std::string::npos);
#ifdef JSPDLOG_TEST_NLOHMANN
    if constexpr (LINES_ARE_JSON)
    {
        const auto parsed = nlohmann::json::parse(line, nullptr, false);
        REQUIRE(parsed.is_object());
        // Each path writes the text under exactly one key or as exactly one value, found by the caller.
        CHECK(line.find(expected_text) != std::string::npos);
    }
#endif
}

} // namespace

TEST_CASE("utf8: the build's policy on every path", "[utf8]")
{
    for (const utf8_case &c : CASES)
    {
        const std::string text = expected_for(c);
        const std::string input(c.input);
        INFO("input: " << input);

        const fixture f;
        // MSVC 19.44 mis-stringifies raw strings with backslashes inside Catch2 macros, so the lines are built first.
        const std::string head = R"({"logger":"t","level":"info",)";

        // "a value, in a braced list, a lazy value, a fields object and bound context"
        {
            const std::string expected = head + "\"k\":\"" + text + "\"}";
            f.logger.info({"k", c.input});
            CHECK(f.last() == expected);
            check_line(f.last(), text, "braced list value");

            f.logger.info({"k", [&] { return input; }});
            CHECK(f.last() == expected);

            f.logger.info(jspdlog::fields{"k", c.input});
            CHECK(f.last() == expected);

            f.logger.with(jspdlog::fields{"k", c.input}).info(jspdlog::fields{});
            CHECK(f.last() == expected);

            f.logger.with({"k", c.input}).info(jspdlog::fields{});
            CHECK(f.last() == expected);
        }
        // "a key, in a braced list, a fields object and bound context"
        {
            const std::string expected = head + "\"" + text + "\":1}";
            f.logger.info({c.input, 1});
            CHECK(f.last() == expected);
            check_line(f.last(), text, "braced list key");

            f.logger.info(jspdlog::fields{c.input, 1});
            CHECK(f.last() == expected);

            f.logger.with(jspdlog::fields{c.input, 1}).info(jspdlog::fields{});
            CHECK(f.last() == expected);
        }
        // "a message, as a format argument and as a runtime string"
        {
            const std::string expected = head + "\"message\":\"" + text + "\"}";
            f.logger.info("{}", c.input);
            CHECK(f.last() == expected);
            check_line(f.last(), text, "format argument");

            f.logger.info(input);
            CHECK(f.last() == expected);
        }
        // "the logger name"
        {
            const fixture named(input);
            named.logger.info("m");
            const std::string expected = R"({"logger":")" + text + R"(","level":"info","message":"m"})";
            CHECK(named.last() == expected);
            check_line(named.last(), text, "logger name");
        }
        // "a foreign payload"
        {
            const std::string expected = head + "\"message\":\"" + text + "\"}";
            f.logger.spdlog_logger()->info(input);
            CHECK(f.last() == expected);
            check_line(f.last(), text, "foreign payload");
        }
        // "the call site"
        {
            auto opts = deterministic_options();
            opts.source_key = "src";
            const fixture sourced("t", opts);
            const std::string function = input;
            sourced.logger.spdlog_logger()->log(
                spdlog::source_loc{"f.cpp", 7, function.c_str()}, jspdlog::level::info, "m"
            );
            const std::string expected = R"({"logger":"t","level":"info","src":{"file":"f.cpp","line":7,"function":")" +
                                         text + R"("},"message":"m"})";
            CHECK(sourced.last() == expected);
            check_line(sourced.last(), text, "function");

            // The file name, which is cut after the last slash first.
            const std::string file = "dir/" + input;
            sourced.logger.spdlog_logger()->log(spdlog::source_loc{file.c_str(), 7, "fn"}, jspdlog::level::info, "m");
            const std::string expected_file = R"({"logger":"t","level":"info","src":{"file":")" + text +
                                              R"(","line":7,"function":"fn"},"message":"m"})";
            CHECK(sourced.last() == expected_file);
            check_line(sourced.last(), text, "file");
        }
        // "a key in the options"
        {
            auto opts = deterministic_options();
            opts.logger_key = input;
            const fixture keyed("t", opts);
            keyed.logger.info("m");
            const std::string expected = "{\"" + text + "\":\"t\",\"level\":\"info\",\"message\":\"m\"}";
            CHECK(keyed.last() == expected);
            check_line(keyed.last(), text, "option key");
        }
        // "a level name"
        {
            auto opts = deterministic_options();
            opts.level_names = {{input, input, input, input, input, input, input}};
            const fixture named("t", opts);
            named.logger.info("m");
            const std::string expected = R"({"logger":"t","level":")" + text + R"(","message":"m"})";
            CHECK(named.last() == expected);
        }
    }
}

TEST_CASE("utf8: a long text with invalid bytes in the middle", "[utf8]")
{
    // The invalid bytes sit past the point where short texts end, in the middle of a run of valid text.
    const std::string prefix(1000, 'a');
    const std::string suffix = "\xC3\xA9" + std::string(1000, 'b');
    const std::string input = prefix + "\xFF" + suffix;
    const fixture f;
    f.logger.info(input);
#if JSPDLOG_UTF8_POLICY == JSPDLOG_UTF8_REPLACE
    const std::string bad = "\xEF\xBF\xBD";
#elif JSPDLOG_UTF8_POLICY == JSPDLOG_UTF8_ESCAPE
    const std::string bad = "\\u00ff";
#else
    const std::string bad = "\xFF";
#endif
    const std::string expected = R"({"logger":"t","level":"info","message":")" + prefix + bad + suffix + "\"}";
    CHECK(f.last() == expected);
}

TEST_CASE("utf8: raw_json is written unchanged", "[utf8]")
{
    const fixture f;
    f.logger.info({"k", jspdlog::raw_json{"\"\xFF\""}});
    const std::string expected = "{\"logger\":\"t\",\"level\":\"info\",\"k\":\"\xFF\"}";
    CHECK(f.last() == expected);
}

TEST_CASE("utf8: the separators U+2028 and U+2029", "[utf8]")
{
    const fixture f;
    f.logger.info("a\xE2\x80\xA8z\xE2\x80\xA9y");
#if JSPDLOG_ESCAPE_LINE_SEPARATORS
    // Not a raw string: MSVC reads a backslash-u sequence in a raw string as the character it names.
    const std::string expected = "{\"logger\":\"t\",\"level\":\"info\",\"message\":\"a\\u2028z\\u2029y\"}";
#else
    const std::string expected = "{\"logger\":\"t\",\"level\":\"info\",\"message\":\"a\xE2\x80\xA8z\xE2\x80\xA9y\"}";
#endif
    CHECK(f.last() == expected);

    // Neighbors of the separators are not separators.
    f.logger.info("\xE2\x80\xA7\xE2\x80\xAA");
    const std::string neighbors = "{\"logger\":\"t\",\"level\":\"info\",\"message\":\"\xE2\x80\xA7\xE2\x80\xAA\"}";
    CHECK(f.last() == neighbors);
}

#ifdef JSPDLOG_TEST_NLOHMANN
TEST_CASE("utf8: the default build decodes to the replaced text", "[utf8]")
{
#if JSPDLOG_UTF8_POLICY == JSPDLOG_UTF8_REPLACE
    const fixture f;
    f.logger.info("a\xFFz");
    CHECK(test_support::message_of(f.last()) == "a\xEF\xBF\xBDz");
    f.logger.info({"k\xC3", "v\xE2\x82"}, "m");
    const auto parsed = nlohmann::json::parse(f.last());
    CHECK(parsed.at("k\xEF\xBF\xBD").get<std::string>() == "v\xEF\xBF\xBD");
#endif
}
#endif
