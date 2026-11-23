# Fuzz targets. Every target builds two ways:
#   fuzz_replay_<target>  runs the saved inputs as a CTest case, on every compiler and platform;
#   fuzz_<target>         is the libFuzzer executable, built only with JSPDLOG_BUILD_FUZZERS (Clang on Linux).
# The parent directory has already found nlohmann/json, which the oracle uses to parse every line.

set(JSPDLOG_FUZZ_TARGETS
    escape
    fields
    logger_line
    foreign_payload
    numbers
)

foreach(target IN LISTS JSPDLOG_FUZZ_TARGETS)
    add_executable(fuzz_replay_${target} ${target}.cpp replay_main.cpp)
    target_link_libraries(fuzz_replay_${target} PRIVATE jspdlog::jspdlog nlohmann_json::nlohmann_json)
    jspdlog_set_warnings(fuzz_replay_${target})
    add_test(
        NAME fuzz_replay.${target}
        COMMAND
            fuzz_replay_${target} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${target}
            ${CMAKE_CURRENT_SOURCE_DIR}/regressions/${target}
    )
    set_tests_properties(fuzz_replay.${target} PROPERTIES LABELS fuzz-replay)

    if(JSPDLOG_BUILD_FUZZERS)
        add_executable(fuzz_${target} ${target}.cpp)
        target_link_libraries(fuzz_${target} PRIVATE jspdlog::jspdlog nlohmann_json::nlohmann_json)
        target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
        jspdlog_set_warnings(fuzz_${target})
    endif()
endforeach()
