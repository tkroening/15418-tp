project(gpu)
add_library(gpu SHARED gpu.cpp sm.cpp)
target_include_directories(gpu PRIVATE ../common)
