// Ported from the original HID LoggingTest.cpp, adapted to:
//   * the new misuse-proof jspdlog::json_logger constructor
//     (name + spdlog sink instead of LoggingFactory::CreateStringStreamLogger)
//   * with_properties() instead of LoggingFactory::CopyLogger
//   * snake_case API throughout
//   * Catch2 v3 instead of GoogleTest

#include <jspdlog/jspdlog.h>

#include <spdlog/details/null_mutex.h>
#include <spdlog/details/os.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/ostream_sink.h>

#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{

// Builds an ostream-sink-backed json_logger. Trace-level so that every test
// call is emitted. Mirrors the original CreateStringStreamLogger helper.
jspdlog::json_logger make_stream_logger(const std::string &name, std::ostringstream &oss)
{
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger(name, std::move(sink));
    logger.set_level(spdlog::level::trace);
    return logger;
}

std::string add_endline(const std::string &text)
{
    return text + spdlog::details::os::default_eol;
}

bool matches(const std::string &actual, const std::string &pattern)
{
    return std::regex_match(actual, std::regex(pattern));
}

// Regex fragment that matches the ISO-8601 timestamp jspdlog emits. The
// timezone offset uses spdlog's %z, which renders as either `+02:00` or
// `+0200` depending on the platform, so the optional colon stays.
constexpr const char *TIMESTAMP_RE =
    R"([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}[+-][0-9]{2}:?[0-9]{2})";

// Builds a regex matching one full JSON log line. `name` and `level` are
// inserted as literals (caller must regex-escape any special characters);
// `tail` is whatever appears between the `thread` field and the closing
// `}` -- typically a leading comma plus one or more `,"key":value`
// fragments such as `,"message":"hi"` or `,"k":1,"message":"hi"`.
std::string expected_line(const std::string &name, const std::string &level, const std::string &tail)
{
    return add_endline(
        std::string(R"(\{"timestamp":")") + TIMESTAMP_RE + R"(","logger":")" + name + R"(","level":")" + level +
        R"(","process":[0-9]+,"thread":[0-9]+)" + tail + R"(\})"
    );
}

// Sink that always throws on sink_it_, used to drive spdlog's error handler.
// spdlog catches the exception, calls the error handler, and resumes.
class throwing_sink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    explicit throwing_sink(std::string what)
        : what_(std::move(what))
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg & /*msg*/) override { throw std::runtime_error(what_); }
    void flush_() override {}

private:
    std::string what_;
};

} // namespace

TEST_CASE("json_logger: basic print methods", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("BasicPrintMethods", oss);

    logger.info("Test 1");
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "info", R"(,"message":"Test 1")")));
    oss.str({});

    logger.debug(1.23);
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "debug", R"(,"message":"1.23")")));
    oss.str({});

    logger.warn("{} {}", "Test", 3);
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "warning", R"(,"message":"Test 3")")));
    oss.str({});

    logger.info("Test {}", "Test");
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "info", R"(,"message":"Test Test")")));
    oss.str({});

    logger.trace(jspdlog::json_properties{"property", 1.23});
    REQUIRE(matches(oss.str(), expected_line("BasicPrintMethods", "trace", R"(,"property":1.23)")));
    oss.str({});

    logger.error(jspdlog::json_properties{"property1", "abcd", "property2", true}, "{} 4", "Test");
    REQUIRE(matches(
        oss.str(),
        expected_line("BasicPrintMethods", "error", R"(,"property1":"abcd","property2":true,"message":"Test 4")")
    ));
}

TEST_CASE("json_logger: with_properties adds bound properties without affecting parent", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("AddingProperties", oss);
    auto logger1 = root.with_properties({"property1", 321});

    logger1.trace("Test 1");
    REQUIRE(matches(oss.str(), expected_line("AddingProperties", "trace", R"(,"property1":321,"message":"Test 1")")));
    oss.str({});

    {
        auto logger2 = logger1.with_properties({"property2", false});

        logger1.info("Test 2");
        REQUIRE(matches(oss.str(), expected_line("AddingProperties", "info", R"(,"property1":321,"message":"Test 2")"))
        );
        oss.str({});

        logger2.info("Test 3");
        REQUIRE(matches(
            oss.str(),
            expected_line("AddingProperties", "info", R"(,"property1":321,"property2":false,"message":"Test 3")")
        ));
        oss.str({});
    }

    logger1.info("Test 4");
    REQUIRE(matches(oss.str(), expected_line("AddingProperties", "info", R"(,"property1":321,"message":"Test 4")")));
}

