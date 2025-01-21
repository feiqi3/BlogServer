# Set minimum required CMake version
cmake_minimum_required(VERSION 3.10)

# Set project name and version
project(FeiLib VERSION 1.0  LANGUAGES CXX)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add source files
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h")

# Create library
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

#3rd
add_subdirectory(3rd/spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rd/reflect)

add_subdirectory(3rd/json)
target_link_libraries(${PROJECT_NAME} PUBLIC nlohmann_json::nlohmann_json)

set(INJA_USE_EMBEDDED_JSON Off)
set(INJA_BUILD_TESTS OFF)
add_subdirectory(3rd/inja)
target_link_libraries(${PROJECT_NAME} PUBLIC inja)

##work around for re2, the absl installed on system with wrong version may cause a link error in mingw
##Indeed we dont really use it
set(ABSL_ENABLE_INSTALL ON)
add_subdirectory(3rd/absl)

add_subdirectory(3rd/re2)
target_link_libraries(${PROJECT_NAME} PUBLIC re2::re2)

set(TBB_TEST OFF)
set(TBBMALLOC_BUILD  OFF)
#for TBB
set(BUILD_SHARED_LIBS ON)
add_subdirectory(3rd/oneTBB)
target_link_libraries(${PROJECT_NAME} PUBLIC TBB::tbb)

add_custom_target(genexdebug COMMAND ${CMAKE_COMMAND} -E echo " $<TARGET_FILE:TBB::tbb>")

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:TBB::tbb>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying TBB $<TARGET_FILE:TBB::tbb> to $<TARGET_FILE_DIR:${PROJECT_NAME}>"
)


if(WIN32)
    add_subdirectory(3rd/wepoll)
    target_link_libraries(${PROJECT_NAME} PRIVATE wepoll)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rd/wepoll/include )
endif()

# Link platform-specific socket libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
    # stupid msvc
    target_compile_definitions(${PROJECT_NAME} PRIVATE
    _CRT_SECURE_NO_WARNINGS
)
endif()

# Set build-specific output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Bin/Release"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Lib/Debug"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Lib/Release"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Lib/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Lib/Release"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    _F_EXPORT=1
)

# Set compile options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# Set optimization flags for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /O2)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -O3)
    endif()
endif()
# Set debug flags for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /Od /Zi)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -O0 -g)
    endif()
endif()

if(UNIX)
    target_compile_options(${PROJECT_NAME} PRIVATE -fPIC)
    target_compile_options(${PROJECT_NAME} PRIVATE -fvisibility=hidden)
endif()


# Set different binary suffixes for Debug and Release builds
set_target_properties(${PROJECT_NAME} PROPERTIES 
    DEBUG_POSTFIX "D"
    RELEASE_POSTFIX ""
)

# Copy library files to 3rd party directory
# Copy files after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD

# Create necessary directories if they don't exist
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/bin
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/bin/Debug
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/bin/Release
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/lib/Debug
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/lib/Release
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/include/${PROJECT_NAME}

    # Copy DLL files
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${PROJECT_NAME}>
        ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/bin/$<CONFIG>/

    # Copy LIB files
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_LINKER_FILE:${PROJECT_NAME}>
        ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/lib/$<CONFIG>/

    # Copy header files
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/3rd/${PROJECT_NAME}/include/${PROJECT_NAME}
    COMMAND echo Copy FeiLib libs,headers and dlls.
)

