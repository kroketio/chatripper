cmake_minimum_required(VERSION 3.18)
project(chatripper
    VERSION "0.0.1"
    DESCRIPTION "chatripper"
    LANGUAGES CXX C
)

option(ENABLE_DEBUG_TIMINGS "Write time measurements to /tmp/server.timings to debug performance." TRUE)

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG ON)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(EXECUTABLE_FLAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindCcache)

configure_file("cmake/config-chatripper.h.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/config-chatripper.h")

# https://github.com/kroketio/libminisign
find_package(minisign REQUIRED)

find_package(RapidJSON REQUIRED)
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

add_executable(chatripper
    ${SOURCE_FILES}
)

if(ENABLE_DEBUG_TIMINGS)
    target_compile_definitions(chatripper
        PUBLIC
        ENABLE_DEBUG_TIMINGS=1
    )
endif()

if(DEBUG)
    message(STATUS "DEBUG BUILD")
    target_compile_definitions(chatripper PRIVATE DEBUG=1)
endif()

target_include_directories(chatripper PRIVATE
    src
    src/lib
    ${RapidJSON_INCLUDE_DIRS})

target_link_libraries(chatripper PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Gui
    Qt6::Sql
    Qt6::HttpServer
    Qt6::Concurrent
    Python3::Python
    logger_std
    minisign::minisign
)

set_target_properties(chatripper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LINK_FLAGS_RELEASE -s
)
