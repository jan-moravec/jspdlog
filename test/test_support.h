// Shared helpers for the jspdlog test suite.

#pragma once

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/base_sink.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace test_support
{

// Collects every formatted line (without the trailing eol).
class capture_sink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    std::vector<std::string> lines;

    std::string last() const { return lines.empty() ? std::string{} : lines.back(); }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        std::string line(formatted.data(), formatted.size());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    void flush_() override {}
};

// Options that leave only the deterministic header fields (logger, level), so
// tests can compare whole lines exactly.
inline jspdlog::json_options deterministic_options()
{
    jspdlog::json_options opts;
    opts.timestamp_key = std::nullopt;
    opts.process_key = std::nullopt;
    opts.thread_key = std::nullopt;
    opts.eol = "\n";
    return opts;
}

struct fixture
{
    std::shared_ptr<capture_sink> sink = std::make_shared<capture_sink>();
    jspdlog::json_logger logger;

    explicit fixture(std::string name = "t", const jspdlog::json_options &opts = deterministic_options())
        : logger(std::move(name), sink, opts)
    {
        logger.set_level(jspdlog::level::trace);
    }

    std::string last() const { return sink->last(); }
};

} // namespace test_support
