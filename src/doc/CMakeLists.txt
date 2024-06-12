

find_package(Doxygen)

if(DOXYGEN_FOUND)
    # Find Plantuml, if PLANTUML_JAR_PATH environment variable is set, use that, 
    # else if /opt/plantuml.jar exists and use that,
    # else do not generate plantuml documentation
    if(NOT "$ENV{PLANTUML_JAR_PATH}" STREQUAL "")
        if(EXISTS "$ENV{PLANTUML_JAR_PATH}")
            set(PLANTUML_JAR_PATH $ENV{PLANTUML_JAR_PATH})
        else()
            message(WARNING "Plantuml set in PLANTUML_JAR_PATH env, file doesn't exist at $ENV{PLANTUML_JAR_PATH}, documentation will be incomplete")
        endif()
    elseif(EXISTS "/opt/plantuml.jar")
        set(PLANTUML_JAR_PATH "/opt/plantuml.jar")
    else()
        message(WARNING "PLANTUML_JAR_PATH environment variable not set, documentation will be incomplete")
    endif()
    if(DEFINED PLANTUML_JAR_PATH)
        message(STATUS "Found Plantuml JAR: " ${PLANTUML_JAR_PATH})
        # If plantuml is found, then Java is Required
        find_package(Java COMPONENTS Runtime REQUIRED)
    endif()

    configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(main.dox.in ${CMAKE_CURRENT_BINARY_DIR}/main.dox)

    add_custom_target(doc
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
                    COMMAND roffit < zipgateway.3 > ${CMAKE_CURRENT_BINARY_DIR}/html/zip.html
                    )
    add_custom_target(doc_zip
                      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                      COMMAND zip -r zipgateway-${VERSION_SHORT}-docs.zip html
                      DEPENDS doc)

    install(FILES zipgateway.3 DESTINATION man/man3 )
endif()
