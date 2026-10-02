find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(pgw_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT COMMAND pgw_generate_dbc)
    include("${pgw_root}/tools/dbc_codegen/CMakeLists.txt")
endif()
add_test(NAME pgw_codegen_host
    COMMAND "${PGW_DBC_PYTHON}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}" -v)
if(TARGET PGW::binding_signal)
    pgw_generate_dbc(pgw_codegen_test_codec
        "${pgw_root}/examples/can_dds/example.dbc"
        "${pgw_root}/examples/can_dds/mapping.json"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_executable(pgw_test_codec test_codec.c)
    target_link_libraries(pgw_test_codec PRIVATE pgw_codegen_test_codec)
    add_test(NAME pgw_codec_golden COMMAND pgw_test_codec)
    if(TARGET pgw_example_dds_model)
        add_executable(pgw_test_signal_dds_conversion test_dds_conversion.c)
        target_link_libraries(pgw_test_signal_dds_conversion PRIVATE pgw_example_dds_model)
        add_test(NAME pgw_signal_dds_conversion COMMAND pgw_test_signal_dds_conversion)
    endif()
endif()
