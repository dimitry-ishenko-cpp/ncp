set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(asio REQUIRED IMPORTED_TARGET asio)

include(GNUInstallDirs)

add_executable(ncp
    io_file.cpp
    ncp.cpp
    report.cpp
    walk.cpp
)
target_link_libraries(ncp PRIVATE PkgConfig::asio)

install(TARGETS ncp DESTINATION ${CMAKE_INSTALL_BINDIR})
