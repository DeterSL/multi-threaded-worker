cmake_minimum_required(VERSION 3.18)
project(detersl-worker)
set(CMAKE_CXX_STANDARD 20)
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

find_package(simdjson CONFIG QUIET)
if (NOT TARGET simdjson AND TARGET simdjson::simdjson)
    add_library(simdjson ALIAS simdjson::simdjson)
endif()
if (NOT TARGET simdjson)
    message(STATUS "simdjson not found, fetching...")
    FetchContent_Declare(
        simdjson
        GIT_REPOSITORY https://github.com/simdjson/simdjson.git
        GIT_TAG        v4.2.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(simdjson)
endif()

find_package(OpenSSL REQUIRED)

message(STATUS "OpenSSL_VERSION = ${OpenSSL_VERSION}")
message(STATUS "OPENSSL_INCLUDE_DIR = ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OPENSSL_SSL_LIBRARY = ${OPENSSL_SSL_LIBRARY}")
message(STATUS "OPENSSL_CRYPTO_LIBRARY = ${OPENSSL_CRYPTO_LIBRARY}")

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
)
FetchContent_MakeAvailable(cpp_httplib)

FetchContent_Declare(
    atomic_queue
    GIT_REPOSITORY https://github.com/max0x7ba/atomic_queue.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(atomic_queue)

find_path(NATS_INCLUDE_DIR nats/nats.h)
find_library(NATS_LIBRARY NAMES nats nats_static)
if (NOT NATS_INCLUDE_DIR OR NOT NATS_LIBRARY)
    message(WARNING "NATS C client not found. You must install 'nats.c' (headers+lib).")
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
    /usr/include
)

target_compile_definitions(detersl_common INTERFACE
    -DSNMALLOC_USE_WAIT_ON_ADDRESS=1
    -DSNMALLOC_PASS_THROUGH
)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
  target_compile_options(detersl_common INTERFACE -mcx16)
endif()

target_link_libraries(detersl_common INTERFACE
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ------------------------------------------------------------
# Executable
# ------------------------------------------------------------

add_library(detersl_worker_core STATIC
    src/runner.cpp
    src/registry.cpp
    src/registrar.cpp
    src/scheduling.cpp
    src/graph.cpp
    src/status.cpp
    src/utils.cpp
    src/nats_raii.cpp
    src/nats-worker.cpp
)

add_dependencies(detersl_worker_core detersl_rust)

target_link_libraries(detersl_worker_core PUBLIC
    detersl_common
    nlohmann_json::nlohmann_json
    simdjson
    httplib::httplib
)

if (TARGET atomic_queue::atomic_queue)
    target_link_libraries(detersl_worker_core PUBLIC atomic_queue::atomic_queue)
else()
    target_include_directories(detersl_worker_core PUBLIC ${atomic_queue_SOURCE_DIR}/include)
endif()

target_include_directories(detersl_worker_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${NATS_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/wasm-exec-env_build/cxxbridge
)

if (NATS_LIBRARY)
    target_link_libraries(detersl_worker_core PUBLIC ${NATS_LIBRARY})
endif()

add_executable(detersl-worker 
    src/main.cpp
)

target_link_libraries(detersl-worker PRIVATE
    detersl_worker_core
    detersl::ffi
    OpenSSL::SSL
    OpenSSL::Crypto
    dl
    pthread
)

target_include_directories(detersl-worker PRIVATE ${NATS_INCLUDE_DIR})
if (NATS_LIBRARY)
  target_link_libraries(detersl-worker PRIVATE ${NATS_LIBRARY})
endif()

target_include_directories(detersl-worker PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(detersl-worker PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY})

if (UNIX AND NOT APPLE)
  target_link_options(detersl-worker PRIVATE -Wl,--no-as-needed)
endif()

target_compile_options(detersl_worker_core PRIVATE
  $<$<CONFIG:Release>:-O3 -march=native>
)

target_link_options(detersl-worker PRIVATE
  $<$<CONFIG:Release>:-flto>
)
