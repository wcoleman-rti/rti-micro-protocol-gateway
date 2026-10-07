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

add_library(pgw_adapter_can STATIC src/can.c)
add_library(PGW::adapter_can ALIAS pgw_adapter_can)
find_package(Threads REQUIRED)
set_property(TARGET pgw_adapter_can PROPERTY PGW_ADAPTER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter.xml")
pgw_generate_adapter_control_idl(pgw_adapter_can)
target_include_directories(pgw_adapter_can PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(pgw_adapter_can PUBLIC PGW::core PGW::binding_signal
    PRIVATE PGW::build_options Threads::Threads)

add_library(pgw_can_memory STATIC src/memory.c)
add_library(PGW::can_memory ALIAS pgw_can_memory)
target_link_libraries(pgw_can_memory PUBLIC PGW::adapter_can
    PRIVATE PGW::build_options)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(pgw_can_socketcan STATIC src/socketcan.c)
    add_library(PGW::can_socketcan ALIAS pgw_can_socketcan)
    target_link_libraries(pgw_can_socketcan PUBLIC PGW::adapter_can
        PRIVATE PGW::build_options)
endif()

if(PGW_BUILD_TESTS)
    if(NOT COMMAND pgw_generate_dbc)
        include("${PROJECT_SOURCE_DIR}/tools/dbc_codegen/CMakeLists.txt")
    endif()
    set(_can_codec_dir "${CMAKE_CURRENT_BINARY_DIR}/test-codec")
    pgw_generate_dbc(pgw_can_test_codec
        "${PROJECT_SOURCE_DIR}/examples/can_dds/example.dbc"
        "${PROJECT_SOURCE_DIR}/examples/can_dds/mapping.json"
        "${_can_codec_dir}")
    add_executable(pgw_can_test tests/test_can.c)
    target_link_libraries(pgw_can_test PRIVATE PGW::can_memory
        pgw_can_test_codec PGW::build_options)
    target_compile_options(pgw_can_test PRIVATE
        "$<$<C_COMPILER_ID:GNU,Clang>:-UNDEBUG>"
        "$<$<C_COMPILER_ID:MSVC>:/UNDEBUG>")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(pgw_can_test PRIVATE PGW_CAN_WRAP_ALLOCATIONS)
        target_link_options(pgw_can_test PRIVATE
            -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
            -Wl,--wrap=aligned_alloc)
    endif()
    add_test(NAME can.bounded_mapping COMMAND pgw_can_test)
    if(TARGET pgw_can_socketcan)
        add_executable(pgw_can_socket_test tests/test_socketcan.c)
        target_link_libraries(pgw_can_socket_test PRIVATE
            PGW::can_socketcan PGW::build_options)
        target_compile_options(pgw_can_socket_test PRIVATE
            "$<$<C_COMPILER_ID:GNU,Clang>:-UNDEBUG>")
        add_test(NAME can.socket_errors COMMAND pgw_can_socket_test)
    endif()
endif()
