#.rst:
# (c) 2023 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the software solely for use with RTI Connext Micro. Licensee may
# redistribute copies of the software provided that all such copies are
# subject to this license. The software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is
# under no obligation to maintain or support the software.  RTI shall not be
# liable for any incidental or consequential damages arising out of the use or
# inability to use the software.
#
# FindRTIConnextMicroDDS
# -----------------------
#
# Find RTI Connext Micro libraries.
#
# Components
# ^^^^^^^^^^
# This module sets variables for the following components that are part of RTI
#
# Default (always included):
# - core        rti_me
# - c_api       rti_me
# - cpp_api     rti_me_cpp + rti_me
# - rhsm        rti_me_rhsm + rti_me
# - whsm        rti_me_whsm + rti_me
# - osapi       [rti_me_ospsl] + rti_me
# - netio       [rti_me_netiopsl] + rti_me
# - netio_cpp   [rti_me_netiopsl_cpp] + [rti_me_netiopsl] + rti_me
#
# Optional (must be explicitly selected):
# - dpse:       rti_me_discdpse + rti_me
# - dpde:       rti_me_discdpde + rti_me
# - appgen:     rti_me_appgen + [rti_me_ospsl] + [rti_me_netiopsl] + rti_me + rti_me_discdpde + rti_me_discdpse
# - filter:     rti_me_ddsfilter + rti_me
# - xtypes:     rti_me_ddsxtypes + rti_me (not available in CERT profile)
# - psk:        rti_me_ddspsk + rti_me
# - pskpsl_openssl3: rti_me_pskpsl_openssl3 (separate security package) + OpenSSL::Crypto
# - shmem:      rti_me_netioshmem + [rti_me_netiopsl] + rti_me
# - zcv1:       rti_me_netiosdm + rti_me_netioshmem + [rti_me_netiopsl] + rti_me
# - zcv2:       rti_me_netiozcopy + rti_me_netioshmem + [rti_me_netiopsl] + rti_me
#
# Optional (combined imported targets)
# - c:          rti_me + [rti_me_ospsl] + [rti_me_netiopsl] + rti_me_rhsm + rti_me_whsm
# - cpp:        rti_me_cpp + [rti_me_ospsl] + [rti_me_netiopsl_cpp] + [rti_me_netiopsl] + rti_me_rhsm + rti_me_whsm
#
# Core is always selected, because the rest of components depend on it.
# However, the rest of components must be explicitly selected in the
# find_package invocation for this module to set variables for the different
# libraries associated with them.
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
# This module defines the following `IMPORTED` targets:
#
# Default (always included):
# - RTIConnextMicroDDS::core        core
# - RTIConnextMicroDDS::c_api       c_api (alias of core)
# - RTIConnextMicroDDS::cpp_api     cpp_api + {c_api}
# - RTIConnextMicroDDS::rhsm        rhsm + {core}
# - RTIConnextMicroDDS::whsm        whsm + {core}
# - RTIConnextMicroDDS::osapi       [ospsl] + {core}
# - RTIConnextMicroDDS::netio       [netiopsl] + {core}
# - RTIConnextMicroDDS::netio_cpp   [netiopsl_cpp] + {netio}
#
# Optional (must be explicitly selected):
# - RTIConnextMicroDDS::dpse:       discdpse + {core}
# - RTIConnextMicroDDS::dpde:       discdpde + {core}
# - RTIConnextMicroDDS::appgen:     appgen + {core} + {dpde} + {dpse}
# - RTIConnextMicroDDS::filter:     ddsfilter + {core}
# - RTIConnextMicroDDS::xtypes:     ddsxtypes + {core}
# - RTIConnextMicroDDS::psk:        ddspsk + {core}
# - RTIConnextMicroDDS::pskpsl_openssl3: pskpsl_openssl3 + OpenSSL::Crypto
# - RTIConnextMicroDDS::shmem:      netioshmem + {netio}
# - RTIConnextMicroDDS::zcv1:       netiosdm + {shmem}
# - RTIConnextMicroDDS::zcv2:       netiozcopy + {shmem}
#
# Optional (combined imported targets)
# - RTIConnextMicroDDS::c:          {c_api} + {osapi} + {netio} + {rhsm} + {whsm}
# - RTIConnextMicroDDS::cpp:        {cpp_api} + {osapi} + {netio_cpp} + {rhsm} + {whsm}
#
#
# Result Variables
# ^^^^^^^^^^^^^^^^
# This module will set the following variables in your project:
#
#   ``RTIME_TOOLCHAIN_FILE``
#   Path to the target platform toolchain file used to build RTI Connext Micro.
# - ``RTIME_COMPILE_DEFINITIONS``
#   RTI Connext Micro Compiler definitions as a list.
# - ``RTIME_EXTERNAL_LIBS``
#   RTI Connext Micro external dependencies.
# - ``RTIME_INCLUDE_DIRS``
#   RTI Connext Micro include directories.
# - ``RTI_MICRO_CODEGEN_DIR``
#   Path to the directory where RTI Codegen is placed.
# - ``RTICODEGEN``
#   Path to the RTI Codegen executable.
# - ``RTICODEGEN_VERSION``
#   RTI Codegen version.
#
# This module will set the following variables for the different libraries
# that are part of the components selected at configuration time.
# *Note that <BUILD_TYPE> and <LINK_TYPE> are placeholders that are
# derived from CMAKE_BUILD_TYPE (RELEASE/DEBUG) and BUILD_SHARED_LIBS
# (DEBUG/STATIC) variable values.
#
# - ``<LIBRARY_NAME>_LIBRARIES_<BUILD_TYPE>_<LINK_TYPE>``
#   Build configuration-specific libraries for <LIBRARY_NAME>.
#   (e.g., ``RTIME_CPP_API_LIBRARIES_RELEASE_STATIC``).
# - ``<LIBRARY_NAME>_LIBRARIES``
#   Depending on the value of CMAKE_BUILD_TYPE and BUILD_SHARED_LIBS,
#   this variable will be set with the requested value.
#   (e.g., if ``CMAKE_BUILD_TYPE`` is ``Release`` and ``BUILD_SHARED_LIBS`` is
#   ``OFF``, will have the same value as
#   ``RTIME_CPP_API_LIBRARIES_RELEASE_STATIC``).
#
#   Also, the ``<VAR>_FOUND`` variable is set for each one of the previous
#   variables.
#
# If you are building a simple RTIConnextMicroDDS application, use the following:
#
#  - For a C application: RTIME_C_API
#  - For a C++ application: RTIME_CPP_API
#
# Lastly, if you want to use the DPDE discovery plugin (or any other component),
# add the appropriate variables to your CMake script.
#
# This module requires an explicit RTIMEHOME. Set RTIME_PIL_ARCH,
# RTIME_PSL_ARCH and RTIME_TARGET_NAME when the installation contains multiple
# target architectures. RTIME_LIBS_BUILD_TYPE selects the matching archive
# variant; Debug and Release archives must not be mixed.
# Logging in versions lower than CMake 3.15
# ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
# For versions lower than CMake 3.15, the ``RTIME_LOG_LEVEL`` variable
# should be used to define logging levels lower than ``STATUS`` mode. All these
# modes will show messages of current level and higher. The following modes are
# available:
#
# - ``STATUS``
#   Default mode. This will olnly show messages with same or higher logging
#   level than CMake ``STATUS``.
# - ``VERBOSE``
#   This mode will show all messages from ``VERBOSE`` level and higher.
# - ``DEBUG``
#   This last mode will show messages from all previous levels and ``DEBUG``.


include(CMakeParseArguments)

# Starting with CMake 3.12, find_package function calls searches for prefixes
# specified by the <PackageName>_ROOT variable and its corresponding environment
# variable. The OLD behaviour is to ignore those kind of variables, so we set
# the policy to NEW.
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

# Starting with CMake 3.3, the support for the IN_LIST operator in the if block
# was added. The OLD behaviour is to ignore this operator, so we set the policy
# to NEW.
if(POLICY CMP0057)
    cmake_policy(SET CMP0057 NEW)
endif()

#####################################################################
# Global Variables                                                  #
#####################################################################

