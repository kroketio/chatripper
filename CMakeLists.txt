cmake_minimum_required(VERSION 3.18)
project(snakepit LANGUAGES CXX)

option(ENABLE_DEBUG_TIMINGS "Write time measurements to /tmp/server.timings to debug performance." TRUE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG ON)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(EXECUTABLE_FLAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Gui Sql HttpServer Concurrent)
message(STATUS "Qt6 version: ${Qt6Core_VERSION}")

find_package(Python3 3.13 REQUIRED COMPONENTS Interpreter Development)
message(STATUS "Python3 version: ${Python3_VERSION}")
message(STATUS "Python3 include dir: ${Python3_INCLUDE_DIRS}")
message(STATUS "Python3 library: ${Python3_LIBRARIES}")

file(GLOB SOURCE_FILES
        "src/*.h"
        "src/*.cpp"
        "src/lib/*.h"
        "src/lib/*.cpp"
        "src/irc/*.h"
        "src/irc/*.cpp"
        "src/core/*.h"
        "src/core/*.cpp"
        "src/python/*.h"
        "src/python/*.cpp"
        "src/web/*.h"
        "src/web/*.cpp"
        "src/web/routes/*.h"
        "src/web/routes/*.cpp"
        "src/lib/bcrypt/*.h"
        "src/lib/bcrypt/*.cpp"
        "src/assets.qrc"
)

add_subdirectory(src/lib/logger_std)

add_executable(snakepit
    src/main.cpp
    ${SOURCE_FILES}
)

if(ENABLE_DEBUG_TIMINGS)
    target_compile_definitions(snakepit
        PUBLIC
        ENABLE_DEBUG_TIMINGS=1
    )
endif()

if(DEBUG)
    message(STATUS "DEBUG BUILD")
    target_compile_definitions(snakepit PRIVATE DEBUG=1)
endif()

target_include_directories(snakepit PRIVATE src src/lib)

target_link_libraries(snakepit PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Gui
        Qt6::Sql
        Qt6::HttpServer
        Qt6::Concurrent
        Python3::Python
        logger_std
)
