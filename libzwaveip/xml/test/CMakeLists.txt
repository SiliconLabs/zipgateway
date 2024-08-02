find_package(LibXml2)

get_filename_component(LIBXML2_LIBRARY_DIR ${LIBXML2_LIBRARIES} DIRECTORY)
link_directories(${LIBXML2_LIBRARY_DIR})

add_executable(test_parse_xml
  test_parse_xml.c
 ../../libzwaveip/libzw_log.c
)
target_link_libraries( test_parse_xml parse_xml xml2 )
target_include_directories(test_parse_xml
  PUBLIC
  ${LIBXML2_INCLUDE_DIR}
  ..
)

configure_file( ../../config/ZWave_custom_cmd_classes.xml ${CMAKE_CURRENT_BINARY_DIR})
add_executable( test_cmd_tool test_cmd_tool.c  )
target_link_libraries (test_cmd_tool zw_cmd_tool )
