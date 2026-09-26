cmake_minimum_required(VERSION 3.21)

project(xds VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(XDS_BUILD_TOOLS "Build the xds command-line tool" ON)
option(XDS_BUILD_TESTS "Build the unit tests" ON)
option(XDS_BUILD_QT_PLUGIN "Build the Qt image format plugin used by Gwenview and other Qt applications" OFF)

include(GNUInstallDirs)

find_package(ZLIB REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_library(xds
    include/xds/xds.h
    src/dds.cpp
    src/decode.cpp
    src/format.cpp
    src/internal.h
    src/png.cpp
    src/texture.cpp
)
add_library(xds::xds ALIAS xds)
target_include_directories(xds
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src
)
target_link_libraries(xds PRIVATE ZLIB::ZLIB)
set_target_properties(xds PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS xds EXPORT xdsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/xds/xds.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xds)
install(EXPORT xdsTargets NAMESPACE xds:: FILE xdsTargets.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xds)

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/xdsConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/xdsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xds
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/xdsConfigVersion.cmake COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/xdsConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/xdsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xds
)

if(XDS_BUILD_TOOLS)
    add_executable(xds-tool tools/xds.cpp)
    set_target_properties(xds-tool PROPERTIES OUTPUT_NAME xds)
    target_link_libraries(xds-tool PRIVATE xds::xds)
    install(TARGETS xds-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(XDS_BUILD_QT_PLUGIN)
    add_subdirectory(qt)
endif()

if(XDS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
