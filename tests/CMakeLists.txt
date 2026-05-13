include(FetchContent)

find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(Catch)

add_executable(
    jspdlog_tests
    test_json_logger.cpp
    test_json_properties.cpp
)
target_link_libraries(jspdlog_tests PRIVATE jspdlog::jspdlog Catch2::Catch2WithMain)

catch_discover_tests(jspdlog_tests)

if(JSPDLOG_TEST_NLOHMANN_INTEROP)
    find_package(nlohmann_json 3 CONFIG QUIET)
    if(NOT nlohmann_json_FOUND)
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG        v3.11.3
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif()

    add_executable(jspdlog_nlohmann_interop_test test_nlohmann_interop.cpp)
    target_link_libraries(
        jspdlog_nlohmann_interop_test
        PRIVATE jspdlog::jspdlog Catch2::Catch2WithMain nlohmann_json::nlohmann_json
    )
    catch_discover_tests(jspdlog_nlohmann_interop_test)
endif()

# Smoke test for the install + find_package() flow. Configured and built only
# when explicitly enabled, since it requires a prior `cmake --install`.
option(JSPDLOG_TEST_INSTALL "Build the install_smoke consumer project" OFF)
if(JSPDLOG_TEST_INSTALL)
    add_subdirectory(install_smoke)
endif()
