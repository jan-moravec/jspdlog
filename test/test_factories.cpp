// The factories of jspdlog/factories.h. What the stdout and stderr loggers write is covered by the examples, whose
// output CI parses: spdlog does not expose the formatter of a sink. The async tests also run under TSan.

#include <jspdlog/factories.h>

#include "test_support.h"

#include <spdlog/sinks/base_sink.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

using test_support::capture_sink;
using test_support::deterministic_options;

namespace
{

#if JSPDLOG_SPDLOG_V2
using spdlog_overflow_policy = spdlog::sinks::async_sink::overflow_policy;
#else
using spdlog_overflow_policy = spdlog::async_overflow_policy;
#endif

// A sink that throws on every line, standing in for a full disk.
class failing_sink final : public spdlog::sinks::base_sink<std::mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg & /*msg*/) override { throw spdlog::spdlog_ex("disk full"); }
    void flush_() override {}
};

// A capture sink that holds the worker thread until it is opened, so that lines stay in the queue.
class gated_sink final : public capture_sink
{
public:
    void open()
    {
        {
            const std::scoped_lock lock(gate_mutex_);
            open_ = true;
        }
        opened_.notify_all();
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        {
            std::unique_lock lock(gate_mutex_);
            opened_.wait(lock, [this] { return open_; });
        }
        capture_sink::sink_it_(msg);
    }

private:
    std::mutex gate_mutex_;
    std::condition_variable opened_;
    bool open_ = false;
};

// std::tmpfile is deprecated on MSVC, which has tmpfile_s.
std::FILE *open_temp_file()
{
#ifdef _MSC_VER
    std::FILE *file = nullptr;
    return tmpfile_s(&file) == 0 ? file : nullptr;
#else
    // NOLINTNEXTLINE(bugprone-unsafe-functions): tmpfile_s is not in the standard library of Linux or macOS
    return std::tmpfile();
#endif
}

int descriptor_of(std::FILE *file)
{
#ifdef _WIN32
    return _fileno(file);
#else
    return fileno(file);
#endif
}

int duplicate(int descriptor)
{
#ifdef _WIN32
    return _dup(descriptor);
#else
    return dup(descriptor);
#endif
}

void redirect(int from, int to)
{
#ifdef _WIN32
    static_cast<void>(_dup2(from, to));
#else
    static_cast<void>(dup2(from, to));
#endif
}

void close_descriptor(int descriptor)
{
#ifdef _WIN32
    static_cast<void>(_close(descriptor));
#else
    static_cast<void>(close(descriptor));
#endif
}

// Sends standard error to a temporary file until finish, which returns what was written.
class stderr_capture
{
public:
    stderr_capture()
        : file_(open_temp_file())
    {
        REQUIRE(file_ != nullptr);
        static_cast<void>(std::fflush(stderr));
        saved_ = duplicate(descriptor_of(stderr));
        redirect(descriptor_of(file_), descriptor_of(stderr));
    }
    stderr_capture(const stderr_capture &) = delete;
    stderr_capture &operator=(const stderr_capture &) = delete;
    ~stderr_capture()
    {
        restore_();
        if (file_ != nullptr)
        {
            static_cast<void>(std::fclose(file_));
        }
    }

    std::string finish()
    {
        restore_();
        REQUIRE(std::fseek(file_, 0, SEEK_SET) == 0);
        std::string text;
        for (int c = std::fgetc(file_); c != EOF; c = std::fgetc(file_))
        {
            text.push_back(static_cast<char>(c));
        }
        return text;
    }

private:
    void restore_()
    {
        if (saved_ >= 0)
        {
            static_cast<void>(std::fflush(stderr));
            redirect(saved_, descriptor_of(stderr));
            close_descriptor(saved_);
            saved_ = -1;
        }
    }

    std::FILE *file_;
    int saved_ = -1;
};

