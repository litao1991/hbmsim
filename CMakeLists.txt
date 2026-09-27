cmake_minimum_required(VERSION 3.20)

project(hbmsim VERSION 0.7.6 LANGUAGES CXX)

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
  src/resource/bandwidth_rate.cpp
  src/timing/timing_engine.cpp
  src/timing/timing_spec.cpp
)
target_include_directories(hbmsim_core PUBLIC include)
target_compile_options(hbmsim_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hbmsim src/main.cpp)
target_link_libraries(hbmsim PRIVATE hbmsim_core)

include(CTest)
option(HBMSIM_BUILD_TESTING "Build HBMSim's own tests" ON)
if(BUILD_TESTING AND HBMSIM_BUILD_TESTING)
  # Tests deliberately use assert: keep both checks and their expressions alive.
  add_compile_options(-UNDEBUG)
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

  add_executable(test_hbm_standard tests/test_hbm_standard.cpp)
  target_link_libraries(test_hbm_standard PRIVATE hbmsim_core)
  add_test(NAME hbm_standard COMMAND test_hbm_standard)
  add_executable(test_h7_boundary tests/test_h7_boundary.cpp)
  target_link_libraries(test_h7_boundary PRIVATE hbmsim_core)
  add_test(NAME h7_boundary COMMAND test_h7_boundary)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME cli_backpressure COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py $<TARGET_FILE:hbmsim>)
endif()