set(RTIME_LIBS_BUILD_TYPE_LIST "Release" "Debug" "Auto")
set(RTIME_LIBS_BUILD_TYPE "Auto" CACHE STRING
    "RTI Connext Micro imported target libraries build type to use"
)

if(NOT RTIME_LIBS_BUILD_TYPE IN_LIST RTIME_LIBS_BUILD_TYPE_LIST)
    message(FATAL_ERROR
        "Ensure the RTIME_LIBS_BUILD_TYPE value is one of Auto, Release or"
        " Debug without quotes"
    )
endif()

if(NOT PGW_MICRO_RUNTIME_ONLY AND CMAKE_BUILD_TYPE AND
    ((CMAKE_BUILD_TYPE STREQUAL "Debug" AND RTIME_LIBS_BUILD_TYPE STREQUAL "Release") OR
     (NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND RTIME_LIBS_BUILD_TYPE STREQUAL "Debug")))
    message(WARNING
        "CMAKE_BUILD_TYPE and RTI_LIBS_BUILD_TYPE are conflicting. "
        "Mixing RTI Connext Micro Release and Debug "
        "libraries can result in build errors or application problems like "
        "double-freeing memory."
    )
endif()

# RTIME_REQUIRED_BUILD_TYPE is the build type that will be used to find the RTI Connext
# Micro libraries. It is set to:
# - RTIME_LIBS_BUILD_TYPE if not set to Auto
# - else, CMAKE_BUILD_TYPE if it is set
# - else, "Release" if RTIME_LIBS_BUILD_TYPE is set to Auto
if(NOT RTIME_LIBS_BUILD_TYPE STREQUAL "Auto")
    string(TOUPPER "${RTIME_LIBS_BUILD_TYPE}" RTIME_REQUIRED_BUILD_TYPE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RTIME_REQUIRED_BUILD_TYPE "DEBUG")
else()
    set(RTIME_REQUIRED_BUILD_TYPE "RELEASE")
endif()

#####################################################################
# Logging Macros                                                    #
#####################################################################

# These two macros allow better code tracing with debug and verbose messages
# using new CMake 3.15 message types. If CMake 3.14 or lower is in use,
# default messages will be displayed starting with ``DEBUG`` or ``VERBOSE``
# instead.
#
# Arguments:
# - message: provides the text message
if("${CMAKE_VERSION}" VERSION_GREATER_EQUAL "3.15")
    macro(rtime_log_verbose)
        message(VERBOSE "VERBOSE " ${ARGN})
    endmacro()

    macro(rtime_log_debug)
        message(DEBUG "  DEBUG " ${ARGN})
    endmacro()
else()
    set(RTIME_LOG_LEVEL_LIST "STATUS" "VERBOSE" "DEBUG")

    if(NOT RTIME_LOG_LEVEL)
        set(RTIME_LOG_LEVEL "STATUS")
    endif()

    if(NOT RTIME_LOG_LEVEL IN_LIST RTIME_LOG_LEVEL_LIST)
        string(REPLACE ";"
            " " LOG_LEVELS_STRING
            "${RTIME_LOG_LEVEL_LIST}")
        message(FATAL_ERROR "Log level must be one of: ${LOG_LEVELS_STRING}. "
                "It's default value is STATUS.")
    endif()

    macro(rtime_log_verbose)
        if("${RTIME_LOG_LEVEL}" MATCHES "VERBOSE|DEBUG")
            message(STATUS "VERBOSE " ${ARGN})
        endif()
    endmacro()

    macro(rtime_log_debug)
        if("${RTIME_LOG_LEVEL}" STREQUAL "DEBUG")
            message(STATUS "  DEBUG " ${ARGN})
        endif()
    endmacro()
endif()

# This macro allow xml login with previous ``VERBOSE`` loger macros.
#
# Arguments:
# - XML_NAME: provides the name of the xml
# - XML: provides the xml contents
macro(rtime_log_xml XML_NAME XML)
    string(REPLACE "\n"
        ";" lines
        ${XML})

    rtime_log_verbose("~~~~~~~START ${XML_NAME}~~~~~~~")
    foreach(line ${lines})
        rtime_log_verbose("\t${line}")
    endforeach()
    rtime_log_verbose("~~~~~~~FINISH ${XML_NAME}~~~~~~~")
endmacro()

#####################################################################
# Preconditions Check                                               #
#####################################################################
#
# The selected SDK root is explicit: machine-specific default installation
# paths are not searched.
if(NOT RTIMEHOME)
    if(DEFINED ENV{RTIMEHOME})
        set(RTIMEHOME "$ENV{RTIMEHOME}")
    else()
        message(FATAL_ERROR "Set RTIMEHOME to the installed Connext Micro SDK root")
    endif()
endif()

# Micro installations must have a dds_c_config.h header file under
# the installation directory as we will use it to verify that the
# version is appropriate.
find_path(RTIMEHOME
    NAMES
        "include/rti_me/dds_c/dds_c_config.h"
    HINTS
        "${RTIMEHOME}"
)

if(NOT RTIMEHOME)
    set(error
        "RTIMEHOME not specified. Please set -DRTIMEHOME= to "
        "your RTI Connext Micro installation directory"
    )
    message(FATAL_ERROR ${error})
endif()

message(STATUS "RTI Connext Micro installation directory: ${RTIMEHOME}")


#####################################################################
# Helper Functions                                                  #
#####################################################################

