include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

set(TS2 "${LIBS2}/transport_service")
set(TS2_SRC ${TS2}/transport_service2.c ${TS2}/transport2_fsm.c)

add_subdirectory(analyzer)
add_subdirectory(doc)

BISON_TARGET(pvsparser pvs_cfg.y ${CMAKE_CURRENT_BINARY_DIR}/pvs_cfg.tab.c VERBOSE)
FLEX_TARGET(pvslexer pvs_cfg.l ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.c )
ADD_FLEX_BISON_DEPENDENCY(pvslexer pvsparser)

set(HANDLER_SRC
CC_InclusionController.c
CC_Gateway.c
CC_Version.c
CC_NetworkManagement.c
CC_NetworkManagement_queue.c
CC_PowerLevel.c
CC_ManufacturerSpecific.c
CC_ZWavePlus.c
CC_ZIPNaming.c
CC_Supervision.c
CC_InstallationAndMaintenance.c
CC_Indicator.c
CC_Time.c
CC_provisioning_list.c
CC_FirmwareUpdate.c
CC_Portal.c
CC_Wakeup.c
CC_Multicmd.c
)

set(GW_SRC
${HANDLER_SRC}
txmodem.c
${CMAKE_SOURCE_DIR}/src-gen/Secure_learn.c
Bridge_ip_assoc.c
Bridge_temp_assoc.c
Bridge.c
command_handler.c
ClassicZIPNode.c
Mailbox.c
RD_DataStore_Sqlite.c
RD_internal.c
ResourceDirectory.c
RD_probe_cc_version.c
zip_router_ipv6_utils.c
ZIP_Router.c
ZW_ZIPApplication.c
ZW_udp_server.c
crc32alg.c
dhcpc2.c
ipv46_nat.c
ipv4_interface.c
ipv46_if_handler.c
mDNSService.c
node_queue.c
serial_api_process.c
serialapi/Serialapi.c
serialapi/conhandle.c
serialapi/sdk_versioning.c
serialapi/nvm_tools.c
transport/Security_Scheme0.c
transport/ZW_PRNG.c
transport/ZW_SendDataAppl.c
transport/ZW_SendRequest.c
transport/rijndael-alg-fst.c
transport/security_layer.c
transport/S2_wrap.c
transport/s2_keystore.c
transport/zw_frame_buffer.c
#transport/S2_multicast_auto.c
#multicast_group_manager.c
utls/zgw_nodemask.c
utls/zgw_crc.c
utls/zgw_str.c
multicast_tlv.c
zwdb.c
zwpgrmr/crc32.c
zwpgrmr/linux_serial_interface.c
zwpgrmr/zpg.c
zwpgrmr/zpgp.c
provisioning_list.c
pvs_parse_help.c
zw_appl_nvm.c
${BISON_pvsparser_OUTPUTS}
${FLEX_pvslexer_OUTPUTS}
${TS2_SRC} 
zgw_backup.c
ZIP_Router_logging.c
)

if(NOT ${DISABLE_DTLS})
    list (APPEND GW_SRC DTLS_server.c)
endif()


if( ANDROID )
    find_library(log-lib log )
    add_library(
        zipgateway-lib
        SHARED
        ${GW_SRC}
        jni-glue.c
        )
    target_compile_definitions(zipgateway-lib PRIVATE -DL3_ONLY_ -DDEBUG_ALLOW_NONSECURE -DDISABLE_DTLS )
    target_link_libraries(zipgateway-lib ZWaveAnalyzer ${S2_LIBRARIES} ${log-lib} contiki )
else()
    list (APPEND GW_SRC
        zwpgrmr/linux_usb_interface.c
        ZWFirmwareUpdate.c
        ZW_tcp_client.c
    )

    add_library(zipgateway-lib ${GW_SRC})
    target_include_directories(zipgateway-lib PUBLIC 
      ${LibUSB_INCLUDE_DIRS}
      ${CMAKE_CURRENT_BINARY_DIR} 
      ${TS2_INCLUDE_DIRS}
      ${ZWAVE_INCLUDE_DIR}
      serialapi
      ${CMAKE_SOURCE_DIR}/src-gen
      zwpgrmr
      transport
      utls
      ${CMAKE_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/src
      ..
      )
    target_link_libraries(zipgateway-lib PUBLIC ZWaveAnalyzer contiki  ${S2_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto ${LibUSB_LIBRARIES} 
    sqlite3)

    if(${DISABLE_DTLS})
        message("Gateway if configured without DTLS support.")
        target_compile_definitions(zipgateway-lib PUBLIC -DDEBUG_ALLOW_NONSECURE -DDISABLE_DTLS )
    endif()

    target_compile_options( zipgateway-lib PUBLIC -Wno-address-of-packed-member )

    #file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/null.c "")
    #add_executable( zipgateway ${CMAKE_CURRENT_BINARY_DIR}/null.c )
    
    # For some reason the linker will remove some of the handlers if they are 
    # inside the zipgateway-lib. For this reason we will put the files in the 
    # executable target as well. This is not pretty but what to do? 
    add_executable( zipgateway ${HANDLER_SRC} )
    target_link_libraries( zipgateway contiki-main zipgateway-lib)
    install(TARGETS zipgateway RUNTIME DESTINATION sbin )

endif()
