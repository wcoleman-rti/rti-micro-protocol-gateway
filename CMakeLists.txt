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

cmake_minimum_required(VERSION 3.24)
project(protocol_gateway VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

option(PGW_BUILD_TESTS "Build requirement verification tests" ON)
option(PGW_BUILD_EXAMPLES "Build runnable examples" ON)
option(PGW_BUILD_BENCHMARKS "Build benchmark workloads" OFF)
option(PGW_ENABLE_CAN "Build the CAN adapter" ON)
option(PGW_ENABLE_DDS "Build the Connext Micro DDS adapter" ON)
option(PGW_ENABLE_REMOTE_CONTROL "Build the opt-in DDS remote-control interface" OFF)
option(PGW_ENABLE_ROUTE_LATENCY_METRICS
    "Compile allocation-free per-route batch latency metrics" OFF)
set(PGW_REMOTE_CONTROL_MAX_CONTROLLERS 1 CACHE STRING
    "Maximum DDS controller peers admitted by the control participant")
option(PGW_ENABLE_RUNNER "Build core's optional initialization-created scheduler runner" ON)
option(PGW_WARNINGS_AS_ERRORS "Treat gateway compiler warnings as errors" OFF)
set(PGW_PYTHON_EXECUTABLE "" CACHE FILEPATH
    "Explicit host Python interpreter for all generators/tests (e.g. .venv/bin/python)")
if(PGW_ENABLE_REMOTE_CONTROL AND NOT PGW_ENABLE_DDS)
    message(FATAL_ERROR "PGW_ENABLE_REMOTE_CONTROL requires PGW_ENABLE_DDS")
endif()
if(PGW_ENABLE_REMOTE_CONTROL AND
   (NOT PGW_REMOTE_CONTROL_MAX_CONTROLLERS MATCHES "^[1-9][0-9]*$" OR
    PGW_REMOTE_CONTROL_MAX_CONTROLLERS GREATER 32))
    message(FATAL_ERROR "PGW_REMOTE_CONTROL_MAX_CONTROLLERS must be in 1..32")
endif()
if(PGW_PYTHON_EXECUTABLE)
    get_filename_component(PGW_PYTHON_EXECUTABLE "${PGW_PYTHON_EXECUTABLE}" ABSOLUTE
        BASE_DIR "${PROJECT_SOURCE_DIR}")
    set(Python3_EXECUTABLE "${PGW_PYTHON_EXECUTABLE}" CACHE FILEPATH
        "Python interpreter selected by PGW_PYTHON_EXECUTABLE" FORCE)
endif()

include(PGWMicro)
include(PGWCodegen)
include(PGWProvenance)
pgw_generate_provenance()
add_library(pgw_build_options INTERFACE)
add_library(PGW::build_options ALIAS pgw_build_options)
target_compile_options(pgw_build_options INTERFACE
    "$<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
    "$<$<AND:$<BOOL:${PGW_WARNINGS_AS_ERRORS}>,$<C_COMPILER_ID:GNU,Clang>>:-Werror>")

if(PGW_BUILD_TESTS)
    enable_testing()
    add_test(NAME build.rti_launcher COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/test_rti_launcher.py"
        "${PGW_GENERATOR_WRAPPER}" "${PGW_JREHOME}"
        "${PGW_GENERATOR_WARNINGS_AS_ERRORS}" "${PGW_GENERATOR_WARNING_ALLOW_REGEX}")
endif()

function(pgw_add_component path)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${path}/CMakeLists.txt")
        add_subdirectory("${path}")
    endif()
endfunction()

function(pgw_link_adapter consumer adapter)
    if(NOT TARGET "${consumer}" OR NOT TARGET "${adapter}")
        message(FATAL_ERROR "pgw_link_adapter requires existing consumer and adapter targets")
    endif()
    get_target_property(_consumer_type "${consumer}" TYPE)
    if(_consumer_type STREQUAL "INTERFACE_LIBRARY")
        target_link_libraries("${consumer}" INTERFACE "${adapter}")
    else()
        target_link_libraries("${consumer}" PUBLIC "${adapter}")
    endif()
    get_target_property(_manifest "${adapter}" PGW_ADAPTER_MANIFEST)
    if(NOT _manifest OR NOT EXISTS "${_manifest}")
        if(PGW_ENABLE_REMOTE_CONTROL)
            message(FATAL_ERROR "Adapter ${adapter} has no installed control manifest")
        endif()
        return()
    endif()
    set_property(TARGET "${consumer}" APPEND PROPERTY PGW_LINKED_ADAPTER_MANIFESTS
        "${_manifest}")
endfunction()

function(pgw_generate_adapter_control_idl adapter_target)
    if(NOT PGW_ENABLE_REMOTE_CONTROL)
        return()
    endif()
    get_target_property(_manifest "${adapter_target}" PGW_ADAPTER_MANIFEST)
    if(NOT _manifest OR NOT EXISTS "${_manifest}")
        message(FATAL_ERROR "Cannot generate control IDL without adapter metadata: ${adapter_target}")
    endif()
    set(_generated "${CMAKE_CURRENT_BINARY_DIR}/${adapter_target}-control-idl")
    file(MAKE_DIRECTORY "${_generated}")
    set(_source_idl "${_generated}/control_adapter_source.idl")
    set(_flat_idl "${_generated}/control_adapter.idl")
    set(_metadata_header "${_generated}/control_manifest.h")
    add_custom_command(OUTPUT "${_flat_idl}" "${_metadata_header}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/adapter_codegen.py"
            --manifest "${_manifest}"
            --common-idl "${PROJECT_SOURCE_DIR}/core/control/idl/control_common.idl"
            --output "${_source_idl}" --header "${_metadata_header}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py"
            "${_flat_idl}" "${_source_idl}"
            --include-dir "${PROJECT_SOURCE_DIR}/core/control/idl"
        DEPENDS "${_manifest}" "${PROJECT_SOURCE_DIR}/core/control/idl/control_common.idl"
            "${PROJECT_SOURCE_DIR}/resources/schema/adapter-library.xsd"
            "${PROJECT_SOURCE_DIR}/tools/config_codegen/adapter_codegen.py"
            "${PROJECT_SOURCE_DIR}/tools/config_codegen/config_codegen.py"
            "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py"
        BYPRODUCTS "${_source_idl}" VERBATIM)
    string(REPLACE "::" "_" _target_name "${adapter_target}")
    pgw_micro_codegen(IDL_FILE "${_flat_idl}" OUTPUT_DIRECTORY "${_generated}"
        VAR ${_target_name}_CONTROL DEPENDS "${_flat_idl}")
    set(_codegen_target "${_target_name}_control_idl_codegen")
    add_custom_target("${_codegen_target}" DEPENDS ${${_target_name}_CONTROL_C_SOURCES})
    add_dependencies("${adapter_target}" "${_codegen_target}")
    set_property(TARGET "${adapter_target}" PROPERTY PGW_ADAPTER_CONTROL_IDL "${_flat_idl}")
    set_property(TARGET "${adapter_target}" PROPERTY PGW_ADAPTER_CONTROL_METADATA_HEADER
        "${_metadata_header}")
    target_include_directories("${adapter_target}" PRIVATE "${_generated}")
endfunction()

pgw_add_component(core)
if(PGW_ENABLE_REMOTE_CONTROL)
    target_compile_definitions(pgw_core PUBLIC PGW_ENABLE_REMOTE_CONTROL=1)
endif()
if(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    target_compile_definitions(pgw_core PUBLIC PGW_ENABLE_ROUTE_LATENCY_METRICS=1)
endif()
pgw_add_component(tools)
if(PGW_ENABLE_CAN)
    pgw_add_component(adapters/can)
endif()
if(PGW_ENABLE_DDS)
    pgw_add_component(adapters/dds/connext_micro)
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/bindings/CMakeLists.txt")
    pgw_add_component(bindings)
else()
    pgw_add_component(bindings/signal)
endif()
if(PGW_BUILD_EXAMPLES)
    pgw_add_component(examples)
endif()
if(PGW_BUILD_TESTS)
    pgw_add_component(tests)
    if(PGW_ENABLE_RUNNER AND TARGET pgw_runtime_test AND TARGET pgw_allocation_probe)
        target_link_libraries(pgw_runtime_test PRIVATE pgw_allocation_probe)
        target_compile_definitions(pgw_runtime_test PRIVATE PGW_TEST_ALLOCATION_PROBE=1)
    endif()
endif()
if(PGW_BUILD_BENCHMARKS)
    pgw_add_component(benchmarks)
endif()
include(PGWInstall)