# This function is used to derive the build and link type-specific
# library name from a base name.
#
# Arguments:
# - VAR: variable to set with the library name
# - NAME: base name of the library to find
# - BUILD_TYPE: build type (Release or Debug). Default is RTIME_REQUIRED_BUILD_TYPE.
# - LINK_TYPE: link type (Static or Shared). Default is derived from BUILD_SHARED_LIBS.
function(get_rtime_lib_name)
    set(options "")
    set(single_value_args "VAR" "NAME" "BUILD_TYPE" "LINK_TYPE")
    set(multi_value_args "")
    cmake_parse_arguments(_RTIME
        "${options}"
        "${single_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    rtime_log_debug("get_rtime_lib_name called")
    rtime_log_debug(
        "====================================================================")
    rtime_log_debug("\tvar: ${_RTIME_VAR}")
    rtime_log_debug("\tname: ${_RTIME_NAME}")
    rtime_log_debug("\tbuild_type: ${_RTIME_BUILD_TYPE}")
    rtime_log_debug("\tlink_type: ${_RTIME_LINK_TYPE}")

    if(NOT _RTIME_VAR OR NOT _RTIME_NAME)
        message(FATAL_ERROR "get_rtime_lib_name: VAR and NAME arguments are required")
    endif()

    if(NOT _RTIME_BUILD_TYPE)
        set(_RTIME_BUILD_TYPE ${RTIME_REQUIRED_BUILD_TYPE})
    endif()
    if(NOT _RTIME_LINK_TYPE)
        if(BUILD_SHARED_LIBS)
            set(_RTIME_LINK_TYPE "SHARED")
        else()
            set(_RTIME_LINK_TYPE "STATIC")
        endif()
    endif()

    string(TOUPPER ${_RTIME_BUILD_TYPE} build_type_upper)
    string(TOUPPER ${_RTIME_LINK_TYPE} link_type_upper)


    rtime_log_debug("\t\t(modified build_type: ${_RTIME_BUILD_TYPE})")
    rtime_log_debug("\t\t(modified link_type: ${_RTIME_LINK_TYPE})")

    set(lib_name "${_RTIME_NAME}")
    if(${link_type_upper} STREQUAL "STATIC")
        set(lib_name "${lib_name}z")
    endif()
    if(${build_type_upper} STREQUAL "DEBUG")
        set(lib_name "${lib_name}d")
    endif()

    set(${_RTIME_VAR} "${lib_name}" PARENT_SCOPE)
    rtime_log_debug(
        "\tget_rtime_lib_name: ${_RTIME_VAR} = ${lib_name}"
    )
    rtime_log_debug(
        "====================================================================")
endfunction()

# This method searches the libraries indicated in `library_names` under
# `<RTI Connext Micro directory>/lib/<architecture>. If one of the libraries in
# one of the modes is not found, the variable <result_var_name>_FOUND is set
# to `FALSE`.
#
# After finding the libraries, the libraries are stored in different variables
# following the structure <result_var_name>_<build_type>_<link_type>. Each
# variable contain the libraries for a build and link mode. If one of the
# libraries is not found, the <result_var_name>_<build_type>_<link_type>_FOUND
# is set to `FALSE`.
#
# Arguments:
# - library_names: names of the libraries to find (sorted)
# - result_var_name: name of the variable to set with the found libraries
# Returns:
# - <result_var_name>_RELEASE_STATIC: containing the release/static libraries if found
# - <result_var_name>_RELEASE_SHARED:  containing the release/shared libraries if found
# - <result_var_name>_DEBUG_STATIC:  containing the debug/static libraries if found
# - <result_var_name>_DEBUG_SHARED:  containing the debug/shared libraries if found
# - <result_var_name>_FOUND: `True` if the libraries were found
function(get_all_library_variables
    library_names
    result_var_name)
    rtime_log_debug("get_all_library_variables called")
    rtime_log_debug(
        "====================================================================")
    rtime_log_debug("\tlibrary_names: ${library_names}")
    rtime_log_debug("\tresult_var_name: ${result_var_name}")

    set(mode_library_found TRUE)
    set(${result_var_name}_FOUND TRUE PARENT_SCOPE)

    foreach(build_mode "release" "debug")
        foreach(link_mode "static" "shared")
            set(mode_library_found TRUE)
            set(libraries)
            foreach(library_name ${library_names})

                get_rtime_lib_name(
                    VAR name
                    NAME ${library_name}
                    BUILD_TYPE ${build_mode}
                    LINK_TYPE ${link_mode}
                )
                rtime_log_debug(
                    "\t\t[${build_mode}/${link_mode}]Library name: ${name}")

                find_library(lib${library_name}_${build_mode}_${link_mode}
                    NAMES
                        ${name}
                    PATHS
                        ${RTIME_TARGET_DIR}
                        ${RTIME_PIL_DIR}
                        ${RTIME_PSL_DIR}
                    NO_DEFAULT_PATH
                    NO_CMAKE_PATH
                    NO_CMAKE_ENVIRONMENT_PATH
                    NO_SYSTEM_ENVIRONMENT_PATH
                    NO_CMAKE_SYSTEM_PATH
                )

                if(NOT lib${library_name}_${build_mode}_${link_mode})
                    set(mode_library_found FALSE)
                    break()
                else()
                    list(APPEND libraries
                        ${lib${library_name}_${build_mode}_${link_mode}})
                endif()
            endforeach()

            string(TOUPPER ${link_mode} upper_link_mode)
            string(TOUPPER ${build_mode} upper_build_mode)
            set(lib_var
                "${result_var_name}_LIBRARIES_${upper_build_mode}_${upper_link_mode}"
            )

            if(${mode_library_found})
                set(${lib_var} ${libraries} PARENT_SCOPE)
                set(${lib_var}_FOUND TRUE)
                rtime_log_debug("\t${lib_var} = ${libraries}")
                set(${lib_var}_FOUND TRUE PARENT_SCOPE)
            else()
                set(${lib_var}_FOUND FALSE)
                set(${lib_var}_FOUND FALSE PARENT_SCOPE)
            endif()
            unset(lib${library_name}_${build_mode}_${link_mode} CACHE)
        endforeach()
    endforeach()

    set(build_mode "${RTIME_REQUIRED_BUILD_TYPE}")

    if(${BUILD_SHARED_LIBS})
        set(link_mode "SHARED")
    else()
        set(link_mode "STATIC")
    endif()

    set(${result_var_name}_LIBRARIES
        ${${result_var_name}_LIBRARIES_${build_mode}_${link_mode}} PARENT_SCOPE)
    set(selected_mode_found
        "${result_var_name}_LIBRARIES_${build_mode}_${link_mode}_FOUND")
    if(DEFINED ${selected_mode_found} AND ${selected_mode_found})
        set(${result_var_name}_FOUND TRUE PARENT_SCOPE)
    else()
        set(${result_var_name}_FOUND FALSE PARENT_SCOPE)
    endif()
    rtime_log_debug(
        "\t${result_var_name}_LIBRARIES=${${result_var_name}_LIBRARIES}"
    )
    rtime_log_debug(
        "===================================================================="
    )
endfunction()

# This macro adds the location property to a list of properties in order to
# clean the ``create_connext_imported_target`` function.
# Arguments:
# - _list_var: List variable name to which to append the location properties.
# - _location_property_name: The name of the location property to append to the
#   list.
# - _library_var: Library path variable name.
macro(_append_location_property _list_var _location_property_name _library_var)
    list(GET ${_library_var} 0 _imported_library)
    rtime_log_debug("\t${_location_property_name}=${_library_var}")
    rtime_log_debug("\t\t${_imported_library}")
    list(APPEND ${_list_var} ${_location_property_name} "${_imported_library}")
    unset(_imported_library)
endmacro()

# This function helps to create a RTIConnextMicroDDS CMake imported target.
# Arguments:
# - TARGET: the name of the target to be created. Note that the prefix
#   `RTIConnextMicroDDS::` will be added.
# - VAR: name of the variable where the library and its dependencies were
#   stored. The library we want to use for the imported target must be at the
#   0 position. Note the dependencies included in this variable will not be
#   used for the creation of the imported target. The `DEPENDENCIES` argument
#   is for that purpose.
# - DEPENDENCIES: other imported targets or libraries that are dependencies.
# Returns:
# - A new target called `RTIConnextMicroDDS::<TARGET>`, set to work properly with
#   the desired RTIConnextMicroDDS library, using the DEPENDENCIES as interface
#   dependencies.
#   If the target was created previusly or the library was not found, nothing
#   will happen.
function(create_rtime_imported_target)
    set(options "")
    set(single_value_args "TARGET" "VAR")
    set(multi_value_args "DEPENDENCIES")
    cmake_parse_arguments(_RTIME
        "${options}"
        "${single_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    rtime_log_debug("create_rtime_imported_target called")
    rtime_log_debug(
        "===================================================================="
    )

    set(target_name RTIConnextMicroDDS::${_RTIME_TARGET})
    rtime_log_debug("\ttarget_name=${target_name}")

    if(TARGET ${target_name})
        rtime_log_debug("\tThe target already exists. Skipping...")
        rtime_log_debug(
            "=================================================================="
            "=="
        )
        return() # Nothing to be done
    endif()

    if(NOT ${_RTIME_VAR}_FOUND)
        rtime_log_debug(
            "\tNot every library configuration has been found. Skipping..."
        )
        rtime_log_debug(
            "=================================================================="
            "=="
        )
        return() # Nothing to be done
    endif()

    set(imported_lib_base "${_RTIME_VAR}_LIBRARIES")

    # Set the build type to use
    if(BUILD_SHARED_LIBS)
        set(link_mode "SHARED")
        set(extra_options IMPORTED_NO_SONAME TRUE)
    else()
        set(link_mode "STATIC")
        set(extra_options)
    endif()

    # Define the location property to use
    if(WIN32 AND BUILD_SHARED_LIBS)
        set(location_property IMPORTED_IMPLIB)
    else()
        set(location_property IMPORTED_LOCATION)
    endif()

    set(import_location_properties)

    _append_location_property(
        "import_location_properties"
        "${location_property}"
        "${imported_lib_base}_${RTIME_REQUIRED_BUILD_TYPE}_${link_mode}"
    )

    if(RTIME_LIBS_BUILD_TYPE STREQUAL "Auto")
        foreach(build_mode "RELEASE" "DEBUG")
            _append_location_property(
                "import_location_properties"
                "${location_property}_${build_mode}"
                "${imported_lib_base}_${build_mode}_${link_mode}"
            )
        endforeach()
        list(APPEND import_location_properties
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
            MAP_IMPORTED_CONFIG_MINSIZEREL Release)
    else()
        _append_location_property(
            "import_location_properties"
            "${location_property}_${RTIME_REQUIRED_BUILD_TYPE}"
            "${imported_lib_base}_${RTIME_REQUIRED_BUILD_TYPE}_${link_mode}")
        list(APPEND import_location_properties
            MAP_IMPORTED_CONFIG_DEBUG "${RTIME_REQUIRED_BUILD_TYPE}"
            MAP_IMPORTED_CONFIG_RELEASE "${RTIME_REQUIRED_BUILD_TYPE}"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO "${RTIME_REQUIRED_BUILD_TYPE}"
            MAP_IMPORTED_CONFIG_MINSIZEREL "${RTIME_REQUIRED_BUILD_TYPE}")
    endif()

    # Create the library
    add_library(${target_name} ${link_mode} IMPORTED)

    # Set properties for all the targets
    set_target_properties(${target_name}
        PROPERTIES
            ${import_location_properties}
            ${extra_options}
    )

    # Add dependencies
    if(_RTIME_DEPENDENCIES)
        set_target_properties(${target_name}
            PROPERTIES
                INTERFACE_LINK_LIBRARIES
                   "${_RTIME_DEPENDENCIES}"
        )
    endif()

    rtime_log_debug(
        "===================================================================="
    )
endfunction()

# This function sets the order of libraries in a list variable based on a predefined target order.
# Arguments:
# - INPUT_LIST_VAR: the name of the list variable containing the libraries to reorder.
function(rtime_sort_libraries)
    set(options "")
    set(single_value_args "VAR")
    set(multi_value_args "LIBRARIES")
    cmake_parse_arguments(_RTIME
        "${options}"
        "${single_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    # The target library order
    set(target_order
        rti_me_appgen
        rti_me_cpp
        rti_me_netiosdm
        rti_me_netioshmem
        rti_me_netiozcopy
        rti_me_rhsm
        rti_me_whsm
        rti_me_discdpse
        rti_me_discdpde
        rti_me_netiopsl_cpp
        rti_me_netiopsl
        rti_me_ddsfilter
        rti_me_ddsxtypes
        rti_me_ddspsk
        rti_me_pskpsl_openssl3
        rti_me_ospsl
        rti_me
    )

    set(input_list ${_RTIME_LIBRARIES})

    # Remove duplicates
    list(LENGTH input_list LIST_LENGTH)
    if(LIST_LENGTH GREATER 0)
        list(REMOVE_DUPLICATES input_list)
    endif()

    # Reorder the items based on the target order
    set(reordered_libs)
    foreach(item IN LISTS target_order)
        if(item IN_LIST input_list)
            list(APPEND reordered_libs ${item})
        endif()
    endforeach()

    set(${_RTIME_VAR} ${reordered_libs} PARENT_SCOPE)
endfunction()

# This macro sets a variable to a value extracted from a #define in a C-header file.
# Arguments:
# - _header_file: the path to the header file.
# - _name: the name of the variable to extract.
# - _var: the optional varable to set the value of (default: ${_name}).
macro(_get_define_from_header _header_file _name) # _var
    rtime_log_debug("Extracting ${_name} from ${_header_file}")
    file(STRINGS
        "${_header_file}"
        _lines
        REGEX "^#define[ \t]+${_name}[ \t]+.*$"
    )

    if(_lines)
        if(${ARGC} EQUAL 3)
            set(_var "${ARGN}")
        else()
            set(_var ${_name})
        endif()
        list(GET _lines 0 _line)
        string(REGEX REPLACE
            "^#define[ \t]+${_name}[ \t]+(.*)$" "\\1"
            ${_var}
            "${_line}"
        )
        rtime_log_debug("  ${_var}=${${_var}}")
    else()
        rtime_log_debug("  ${_define} not found in ${_header_file}")
    endif()
    unset(_lines)
    unset(_line)
    unset(_var)
endmacro()

#####################################################################
# Get the version                                                   #
#####################################################################
# In the header files, there are variables that contain the version numbers
# in pieces. We will extract them and concatenate them to get the version.
find_file(
    config_header_file
    NAME "dds_c_config.h"
    HINTS
        "${RTIMEHOME}/include/dds_c"
        "${RTIMEHOME}/include/rti_me/dds_c"
)
if(NOT config_header_file)
    message(FATAL_ERROR "RTI Connext Micro \"dds_c_config.h\" header file not found. "
        "Please ensure it exists under your RTIMEHOME/include/rti_me/dds_c directory")
endif()
_get_define_from_header("${config_header_file}" "RTIME_DDS_VERSION_MAJOR"     "RTIME_VERSION_MAJOR")
_get_define_from_header("${config_header_file}" "RTIME_DDS_VERSION_MINOR"     "RTIME_VERSION_MINOR")
if(RTIME_VERSION_MAJOR GREATER 2)
    _get_define_from_header("${config_header_file}" "RTIME_DDS_VERSION_RELEASE"   "RTIME_VERSION_RELEASE")
    _get_define_from_header("${config_header_file}" "RTIME_DDS_VERSION_REVISION"  "RTIME_VERSION_REVISION")
else()
    # In versions 2.x and lower, the release and revision numbers are swapped
    # in the header file. We need to swap them back to get the correct version.
    _get_define_from_header("${config_header_file}" "RTIME_DDS_VERSION_REVISION"  "RTIME_VERSION_RELEASE")
    _get_define_from_header("${config_header_file}" "RTIME_DDS_VERSION_RELEASE"   "RTIME_VERSION_REVISION")
endif()
set(RTIME_VERSION
    "${RTIME_VERSION_MAJOR}.${RTIME_VERSION_MINOR}.${RTIME_VERSION_RELEASE}.${RTIME_VERSION_REVISION}"
)
rtime_log_verbose("RTI Connext Micro version: ${RTIME_VERSION}")


#####################################################################
# Find Code Generator and Application Generator                     #
#####################################################################

if(NOT PGW_MICRO_RUNTIME_ONLY)
set(codegen_name "rtiddsgen")
if(WIN32)
    set(codegen_name "${codegen_name}.bat")
endif()
rtime_log_debug("Codegen script ${codegen_name}")

find_path(RTI_MICRO_CODEGEN_DIR
    NAME "${codegen_name}"
    HINTS
        "${RTIMEHOME}/rtiddsgen/scripts"
        ENV RTI_MICRO_CODEGEN_DIR
)

if(NOT RTI_MICRO_CODEGEN_DIR)
    set(warning
        "Codegen was not found. Please, check if rtiddsgen is under your "
        "RTIMEHOME/rtiddsgen/scripts directory or provide it to CMake using -DRTI_MICRO_CODEGEN_DIR"
    )
    message(WARNING ${warning})
else()
    find_program(RTICODEGEN
        NAME
            "${codegen_name}"
        HINTS
            ${RTI_MICRO_CODEGEN_DIR}
        DOC "Path to RTI Codegen"
    )

    # Execute RTI Connext Micro Code Generator to get the version
    rtime_log_debug("Get the Codegen version: '${RTICODEGEN} -version'")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env "JREHOME=${PGW_JREHOME}" "${RTICODEGEN}" -version
        OUTPUT_VARIABLE codegen_version_string
        ERROR_VARIABLE codegen_version_string_error
        RESULT_VARIABLE codegen_version_result
    )
    if(NOT codegen_version_result EQUAL 0)
        message(FATAL_ERROR "Codegen version probe failed: ${codegen_version_string_error}")
    endif()
    if(codegen_version_string_error)
        message(WARNING
            "Error executing Codegen: ${codegen_version_string_error}"
        )
    else()

        # Sanitize Codegen output. When run for the first time, it shows some paths
        # and CMake doesn't like Windows paths
        string(REPLACE "\\" "/" codegen_version_string "${codegen_version_string}")
        rtime_log_debug("Command output:")
        rtime_log_debug("${codegen_version_string}")
        # Instead of searching for a version (also found in the RTIMEHOME directory
        # showed by Codegen when run for the first time), try to match a safer
        # string and then select the matching group for the version
        string(REGEX MATCH
            "rtiddsgen version ([0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?)"
            _ "${codegen_version_string}"
        )
        set(RTICODEGEN_VERSION "${CMAKE_MATCH_1}")
        rtime_log_verbose("Codegen version: ${RTICODEGEN_VERSION}")
    endif()
endif()

if(RTIME_VERSION_MAJOR GREATER 2)
    set(appgen_name "rtiddsmag")
    if(WIN32)
        set(appgen_name "${appgen_name}.bat")
    endif()
    rtime_log_debug("Appgen script ${appgen_name}")

    find_path(RTIAPPGEN_DIR
        NAME "${appgen_name}"
        HINTS
            "${RTIMEHOME}/rtiddsmag/scripts"
            ENV RTIAPPGEN_DIR
    )

    if(NOT RTIAPPGEN_DIR)
        set(warning
            "Appgen was not found. Please, check if rtiddsmag is under your "
            "RTIMEHOME/rtiddsmag/scripts directory or provide it to CMake using -DRTIAPPGEN_DIR"
        )
        message(WARNING ${warning})
    else()
        find_program(RTIAPPGEN
            NAME
                "${appgen_name}"
            HINTS
                ${RTIAPPGEN_DIR}
            DOC "Path to RTI Appgen"
        )

        # Execute RTI Micro Application Generator to get the version
        rtime_log_debug("Get the Appgen version: '${RTIAPPGEN} -version'")
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}" -E env "JREHOME=${PGW_JREHOME}" "${RTIAPPGEN}" -help
            OUTPUT_VARIABLE appgen_version_string
            ERROR_VARIABLE appgen_version_string_error
            RESULT_VARIABLE appgen_version_result
        )
        if(NOT appgen_version_result EQUAL 0)
            message(FATAL_ERROR "Appgen version probe failed: ${appgen_version_string_error}")
        endif()
        # Micro 4.3 MAG does not implement -version. Its distribution
        # manifest is authoritative; -help above checks the configured JRE.
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -c
                "import zipfile,sys; print(zipfile.ZipFile(sys.argv[1]).read('META-INF/MANIFEST.MF').decode())"
                "${RTIMEHOME}/rtiddsmag/class/rtiddsmag.jar"
            OUTPUT_VARIABLE appgen_manifest
            RESULT_VARIABLE appgen_manifest_result
        )
        if(NOT appgen_manifest_result EQUAL 0)
            message(FATAL_ERROR "Cannot read Micro MAG distribution version")
        endif()
        string(REGEX MATCH "Implementation-Version: ([0-9.]+)" _ "${appgen_manifest}")
        set(RTIAPPGEN_VERSION "${CMAKE_MATCH_1}")
        if(appgen_version_string_error)
            message(WARNING
                "Error executing Appgen: ${appgen_version_string_error}"
            )
        else()
            # Sanitize Appgen output. When run for the first time, it shows some paths
            # and CMake doesn't like Windows paths
            string(REPLACE "\\" "/" appgen_version_string "${appgen_version_string}")
            rtime_log_debug("Command output:")
            rtime_log_debug("${appgen_version_string}")
            # Instead of searching for a version (also found in the RTIMEHOME directory
            # showed by Appgen when run for the first time), try to match a safer
            # string and then select the matching group for the version
            rtime_log_verbose("Appgen version: ${RTIAPPGEN_VERSION}")
        endif()
    endif()
