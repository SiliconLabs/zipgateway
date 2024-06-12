add_definitions(-Wno-undef)
add_definitions(-DPROVISIONING_LIST_SIZE=232 -DPROVISIONING_LIST_STORE_FILENAME_DEFAULT="./provisioning_list_store.dat")

include_directories(${CMAKE_SOURCE_DIR}/contiki/core/sys  ${CMAKE_SOURCE_DIR}/Z-Wave/include
${CMAKE_SOURCE_DIR}/contiki/cpu/native ${CMAKE_SOURCE_DIR}/src/transport  ${CMAKE_SOURCE_DIR}/test)


add_executable(test_CC_provisioning_list test_CC_provisioning_list.c 
  ${TEST_HELPERS}
  ${CMAKE_SOURCE_DIR}/test/test_CC_helpers.c
  ${CMAKE_SOURCE_DIR}/src/CC_provisioning_list.c
  ${CMAKE_SOURCE_DIR}/src/utls/zgw_str.c
)

find_package(BISON 3.0.4)
find_package(FLEX)

if (BISON_FOUND)
  include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    )
  BISON_TARGET(pvlparser ${CMAKE_SOURCE_DIR}/src/pvs_cfg.y ${CMAKE_CURRENT_BINARY_DIR}/pvs_cfg.tab.c VERBOSE)

  if (FLEX_FOUND)
    FLEX_TARGET(pvllexer ${CMAKE_SOURCE_DIR}/src/pvs_cfg.l ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.c )
    ADD_FLEX_BISON_DEPENDENCY(pvllexer pvlparser)

    set(PVL_SRC ${PVL_SRC} ${BISON_pvlparser_OUTPUTS} ${FLEX_pvllexer_OUTPUTS})

  endif(FLEX_FOUND)
endif(BISON_FOUND)

add_executable(test_CC_and_pvl test_CC_provisioning_list2.c 
  ${TEST_HELPERS}
  ${CMAKE_SOURCE_DIR}/test/test_CC_helpers.c
  ${CMAKE_SOURCE_DIR}/src/CC_provisioning_list.c 
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log_int.c
  ${CMAKE_SOURCE_DIR}/test/serialapi-stub.c
  ${CMAKE_SOURCE_DIR}/src/utls/zgw_str.c
  ${PVL_SRC}
  ${BISON_pvlparser_OUTPUTS} 
  ${FLEX_pvllexer_OUTPUTS}
  )
target_compile_definitions(test_CC_and_pvl PUBLIC
  ZGW_LOG ZGW_LOG_LOG_TO_FILE ZGW_LOG_LVL_INIT=5)

set_target_properties(test_CC_and_pvl PROPERTIES LINKER_LANGUAGE C)

add_test(CC_provisioning_list test_CC_provisioning_list)
add_test(CC_and_pvl test_CC_and_pvl)
