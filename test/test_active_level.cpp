// Built with JSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO (see test/CMakeLists.txt) and linked into the same executable as
// the units with the default level. Only the macros read JSPDLOG_ACTIVE_LEVEL, so the inline functions of the header
// are identical in both and the link is safe: that is the check of the one definition rule.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

static_assert(JSPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_INFO, "this unit must be built with JSPDLOG_ACTIVE_LEVEL=info");

bool default_unit_debug_macro_writes();

namespace
{

int evaluations = 0;
int next()
{
    return ++evaluations;
}

} // namespace

TEST_CASE("active level: calls below it are removed, calls at or above it are written", "[active_level]")
{
    test_support::fixture f; // the runtime level is trace
    evaluations = 0;

    JSPDLOG_TRACE(f.logger, {"v", next()}, "trace {}", next());
    JSPDLOG_DEBUG(f.logger, {"v", next()}, "debug {}", next());
    CHECK(evaluations == 0);
    CHECK(f.sink->lines.empty());

    JSPDLOG_INFO(f.logger, {"v", next()}, "info {}", 7);
    CHECK(evaluations == 1);
    CHECK(f.last() == R"({"logger":"t","level":"info","v":1,"message":"info 7"})");
    JSPDLOG_WARN(f.logger, "warn");
    JSPDLOG_ERROR(f.logger, "error");
    JSPDLOG_CRITICAL(f.logger, "critical");
    CHECK(f.sink->lines.size() == 4);

    // JSPDLOG_LOG compares the level at run time, with the same limit.
    JSPDLOG_LOG(f.logger, jspdlog::level::debug, "debug through JSPDLOG_LOG");
    CHECK(f.sink->lines.size() == 4);
    JSPDLOG_LOG(f.logger, jspdlog::level::info, "info through JSPDLOG_LOG");
    CHECK(f.sink->lines.size() == 5);

    // The member functions do not read JSPDLOG_ACTIVE_LEVEL.
    f.logger.debug("debug through the member function");
    CHECK(f.sink->lines.size() == 6);
}

TEST_CASE("active level: units built with different values link into one program", "[active_level]")
{
    CHECK(default_unit_debug_macro_writes());
}
