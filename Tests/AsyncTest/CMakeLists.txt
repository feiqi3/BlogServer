cmake_minimum_required(VERSION 3.16)

# May be built alone for isolated transport tests, or from the repository's Tests/.
if(NOT TARGET FeiLib)
    project(FeiLibAsyncValidation LANGUAGES C CXX)
    enable_testing()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    get_filename_component(FEILIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../FeiLib" ABSOLUTE)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
    file(GLOB ASYNC_IMPLEMENTATION CONFIGURE_DEPENDS "${FEILIB_ROOT}/src/Async/*.cpp")
    add_library(FeiLibAsyncUnderTest STATIC
        ${ASYNC_IMPLEMENTATION}
        "${FEILIB_ROOT}/src/Http/FHttpClient.cpp"
        "${FEILIB_ROOT}/src/Http/FHttpResponseParser.cpp"
        "${FEILIB_ROOT}/src/Http/FHttpRequestParser.cpp"
        "${FEILIB_ROOT}/src/Http/FhttpDef.cpp"
        "${FEILIB_ROOT}/src/Http/FCookie.cpp"
        "${FEILIB_ROOT}/src/FBuffer.cpp"
        "${FEILIB_ROOT}/src/FBufferReader.cpp"
        "${FEILIB_ROOT}/src/FEvent.cpp"
        "${FEILIB_ROOT}/src/FListener.cpp"
        "${FEILIB_ROOT}/src/FEventLoop.cpp"
        "${FEILIB_ROOT}/src/FTimeQueue.cpp"
        "${FEILIB_ROOT}/src/FEPollListener.cpp"
        "${FEILIB_ROOT}/src/FREventDef.cpp"
        "${FEILIB_ROOT}/src/FSocket.cpp"
        "${FEILIB_ROOT}/src/FLogger.cpp")
    target_include_directories(FeiLibAsyncUnderTest PUBLIC "${FEILIB_ROOT}/include" "${FEILIB_ROOT}/3rd/spdlog/include")
    target_compile_definitions(FeiLibAsyncUnderTest PUBLIC _F_EXPORT=1)
    target_link_libraries(FeiLibAsyncUnderTest PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    if(WIN32)
        target_sources(FeiLibAsyncUnderTest PRIVATE "${FEILIB_ROOT}/3rd/wepoll/wepoll.c")
        target_include_directories(FeiLibAsyncUnderTest PRIVATE "${FEILIB_ROOT}/3rd/wepoll/include")
        target_link_libraries(FeiLibAsyncUnderTest PUBLIC ws2_32)
    endif()
    set(ASYNC_LIBRARY FeiLibAsyncUnderTest)
else()
    set(ASYNC_LIBRARY FeiLib)
endif()

file(GLOB ASYNC_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
add_executable(FeiLibAsyncTests main.cpp ${ASYNC_TEST_SOURCES})
target_include_directories(FeiLibAsyncTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(FeiLibAsyncTests PRIVATE cxx_std_20)
target_link_libraries(FeiLibAsyncTests PRIVATE ${ASYNC_LIBRARY})
if(WIN32)
    # Standalone Windows tests need the matching OpenSSL DLLs next to the exe.
    # The full build may supply these through PATH; copy only when available.
    if(NOT OPENSSL_INCLUDE_DIR)
        find_package(OpenSSL REQUIRED)
    endif()
    file(GLOB ASYNC_OPENSSL_DLLS "${OPENSSL_INCLUDE_DIR}/../bin/*ssl*.dll" "${OPENSSL_INCLUDE_DIR}/../bin/*crypto*.dll")
    foreach(ASYNC_DLL IN LISTS ASYNC_OPENSSL_DLLS)
        add_custom_command(TARGET FeiLibAsyncTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASYNC_DLL}" "$<TARGET_FILE_DIR:FeiLibAsyncTests>")
    endforeach()
endif()
if(WIN32 AND ASYNC_LIBRARY STREQUAL "FeiLib")
    add_custom_command(TARGET FeiLibAsyncTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/Bin/$<CONFIG>" "$<TARGET_FILE_DIR:FeiLibAsyncTests>")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME FeiLib.AsyncInfrastructure COMMAND FeiLibAsyncTests infra)
add_test(NAME FeiLib.ResponseParser COMMAND FeiLibAsyncTests parser)
add_test(NAME FeiLib.JobGraph COMMAND FeiLibAsyncTests jobs)
add_test(NAME FeiLib.HttpAndWorkflow
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py" "$<TARGET_FILE:FeiLibAsyncTests>" --groups http integration)
set_tests_properties(FeiLib.AsyncInfrastructure FeiLib.ResponseParser FeiLib.JobGraph PROPERTIES TIMEOUT 45)
set_tests_properties(FeiLib.HttpAndWorkflow PROPERTIES TIMEOUT 180)