TEST_CASE("json_logger: with_properties can override an existing key", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("ReplaceProperties", oss);
    auto logger1 = root.with_properties({"property", 321});
    auto logger2 = logger1.with_properties({"property", false});

    logger1.trace("Test");
    REQUIRE(matches(oss.str(), expected_line("ReplaceProperties", "trace", R"(,"property":321,"message":"Test")")));
    oss.str({});

    logger2.trace("Test");
    REQUIRE(matches(oss.str(), expected_line("ReplaceProperties", "trace", R"(,"property":false,"message":"Test")")));
}

TEST_CASE("json_logger: escapes special characters in messages", "[json_logger]")
{
    std::ostringstream oss;
    auto logger = make_stream_logger("EscapingString", oss);

    logger.info("First line\nsecond line\n\ttabbed line\n\"quoted line\"");
    // Hoisted into a local because MSVC's macro preprocessor mishandles
    // raw strings containing both backslash-quote sequences and our long
    // timestamp regex when stringified by Catch2's REQUIRE.
    const std::string expected = expected_line(
        "EscapingString", "info", R"(,"message":"First line\\nsecond line\\n\\ttabbed line\\n\\\"quoted line\\\"")"
    );
    REQUIRE(matches(oss.str(), expected));
}

TEST_CASE("json_logger: a per-call property can override a bound property", "[json_logger]")
{
    std::ostringstream oss;
    auto root = make_stream_logger("DuplicateProperties", oss);
    auto logger1 = root.with_properties({"property", 321});
    logger1.set_level(spdlog::level::critical);

    logger1.critical("Test 1");
    REQUIRE(
        matches(oss.str(), expected_line("DuplicateProperties", "critical", R"(,"property":321,"message":"Test 1")"))
    );
    oss.str({});

    auto logger2 = logger1.with_properties({"property", false});
    logger2.critical("Test 2");
    REQUIRE(
        matches(oss.str(), expected_line("DuplicateProperties", "critical", R"(,"property":false,"message":"Test 2")"))
    );
    oss.str({});

    logger2.critical({"property", nullptr}, "Test 3");
    REQUIRE(
        matches(oss.str(), expected_line("DuplicateProperties", "critical", R"(,"property":null,"message":"Test 3")"))
    );
}

TEST_CASE("json_logger: logger name with special characters stays valid JSON", "[json_logger]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    // Name contains a literal quote, a backslash and a tab. These must be
    // JSON-escaped where they appear in the pattern, not interpolated raw.
    jspdlog::json_logger logger("weird\"name\\with\ttab", std::move(sink));
    logger.info("hi");

    // Hoist the expected fragment out of the REQUIRE macro: MSVC's stringifier
    // mishandles raw string literals containing backslashes when stringified
    // by the Catch2 expression-capture machinery.
    const std::string expected_name_field = R"("logger":"weird\"name\\with\ttab")";
    REQUIRE(oss.str().find(expected_name_field) != std::string::npos);
}

TEST_CASE("json_logger: logger name with percent signs stays valid JSON", "[json_logger]")
{
    // `%` is the spdlog pattern-flag prefix. Without an extra layer of escaping,
    // a name like "50%off" would cause spdlog to interpret `%o` as a flag (or,
    // worse, names containing `%v`/`%n` would splice the message/name right
    // into the "logger" field and break the JSON structure).
    SECTION("plain percent")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        jspdlog::json_logger logger("50%off", std::move(sink));
        logger.info("hi");
        REQUIRE(oss.str().find(R"("logger":"50%off")") != std::string::npos);
        REQUIRE(oss.str().find(R"("message":"hi")") != std::string::npos);
    }

    SECTION("percent v does not splice the message")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        jspdlog::json_logger logger("svc-%v-edge", std::move(sink));
        logger.info("payload");
        REQUIRE(oss.str().find(R"("logger":"svc-%v-edge")") != std::string::npos);
        // Exactly one occurrence of the message text -- not duplicated into
        // the "logger" field by a spurious %v expansion.
        const std::string out = oss.str();
        const auto first = out.find("payload");
        REQUIRE(first != std::string::npos);
        REQUIRE(out.find("payload", first + 1) == std::string::npos);
    }

    SECTION("percent n does not splice the logger name")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        jspdlog::json_logger logger("a%nb", std::move(sink));
        logger.info("ok");
        REQUIRE(oss.str().find(R"("logger":"a%nb")") != std::string::npos);
    }
}

