cmake_minimum_required(VERSION 3.16)

include(FindPkgConfig)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS_DEBUG_INIT "-Wall -Wextra -Werror -lefence -g3 -O0")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Wall -O3")

# set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
# set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")


find_package(Boost REQUIRED)
find_package(PkgConfig REQUIRED)

include_directories(
    "${PROJECT_BINARY_DIR}"
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src

    SYSTEM
    ${PROJECT_SOURCE_DIR}/external/spdlog-1.9.0
    ${PROJECT_SOURCE_DIR}/external/tracy
    )

set(CMAKE_CXX_CLANG_TIDY clang-tidy --format-style=google --checks=clang-diagnostic-*,clang-analyzer-*,-*,bugprone*,modernize*,performance*)


add_definitions(-DVISIBILITY_ATTRIBUTE)

add_executable(flute_sender_program flute_sender_program.cpp)
add_executable(flute_receiver flute_receiver.cpp)
add_library(flute_retriever SHARED flute_retriever.cpp)
add_library(flute_sender SHARED flute_sender.cpp)
add_library(flute_server SHARED flute_server.cpp)
add_library(flute_tester SHARED flute_tester.cpp)

target_link_libraries( flute_sender_program
    PUBLIC
    spdlog::spdlog
    config++
    flute
    pthread
    TracyClient
)
target_link_libraries( flute_receiver
    PUBLIC
    spdlog::spdlog
    config++
    flute
    pthread
    TracyClient
)
target_link_libraries( flute_retriever
    PUBLIC
    spdlog::spdlog
    config++
    flute
    pthread
    TracyClient
)
target_link_libraries( flute_sender
    PUBLIC
    spdlog::spdlog
    config++
    flute
    pthread
    TracyClient
)
target_link_libraries( flute_server
    PUBLIC
    spdlog::spdlog
    config++
    flute
    pthread
    TracyClient
)
target_link_libraries( flute_tester
    PUBLIC
    spdlog::spdlog
    config++
    flute
    pthread
    TracyClient
)