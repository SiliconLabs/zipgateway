


add_unity_test(NAME test_multicast_auto FILES 
	test_multicast_auto.c 
	${CMAKE_SOURCE_DIR}/src/transport/S2_multicast_auto.c
	${CMAKE_SOURCE_DIR}/src/utls/zgw_nodemask.c
	${CMAKE_SOURCE_DIR}/test/test_gw_helpers.c
	
	)

