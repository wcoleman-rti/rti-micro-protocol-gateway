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

if(NOT PGW_ENABLE_DDS)
    return()
endif()
get_target_property(_can_manifest pgw_adapter_can PGW_ADAPTER_MANIFEST)
get_target_property(_dds_manifest pgw_adapter_dds_connext_micro PGW_ADAPTER_MANIFEST)
set(PGW_EXAMPLE_ADAPTER_MANIFESTS "${_can_manifest}" "${_dds_manifest}")
set(adapter_manifest_arguments)
foreach(adapter_manifest IN LISTS PGW_EXAMPLE_ADAPTER_MANIFESTS)
    list(APPEND adapter_manifest_arguments --adapter-manifest "${adapter_manifest}")
endforeach()
set(gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${gen}")
set(model_plugin_headers
    "#include \"signalsPlugin.h\"\n#include \"probePlugin.h\"\n#include \"diagnosticsPlugin.h\"\n")
if(PGW_ENABLE_REMOTE_CONTROL)
    string(APPEND model_plugin_headers "#include \"controllerPlugin.h\"\n")
endif()
file(CONFIGURE OUTPUT "${gen}/modelPlugin.h"
    CONTENT "${model_plugin_headers}")
set(PGW_EXAMPLE_DDS_DOMAIN 83 CACHE STRING "Private loopback demonstration DDS domain")
option(PGW_DDS_DIAGNOSTICS "Compile named fixed-size DDS route snapshot endpoints" ON)
option(PGW_EXAMPLE_CONTROL_TELEMETRY
    "Select CAN received-frame telemetry in the remote-control example" ON)
if(PGW_DDS_DIAGNOSTICS)
    set(diag 1)
else()
    set(diag 0)
endif()
file(GENERATE OUTPUT "${gen}/workload-settings.txt"
    CONTENT "domain=${PGW_EXAMPLE_DDS_DOMAIN}\ndiagnostics=${diag}\n")
pgw_generate_dbc(pgw_example_codec
    "${CMAKE_CURRENT_SOURCE_DIR}/example.dbc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mapping.json" "${gen}")
pgw_micro_codegen(IDL_FILE "${gen}/signals.idl" OUTPUT_DIRECTORY "${gen}" VAR SIGNAL
    DEPENDS "${gen}/signals.idl")
pgw_micro_codegen(IDL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/probe.idl"
    OUTPUT_DIRECTORY "${gen}" VAR PROBE)
pgw_micro_codegen(IDL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.idl"
    OUTPUT_DIRECTORY "${gen}" VAR DIAGNOSTICS)
if(PGW_ENABLE_REMOTE_CONTROL)
    set(remote_control_argument --remote-control)
    set(gateway_control_arguments --remote-control --max-controller-peers
        "${PGW_REMOTE_CONTROL_MAX_CONTROLLERS}")
else()
    set(remote_control_argument)
    set(gateway_control_arguments)
endif()
if(PGW_ENABLE_REMOTE_CONTROL AND PGW_EXAMPLE_CONTROL_TELEMETRY)
    set(control_telemetry_argument --telemetry)
else()
    set(control_telemetry_argument)
endif()
set(extra_model_idl_inputs)
if(PGW_ENABLE_REMOTE_CONTROL)
    add_custom_command(OUTPUT "${gen}/controller.idl" "${gen}/control_resources.h"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/control_codegen.py"
            --gateway "${gen}/gateway.xml"
            --common-idl "${PROJECT_SOURCE_DIR}/core/control/idl/control_common.idl"
            --output "${gen}/control-service.idl"
            --resource-header "${gen}/control_resources.h" ${adapter_manifest_arguments}
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py"
            "${gen}/controller.idl" "${gen}/control-service.idl"
            --include-dir "${PROJECT_SOURCE_DIR}/core/control/idl"
        DEPENDS "${gen}/gateway.xml" "${PROJECT_SOURCE_DIR}/core/control/idl/control_common.idl"
            ${PGW_EXAMPLE_ADAPTER_MANIFESTS}
            "${PROJECT_SOURCE_DIR}/tools/config_codegen/control_codegen.py"
            "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py"
            "${PROJECT_SOURCE_DIR}/tools/config_codegen/config_codegen.py"
            "${PROJECT_SOURCE_DIR}/resources/schema/gateway.xsd" VERBATIM)
    install(FILES "${gen}/controller.idl"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/pgw/controllers")
    pgw_micro_codegen(IDL_FILE "${gen}/controller.idl" OUTPUT_DIRECTORY "${gen}" VAR CONTROL
        DEPENDS "${gen}/controller.idl")
    list(APPEND extra_model_idl_inputs "${gen}/controller.idl")
endif()
add_custom_command(OUTPUT "${gen}/model.idl"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py"
        "${gen}/model.idl" "${gen}/signals.idl" "${CMAKE_CURRENT_SOURCE_DIR}/probe.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.idl" ${extra_model_idl_inputs}
        --include-dir "${PROJECT_SOURCE_DIR}/core/control/idl"
    DEPENDS "${gen}/signals.idl" "${CMAKE_CURRENT_SOURCE_DIR}/probe.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.idl" ${extra_model_idl_inputs}
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py" VERBATIM)
pgw_micro_convert(FROM IDL TO XML INPUT "${gen}/model.idl"
    OUTPUT_DIRECTORY "${gen}" VAR PGW_MODEL_TYPES_XML
    DEPENDS "${gen}/model.idl")
add_custom_command(OUTPUT "${gen}/gateway.xml" "${gen}/schema_fingerprints.h"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/gateway_input.py"
        --template "${CMAKE_CURRENT_SOURCE_DIR}/gateway.xml.in"
        --inventory "${gen}/manifest.json" --diagnostics "${diag}" --output "${gen}/gateway.xml"
        --probe-idl "${CMAKE_CURRENT_SOURCE_DIR}/probe.idl"
        --diagnostics-idl "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.idl"
        --schema-header "${gen}/schema_fingerprints.h"
        ${gateway_control_arguments} ${control_telemetry_argument}
    DEPENDS "${gen}/manifest.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/gateway.xml.in"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/gateway_input.py" VERBATIM)
add_custom_command(OUTPUT "${gen}/dds_type_bindings.c" "${gen}/dds_type_bindings.h"
    COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/dds_type_binding_codegen.py"
        --types-xml "${PGW_MODEL_TYPES_XML}" --gateway "${gen}/gateway.xml"
        --output "${gen}/dds_type_bindings.c" --header "${gen}/dds_type_bindings.h"
    DEPENDS "${PGW_MODEL_TYPES_XML}" "${gen}/gateway.xml"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/dds_type_binding_codegen.py"
    VERBATIM)
add_custom_command(OUTPUT "${gen}/dds.xml"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/dds_workload.py"
        --template "${CMAKE_CURRENT_SOURCE_DIR}/dds.xml.in" --inventory "${gen}/manifest.json"
        --types "${PGW_MODEL_TYPES_XML}" --domain "${PGW_EXAMPLE_DDS_DOMAIN}"
        --diagnostics "${diag}" --gateway "${gen}/gateway.xml" --output "${gen}/dds.xml"
    DEPENDS "${gen}/manifest.json" "${PGW_MODEL_TYPES_XML}" "${gen}/gateway.xml"
        "${gen}/workload-settings.txt" "${CMAKE_CURRENT_SOURCE_DIR}/dds.xml.in"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/dds_workload.py" VERBATIM)
add_custom_command(OUTPUT "${gen}/gateway_config.c"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/config_codegen.py"
        --gateway "${gen}/gateway.xml" --inventory "${gen}/manifest.json"
        --output "${gen}/gateway_config.c" ${remote_control_argument}
        ${adapter_manifest_arguments}
    DEPENDS "${gen}/gateway.xml" "${gen}/dds.xml" "${gen}/manifest.json"
        ${PGW_EXAMPLE_ADAPTER_MANIFESTS}
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/config_codegen.py"
        "${PROJECT_SOURCE_DIR}/resources/schema/gateway.xsd" VERBATIM)
pgw_micro_appgen(XML_FILE "${gen}/dds.xml" IDL_FILE model.idl
    OUTPUT_DIRECTORY "${gen}" VAR DDS)
add_library(pgw_example_dds_model STATIC
    ${SIGNAL_C_SOURCES} ${PROBE_C_SOURCES} ${DIAGNOSTICS_C_SOURCES} ${DDS_C_APPGEN_SOURCES}
    ${CONTROL_C_SOURCES}
    "${gen}/gateway_config.c" "${gen}/schema_fingerprints.h"
    "${gen}/dds_type_bindings.c"
    "${PROJECT_SOURCE_DIR}/bindings/signal/src/dds_conversion.c"
    diagnostics_conversion.c diagnostics_binding.c graph.c)
if(PGW_ENABLE_REMOTE_CONTROL)
    target_sources(pgw_example_dds_model PRIVATE control_binding.c)
endif()
target_include_directories(pgw_example_dds_model PUBLIC "${gen}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(pgw_example_dds_model PUBLIC
    "${PROJECT_SOURCE_DIR}/tools/config_codegen/include")
pgw_link_adapter(pgw_example_dds_model pgw_adapter_can)
pgw_link_adapter(pgw_example_dds_model pgw_adapter_dds_connext_micro)
target_link_libraries(pgw_example_dds_model PUBLIC pgw_example_codec
    PRIVATE PGW::build_options)
target_compile_definitions(pgw_example_dds_model PUBLIC PGW_DDS_DIAGNOSTICS=${diag})
add_executable(pgw_dds_companion companion.c)
target_link_libraries(pgw_dds_companion PRIVATE pgw_example_dds_model PGW::build_options)
if(PGW_ENABLE_REMOTE_CONTROL)
    add_executable(pgw_dds_control_controller control_controller.c)
    target_link_libraries(pgw_dds_control_controller PRIVATE
        pgw_example_dds_model PGW::build_options)
endif()
if(TARGET pgw_can_socketcan)
    add_executable(pgw_can_dds_gateway gateway.c)
    target_link_libraries(pgw_can_dds_gateway PRIVATE pgw_example_dds_model
        PGW::can_socketcan PGW::can_memory PGW::core PGW::build_options)
endif()
if(PGW_BUILD_TESTS)
    add_executable(pgw_dds_test test_dds.c)
    target_link_libraries(pgw_dds_test PRIVATE pgw_example_dds_model
        PGW::can_memory pgw_allocation_probe PGW::build_options)
    if(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        target_sources(pgw_dds_test PRIVATE route_latency_benchmark.c)
    endif()
    if(PGW_ENABLE_REMOTE_CONTROL)
        math(EXPR dynamic_domain "${PGW_EXAMPLE_DDS_DOMAIN} + 1")
        target_compile_definitions(pgw_dds_test PRIVATE
            PGW_EXAMPLE_DYNAMIC_DOMAIN=${dynamic_domain})
    endif()
    target_link_options(pgw_dds_test PRIVATE -Wl,--wrap=PGW_Arena_allocate)
    add_test(NAME dds.real_gateway COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/private_domain.py"
        --domain "${PGW_EXAMPLE_DDS_DOMAIN}" "$<TARGET_FILE:pgw_dds_test>")
    set_tests_properties(dds.real_gateway PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE
        LABELS "DDS;integration;private-domain" SKIP_RETURN_CODE 77)
    add_test(NAME config.actual_mag COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/test_pipeline.py"
        --dds "${gen}/dds.xml" --wrapper "${PGW_GENERATOR_WRAPPER}" --mag "${RTIAPPGEN}")
    set_tests_properties(config.actual_mag PROPERTIES TIMEOUT 60 LABELS "MAG;configuration")
    if(TARGET pgw_can_dds_gateway)
        add_test(NAME dds.companion_processes COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/private_domain.py" --domain "${PGW_EXAMPLE_DDS_DOMAIN}"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_processes.py"
            --gateway "$<TARGET_FILE:pgw_can_dds_gateway>"
            --companion "$<TARGET_FILE:pgw_dds_companion>")
        set_tests_properties(dds.companion_processes PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE
            SKIP_RETURN_CODE 77 LABELS "DDS;integration;private-domain")
    endif()
    if(PGW_ENABLE_REMOTE_CONTROL AND TARGET pgw_can_dds_gateway)
        if(PGW_EXAMPLE_CONTROL_TELEMETRY)
            set(control_test_arguments --telemetry)
        else()
            set(control_test_arguments)
        endif()
        add_test(NAME dds.remote_control_roundtrip COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/private_domain.py" --domain "${dynamic_domain}"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_control_processes.py"
            --gateway "$<TARGET_FILE:pgw_can_dds_gateway>"
            --controller "$<TARGET_FILE:pgw_dds_control_controller>"
            --domain "${dynamic_domain}" --controller-idl "${gen}/controller.idl"
            ${control_test_arguments})
        set_tests_properties(dds.remote_control_roundtrip PROPERTIES TIMEOUT 30
            RUN_SERIAL TRUE SKIP_RETURN_CODE 77 LABELS "DDS;integration;remote-control")
    endif()
endif()
