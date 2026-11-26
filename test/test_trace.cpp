// Trace context: the traceparent parser and the fields it is bound as, with the names and value formats of
// OpenTelemetry, ECS, Google Cloud Logging and Datadog.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using jspdlog::parse_traceparent;
using jspdlog::trace_context;

namespace
{

// The example header of the W3C Trace Context specification.
constexpr std::string_view HEADER = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";

static_assert(noexcept(parse_traceparent(HEADER)), "parse_traceparent must not throw");

trace_context example_context()
{
    const trace_context ctx = parse_traceparent(HEADER).value_or(trace_context{});
    REQUIRE(ctx.valid());
    return ctx;
}

// HEADER with the text at one position replaced.
std::string with_text_at(std::size_t at, std::string_view text)
{
    std::string header{HEADER};
    header.replace(at, text.size(), text);
    return header;
}

} // namespace

TEST_CASE("trace: parse_traceparent reads the ids and the flags", "[trace]")
{
    const trace_context ctx = example_context();
    const std::array<std::uint8_t, 16> trace_id{
        0x4b, 0xf9, 0x2f, 0x35, 0x77, 0xb3, 0x4d, 0xa6, 0xa3, 0xce, 0x92, 0x9d, 0x0e, 0x0e, 0x47, 0x36
    };
    const std::array<std::uint8_t, 8> span_id{0x00, 0xf0, 0x67, 0xaa, 0x0b, 0xa9, 0x02, 0xb7};
    CHECK(ctx.trace_id == trace_id);
    CHECK(ctx.span_id == span_id);
    CHECK(ctx.trace_flags == 0x01);
    CHECK(ctx.valid());
}

TEST_CASE("trace: parse_traceparent keeps every flag bit", "[trace]")
{
    for (const auto &[text, flags] : {std::pair{"00", 0x00}, std::pair{"02", 0x02}, std::pair{"ff", 0xff}})
    {
        const trace_context ctx = parse_traceparent(with_text_at(53, text)).value_or(trace_context{});
        REQUIRE(ctx.valid());
        CHECK(ctx.trace_flags == flags);
    }
}

TEST_CASE("trace: parse_traceparent accepts ids with a single non-zero digit", "[trace]")
{
    const trace_context ctx =
        parse_traceparent("00-00000000000000000000000000000001-0000000000000001-00").value_or(trace_context{});
    REQUIRE(ctx.valid());
    CHECK(ctx.trace_id.back() == 1);
    CHECK(ctx.span_id.back() == 1);
}

TEST_CASE("trace: parse_traceparent rejects every other form", "[trace]")
{
    const std::string header{HEADER};
    const std::string invalid[] = {
        "",
        "00",
        "not a traceparent",
        with_text_at(0, "01"), // a later version
        with_text_at(0, "ff"), // the forbidden version
        with_text_at(0, "0x"),
        with_text_at(2, "_"), // wrong separators
        with_text_at(35, "_"),
        with_text_at(52, "0"),
        with_text_at(3, "4BF92F"), // uppercase hex in each part
        with_text_at(36, "00F067"),
        with_text_at(53, "0A"),
        with_text_at(3, "g"), // not hex, at both ends of each part
        with_text_at(34, "g"),
        with_text_at(36, " "),
        with_text_at(51, "-"),
        with_text_at(53, "x"),
        with_text_at(54, "x"),
        with_text_at(10, std::string_view{"\0", 1}),
        with_text_at(3, std::string(32, '0')),  // an all-zero trace id
        with_text_at(36, std::string(16, '0')), // an all-zero span id
        header.substr(0, 54),                   // too short
        header.substr(1),
        header + "0", // too long
        header + "-",
        header + "-extra", // trailing data
        " " + header,
        header + " ",
        "00-4bf92f3577b34da6a3ce929d0e0e473-600f067aa0ba902b7-01", // ids of the wrong lengths
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b-701",
    };
    for (const std::string &text : invalid)
    {
        CAPTURE(text);
        CHECK_FALSE(parse_traceparent(text).has_value());
    }
}

TEST_CASE("trace: parse_traceparent reads no further than the string view", "[trace]")
{
    const std::string longer = std::string{HEADER} + "-and more";
    CHECK(parse_traceparent(std::string_view{longer}.substr(0, HEADER.size())).has_value());
    CHECK_FALSE(parse_traceparent(HEADER.substr(0, HEADER.size() - 1)).has_value());
}

TEST_CASE("trace: valid needs both ids", "[trace]")
{
    trace_context ctx;
    CHECK_FALSE(ctx.valid());
    ctx.trace_flags = 1;
    CHECK_FALSE(ctx.valid());
    ctx.trace_id[0] = 1;
    CHECK_FALSE(ctx.valid());
    ctx.span_id[7] = 1;
    CHECK(ctx.valid());
    ctx.trace_id[0] = 0;
    CHECK_FALSE(ctx.valid());
}

