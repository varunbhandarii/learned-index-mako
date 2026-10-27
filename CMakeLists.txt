cmake_minimum_required(VERSION 3.18)
project(learned_index_mako LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(lim_top_level ON)
else()
  set(lim_top_level OFF)  # built from inside Mako (see patches/mako.patch)
endif()
option(LIM_BUILD_TESTS "Build the unit tests" ${lim_top_level})
set(LIM_SANITIZE "" CACHE STRING "Sanitizer for a top-level build: address, thread, or empty")

if(lim_top_level AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(lim_top_level AND LIM_SANITIZE)
  add_compile_options(-fsanitize=${LIM_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${LIM_SANITIZE})
  if(LIM_SANITIZE STREQUAL "address")
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=undefined)
  endif()
endif()

# ALEX is used from the submodule with patches/alex.patch applied to a copy in the build
# tree, so the submodule itself stays clean.
set(lim_alex_src "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ALEX/src/core")
set(lim_alex_patch "${CMAKE_CURRENT_SOURCE_DIR}/patches/alex.patch")
set(lim_alex_patched "${CMAKE_CURRENT_BINARY_DIR}/alex-patched")
if(NOT EXISTS "${lim_alex_src}/alex.h")
  message(FATAL_ERROR "ALEX submodule missing; run: git submodule update --init third_party/ALEX")
endif()
find_program(LIM_PATCH_EXE patch REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${lim_alex_patch}")
file(REMOVE_RECURSE "${lim_alex_patched}")
file(COPY "${lim_alex_src}" DESTINATION "${lim_alex_patched}/src")
# A Windows checkout with core.autocrlf=true has CRLF endings, which the patch won't match.
file(GLOB lim_alex_headers "${lim_alex_patched}/src/core/*.h")
foreach(header IN LISTS lim_alex_headers)
  file(READ "${header}" content)
  string(REPLACE "\r\n" "\n" content "${content}")
  file(WRITE "${header}" "${content}")
endforeach()
execute_process(
  COMMAND "${LIM_PATCH_EXE}" -p1 --forward --batch -i "${lim_alex_patch}"
  WORKING_DIRECTORY "${lim_alex_patched}"
  RESULT_VARIABLE lim_patch_rc OUTPUT_VARIABLE lim_patch_out ERROR_VARIABLE lim_patch_out)
if(NOT lim_patch_rc EQUAL 0)
  message(FATAL_ERROR "Applying ${lim_alex_patch} failed:\n${lim_patch_out}")
endif()

find_package(Threads REQUIRED)

add_library(lim_alex STATIC src/alex_index.cc)
target_include_directories(lim_alex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(lim_alex SYSTEM PRIVATE "${lim_alex_patched}/src/core")
# ALEX uses std::allocator::rebind, which C++20 removed, so this target stays on C++17 even
# inside Mako's C++23 build. Its header exposes only plain types.
set_target_properties(lim_alex PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)
target_compile_options(lim_alex PRIVATE -mpopcnt -mbmi -mlzcnt -Wall -Wno-unused-variable)
if(NOT lim_top_level)
  # Mako sets its optimisation flags per target rather than through CMAKE_BUILD_TYPE, so
  # match them here; otherwise ALEX would be built unoptimised and the comparison unfair.
  target_compile_options(lim_alex PRIVATE -O2 -march=native)
endif()
target_link_libraries(lim_alex PUBLIC Threads::Threads)

if(LIM_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
      GIT_SHALLOW TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(alex_index_test tests/alex_index_test.cc)
  set_target_properties(alex_index_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(alex_index_test PRIVATE lim_alex GTest::gtest_main)
  add_test(NAME alex_index_test COMMAND alex_index_test)

  # alex_enabled_for() reads the environment once, so it gets its own process.
  add_executable(alex_config_test tests/alex_config_test.cc)
  set_target_properties(alex_config_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(alex_config_test PRIVATE lim_alex GTest::gtest_main)
  add_test(NAME alex_config_test COMMAND alex_config_test)
endif()
