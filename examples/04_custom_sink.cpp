// Plug any user-written spdlog sink into a json_logger. Here we collect log
// lines into a std::vector for testing or in-memory inspection.

#include <jspdlog/jspdlog.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include <iostream>
#include <mutex>
#include <vector>

class memory_sink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    std::vector<std::string> lines;

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
        lines.emplace_back(formatted.data(), formatted.size());
    }
    void flush_() override
    {
    }
};

int main()
{
    auto sink = std::make_shared<memory_sink>();
    jspdlog::json_logger logger("svc", sink);
    logger.info({"k", 1}, "hello");

    for (const auto &line : sink->lines)
    {
        std::cout << line;
    }
    return 0;
}
