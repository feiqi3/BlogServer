cmake_minimum_required(VERSION 3.10)
project(Server)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# tests 
add_subdirectory(SimpleEchoServer)
add_subdirectory(HttpTest)
add_subdirectory(HttpServerTest)
add_subdirectory(HttpPathMatcherTest)