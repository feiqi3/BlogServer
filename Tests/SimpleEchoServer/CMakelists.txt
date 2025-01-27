project(SimpleEchoServer)

set (projectname_server ${PROJECT_NAME}_Server)
set (projectname_client ${PROJECT_NAME}_Client)

add_executable(${projectname_server} server.cpp)
set_target_properties(${projectname_server} PROPERTIES FOLDER "Tests")
add_executable(${projectname_client} client.cpp)
set_target_properties(${projectname_client} PROPERTIES FOLDER "Tests")


target_link_libraries(${projectname_server} PUBLIC FeiLib)

target_link_libraries(${projectname_client} PUBLIC FeiLib)

add_custom_command(
    TARGET ${projectname_client} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        $<TARGET_FILE_DIR:FeiLib>
        $<TARGET_FILE_DIR:${projectname_client}>
    COMMENT "Copying Dependency"
)

add_custom_command(
    TARGET ${projectname_server} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        $<TARGET_FILE_DIR:FeiLib>
        $<TARGET_FILE_DIR:${projectname_server}>
    COMMENT "Copying Dependency"
)