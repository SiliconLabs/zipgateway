
if( NOT APPLE )
  add_unity_test(NAME test_ZIP_router FILES test_ZIP_router.c ../zipgateway_main_stubs.c LIBRARIES zipgateway-lib)
  set_target_properties(test_ZIP_router PROPERTIES LINK_FLAGS "-Wl,-wrap=rd_register_new_node -Wl,-wrap=rd_register_node_probe_notifier -Wl,-wrap=mb_wakeup_event -Wl,-wrap=highest_scheme -Wl,-wrap=GetCacheEntryFlag -Wl,-wrap=rd_get_node_probe_flags -Wl,-wrap=rd_get_node_state")

  add_unity_test(NAME test_s2_wrap FILES test_s2_wrap.c ../zipgateway_main_stubs.c LIBRARIES zipgateway-lib )
  set_target_properties(test_s2_wrap PROPERTIES LINK_FLAGS "-Wl,-wrap=dev_urandom -Wl,-wrap=zw_appl_nvm_read -Wl,-wrap=zw_appl_nvm_write")

  add_unity_test(NAME test_s2_keystore FILES test_s2_keystore.c ../zipgateway_main_stubs.c LIBRARIES zipgateway-lib )
  set_target_properties(test_s2_keystore PROPERTIES LINK_FLAGS "-Wl,-wrap=NetworkManagement_getState -Wl,-wrap=ZW_NVRGetValue -Wl,-wrap=zw_appl_nvm_read")

  add_unity_test(NAME test_CC_Multicmd FILES test_CC_Multicmd.c ../zipgateway_main_stubs.c ../../src/CC_Multicmd.c LIBRARIES zipgateway-lib)
  set_target_properties(test_CC_Multicmd PROPERTIES LINK_FLAGS "-Wl,-wrap=ApplicationIpCommandHandler")

endif()
