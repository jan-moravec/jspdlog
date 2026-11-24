// Single-thread benchmark of what a log call costs. A sink formats every line and throws it away, so the numbers
// cover the call, the serialization and the formatter, and nothing else. They are for comparing cases within one run
// on one machine: CI machines are too noisy for thresholds, so nothing here gates a build.
//
// Usage: jspdlog_bench [iterations]. Prints a Markdown table on stdout.

#include <jspdlog/jspdlog.h>

#include <spdlog/details/null_mutex.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

// Formats like a real sink and keeps only the byte count, so the work cannot be optimized away.
class discard_sink final : public spdlog::sinks::base_sink<spdlog::details::null_mutex>
{
public:
    std::size_t bytes = 0;

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        bytes += formatted.size();
    }
    void flush_() override {}
};

struct result
{
    std::string name;
    double ns_per_call = 0.0;
};

constexpr int REPEATS = 5;

// The best of several repeats, after a warm-up, in nanoseconds per call.
double measure(std::size_t iterations, const std::function<void()> &call)
{
    for (std::size_t i = 0; i < (iterations / 10) + 1; ++i)
    {
        call();
    }
    double best = std::numeric_limits<double>::max();
    for (int rep = 0; rep < REPEATS; ++rep)
    {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iterations; ++i)
        {
            call();
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const double ns = std::chrono::duration<double, std::nano>(elapsed).count();
        best = std::min(best, ns / static_cast<double>(iterations));
    }
    return best;
}

jspdlog::json_options options_with(bool process, bool source)
{
    jspdlog::json_options options;
    options.eol = "\n";
    if (!process)
    {
        options.process_key = std::nullopt;
    }
    if (source)
    {
        options.source_key = "source";
    }
    return options;
}

// Five bound fields, as a service would set once per request or per component.
jspdlog::fields bound_fields()
{
    return {"service", "api", "version", "1.4.2", "region", "eu", "host", "h1", "env", "prod"};
}

} // namespace

// NOLINTNEXTLINE(misc-const-correctness): main takes char **
int main(int argc, char **argv)
{
    std::size_t iterations = 200000;
    if (argc > 1)
    {
        iterations = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
        if (iterations == 0)
        {
            std::fprintf(stderr, "usage: %s [iterations]\n", argv[0]);
            return 2;
        }
    }

    std::vector<result> results;
    std::vector<std::shared_ptr<discard_sink>> sinks;
    const auto run = [&](const std::string &name, const std::function<void()> &call) {
        results.push_back({name, measure(iterations, call)});
    };
    const auto make_sink = [&] {
        sinks.push_back(std::make_shared<discard_sink>());
        return sinks.back();
    };

    const std::string user = "alice";
    const std::string text = "plain message with \"quotes\" and a tab\t";

    // The reference: spdlog's default text pattern.
    {
        spdlog::logger logger("bench", make_sink());
        run("spdlog, plain-text pattern", [&] { logger.info("request {} handled in {} ms", 42, 7); });
    }

    // Message only, and the cost of reading the process id on every line.
    {
        jspdlog::json_logger logger("bench", make_sink(), options_with(true, false));
        run("jspdlog, message only", [&] { logger.info("request {} handled in {} ms", 42, 7); });
    }
    {
        jspdlog::json_logger logger("bench", make_sink(), options_with(false, false));
        run("jspdlog, message only, process_key off", [&] { logger.info("request {} handled in {} ms", 42, 7); });
    }

    // The common service case: bound context plus a few fields per call.
    {
        const jspdlog::json_logger root("bench", make_sink(), options_with(true, false));
        const auto logger = root.with(bound_fields());
        run("jspdlog, 5 bound + 2 per-call fields",
            [&] { logger.info({"user", user, "status", 200}, "request {} handled in {} ms", 42, 7); });
    }

    // Per-call keys that are also bound: the quadratic path of the merge.
    {
        const jspdlog::json_logger root("bench", make_sink(), options_with(true, false));
        const auto logger = root.with(bound_fields());
        run("jspdlog, 2 per-call fields overriding bound keys",
            [&] { logger.info({"region", "us", "env", "test"}, "request {} handled in {} ms", 42, 7); });
    }

    // The call site is recorded and its file name reduced to a base name.
    {
        jspdlog::json_logger logger("bench", make_sink(), options_with(true, true));
        run("jspdlog, source location on", [&] { logger.info("request {} handled in {} ms", 42, 7); });
    }

    // Fields are built and serialized before the level is checked: the known cost of eager fields.
    {
        jspdlog::json_logger logger("bench", make_sink(), options_with(true, false));
        logger.set_level(jspdlog::level::off);
        run("jspdlog, level off, 2 per-call fields",
            [&] { logger.info({"user", user, "status", 200}, "request {} handled in {} ms", 42, 7); });
    }

    // Third-party code logging straight on the spdlog logger: the whole payload is escaped into the message.
    {
        const jspdlog::json_logger logger("bench", make_sink(), options_with(true, false));
        const auto &direct = logger.spdlog_logger();
        run("direct spdlog call through json_formatter", [&] { direct->info(text); });
    }

    // Value types: containers, optionals, enums, durations and time points, serialized on every call.
    enum class bench_state
    {
        idle,
        busy
    };
    {
        const jspdlog::json_logger logger("bench", make_sink(), options_with(true, false));
        const std::vector<int> numbers{1, 2, 3, 4, 5, 6, 7, 8};
        run("jspdlog, per-call vector<int> of 8", [&] { logger.info({"numbers", numbers}, "done"); });
        const std::map<std::string, int> counts{{"a", 1}, {"b", 2}, {"c", 3}};
        run("jspdlog, per-call map<string,int> of 3", [&] { logger.info({"counts", counts}, "done"); });
        const std::optional<int> maybe = 5;
        run("jspdlog, per-call optional<int> + enum + duration", [&] {
            logger.info(
                {"maybe", maybe, "state", bench_state::busy, "elapsed_ms", std::chrono::microseconds(1500)}, "done"
            );
        });
        const auto now = std::chrono::system_clock::now();
        run("jspdlog, per-call time_point", [&] { logger.info({"at", now}, "done"); });
    }

    const double baseline = results.front().ns_per_call;
    std::printf("| Case | ns per call | million calls per second | vs spdlog |\n");
    std::printf("| ---- | ----------: | -----------------------: | --------: |\n");
    for (const auto &r : results)
    {
        std::printf(
            "| %s | %.0f | %.2f | %.2fx |\n",
            r.name.c_str(),
            r.ns_per_call,
            1000.0 / r.ns_per_call,
            r.ns_per_call / baseline
        );
    }
    std::printf("\n%zu iterations per repeat, best of %d, one thread.\n", iterations, REPEATS);

    std::size_t written = 0;
    for (const auto &sink : sinks)
    {
        written += sink->bytes;
    }
    return written == 0 ? 1 : 0; // nothing written would mean the cases did not run
}
