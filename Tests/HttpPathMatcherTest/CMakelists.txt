project(HttpPathMatcherTest)

add_executable(${PROJECT_NAME} test.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "Tests")

target_link_libraries(${PROJECT_NAME} PUBLIC FeiLib)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:FeiLib>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying Dependency"
)
