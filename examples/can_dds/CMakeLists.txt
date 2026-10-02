if(NOT PGW_ENABLE_DDS)
    return()
endif()
set(gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${gen}")
file(CONFIGURE OUTPUT "${gen}/modelPlugin.h"
    CONTENT "#include \"signalsPlugin.h\"\n#include \"probePlugin.h\"\n#include \"diagnosticsPlugin.h\"\n")
set(PGW_EXAMPLE_DDS_DOMAIN 83 CACHE STRING "Private loopback demonstration DDS domain")
option(PGW_DDS_DIAGNOSTICS "Compile named fixed-size DDS route snapshot endpoints" ON)
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
add_custom_command(OUTPUT "${gen}/model.idl"
    COMMAND "${CMAKE_COMMAND}" -E copy "${gen}/signals.idl" "${gen}/model.idl"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py"
        "${gen}/model.idl" "${CMAKE_CURRENT_SOURCE_DIR}/probe.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.idl"
    DEPENDS "${gen}/signals.idl" "${CMAKE_CURRENT_SOURCE_DIR}/probe.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.idl"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/combine_idl.py" VERBATIM)
add_custom_command(OUTPUT "${gen}/model.xml"
    COMMAND "${Python3_EXECUTABLE}" "${PGW_GENERATOR_WRAPPER}" "${RTICODEGEN}"
        -convertToXml -replace -d "${gen}" "${gen}/model.idl"
    DEPENDS "${gen}/model.idl" "${PGW_GENERATOR_WRAPPER}" VERBATIM)
add_custom_command(OUTPUT "${gen}/dds.xml" "${gen}/gateway.xml" "${gen}/schema_fingerprints.h"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/dds_workload.py"
        --template "${CMAKE_CURRENT_SOURCE_DIR}/dds.xml.in" --inventory "${gen}/manifest.json"
        --types "${gen}/model.xml" --domain "${PGW_EXAMPLE_DDS_DOMAIN}"
        --diagnostics "${diag}" --output "${gen}/dds.xml"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/gateway_input.py"
        --template "${CMAKE_CURRENT_SOURCE_DIR}/gateway.xml.in"
        --inventory "${gen}/manifest.json" --diagnostics "${diag}" --output "${gen}/gateway.xml"
        --probe-idl "${CMAKE_CURRENT_SOURCE_DIR}/probe.idl"
        --diagnostics-idl "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.idl"
        --schema-header "${gen}/schema_fingerprints.h"
    DEPENDS "${gen}/manifest.json" "${gen}/model.xml"
        "${gen}/workload-settings.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/dds.xml.in" "${CMAKE_CURRENT_SOURCE_DIR}/gateway.xml.in"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/dds_workload.py"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/gateway_input.py" VERBATIM)
add_custom_command(OUTPUT "${gen}/gateway_config.c"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/config_codegen/config_codegen.py"
        --gateway "${gen}/gateway.xml" --inventory "${gen}/manifest.json"
        --output "${gen}/gateway_config.c"
    DEPENDS "${gen}/gateway.xml" "${gen}/dds.xml" "${gen}/manifest.json"
        "${PROJECT_SOURCE_DIR}/tools/config_codegen/config_codegen.py"
        "${PROJECT_SOURCE_DIR}/config/gateway.xsd" VERBATIM)
pgw_micro_appgen(XML_FILE "${gen}/dds.xml" IDL_FILE model.idl
    OUTPUT_DIRECTORY "${gen}" VAR DDS)
add_library(pgw_example_dds_model STATIC
    ${SIGNAL_C_SOURCES} ${PROBE_C_SOURCES} ${DIAGNOSTICS_C_SOURCES} ${DDS_C_APPGEN_SOURCES}
    "${gen}/gateway_config.c" "${gen}/schema_fingerprints.h"
    probe_binding.c diagnostics_binding.c graph.c)
target_include_directories(pgw_example_dds_model PUBLIC "${gen}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(pgw_example_dds_model PUBLIC
    "${PROJECT_SOURCE_DIR}/tools/config_codegen/include")
target_link_libraries(pgw_example_dds_model PUBLIC PGW::adapter_dds_micro
    PGW::adapter_can pgw_example_codec PRIVATE PGW::build_options)
target_compile_definitions(pgw_example_dds_model PUBLIC PGW_DDS_DIAGNOSTICS=${diag})
if(EXISTS "${PROJECT_SOURCE_DIR}/bindings/signal/src/dds_binding.c")
    target_sources(pgw_example_dds_model PRIVATE "${PROJECT_SOURCE_DIR}/bindings/signal/src/dds_binding.c")
endif()
add_executable(pgw_dds_companion companion.c)
target_link_libraries(pgw_dds_companion PRIVATE pgw_example_dds_model PGW::build_options)
if(TARGET pgw_can_socketcan)
    add_executable(pgw_can_dds_gateway gateway.c)
    target_link_libraries(pgw_can_dds_gateway PRIVATE pgw_example_dds_model
        PGW::can_socketcan PGW::can_memory PGW::core PGW::build_options)
endif()
if(PGW_BUILD_TESTS)
    add_executable(pgw_dds_test test_dds.c)
    target_link_libraries(pgw_dds_test PRIVATE pgw_example_dds_model
        PGW::can_memory pgw_allocation_probe PGW::build_options)
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
endif()
