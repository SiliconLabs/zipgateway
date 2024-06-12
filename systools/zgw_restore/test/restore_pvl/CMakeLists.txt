#set (RESTORE_API  )

add_executable(test_restore_pvl test_restore_pvl.c ${COMMON_TEST_SRC})
target_link_libraries(test_restore_pvl zgw_restore-lib)
add_test(restore_pvl test_restore_pvl)
