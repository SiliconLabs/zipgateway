project(zwlogtest C )

# Test of the log system is largely about differences controlled at
# compile time, so to test, we build several different configurations
# of the logging code and the helper C functions.

# The parameters controlled at compile time are:
# 1 - initial enabled level
# 2 - max level available
# 3 - file or stdout output
# 4 - file size limit
# 5 - colorization

# 1 and 2 can all be set to any level (except initial cannot be higher
# than max), but we only compile a sample of the combinations.

# TODO: So far, we mainly build and run.  There is almost no validation in these cases yet.

############################################################
# Prerequisites
set(CONTIKI ${CMAKE_SOURCE_DIR}/contiki)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CONTIKI}/core/lib
  ${CONTIKI}/platform/linux/
  )


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -ggdb")


############################################################
# Libraries

# Log, at different compile levels
set(LOG_LEVELS "0;4;10")
#set(LOG_LEVELS "0;1;2;3;4;5;6;7;8;9;10")

set(ZWLOG_FILE __/__/contiki/platform/linux/zgw_log_int.o )

# One of these for each level
foreach(LVL ${LOG_LEVELS})
  add_library( zgw_log${LVL} ${CONTIKI}/platform/linux/./zgw_log_int.c)
  target_compile_definitions(zgw_log${LVL} PRIVATE ZGW_LOG ZGW_LOG_LVL_INIT=${LVL} )
  target_compile_definitions(zgw_log${LVL} INTERFACE ZGW_LOG )
  target_compile_definitions(zgw_log${LVL} INTERFACE ZGW_LOG_LVL_INIT=${LVL})
  set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_log${LVL}.dir/${ZWLOG_FILE})
  #FILE(GLOB log_cov_objects${LVL} ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log*.c)
endforeach()

# One of these for each level
foreach(LVL ${LOG_LEVELS})
  add_library( zgw_log_max${LVL} ${CONTIKI}/platform/linux/./zgw_log_int.c)
  target_compile_definitions(zgw_log_max${LVL} PRIVATE ZGW_LOG ZGW_LOG_LVL_INIT=0 ZGW_LOG_LVL_MAX=${LVL})
  target_compile_definitions(zgw_log_max${LVL} INTERFACE ZGW_LOG )
  target_compile_definitions(zgw_log_max${LVL} INTERFACE ZGW_LOG_LVL_INIT=0  ZGW_LOG_LVL_MAX=${LVL})
  set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_log_max${LVL}.dir/${ZWLOG_FILE})
endforeach()
#
## With colors
set(COLOR_DEF_LVL 7)
add_library(zgw_log_color${COLOR_DEF_LVL} ${CONTIKI}/platform/linux/./zgw_log_int.c)
target_compile_definitions(zgw_log_color${COLOR_DEF_LVL} PRIVATE ZGW_LOG ZGW_LOG_LVL_INIT=${COLOR_DEF_LVL} ZGW_LOG_COLOR)
target_compile_definitions(zgw_log_color${COLOR_DEF_LVL} INTERFACE ZGW_LOG ZGW_LOG_COLOR)
target_compile_definitions(zgw_log_color${COLOR_DEF_LVL} INTERFACE ZGW_LOG_LVL_INIT=${COLOR_DEF_LVL})
target_compile_definitions(zgw_log_color${COLOR_DEF_LVL} INTERFACE ZGW_LOG_LOG_TO_FILE)
set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_log_color${COLOR_DEF_LVL}.dir/${ZWLOG_FILE})

## With file
set(FILE_DEF_LVL 4)
add_library(zgw_log_file_def${FILE_DEF_LVL} ${CONTIKI}/platform/linux/./zgw_log_int.c)
target_compile_definitions(zgw_log_file_def${FILE_DEF_LVL} PRIVATE ZGW_LOG ZGW_LOG_LVL_INIT=${FILE_DEF_LVL} ZGW_LOG_LOG_TO_FILE)
target_compile_definitions(zgw_log_file_def${FILE_DEF_LVL} INTERFACE ZGW_LOG )
target_compile_definitions(zgw_log_file_def${FILE_DEF_LVL} INTERFACE ZGW_LOG_LVL_INIT=${FILE_DEF_LVL})
target_compile_definitions(zgw_log_file_def${FILE_DEF_LVL} INTERFACE ZGW_LOG_LOG_TO_FILE)
set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_log_file_def${FILE_DEF_LVL}.dir/${ZWLOG_FILE})

