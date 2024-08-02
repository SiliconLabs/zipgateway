add_executable(test_tokquote tokquote.c test_tokquote.c ../../../libzwaveip/libzw_log.c)
target_include_directories(test_tokquote
  PUBLIC
  ../../../include
)
