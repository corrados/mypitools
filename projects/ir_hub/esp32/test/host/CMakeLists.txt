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

add_executable(test_state_machine
    test_state_machine.cpp
    ../../arduino/ir_hub/src/control/state_machine.cpp
    ../../arduino/ir_hub/src/control/state_maps.cpp
    ../../arduino/ir_hub/src/control/state_preview.cpp
)
target_include_directories(test_state_machine PRIVATE ../../arduino/ir_hub/src)
target_compile_options(test_state_machine PRIVATE -Wall -Wextra -Werror)

add_test(NAME state_machine COMMAND test_state_machine)

add_executable(test_ps3_remote
    test_ps3_remote.cpp
    ../../arduino/ir_hub/src/input/ps3_remote.cpp
)
target_include_directories(test_ps3_remote PRIVATE ../../arduino/ir_hub/src)
target_compile_options(test_ps3_remote PRIVATE -Wall -Wextra -Werror)

add_test(NAME ps3_remote COMMAND test_ps3_remote)

add_executable(test_ps3_protocol
    test_ps3_protocol.cpp
    ../../arduino/ir_hub/src/input/ps3_protocol.cpp
)
target_include_directories(test_ps3_protocol PRIVATE ../../arduino/ir_hub/src)
target_compile_options(test_ps3_protocol PRIVATE -Wall -Wextra -Werror)

add_test(NAME ps3_protocol COMMAND test_ps3_protocol)
