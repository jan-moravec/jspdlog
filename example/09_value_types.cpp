// Field values beyond scalars: optionals, enums, durations, time points, containers, and your own types.

#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

enum class state
{
    idle,
    busy
};

namespace shop
{

struct money
{
    std::int64_t cents;
    std::string currency;
};

// Found by argument-dependent lookup, so it lives next to the type.
inline void jspdlog_serialize(jspdlog::json_writer &w, const money &m)
{
    w.begin_object();
    w.member("cents", m.cents);
    w.member("currency", m.currency);
    w.end_object();
}

} // namespace shop

namespace third_party
{

struct handle
{
    int id;
};

} // namespace third_party

// For a type whose namespace you do not own, specialize jspdlog::serializer instead.
template <>
struct jspdlog::serializer<third_party::handle>
{
    static void write(jspdlog::json_writer &w, const third_party::handle &h) { w.value(h.id); }
};

int main()
{
    using namespace std::chrono_literals;
    const jspdlog::json_logger logger("svc", std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    const std::optional<int> retry_after;
    const std::map<std::string, std::vector<int>> latencies{{"read", {3, 5}}, {"write", {8}}};
    // state is a number (this enum has no {fmt} formatter), retry_after is null, elapsed_ms is 1.5, started is a string
    // such as "2026-11-23T08:00:00.123Z", and latencies is {"read":[3,5],"write":[8]}.
    logger.info(
        {"state",
         state::busy,
         "retry_after",
         retry_after,
         "elapsed_ms",
         1500us,
         "started",
         std::chrono::system_clock::now(),
         "latencies",
         latencies,
         "price",
         shop::money{1250, "EUR"},
         "handle",
         third_party::handle{7}},
        "request done"
    );
    return 0;
}
