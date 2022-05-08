cmake_minimum_required (VERSION 3.1)
project (Bomberman)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER "/usr/bin/g++-11")
set(CMAKE_CXX_FLAGS "-std=c++20 -g -Wall -Wextra")


# http://stackoverflow.com/questions/10555706/
macro (add_executable _name)
    # invoke built-in add_executable
    _add_executable(${ARGV})
    if (TARGET ${_name})
        target_link_libraries(${_name} ${Boost_LIBRARIES})
        target_link_libraries(${_name} pthread)
    endif()
endmacro()


FIND_PACKAGE( Boost COMPONENTS program_options log log_setup )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

add_executable(robots-client src/robots-client.cpp src/client.h src/messages.h src/common.h)
#add_executable(Bomberman src/robots-client.cpp)


# tests
#add_executable(test1 ../test/test1.cpp )

install(TARGETS DESTINATION .)