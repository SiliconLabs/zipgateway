
cmake_minimum_required(VERSION 3.7)

# If toolchain is not defined, do run "uname -m" to determine if it is a x86_64, and load i386 cross compilation toolchain
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  execute_process(COMMAND uname OUTPUT_VARIABLE ZW_HOST_OS)
  execute_process(COMMAND uname -m OUTPUT_VARIABLE ZW_HOST_ARCH)
  if(${ZW_HOST_OS} MATCHES "Darwin")
    message(STATUS "Compiling for on MacOs, loading 64 bit MacOs toolchain")
    include(cmake/macos.cmake)
  elseif(${ZW_HOST_ARCH} MATCHES "x86_64")
    message(STATUS "Compiling for host on x86_64 system, loading cmake/i386.cmake toolchain for 32 bit cross compile")
    include(cmake/i386.cmake)
  endif()
else()
  message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
endif()

if(DEFINED JSON_C_SRC)
  message(STATUS "Custom json-c install path: ${JSON_C_SRC}")
endif()
if(DEFINED OPENSSL_SRC)
  message(STATUS "Custom openssl install path: ${OPENSSL_SRC}")
endif()
if(DEFINED LIBUSB_SRC)
  message(STATUS "Custom libusb install path: ${LIBUSB_SRC}")
endif()

project(zipgateway  C CXX)

if(NOT CMAKE_CONFIGURATION_TYPES)
  if(NOT CMAKE_BUILD_TYPE)
    # Encourage user to specify build type.
    message(STATUS "Setting build type to 'Debug' as none was specified.")
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY VALUE "Debug")
  endif()
endif()

include( ExternalProject )
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pipe -Werror -Wparentheses -Wno-array-bounds -Wno-unused-result -std=gnu99 -D_DEFAULT_SOURCE")


if(DEFINED ENABLE_CODE_COVERAGE)
  message(STATUS "Adding GCOV flags")
  # Add gcov support
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
  # Use the ENABLE_CODE_COVERAGE variable to prevent warning when running cmake
  set(ignoreMe "${ENABLE_CODE_COVERAGE}")
endif()

find_program(CMAKE_CUSTOM_C_CPPCHECK NAMES cppcheck)
if (CMAKE_CUSTOM_C_CPPCHECK)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  add_custom_target(cppcheck-analysis COMMAND ${CMAKE_CUSTOM_C_CPPCHECK} --project="${PROJECT_BINARY_DIR}/compile_commands.json" -j12 --xml --xml-version=2 --enable=warning,portability --output-file="${PROJECT_BINARY_DIR}/cppcheck_results.xml")
endif()

if(DEFINED CUSTOM_PKG_CONFIG_PATH)
  set(ENV{PKG_CONFIG_PATH} ${CUSTOM_PKG_CONFIG_PATH})
endif()
set(CMAKE_C_OUTPUT_EXTENSION_REPLACE 1)

add_definitions ( -DZIPGW -DDEBUG_S2_INCL_FSM -DDEBUG_S2_FSM)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

if (NOT DEFINED SKIP_TESTING)
  include(CTest)
  message(STATUS "Enable testing")
  enable_testing()
  find_program( MEMORYCHECK_COMMAND valgrind )
  if(${MEMORYCHECK_COMMAND})
    message(STATUS "Found Vagrind: " ${MEMORYCHECK_COMMAND})
    set( MEMORYCHECK_COMMAND_OPTIONS "--trace-children=yes --leak-check=full" )
  endif()
else ()
  # Use the SKIP_TESTING variable to prevent warning when running cmake
  set(ignoreMe "${SKIP_TESTING}")
endif()

if(NOT ANDROID)
  find_package(LibUSB 1.0 REQUIRED)
  find_package(OpenSSL 1.1.0 REQUIRED)
endif()

find_package(BISON 3.0.4 REQUIRED)
find_package(FLEX REQUIRED)

set(ZWAVE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Z-Wave/include )
set(CONTIKI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contiki)
set(ZGW_SRC_DIR ${CMAKE_SOURCE_DIR}/src)

# Include lib S2
set(LIBS2 "${CMAKE_CURRENT_SOURCE_DIR}/libs2" )

set(S2_LIBRARIES s2_controller s2crypto aes)

add_subdirectory( sqlite )
add_subdirectory( libs2 )
add_subdirectory ( systools )

if (NOT DEFINED SKIP_TESTING)
  add_subdirectory( test )
  message(STATUS "Adding unit test for zipgateway")
endif ()

set(TS2_INCLUDE_DIRS "${LIBS2}/transport_service")

include(GetGitRevisionDescription)

git_describe(VERSION --tags --match "ver*" )

if(NOT VERSION MATCHES "^ver")
  message(WARNING "log: Base is a tagged version: ${VERSION}, fallback to default (0)")
  set(VERSION ver0_0.00)
endif()
  
message(STATUS "log: parse the version information into pieces : ${VERSION}")
string(REGEX REPLACE "^ver([0-9]+)_.*" "\\1" VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_([0-9]+).*" "\\1" VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_[0-9]+.[0-9]+[0-9,R,C,_]*-.[0-9]*-(.*)" "\\1" VERSION_SHA1 "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_[0-9]+.([0-9]+).*" "\\1" PATCH_LEVEL "${VERSION}")
string(REGEX REPLACE "^ver[0-9]+_[0-9]+.[0-9]+([0-9,R,C,_]*).*" "\\1" VERSION_TWEAK "${VERSION}")
#    MESSAGE(STATUS "VERSION_TWEAK: " ${VERSION_TWEAK})
if (${VERSION_TWEAK} MATCHES ${VERSION})
  unset(VERSION_TWEAK)