else()
    set(RTIAPPGEN "RTIAPPGEN-NOTFOUND" CACHE PATH
        "Path to RTI Micro Appgen")
    set(RTIAPPGEN_VERSION "RTIAPPGEN_VERSION-NOTFOUND" CACHE STRING
            "RTI Micro Appgen version")
endif()

endif() # NOT PGW_MICRO_RUNTIME_ONLY

#####################################################################
# Determine the target architecture                                 #
#####################################################################
# In this module, there are 3 relevant cache variables:
#   - RTIME_TARGET: Target architecture.
#     This is used to find the target platform's toolchain file under
#     $RTIMEHOME/resource/cmake/architectures. A custom toolchain file can be
#     provided using either the RTIME_TOOLCHAIN_FILE or CMAKE_TOOLCHAIN_FILE
#     variables instead, in which case RTIME_TARGET is ignored.
#     Supply a target name present in the selected SDK.
#   - RTIME_TARGET_NAME: Target architecture name .
#     This is used to find the installed integrated or Platform Specific Library (PSL)
#     architecture under $RTIMEHOME/lib.
#   - RTIME_PIL_ARCH: Platform Independent Library (PIL) architecture name.
#     This is used to find the installed PIL architecture under $RTIMEHOME/lib.

# Get and validate RTIME_TARGET
find_file(
    RTIME_TOOLCHAIN_FILE
    NAMES
        ${RTIME_TOOLCHAIN_FILE}
        $ENV{RTIME_TOOLCHAIN_FILE}
        ${RTIME_TARGET}.tc
        $ENV{RTIME_TARGET}.tc
        ${RTIMEARCH}.tc
        $ENV{RTIMEARCH}.tc
        ${CMAKE_TOOLCHAIN_FILE}
        self.tc
    PATHS
        "${RTIMEHOME}/resource/cmake/architectures"
    DOC
        "RTI Connext Micro toolchain file"
)
if(NOT RTIME_TOOLCHAIN_FILE)
    rtime_log_verbose("RTI Connext Micro target architecture not found (checked "
                      "RTIME_TARGET and RTIMEARCH)")
    message(WARNING "RTI Connext Micro target architecture not found. Output "
        "variable RTIME_TOOLCHAIN_FILE will not be set. Please set the RTIME_TARGET "
        "variable to the target name you are trying to build against. See the list "
        "of available architectures in the RTI Connext Micro installation "
        "RTIMEHOME/resource/cmake/architectures directory. The target name can be set "
        "using the CMake variables -DRTIME_TARGET or -DRTIMEARCH.")
