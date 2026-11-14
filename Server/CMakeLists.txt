cmake_minimum_required(VERSION 3.10)
project(Server)

option(BlogGenTools "Gen tools for server" ON)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add source files
file(GLOB_RECURSE SOURCES "src/*.cpp"  "src/*.h")

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Third-party: miniz (gzip inflate) - single .c implementation TU
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rd/miniz/miniz.c)

# Use clang-cl toolset for Server target (VS generator only)
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_target_properties(${PROJECT_NAME} PROPERTIES VS_PLATFORM_TOOLSET ClangCL)
endif()

# Keep Server.exe in its own output dir (debug/release); runtime DLLs are
# staged in Bin/<Config> and copied over by the POST_BUILD below.
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/release"
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rd/stb ${CMAKE_CURRENT_SOURCE_DIR}/3rd/miniz)

#3rd
target_link_libraries(${PROJECT_NAME} PRIVATE FeiLib)
# OpenSSL for ImageDimReader HTTPS client (TLS_client_method)
find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
if(WIN32)
    # winsock2 direct use (WSAGetLastError in ImageDimReader error paths)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()
# std::thread pool
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
#sqlite3
add_subdirectory(3rd/sqlite3)
target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3)
set_target_properties(sqlite3 PROPERTIES FOLDER "Server3rd")

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

message(STATUS "Compile options:")
get_target_property(COMPILE_OPTIONS ${PROJECT_NAME} COMPILE_OPTIONS)
if(COMPILE_OPTIONS)
    foreach(OPTION ${COMPILE_OPTIONS})
        message(STATUS "  ${OPTION}")
    endforeach()
else()
    message(STATUS "  No compile options set")
endif()

#tools
if(BlogGenTools)
	add_subdirectory(Tools)
endif()

## Server Related
target_compile_definitions(${PROJECT_NAME} PRIVATE SERVER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/")

## code filter 
function(collect_sources DIR GROUP_NAME)
    set(SOURCES )
    file(GLOB_RECURSE FILES "${DIR}/*")
    foreach(FILE ${FILES})
        if(IS_DIRECTORY ${FILE})
            continue()
        endif()
        list(APPEND ${SOURCES} ${FILE})
    endforeach()
    source_group(${GROUP_NAME} FILES ${FILES} )
endfunction()

collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/Controller Controller SRC_SOURCE)
collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/DAO DAO SRC_SOURCE)
collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/MODEL Model SRC_SOURCE)
collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/Service Service SRC_SOURCE)
collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/Utils Utils SRC_SOURCE)
collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/Server Server SRC_SOURCE)

# Copy all runtime deps from FeiLib's Bin/<Config> (FeiLib + TBB + nghttp2)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Bin/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/Bin/$<CONFIG>"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying runtime deps from Bin/$<CONFIG>"
)
# Copy sqlite3 (Server's own 3rd-party, NOT under FeiLib)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:sqlite3>"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying sqlite3 runtime"
)

