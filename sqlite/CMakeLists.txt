
add_library(sqlite3 sqlite3.c)
target_include_directories(sqlite3 PUBLIC .)
#target_compile_definitions(sqlite3 PRIVATE SQLITE_MAX_WORKER_THREADS=0)
target_link_libraries(sqlite3 PUBLIC dl pthread)