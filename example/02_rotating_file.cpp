// Rotating file logging. Works with any spdlog sink unchanged.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/rotating_file_sink.h>

int main()
{
    constexpr std::size_t BYTES_PER_MEGABYTE = 1024 * 1024;
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "example_02_rotating.log", 10 * BYTES_PER_MEGABYTE, /*max_files=*/5
    );

    jspdlog::json_logger logger("app", std::move(sink));
    // Anything at warn or above is flushed to disk immediately; the info-level
    // ticks are buffered until the explicit flush() call at the bottom of main.
    logger.flush_on(spdlog::level::warn);

    for (int i = 0; i < 100; ++i)
    {
        logger.info("tick {}", i);
    }
    logger.warn({"reason", "demo"}, "this line is flushed synchronously");
    logger.flush();
    return 0;
}
