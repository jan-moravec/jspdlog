#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a number after fields is not a message.
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    logger.info({"k", 1}, "2");
#else
    logger.info({"k", 1}, 2);
#endif
}
