include(FetchContent)

# Declared here (rather than in the top-level CMakeLists) so the default
# tracks the current value of JSPDLOG_BUILD_TESTS even across reconfigures.
option(JSPDLOG_TEST_WITH_NLOHMANN "Build the tests that need nlohmann/json: interop and fuzz replay (fetches it)" ON)

find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE
        ${JSPDLOG_FETCH_SYSTEM}
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(Catch)

if(JSPDLOG_TEST_WITH_NLOHMANN)
    find_package(nlohmann_json 3 CONFIG QUIET)
    if(NOT nlohmann_json_FOUND)
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
            GIT_SHALLOW TRUE
            ${JSPDLOG_FETCH_SYSTEM}
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

add_executable(
    jspdlog_tests
    test_formatter.cpp
    test_json_logger.cpp
    test_fields.cpp
    test_lazy.cpp
    test_value_types.cpp
    test_source_location.cpp
    test_spdlog_interop.cpp
)
target_link_libraries(jspdlog_tests PRIVATE jspdlog::jspdlog Catch2::Catch2WithMain)
# Lets a test check that the header's version macros match project(VERSION).
target_compile_definitions(jspdlog_tests PRIVATE JSPDLOG_TEST_PROJECT_VERSION="${PROJECT_VERSION}")
# The formatter tests parse each line with nlohmann/json when it is available and skip those checks otherwise.
if(JSPDLOG_TEST_WITH_NLOHMANN)
    target_link_libraries(jspdlog_tests PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(jspdlog_tests PRIVATE JSPDLOG_TEST_NLOHMANN=1)
endif()
jspdlog_set_warnings(jspdlog_tests)

catch_discover_tests(jspdlog_tests)

# Defines its own JSPDLOG_ASSERT, so it is its own executable.
add_executable(jspdlog_assert_tests test_assert.cpp)
target_link_libraries(jspdlog_assert_tests PRIVATE jspdlog::jspdlog Catch2::Catch2WithMain)
jspdlog_set_warnings(jspdlog_assert_tests)
catch_discover_tests(jspdlog_assert_tests)

# Code that must not compile: each file is a target excluded from the build, and the test builds it and expects failure.
# A control target builds the same file with the offending line replaced by a valid one and must succeed, so a failure
# for an unrelated reason (a broken include, a typo) cannot pass as the expected one. The tests build in the shared
# build directory, so they hold a lock and never run in parallel.
foreach(
    case
    info_int
    info_vector
    odd_fields
    fields_int
    value_pointer
    value_wchar
    value_unknown
    value_path_like
    value_steady_clock
    value_implicit_bool
    too_many_fields
)
    foreach(kind fail control)
        add_executable(compile_${kind}_${case} EXCLUDE_FROM_ALL compile_fail/${case}.cpp)
        target_link_libraries(compile_${kind}_${case} PRIVATE jspdlog::jspdlog)
        add_test(
            NAME compile_${kind}.${case}
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target compile_${kind}_${case} --config $<CONFIG>
        )
        set_tests_properties(compile_${kind}.${case} PROPERTIES RESOURCE_LOCK jspdlog_build_dir)
    endforeach()
    target_compile_definitions(compile_control_${case} PRIVATE JSPDLOG_COMPILE_FAIL_CONTROL)
    jspdlog_set_warnings(compile_control_${case})
    set_tests_properties(compile_fail.${case} PROPERTIES WILL_FAIL TRUE)
endforeach()

if(JSPDLOG_TEST_WITH_NLOHMANN)
    add_executable(jspdlog_nlohmann_interop_test test_nlohmann_interop.cpp)
    target_link_libraries(
        jspdlog_nlohmann_interop_test
        PRIVATE jspdlog::jspdlog Catch2::Catch2WithMain nlohmann_json::nlohmann_json
    )
    jspdlog_set_warnings(jspdlog_nlohmann_interop_test)
    catch_discover_tests(jspdlog_nlohmann_interop_test)
endif()

# install_smoke is a separate standalone project (see install_smoke/CMakeLists.txt)
# that has to find_package() jspdlog from an installed tree. It is driven from
# CI as a top-level configure, not added as a subdirectory here, because the
# find_package would otherwise fail before jspdlog is installed.

# Fuzz targets, replayed as ordinary tests. They parse every line with nlohmann/json.
if(JSPDLOG_TEST_WITH_NLOHMANN)
    add_subdirectory(fuzz)
endif()
