#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

#include <string>
#include <vector>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a message must be a string: typed values belong in fields.
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    logger.info(std::to_string(std::vector<int>{}.size()));
#else
    logger.info(std::vector<int>{});
#endif
}
