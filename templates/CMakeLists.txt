cmake_minimum_required(VERSION 3.16)
project(ArchitectGenLibraries VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

include(GNUInstallDirs)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

set(ARCHITECTGEN_LIBRARY_DOMAINS
    Data_Persistence
)

foreach(domain IN LISTS ARCHITECTGEN_LIBRARY_DOMAINS)
    add_subdirectory(${domain})
endforeach()

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArchitectGenLibrariesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ArchitectGenLibraries"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT ArchitectGenTargets
    FILE ArchitectGenLibrariesTargets.cmake
    NAMESPACE ArchitectGen::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ArchitectGenLibraries"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ArchitectGenLibraries"
)
