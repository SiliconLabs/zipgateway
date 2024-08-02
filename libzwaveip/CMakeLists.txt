project(libzwaveip)
cmake_minimum_required(VERSION 2.9)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

#TODO better versioning here when releasing


# Enforce build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type, options are: Debug Release." FORCE)
endif()

if (NOT DEFINED SKIP_TESTING)
  include(CTest)
  message(STATUS "Enable testing")
  enable_testing()
endif()

add_compile_options(-Wall -Werror)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -std=gnu99 -Og -ggdb -DDEBUG=1")
if("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  # Use Address Sanitizer for Debug builds when using Clang
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
  add_definitions(-DATTR_UNUSED=__attribute__\(\(unused\)\))
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  # Enable gcc buffer overflow detection by setting _FORTIFY_SOURCE
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_FORTIFY_SOURCE=2")
  add_definitions(-DATTR_UNUSED=__attribute__\(\(unused\)\))
else()
  add_definitions(-DATTR_UNUSED)
endif()
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -std=gnu99 -Os")

find_package(OpenSSL 1.1.0 REQUIRED)

find_library(AVAHI_LIBRARY-COMMON NAMES avahi-common)
find_library(AVAHI_LIBRARY-CLIENT NAMES avahi-client)
find_path(AVAHI_CLIENT_INCLUDE_DIR avahi-client/client.h)

if(AVAHI_LIBRARY-CLIENT)
    add_definitions(-DWITH_MDNS)
    set(MDNS_LIBS ${AVAHI_LIBRARY-COMMON} ${AVAHI_LIBRARY-CLIENT})
    include_directories(${AVAHI_CLIENT_INCLUDE_DIR})
elseif(APPLE)
    # macOS has mDNS support built-in
    add_definitions(-DWITH_MDNS)
endif()

find_package(LibXml2)

message (STATUS "openssl libraries: ${OPENSSL_LIBRARIES}")

if(NOT PROJECT_IS_TOP_LEVEL)
  set(VERSION_SHORT "latest")
  set(CMAKE_PROJECT_NAME "libzwaveip")
else()

  # Figure out a proper versioning for the build
  if(NOT GIT_FOUND)
    find_package(Git QUIET)
  endif()

  execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --match "ver*"
    OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(NOT VERSION MATCHES "^ver")
  set(VERSION ver0_0.00)
endif()

message(STATUS "log: parse the version information into pieces: ${VERSION}")
string(REGEX REPLACE "^ver([0-9]+)_.*" "\\1" VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_([0-9]+).*" "\\1" VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_[0-9]+.[0-9]+[0-9,R,C,_]*-.[0-9]*-(.*)" "\\1" VERSION_SHA1 "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_[0-9]+.([0-9]+).*" "\\1" PATCH_LEVEL "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_[0-9]+.[0-9]+([0-9,R,C,_]*).*" "\\1" VERSION_TWEAK "${VERSION}")
if (${VERSION_TWEAK} MATCHES ${VERSION})
  unset(VERSION_TWEAK)
endif()
if (${VERSION_SHA1} MATCHES ${VERSION})
  unset(VERSION_SHA1)
endif()
set(VERSION_SHORT "${VERSION_MAJOR}.${VERSION_MINOR}.${PATCH_LEVEL}")
if (DEFINED VERSION_SHA1)
  set(VERSION_SHORT "${VERSION_SHORT}-${VERSION_SHA1}")
endif()

message(STATUS "VERSION_MAJOR: " ${VERSION_MAJOR})
message(STATUS "VERSION_MINOR: " ${VERSION_MINOR})
message(STATUS "PATCH_LEVEL: " ${PATCH_LEVEL})
message(STATUS "VERSION_TWEAK: " ${VERSION_TWEAK})
message(STATUS "VERSION_SHA1: " ${VERSION_SHA1})
message(STATUS "VERSION_SHORT: " ${VERSION_SHORT})

configure_file(pkgconfig.h.cmake.in pkgconfig.h)

add_subdirectory( libzwaveip )
add_subdirectory( xml )
add_subdirectory( libedit )
add_subdirectory( examples )
add_subdirectory( test )
add_subdirectory( doc )

if(CPack_CMake_INCLUDED EQUAL 1)
  install(FILES "config/ZWave_custom_cmd_classes.xml"
    DESTINATION "share/zwave/"
  )
else()
  message("warning: Legacy package for standalone building, to be deprecated")
  set(CPACK_SOURCE_GENERATOR "ZIP")
  set(CPACK_SOURCE_IGNORE_FILES
    "Jenkinsfile"
    "/build.*/"
  )

  set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
  set (CPACK_DEB_COMPONENT_INSTALL ON)
  INCLUDE(CPack)

  if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
    set(CPACK_FILE_NAME_EXTRA "stretch")
  else()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
  endif()
  set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${PATCH_LEVEL})
  set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${VERSION_SHORT}-Source")

  set(BINARY_ZIP_FILENAME libzwaveip-${VERSION_SHORT}-${CMAKE_SYSTEM_NAME})
  if (CPACK_FILE_NAME_EXTRA)
    set(BINARY_ZIP_FILENAME ${BINARY_ZIP_FILENAME}-${CPACK_FILE_NAME_EXTRA})
  endif()
  set(BINARY_ZIP_FILENAME ${BINARY_ZIP_FILENAME}-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}-Binaries)
  add_custom_target(binary_zip
                  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                  COMMAND mkdir -p ${BINARY_ZIP_FILENAME} &&
                          cp reference_client reference_listener
                          ZWave_custom_cmd_classes.xml ${BINARY_ZIP_FILENAME}/ &&
                          zip -r ${BINARY_ZIP_FILENAME}.zip
                          ${BINARY_ZIP_FILENAME}/)
  add_dependencies(binary_zip reference_client reference_listener)
endif()
