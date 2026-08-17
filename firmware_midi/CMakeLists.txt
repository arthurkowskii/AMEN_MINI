cmake_minimum_required(VERSION 3.16)
project(amen_midi LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(amen_midi
  src/music/harmony.cpp src/midi/note_registry.cpp
  src/performance/engine.cpp src/profiles/instrument_profiles.cpp
  src/controls/control_surface.cpp src/algorithms/algorithms.cpp src/ui/text_model.cpp)
target_include_directories(amen_midi PUBLIC src)
target_compile_options(amen_midi PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(amen_midi_tests tests/test_main.cpp)
target_link_libraries(amen_midi_tests PRIVATE amen_midi)
target_compile_options(amen_midi_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(amen_midi_harness apps/console_harness.cpp)
target_link_libraries(amen_midi_harness PRIVATE amen_midi)
target_compile_options(amen_midi_harness PRIVATE -Wall -Wextra -Wpedantic -Werror)
enable_testing()
add_test(NAME amen_midi_tests COMMAND amen_midi_tests)
