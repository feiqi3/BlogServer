cmake_minimum_required(VERSION 3.10)
project(Server)
# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add source files
file(GLOB_RECURSE SOURCES "src/*.cpp"  "src/*.h")

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

#3rd
target_link_libraries(${PROJECT_NAME} PRIVATE FeiLib)
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

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:FeiLib>
        $<TARGET_FILE_DIR:${PROJECT_NAME} 
    COMMENT "Copying Dependency"
)