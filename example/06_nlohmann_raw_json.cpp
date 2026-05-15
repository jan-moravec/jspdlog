// jspdlog has no JSON library dependency. To embed arrays/objects produced
// by your favorite JSON library, stringify on your side and hand the result
// to jspdlog::raw_json. The library inserts it verbatim into the log line.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

int main()
{
    jspdlog::json_logger logger("svc", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    const nlohmann::json features = {{"streaming", true}, {"limits", {10, 100, 1000}}};
    const nlohmann::json items = {"alpha", "beta", "gamma"};

    logger.info(
        {"features", jspdlog::raw_json{features.dump()}, "items", jspdlog::raw_json{items.dump()}}, "loaded config"
    );
    return 0;
}