TEST_CASE("json_logger: pattern is re-applied even when adopting an existing spdlog logger", "[json_logger]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto spdlog_logger = std::make_shared<spdlog::logger>("Adopted", sink);

    // Set a hostile pattern that would otherwise break JSON output.
    spdlog_logger->set_pattern("non-json: %v");
    spdlog_logger->set_level(spdlog::level::trace);

    auto logger = jspdlog::json_logger::adopt(std::move(spdlog_logger));
    logger.info("after adopt");

    REQUIRE(matches(oss.str(), expected_line("Adopted", "info", R"(,"message":"after adopt")")));
}

TEST_CASE("json_logger: set_pattern_time keeps the JSON pattern intact when switching to UTC", "[json_logger]")
{
    // spdlog's set_pattern() implicitly resets the time mode to local. After
    // adopt() (which calls set_pattern internally) the adopted logger has
    // therefore lost any UTC configuration it used to have. set_pattern_time
    // exists to restore that mode without dropping the pinned JSON pattern,
    // so the timestamp offset should now be `+00:00` or `+0000` (and we must
    // not accidentally clobber the JSON structure while doing it). We can't
    // reuse expected_line() here because that helper allows any offset; we
    // specifically want to assert the offset is +00:00 / +0000.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto spdlog_logger = std::make_shared<spdlog::logger>("Utc", sink);
    spdlog_logger->set_level(spdlog::level::trace);

    auto logger = jspdlog::json_logger::adopt(std::move(spdlog_logger));
    logger.set_pattern_time(spdlog::pattern_time_type::utc);
    logger.info("zulu");

    const std::string out = oss.str();
    REQUIRE(matches(
        out,
        add_endline(
            R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\+00:?00","logger":"Utc","level":"info",)"
            R"("process":[0-9]+,"thread":[0-9]+,"message":"zulu"\})"
        )
    ));
}

TEST_CASE("json_logger: adopt accepts a pattern_time_type up front", "[json_logger]")
{
    // The optional time_type argument on adopt() is the one-call equivalent
    // of "adopt then set_pattern_time(utc)" -- callers shouldn't have to
    // remember the second call to keep a UTC-configured logger UTC. Same
    // offset assertion as the set_pattern_time test above.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto spdlog_logger = std::make_shared<spdlog::logger>("AdoptUtc", sink);
    spdlog_logger->set_level(spdlog::level::trace);

    auto logger = jspdlog::json_logger::adopt(std::move(spdlog_logger), spdlog::pattern_time_type::utc);
    logger.info("zulu");

    const std::string out = oss.str();
    REQUIRE(matches(
        out,
        add_endline(
            R"(\{"timestamp":"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\+00:?00","logger":"AdoptUtc","level":"info",)"
            R"("process":[0-9]+,"thread":[0-9]+,"message":"zulu"\})"
        )
    ));
}

TEST_CASE("json_logger: with_properties && mutates an rvalue logger in place", "[json_logger]")
{
    // The rvalue-qualified with_properties() is a chained-construction
    // optimization: composing properties onto a freshly-constructed logger
    // should observably attach them without involving the lvalue-overload's
    // copy step. We can't directly observe the absence of a copy, but we
    // can pin the user-visible contract: a chained call produces a logger
    // that emits both bound properties on every log line.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);

    auto chained =
        jspdlog::json_logger("Chained", sink).with_properties({"a", 1}).with_properties({"b", 2});
    chained.set_level(spdlog::level::trace);
    chained.info("hi");

    REQUIRE(matches(oss.str(), expected_line("Chained", "info", R"(,"a":1,"b":2,"message":"hi")")));
}

