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

if(NOT TARGET pgw_example_dds_model OR NOT TARGET pgw_allocation_probe)
    message(FATAL_ERROR "DDS benchmark requires the actual example MAG model and allocation probe")
endif()
file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/build-dds-domain-locks")
add_executable(pgw_dds_benchmark dds_benchmark.c)
target_link_libraries(pgw_dds_benchmark PRIVATE pgw_example_dds_model
    PGW::can_memory PGW::core pgw_allocation_probe PGW::build_options)
target_link_options(pgw_dds_benchmark PRIVATE -Wl,--wrap=PGW_Arena_allocate)
target_compile_definitions(pgw_dds_benchmark PRIVATE
    PGW_BENCHMARK_DDS_DOMAIN=${PGW_EXAMPLE_DDS_DOMAIN}
    PGW_BENCHMARK_DOMAIN_LOCK="${PROJECT_SOURCE_DIR}/build-dds-domain-locks/domain-${PGW_EXAMPLE_DDS_DOMAIN}.lock")
target_compile_options(pgw_dds_benchmark PRIVATE
    "$<$<C_COMPILER_ID:GNU,Clang>:-UNDEBUG>")
if(PGW_BUILD_TESTS)
    add_test(NAME REQ_BENCHMARK_DDS_SMOKE COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_benchmark.py" $<TARGET_FILE:pgw_dds_benchmark> 1000 1 1)
    add_test(NAME REQ_BENCHMARK_DDS_COUNTER_ONLY COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_benchmark.py" $<TARGET_FILE:pgw_dds_benchmark> 15 0)
    set_tests_properties(REQ_BENCHMARK_DDS_SMOKE REQ_BENCHMARK_DDS_COUNTER_ONLY
        PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE SKIP_RETURN_CODE 77
        LABELS "DDS;benchmark;private-domain")
endif()
