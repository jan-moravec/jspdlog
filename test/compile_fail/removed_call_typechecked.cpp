// A level below JSPDLOG_ACTIVE_LEVEL is removed, but its arguments are still type-checked.
#define JSPDLOG_ACTIVE_LEVEL 2 // SPDLOG_LEVEL_INFO: debug is removed

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a message must be a string, also in a call that is removed at compile time.
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    JSPDLOG_DEBUG(logger, "x");
#else
    JSPDLOG_DEBUG(logger, 42);
#endif
}
