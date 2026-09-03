set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(acl REQUIRED IMPORTED_TARGET libacl)
pkg_check_modules(asio REQUIRED IMPORTED_TARGET asio)
pkg_check_modules(cap REQUIRED IMPORTED_TARGET libcap)

include(GNUInstallDirs)

add_executable(ncp
    io/file.cpp

    file.cpp
    misc.cpp
    ncp.cpp
)
target_link_libraries(ncp PRIVATE PkgConfig::acl PkgConfig::asio PkgConfig::cap)

set(nmv ${CMAKE_CURRENT_BINARY_DIR}/nmv)
file(CREATE_LINK ncp ${nmv} SYMBOLIC)

install(TARGETS ncp DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${nmv} DESTINATION ${CMAKE_INSTALL_BINDIR})
