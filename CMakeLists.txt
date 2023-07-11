cmake_minimum_required(VERSION 3.0)
project(jdic)

find_package(EXPAT REQUIRED)
find_package(SQLite3 REQUIRED)

set(JDIC_SOURCE
    ./src/jdic.c
    ./src/util.c
    ./src/jmdict.c
)
set(CMAKE_EXPORT_COMPILE_COMMANDS YES)

add_executable(jdic ${JDIC_SOURCE})

target_compile_options(jdic PUBLIC -g -Wall -Wconversion -Wfloat-equal -Wredundant-decls)
target_include_directories(jdic PUBLIC
    /usr/include
    /usr/local/include
    ${EXPAT_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(jdic PUBLIC
    ${EXPAT_LIBRARIES}
    ${SQLite3_LIBRARIES}
)
