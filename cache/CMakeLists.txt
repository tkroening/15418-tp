project(cache)
add_library(cache SHARED cache.cpp)
target_include_directories(cache PRIVATE ../common)
