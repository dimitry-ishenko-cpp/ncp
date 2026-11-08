cmake_minimum_required(VERSION 3.16)
project(ncp VERSION 0.11)

add_definitions(-DVERSION="${PROJECT_VERSION}")

add_subdirectory(src)

option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
