cmake_minimum_required(VERSION 3.18)
project(detersl-worker)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_BUILD_RPATH "/opt/homebrew/lib")
    set(CMAKE_INSTALL_RPATH "/opt/homebrew/lib")
endif()

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

set(WASM_EXEC_ENV_PATH "${CMAKE_CURRENT_LIST_DIR}/external/wasm-exec-env" CACHE PATH "Path to wasm-exec-env crate")
add_subdirectory(${WASM_EXEC_ENV_PATH} wasm-exec-env_build)

add_library(detersl_common INTERFACE)

target_include_directories(detersl_common INTERFACE
    ${VERONA_RT_PATH}/src/rt
    ${SNMALLOC_PATH}/src
    ${CMAKE_BINARY_DIR}/wasm-exec-env_build/cxxbridge
    ${WASM_EXEC_ENV_PATH}/include
)

target_compile_definitions(detersl_common INTERFACE
    -DSNMALLOC_USE_WAIT_ON_ADDRESS=1
    -DSNMALLOC_PASS_THROUGH
)

target_compile_options(detersl_common INTERFACE -mcx16)

# ------------------------------------------------------------
# Executable
# ------------------------------------------------------------

add_library(detersl_worker_core STATIC
    src/runner.cpp
    src/scheduling.cpp
)

target_link_libraries(detersl_worker_core PUBLIC
    detersl_common
    nlohmann_json::nlohmann_json
)

target_include_directories(detersl_worker_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(detersl-worker 
    src/main.cpp
)

target_link_libraries(detersl-worker PRIVATE
    detersl_worker_core
)

# add_executable(detersl-worker 
#     src/main.cpp
#     src/runner.cpp
#     src/scheduling.cpp
# )

# target_link_libraries(detersl-worker PRIVATE
#     detersl_common
#     nlohmann_json::nlohmann_json
# )

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
    
    target_include_directories(${BASENAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    set_target_properties(${BASENAME} PROPERTIES
        PREFIX ""                         # removes 'lib'
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()
