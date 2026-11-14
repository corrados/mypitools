cmake_minimum_required(VERSION 3.16)

project(ir_hub_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(test_state_preview
    test_state_preview.cpp
    ../../arduino/ir_hub/src/control/state_preview.cpp
)
target_include_directories(test_state_preview PRIVATE ../../arduino/ir_hub/src)
target_compile_options(test_state_preview PRIVATE -Wall -Wextra -Werror)

add_test(NAME state_preview COMMAND test_state_preview)

add_executable(test_ir_waveform
    test_ir_waveform.cpp
    ../../arduino/ir_hub/src/ir/ir_codes.cpp
    ../../arduino/ir_hub/src/ir/ir_waveform.cpp
)
target_include_directories(test_ir_waveform PRIVATE ../../arduino/ir_hub/src)
target_compile_options(test_ir_waveform PRIVATE -Wall -Wextra -Werror)

add_test(NAME ir_waveform COMMAND test_ir_waveform)
