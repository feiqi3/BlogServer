cmake_minimum_required(VERSION 3.10)
project(sqlite3)

set(SQLITE_SRC sqlite3.c)

if(WIN32)
set(SQLITE_API_DECL "SQLITE_API=__declspec(dllexport)")
endif()

add_library(sqlite3 SHARED ${SQLITE_SRC})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU") # GCC
    target_compile_options(sqlite3 PRIVATE     
        -fPIC  
        -march=native       
        -mtune=native       
        -fno-omit-frame-pointer 
        -fstack-protector-strong 
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang") # Clang
    target_compile_options(sqlite3 PRIVATE
        -fPIC  
        -march=native
        -mtune=native
        -fno-omit-frame-pointer
        -fstack-protector-strong
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC") # Microsoft Visual C++
    target_compile_options(sqlite3 PRIVATE
          /arch:AVX2
	  /fp：fast
    )
	
endif()



target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5        
    SQLITE_ENABLE_JSON1       
    SQLITE_THREADSAFE=2       
    SQLITE_OMIT_LOAD_EXTENSION 
    SQLITE_LIKE_DOESNT_MATCH_BLOBS 
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_AUTOINIT
    ${SQLITE_API_DECL}
)