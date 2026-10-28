cmake_minimum_required(VERSION 3.26)
project(bodensee_sar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SAR_BUILD_PYTHON "Build the Python extension" ON)
option(SAR_BUILD_TESTS "Build the C++ tests (Catch2)" OFF)
option(SAR_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(sarcore STATIC
  cpp/src/lake.cpp
  cpp/src/env.cpp
  cpp/src/planners.cpp
  cpp/src/mavlink.cpp
  cpp/src/dis.cpp)
target_include_directories(sarcore PUBLIC cpp/include)
if(MSVC)
  target_compile_options(sarcore PRIVATE /W4)
else()
  target_compile_options(sarcore PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
if(SAR_SANITIZE)
  target_compile_options(sarcore PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(sarcore PUBLIC -fsanitize=address,undefined)
endif()

if(SAR_BUILD_PYTHON)
  find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
  execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
                  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
  find_package(nanobind CONFIG REQUIRED)
  nanobind_add_module(_sarsim NB_STATIC cpp/bindings/module.cpp)
  target_link_libraries(_sarsim PRIVATE sarcore)
  install(TARGETS _sarsim LIBRARY DESTINATION sar)
endif()

if(SAR_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.8.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  add_executable(sar_tests
    cpp/tests/test_geo.cpp
    cpp/tests/test_env.cpp
    cpp/tests/test_planners.cpp
    cpp/tests/test_protocols.cpp)
  target_link_libraries(sar_tests PRIVATE sarcore Catch2::Catch2WithMain)
  add_executable(sar_bench cpp/tests/bench.cpp)
  target_link_libraries(sar_bench PRIVATE sarcore)
  enable_testing()
  add_test(NAME sar_tests COMMAND sar_tests)
endif()
