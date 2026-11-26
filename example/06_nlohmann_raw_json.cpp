// jspdlog has no JSON library dependency. To embed arrays and objects of your favorite JSON library, stringify on your
// side and hand the result to jspdlog::raw_json, which is inserted verbatim into the log line. For nlohmann/json the
// optional header jspdlog/nlohmann.h does it for you: its values are field values as they are.

#include <jspdlog/nlohmann.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

int main()
{
    const jspdlog::json_logger logger("svc", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    const nlohmann::json features = {{"streaming", true}, {"limits", {10, 100, 1000}}};
    const nlohmann::json items = {"alpha", "beta", "gamma"};

    // With jspdlog/nlohmann.h.
    logger.info({"features", features, "items", items}, "loaded config");

    // The same line through raw_json, which works with any JSON library.
    logger.info(
        {"features", jspdlog::raw_json{features.dump()}, "items", jspdlog::raw_json{items.dump()}}, "loaded config"
    );
    return 0;
}
