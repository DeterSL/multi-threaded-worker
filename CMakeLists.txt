include(FetchContent)

cmake_minimum_required(VERSION 3.18)
project(detersl-worker)
set(CMAKE_CXX_STANDARD 17)


set(VERONA_RT_PATH "external/verona-rt")
set(SNMALLOC_PATH "external/snmalloc")

add_executable(detersl-worker src/main.cc)

target_include_directories(detersl-worker PRIVATE ${VERONA_RT_PATH}/src/rt)
target_include_directories(detersl-worker PRIVATE ${SNMALLOC_PATH}/src)
target_compile_definitions(detersl-worker PRIVATE -DSNMALLOC_USE_WAIT_ON_ADDRESS=1)
target_compile_definitions(detersl-worker PRIVATE -DSNMALLOC_PASS_THROUGH)
