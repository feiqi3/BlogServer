project(SimpleHttpTest)

set (projectname_server ${PROJECT_NAME}_Server)

add_executable(${projectname_server} HttpTest.cpp)
set_target_properties(${projectname_server} PROPERTIES FOLDER "Tests")


target_include_directories(${projectname_server} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${projectname_server} PRIVATE ${CMAKE_SOURCE_DIR}/3rd/FeiLib/include)

target_link_libraries(${projectname_server} PUBLIC FeiLib)

add_custom_command(TARGET ${projectname_server} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:FeiLib>
        $<TARGET_FILE_DIR:${projectname_server}
    COMMENT "Copying Dependency"
)