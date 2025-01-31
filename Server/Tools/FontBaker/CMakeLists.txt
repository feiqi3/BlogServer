cmake_minimum_required(VERSION 3.10)

# 项目名称
project(FontBaker)

add_subdirectory(3rd/uchardet)
set_property(TARGET libuchardet PROPERTY FOLDER "Tools3rd")

find_package(Iconv)

if(NOT Iconv_FOUND)
	if(WIN32)
    		add_subdirectory(3rd/iconvWin)
		set_property(TARGET iconv PROPERTY FOLDER "Tools3rd")
	else()
		message(FATAL_ERROR "libiconv not found!")
	endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 查找当前目录下的所有 .cpp 文件
file(GLOB SOURCES "*.cpp")

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME}  private libuchardet )
target_link_libraries(FontBaker public iconv)
set_property(TARGET ${PROJECT_NAME}  PROPERTY FOLDER "Tools")
