# ============================================================
# CMakeLists.txt — backend
# ============================================================
#
# Top-level build file for backend/. Any git submodules vendored
# under external/ (see .gitmodules once you add one) get pulled
# in via add_subdirectory below.
# ============================================================

cmake_minimum_required(VERSION 3.20)

project(
    DnsLint
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ============================================================================
# Submodules (untouched, upstream -- see .gitmodules)
# ============================================================================

# A submodule vendored under external/ often exposes its own
# BUILD_TESTS / BUILD_BENCHMARKS / BUILD_EXAMPLES options,
# defaulting ON in its own CMakeLists.txt. Force them off here
# before add_subdirectory() picks up those defaults -- this repo
# only wants the library itself, not its test/benchmark/example
# suites.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESSION OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# external/dnsresolver   -> DnsPro       (Builder/Parser/Packet types)
# external/falconhttp    -> FalconHTTP   (HTTP server, routing, middleware)
# external/ratethrottle  -> ThrottlePro  (rate limiter used by RateLimiterMiddleware.h)
# external/minidatabase  -> MiniDB       (check-history persistence used by HistoryStore.h)
#
# NOTE: if a submodule itself vendors independent copies of other shared
# libraries, and more than one submodule vendors the *same* shared
# dependency (all three of these vendor VectorPro, for instance), linking
# can fail with "multiple definition of ..." -- that's a submodule-level
# packaging issue, not something wrong with this file. Try building
# first; only chase that fix if the linker actually complains.
add_subdirectory(external/dnsresolver)
add_subdirectory(external/falconhttp)
add_subdirectory(external/ratethrottle)
add_subdirectory(external/minidatabase)

# ============================================================================
# Executable
# ============================================================================

# Everything under src/ except main.cpp is built once, as a static library,
# so the server, the unit tests and the fuzz harnesses all link the same code.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")

add_library(dnslint_core STATIC ${CORE_SOURCES})

target_include_directories(
    dnslint_core
    PUBLIC
        include
        src
)

target_link_libraries(
    dnslint_core
    PUBLIC
        DnsPro
        FalconHTTP
        ThrottlePro
        MiniDB
)

add_executable(dnslint_backend src/main.cpp)

target_link_libraries(dnslint_backend PRIVATE dnslint_core)

# ============================================================================
# Tests
# ============================================================================

# Named DNSLINT_BUILD_TESTS, not BUILD_TESTS: the latter is force-set OFF
# above for the submodules.
option(DNSLINT_BUILD_TESTS "Build the DnsLint test suite" ON)

if(DNSLINT_BUILD_TESTS)
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )

    add_executable(dnslint_tests ${TEST_SOURCES})

    target_include_directories(
        dnslint_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(dnslint_tests PRIVATE dnslint_core)
endif()

# ============================================================================
# Fuzz harnesses (built by ClusterFuzzLite; see ../.clusterfuzzlite/)
# ============================================================================

# OFF by default: the harnesses need a fuzzing engine to link against, which
# ClusterFuzzLite provides through $LIB_FUZZING_ENGINE. Every
# fuzz/fuzz_<name>.cpp becomes its own executable, no edit needed here.
option(DNSLINT_BUILD_FUZZERS "Build the libFuzzer harnesses" OFF)

if(DNSLINT_BUILD_FUZZERS)
    file(GLOB FUZZ_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/fuzz_*.cpp"
    )

    foreach(fuzz_source ${FUZZ_SOURCES})
        get_filename_component(fuzz_name ${fuzz_source} NAME_WE)

        add_executable(${fuzz_name} ${fuzz_source})

        target_link_libraries(
            ${fuzz_name}
            PRIVATE
                dnslint_core
                $ENV{LIB_FUZZING_ENGINE}
        )
    endforeach()
endif()