# With file that wraps
add_library(zgw_log_file_maxd ${CONTIKI}/platform/linux/./zgw_log_int.c)
target_compile_definitions(zgw_log_file_maxd PRIVATE ZGW_LOG ZGW_LOG_LOG_TO_FILE ZGW_LOG_LVL_INIT=4)
target_compile_definitions(zgw_log_file_maxd PRIVATE ZGW_LOG_MAX_SZ=4096UL)
target_compile_definitions(zgw_log_file_maxd INTERFACE ZGW_LOG )
target_compile_definitions(zgw_log_file_maxd INTERFACE ZGW_LOG_LVL_INIT=4)
target_compile_definitions(zgw_log_file_maxd INTERFACE ZGW_LOG_LOG_TO_FILE ZGW_LOG_MAX_SZ=4096UL)
set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_log_file_maxd.dir/${ZWLOG_FILE})

# With defined file name
add_library(zgw_log_file_named${FILE_DEF_LVL} ${CONTIKI}/platform/linux/./zgw_log_int.c)
target_compile_definitions(zgw_log_file_named${FILE_DEF_LVL} PRIVATE
  ZGW_LOG ZGW_LOG_LOG_TO_FILE ZGW_LOG_LVL_INIT=${FILE_DEF_LVL} ZGW_LOG_FILENAME="foo.log"
  )
target_compile_definitions(zgw_log_file_named${FILE_DEF_LVL} INTERFACE
  ZGW_LOG ZGW_LOG_LOG_TO_FILE ZGW_LOG_LVL_INIT=${FILE_DEF_LVL} ZGW_LOG_FILENAME="foo.log"
  )
set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_log_file_named${FILE_DEF_LVL}.dir/${ZWLOG_FILE})

## With defined file name and file that wraps
add_library(zgw_log_file_named_maxd ${CONTIKI}/platform/linux/./zgw_log_int.c)
target_compile_definitions(zgw_log_file_named_maxd PRIVATE
  ZGW_LOG ZGW_LOG_LOG_TO_FILE ZGW_LOG_LVL_INIT=4
  ZGW_LOG_FILENAME="this-is-my-private-log-file-for-a-very-specific-test-case.log"
  ZGW_LOG_MAX_SZ=8000UL
  )
target_compile_definitions(zgw_log_file_named_maxd INTERFACE
  ZGW_LOG ZGW_LOG_FILENAME="this-is-my-private-log-file-for-a-very-specific-test-case.log"
  ZGW_LOG_LVL_INIT=4 ZGW_LOG_LOG_TO_FILE ZGW_LOG_MAX_SZ=8000UL
  )
set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_log_file_named_maxd.dir/${ZWLOG_FILE})

# No log
add_library( zgw_logNone ${CONTIKI}/platform/linux/./zgw_log_int.c)
target_compile_definitions(zgw_logNone INTERFACE )
target_compile_definitions(zgw_logNone PRIVATE ZGW_LOG_LVL_DEFAULT=7)
set(ZWLOG_GCOV_OBJECTS ${ZWLOG_GCOV_OBJECTS} ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/zgw_logNone.dir/${ZWLOG_FILE})


############################################################
# Test executables
############################################################

set(test_c_files
  test_log_main.c test_log_second.c test_log_file_name_trim_in_log.c)