else()
    # Get the full filename with extension (but no directory)
    get_filename_component(_filename "${RTIME_TOOLCHAIN_FILE}" NAME)

    # Remove ONLY the trailing ".tc"
    string(REGEX REPLACE "\\.tc$" "" RTIME_TARGET "${_filename}")
    set(RTIME_TARGET ${RTIME_TARGET} CACHE STRING
        "RTI Connext Micro target architecture")
    message(STATUS "Found toolchain file: ${RTIME_TOOLCHAIN_FILE} (see value of RTIME_TOOLCHAIN_FILE)")
    message(STATUS "Using RTIME_TARGET: ${RTIME_TARGET}")
endif()


# Check if there is a single architecture installed
file(GLOB architectures_installed
    RELATIVE "${RTIMEHOME}/lib"
    "${RTIMEHOME}/lib/*"
)
list(LENGTH architectures_installed num_architectures)
rtime_log_debug("Looking for installed target architectures")
if(num_architectures EQUAL 0)
    message(FATAL_ERROR "No target architectures found under ${RTIMEHOME}/lib")
elseif(num_architectures EQUAL 1)
    rtime_log_verbose("Found single installed target architecture: ${architectures_installed}")
    get_filename_component(default_arch "${architectures_installed}" NAME)
elseif(num_architectures EQUAL 2)
    # Check if it's one PIL/PSL installed target
    list(GET architectures_installed 0 arch1)
    list(GET architectures_installed 1 arch2)
    get_filename_component(arch1 "${arch1}" NAME)
    get_filename_component(arch2 "${arch2}" NAME)

    string(REGEX MATCH "(.*)-(.*)" pil_match ${arch1})
    if(pil_match AND CMAKE_MATCH_1 STREQUAL ${arch2})
        set(default_arch ${arch1})
    else()
        string(REGEX MATCH "(.*)-(.*)" pil_match ${arch2})
        if(pil_match AND CMAKE_MATCH_1 STREQUAL ${arch1})
            set(default_arch ${arch2})
        endif()
    endif()
endif()

