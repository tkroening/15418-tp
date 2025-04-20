project(trace)

add_library(trace SHARED trace.cpp)
target_include_directories(trace PUBLIC ../common)

# add_subdirectory(taskLib)