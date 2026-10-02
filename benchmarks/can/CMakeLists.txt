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

if(NOT TARGET PGW::can_memory)
    message(FATAL_ERROR "The CAN benchmark requires PGW_ENABLE_CAN")
endif()
if(NOT TARGET pgw_allocation_probe)
    message(FATAL_ERROR "Add the shared allocation probe before benchmarks/can")
endif()
if(NOT COMMAND pgw_generate_dbc)
    include("${PROJECT_SOURCE_DIR}/tools/dbc_codegen/CMakeLists.txt")
endif()
pgw_generate_dbc(pgw_can_benchmark_codec
    "${PROJECT_SOURCE_DIR}/examples/can_dds/example.dbc"
    "${PROJECT_SOURCE_DIR}/examples/can_dds/mapping.json"
    "${CMAKE_CURRENT_BINARY_DIR}/codec")
add_executable(pgw_can_benchmark can_benchmark.c)
target_link_libraries(pgw_can_benchmark PRIVATE PGW::can_memory PGW::core
    pgw_can_benchmark_codec pgw_allocation_probe PGW::build_options)
target_compile_options(pgw_can_benchmark PRIVATE
    "$<$<C_COMPILER_ID:GNU,Clang>:-UNDEBUG>")
if(PGW_BUILD_TESTS)
    add_test(NAME REQ_BENCHMARK_CAN_SMOKE COMMAND pgw_can_benchmark 1000 1)
    add_test(NAME REQ_BENCHMARK_CAN_COUNTER_ONLY COMMAND pgw_can_benchmark 15 0)
    add_test(NAME REQ_BENCHMARK_CAN_METADATA_DISABLED COMMAND pgw_can_benchmark 1000 1 0)
endif()