############################################################
# Test builds, batch 1, initial level.  One for each available level.
foreach(LVL ${LOG_LEVELS})
  add_executable( zgw_log_test_init${LVL} ${test_c_files}
    #    feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS}
    )
  # target compile definitions of ZGW_LOG=${LVL} and
  # ZGW_LOG_LVL_INIT=${LVL} are inherited from the library's
  # INTERFACE
  target_link_libraries(zgw_log_test_init${LVL} zgw_log${LVL})
  add_test(zgw_log_test_init${LVL} zgw_log_test_init${LVL})
endforeach()

############################################################
# Test builds, batch 2, Max level.  One of these for each available level
foreach(LVL ${LOG_LEVELS})
  add_executable( zgw_log_test_max${LVL}  ${test_c_files}
#    feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS} 
    )
  # target compile definitions of ZGW_LOG=${LVL} and
  # ZGW_LOG_LVL_INIT=${LVL} are inherited from the library's
  # INTERFACE
  target_link_libraries(zgw_log_test_max${LVL} zgw_log_max${LVL})
  add_test(zgw_log_test_max${LVL} zgw_log_test_max${LVL})
endforeach()

############################################################
# Test build, batch 3, Colorization.

# The initial level we use when testing colorization
set(COLOR_LVL 7)

add_executable( zgw_log_test_color${COLOR_LVL}  ${test_c_files} 
#  feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS} 
  )
# target compile definitions are inherited from the library's
# INTERFACE
target_link_libraries(zgw_log_test_color${COLOR_LVL} zgw_log_color${COLOR_LVL})
add_test(zgw_log_test_color${COLOR_LVL} zgw_log_test_color${COLOR_LVL})


############################################################
# Test builds, batch 4, file output.

# The initial level we use when testing file output
set(FILE_DEF_LVL 4)

# File output with default values.
add_executable( zgw_log_test_file_def${FILE_DEF_LVL}  ${test_c_files} 
#  feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS} 
  )
target_link_libraries(zgw_log_test_file_def${FILE_DEF_LVL} zgw_log_file_def${FILE_DEF_LVL})
add_test(zgw_log_test_file_def${FILE_DEF_LVL} zgw_log_test_file_def${FILE_DEF_LVL})


# File output with max file size.
add_executable( zgw_log_test_file_maxd  ${test_c_files} 
#  feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS} 
  )
target_link_libraries(zgw_log_test_file_maxd zgw_log_file_maxd)
add_test(zgw_log_test_file_maxd zgw_log_test_file_maxd -u 3)


# File output with named log file.
add_executable(zgw_log_test_file_named${FILE_DEF_LVL}  ${test_c_files}
#  feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS}
  )
target_link_libraries(zgw_log_test_file_named${FILE_DEF_LVL}
  zgw_log_file_named${FILE_DEF_LVL})
add_test(zgw_log_test_file_named${FILE_DEF_LVL} 
  zgw_log_test_file_named${FILE_DEF_LVL})


# File output with named log file and max file size.
add_executable( zgw_log_test_file_named_maxd  ${test_c_files} 
#  feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS} 
  )
target_link_libraries(zgw_log_test_file_named_maxd zgw_log_file_named_maxd)
add_test(zgw_log_test_file_named_maxd zgw_log_test_file_named_maxd -u 3)

# File output with named log file and max file size, overwritten by command line file name.
add_test(zgw_log_test_file_named_maxd_cmdline 
  zgw_log_test_file_named_maxd -u 3 -f "/tmp/anotherfile.log")


############################################################
# Test build, batch 5, no log (compile time removal)
add_executable( zgw_log_testNone ${test_c_files}
  #  feeder_env.c ${BISON_testparser_OUTPUTS} ${FLEX_testlexer_OUTPUTS} 
  )
target_compile_definitions(zgw_log_testNone PRIVATE )
target_link_libraries(zgw_log_testNone zgw_logNone)
add_test(zgw_log_testNone zgw_log_testNone)

set(GCOV_OBJECTS ${GCOV_OBJECTS}  ${ZWLOG_GCOV_OBJECTS} PARENT_SCOPE)

# Finally gcov everything (with branch counting, too)
add_test(zwlog_coverage gcov -b   ${ZWLOG_GCOV_OBJECTS})
