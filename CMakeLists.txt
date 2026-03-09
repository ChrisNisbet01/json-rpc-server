cmake_minimum_required(VERSION 3.10)
project(rpc_server C CXX)

option(WITH_UNIT_TESTS OFF)

find_package(PkgConfig REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so") # Prioritize static libraries

include(FetchContent)

pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c)

add_subdirectory(/home/chris/projects/easy_pc easy_pc_build EXCLUDE_FROM_ALL)

add_subdirectory(src)

if(WITH_UNIT_TESTS)
  message(STATUS "Unit tests will be included in the build")
  # CppUTest
  set(CPPUTEST_USE_GCOV OFF)
  set(CPPUTEST_USE_MEM_LEAK_DETECTION OFF)
  set(CPPUTEST_USE_VALGRIND OFF)
  set(CPPUTEST_USE_VLD OFF)
  set(CPPUTEST_USE_SANITIZERS OFF)
  set(CPPUTEST_USE_COVERAGE OFF)
  set(CPPUTEST_USE_PCH OFF)
  set(CPPUTEST_USE_MODULES OFF)
  set(CPPUTEST_USE_TDD_MACROS OFF)
  set(CPPUTEST_USE_REAL_GCC_THR OFF)
  set(CPPUTEST_USE_REAL_MINGW_THR OFF)
  set(CPPUTEST_USE_REAL_PTHREAD OFF)
  set(CPPUTEST_USE_REAL_STD_THR OFF)
  set(CPPUTEST_USE_REAL_WIN_THR OFF)
  set(CPPUTEST_USE_EXTENSIONS ON)

  include(FetchContent)
  FetchContent_Declare(
    cpputest
    GIT_REPOSITORY https://github.com/cpputest/cpputest.git
    GIT_TAG        v4.0
  )
  FetchContent_MakeAvailable(cpputest)
  add_subdirectory(tests)
endif()
