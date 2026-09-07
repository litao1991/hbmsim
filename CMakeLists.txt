cmake_minimum_required(VERSION 3.20)

project(hbmsim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hbmsim_core
  src/hbm_system.cpp
  src/controller/command_planner.cpp
  src/controller/hbm_controller.cpp
  src/controller/refresh_manager.cpp
  src/controller/row_policy.cpp
  src/controller/scheduler.cpp
  src/dram/hbm_standard.cpp
  src/kernel/event_queue.cpp
  src/mapping/address_mapper.cpp
  src/timing/timing_engine.cpp
  src/timing/timing_spec.cpp
)
target_include_directories(hbmsim_core PUBLIC include)
target_compile_options(hbmsim_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hbmsim src/main.cpp)
target_link_libraries(hbmsim PRIVATE hbmsim_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_event_queue tests/test_event_queue.cpp)
  target_link_libraries(test_event_queue PRIVATE hbmsim_core)
  add_test(NAME event_queue COMMAND test_event_queue)

  add_executable(test_hbm_system tests/test_hbm_system.cpp)
  target_link_libraries(test_hbm_system PRIVATE hbmsim_core)
  add_test(NAME hbm_system COMMAND test_hbm_system)

  add_executable(test_timing_engine tests/test_timing_engine.cpp)
  target_link_libraries(test_timing_engine PRIVATE hbmsim_core)
  add_test(NAME timing_engine COMMAND test_timing_engine)

  add_executable(test_scheduler tests/test_scheduler.cpp)
  target_link_libraries(test_scheduler PRIVATE hbmsim_core)
  add_test(NAME scheduler COMMAND test_scheduler)
endif()
