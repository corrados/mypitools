cmake_minimum_required(VERSION 3.16)

project(ir_hub_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(test_state_preview
    test_state_preview.cpp
    ../../main/control/state_preview.cpp
)
target_include_directories(test_state_preview PRIVATE ../../main)
target_compile_options(test_state_preview PRIVATE -Wall -Wextra -Werror)

add_test(NAME state_preview COMMAND test_state_preview)
