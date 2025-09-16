cmake_minimum_required(VERSION 3.18)
project(qt_irc_server LANGUAGES CXX)

option(ENABLE_DEBUG_TIMINGS "Write time measurements to /tmp/server.timings to debug performance." TRUE)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(EXECUTABLE_FLAG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Gui Sql)

file(GLOB SOURCE_FILES
        "src/*.h"
        "src/*.cpp"
        "src/lib/*.h"
        "src/lib/*.cpp"
        "src/irc/*.h"
        "src/irc/*.cpp"
        "src/core/*.h"
        "src/core/*.cpp"
        "src/lib/bcrypt/*.h"
        "src/lib/bcrypt/*.cpp"
)



add_subdirectory(src/lib/logger_std)

add_executable(qt_irc_server
    src/main.cpp
    ${SOURCE_FILES}
)

if(ENABLE_DEBUG_TIMINGS)
    target_compile_definitions(qt_irc_server
        PUBLIC
        ENABLE_DEBUG_TIMINGS=1
    )
endif()

target_include_directories(qt_irc_server PRIVATE src src/lib)

target_link_libraries(qt_irc_server PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Gui
        Qt6::Sql
        logger_std
)
#target_compile_definitions(qt_irc_server PRIVATE QT_NO_KEYWORDS)
