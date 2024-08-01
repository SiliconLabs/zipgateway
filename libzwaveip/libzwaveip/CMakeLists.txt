if(AVAHI_LIBRARY-CLIENT)
    set(MDNS_SOURCES "avahi-mdns.c")
elseif(APPLE)
    set(MDNS_SOURCES "dnssd-mdns.c")
endif()

add_library(zwaveip ${MDNS_SOURCES} libzwaveip.c zconnection.c network_management.c node_provisioning_list.c zresource.c libzw_log.c ssldbg_utils.c)

# Enable next line to get compile errors if using functions deprecated in OpenSSL 1.1.0
#target_compile_definitions(zwaveip PRIVATE OPENSSL_API_COMPAT=0x10100000L)

target_link_libraries(zwaveip ${OPENSSL_LIBRARIES} pthread)
