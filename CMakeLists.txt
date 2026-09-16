cmake_minimum_required(VERSION 3.16)
project(ncp VERSION 0.8)

add_definitions(-DVERSION="${PROJECT_VERSION}")

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
