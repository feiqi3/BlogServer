cmake_minimum_required(VERSION 3.10)
project(BlogServer)

set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel.")
set(BUILD_TEST ON CACHE BOOL "Build test")
# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_compile_definitions(FEI_DEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES "Release")
    add_compile_definitions(FEI_RELEASE)
else()
    message(FATAL_ERROR "Invalid build type. Use Debug or Release.")
endif(CMAKE_BUILD_TYPE MATCHES "Debug")


# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/release)


add_subdirectory(FeiLib)
add_subdirectory(Client)
add_subdirectory(Server)
if(BUILD_TEST)
    add_subdirectory(Tests)
endif()
