cmake_minimum_required (VERSION 2.6)
project (libsyspower)

add_library(syspower lib/core.c)
target_include_directories(syspower PUBLIC include)

add_executable(syspowernap tools/syspowernap.c)
target_include_directories(syspowernap PRIVATE include)
target_link_libraries(syspowernap PRIVATE syspower)

add_executable(syspowerwakesrc tools/syspowerwakesrc.c)
target_include_directories(syspowerwakesrc PRIVATE include)
target_link_libraries(syspowerwakesrc PRIVATE syspower)