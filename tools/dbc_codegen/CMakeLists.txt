find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(pgw_dbc_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PGW_DBC_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
    "Selected host Python environment containing the pinned DBC parser")
if(NOT EXISTS "${PGW_DBC_PYTHON}")
    message(FATAL_ERROR "PGW_DBC_PYTHON does not exist: ${PGW_DBC_PYTHON}. "
        "Create .venv and install tools/dbc_codegen/requirements.txt, "
        "or configure -DPGW_DBC_PYTHON=/path/to/environment/python.")
endif()
execute_process(COMMAND "${PGW_DBC_PYTHON}" -c
    "import cantools; assert cantools.__version__ == '40.7.1', 'cantools 40.7.1 required'"
    RESULT_VARIABLE pgw_dbc_dependency_status
    ERROR_VARIABLE pgw_dbc_dependency_error)
if(NOT pgw_dbc_dependency_status EQUAL 0)
    message(FATAL_ERROR "PGW_DBC_PYTHON needs the pinned dependencies. "
        "Create a repository-local .venv with python3 -m venv .venv, "
        "install tools/dbc_codegen/requirements.txt using .venv/bin/python -m pip, "
        "and configure -DPGW_PYTHON_EXECUTABLE=${pgw_dbc_root}/.venv/bin/python "
        "(clear PGW_DBC_PYTHON if it was cached).\n"
        "${pgw_dbc_dependency_error}")
endif()
function(pgw_generate_dbc target dbc mapping output_dir)
    get_filename_component(generator_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    set(outputs "${output_dir}/pgw_codec.c" "${output_dir}/pgw_codec.h"
        "${output_dir}/signals.idl" "${output_dir}/manifest.json")
    add_custom_command(OUTPUT ${outputs}
        COMMAND "${PGW_DBC_PYTHON}" "${generator_root}/tools/dbc_codegen/generate.py"
            --dbc "${dbc}" --mapping "${mapping}" --output "${output_dir}"
        DEPENDS "${dbc}" "${mapping}"
            "${generator_root}/tools/dbc_codegen/generate.py"
            "${generator_root}/tools/dbc_codegen/codec.c.in"
            "${generator_root}/schemas/signal.idl"
        VERBATIM)
    add_library(${target} STATIC "${output_dir}/pgw_codec.c")
    target_include_directories(${target} PUBLIC "${output_dir}")
    target_link_libraries(${target} PUBLIC PGW::binding_signal)
    if(UNIX)
        target_link_libraries(${target} PUBLIC m)
    endif()
endfunction()
