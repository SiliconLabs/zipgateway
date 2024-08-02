find_package(Doxygen)

if(DOXYGEN_FOUND)
    configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(main.dox.in ${CMAKE_CURRENT_BINARY_DIR}/main.dox)
    file(COPY logo.png DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    configure_file(style.css.in ${CMAKE_CURRENT_BINARY_DIR}/style.css)

    add_custom_target(libzipgateway_doc
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    add_custom_target(libzipgateway_doc_zip
                      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                      COMMAND zip -r libzwaveip-${VERSION_SHORT}-docs.zip html
                      DEPENDS libzipgateway_doc)
else()
    message("Skipping documentation until doxygen is installed")
endif()
