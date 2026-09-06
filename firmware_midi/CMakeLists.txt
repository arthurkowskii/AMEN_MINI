cmake_minimum_required(VERSION 3.16)
project(amen_midi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(amen_midi_tests tests/test_main.cpp)
target_include_directories(amen_midi_tests PRIVATE teensy/amen_midi)
if(MSVC)
  target_compile_options(amen_midi_tests PRIVATE /W4 /WX)
else()
  target_compile_options(amen_midi_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME amen_midi_tests COMMAND amen_midi_tests)
