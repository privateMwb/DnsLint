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

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(
    dnslint_backend
    ${APP_SOURCES}
)

target_include_directories(
    dnslint_backend
    PRIVATE
        include
        src
)

target_link_libraries(
    dnslint_backend
    PRIVATE
        DnsPro
        FalconHTTP
        ThrottlePro
        MiniDB
)
