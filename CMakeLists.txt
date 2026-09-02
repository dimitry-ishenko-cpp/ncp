cmake_minimum_required(VERSION 3.16)
project(ncp VERSION 0.5.1)

add_definitions(-DVERSION="${PROJECT_VERSION}")

add_subdirectory(src)
