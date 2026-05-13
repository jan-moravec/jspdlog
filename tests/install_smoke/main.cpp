// Smallest possible consumer of the installed jspdlog package. The test is
// "this file builds and runs"; we don't validate output here, the regular
// unit tests do that.

#include <jspdlog/jspdlog.h>
#include <spdlog/sinks/null_sink.h>

int main()
{
    jspdlog::json_logger logger("install_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());
    logger.info(jspdlog::json_properties{"ok", true}, "hello {}", "world");
    return 0;
}
