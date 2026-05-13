// Console logging: every line is one valid JSON object.

#include <jspdlog/jspdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main()
{
    jspdlog::json_logger logger("app", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    logger.set_level(spdlog::level::trace);

    logger.info("hello {}", "world");
    logger.warn("queue depth = {}", 42);
    logger.error("something went wrong");
    return 0;
}
