cmake_minimum_required(VERSION 3.10)
project(SPSock LANGUAGES CXX)

# BASE
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# COMPLILE
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  

# OUTPUT
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)

# DEBUG/RELEASE
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
else()
    add_compile_options(-O2)
endif()

# .a/.so
option(BUILD_STATIC "Build as static library" OFF)

# SRC
set(SOURCES
    SPBuffer.cpp
    SPController.cpp
    SPDeferred.cpp
    SPSock.cpp
)

# DEST
if(BUILD_STATIC)
    add_library(${PROJECT_NAME} STATIC ${SOURCES})
else()
    add_library(${PROJECT_NAME} SHARED ${SOURCES})
endif()

# TEST
option(BUILD_TEST "Build test samples" OFF)

if(BUILD_TEST)
    # TCP
    add_executable(example_tcp example/example_tcp.cpp)
    target_link_libraries(example_tcp ${PROJECT_NAME})
    
    # UDP
    add_executable(example_udp example/example_udp.cpp)
    target_link_libraries(example_udp ${PROJECT_NAME})

    if(UNIX AND NOT BUILD_STATIC)
        set_target_properties(example_tcp example_udp
            PROPERTIES
            LINK_FLAGS "-Wl,-rpath,${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        )
    endif()
endif()