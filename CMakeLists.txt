cmake_minimum_required(VERSION 3.16)
project(tick-guard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif ()

# Header-only library target
add_library(event_system INTERFACE)
target_include_directories(event_system INTERFACE
   ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(event_system INTERFACE cxx_std_20)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(event_system INTERFACE -Wall -Wextra -Wpedantic)
endif ()

if (MSVC)
  target_compile_options(event_system INTERFACE /W4)
endif ()

# Thread support (needed by EventSupervisor)
find_package(Threads REQUIRED)
target_link_libraries(event_system INTERFACE Threads::Threads)

option(EVENT_SYSTEM_BUILD_TESTS "Build unit tests" ON)
option(EVENT_SYSTEM_BUILD_EXAMPLES "Build example demo executable" ON)

if (EVENT_SYSTEM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()

if (EVENT_SYSTEM_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif ()
