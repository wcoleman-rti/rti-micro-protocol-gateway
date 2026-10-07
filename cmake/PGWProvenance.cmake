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

include_guard(GLOBAL)

function(pgw_generate_provenance)
    set(_provenance [=[{
        "schema_version": 1,
        "sdk": {},
        "host_tools": {},
        "compiler": {},
        "build": {"flags": {}, "options": {}}
    }]=])
    macro(_pgw_provenance_string value)
        string(REPLACE "\\" "\\\\" _escaped "${value}")
        string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
        string(REPLACE "\n" "\\n" _escaped "${_escaped}")
        string(REPLACE "\r" "\\r" _escaped "${_escaped}")
        string(REPLACE "\t" "\\t" _escaped "${_escaped}")
        string(JSON _provenance SET "${_provenance}" ${ARGN} "\"${_escaped}\"")
    endmacro()

    _pgw_provenance_string("${PROJECT_VERSION}" project_version)
    _pgw_provenance_string("${RTIME_VERSION}" sdk version)
    _pgw_provenance_string("${RTIMEHOME}" sdk root)
    _pgw_provenance_string("${RTIME_PIL_ARCH}" sdk pil)
    _pgw_provenance_string("${RTIME_PSL_ARCH}" sdk psl)
    _pgw_provenance_string("$<IF:$<CONFIG:Debug>,Debug,Release>" sdk archive_configuration)
    string(JSON _provenance SET "${_provenance}" sdk static_linkage true)
    _pgw_provenance_string("${PGW_JREHOME}" host_tools jre_home)
    _pgw_provenance_string("${PGW_JRE_VERSION}" host_tools java_version)
    _pgw_provenance_string("${RTICODEGEN_VERSION}" host_tools codegen_version)
    _pgw_provenance_string("${RTIAPPGEN_VERSION}" host_tools mag_version)
    _pgw_provenance_string("${Python3_EXECUTABLE}" host_tools python)
    _pgw_provenance_string("${Python3_VERSION}" host_tools python_version)
    _pgw_provenance_string("${CMAKE_VERSION}" host_tools cmake_version)
    _pgw_provenance_string("${CMAKE_C_COMPILER}" compiler executable)
    _pgw_provenance_string("${CMAKE_C_COMPILER_ID}" compiler id)
    _pgw_provenance_string("${CMAKE_C_COMPILER_VERSION}" compiler version)
    string(JSON _provenance SET "${_provenance}" compiler c_standard 11)
    _pgw_provenance_string("$<CONFIG>" build configuration)
    _pgw_provenance_string("${CMAKE_GENERATOR}" build generator)
    _pgw_provenance_string("${CMAKE_C_FLAGS}" build flags common)
    foreach(_configuration IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        _pgw_provenance_string("${CMAKE_C_FLAGS_${_configuration}}"
            build flags "${_configuration}")
    endforeach()
    foreach(_option IN ITEMS PGW_ENABLE_CAN PGW_ENABLE_DDS PGW_ENABLE_REMOTE_CONTROL
            PGW_BUILD_TESTS PGW_BUILD_EXAMPLES PGW_BUILD_BENCHMARKS
            PGW_WARNINGS_AS_ERRORS PGW_GENERATOR_WARNINGS_AS_ERRORS)
        if(${_option})
            set(_boolean true)
        else()
            set(_boolean false)
        endif()
        string(JSON _provenance SET "${_provenance}" build options "${_option}" ${_boolean})
    endforeach()
    _pgw_provenance_string("${PGW_GENERATOR_WARNING_ALLOW_REGEX}" build generator_warning_allow_regex)
    _pgw_provenance_string("${PGW_REMOTE_CONTROL_MAX_CONTROLLERS}"
        build remote_control_max_controllers)
    if(CMAKE_CONFIGURATION_TYPES)
        set(PGW_PROVENANCE_FILE "${CMAKE_BINARY_DIR}/$<CONFIG>/provenance.json")
    else()
        set(PGW_PROVENANCE_FILE "${CMAKE_BINARY_DIR}/provenance.json")
    endif()
    file(GENERATE OUTPUT "${PGW_PROVENANCE_FILE}" CONTENT "${_provenance}\n")
    set(PGW_PROVENANCE_FILE "${PGW_PROVENANCE_FILE}" PARENT_SCOPE)
endfunction()
