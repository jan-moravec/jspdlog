// Expensive values: a braced list costs nothing at a disabled level, a lazy value runs only when the line is written,
// and the optional macros skip format arguments too.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <string>
#include <vector>

namespace
{

struct query
{
    int id;
    std::vector<std::string> tables;
};

// Stands in for something slow, such as building a query plan.
std::string explain(const query &q)
{
    std::string plan = "scan";
    for (const auto &t : q.tables)
    {
        plan += " " + t;
    }
    return plan;
}

} // namespace

int main()
{
    jspdlog::json_logger logger("app", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    const query q{7, {"users", "orders"}};

    // Debug is off: the braced list is not serialized and the lambda never runs.
    logger.set_level(jspdlog::level::info);
    logger.debug({"id", q.id, "plan", [&] { return explain(q); }}, "planned");

    // Debug is on: the lambda runs once, in this thread, and its result is the value.
    logger.set_level(jspdlog::level::debug);
    logger.debug({"id", q.id, "plan", [&] { return explain(q); }}, "planned");

    // A format argument runs before a function call, so use a macro when it is expensive. With the level off, nothing
    // in the macro is evaluated. Compile with JSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO to remove the call entirely.
    logger.set_level(jspdlog::level::info);
    JSPDLOG_DEBUG(logger, {"id", q.id}, "planned {}", explain(q));
    JSPDLOG_INFO(logger, {"id", q.id}, "query {} done", q.id);
    return 0;
}