TEST_CASE(
    "json_logger: silence_errors disables a previously installed forwarder", "[json_logger][error_handler]"
)
{
    // The only public way to install an error handler is forward_errors_to;
    // silence_errors() is the documented way to disconnect it again. We use
    // the two together to verify both halves: errors land on the destination
    // before silence_errors(), and stop landing after.
    auto throwing = std::make_shared<throwing_sink>("disk full");
    jspdlog::json_logger source("ErrHandler", std::move(throwing));
    source.set_level(spdlog::level::trace);

    std::ostringstream dest_oss;
    auto dest = make_stream_logger("Captor", dest_oss);
    source.forward_errors_to(dest);

    source.info("first");
    const std::string after_first = dest_oss.str();
    REQUIRE(after_first.find("disk full") != std::string::npos);

    source.silence_errors();
    source.info("second");
    REQUIRE(dest_oss.str() == after_first);
}

TEST_CASE(
    "json_logger: forward_errors_to surfaces sink errors as JSON warn lines on the destination",
    "[json_logger][error_handler]"
)
{
    // Source's sink throws on every write; destination is a normal stream sink
    // we can inspect.
    auto throwing = std::make_shared<throwing_sink>("connection reset");
    jspdlog::json_logger source("ProducerLogger", std::move(throwing));
    source.set_level(spdlog::level::trace);

    std::ostringstream dest_oss;
    auto dest = make_stream_logger("FailureLogger", dest_oss);

    source.forward_errors_to(dest);

    source.info("original message");

    const std::string out = dest_oss.str();
    REQUIRE(matches(
        out,
        expected_line(
            "FailureLogger", "warning", R"(,"source":"ProducerLogger","message":"[^"]*connection reset[^"]*")"
        )
    ));
}

TEST_CASE("json_logger: forward_errors_to snapshots the destination at call time", "[json_logger][error_handler]")
{
    auto throwing = std::make_shared<throwing_sink>("oops");
    jspdlog::json_logger source("Producer", std::move(throwing));

    std::ostringstream oss;
    auto dest = make_stream_logger("Sink", oss);

    source.forward_errors_to(dest);

    // Mutating the original `dest` after wiring up forwarding must not affect
    // what the forwarder writes (the lambda holds its own copy).
    auto enriched = dest.with_properties({"environment", "test"});
    (void)enriched;

    source.info("trigger");

    const std::string out = oss.str();
    REQUIRE(out.find(R"("source":"Producer")") != std::string::npos);
    REQUIRE(out.find(R"("environment")") == std::string::npos);
}

TEST_CASE(
    "json_logger: forward_errors_to tolerates a destination that shares a sink with the source",
    "[json_logger][error_handler]"
)
{
    // Sharing a sink is supported (only sharing the underlying spdlog::logger
    // is forbidden -- see the next test case). Source's failing sink throws,
    // its err_helper invokes the forwarder, the forwarder calls dest.warn,
    // dest's spdlog::logger writes to the same throwing sink and calls *its
    // own* err_helper (which has no custom handler installed). No recursion,
    // no deadlock; the call simply returns.
    auto throwing = std::make_shared<throwing_sink>("recursive boom");
    jspdlog::json_logger source("Source", throwing);
    const jspdlog::json_logger destination("Dest", throwing);

    source.forward_errors_to(destination);

    REQUIRE_NOTHROW(source.info("trigger"));
}

// ============================================================================
// json_pattern_options: customize/omit fixed header fields.
// ============================================================================

TEST_CASE("json_pattern_options: renaming fields keeps the JSON line valid", "[json_logger][pattern_options]")
{
    // Renaming individual fields must produce a structurally valid JSON line
    // with the new keys in the same fixed order as before. Field values stay
    // unchanged: timestamp is still ISO-8601, level is still the spdlog
    // string, process/thread are still numeric.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_pattern_options opts;
    opts.timestamp = "ts";
    opts.logger = "svc";
    opts.level = "lvl";
    opts.process = "pid";
    opts.thread = "tid";
    jspdlog::json_logger logger("RenamedFields", std::move(sink), opts);
    logger.set_level(spdlog::level::trace);

    logger.info("hi");
    REQUIRE(matches(
        oss.str(),
        add_endline(
            std::string(R"(\{"ts":")") + TIMESTAMP_RE + R"(","svc":"RenamedFields","lvl":"info",)" +
            R"("pid":[0-9]+,"tid":[0-9]+,"message":"hi"\})"
        )
    ));
}

TEST_CASE("json_pattern_options: omitting a single field drops it from the output", "[json_logger][pattern_options]")
{
    // Setting a field to std::nullopt must remove it entirely from the
    // emitted line; the surrounding fields keep their key names and the
    // ones still present remain comma-separated correctly (no stray
    // double-commas or trailing commas).
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_pattern_options opts;
    opts.process = std::nullopt;
    jspdlog::json_logger logger("NoProcess", std::move(sink), opts);
    logger.set_level(spdlog::level::trace);

    logger.info("hi");
    const std::string out = oss.str();
    REQUIRE(matches(
        out,
        add_endline(
            std::string(R"(\{"timestamp":")") + TIMESTAMP_RE +
            R"(","logger":"NoProcess","level":"info","thread":[0-9]+,"message":"hi"\})"
        )
    ));
    REQUIRE(out.find("\"process\"") == std::string::npos);
}

