find_program(XSLTPROC xsltproc)

add_custom_command( OUTPUT command_class_validator.c 
    COMMAND
        ${XSLTPROC} ${CMAKE_CURRENT_SOURCE_DIR}/cvalidator.xsl ${CMAKE_CURRENT_SOURCE_DIR}/ZWave_custom_cmd_classes.xml > command_class_validator.c
    DEPENDS cvalidator.xsl ZWave_custom_cmd_classes.xml 
)

add_custom_command( OUTPUT get_list.c 
    COMMAND
    ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_get.py ${CMAKE_CURRENT_SOURCE_DIR}/ZWave_custom_cmd_classes.xml
    > get_list.c )

# Uncomment next line to enable DBG_PRINTF
# include_directories(../../contiki/core ../../contiki/platform/linux)

add_library( ZWaveAnalyzer command_class_validator.c get_list.c CommandAnalyzer.c )
target_include_directories( ZWaveAnalyzer PUBLIC . )
