if( NOT APPLE )
  add_unity_test(NAME test_print_frame FILES test_print_frame.c ../../src/ZIP_Router_logging.c LIBRARIES zipgateway-lib)
endif()
