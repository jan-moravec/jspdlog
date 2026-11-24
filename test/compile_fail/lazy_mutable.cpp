#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a lazy value is called as const, so a mutable lambda is rejected.
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    logger.info({"v", [n = 0] { return n + 1; }});
#else
    logger.info({"v", [n = 0]() mutable { return ++n; }});
#endif
}