TEST_CASE(
    "json_pattern_options: omitting every fixed field still produces valid JSON",
    "[json_logger][pattern_options]"
)
{
    // The "remove everything" corner case: with no fixed fields the pinned
    // pattern collapses to `{%v}`, and json_logger has to skip the leading
    // comma on the %v fragment that would otherwise turn the line into
    // `{,...}` (invalid JSON).
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_pattern_options opts;
    opts.timestamp = std::nullopt;
    opts.logger = std::nullopt;
    opts.level = std::nullopt;
    opts.process = std::nullopt;
    opts.thread = std::nullopt;
    jspdlog::json_logger logger("Stripped", std::move(sink), opts);
    logger.set_level(spdlog::level::trace);

    SECTION("message only")
    {
        logger.info("hi");
        REQUIRE(oss.str() == add_endline(R"({"message":"hi"})"));
    }

    SECTION("properties only")
    {
        logger.info(jspdlog::json_properties{"k", 1});
        REQUIRE(oss.str() == add_endline(R"({"k":1})"));
    }

    SECTION("properties plus message")
    {
        logger.warn(jspdlog::json_properties{"k", 1}, "hi");
        REQUIRE(oss.str() == add_endline(R"({"k":1,"message":"hi"})"));
    }

    SECTION("bound properties only, no message")
    {
        auto bound = logger.with_properties({"app", "x"});
        bound.info(jspdlog::json_properties{});
        REQUIRE(oss.str() == add_endline(R"({"app":"x"})"));
    }

    SECTION("bound properties + message routed through log_message_(level, msg)")
    {
        // Hits the third leading-comma branch: `cached_properties_` is non-
        // empty AND we go through the props-less log_message_ overload, so
        // the assembled fragment is `,"app":"x","message":"hi"` and
        // trim_leading_comma_ has to strip the first byte to keep the line
        // structurally valid.
        auto bound = logger.with_properties({"app", "x"});
        bound.info("hi");
        REQUIRE(oss.str() == add_endline(R"({"app":"x","message":"hi"})"));
    }

    SECTION("empty properties-only call produces an empty object")
    {
        logger.info(jspdlog::json_properties{});
        REQUIRE(oss.str() == add_endline(R"({})"));
    }
}

TEST_CASE("json_pattern_options: keys with special characters are JSON-escaped", "[json_logger][pattern_options]")
{
    // User-supplied keys go through the same JSON-escape + spdlog-pattern
    // hardening as the logger name: quotes, backslashes, control characters
    // and percent signs must all be safe.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_pattern_options opts;
    opts.timestamp = std::nullopt;
    opts.logger = std::nullopt;
    opts.level = std::nullopt;
    opts.process = std::nullopt;
    opts.thread = "weird\"key\\with\ttab%v";
    jspdlog::json_logger logger("Esc", std::move(sink), opts);
    logger.set_level(spdlog::level::trace);

    logger.info("hi");

    // Hoisted out of the REQUIRE because MSVC's stringifier mishandles raw
    // string literals with backslashes inside Catch2's expression capture.
    const std::string expected_key = R"("weird\"key\\with\ttab%v")";
    const std::string out = oss.str();
    REQUIRE(out.find(expected_key) != std::string::npos);
    REQUIRE(out.find("\"message\":\"hi\"") != std::string::npos);
    // The %v in the key must NOT have spliced the message into the key name.
    REQUIRE(out.find("\"weird\\\"key\\\\with\\ttabhi") == std::string::npos);
}