# Get and validate RTIME_TARGET_NAME
if(RTIME_VERSION_MAJOR GREATER_EQUAL 4)
    # Search for platform-specific libraries.

    get_rtime_lib_name(
        VAR psl_lib_name
        NAME "rti_me_ospsl"
    )
    rtime_log_debug(
        "\t\tPlatform-specific library name: ${psl_lib_name}")

    find_library(FIND_RTIME_PSL
        NAMES
            ${psl_lib_name}
        PATHS
            ${RTIME_PSL_DIR}
            $ENV{RTIME_PSL_DIR}
            ${RTIME_TARGET_DIR}
            $ENV{RTIME_TARGET_DIR}
        NO_DEFAULT_PATH
        NO_CMAKE_PATH
        NO_CMAKE_ENVIRONMENT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
        NO_CMAKE_SYSTEM_PATH
    )
    if(NOT FIND_RTIME_PSL)
        rtime_log_debug("RTI Connext Micro target PSL name not found. Searching for "
            "platform-specific libraries under RTIMEHOME/lib")
        find_library(FIND_RTIME_PSL
            NAMES
                ${psl_lib_name}
            PATHS
                "${RTIMEHOME}/lib"
            PATH_SUFFIXES
                ${RTIME_PSL_ARCH}
                ${RTIME_TARGET_NAME}
                $ENV{RTIME_TARGET_NAME}
                ${RTIME_TARGET}
                $ENV{RTIME_TARGET}
                ${default_arch}
            NO_DEFAULT_PATH
            NO_CMAKE_PATH
            NO_CMAKE_ENVIRONMENT_PATH
            NO_SYSTEM_ENVIRONMENT_PATH
            NO_CMAKE_SYSTEM_PATH
        )
    endif()
    if(NOT FIND_RTIME_PSL)
        message(WARNING "RTI split libraries target name not found. If this is not intended, verify the values of "
            "RTIME_PIL_ARCH, RTIME_PSL_ARCH, RTIME_TARGET_NAME, and RTIME_TARGET are set correctly. "
            "Matching integrated libraries will be looked for instead.")
        rtime_log_verbose("    RTIME_PIL_ARCH: ${RTIME_PIL_ARCH}")
        rtime_log_verbose("    RTIME_PSL_ARCH: ${RTIME_PSL_ARCH}")
        rtime_log_verbose("    RTIME_TARGET_NAME: ${RTIME_TARGET_NAME}")
        rtime_log_verbose("    RTIME_TARGET: ${RTIME_TARGET}")
        unset(RTIME_PSL_ARCH)
        unset(RTIME_PSL_DIR)
    else()
        get_filename_component(RTIME_PSL_DIR "${FIND_RTIME_PSL}" DIRECTORY CACHE)
        get_filename_component(RTIME_PSL_ARCH "${RTIME_PSL_DIR}" NAME CACHE)
        message(STATUS "Using RTIME_PSL_ARCH: ${RTIME_PSL_ARCH}")
        rtime_log_verbose("Found RTI Connext Micro target PSL directory: ${RTIME_PSL_DIR}")

        set(RTIME_TARGET_NAME ${RTIME_PSL_ARCH} CACHE STRING
            "RTI Connext Micro target name")
        message(STATUS "Using RTIME_TARGET_NAME: ${RTIME_TARGET_NAME}")

        # Get and validate RTIME_PIL_ARCH
        # Search for platform-independent libraries.
        string(REGEX MATCH "(.*)-(.*)" pil_match ${RTIME_PSL_ARCH})
        if(pil_match)
            set(guessed_pil_name ${CMAKE_MATCH_1})
            get_filename_component(guessed_pil_dir "${RTIME_PSL_DIR}" DIRECTORY)
            set(guessed_pil_dir "${guessed_pil_dir}/${guessed_pil_name}")
        endif()

        get_rtime_lib_name(
            VAR pil_lib_name
            NAME "rti_me"
        )
        rtime_log_debug(
            "\t\tPlatform-independent library name: ${pil_lib_name}")

        find_library(FIND_RTIME_PIL
            NAMES
                ${pil_lib_name}
            PATHS
                ${RTIME_PIL_DIR}
                $ENV{RTIME_PIL_DIR}
                ${guessed_pil_dir}
            NO_DEFAULT_PATH
            NO_CMAKE_PATH
            NO_CMAKE_ENVIRONMENT_PATH
            NO_SYSTEM_ENVIRONMENT_PATH
            NO_CMAKE_SYSTEM_PATH
        )
        if(NOT FIND_RTIME_PIL)
            rtime_log_debug("RTI Connext Micro target PIL name not found. Searching for "
                "platform-independent libraries under RTIMEHOME/lib")
            find_library(FIND_RTIME_PIL
                NAMES
                    ${pil_lib_name}
                PATHS
                    "${RTIMEHOME}/lib"
                PATH_SUFFIXES
                    ${RTIME_PIL_ARCH}
                    $ENV{RTIME_PIL_ARCH}
                    ${guessed_pil_name}
                NO_DEFAULT_PATH
                NO_CMAKE_PATH
                NO_CMAKE_ENVIRONMENT_PATH
                NO_SYSTEM_ENVIRONMENT_PATH
                NO_CMAKE_SYSTEM_PATH
            )
        endif()
        if(NOT FIND_RTIME_PIL)
            message(WARNING "RTI Connext Micro PS target name found but not PI target name. Please explicitly set "
                "RTIME_PIL_ARCH and RTIME_PSL_ARCH variables to the platform-independent and platform-specific target "
                "names you are trying to build against. Alternatively, set RTIME_TARGET_NAME to the platform-specific "
                "target name and ensure the target names follow the proper RTI Micro split libraries naming convention. "
                " be derived.")
            # Unset the PSL variables to avoid mixing libraries with integrated libraries
            unset(RTIME_PIL_ARCH CACHE)
            unset(RTIME_PIL_DIR CACHE)
            unset(RTIME_PSL_ARCH CACHE)
            unset(RTIME_PSL_DIR CACHE)
        else()
            get_filename_component(RTIME_PIL_DIR "${FIND_RTIME_PIL}" DIRECTORY CACHE)
            get_filename_component(RTIME_PIL_ARCH "${RTIME_PIL_DIR}" NAME CACHE)
            message(STATUS "Using RTIME_PIL_ARCH: ${RTIME_PIL_ARCH}")
            rtime_log_verbose("Found RTI Connext Micro target PIL directory: ${RTIME_PIL_DIR}")
        endif()
    endif()
endif()

if(NOT RTIME_PSL_ARCH AND NOT RTIME_PIL_ARCH)
    # PSL was not found, search for integrated libraries.

    get_rtime_lib_name(
        VAR integrated_lib_name
        NAME "rti_me"
    )
    rtime_log_debug(
        "\t\tIntegrated library name: ${integrated_lib_name}")

    find_library(FIND_RTIME_TARGET
        NAMES
            ${integrated_lib_name}
        PATHS
            "${RTIMEHOME}/lib"
        PATH_SUFFIXES
            ${RTIME_PSL_ARCH}
            ${RTIME_TARGET_NAME}
            $ENV{RTIME_TARGET_NAME}
            ${RTIME_TARGET}
            $ENV{RTIME_TARGET}
            ${default_arch}
        NO_DEFAULT_PATH
        NO_CMAKE_PATH
        NO_CMAKE_ENVIRONMENT_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
        NO_CMAKE_SYSTEM_PATH
    )
    if(NOT FIND_RTIME_TARGET)
        message(FATAL_ERROR "RTI Connext Micro target name not found. Please set the RTIME_TARGET_NAME variable to the "
            "target name you are trying to build against. The target name is the name of the directory under your "
            "RTIMEHOME/lib directory. The target name can be set using the CMake variable -DRTIME_TARGET_NAME or use the "
            "value of -DRTIME_TARGET.")
    else()
        get_filename_component(RTIME_TARGET_DIR "${FIND_RTIME_TARGET}" DIRECTORY CACHE)
        get_filename_component(RTIME_TARGET_NAME "${RTIME_TARGET_DIR}" NAME CACHE)
        message(STATUS "Using RTIME_TARGET_NAME: ${RTIME_TARGET_NAME}")
        rtime_log_verbose("Found RTI Connext Micro integrated target directory: ${RTIME_TARGET_DIR}")
    endif()
endif()

#####################################################################
# Platform-specific Definitions                                     #
#####################################################################

# Apply PLATFORM_LIBS --> RTIME_EXTERNAL_LIBS
# This variable is set in the toolchain file and contains the external libraries
if(RTIME_TARGET_NAME MATCHES "Linux")
    # Linux Platforms
    set(RTIME_EXTERNAL_LIBS
        "-lrt"
        "-lpthread"
        "-lm"
    )
    set(RTIME_COMPILE_DEFINITIONS RTI_UNIX RTI_LINUX)
