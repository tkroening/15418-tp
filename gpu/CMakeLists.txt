project(gpu)
add_library(gpu SHARED gpu.cpp)
target_include_directories(gpu PRIVATE ../common)
