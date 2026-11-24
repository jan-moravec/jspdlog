#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a lazy value would dangle in a child logger, so with() rejects it.
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    const auto child = logger.with({"a", 1});
#else
    const auto child = logger.with({"a", [] { return 1; }});
#endif
    child.info("x");
}
