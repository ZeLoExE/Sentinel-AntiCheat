# ============================================================================
# Sentinel AntiCheat - CMake Build System
# ============================================================================
# A professional server-side AntiCheat for Counter-Strike 1.6
# Supports: ReHLDS, ReGameDLL, ReAPI, Metamod, AMX Mod X
# Platforms: Linux (GCC/Clang), Windows (MSVC/MinGW)
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(SentinelAntiCheat VERSION 1.0.0 LANGUAGES CXX)

# ── C++ Standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build Types ───────────────────────────────────────────────────────────
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel")

# ── Options ───────────────────────────────────────────────────────────────
option(SENTINEL_BUILD_TESTS   "Build unit and integration tests" ON)
option(SENTINEL_BUILD_DOCS    "Generate documentation" OFF)
option(SENTINEL_BUILD_EXAMPLES "Build example plugins" ON)
option(SENTINEL_ENABLE_DEBUG  "Enable debug logging and assertions" OFF)
option(SENTINEL_USE_SQLITE    "Use SQLite database backend" ON)

# ── Platform Detection ────────────────────────────────────────────────────
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DSENTINEL_WINDOWS)
    set(PLATFORM_LIBS ws2_32.lib)
else()
    add_definitions(-DSENTINEL_LINUX)
    set(PLATFORM_LIBS pthread dl)
endif()

# ── Compiler Flags ────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /utf-8 /MP /Zc:__cplusplus)
    add_compile_options(/wd4100 /wd4201 /wd4244 /wd4267)  # Suppress less critical warnings
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    add_compile_options(-fvisibility=hidden -fPIC)
endif()

# ── Dependencies ──────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# SQLite3
if(SENTINEL_USE_SQLITE)
    find_package(SQLite3 QUIET)
    if(NOT SQLite3_FOUND)
        message(STATUS "SQLite3 not found, using bundled version")
        add_subdirectory(third_party/sqlite3)
        set(SQLite3_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/sqlite3)
        set(SQLite3_LIBRARIES sqlite3)
    endif()
endif()

# nlohmann/json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann/json not found, using bundled header")
    set(JSON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/json)
else()
    set(JSON_INCLUDE_DIR ${nlohmann_json_INCLUDE_DIRS})
endif()

# yaml-cpp
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found, using bundled")
    set(YAML_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/yaml-cpp/include)
else()
    set(YAML_INCLUDE_DIR ${yaml-cpp_INCLUDE_DIRS})
endif()

# httplib (for REST API)
find_package(httplib QUIET)
if(NOT httplib_FOUND)
    message(STATUS "httplib not found, using bundled header")
    set(HTTPLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/httplib)
else()
    set(HTTPLIB_INCLUDE_DIR ${httplib_INCLUDE_DIRS})
endif()

# ── Include Directories ───────────────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${SQLite3_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIR}
    ${YAML_INCLUDE_DIR}
    ${HTTPLIB_INCLUDE_DIR}
)

# ── Sources (collected via glob for simplicity) ──────────────────────────
file(GLOB_RECURSE SENTINEL_SOURCES
    src/*.h
    src/*.cpp
    src/*.hpp
)

# ── Main Library ──────────────────────────────────────────────────────────
add_library(sentinel_anticheat SHARED ${SENTINEL_SOURCES})
target_link_libraries(sentinel_anticheat PRIVATE
    Threads::Threads
    ${SQLite3_LIBRARIES}
    ${PLATFORM_LIBS}
)
set_target_properties(sentinel_anticheat PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PREFIX ""
    SUFFIX ".so"
)

if(WIN32)
    set_target_properties(sentinel_anticheat PROPERTIES
        SUFFIX ".dll"
        PREFIX ""
    )
endif()

# ── Tests ─────────────────────────────────────────────────────────────────
if(SENTINEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Examples ───────────────────────────────────────────────────────────────
if(SENTINEL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Install ────────────────────────────────────────────────────────────────
install(TARGETS sentinel_anticheat
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY config/ DESTINATION etc/sentinel)
install(FILES README.md LICENSE DESTINATION share/doc/sentinel)

# ── Custom Targets ────────────────────────────────────────────────────────
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning build directory..."
)

# ── Message ───────────────────────────────────────────────────────────────
message(STATUS "========================================")
message(STATUS "  Sentinel AntiCheat v${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Platform:     ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:     ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:        ${SENTINEL_BUILD_TESTS}")
message(STATUS "  Docs:         ${SENTINEL_BUILD_DOCS}")
message(STATUS "========================================")
