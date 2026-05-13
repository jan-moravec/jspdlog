// Rotating file logging. Works with any spdlog sink unchanged.

#include <jspdlog/jspdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

int main()
{
    constexpr std::size_t bytes_per_megabyte = 1024 * 1024;
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "example_02_rotating.log", 10 * bytes_per_megabyte, /*max_files=*/5);

    jspdlog::json_logger logger("app", std::move(sink));
    logger.flush_on(spdlog::level::warn);

    for (int i = 0; i < 100; ++i)
    {
        logger.info("tick {}", i);
    }
    logger.flush();
    return 0;
}
