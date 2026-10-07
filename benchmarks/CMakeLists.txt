#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is under no
# obligation to maintain or support the Software. RTI shall not be liable for any
# incidental or consequential damages arising out of the use or inability to use
# the software.
#

if(NOT PGW_ALLOCATION_INTERPOSITION_SUPPORTED)
    message(STATUS "Skipping allocation-interposing benchmarks on this toolchain")
    return()
endif()

if(NOT TARGET pgw_test_support)
    add_library(pgw_test_support STATIC ../tests/support/fake.c)
    target_include_directories(pgw_test_support PUBLIC ../tests/support)
    target_link_libraries(pgw_test_support PUBLIC PGW::core)
endif()
if(NOT TARGET pgw_allocation_probe)
    add_library(pgw_allocation_probe STATIC ../tests/support/allocation.c)
    target_include_directories(pgw_allocation_probe PUBLIC ../tests/support)
    target_link_libraries(pgw_allocation_probe PUBLIC PGW::core)
    target_compile_definitions(pgw_allocation_probe PUBLIC
        PGW_ALLOCATION_INTERPOSITION_SUPPORTED=1)
    target_link_options(pgw_allocation_probe INTERFACE
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
        -Wl,--wrap=aligned_alloc -Wl,--wrap=posix_memalign
        -Wl,--wrap=OSAPI_Heap_allocate -Wl,--wrap=OSAPI_Heap_realloc
        -Wl,--wrap=OSAPI_Heap_allocate_buffer)
endif()
add_executable(pgw_core_benchmark core_benchmark.c)
target_link_libraries(pgw_core_benchmark PRIVATE pgw_test_support
    pgw_allocation_probe PGW::build_options)
if(PGW_BUILD_TESTS)
    add_test(NAME REQ_BENCHMARK_CORE_SMOKE COMMAND pgw_core_benchmark 1000 1)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME REQ_BENCHMARK_PERSISTENCE_COMPARISON
        COMMAND ${CMAKE_COMMAND} -E env PGW_BENCHMARK=$<TARGET_FILE:pgw_core_benchmark>
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/runner/test_run.py)
endif()
if(PGW_ENABLE_CAN AND TARGET PGW::adapter_can AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/can/CMakeLists.txt")
    add_subdirectory(can)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PGW_ENABLE_DDS AND
   TARGET pgw_example_dds_model AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dds/CMakeLists.txt")
    add_subdirectory(dds)
endif()
