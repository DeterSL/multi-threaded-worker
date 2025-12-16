cmake_minimum_required(VERSION 3.18)
project(detersl-worker)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, fetching...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ------------------------------------------------------------
# Shared header dependencies (Verona, snmalloc, etc)
# ------------------------------------------------------------
set(VERONA_RT_PATH "external/verona-rt")
set(SNMALLOC_PATH "external/snmalloc")

add_library(detersl_common INTERFACE)

target_include_directories(detersl_common INTERFACE
    ${VERONA_RT_PATH}/src/rt
    ${SNMALLOC_PATH}/src
)

target_compile_definitions(detersl_common INTERFACE
    -DSNMALLOC_USE_WAIT_ON_ADDRESS=1
    -DSNMALLOC_PASS_THROUGH
)

# ------------------------------------------------------------
# Executable
# ------------------------------------------------------------
add_executable(detersl-worker 
    src/main.cpp
    src/runner.cpp
)

target_link_libraries(detersl-worker PRIVATE
    detersl_common
    nlohmann_json::nlohmann_json
)

# ------------------------------------------------------------
# Shared plugin
# ------------------------------------------------------------
# Get all cpp files under functions/
file(GLOB FUNCTION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/functions/*.cpp")

foreach(FN ${FUNCTION_SOURCES})
    get_filename_component(BASENAME ${FN} NAME_WE)

    add_library(${BASENAME} SHARED ${FN})

    target_link_libraries(${BASENAME} PRIVATE
        detersl_common
        nlohmann_json::nlohmann_json
    )

    set_target_properties(${BASENAME} PROPERTIES
        PREFIX ""                         # removes 'lib'
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()