TEST_CASE("json_pattern_options: properties combine correctly with omitted fields", "[json_logger][pattern_options]")
{
    // With only timestamp kept, the `%v` fragment still needs its leading
    // comma. Bound + per-call properties exercise the merge path; the
    // result must remain valid JSON with properties separated correctly.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_pattern_options opts;
    opts.logger = std::nullopt;
    opts.level = std::nullopt;
    opts.process = std::nullopt;
    opts.thread = std::nullopt;
    jspdlog::json_logger root("Combo", std::move(sink), opts);
    root.set_level(spdlog::level::trace);

    auto child = root.with_properties({"app", "svc", "v", 2});
    child.warn(jspdlog::json_properties{"v", 3, "extra", true}, "hi");

    REQUIRE(matches(
        oss.str(),
        add_endline(
            std::string(R"(\{"timestamp":")") + TIMESTAMP_RE +
            R"(","app":"svc","extra":true,"v":3,"message":"hi"\})"
        )
    ));
}

TEST_CASE(
    "json_pattern_options: set_pattern_time and adopt preserve customization", "[json_logger][pattern_options]"
)
{
    // Pattern reapplication paths (set_pattern_time + the adopt() overload
    // that takes options) must keep using the configured options rather
    // than silently reverting to the defaults. We assert both the renamed
    // key and the UTC offset are present after each mutation.
    SECTION("set_pattern_time keeps custom keys")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        jspdlog::json_pattern_options opts;
        opts.timestamp = "ts";
        jspdlog::json_logger logger("KeepOpts", std::move(sink), opts);
        logger.set_level(spdlog::level::trace);
        logger.set_pattern_time(spdlog::pattern_time_type::utc);

        logger.info("hi");
        const std::string out = oss.str();
        REQUIRE(out.find(R"("ts":")") != std::string::npos);
        const bool has_utc_offset = out.find(R"(+00:00")") != std::string::npos ||
                                    out.find(R"(+0000")") != std::string::npos;
        REQUIRE(has_utc_offset);
    }

    SECTION("adopt(options) installs the customization on an adopted logger")
    {
        std::ostringstream oss;
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
        auto inner = std::make_shared<spdlog::logger>("Adopted", sink);
        inner->set_pattern("non-json: %v");
        inner->set_level(spdlog::level::trace);

        jspdlog::json_pattern_options opts;
        opts.timestamp = "ts";
        opts.process = std::nullopt;
        auto logger = jspdlog::json_logger::adopt(std::move(inner), opts);

        logger.info("hi");
        const std::string out = oss.str();
        REQUIRE(matches(
            out,
            add_endline(
                std::string(R"(\{"ts":")") + TIMESTAMP_RE +
                R"(","logger":"Adopted","level":"info","thread":[0-9]+,"message":"hi"\})"
            )
        ));
        REQUIRE(out.find("\"process\"") == std::string::npos);
    }
}

TEST_CASE("json_logger: pattern_time() reflects the persisted mode", "[json_logger]")
{
    // The new accessor lets callers (and tests) round-trip the time mode
    // without having to inspect spdlog directly. Defaults to local for
    // public constructors and adopt(); set_pattern_time and adopt(time_type)
    // both update the persisted value.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);

    SECTION("constructor defaults to local")
    {
        const jspdlog::json_logger logger("LocalDefault", sink);
        REQUIRE(logger.pattern_time() == spdlog::pattern_time_type::local);
    }

    SECTION("set_pattern_time persists the new value")
    {
        jspdlog::json_logger logger("Switcher", sink);
        logger.set_pattern_time(spdlog::pattern_time_type::utc);
        REQUIRE(logger.pattern_time() == spdlog::pattern_time_type::utc);

        logger.set_pattern_time(spdlog::pattern_time_type::local);
        REQUIRE(logger.pattern_time() == spdlog::pattern_time_type::local);
    }

    SECTION("adopt(logger, utc) persists utc")
    {
        auto inner = std::make_shared<spdlog::logger>("AdoptedUtc", sink);
        const auto adopted = jspdlog::json_logger::adopt(std::move(inner), spdlog::pattern_time_type::utc);
        REQUIRE(adopted.pattern_time() == spdlog::pattern_time_type::utc);
    }

    SECTION("adopt(logger, options) defaults to local")
    {
        auto inner = std::make_shared<spdlog::logger>("AdoptedOpts", sink);
        const auto adopted = jspdlog::json_logger::adopt(std::move(inner), jspdlog::json_pattern_options{});
        REQUIRE(adopted.pattern_time() == spdlog::pattern_time_type::local);
    }
}

