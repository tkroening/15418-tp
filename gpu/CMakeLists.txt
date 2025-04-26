project(gpu)
add_library(gpu SHARED cfg.cpp scheduler.cpp gpu.cpp sm.cpp)
target_include_directories(gpu PRIVATE ../common)