elseif(RTIME_TARGET_NAME MATCHES "Win")
    # Windows Platforms
    set(RTIME_EXTERNAL_LIBS
        "ws2_32"
        "netapi32"
        "advapi32"
        "user32"
        "winmm"
    )

    set(RTIME_COMPILE_DEFINITIONS
        "WIN32_LEAN_AND_MEAN"
    )
    if(RTIME_VERSION_MAJOR EQUAL 2)
         list(APPEND RTIME_COMPILE_DEFINITIONS "_WIN32_WINNT=0x0600")
    endif()

elseif(RTIME_TARGET_NAME MATCHES "Darwin")
    # Darwin Platforms
    set(RTIME_EXTERNAL_LIBS "")
    set(RTIME_COMPILE_DEFINITIONS "")
elseif(RTIME_TARGET_NAME MATCHES "QNX|QOS")
    set(RTIME_EXTERNAL_LIBS
        "-lm"
        "-lsocket"
    )
    set(RTIME_COMPILE_DEFINITIONS "")
else()
    message(WARNING "System configuration not detected from RTIME_TARGET_NAME (${RTIME_TARGET_NAME}). "
            "The following variables will not be set: RTIME_EXTERNAL_LIBS, RTIME_COMPILE_DEFINITIONS. "
            "Please set them manually, or call: include(\${RTIME_TOOLCHAIN_FILE}).")
endif()


#####################################################################
# Component Libraries & Dependencies                                #
#####################################################################

# For each <component>_libs list variable, the first element is the
# component library name, and the rest are the dependencies.


# Default library components.
# These libraries are always included.
set(core_libs
    "rti_me"
)
set(c_api_libs
    "rti_me"
)
set(cpp_api_libs
    "rti_me_cpp"
    ${c_api_libs}
)
set(rhsm_libs
    "rti_me_rhsm"
    ${core_libs}
)
set(whsm_libs
    "rti_me_whsm"
    ${core_libs}
)
if(RTIME_VERSION_MAJOR GREATER_EQUAL 4 AND RTIME_PSL_ARCH)
    set(osapi_libs
        "rti_me_ospsl"
        ${core_libs}
    )
    set(netio_libs
        "rti_me_netiopsl"
        ${core_libs}
    )
    set(netio_cpp_libs
        "rti_me_netiopsl_cpp"
        ${netio_libs}
    )
else()
    set(osapi_libs
        ${core_libs})
    set(netio_libs
        ${core_libs})
    set(netio_cpp_libs
        ${netio_libs})
endif()

# Optional library components.
# These libraries must be selected to be included.
set(dpse_libs
    "rti_me_discdpse"
    ${core_libs}
)
set(dpde_libs
    "rti_me_discdpde"
    ${core_libs}
)
set(appgen_libs
    "rti_me_appgen"
    ${osapi_libs}
    ${netio_libs}
    ${dpse_libs}
    ${dpde_libs}
    ${core_libs}
)

set(ddsfilter_libs
    "rti_me_ddsfilter"
    ${core_libs}
)
set(ddsxtypes_libs
    "rti_me_ddsxtypes"
    ${core_libs}
)
set(ddspsk_libs
    "rti_me_ddspsk"
    ${core_libs}
)
set(pskpsl_openssl3_libs
    "rti_me_pskpsl_openssl3"
    ${core_libs}
)

# CERT architectures consolidate DPSE and history support into rti_me. Their
# AppGen archive also resolves those symbols from core rather than separate
# DPSE/RHSM/WHSM archives.
if(RTI_CERT OR RTIME_CERT OR RTIME_TARGET_NAME MATCHES "CERT")
    set(rhsm_libs ${core_libs})
    set(whsm_libs ${core_libs})
    set(dpse_libs ${core_libs})
    set(appgen_libs
        "rti_me_appgen"
        ${osapi_libs}
        ${netio_libs}
        ${dpde_libs}
        ${core_libs}
    )
endif()
set(shmem_libs
    "rti_me_netioshmem"
    ${netio_libs}
)
set(zcv1_libs
    "rti_me_netiosdm"
    ${shmem_libs}
)
set(zcv2_libs
    "rti_me_netiozcopy"
    ${shmem_libs}
)

# Combined library components.
# These libraries must be selected to be included.
# These libraries simplify the inclusing of common libraries.
set(c_libs
    ${c_api_libs}
    ${osapi_libs}
    ${netio_libs}
    ${rhsm_libs}
    ${whsm_libs}
)
set(cpp_libs
    ${cpp_api_libs}
    ${osapi_libs}
    ${netio_cpp_libs}
    ${rhsm_libs}
    ${whsm_libs}
)

# Sort libraries for proper linking order.
rtime_sort_libraries(VAR core_libs      LIBRARIES ${core_libs})
rtime_sort_libraries(VAR c_api_libs     LIBRARIES ${c_api_libs})
rtime_sort_libraries(VAR cpp_api_libs   LIBRARIES ${cpp_api_libs})
rtime_sort_libraries(VAR rhsm_libs      LIBRARIES ${rhsm_libs})
rtime_sort_libraries(VAR whsm_libs      LIBRARIES ${whsm_libs})
rtime_sort_libraries(VAR osapi_libs     LIBRARIES ${osapi_libs})
rtime_sort_libraries(VAR netio_libs     LIBRARIES ${netio_libs})
rtime_sort_libraries(VAR netio_cpp_libs LIBRARIES ${netio_cpp_libs})
rtime_sort_libraries(VAR dpse_libs      LIBRARIES ${dpse_libs})
rtime_sort_libraries(VAR dpde_libs      LIBRARIES ${dpde_libs})
rtime_sort_libraries(VAR ddsfilter_libs LIBRARIES ${ddsfilter_libs})
rtime_sort_libraries(VAR ddsxtypes_libs LIBRARIES ${ddsxtypes_libs})
rtime_sort_libraries(VAR ddspsk_libs    LIBRARIES ${ddspsk_libs})
rtime_sort_libraries(VAR appgen_libs    LIBRARIES ${appgen_libs})
rtime_sort_libraries(VAR pskpsl_openssl3_libs LIBRARIES ${pskpsl_openssl3_libs})
rtime_sort_libraries(VAR shmem_libs     LIBRARIES ${shmem_libs})
rtime_sort_libraries(VAR zcv1_libs      LIBRARIES ${zcv1_libs})
rtime_sort_libraries(VAR zcv2_libs      LIBRARIES ${zcv2_libs})
rtime_sort_libraries(VAR c_libs         LIBRARIES ${c_libs})
rtime_sort_libraries(VAR cpp_libs       LIBRARIES ${cpp_libs})

#####################################################################
# Library Component Variables                                       #
#####################################################################

set(core_libs "rti_me")
get_all_library_variables("${core_libs}" "RTIME_CORE")
get_all_library_variables("${c_api_libs}" "RTIME_C_API")
get_all_library_variables("${cpp_api_libs}" "RTIME_CPP_API")
get_all_library_variables("${rhsm_libs}" "RTIME_RHSM")
get_all_library_variables("${whsm_libs}" "RTIME_WHSM")
get_all_library_variables("${osapi_libs}" "RTIME_OSAPI")
get_all_library_variables("${netio_libs}" "RTIME_NETIO")
get_all_library_variables("${netio_cpp_libs}" "RTIME_NETIO_CPP")
get_all_library_variables("${dpse_libs}" "RTIME_DPSE")
get_all_library_variables("${dpde_libs}" "RTIME_DPDE")
get_all_library_variables("${ddsfilter_libs}" "RTIME_DDSFILTER")
get_all_library_variables("${ddsxtypes_libs}" "RTIME_DDSXTYPES")
get_all_library_variables("${ddspsk_libs}" "RTIME_DDSPSK")
get_all_library_variables("${appgen_libs}" "RTIME_APPGEN")
get_all_library_variables("${pskpsl_openssl3_libs}" "RTIME_PSKPSL_OPENSSL3")

if(RTI_CERT OR RTIME_CERT OR RTIME_TARGET_NAME MATCHES "CERT")
    set(RTIME_DDSXTYPES_FOUND FALSE)
