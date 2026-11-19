cmake_minimum_required(VERSION 3.16)
project(peripheral_contract_kit LANGUAGES CXX)

add_library(periph_contract INTERFACE)
target_include_directories(periph_contract INTERFACE include)
target_compile_features(periph_contract INTERFACE cxx_std_11)

add_executable(periph-contract-tests tests/test_scripted_hal.cpp)
target_link_libraries(periph-contract-tests PRIVATE periph_contract)
add_executable(periph-generated-tests tests/test_generated_script.cpp)
target_include_directories(periph-generated-tests PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(periph-generated-tests PRIVATE periph_contract)
set_target_properties(periph-contract-tests periph-generated-tests PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(periph-contract-tests periph-generated-tests PRIVATE /W4 /WX)
else()
    target_compile_options(periph-contract-tests periph-generated-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME periph-contract-tests COMMAND periph-contract-tests)
add_test(NAME periph-generated-tests COMMAND periph-generated-tests)
