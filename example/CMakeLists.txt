add_executable(example_01_console 01_console.cpp)
target_link_libraries(example_01_console PRIVATE jspdlog::jspdlog)
jspdlog_set_warnings(example_01_console)

add_executable(example_02_rotating_file 02_rotating_file.cpp)
target_link_libraries(example_02_rotating_file PRIVATE jspdlog::jspdlog)
jspdlog_set_warnings(example_02_rotating_file)

add_executable(example_03_properties 03_properties.cpp)
target_link_libraries(example_03_properties PRIVATE jspdlog::jspdlog)
jspdlog_set_warnings(example_03_properties)

add_executable(example_04_custom_sink 04_custom_sink.cpp)
target_link_libraries(example_04_custom_sink PRIVATE jspdlog::jspdlog)
jspdlog_set_warnings(example_04_custom_sink)

add_executable(example_05_async 05_async.cpp)
target_link_libraries(example_05_async PRIVATE jspdlog::jspdlog)
jspdlog_set_warnings(example_05_async)

# nlohmann/json is only used by this single example, fetched lazily.
include(FetchContent)
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

add_executable(example_06_nlohmann_raw_json 06_nlohmann_raw_json.cpp)
target_link_libraries(example_06_nlohmann_raw_json PRIVATE jspdlog::jspdlog nlohmann_json::nlohmann_json)
jspdlog_set_warnings(example_06_nlohmann_raw_json)

add_executable(example_07_third_party 07_third_party.cpp)
target_link_libraries(example_07_third_party PRIVATE jspdlog::jspdlog)
jspdlog_set_warnings(example_07_third_party)