endif()

if(pskpsl_openssl3 IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS AND RTIME_PSKPSL_OPENSSL3_FOUND)
    find_package(OpenSSL 3.5 QUIET COMPONENTS Crypto)
    if(NOT OpenSSL_FOUND OR NOT TARGET OpenSSL::Crypto OR NOT RTIME_DDSPSK_FOUND)
        set(RTIME_PSKPSL_OPENSSL3_FOUND FALSE)
    endif()
endif()
set(RTIME_FILTER_FOUND ${RTIME_DDSFILTER_FOUND})
set(RTIME_XTYPES_FOUND ${RTIME_DDSXTYPES_FOUND})
set(RTIME_PSK_FOUND ${RTIME_DDSPSK_FOUND})
get_all_library_variables("${shmem_libs}" "RTIME_SHMEM")
get_all_library_variables("${zcv1_libs}" "RTIME_ZCV1")
get_all_library_variables("${zcv2_libs}" "RTIME_ZCV2")
get_all_library_variables("${c_libs}" "RTIME_C")
get_all_library_variables("${cpp_libs}" "RTIME_CPP")

#####################################################################
# Findable Library Component Results                                #
#####################################################################

macro(set_component_found _component_name)
    string(TOUPPER "${_component_name}" _upper_component_name)
    if(RTIME_${_upper_component_name}_FOUND)
        set(RTIConnextMicroDDS_${_component_name}_FOUND TRUE)
    else()
        set(RTIConnextMicroDDS_${_component_name}_FOUND FALSE)
    endif()
    rtime_log_debug("RTIConnextMicroDDS_${_component_name}_FOUND: ${RTIConnextMicroDDS_${_component_name}_FOUND}")
    unset(_upper_component_name)
endmacro()

# Required components
set_component_found(core)
set_component_found(c_api)
set_component_found(cpp_api)
set_component_found(rhsm)
set_component_found(whsm)
set_component_found(osapi)
set_component_found(netio)
set_component_found(netio_cpp)

# Optional components
if (appgen IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(appgen)
    list(APPEND RTIConnextMicroDDS_FIND_COMPONENTS "dpse" "dpde")
endif()
if (dpse IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(dpse)
endif()
if (dpde IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(dpde)
endif()
if(filter IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(filter)
endif()
if(xtypes IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(xtypes)
endif()
if(pskpsl_openssl3 IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    list(APPEND RTIConnextMicroDDS_FIND_COMPONENTS "psk")
    set_component_found(pskpsl_openssl3)
endif()
if(psk IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(psk)
endif()
if(zcv1 IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(zcv1)
    list(APPEND RTIConnextMicroDDS_FIND_COMPONENTS "shmem")
endif()
if(zcv2 IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(zcv2)
    list(APPEND RTIConnextMicroDDS_FIND_COMPONENTS "shmem")
endif()
if(shmem IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(shmem)
endif()

# Optional combined components
if(c IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(c)
endif()
if(cpp IN_LIST RTIConnextMicroDDS_FIND_COMPONENTS)
    set_component_found(cpp)
endif()


#####################################################################
# Version checks                                                    #
#####################################################################

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RTIConnextMicroDDS
    REQUIRED_VARS
        RTIMEHOME
        RTIME_VERSION
    VERSION_VAR
        RTIME_VERSION
    HANDLE_COMPONENTS)

#####################################################################
# Create the imported targets                                       #
#####################################################################
if(RTIConnextMicroDDS_FOUND)

    #################### default/required imported targets #####################
    # CORE
    create_rtime_imported_target(
        TARGET "core"
        VAR "RTIME_CORE"
        DEPENDENCIES
            ${RTIME_EXTERNAL_LIBS}
    )

    set(target_definitions ${RTIME_COMPILE_DEFINITIONS})

    set(RTIME_INCLUDE_DIRS
        "${RTIMEHOME}/include"
    )

    if(RTIME_VERSION_MAJOR GREATER 2)
        list(APPEND RTIME_INCLUDE_DIRS
            "${RTIMEHOME}/include/rti_me"
        )
    endif()

    set_target_properties(RTIConnextMicroDDS::core
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES
                "${RTIME_INCLUDE_DIRS}"
            INTERFACE_LINK_DIRECTORIES
                "${RTIME_TARGET_DIR}"
                "${RTIME_PIL_DIR}"
                "${RTIME_PSL_DIR}"
            INTERFACE_COMPILE_DEFINITIONS
                "${target_definitions}"
    )

    # C_API
    create_rtime_imported_target(
        TARGET "c_api"
        VAR "RTIME_C_API"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    # CPP_API
    create_rtime_imported_target(
        TARGET "cpp_api"
        VAR "RTIME_CPP_API"
        DEPENDENCIES
            RTIConnextMicroDDS::c_api
    )

    # RHSM
    create_rtime_imported_target(
        TARGET "rhsm"
        VAR "RTIME_RHSM"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    # WHSM
    create_rtime_imported_target(
        TARGET "whsm"
        VAR "RTIME_WHSM"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    # OSAPI
    create_rtime_imported_target(
        TARGET "osapi"
        VAR "RTIME_OSAPI"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    # NETIO
    create_rtime_imported_target(
        TARGET "netio"
        VAR "RTIME_NETIO"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    # NETIOCPP
    create_rtime_imported_target(
        TARGET "netio_cpp"
        VAR "RTIME_NETIO_CPP"
        DEPENDENCIES
            RTIConnextMicroDDS::netio
    )

    #################### optional imported targets #####################

    # DPSE
    create_rtime_imported_target(
        TARGET "dpse"
        VAR "RTIME_DPSE"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    # DPDE
    create_rtime_imported_target(
        TARGET "dpde"
        VAR "RTIME_DPDE"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    # APPGEN
    create_rtime_imported_target(
        TARGET "appgen"
        VAR "RTIME_APPGEN"
        DEPENDENCIES
            RTIConnextMicroDDS::core
            RTIConnextMicroDDS::dpse
            RTIConnextMicroDDS::dpde
    )

    # Optional feature libraries
    create_rtime_imported_target(
        TARGET "filter"
        VAR "RTIME_DDSFILTER"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    create_rtime_imported_target(
        TARGET "xtypes"
        VAR "RTIME_DDSXTYPES"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    create_rtime_imported_target(
        TARGET "psk"
        VAR "RTIME_DDSPSK"
        DEPENDENCIES
            RTIConnextMicroDDS::core
    )

    if(TARGET OpenSSL::Crypto)
        create_rtime_imported_target(
            TARGET "pskpsl_openssl3"
            VAR "RTIME_PSKPSL_OPENSSL3"
            DEPENDENCIES
                OpenSSL::Crypto
                RTIConnextMicroDDS::core
        )
    endif()

    # SHMEM
    create_rtime_imported_target(
        TARGET "shmem"
        VAR "RTIME_SHMEM"
        DEPENDENCIES
            RTIConnextMicroDDS::netio
    )

    # ZCV1
    create_rtime_imported_target(
        TARGET "zcv1"
        VAR "RTIME_ZCV1"
        DEPENDENCIES
            RTIConnextMicroDDS::shmem
    )

    # ZCV2
    create_rtime_imported_target(
        TARGET "zcv2"
        VAR "RTIME_ZCV2"
        DEPENDENCIES
            RTIConnextMicroDDS::shmem
    )

    #################### optional combined imported targets #####################

    # C
    create_rtime_imported_target(
        TARGET "c"
        VAR "RTIME_C"
        DEPENDENCIES
            RTIConnextMicroDDS::c_api
            RTIConnextMicroDDS::osapi
            RTIConnextMicroDDS::netio
            RTIConnextMicroDDS::rhsm
            RTIConnextMicroDDS::whsm
    )

    # CPP
    create_rtime_imported_target(
        TARGET "cpp"
        VAR "RTIME_CPP"
        DEPENDENCIES
            RTIConnextMicroDDS::cpp_api
            RTIConnextMicroDDS::osapi
            RTIConnextMicroDDS::netio_cpp
            RTIConnextMicroDDS::rhsm
            RTIConnextMicroDDS::whsm
    )

endif()