// A directory of its own under the temporary directory, removed with its contents.
class temp_directory
{
public:
    temp_directory()
        : path_(
              std::filesystem::temp_directory_path() /
              ("jspdlog_factories_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
          )
    {
        std::filesystem::create_directories(path_);
    }
    temp_directory(const temp_directory &) = delete;
    temp_directory &operator=(const temp_directory &) = delete;
    ~temp_directory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::vector<std::string> read_lines(const std::filesystem::path &path)
{
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
    {
        lines.push_back(line);
    }
    return lines;
}

// The line of logger "app" with deterministic options for info({"n", n}, "line").
std::string numbered_line(int n)
{
    return R"({"logger":"app","level":"info","n":)" + std::to_string(n) + R"(,"message":"line"})";
}

// Logs 100 numbered lines into a queue of four while the worker is held, then returns what reached the sink.
std::vector<std::string> overflow(jspdlog::overflow_policy policy)
{
    const auto sink = std::make_shared<gated_sink>();
    {
        jspdlog::async_options async;
        async.queue_size = 4;
        async.overflow = policy;
        const auto logger = jspdlog::make_async_logger("app", {sink}, async, deterministic_options());
        for (int i = 0; i < 100; ++i)
        {
            logger.info({"n", i}, "line");
        }
        sink->open();
    }
    return sink->lines;
}

} // namespace

TEST_CASE("factories: the console loggers write through spdlog's thread-safe sinks", "[factories]")
{
    const auto out = jspdlog::make_stdout_logger("app");
    CHECK(out.name() == "app");
    CHECK(out.log_level() == jspdlog::level::info);
    REQUIRE(out.spdlog_logger()->sinks().size() == 1);
    const auto out_sink = out.spdlog_logger()->sinks().front();
    CHECK(std::dynamic_pointer_cast<spdlog::sinks::stdout_sink_mt>(out_sink) != nullptr);

    const auto err = jspdlog::make_stderr_logger("errors");
    CHECK(err.name() == "errors");
    REQUIRE(err.spdlog_logger()->sinks().size() == 1);
    const auto err_sink = err.spdlog_logger()->sinks().front();
    CHECK(std::dynamic_pointer_cast<spdlog::sinks::stderr_sink_mt>(err_sink) != nullptr);
}

TEST_CASE("factories: the rotating file logger writes JSON lines and rotates", "[factories]")
{
    const temp_directory dir;
    const std::string base = (dir.path() / "app.log").string();
    {
        // Each line is longer than half of max_size, so every line after the first starts a new file.
        const auto logger = jspdlog::make_rotating_file_logger("app", base, 64, 5, deterministic_options());
        for (int i = 0; i < 3; ++i)
        {
            logger.info({"n", i}, "line");
        }
    }
    CHECK(read_lines(dir.path() / "app.log") == std::vector<std::string>{numbered_line(2)});
    CHECK(read_lines(dir.path() / "app.1.log") == std::vector<std::string>{numbered_line(1)});
    CHECK(read_lines(dir.path() / "app.2.log") == std::vector<std::string>{numbered_line(0)});
    CHECK_FALSE(std::filesystem::exists(dir.path() / "app.3.log"));
}

TEST_CASE("factories: an async logger writes every queued line before its worker stops", "[factories][async]")
{
    constexpr int LINES = 1000;
    const auto sink = std::make_shared<gated_sink>();
    std::thread opener;
    {
        const auto logger = jspdlog::make_async_logger("app", {sink}, {}, deterministic_options());
        for (int i = 0; i < LINES; ++i)
        {
            logger.info({"n", i}, "line");
        }
        // Opened while the destructor of the logger below is waiting for the worker, with the lines still queued.
        opener = std::thread([&sink] {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
            sink->open();
        });
    }
    // Read before the thread is joined: the logger's destructor alone must have waited for every line.
    const std::vector<std::string> lines = sink->lines;
    opener.join();
    REQUIRE(lines.size() == LINES);
    for (int i = 0; i < LINES; ++i)
    {
        CHECK(lines[static_cast<std::size_t>(i)] == numbered_line(i));
    }
}

TEST_CASE("factories: an async logger keeps the order when calls wait for space", "[factories][async]")
{
    constexpr int LINES = 1000;
    const auto sink = std::make_shared<capture_sink>();
    {
        jspdlog::async_options async;
        async.queue_size = 8;
        const auto logger = jspdlog::make_async_logger("app", {sink}, async, deterministic_options());
        for (int i = 0; i < LINES; ++i)
        {
            logger.info({"n", i}, "line");
        }
    }
    REQUIRE(sink->lines.size() == LINES);
    for (int i = 0; i < LINES; ++i)
    {
        CHECK(sink->lines[static_cast<std::size_t>(i)] == numbered_line(i));
    }
}

TEST_CASE("factories: every sink of an async logger writes JSON lines with default options", "[factories][async]")
{
    const auto first = std::make_shared<capture_sink>();
    const auto second = std::make_shared<capture_sink>();
    {
        const auto logger = jspdlog::make_async_logger("app", {first, second});
        logger.info({"n", 1}, "line");
        logger.spdlog_logger()->warn("plain {}", "spdlog");
    }
    for (const auto &sink : {first, second})
    {
        REQUIRE(sink->lines.size() == 2);
        for (const std::string &line : sink->lines)
        {
            CHECK(test_support::is_single_json_object(line));
            CHECK(line.find(R"("timestamp":)") != std::string::npos);
        }
        CHECK(sink->lines[0].find(R"("n":1,"message":"line")") != std::string::npos);
        CHECK(sink->lines[1].find(R"("message":"plain spdlog")") != std::string::npos);
    }
}

TEST_CASE("factories: children and spdlog_logger pointers keep the worker alive", "[factories][async]")
{
    const auto sink = std::make_shared<capture_sink>();
    std::shared_ptr<spdlog::logger> handle;
    {
        std::optional<jspdlog::json_logger> child;
        {
            const auto root = jspdlog::make_async_logger("app", {sink}, {}, deterministic_options());
            child.emplace(root.with({"child", true}));
        }
        child->info("from the child");
        handle = child->spdlog_logger();
    }
    handle->info("from the pointer");
    handle.reset();
    REQUIRE(sink->lines.size() == 2);
    CHECK(sink->lines[0] == R"({"logger":"app","level":"info","child":true,"message":"from the child"})");
    CHECK(sink->lines[1] == R"({"logger":"app","level":"info","message":"from the pointer"})");
}

TEST_CASE("factories: an async logger reports a failing sink as JSON on standard error", "[factories][async]")
{
    auto options = deterministic_options();
    options.level_key = "severity";
    std::string reported;
    {
        stderr_capture capture;
        {
            const auto logger = jspdlog::make_async_logger("app", {std::make_shared<failing_sink>()}, {}, options);
            CHECK_NOTHROW(logger.info("line"));
        }
        reported = capture.finish();
    }
    // spdlog v1 appends the call site to the message, so only the start is compared.
    CHECK(reported.find(R"({"logger":"app","severity":"error","message":"disk full)") == 0);
}

TEST_CASE("factories: an async logger reports errors of the calling thread as JSON too", "[factories][async]")
{
    const auto sink = std::make_shared<capture_sink>();
    std::string reported;
    {
        stderr_capture capture;
        {
            const auto logger = jspdlog::make_async_logger("app", {sink}, {}, deterministic_options());
            CHECK_NOTHROW(logger.info("{}", test_support::throws_when_formatted{}));
        }
        reported = capture.finish();
    }
    CHECK(sink->lines.empty());
    CHECK(reported.find(R"({"logger":"app","level":"error","message":"formatter failed)") == 0);
}

TEST_CASE("factories: json_errors off keeps spdlog's error handler", "[factories][async]")
{
    std::string reported;
    {
        stderr_capture capture;
        {
            jspdlog::async_options async;
            async.json_errors = false;
            const auto logger = jspdlog::make_async_logger("app", {std::make_shared<failing_sink>()}, async);
            CHECK_NOTHROW(logger.info("line"));
        }
        reported = capture.finish();
    }
    CHECK(reported.find(R"({"logger")") == std::string::npos);
}

TEST_CASE("factories: overrun_oldest keeps the newest lines of a full queue", "[factories][async]")
{
    const std::vector<std::string> lines = overflow(jspdlog::overflow_policy::overrun_oldest);
    // The queue of four, plus the line the worker may already hold.
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.size() <= 5);
    CHECK(lines.back() == numbered_line(99));
}

TEST_CASE("factories: discard_new keeps the oldest lines, where spdlog has it", "[factories][async]")
{
    if constexpr (jspdlog::detail::has_discard_new_v<spdlog_overflow_policy>)
    {
        const std::vector<std::string> lines = overflow(jspdlog::overflow_policy::discard_new);
        REQUIRE_FALSE(lines.empty());
        CHECK(lines.size() <= 5);
        CHECK(lines.front() == numbered_line(0));
        CHECK(lines.back() != numbered_line(99));
    }
    else
    {
        jspdlog::async_options async;
        async.overflow = jspdlog::overflow_policy::discard_new;
        CHECK_THROWS_AS(jspdlog::make_async_logger("app", {}, async), std::invalid_argument);
    }
}

TEST_CASE("factories: make_async_logger rejects a null sink and a queue size spdlog cannot take", "[factories][async]")
{
    const auto sink = std::make_shared<capture_sink>();
    CHECK_THROWS_AS(jspdlog::make_async_logger("app", {sink, nullptr}), std::invalid_argument);
    jspdlog::async_options async;
    async.queue_size = 0;
    CHECK_THROWS_AS(jspdlog::make_async_logger("app", {sink}, async), std::invalid_argument);
#if JSPDLOG_SPDLOG_V2
    async.queue_size = spdlog::sinks::async_sink::max_queue_size;
    CHECK_NOTHROW(jspdlog::make_async_logger("app", {sink}, async));
    async.queue_size = spdlog::sinks::async_sink::max_queue_size + 1;
    CHECK_THROWS_AS(jspdlog::make_async_logger("app", {sink}, async), std::invalid_argument);
#endif
}

TEST_CASE("factories: the last copy of an async logger may go away on any thread", "[factories][async]")
{
    constexpr int THREADS = 4;
    constexpr int LINES_PER_THREAD = 250;
    const auto sink = std::make_shared<capture_sink>();
    std::vector<std::thread> threads;
    {
        jspdlog::async_options async;
        async.queue_size = 16;
        const auto root = jspdlog::make_async_logger("app", {sink}, async, deterministic_options());
        for (int t = 0; t < THREADS; ++t)
        {
            // Each thread holds the only copies but this one, which goes away first, so a logging thread joins the
            // worker.
            threads.emplace_back([logger = root.with({"t", t})] {
                for (int i = 0; i < LINES_PER_THREAD; ++i)
                {
                    logger.info({"n", i}, "line");
                }
            });
        }
    }
    for (std::thread &thread : threads)
    {
        thread.join();
    }
    REQUIRE(sink->lines.size() == static_cast<std::size_t>(THREADS * LINES_PER_THREAD));
    std::vector<int> next(THREADS, 0);
    for (const std::string &line : sink->lines)
    {
        bool matched = false;
        for (int t = 0; t < THREADS && !matched; ++t)
        {
            const std::string expected = R"({"logger":"app","level":"info","t":)" + std::to_string(t) + R"(,"n":)" +
                                         std::to_string(next[static_cast<std::size_t>(t)]) + R"(,"message":"line"})";
            if (line == expected)
            {
                ++next[static_cast<std::size_t>(t)];
                matched = true;
            }
        }
        CAPTURE(line);
        CHECK(matched);
    }
}

TEST_CASE("factories: a clone of an async logger's spdlog::logger", "[factories][async]")
{
    const auto sink = std::make_shared<capture_sink>();
    std::shared_ptr<spdlog::logger> clone;
    std::string reported;
    {
        stderr_capture capture;
        {
            const auto logger = jspdlog::make_async_logger("app", {sink}, {}, deterministic_options());
            clone = logger.spdlog_logger()->clone("clone");
        }
        CHECK_NOTHROW(clone->info("after the logger"));
        clone.reset();
        reported = capture.finish();
    }
#if JSPDLOG_SPDLOG_V2
    // The clone shares the async_sink, which owns the worker, so the line is written.
    REQUIRE(sink->lines.size() == 1);
    CHECK(sink->lines[0] == R"({"logger":"clone","level":"info","message":"after the logger"})");
    CHECK(reported.empty());
#else
    // The clone only observes the thread pool, which went away with the logger: the line is reported, not written.
    CHECK(sink->lines.empty());
    CHECK(reported.find(R"({"logger":"app","level":"error","message":")") == 0);
#endif
}
