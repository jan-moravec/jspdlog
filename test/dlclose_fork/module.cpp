// A module that logs through jspdlog, for main.cpp to load, call and unload. Writing the process id registers a
// pthread_atfork handler from this module.

#include <jspdlog/jspdlog.h>

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include <memory>
#include <string_view>

namespace
{

// Formats each line, which spdlog's null_sink does not, and remembers whether it held the process id.
class process_sink final : public spdlog::sinks::base_sink<spdlog::details::null_mutex>
{
public:
    bool wrote_process = false;

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t line;
        formatter_->format(msg, line);
        const std::string_view text{line.data(), line.size()};
        wrote_process = text.find("\"process\":") != std::string_view::npos;
    }
    void flush_() override {}
};

} // namespace

// Whether a line with the process id was written.
extern "C" bool jspdlog_test_log();
extern "C" bool jspdlog_test_log()
{
    const auto sink = std::make_shared<process_sink>();
    const jspdlog::json_logger logger("module", sink);
    logger.info("from the module");
    return sink->wrote_process;
}
