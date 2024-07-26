cmake_minimum_required(VERSION 3.16)

project (libflute VERSION 0.12.0 LANGUAGES C CXX)

option(ENABLE_RAPTOR "Enable support for Raptor FEC" ON)
# Comment out to disable Tracy profiler, OFF does not work
option(TRACY_ENABLE "Enable support for Tracy profiler" ON)
option(TRACY_NO_BROADCAST "" OFF)
option(TRACY_ON_DEMAND "" ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_C_FLAGS_DEBUG_INIT "-Wall -Wextra -Werror -g3 -O0")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-Wall -Wextra -Werror -g3 -O0")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Wall -O3")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Wall -O3")

# If debug, then enable asan
# if(CMAKE_BUILD_TYPE MATCHES Debug)
#   message(STATUS "Debug build detected, enabling ASAN")
#   set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
#   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
# endif()

set(OPENSSL_USE_STATIC_LIBS TRUE)
add_subdirectory(${PROJECT_SOURCE_DIR}/external/spdlog-1.9.0)
add_subdirectory(${PROJECT_SOURCE_DIR}/external/tinyxml2-debian-9.0.0+dfsg-3.1)
add_subdirectory(${PROJECT_SOURCE_DIR}/external/tracy)

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NETLINK REQUIRED IMPORTED_TARGET libnl-3.0)

add_subdirectory(examples)

include_directories(
    "${PROJECT_BINARY_DIR}"
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/raptor
    ${PROJECT_SOURCE_DIR}/external/tracy

    SYSTEM
    ${PROJECT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/external/spdlog-1.9.0
    ${PROJECT_SOURCE_DIR}/external/tinyxml2-debian-9.0.0+dfsg-3.1
    PkgConfig::NETLINK
    )

target_compile_options(spdlog PRIVATE -fPIC)
target_compile_options(tinyxml2 PRIVATE -fPIC)
target_compile_options(TracyClient PRIVATE -fPIC)

configure_file("include/Version.h.in" "Version.h")

link_directories(
    )

set(CMAKE_CXX_CLANG_TIDY clang-tidy)

if(ENABLE_RAPTOR)
  message(STATUS "Compiling raptor library for Raptor FEC support. To disable support build with -DENABLE_RAPTOR=OFF")
  add_library(raptor STATIC)
  target_sources(raptor
    PRIVATE
    external/raptor/bipartite.c
    external/raptor/decoder.c
    external/raptor/encoder.c
    external/raptor/galois.c
    external/raptor/gaussian.c
    external/raptor/pivoting.c
    external/raptor/random.c
    PUBLIC
    external/raptor/raptor.h
  )
  target_link_libraries(raptor PUBLIC m)
  target_compile_options(raptor PRIVATE -fPIC) # The library should have platform independent code.
else()
  message(STATUS "Skipping raptor library for Raptor FEC support. To enable support build with -DENABLE_RAPTOR=ON")
endif()

add_library(flute "")
target_sources(flute
  PRIVATE
    src/Component/Receiver.cpp
    src/Component/Retriever.cpp
    src/Component/Transmitter.cpp
    src/Metric/Gauge.cpp
    src/Metric/Metrics.cpp
    src/Metric/ThreadedCPUUsage.cpp
    src/Object/File.cpp
    src/Object/FileStream.cpp
    src/Object/FileBase.cpp
    src/Object/FileDeliveryTable.cpp
    src/Packet/AlcPacket.cpp
    src/Packet/EncodingSymbol.cpp
    src/Recovery/Client.cpp
    src/Recovery/Fetcher.cpp
    src/Utils/FakeNetworkSocket.cpp
    src/Utils/IpSec.cpp
    src/Utils/base64.cpp
  PUBLIC
    include/Component/Receiver.h
    include/Component/Retriever.h
    include/Component/Transmitter.h
    include/Fec/FecTransformer.h
    include/Metric/Gauge.h
    include/Metric/Metrics.h
    include/Metric/ThreadedCPUUsage.h
    include/Object/File.h
    include/Object/FileStream.h
    include/Object/FileBase.h
    include/Object/FileDeliveryTable.h
    include/Packet/AlcPacket.h
    include/Packet/EncodingSymbol.h
    include/Recovery/Client.h
    include/Recovery/Fetcher.h
    include/Utils/FakeNetworkSocket.h
    include/Utils/flute_types.h
    include/Utils/IpSec.h
    include/Utils/base64.h
  )
target_include_directories(flute
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include/
  )

target_compile_options(flute PRIVATE -fPIC) # The library should have platform independent code.

if(ENABLE_RAPTOR)
  add_compile_definitions(RAPTOR_ENABLED)
  target_include_directories(flute PUBLIC ${CMAKE_CURRENT_LIST_DIR}/external/raptor/)
  target_sources(flute
    PRIVATE
	  src/Fec/RaptorFEC.cpp
	PUBLIC
	  include/Fec/RaptorFEC.h
  )
  target_link_libraries(flute PUBLIC raptor)
endif()

if (TRACY_ENABLE)
  message(STATUS "Compiling with Tracy profiler support. To disable, uncomment TRACY_ENABLE in CMakeLists.txt")
  target_include_directories(flute PUBLIC ${CMAKE_CURRENT_LIST_DIR}/external/tracy/public/)
  target_sources(flute
    PRIVATE
	  external/tracy/public/TracyClient.cpp
    PUBLIC
    external/tracy/public/tracy/Tracy.hpp
  )
  target_link_libraries(flute PUBLIC TracyClient)
endif()

target_link_libraries( flute
    PUBLIC
    spdlog::spdlog
    z
    pthread
    OpenSSL::Crypto
    tinyxml2::tinyxml2
    PkgConfig::NETLINK
)
