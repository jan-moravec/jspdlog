#include <jspdlog/jspdlog.h>

#include <spdlog/sinks/null_sink.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a braced list holds at most 16 fields; build a jspdlog::fields object for more.
int main()
{
    const jspdlog::json_logger logger("x", std::make_shared<spdlog::sinks::null_sink_mt>());
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    // 16 pairs.
    logger.info({"k0", 0, "k1", 1, "k2",  2,  "k3",  3,  "k4",  4,  "k5",  5,  "k6",  6,  "k7",  7,
                 "k8", 8, "k9", 9, "k10", 10, "k11", 11, "k12", 12, "k13", 13, "k14", 14, "k15", 15});
#else
    // 17 pairs.
    logger.info({"k0", 0, "k1",  1,  "k2",  2,  "k3",  3,  "k4",  4,  "k5",  5,  "k6",  6,  "k7",  7, "k8", 8,
                 "k9", 9, "k10", 10, "k11", 11, "k12", 12, "k13", 13, "k14", 14, "k15", 15, "k16", 16});
#endif
}
