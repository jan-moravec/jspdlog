// The error handler made by jspdlog::make_error_handler: it reports a failure as one JSON line without calling back
// into spdlog. These tests run under a CTest timeout, so a handler that deadlocks shows up as a timeout.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <spdlog/sinks/base_sink.h>

#include <catch2/catch_test_macros.hpp>

#if JSPDLOG_SPDLOG_V2
#include <spdlog/sinks/async_sink.h>
#else
#include <spdlog/async.h>
#endif

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

using test_support::capture_sink;
using test_support::deterministic_options;

namespace
{

// A sink that throws on every line, standing in for a full disk.
class failing_sink final : public spdlog::sinks::base_sink<std::mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg & /*msg*/) override { throw spdlog::spdlog_ex("disk full"); }
    void flush_() override {}
};

// Whether a report line is {"logger":"app","level":"error",<extra>"message":"disk full"}. spdlog v1 appends the call
// site, " [file(line)]", to the message of a call that has one, so there the message only has to start with the text.
bool is_disk_full_report(const std::string &line, const std::string &extra = {})
{
    const std::string head = R"({"logger":"app","level":"error",)" + extra + R"("message":"disk full)";
    const std::string tail = R"("})";
#if JSPDLOG_SPDLOG_V2
    return line == head + tail;
#else
    return line.size() >= head.size() + tail.size() && line.compare(0, head.size(), head) == 0 &&
           line.compare(line.size() - tail.size(), tail.size(), tail) == 0;
#endif
}

// std::tmpfile is deprecated on MSVC, which has tmpfile_s.
std::FILE *open_temp_file()
{
#ifdef _MSC_VER
    std::FILE *file = nullptr;
    return tmpfile_s(&file) == 0 ? file : nullptr;
#else
    return std::tmpfile();
#endif
}

// A capture sink with a json_formatter, which receives the reports.
std::shared_ptr<capture_sink> make_report_sink()
{
    auto report = std::make_shared<capture_sink>();
    jspdlog::install_json_formatter(*report, deterministic_options());
    return report;
}

jspdlog::error_report_options report_to(
    const std::shared_ptr<capture_sink> &report, std::chrono::milliseconds min_interval = std::chrono::milliseconds{0}
)
{
    jspdlog::error_report_options options;
    options.format = deterministic_options();
    options.sink = report;
    options.min_interval = min_interval;
    return options;
}

} // namespace

TEST_CASE("error handler: reports a failing sink as one JSON line and does not throw", "[error_handler]")
{
    const auto failing = std::make_shared<failing_sink>();
    const auto report = make_report_sink();
    const jspdlog::json_logger logger("app", failing, deterministic_options());
    logger.spdlog_logger()->set_error_handler(jspdlog::make_error_handler(logger.name(), report_to(report)));

    CHECK_NOTHROW(logger.info("x"));
    REQUIRE(report->lines.size() == 1);
    CHECK(is_disk_full_report(report->last()));
    CHECK(test_support::is_single_json_object(report->last()));
}

TEST_CASE("error handler: the message is escaped", "[error_handler]")
{
    const auto report = make_report_sink();
    const auto handler = jspdlog::make_error_handler("app", report_to(report));
    handler("quote \" and\nnewline");
    REQUIRE(report->lines.size() == 1);
    const std::string expected = R"({"logger":"app","level":"error","message":"quote \" and\nnewline"})";
    CHECK(report->last() == expected);
}

TEST_CASE("error handler: reports closer together than min_interval are counted", "[error_handler]")
{
    SECTION("within the interval, one line")
    {
        const auto failing = std::make_shared<failing_sink>();
        const auto report = make_report_sink();
        const jspdlog::json_logger logger("app", failing, deterministic_options());
        logger.spdlog_logger()->set_error_handler(
            jspdlog::make_error_handler(logger.name(), report_to(report, std::chrono::hours{1}))
        );
        logger.info("a");
        logger.info("b");
        logger.info("c");
        CHECK(report->lines.size() == 1);
    }
    SECTION("the next report carries the number of dropped ones")
    {
        const auto failing = std::make_shared<failing_sink>();
        const auto report = make_report_sink();
        const jspdlog::json_logger logger("app", failing, deterministic_options());
        const std::chrono::milliseconds interval{200};
        logger.spdlog_logger()->set_error_handler(
            jspdlog::make_error_handler(logger.name(), report_to(report, interval))
        );
        logger.info("a");
        logger.info("b");
        logger.info("c");
        REQUIRE(report->lines.size() == 1);
        CHECK(is_disk_full_report(report->lines[0]));

        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < interval + std::chrono::milliseconds{20})
        {
        }
        logger.info("d");
        REQUIRE(report->lines.size() == 2);
        CHECK(is_disk_full_report(report->lines[1], R"("dropped":2,)"));
        CHECK(test_support::is_single_json_object(report->lines[1]));
    }
}

