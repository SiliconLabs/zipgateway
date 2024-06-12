find_package(LibUSB 1.0)


include_directories(
	.
	# Include paths from the ZIP Gateway
	${CMAKE_SOURCE_DIR}/Z-Wave/include
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/utls/
	${CMAKE_SOURCE_DIR}/src/zwpgrmr/
	${CMAKE_SOURCE_DIR}/src/serialapi
	# Contiki include directory
	${CONTIKI_DIR}
	${CONTIKI_DIR}/platform/linux
  ${CONTIKI_DIR}/platform/linux/dev
  ${CONTIKI_DIR}/cpu/native/.
  ${CONTIKI_DIR}/cpu/native/net
  ${CONTIKI_DIR}/core/dev
  ${CONTIKI_DIR}/core/lib
  ${CONTIKI_DIR}/core/net
  ${CONTIKI_DIR}/core/sys
  ${CONTIKI_DIR}/core
  ${LibUSB_INCLUDE_DIRS}
)

add_executable(zw_programmer
	zw_programmer.c
	# Other sources from the ZIP Gateway without any modification
	${CMAKE_SOURCE_DIR}/src/txmodem.c
	${CMAKE_SOURCE_DIR}/src/serialapi/port-timer-linux.c
	${CONTIKI_DIR}/cpu/native/linux-serial.c
	${CONTIKI_DIR}/core/lib/assert.c
	${CMAKE_SOURCE_DIR}/src/serialapi/Serialapi.c
  ${CMAKE_SOURCE_DIR}/src/serialapi/conhandle.c
  ${CMAKE_SOURCE_DIR}/src/serialapi/nvm_tools.c
	${CMAKE_SOURCE_DIR}/src/serialapi/sdk_versioning.c
	${CMAKE_SOURCE_DIR}/src/zwpgrmr/zpg.c
	${CMAKE_SOURCE_DIR}/src/zwpgrmr/zpgp.c
	${CMAKE_SOURCE_DIR}/src/zwpgrmr/linux_usb_interface.c
	${CMAKE_SOURCE_DIR}/src/zwpgrmr/linux_serial_interface.c
	${CMAKE_SOURCE_DIR}/src/zwpgrmr/crc32.c
	${CMAKE_SOURCE_DIR}/src/utls/zgw_crc.c
	${CMAKE_SOURCE_DIR}/src/utls/zgw_nodemask.c
	${CMAKE_SOURCE_DIR}/src/utls/hex_to_bin.c
)

target_link_libraries(zw_programmer ${LibUSB_LIBRARIES})
if( NOT APPLE )
  target_link_libraries(zw_programmer rt)
endif()

install(TARGETS zw_programmer DESTINATION bin)
