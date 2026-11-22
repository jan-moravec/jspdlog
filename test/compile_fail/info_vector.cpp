#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

#include <vector>

// A message must be a string: typed values belong in fields.
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
    logger.info(std::vector<int>{});
}