TEST_CASE("error handler: a report sink that fails too does not recurse or hang", "[error_handler]")
{
    // The report goes to the sink that fails. spdlog v2 calls the handler with its error mutex held, so a handler that
    // logged through the same logger would deadlock here; v1 would recurse until the stack ends.
    const auto failing = std::make_shared<failing_sink>();
    const jspdlog::json_logger logger("app", failing, deterministic_options());
    jspdlog::error_report_options options;
    options.format = deterministic_options();
    options.sink = failing;
    options.min_interval = std::chrono::milliseconds{0};
    logger.spdlog_logger()->set_error_handler(jspdlog::make_error_handler(logger.name(), options));

    CHECK_NOTHROW(logger.info("x"));
    CHECK_NOTHROW(logger.info({"k", 1}, "y"));
}

TEST_CASE("error handler: the handler itself never throws", "[error_handler]")
{
    const auto failing = std::make_shared<failing_sink>();
    jspdlog::error_report_options options;
    options.format = deterministic_options();
    options.sink = failing;
    options.min_interval = std::chrono::milliseconds{0};
    const spdlog::err_handler handler = jspdlog::make_error_handler("app", options);
    CHECK_NOTHROW(handler("a"));
    CHECK_NOTHROW(handler("b"));
}

TEST_CASE("error handler: writes to a stream when there is no sink", "[error_handler]")
{
    std::FILE *file = open_temp_file();
    REQUIRE(file != nullptr);
    {
        jspdlog::error_report_options options;
        options.format = deterministic_options();
        options.stream = file;
        options.min_interval = std::chrono::milliseconds{0};
        const auto failing = std::make_shared<failing_sink>();
        const jspdlog::json_logger logger("app", failing, deterministic_options());
        logger.spdlog_logger()->set_error_handler(jspdlog::make_error_handler(logger.name(), options));
        logger.info("x");
    }
    std::rewind(file);
    std::string text;
    std::array<char, 256> chunk{};
    std::size_t n = 0;
    while ((n = std::fread(chunk.data(), 1, chunk.size(), file)) > 0)
    {
        text.append(chunk.data(), n);
    }
    std::fclose(file);
    REQUIRE(!text.empty());
    CHECK(text.back() == '\n');
    text.pop_back();
    CHECK(is_disk_full_report(text));
}

TEST_CASE("error handler: a stream line ends in a newline even when eol is empty", "[error_handler]")
{
    std::FILE *file = open_temp_file();
    REQUIRE(file != nullptr);
    jspdlog::error_report_options options;
    options.format = deterministic_options();
    options.format.eol = "";
    options.stream = file;
    options.min_interval = std::chrono::milliseconds{0};
    const auto handler = jspdlog::make_error_handler("app", options);
    handler("x");
    std::rewind(file);
    std::array<char, 256> chunk{};
    const std::size_t n = std::fread(chunk.data(), 1, chunk.size(), file);
    std::fclose(file);
    CHECK(std::string(chunk.data(), n) == "{\"logger\":\"app\",\"level\":\"error\",\"message\":\"x\"}\n");
}

#if JSPDLOG_SPDLOG_V2
TEST_CASE("error handler: an async_sink reports from its worker", "[error_handler]")
{
    const auto report = make_report_sink();
    {
        spdlog::sinks::async_sink::config config;
        config.sinks.push_back(std::make_shared<failing_sink>());
        config.custom_err_handler = jspdlog::make_error_handler("app", report_to(report));
        const jspdlog::json_logger logger(
            "app", std::make_shared<spdlog::sinks::async_sink>(std::move(config)), deterministic_options()
        );
        logger.info("x");
        // Destroying the sink drains its queue and joins its worker.
    }
    REQUIRE(report->lines.size() == 1);
    CHECK(is_disk_full_report(report->last()));
}
#else
TEST_CASE("error handler: an async logger reports from its worker", "[error_handler]")
{
    const auto report = make_report_sink();
    {
        auto pool = std::make_shared<spdlog::details::thread_pool>(64, 1);
        auto async = std::make_shared<spdlog::async_logger>(
            "app", std::make_shared<failing_sink>(), pool, spdlog::async_overflow_policy::block
        );
        const auto logger = jspdlog::json_logger::adopt(async, deterministic_options());
        async->set_error_handler(jspdlog::make_error_handler("app", report_to(report)));
        logger.info("x");
        pool.reset(); // The pool drains its queue before its worker stops.
    }
    REQUIRE(report->lines.size() == 1);
    CHECK(is_disk_full_report(report->last()));
}
#endif
