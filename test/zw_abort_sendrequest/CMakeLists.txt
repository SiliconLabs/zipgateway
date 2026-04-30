add_definitions(-DUNIT_TEST)

add_unity_test(NAME test_zw_abort_sendrequest
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_zw_abort_sendrequest.c
    ${CMAKE_SOURCE_DIR}/src/transport/ZW_SendRequest.c
    ${CMAKE_SOURCE_DIR}/contiki/core/lib/list.c
    ${CMAKE_SOURCE_DIR}/contiki/core/lib/memb.c
)
