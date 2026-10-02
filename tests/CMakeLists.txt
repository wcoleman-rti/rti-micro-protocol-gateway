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

find_package(Threads REQUIRED)
add_library(pgw_test_support STATIC support/fake.c)
target_include_directories(pgw_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(pgw_test_support PUBLIC PGW::core)
add_library(pgw_allocation_probe STATIC support/allocation.c)
target_include_directories(pgw_allocation_probe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(pgw_allocation_probe PUBLIC PGW::core)
target_link_options(pgw_allocation_probe INTERFACE
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
    -Wl,--wrap=aligned_alloc -Wl,--wrap=posix_memalign
    -Wl,--wrap=OSAPI_Heap_allocate -Wl,--wrap=OSAPI_Heap_realloc
    -Wl,--wrap=OSAPI_Heap_allocate_buffer)
add_executable(pgw_core_test core_test.c)
target_link_libraries(pgw_core_test PRIVATE pgw_test_support pgw_allocation_probe Threads::Threads)
target_compile_options(pgw_core_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
add_test(NAME REQ_CORE_ROUTING_LOANS_ALLOCATION_DIAGNOSTICS COMMAND pgw_core_test)
add_executable(pgw_sink_test sink_test.c)
target_link_libraries(pgw_sink_test PRIVATE PGW::diagnostics_local pgw_allocation_probe)
target_compile_options(pgw_sink_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
add_test(NAME REQ_DIAG_LOCAL_SINK_ALLOCATION_BACKPRESSURE COMMAND pgw_sink_test)
foreach(component codegen can config dds)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${component}/CMakeLists.txt")
        if(component STREQUAL "codegen" AND NOT TARGET PGW::binding_signal)
            continue()
        endif()
        if(component STREQUAL "can" AND NOT PGW_ENABLE_CAN)
            continue()
        endif()
        if(component STREQUAL "dds" AND NOT PGW_ENABLE_DDS)
            continue()
        endif()
        add_subdirectory(${component})
    endif()
endforeach()