TEST_CASE("trace: trace_fields uses the OpenTelemetry names", "[trace]")
{
    CHECK(
        jspdlog::trace_fields(example_context()).to_json() ==
        R"({"trace_id":"4bf92f3577b34da6a3ce929d0e0e4736","span_id":"00f067aa0ba902b7","trace_flags":"01"})"
    );
}

TEST_CASE("trace: trace_keys renames the fields and can leave the flags out", "[trace]")
{
    jspdlog::trace_keys keys;
    keys.trace_id = "traceId";
    keys.span_id = "span \"id\""; // escaped like any key
    keys.trace_flags = std::nullopt;
    // A local, because MSVC mis-stringifies raw literals with backslashes inside assertion macros.
    const std::string expected = R"({"traceId":"4bf92f3577b34da6a3ce929d0e0e4736","span \"id\"":"00f067aa0ba902b7"})";
    CHECK(jspdlog::trace_fields(example_context(), keys).to_json() == expected);
}

TEST_CASE("trace: ecs_trace_fields writes trace.id and span.id", "[trace]")
{
    CHECK(
        jspdlog::presets::ecs_trace_fields(example_context()).to_json() ==
        R"({"trace.id":"4bf92f3577b34da6a3ce929d0e0e4736","span.id":"00f067aa0ba902b7"})"
    );
}

TEST_CASE("trace: gcp_trace_fields writes the bare trace id by default", "[trace]")
{
    CHECK(
        jspdlog::presets::gcp_trace_fields(example_context()).to_json() ==
        R"({"logging.googleapis.com/trace":"4bf92f3577b34da6a3ce929d0e0e4736",)"
        R"("logging.googleapis.com/spanId":"00f067aa0ba902b7","logging.googleapis.com/trace_sampled":true})"
    );
}

TEST_CASE("trace: gcp_trace_fields names the project when given one", "[trace]")
{
    CHECK(
        jspdlog::presets::gcp_trace_fields(example_context(), "my-project").to_json() ==
        R"({"logging.googleapis.com/trace":"projects/my-project/traces/4bf92f3577b34da6a3ce929d0e0e4736",)"
        R"("logging.googleapis.com/spanId":"00f067aa0ba902b7","logging.googleapis.com/trace_sampled":true})"
    );
}

TEST_CASE("trace: gcp_trace_fields takes the sampled decision from bit 0 alone", "[trace]")
{
    trace_context ctx = example_context();
    ctx.trace_flags = 0xfe;
    CHECK(
        jspdlog::presets::gcp_trace_fields(ctx).to_json().find(R"("logging.googleapis.com/trace_sampled":false)") !=
        std::string::npos
    );
    ctx.trace_flags = 0x03;
    CHECK(
        jspdlog::presets::gcp_trace_fields(ctx).to_json().find(R"("logging.googleapis.com/trace_sampled":true)") !=
        std::string::npos
    );
}

TEST_CASE("trace: datadog_trace_fields writes the whole ids in hex", "[trace]")
{
    CHECK(
        jspdlog::presets::datadog_trace_fields(example_context()).to_json() ==
        R"({"trace_id":"4bf92f3577b34da6a3ce929d0e0e4736","span_id":"00f067aa0ba902b7"})"
    );
}

TEST_CASE("trace: a context that is not valid gives no fields", "[trace]")
{
    trace_context ctx = example_context();
    ctx.span_id = {};
    CHECK(jspdlog::trace_fields(ctx).empty());
    CHECK(jspdlog::presets::ecs_trace_fields(ctx).empty());
    CHECK(jspdlog::presets::gcp_trace_fields(ctx, "my-project").empty());
    CHECK(jspdlog::presets::datadog_trace_fields(ctx).empty());
    CHECK(jspdlog::trace_fields(trace_context{}).empty());
}

TEST_CASE("trace: trace fields bind to a child logger", "[trace]")
{
    const test_support::fixture f;
    const auto child = f.logger.with(jspdlog::trace_fields(example_context()));
    child.info({"user", 42}, "handling request");
    CHECK(
        f.last() == R"({"logger":"t","level":"info","trace_id":"4bf92f3577b34da6a3ce929d0e0e4736",)"
                    R"("span_id":"00f067aa0ba902b7","trace_flags":"01","user":42,"message":"handling request"})"
    );
    f.logger.info("without it");
    CHECK(f.last() == R"({"logger":"t","level":"info","message":"without it"})");
}
