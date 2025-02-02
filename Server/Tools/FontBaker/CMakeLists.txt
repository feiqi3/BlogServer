cmake_minimum_required(VERSION 3.10)

# 项目名称
project(FontBaker)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 查找当前目录下的所有 .cpp 文件
file(GLOB SOURCES "*.cpp")

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} PUBLIC nlohmann_json::nlohmann_json)
set_property(TARGET ${PROJECT_NAME}  PROPERTY FOLDER "Tools")
