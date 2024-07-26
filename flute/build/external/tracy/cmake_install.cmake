# Install script for directory: /home/casper/UGent/5GBDash/flute/external/tracy

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/casper/UGent/5GBDash/flute/build/external/tracy/libTracyClient.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tracy" TYPE FILE FILES
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/TracyC.h"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/Tracy.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/TracyD3D11.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/TracyD3D12.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/TracyLua.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/TracyOpenCL.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/TracyOpenGL.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/tracy/TracyVulkan.hpp"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/client" TYPE FILE FILES
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/tracy_concurrentqueue.h"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/tracy_rpmalloc.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/tracy_SPSCQueue.h"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyArmCpuTable.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyCallstack.h"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyCallstack.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyCpuid.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyDebug.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyDxt1.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyFastVector.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyLock.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyProfiler.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyRingBuffer.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyScoped.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyStringHelpers.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracySysPower.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracySysTime.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracySysTrace.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/client/TracyThread.hpp"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/common" TYPE FILE FILES
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/tracy_lz4.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/tracy_lz4hc.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyAlign.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyAlloc.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyApi.h"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyColor.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyForceInline.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyMutex.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyProtocol.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyQueue.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracySocket.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyStackFrames.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracySystem.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyUwp.hpp"
    "/home/casper/UGent/5GBDash/flute/external/tracy/public/common/TracyYield.hpp"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyTargets.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyTargets.cmake"
         "/home/casper/UGent/5GBDash/flute/build/external/tracy/CMakeFiles/Export/share/Tracy/TracyTargets.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyTargets-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyTargets.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "/home/casper/UGent/5GBDash/flute/build/external/tracy/CMakeFiles/Export/share/Tracy/TracyTargets.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "/home/casper/UGent/5GBDash/flute/build/external/tracy/CMakeFiles/Export/share/Tracy/TracyTargets-release.cmake")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "/home/casper/UGent/5GBDash/flute/build/external/tracy/TracyConfig.cmake")
endif()

