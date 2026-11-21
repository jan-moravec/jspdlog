#!/usr/bin/env bash
# Merges the clang-coverage test profiles into coverage.lcov and prints a summary.
# Only the library header is counted: _deps/, build/, test/ and example/ are excluded.
set -euo pipefail

BUILD_DIR=build/clang-coverage
IGNORE='(_deps|build|test|example)/'

llvm-profdata-23 merge -sparse "$BUILD_DIR"/profiles/*.profraw -o "$BUILD_DIR/merged.profdata"
OBJECTS=(
    "$BUILD_DIR/test/jspdlog_tests"
    -object "$BUILD_DIR/test/jspdlog_nlohmann_interop_test"
)
llvm-cov-23 export -format=lcov \
    -instr-profile="$BUILD_DIR/merged.profdata" \
    -ignore-filename-regex="$IGNORE" \
    "${OBJECTS[@]}" >coverage.lcov
echo "::group::Coverage summary (header only)"
llvm-cov-23 report \
    -instr-profile="$BUILD_DIR/merged.profdata" \
    -ignore-filename-regex="$IGNORE" \
    "${OBJECTS[@]}"
echo "::endgroup::"
