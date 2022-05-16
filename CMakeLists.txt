cmake_minimum_required (VERSION 3.1)
project (Bomberman)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER "/usr/bin/g++-11")
set(CMAKE_CXX_FLAGS "-std=gnu++20 -g -Wall -Wextra -Wconversion")
#set(CMAKE_CXX_FLAGS "-std=gnu++20 -Wall -Wextra -Wconversion -Werror -O2")


# http://stackoverflow.com/questions/10555706/
macro (add_executable _name)
    # invoke built-in add_executable
    _add_executable(${ARGV})
    if (TARGET ${_name})
        target_link_libraries(${_name} ${Boost_LIBRARIES})
        target_link_libraries(${_name} pthread)
    endif()
endmacro()


FIND_PACKAGE( Boost REQUIRED program_options log log_setup coroutine  )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

add_executable(robots-client src/robots-client.cpp src/client.h src/messages.h src/errors.h src/net.h src/types.h)

# TEST
# add_executable(test-message-serialization test/test-message-serialization.cpp src/client.h src/messages.h src/common.h src/common.h src/messages.h src/client.h test/test-message-serialization.cpp)
# add_executable(test_timer test/test_timer.cpp src/client.h src/messages.h src/common.h src/common.h src/messages.h src/client.h test/test_timer.cpp)



install(TARGETS DESTINATION .)