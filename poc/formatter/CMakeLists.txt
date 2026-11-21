# Proof of concept: jspdlog on a custom spdlog::formatter. Not installed.
foreach(name 01_source_location 02_foreign_messages 03_async 04_bench)
    add_executable(poc_formatter_${name} ${name}.cpp)
    target_link_libraries(poc_formatter_${name} PRIVATE jspdlog::jspdlog)
endforeach()

if(JSPDLOG_BUILD_TESTS)
    add_executable(poc_formatter_tests test_poc.cpp)
    target_link_libraries(poc_formatter_tests PRIVATE jspdlog::jspdlog Catch2::Catch2WithMain)
    catch_discover_tests(poc_formatter_tests)
endif()
