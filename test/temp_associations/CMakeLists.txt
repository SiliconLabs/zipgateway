if (NOT APPLE)
  add_unity_test(NAME test_temp_associations FILES test_temp_associations.c ../zipgateway_main_stubs.c LIBRARIES zipgateway-lib)
  set_target_properties(test_temp_associations PROPERTIES LINK_FLAGS "-Wl,-wrap=rd_datastore_unpersist_virtual_nodes -Wl,-wrap=ctimer_set -Wl,-wrap=ZW_SendData_UDP -Wl,-wrap=udp_send_wrap -Wl,-wrap=ip_assoc_unpersist_association_table -Wl,-wrap=SerialAPI_ApplicationSlaveNodeInformation -Wl,-wrap=sec2_get_my_node_flags -Wl,-wrap=ZW_GetVirtualNodes -Wl,-wrap=ZW_GetControllerCapabilities -Wl,-wrap=ZW_AddNodeToNetwork -Wl,-wrap=ZW_SendDataAppl -Wl,-wrap=tcpip_ipv6_output -Wl,-wrap=get_queue_state")
endif()
