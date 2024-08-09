add_subdirectory( tokquote )

# place binaries in top-level build folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(LibXml2)
get_filename_component(LIBXML2_LIBRARY_DIR ${LIBXML2_LIBRARIES} DIRECTORY)
link_directories(${LIBXML2_LIBRARY_DIR})

add_executable(reference_client reference_client.c tokenizer.c
    hexchar.c command_completion.c util.c tokquote/tokquote.c)
add_executable(reference_listener reference_listener.c util.c)

# Enable next line to get compile errors if using functions deprecated in OpenSSL 1.1.0
#target_compile_definitions(reference_listener PRIVATE OPENSSL_API_COMPAT=0x10100000L)

target_include_directories(reference_client
  PUBLIC
  listener
  ../..
  ../../libedit
  ${LIBXML2_INCLUDE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/../..
)

target_include_directories(reference_listener
  PUBLIC
  listener
  ../..
  ${CMAKE_CURRENT_BINARY_DIR}/../..
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" OR ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
target_link_libraries(reference_client zwaveip edit parse_xml xml2 zw_cmd_tool -ltermcap ${MDNS_LIBS} ${OPENSSL_LIBRARIES})
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
target_link_libraries(reference_client zwaveip edit parse_xml xml2 zw_cmd_tool -lbsd -ltermcap ${MDNS_LIBS} ${OPENSSL_LIBRARIES})
endif ()
target_link_libraries(reference_listener zwaveip parse_xml xml2 ${OPENSSL_LIBRARIES})

install(TARGETS
  reference_client
  reference_listener
  RUNTIME DESTINATION bin
)
