#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

#include <string>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a std::string with arguments must not compile as a format string, because it cannot be checked (C++20).
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
    const std::string format = "{}";
#if defined(JSPDLOG_COMPILE_FAIL_CONTROL) && !JSPDLOG_STD_FORMAT
    logger.info(spdlog::fmt_lib::runtime(format), 1);
#elif defined(JSPDLOG_COMPILE_FAIL_CONTROL)
    logger.info("{}", 1); // std::format has no runtime format strings before C++26
    logger.info(format);
#else
    logger.info(format, 1);
#endif
}