// ============================================================================
// Disambiguation tests: pin the two adopt() overloads against each other so
// a future refactor doesn't silently collapse them or break overload
// resolution at the explicitly-typed call sites that users actually write.
// (The brace-init form `adopt(logger, {})` is intentionally NOT tested
// because it's documented as ambiguous -- failing to compile there is the
// contract.)
// ============================================================================

TEST_CASE("json_logger: adopt(logger, pattern_time_type) is unambiguous when the type is explicit", "[json_logger]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto inner = std::make_shared<spdlog::logger>("Explicit", sink);
    auto adopted = jspdlog::json_logger::adopt(std::move(inner), spdlog::pattern_time_type::utc);
    REQUIRE(adopted.pattern_time() == spdlog::pattern_time_type::utc);
}

TEST_CASE("json_logger: adopt(logger, json_pattern_options) is unambiguous when the type is explicit", "[json_logger]")
{
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    auto inner = std::make_shared<spdlog::logger>("Explicit", sink);
    jspdlog::json_pattern_options opts;
    opts.timestamp = "ts";
    auto adopted = jspdlog::json_logger::adopt(std::move(inner), opts);
    adopted.set_level(spdlog::level::trace);
    adopted.info("hi");
    REQUIRE(oss.str().find(R"("ts":")") != std::string::npos);
}

// ============================================================================
// set_eol: cross-platform line terminator override.
// ============================================================================

TEST_CASE("json_logger: set_eol forces a custom line terminator across platforms", "[json_logger][eol]")
{
    // spdlog's default eol is platform-specific ("\r\n" on Windows, "\n"
    // elsewhere). set_eol("\n") must produce LF-only on every host so a
    // cross-platform JSON-lines consumer never sees stray carriage returns.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("Eol", std::move(sink));
    logger.set_level(spdlog::level::trace);
    logger.set_eol("\n");

    logger.info("hi");
    const std::string out = oss.str();
    REQUIRE_FALSE(out.empty());
    REQUIRE(out.back() == '\n');
    REQUIRE(out.find('\r') == std::string::npos);
    REQUIRE(logger.eol().has_value());
    REQUIRE(*logger.eol() == "\n");
}

TEST_CASE("json_logger: set_eol(\"\") suppresses the line terminator entirely", "[json_logger][eol]")
{
    // Edge case: the empty eol is occasionally useful for sinks that frame
    // their own delimiters. The JSON line itself must still be intact -- no
    // trailing characters at all, just the closing `}`.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("Eol", std::move(sink));
    logger.set_level(spdlog::level::trace);
    logger.set_eol("");

    logger.info("hi");
    const std::string out = oss.str();
    REQUIRE_FALSE(out.empty());
    REQUIRE(out.back() == '}');
}

TEST_CASE("json_logger: set_eol survives set_pattern_time reapplication", "[json_logger][eol]")
{
    // set_pattern_time triggers an internal pattern reinstallation; the eol
    // override is persisted on the json_logger and must survive that, just
    // like the time mode survives.
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("EolKeep", std::move(sink));
    logger.set_level(spdlog::level::trace);
    logger.set_eol("\n");
    logger.set_pattern_time(spdlog::pattern_time_type::utc);

    logger.info("hi");
    const std::string out = oss.str();
    REQUIRE(out.back() == '\n');
    REQUIRE(out.find('\r') == std::string::npos);
}

TEST_CASE("json_logger: set_eol(nullopt) returns to spdlog's platform default", "[json_logger][eol]")
{
    // After clearing the override, output must match the default add_endline
    // string (which itself queries spdlog::details::os::default_eol).
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    jspdlog::json_logger logger("EolReset", std::move(sink));
    logger.set_level(spdlog::level::trace);
    logger.set_eol("\n");
    logger.set_eol(std::nullopt);

    logger.info("hi");
    REQUIRE(matches(oss.str(), expected_line("EolReset", "info", R"(,"message":"hi")")));
    REQUIRE_FALSE(logger.eol().has_value());
}
