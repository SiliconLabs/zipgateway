find_package(PythonInterp)

if(${PYTHONINTERP_FOUND})
    message(STATUS "Found Python: " ${PYTHON_EXECUTABLE} " (found version \"" ${PYTHON_VERSION_STRING} "\"" )

    set(JSON_SCHEMA_TO_DOX_ARGS ${JSON_SCHEMA_TO_DOX_ARGS} --pu --html_page migration.html)
    # zgw_json.schema.json
    set(JSON_SCHEMA_INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/zgw_json.schema.json)
    execute_process(COMMAND
                    ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/json_schema_to_dox.py
                    ${JSON_SCHEMA_TO_DOX_ARGS}
                    ${JSON_SCHEMA_INPUT_FILE}
                    STDOUT
                    RESULT_VARIABLE JSON_SCHEMA_TO_DOX_RESULT
                    OUTPUT_VARIABLE JSON_SCHEMA_DOX_ZGW
                    ERROR_VARIABLE JSON_SCHEMA_TO_DOX_ERROR)

    if (JSON_SCHEMA_TO_DOX_RESULT MATCHES 0)
        message(STATUS "Successfully parsed " ${JSON_SCHEMA_INPUT_FILE})
    else()
        message(WARNING ${JSON_SCHEMA_TO_DOX_RESULT} ${JSON_SCHEMA_TO_DOX_ERROR})
        message(FATAL_ERROR "Failed to parse " ${JSON_SCHEMA_INPUT_FILE})
    endif()

    # zw_controller.schema.json
    set(JSON_SCHEMA_INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/zw_controller.schema.json)
    execute_process(COMMAND
                           ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/json_schema_to_dox.py
                           ${JSON_SCHEMA_TO_DOX_ARGS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/zw_controller.schema.json
                           STDOUT
                           RESULT_VARIABLE JSON_SCHEMA_TO_DOX_RESULT
                           OUTPUT_VARIABLE JSON_SCHEMA_DOX_ZW_CONTROLLER
                           ERROR_VARIABLE DUMMY)

    if (JSON_SCHEMA_TO_DOX_RESULT MATCHES 0)
        message(STATUS "Successfully parsed " ${JSON_SCHEMA_INPUT_FILE})
    else()
        message(WARNING ${JSON_SCHEMA_TO_DOX_RESULT} ${JSON_SCHEMA_TO_DOX_ERROR})
        message(FATAL_ERROR "Failed to parse " ${JSON_SCHEMA_INPUT_FILE})
    endif()

    configure_file(migration.dox.in ${CMAKE_CURRENT_BINARY_DIR}/migration.dox)
    # Dummy dependency to make cmake rerun if json schema files change
    configure_file(zw_controller.schema.json ${CMAKE_CURRENT_BINARY_DIR}/zw_controller.schema.json COPYONLY)
    configure_file(zgw_json.schema.json ${CMAKE_CURRENT_BINARY_DIR}/zgw_json.schema.json COPYONLY)
else()
    message(STATUS "Didn't find Python")
endif()
