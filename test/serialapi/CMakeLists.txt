if( NOT APPLE )
  add_unity_test(NAME test_serialapi FILES test_serialapi.c ../zipgateway_main_stubs.c LIBRARIES zipgateway-lib util)
endif()
