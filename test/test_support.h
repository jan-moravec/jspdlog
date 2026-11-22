// Shared helpers for the jspdlog test suite.

#pragma once

#include <jspdlog/jspdlog.h>

#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>

#ifdef JSPDLOG_TEST_NLOHMANN
#include <nlohmann/json.hpp>
#endif

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

// A plain spdlog logger on a sink, with jspdlog's formatter installed, standing in for third-party code.
inline std::shared_ptr<spdlog::logger> make_plain_logger(
    std::string name,
    const std::shared_ptr<capture_sink> &sink,
    const jspdlog::json_options &opts = deterministic_options()
)
{
    auto logger = std::make_shared<spdlog::logger>(std::move(name), sink);
    logger->set_level(jspdlog::level::trace);
    jspdlog::install_json_formatter(*logger, opts);
    return logger;
}

#ifdef JSPDLOG_TEST_NLOHMANN
// Whether the line is exactly one JSON object.
inline bool is_single_json_object(const std::string &line)
{
    if (line.find('\n') != std::string::npos)
    {
        return false;
    }
    const auto parsed = nlohmann::json::parse(line, nullptr, false);
    return parsed.is_object();
}

// The "message" member of a line, or an empty string when it is missing or not a string.
inline std::string message_of(const std::string &line, const std::string &key = "message")
{
    const auto parsed = nlohmann::json::parse(line, nullptr, false);
    if (!parsed.is_object() || !parsed.contains(key) || !parsed[key].is_string())
    {
        return {};
    }
    return parsed[key].get<std::string>();
}
#else
inline bool is_single_json_object(const std::string &line)
{
    return line.find('\n') == std::string::npos && !line.empty() && line.front() == '{' && line.back() == '}';
}
#endif

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