endif()
if (${VERSION_SHA1} MATCHES ${VERSION})
  unset(VERSION_SHA1)
endif()

configure_file(pkgconfig.h.cmake.in pkgconfig.h)

MESSAGE(STATUS "VERSION_MAJOR: " ${VERSION_MAJOR})
MESSAGE(STATUS "VERSION_MINOR: " ${VERSION_MINOR})
MESSAGE(STATUS "VERSION_SHA1: " ${VERSION_SHA1})
MESSAGE(STATUS "PATCH_LEVEL: " ${PATCH_LEVEL})
MESSAGE(STATUS "VERSION_TWEAK: " ${VERSION_TWEAK})

set(VERSION_SHORT "${VERSION_MAJOR}.${VERSION_MINOR}.${PATCH_LEVEL}")
if(DEFINED VERSION_SHA1)
  set(VERSION_SHORT "${VERSION_SHORT}-${VERSION_SHA1}")
endif()

MESSAGE(STATUS "VERSION_SHORT: " ${VERSION_SHORT})

option(DISABLE_DTLS "Disable DTLS and TLS" OFF)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/files/zipgateway.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/files/zipgateway)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/files/zgw_import.sh.in ${CMAKE_CURRENT_BINARY_DIR}/files/zgw_import.sh @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/files/zgw_recover.sh.in ${CMAKE_CURRENT_BINARY_DIR}/files/zgw_recover.sh @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/files/zgw_backup.sh.in ${CMAKE_CURRENT_BINARY_DIR}/files/zgw_backup.sh @ONLY)

set(CONTIKI contiki )

add_definitions(  -DSERIAL_LOG -DSUPPORTS_MDNS
-DSECURITY_SUPPORT -DCONTROLLER_CHANGE_SUPPORT -D__ROUTER_VERSION__
-DPROVISIONING_LIST_SIZE=232
-DMCAST_MAX_GROUPS=100)

add_subdirectory( contiki )
add_subdirectory( src )

message (STATUS "openssl libraries: ${OPENSSL_LIBRARIES}")
message (STATUS "libusb libraries: ${LibUSB_LIBRARIES}")

if(NOT ANDROID)

if(NOT OPENWRT)
  install(FILES files/radvd.conf DESTINATION /etc/)
endif()

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/files/zipgateway DESTINATION /etc/init.d)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/files/zgw_import.sh DESTINATION bin )
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/files/zgw_recover.sh DESTINATION bin )
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/files/zgw_backup.sh DESTINATION bin )


install(PROGRAMS 
  files/zipgateway.tun files/zipgateway.fin 
  files/zipgateway_node_identify_rpi3b+_led.sh 
  files/zipgateway_node_identify_generic.sh 
  DESTINATION etc )


install(FILES 
  files/zipgateway_provisioning_list.cfg 
  WRTpackage/files/Portal.ca_x509.pem  
  WRTpackage/files/ZIPR.x509_1024.pem 
  files/zipgateway.cfg
DESTINATION etc)

install(FILES
  WRTpackage/files/ZIPR.key_1024.pem
  DESTINATION etc PERMISSIONS OWNER_WRITE OWNER_READ
)
FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/conffiles
"/etc/radvd.conf\n"
"${CMAKE_INSTALL_PREFIX}/etc/zipgateway.cfg\n"
"${CMAKE_INSTALL_PREFIX}/etc/Portal.ca_x509.pem\n"
"${CMAKE_INSTALL_PREFIX}/etc/ZIPR.x509_1024.pem\n"
"${CMAKE_INSTALL_PREFIX}/etc/ZIPR.key_1024.pem\n"
"${CMAKE_INSTALL_PREFIX}/etc/zipgateway_provisioning_list.cfg\n"
)

if(OPENWRT)
  install(PROGRAMS files/udhcpc-br-zip
          DESTINATION /etc/init.d)
  install(DIRECTORY DESTINATION var/lib/zipgateway)
endif()

execute_process(COMMAND basename $ENV{PWD} COMMAND tr -d "[[:space:]]" OUTPUT_VARIABLE cwd)
#MESSAGE( STATUS " CMAKE_BINARY_DIR: " ${cwd})

# Build Debian package
SET(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${PATCH_LEVEL})
set(CPACK_SOURCE_GENERATOR "TBZ2")
SET(CPACK_GENERATOR "DEB")
SET(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
# If system processor is armv7l set CPACK_DEBIAN_PACKAGE_ARCHITECTURE to armhf
# Else set tit to ${CMAKE_SYSTEM_PROCESSOR}
if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l")
  SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
else()
  SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Silicon Labs") #required
SET(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-${CMAKE_SYSTEM_NAME}")
if (CPACK_FILE_NAME_EXTRA)
  SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-${CPACK_FILE_NAME_EXTRA}${CPACK_FILE_NAME_EXTRA2}")
endif()
if(${DISABLE_DTLS})
  SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-nodtls")
endif()

SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")
if (NOT "${OS_RELEASE_VERSION_ID}" STREQUAL "20.04") 
  SET(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/postinst;${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/config;${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/postrm;${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/templates;${CMAKE_CURRENT_BINARY_DIR}/conffiles" )
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libusb-1.0-0, libssl1.1, radvd, parprouted, bridge-utils, libjson-c2 | libjson-c3 | libjson-c4, net-tools, zip, unzip, logrotate")
endif()
# Set Source package dependds
SET(CPACK_SOURCE_IGNORE_FILES
"/build.*/"
"Jenkinsfile"
"docker/"
"/\\\\..*"
"/src/scxml/"
"/pkgconfig.h.cmake.in"
"/sonar-project.properties"
)
INCLUDE(CPack)

endif()

add_subdirectory( libzwaveip )
