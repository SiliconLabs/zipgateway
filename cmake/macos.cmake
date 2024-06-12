# the name of the target operating system
set(CMAKE_SYSTEM_NAME Darwin)
SET(CMAKE_SYSTEM_VERSION 1)
# Target system Processor

# which compilers to use for C
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-typedef-redefinition -Wno-address-of-packed-member")