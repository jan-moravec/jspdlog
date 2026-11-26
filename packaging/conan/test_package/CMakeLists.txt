cmake_minimum_required(VERSION 3.15)
project(jspdlog_test_package LANGUAGES CXX)

find_package(jspdlog CONFIG REQUIRED)

add_executable(test_package test_package.cpp)
target_compile_features(test_package PRIVATE cxx_std_17)
target_link_libraries(test_package PRIVATE jspdlog::jspdlog)
