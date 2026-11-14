cmake_minimum_required(VERSION 3.16)
project(FeiLibEventLoopRegression LANGUAGES CXX)
enable_testing()

get_filename_component(FEILIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../FeiLib" ABSOLUTE)
find_package(Threads REQUIRED)
add_executable(EventLoopRegression
    regression.cpp
    "${FEILIB_DIR}/src/FEventLoop.cpp"
    "${FEILIB_DIR}/src/FTimeQueue.cpp"
    "${FEILIB_DIR}/src/FEvent.cpp"
    "${FEILIB_DIR}/src/FListener.cpp")
target_include_directories(EventLoopRegression PRIVATE "${FEILIB_DIR}/include")
target_compile_features(EventLoopRegression PRIVATE cxx_std_20)
target_compile_definitions(EventLoopRegression PRIVATE _F_EXPORT=1)
target_link_libraries(EventLoopRegression PRIVATE Threads::Threads)

add_test(NAME EventLoop.TimerInitialization COMMAND EventLoopRegression timer)
add_test(NAME EventLoop.ThreadLocalOwnership COMMAND EventLoopRegression loop)
add_test(NAME EventLoop.CrossThreadTasks COMMAND EventLoopRegression tasks)
set_tests_properties(EventLoop.TimerInitialization EventLoop.ThreadLocalOwnership
    EventLoop.CrossThreadTasks PROPERTIES TIMEOUT 10)
