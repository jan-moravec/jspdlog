# Single-thread benchmark. Built only with JSPDLOG_BUILD_BENCH; use the "bench" preset.
add_executable(jspdlog_bench bench.cpp)
target_link_libraries(jspdlog_bench PRIVATE jspdlog::jspdlog)
jspdlog_set_warnings(jspdlog_bench)
