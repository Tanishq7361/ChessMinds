cmake_minimum_required(VERSION 3.20)

project(ChessMinds
    VERSION 0.1.0
    LANGUAGES CXX
)

# Enforce C++20 standard without compiler-specific extensions
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings interface
add_library(chessmind_warnings INTERFACE)
target_compile_options(chessmind_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>
)

# Core engine library
add_library(chessmind_core
    src/bitboard.cpp
    src/zobrist.cpp
    src/board.cpp
    src/movegen.cpp
    src/eval.cpp
    src/tt.cpp
    src/search.cpp
    src/uci.cpp
    src/engine.cpp
)

target_include_directories(chessmind_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(chessmind_core
    PRIVATE
        chessmind_warnings
)

# Executable target
add_executable(chessmind
    src/main.cpp
)

target_link_libraries(chessmind
    PRIVATE
        chessmind_core
        chessmind_warnings
)

# Unit tests for foundational types and bitboards
add_executable(test_types
    tests/test_types.cpp
)

target_link_libraries(test_types
    PRIVATE
        chessmind_core
        chessmind_warnings
)

# Unit and Perft test target
add_executable(test_perft
    tests/test_perft.cpp
)

target_link_libraries(test_perft
    PRIVATE
        chessmind_core
        chessmind_warnings
)

# Comprehensive Move Generation and Turn Audit test target
add_executable(test_audit
    tests/test_audit.cpp
)

target_link_libraries(test_audit
    PRIVATE
        chessmind_core
        chessmind_warnings
)

# Engine Benchmark target
add_executable(benchmark_suite
    benchmarks/benchmark.cpp
)

target_link_libraries(benchmark_suite
    PRIVATE
        chessmind_core
        chessmind_warnings
)

