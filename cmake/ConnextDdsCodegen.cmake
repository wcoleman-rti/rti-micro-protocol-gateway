# (c) 2017 Copyright, Real-Time Innovations, Inc.  All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the software solely for use with RTI Connext DDS.  Licensee may
# redistribute copies of the software provided that all such copies are
# subject to this license. The software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is
# under no obligation to maintain or support the software.  RTI shall not be
# liable for any incidental or consequential damages arising out of the use or
# inability to use the software.

#[[.rst:
.. _connextdds_codegen:

ConnextDdsCodegen
-----------------

CodeGen usage functions.


.. _connextdds_rtiddsgen_run:

Code generation
^^^^^^^^^^^^^^^

Generate a source files using RTI Codegen::

    connextdds_rtiddsgen_run(
        LANG <language>
        OUTPUT_DIRECTORY <dir>
        IDL_FILE <idl file path>
        [VAR variable]
        [NOT_REPLACE]
        [PACKAGE package]
        [STRING_SIZE size]
        [DISABLE_PREPROCESSOR]
        [INCLUDE_DIRS ...]
        [DEFINES ...]
        [UNBOUNDED]
        [NO_TYPECODE]
        [IGNORE_ALIGNMENT]
        [USE42_ALIGNMENT]
        [OPTIMIZE_ALIGNMENT]
        [STANDALONE]
        [EXTRA_ARGS ...]
        [USE_CODEGEN1]
        [GENERATE_EXAMPLE]
        [MICRO]
    )

This function calls ``codegen`` (rtiddsgen) to generates source files for the
language ``LANG`` passed as an argument to the function.

Arguments:

``IDL_FILE`` (mandatory)
    The IDL filename that will be used to generate code

``OUTPUT_DIRECTORY`` (mandatory)
    The directory where to put generated files

``LANG`` (mandatory)
    The language to generate source files for. Expected values are:
    C, C++, C++98, C++03, C++11, C++/CLI, C#, Java and Python.

``VAR``
    Use ``VAR`` as a prefix instead of using the IDL basename to name return
    values.

``NOT_REPLACE`` (optional)
    By default, we call ``codegen`` with the ``-replace`` argument, so every time
    the input IDL file has changed, we regenerate the source files. Passing this
    argument disables the flag.

``UNBOUNDED`` (optional)
    Generate type files with unbounded support (``-unboundedSupport``) flag.

``PACKAGE`` (optional)
    Specify the package name for Java source type files.

``PACKAGE`` (optional)
    Specify the size of the unbounded strings type.

``IGNORE_ALIGNMENT`` (optional)
    Generate type files with -ignoreAlignment flag enabled

``USE42_ALIGNMENT`` (optional)
    Generate type files with 4.2 compatible alignment (``-use42eAlignment`` flag)

``OPTIMIZE_ALIGNMENT`` (optional)
    Generate code with optimized alignment

``NO_TYPECODE``  (optional)
    Do not generate TypeCode in generated files

``DISABLE_PREPROCESSOR`` (optional)
    Disable the use of a preprocessor in Codegen


``STANDALONE`` (optional)
    Generate typecode files independant to RTI Connext DDS libraries.

``INCLUDE_DIRS``  (optional)
    List of include directories passed to Codegen (-I flag)

``DEFINES``  (optional)
    List of definitions passed to Codegen as arguments (-D flag)

``EXTRA_ARGS``  (optional)
    Extra arguments added to the Codegen command line

``USE_CODEGEN1`` (optional)
    Use rtiddsgen1.

``GENERATE_EXAMPLE`` (optional)
    Generate the source code for the publisher and subscriber.

``MICRO`` (optional)
    Generate code for RTI Connext Micro (-micro flag)

Output values:
The language variable is sanitized according to
:ref:`connextdds_sanitize_language` for the variable name.

``<idl_basename>_<LANG>_SOURCES``
    The list of generated source files. Empty for Java (see note below).

``<idl_basename>_<LANG>_HEADERS``
    The list of generated header files. Empty for Java (see note below).

``<idl_basename>_<LANG>_TIMESTAMP``
    Only used when generating code for Java. This is a dummy empty file that
    triggers the code generation when it doesn't exist or its modification
    time is before the IDL file. CMake will touch this file after each call to
    rtiddsgen.

If ``VAR`` is passed as a parameter, the variable name will be named like this:

``<VAR>_<LANG>_SOURCES``
    The list of the generated source files for the types. Empty for Java (see
    note below).

``<VAR>_<LANG>_HEADERS``
    The list of generated header files. Empty for Java (see note below).

``<VAR>_<LANG>_GENERATED_SOURCES``
    The list of generated source files. Empty for Java (see note below).

``<VAR>_<LANG>_PUBLISHER_SOURCE``
    Source code for the publisher if the example code was generated (see note
    below).

``<VAR>_<LANG>_SUBSCRIBER_SOURCE``
    Source code for the subscriber if the example code was generated (see note
    below).

``<VAR>_<LANG>_TIMESTAMP``
    Only used when generating code for Java. This is a dummy empty file that
    triggers the code generation when it doesn't exist or its modification
    time is before the IDL file. CMake will touch this file after each call to
    rtiddsgen.

.. note::

    In the case of Java the function won't return the list of source and
    header files. Instead, it will set the timestamp variable to a dummy file.
    A CMake target must depend on this file to trigger and handle the code
    generation. The reason is that to get the full list of Java generated
    files the function would need to get this information from CodeGen.
    Otherwise it would need to know the full IDL structure because for each
    IDL type a new file Java is created and for each module a new subfolder.

.. _connextdds_rtiddsgen_convert:

Convert
^^^^^^^
::

    connextdds_rtiddsgen_convert(
        INPUT inputFile
        FROM format
        TO format
        [VAR variable]
        [OUTPUT_DIRECTORY outputPath]
        [NOT_REPLACE]
        [INCLUDE_DIRS ...]
        [DEFINES ...]
        [EXTRA_ARGS ...]
        [DEPENDS ...]
        [USE_CODEGEN1]
    )

Call rtiddsgen to convert the format of the type representation
(IDL, XML or XSD).

``INPUT`` (required):
    The path to the input file.

``FROM`` (required)
    The input format. Accepted values are ``IDL``, ``XML`` and ``XSD``.

``TO`` (required)
    The output format. Accepted values are ``IDL``, ``XML`` and ``XSD``.

``VAR`` (optional)
    The name of the variable to set the output file.

``OUTPUT_DIRECTORY`` (optional)
    The directory to save the output file.

``NOT_REPLACE`` (optional)
    By default the command will overwrite any existing file. Set this file to
    skip converting if the output exists.

``INCLUDE_DIRS`` (optional)
    Additional include directory for the Codegen preprocessor.

``DEFINES`` (optional)
    Additional definitions for the Codegen preprocessor.

``EXTRA_ARGS`` (optional)
    Additional flags for Codegen.

``DEPENDS`` (optional)
    CMake dependencies for this command.

``USE_CODEGEN1`` (optional)
    Use rtiddsgen1.


.. _connextdds_appgen:

ConnextDdsAppgen
-----------------

AppGen usage functions.


.. _connextdds_rtiddsmag_run:

Micro application generation
^^^^^^^^^^^^^^^

Generate RTI Micro application files using RTI Appgen::

    connextdds_rtiddsmag_run(
        LANG <language>
        OUTPUT_DIRECTORY <dir>
        XML_FILE <xml file path>
        [VAR variable]
        [NOT_REPLACE]
        [IDL_FILE]
        [REFERENCED_FILES ...]
        [NO_DEFAULT_VALUES]
        [LEAVE_RESOURCE_LIMITS]
        [NO_DPSE_OPTIMIZATION]
        [NO_XML_LOCATIONS]
        [DPDE_NAME]
        [DPSE_NAME]
        [UDP_NAME]
        [SHMEM_NAME]
        [ZCOPY_NAME]
        [EXTRA_ARGS ...]
    )

This function calls ``appgen`` (rtiddsmag) to generates source files for the
language ``LANG`` passed as an argument to the function.

Arguments:

``XML_FILE`` (mandatory)
    The XML filename that will be used to generate code
    [-inputXml rtiddsmag option]

``OUTPUT_DIRECTORY`` (mandatory)
    The directory where to put generated files
    [-d rtiddsmag option]

``LANG`` (mandatory)
    The language to generate source files for. Expected values are:
    C, C++.
    [-language rtiddsmag option]

``VAR``
    Use ``VAR`` as a prefix instead of using the XML basename to name return
    values.

``NOT_REPLACE`` (optional)
    By default, we call ``appgen`` with the ``-replace`` argument, so every time
    the input XML file has changed, we regenerate the source files. Passing this
    argument disables the flag.
    [-replace rtiddsmag option]

``IDL_FILE`` (optional)
    The IDL file name used to generate the code by rtiddsgen, it doesn't need
    to exist when calling rtiddsmag, (default is using the name from the XML
    file). This value will be added in Appgen.h specifying the Plugin header
    generated by rtiddsgen.
    [-idlFile rtiddsmag option]

``REFERENCED_FILES``  (optional)
    Files which are referenced from the one being used to generate code. Can be
    repeated. (-referencedFile flag)
    [-referencedFile rtiddsmag option]

``NO_DEFAULT_VALUES``  (optional)
    Do not automatically replace the values that are set in each policy to their
    default value if all the values inside the policy are the same as the default
    values used by Micro.
    [-dontUseDefaultValues rtiddsmag option]

``LEAVE_RESOURCE_LIMITS``  (optional)
    Do not automatically update the resource limits of the DomainParticipantFactory,
    DomainParticipants, DataReaders, and DataWriters.
    [-dontUpdateResourceLimits rtiddsmag option]

``NO_DPSE_OPTIMIZATION``  (optional)
    Do not optimize static endpoint discovery.
    [-dontOptimizeSE rtiddsmag option]

``NO_XML_LOCATIONS``  (optional)
    Do not add the locations of the XML element which brought about the output construct.
    [-dontAddLocations rtiddsmag option]

``DPDE_NAME``  (optional)
    Change the name of the DPDE discovery name (default is dpde).
    [-dpdeName rtiddsmag option]

``DPSE_NAME``  (optional)
    Change the name of the DPSE discovery name (default is dpse).
    [-dpseName rtiddsmag option]

``UDP_NAME``  (optional)
    Change the name of the UDP transport name (default is udp).
    [-udpName rtiddsmag option]

``SHMEM_NAME``  (optional)
    Change the name of the SHMEM transport name (default is shmem).
    [-shmemName rtiddsmag option]

``ZCOPY_NAME``  (optional)
    Change the name of the ZCOPY transport name (default is zcopy).
    [-zCopyName rtiddsmag option]

``EXTRA_ARGS``  (optional)
    Extra arguments added to the Appgen command line

Output values:
The language variable is sanitized according to
:ref:`connextdds_sanitize_language` for the variable name.

``<xml_basename>_<LANG>_APPGEN_SOURCES``
    The list of generated source files..

``<xml_basename>_<LANG>_APPGEN_HEADERS``
    The list of generated header files..

If ``VAR`` is passed as a parameter, the variable name will be named like this:

``<VAR>_<LANG>_APPGEN_SOURCES``
    The list of the generated source files..

``<VAR>_<LANG>_APPGEN_HEADERS``
    The list of generated header files..


.. _connextdds_sanitize_language:

Sanitize Language
^^^^^^^^^^^^^^^^^
::

    connextdds_sanitize_language(
        LANG language
        VAR variable
    )

Get the sanitized version of the language to use in variable names. This output
is used by the CodeGen functions to create the default variable names. It will
make the following replacement:

* Replace ``+`` with ``X`` (i.e.: C++11 --> CXX11)
* Replace ``#`` with ``Sharp`` (i.e.: C# --> CSharp)
* Remove ``/`` (i.e.: C++/CLI --> CXXCLI)
#]]

include(CMakeParseArguments)
include(ConnextDdsArgumentChecks)

macro(_connextdds_codegen_find_codegen use_codegen1 use_micro)
    if(WIN32)
        set(script_ext ".bat")
    else()
        set(script_ext "")
    endif()

    if(${use_codegen1})
        # CodeGen 1 doesn't support server mode.
        set(script_name "rtiddsgen1")
    elseif(CONNEXTDDS_USE_CODEGEN_SERVER)
        set(script_name "rtiddsgen_server")
    else()
        set(script_name "rtiddsgen")
    endif()

    if(${use_micro})
        set(CODEGEN_PATH "${RTI_MICRO_CODEGEN_DIR}/${script_name}${script_ext}")
    else()
        set(CODEGEN_PATH "${RTICODEGEN_DIR}/${script_name}${script_ext}")
    endif()

    # Get the absolute path to avoid problems during build time.
    # Otherwise the relative path will go into the module makefiles where
    # the working directory is different.
    get_filename_component(CODEGEN_PATH "${CODEGEN_PATH}"
        ABSOLUTE
        BASE_DIR "${CMAKE_BINARY_DIR}"
    )

    # We assume that external RTICODEGEN_DIR provides their own JRE because.
    # The same for the JNI JVM libraries: a Codegen staging
    # allows to rticommon.sh to find and update the path with the library.
    set(additional_env)
    set(lib_search_def)
    if(TARGET rtiddsgen2)
        get_filename_component(JRE_BIN_DIR ${Java_JAVA_EXECUTABLE} DIRECTORY)
        get_filename_component(JREHOME ${JRE_BIN_DIR} DIRECTORY)
        list(APPEND additional_env "JREHOME=${JREHOME}")

        # For CodeGen server we need JNI JVM
        if(CONNEXTDDS_USE_CODEGEN_SERVER)
            connextdds_get_library_search_path_definition(
                OUTPUT lib_search_def
                DIRECTORIES
                    ${JNI_LIBRARY_DIRS}
            )
        endif()
    endif()

    # For CodeGen 1 we need to set also the Xalan path
    if(${use_codegen1})
        list(APPEND additional_env "XALANHOME=${XALANJ_LIBRARIES_DIR}")
    endif()

    if(additional_env OR codegen_ld_env)
        set(CODEGEN_COMMAND
            ${CMAKE_COMMAND} -E env
                "\"${lib_search_def}\""
                ${additional_env}
            ${CODEGEN_PATH}
        )
     else()
         set(CODEGEN_COMMAND ${CODEGEN_PATH})
     endif()

    if(PGW_GENERATOR_WRAPPER)
        set(CODEGEN_COMMAND "${Python3_EXECUTABLE}" "${PGW_GENERATOR_WRAPPER}" "${CODEGEN_PATH}")
    endif()

    if(${use_micro})
        execute_process(
            COMMAND
                ${CODEGEN_COMMAND} -micro -version
            OUTPUT_QUIET ERROR_QUIET
            RESULT_VARIABLE codegen_micro_supported
        )
        if(NOT codegen_micro_supported EQUAL 0)
            message(FATAL_ERROR
                "Micro code generation not supported: ${CODEGEN_COMMAND} -micro"
            )
        else()
            set(CODEGEN_COMMAND ${CODEGEN_COMMAND} -micro)
        endif()
    endif()

    if(${use_micro})
        set(_pgw_codegen_dir "${RTI_MICRO_CODEGEN_DIR}")
    else()
        set(_pgw_codegen_dir "${RTICODEGEN_DIR}")
    endif()
    if(NOT _pgw_codegen_dir)
        message(FATAL_ERROR
            "Missing generator directory; call find_package for the selected RTI product"
        )
    elseif(NOT EXISTS "${_pgw_codegen_dir}")
        message(FATAL_ERROR
            "Generator directory doesn't exist: ${_pgw_codegen_dir}")
    endif()
endmacro()

# Helper function to determine the generated files based on the language
# Supported languages are: C, C++, Java, C++/CLI, C++03, C++11, C#, and Python
function(_connextdds_codegen_get_generated_file_list)
    set(options STANDALONE DEBUG NO_CODE_GENERATION GENERATE_EXAMPLE MICRO)
    set(single_value_args VAR LANG PACKAGE IDL_BASENAME OUTPUT_DIR)
    set(multi_value_args "")
    cmake_parse_arguments(_CODEGEN
        "${options}"
        "${single_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    set(path_base "${_CODEGEN_OUTPUT_DIR}/${_CODEGEN_IDL_BASENAME}")
    set(timestamp_dir ${_CODEGEN_OUTPUT_DIR})

    if(NOT ${_CODEGEN_MICRO})
        # Connext DDS
        if("${_CODEGEN_LANG}" STREQUAL "C")
            set(sources "${path_base}.c")
            set(headers "${path_base}.h")
            if(NOT _CODEGEN_STANDALONE)
                list(APPEND sources "${path_base}Plugin.c" "${path_base}Support.c")
                list(APPEND headers "${path_base}Plugin.h" "${path_base}Support.h")
            endif()

            # Set in the parent scope
            set(${_CODEGEN_VAR}_HEADERS ${headers} PARENT_SCOPE)

            if(_CODEGEN_GENERATE_EXAMPLE)
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE
                    "${path_base}_publisher.c"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE
                    "${path_base}_subscriber.c"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SOURCES
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES
                    ${sources}
                    "${path_base}_publisher.c"
                    "${path_base}_subscriber.c"
                    PARENT_SCOPE)
            else()
                set(${_CODEGEN_VAR}_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE PARENT_SCOPE)
            endif()

        elseif("${_CODEGEN_LANG}" STREQUAL "C++"
            OR "${_CODEGEN_LANG}" STREQUAL "C++98"
        )
            set(sources "${path_base}.cxx")
            set(headers "${path_base}.h")

            if(NOT _CODEGEN_STANDALONE)
                list(APPEND sources "${path_base}Plugin.cxx" "${path_base}Support.cxx")
                list(APPEND headers "${path_base}Plugin.h" "${path_base}Support.h")
            endif()

            if(${_CODEGEN_GENERATE_EXAMPLE})
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE
                    "${path_base}_publisher.cxx"
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE
                    "${path_base}_subscriber.cxx"
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SOURCES
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES
                    ${sources}
                    "${path_base}_publisher.cxx"
                    "${path_base}_subscriber.cxx"
                    PARENT_SCOPE)
            else()
                # Set in the parent scope
                set(${_CODEGEN_VAR}_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_HEADERS ${headers} PARENT_SCOPE)
            endif()

        elseif("${_CODEGEN_LANG}" STREQUAL "C#"
            OR "${_CODEGEN_LANG}" STREQUAL "C++/CLI"
        )
            set(${_CODEGEN_VAR}_SOURCES
                "${path_base}.cpp"
                "${path_base}Plugin.cpp"
                "${path_base}Support.cpp"
                PARENT_SCOPE)
            set(${_CODEGEN_VAR}_HEADERS
                "${path_base}.h"
                "${path_base}Plugin.h"
                "${path_base}Support.h"
                PARENT_SCOPE)

            if(${_CODEGEN_GENERATE_EXAMPLE})
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE
                    "${path_base}_publisher.cpp"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE
                    "${path_base}_subscriber.cpp"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES
                    ${${_CODEGEN_VAR}_SOURCES}
                    "${path_base}_publisher.cpp"
                    "${path_base}_subscriber.cpp"
                    PARENT_SCOPE)
            endif()
        elseif("${_CODEGEN_LANG}" STREQUAL "C++03"
            OR "${_CODEGEN_LANG}" STREQUAL "C++11"
        )
            set(sources
                "${path_base}.cxx"
                "${path_base}Plugin.cxx"
            )
            set(headers
                "${path_base}.hpp"
                "${path_base}Plugin.hpp"
            )

            if(${_CODEGEN_GENERATE_EXAMPLE})
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE
                    "${path_base}_publisher.cxx"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE
                    "${path_base}_subscriber.cxx"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES ${sources} PARENT_SCOPE)
            else()
                # Set in the parent scope
                set(${_CODEGEN_VAR}_GENERATED_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_HEADERS ${headers} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE PARENT_SCOPE)
            endif()
        elseif("${_CODEGEN_LANG}" STREQUAL "Java")
            # Avoid conflicts generating the same IDL but different package flag.
            # For instance, this happens with corbaInterop in CodeGen 1.
            if(_CODEGEN_PACKAGE)
                set(timestamp_dir "${_CODEGEN_OUTPUT_DIR}/${_CODEGEN_PACKAGE}")
            endif()
        elseif("${_CODEGEN_LANG}" STREQUAL "Python")
            if(RTICONNEXTDDS_VERSION VERSION_LESS "7.0.0")
                message(FATAL_ERROR "Python code generation only supported since 7.0.0")
            endif()
            set(sources "${path_base}.py")
            if ("examplefiles" IN_LIST _CODEGEN_EXTRA_ARGS)
                list(APPEND sources
                    "${path_base}_program.py")
            endif()
            if(_CODEGEN_GENERATE_SERVICE_FILES)
                list(APPEND sources
                    "${path_base}_service.py")
            endif()
            set(${_CODEGEN_VAR}_SOURCES ${sources} PARENT_SCOPE)
        else()
            message(FATAL_ERROR "Language ${_CODEGEN_LANG} is not supported")
        endif()
    else()
        # Connext Micro
        if("${_CODEGEN_LANG}" STREQUAL "C")
            set(sources "${path_base}.c")
            set(headers "${path_base}.h")
            if(NOT _CODEGEN_STANDALONE)
                list(APPEND sources "${path_base}Plugin.c" "${path_base}Support.c")
                list(APPEND headers "${path_base}Plugin.h" "${path_base}Support.h")
            endif()

            # Set in the parent scope
            set(${_CODEGEN_VAR}_HEADERS ${headers} PARENT_SCOPE)

            if(_CODEGEN_GENERATE_EXAMPLE)
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE
                    "${path_base}_publisher.c"
                    "${path_base}Application.c"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE
                    "${path_base}_subscriber.c"
                    "${path_base}Application.c"
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SOURCES
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES
                    ${sources}
                    "${path_base}Application.c"
                    "${path_base}_publisher.c"
                    "${path_base}_subscriber.c"
                    PARENT_SCOPE)
            else()
                set(${_CODEGEN_VAR}_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE PARENT_SCOPE)
            endif()

        elseif("${_CODEGEN_LANG}" STREQUAL "C++")
            set(sources "${path_base}.cxx")
            set(headers "${path_base}.h")

            if(NOT _CODEGEN_STANDALONE)
                list(APPEND sources "${path_base}Plugin.cxx" "${path_base}Support.cxx")
                list(APPEND headers "${path_base}Plugin.h" "${path_base}Support.h")
            endif()

            if(${_CODEGEN_GENERATE_EXAMPLE})
                set(${_CODEGEN_VAR}_PUBLISHER_SOURCE
                    "${path_base}_publisher.cxx"
                    "${path_base}Application.cxx"
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SUBSCRIBER_SOURCE
                    "${path_base}_subscriber.cxx"
                    "${path_base}Application.cxx"
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_SOURCES
                    ${sources}
                    PARENT_SCOPE)
                set(${_CODEGEN_VAR}_GENERATED_SOURCES
                    ${sources}
                    "${path_base}Application.cxx"
                    "${path_base}_publisher.cxx"
                    "${path_base}_subscriber.cxx"
                    PARENT_SCOPE)
            else()
                # Set in the parent scope
                set(${_CODEGEN_VAR}_SOURCES ${sources} PARENT_SCOPE)
                set(${_CODEGEN_VAR}_HEADERS ${headers} PARENT_SCOPE)
            endif()
        else()
            message(FATAL_ERROR "Language ${_CODEGEN_LANG} is not supported with Micro")
        endif()
    endif()

    # Java File names depends on struct names to generate files, not on
    # IDL file name. IDL usually contains more than one structure so we
    # can't guess the source files if we don't know the structure names.
    # For that reason we use a timestamp file. We create the timestamp file
    # for all the languages to avoid complex logic.
    set(TIMESTAMP_DIR ${timestamp_dir} PARENT_SCOPE)
    set(TIMESTAMP
        "${timestamp_dir}/${_CODEGEN_IDL_BASENAME}_timestamp.cmake"
        PARENT_SCOPE)

    if(_CODEGEN_DEBUG)
        set(${_CODEGEN_VAR}_SOURCES
            ${${_CODEGEN_VAR}_SOURCES}
            "${path_base}.idl.rawxml"
            "${path_base}.idl.simplifiedxml"
            PARENT_SCOPE)
    endif()
endfunction()

function(connextdds_sanitize_language)
    cmake_parse_arguments(_LANG "" "LANG;VAR" "" ${ARGN})
    connextdds_check_required_arguments(_LANG_LANG _LANG_VAR)

    # Replaces C++ by CXX and C# by CSharp. Removes / for C++/CLI
    string(TOUPPER ${_LANG_LANG} lang_var)
    string(REPLACE "+" "X" lang_var ${lang_var})
    string(REPLACE "/" "" lang_var ${lang_var})
    string(REPLACE "#" "Sharp" lang_var ${lang_var})

    # Define variable in the caller
    set(${_LANG_VAR} ${lang_var} PARENT_SCOPE)
endfunction()

function(connextdds_rtiddsgen_run)
    set(options
        NOT_REPLACE UNBOUNDED IGNORE_ALIGNMENT USE42_ALIGNMENT
        OPTIMIZE_ALIGNMENT NO_TYPECODE DISABLE_PREPROCESSOR
        STANDALONE USE_CODEGEN1 DEBUG NO_CODE_GENERATION GENERATE_EXAMPLE
        MICRO
    )
    set(single_value_args
        LANG
        OUTPUT_DIRECTORY
        IDL_FILE
        VAR
        PACKAGE STRING_SIZE
        )
    set(multi_value_args INCLUDE_DIRS DEFINES EXTRA_ARGS DEPENDS)
    cmake_parse_arguments(_CODEGEN
        "${options}"
        "${single_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )
    connextdds_check_required_arguments(
        _CODEGEN_IDL_FILE
        _CODEGEN_LANG
        _CODEGEN_OUTPUT_DIRECTORY
    )

    if(_CODEGEN_DEBUG AND NOT _CODEGEN_USE_CODEGEN1)
        message(FATAL_ERROR "The debug option is only available in rtiddsgen1")
    endif()
    if(_CODEGEN_NO_CODE_GENERATION AND NOT _CODEGEN_USE_CODEGEN1)
        message(FATAL_ERROR
            "The noCodeGeneration option is only available in rtiddsgen1"
        )
    endif()

    if(_CODEGEN_USE_CODEGEN1)
        set(use_codegen1 TRUE)
    else()
        set(use_codegen1 FALSE)
    endif()
    if(_CODEGEN_MICRO)
        set(use_micro TRUE)
    else()
        set(use_micro FALSE)
    endif()
    _connextdds_codegen_find_codegen(${use_codegen1} ${use_micro})

    file(MAKE_DIRECTORY "${_CODEGEN_OUTPUT_DIRECTORY}")
    get_filename_component(idl_basename "${_CODEGEN_IDL_FILE}" NAME_WE)

    set(list_extra_args)
    if(_CODEGEN_STANDALONE)
        list(APPEND list_extra_args STANDALONE)
    endif()
    if(_CODEGEN_DEBUG)
        list(APPEND list_extra_args DEBUG)
    endif()
    if(_CODEGEN_NO_CODE_GENERATION)
        list(APPEND list_extra_args NO_CODE_GENERATION)
    endif()

    if(_CODEGEN_GENERATE_EXAMPLE)
        set(generate_example GENERATE_EXAMPLE)
    endif()

    _connextdds_codegen_get_generated_file_list(
        VAR IDL
        IDL_BASENAME ${idl_basename}
        LANG ${_CODEGEN_LANG}
        OUTPUT_DIR "${_CODEGEN_OUTPUT_DIRECTORY}"
        PACKAGE ${_CODEGEN_PACKAGE}
        ${generate_example}
        ${list_extra_args}
    )

    set(include_dirs)
    foreach(dir ${_CODEGEN_INCLUDE_DIRS})
        list(APPEND include_dirs -I "${dir}")
    endforeach()

    set(defines)
    foreach(def ${_CODEGEN_DEFINES})
        list(APPEND defines -D${def})
    endforeach()

    # Create the extra / optional arguments
    set(extra_flags "${_CODEGEN_EXTRA_ARGS}")
    if(_CODEGEN_IGNORE_ALIGNMENT)
        list(APPEND extra_flags "-ignoreAlignment")
    endif()

    if(_CODEGEN_USE42_ALIGNMENT)
        list(APPEND extra_flags "-use42eAlignment")
    endif()

    if(_CODEGEN_OPTIMIZE_ALIGNMENT)
        list(APPEND extra_flags "-optimizeAlignment")
    endif()

    if(_CODEGEN_NO_TYPECODE OR _CODEGEN_STANDALONE)
        list(APPEND extra_flags "-noTypeCode")
    endif()

    if(_CODEGEN_UNBOUNDED)
        list(APPEND extra_flags "-unboundedSupport")
    endif()

    if(_CODEGEN_DISABLE_PREPROCESSOR)
        list(APPEND extra_flags "-ppDisable")
    endif()

    if(_CODEGEN_PACKAGE)
        list(APPEND extra_flags "-package" "${_CODEGEN_PACKAGE}")
    endif()

    if(_CODEGEN_DEBUG)
        list(APPEND extra_flags "-debug")
    endif()

    if(_CODEGEN_NO_CODE_GENERATION)
        list(APPEND extra_flags "-noCodeGeneration")
    endif()

    # By default we overwrite all the generated files
    if(NOT _CODEGEN_NOT_REPLACE)
        list(APPEND extra_flags "-replace")
    endif()

    if(_CODEGEN_GENERATE_EXAMPLE)
        list(APPEND extra_flags "-example" ${CONNEXTDDS_ARCH})
    endif()

    if(_CODEGEN_STRING_SIZE)
        list(APPEND extra_flags "-stringSize" "${_CODEGEN_STRING_SIZE}")
    endif()

    # Call CodeGen
    add_custom_command(
        OUTPUT
            ${IDL_SOURCES}
            ${IDL_HEADERS}
            ${TIMESTAMP}
            ${IDL_PUBLISHER_SOURCE}
            ${IDL_SUBSCRIBER_SOURCE}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_CODEGEN_OUTPUT_DIRECTORY}
        COMMAND
            ${CODEGEN_COMMAND}
                ${include_dirs} ${defines}
                ${extra_flags}
                -language ${_CODEGEN_LANG}
                -d ${_CODEGEN_OUTPUT_DIRECTORY}
                ${_CODEGEN_IDL_FILE}
        # For the case of Java where we can't guess the source files.
        # It shouldn't harm in other cases.
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${TIMESTAMP_DIR}
        COMMAND
            ${CMAKE_COMMAND} -E touch ${TIMESTAMP}
        DEPENDS
            "${_CODEGEN_IDL_FILE}"
            ${_CODEGEN_DEPENDS}
            "${CODEGEN_PATH}"
            "${PGW_GENERATOR_WRAPPER}"
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            "${RTIMEHOME}/rtiddsgen/class/rtiddsgen2.jar"
        VERBATIM
    )

    # Get the name of the language variables (i.e.: C++/CLI -> CXXCLI).
    connextdds_sanitize_language(LANG ${_CODEGEN_LANG} VAR lang_var)

    if(_CODEGEN_VAR)
        set(var_prefix "${_CODEGEN_VAR}")
    else()
        set(var_prefix "${idl_basename}")
    endif()

    # Exports the files generated by Codegen
    set(${var_prefix}_${lang_var}_SOURCES ${IDL_SOURCES} PARENT_SCOPE)
    set(${var_prefix}_${lang_var}_GENERATED_SOURCES ${IDL_GENERATED_SOURCES} PARENT_SCOPE)
    set(${var_prefix}_${lang_var}_PUBLISHER_SOURCE ${IDL_PUBLISHER_SOURCE} PARENT_SCOPE)
    set(${var_prefix}_${lang_var}_SUBSCRIBER_SOURCE ${IDL_SUBSCRIBER_SOURCE} PARENT_SCOPE)
    set(${var_prefix}_${lang_var}_HEADERS ${IDL_HEADERS} PARENT_SCOPE)
    set(${var_prefix}_${lang_var}_TIMESTAMP ${TIMESTAMP} PARENT_SCOPE)
endfunction()

function(connextdds_rtiddsgen_convert)
    set(options NOT_REPLACE USE_CODEGEN1 MICRO)
    set(single_args VAR FROM TO INPUT OUTPUT_DIRECTORY)
    set(multi_args INCLUDE_DIRS DEFINES EXTRA_ARGS DEPENDS)
    cmake_parse_arguments(_CODEGEN "${options}" "${single_args}" "${multi_args}" ${ARGN})
    connextdds_check_required_arguments(_CODEGEN_INPUT _CODEGEN_FROM _CODEGEN_TO)

    if(_CODEGEN_USE_CODEGEN1)
        set(use_codegen1 TRUE)
    else()
        set(use_codegen1 FALSE)
    endif()
    if (_CODEGEN_MICRO)
        set(use_micro TRUE)
    else()
        set(use_micro FALSE)
    endif()
    _connextdds_codegen_find_codegen(${use_codegen1} ${use_micro})

    if(_CODEGEN_USE_CODEGEN1)
        # For CodeGen 1, it is not possible specify the input as full path.
        # We need to specify just the file name and chdir to the path.
        get_filename_component(input_dir ${_CODEGEN_INPUT} DIRECTORY)
        get_filename_component(input_name ${_CODEGEN_INPUT} NAME)
    else()
        # CodeGen 2 has the other bug, it can't parse a file from the
        # working directory. Reference: CODEGENII-877
        set(input_dir)
        set(input_name ${_CODEGEN_INPUT})
    endif()

    # Build the conversion related arguments
    if(_CODEGEN_FROM STREQUAL "IDL")
        set(input_arg -inputIdl "${input_name}")
    elseif(_CODEGEN_FROM STREQUAL "XML")
        set(input_arg -inputXml "${input_name}")
    elseif(_CODEGEN_FROM STREQUAL "XSD")
        set(input_arg -inputXsd "${input_name}")
    elseif(_CODEGEN_FROM STREQUAL "WSDL")
        set(input_arg -inputWsdl ${input_name})
    else()
        message(FATAL_ERROR "Invalid FROM type: ${_CODEGEN_FROM}")
    endif()

    set(output_ext)
    if(_CODEGEN_TO STREQUAL "IDL")
        set(output_arg "-convertToIdl")
        set(output_ext "idl")
    elseif(_CODEGEN_TO STREQUAL "XML")
        set(output_arg "-convertToXml")
        set(output_ext "xml")
    elseif(_CODEGEN_TO STREQUAL "XSD")
        set(output_arg "-convertToXsd")
        set(output_ext "xsd")
    elseif(_CODEGEN_TO STREQUAL "WSDL")
        set(output_arg "-convertToWsdl")
        set(output_ext "wsdl")
    else()
        message(FATAL_ERROR "Invalid TO type: ${_CODEGEN_TO}")
    endif()

    # Build the preprocessor arguments
    set(include_dirs)
    foreach(dir ${_CODEGEN_INCLUDE_DIRS})
        list(APPEND include_dirs -I "${dir}")
    endforeach()

    set(defines)
    foreach(def ${_CODEGEN_DEFINES})
        list(APPEND defines -D${def})
    endforeach()

    # Build the optional arguments
    set(optional_args ${_CODEGEN_EXTRA_ARGS})
    if(NOT _CODEGEN_NOT_REPLACE)
        list(APPEND optional_args "-replace")
    endif()

    # Set the output directory
    # We create the directory at run-time in case it was deleted
    set(output_dir)
    if(_CODEGEN_OUTPUT_DIRECTORY)
        set(output_dir "${_CODEGEN_OUTPUT_DIRECTORY}")
    else()
        get_filename_component(output_dir "${_CODEGEN_INPUT}" DIRECTORY)
    endif()

    # Define the output file in the variable
    get_filename_component(input_name_we "${_CODEGEN_INPUT}" NAME_WE)
    set(output_file "${output_dir}/${input_name_we}.${output_ext}")
    if(_CODEGEN_VAR)
        set(${_CODEGEN_VAR} "${output_file}" PARENT_SCOPE)
    endif()

    # Run CodeGen
    add_custom_command(
        OUTPUT
            "${output_file}"
        COMMENT "Generating from ${_CODEGEN_FROM}: ${output_file}"
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${output_dir}
        COMMAND
            ${CODEGEN_COMMAND}
                ${input_arg} ${output_arg}
                ${include_dirs} ${defines}
                -d ${output_dir}
                ${optional_args}
        WORKING_DIRECTORY ${input_dir}
        DEPENDS
            ${CODEGEN_DEPENDS}
            "${_CODEGEN_INPUT}"
            ${_CODEGEN_DEPENDS}
    )
endfunction()

macro(_connextdds_appgen_find_appgen)
    if(WIN32)
        set(script_ext ".bat")
    else()
        set(script_ext "")
    endif()

    set(script_name "rtiddsmag")
    set(APPGEN_PATH "${RTIAPPGEN_DIR}/${script_name}${script_ext}")

    # Get the absolute path to avoid problems during build time.
    # Otherwise the relative path will go into the module makefiles where
    # the working directory is different.
    get_filename_component(APPGEN_PATH "${APPGEN_PATH}"
        ABSOLUTE
        BASE_DIR "${CMAKE_BINARY_DIR}"
    )

    # We assume that external RTIAPPGEN_DIR provides their own JRE.
    set(additional_env)
    set(lib_search_def)
    # get_filename_component(JRE_BIN_DIR ${Java_JAVA_EXECUTABLE} DIRECTORY)
    # get_filename_component(JREHOME ${JRE_BIN_DIR} DIRECTORY)
    # list(APPEND additional_env "JREHOME=${JREHOME}")

    if(additional_env OR appgen_ld_env)
        set(APPGEN_COMMAND
            ${CMAKE_COMMAND} -E env
                "\"${lib_search_def}\""
                ${additional_env}
            ${APPGEN_PATH}
        )
    else()
        set(APPGEN_COMMAND ${APPGEN_PATH})
    endif()
    if(PGW_GENERATOR_WRAPPER)
        set(APPGEN_COMMAND "${Python3_EXECUTABLE}" "${PGW_GENERATOR_WRAPPER}" "${APPGEN_PATH}")
    endif()

    if(NOT RTIAPPGEN_DIR)
        message(FATAL_ERROR
            "Missing RTIAPPGEN_DIR variable. Please, call the find_package"
            "CMake built-in macro for the RTIConnextMicroDDS package"
        )
    elseif(NOT EXISTS ${RTIAPPGEN_DIR})
        message(FATAL_ERROR
            "RTIAPPGEN_DIR dir doesn't exist: ${RTIAPPGEN_DIR}")
    endif()
endmacro()

# Helper function to determine the generated files based on the language
# Supported languages are: C, C++.
function(_connextdds_appgen_get_generated_file_list)
    set(options)
    set(single_value_args VAR LANG XML_BASENAME OUTPUT_DIR)
    set(multi_value_args "")
    cmake_parse_arguments(_APPGEN
        "${options}"
        "${single_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    set(path_base "${_APPGEN_OUTPUT_DIR}/${_APPGEN_XML_BASENAME}")

    # Common files
    set(sources "${path_base}Appgen.c")
    set(headers "${path_base}Appgen.h")

    # Language specific files
    if("${_APPGEN_LANG}" STREQUAL "C")

    elseif("${_APPGEN_LANG}" STREQUAL "C++")
        list(APPEND sources "${path_base}AppgenPlugin.cxx")
        list(APPEND headers "${path_base}AppgenPlugin.h")
    else()
        message(FATAL_ERROR "Language ${_APPGEN_LANG} is not supported")
    endif()

    set(${_APPGEN_VAR}_APPGEN_SOURCES ${sources} PARENT_SCOPE)
    set(${_APPGEN_VAR}_APPGEN_HEADERS ${headers} PARENT_SCOPE)

endfunction()

function(connextdds_rtiddsmag_run)
    set(options
        NOT_REPLACE
        NO_DEFAULT_VALUES
        LEAVE_RESOURCE_LIMITS
        NO_DPSE_OPTIMIZATION
        NO_XML_LOCATIONS
    )
    set(single_value_args
        LANG
        OUTPUT_DIRECTORY
        XML_FILE
        VAR
        IDL_FILE
        DPDE_NAME
        DPSE_NAME
        UDP_NAME
        SHMEM_NAME
        ZCOPY_NAME
        )
    set(multi_value_args REFERENCED_FILES EXTRA_ARGS DEPENDS)
    cmake_parse_arguments(_APPGEN
        "${options}"
        "${single_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )
    connextdds_check_required_arguments(
        _APPGEN_XML_FILE
        _APPGEN_LANG
        _APPGEN_OUTPUT_DIRECTORY
    )

    _connextdds_appgen_find_appgen()

    file(MAKE_DIRECTORY "${_APPGEN_OUTPUT_DIRECTORY}")
    get_filename_component(xml_basename "${_APPGEN_XML_FILE}" NAME_WE)

    _connextdds_appgen_get_generated_file_list(
        VAR XML
        XML_BASENAME ${xml_basename}
        LANG ${_APPGEN_LANG}
        OUTPUT_DIR "${_APPGEN_OUTPUT_DIRECTORY}"
    )

    # By default we overwrite all the generated files
    set(appgen_flags)
    if(NOT _APPGEN_NOT_REPLACE)
        list(APPEND appgen_flags "-replace")
    endif()

    # Add the IDL file if it is specified
    set(_pgw_appgen_idl_depends)
    if(_APPGEN_IDL_FILE)
        # MAG uses this argument as a generated C include name, not as an
        # input to parse. Absolute paths otherwise leak into Appgen headers.
        get_filename_component(_pgw_appgen_idl_name "${_APPGEN_IDL_FILE}" NAME)
        list(APPEND appgen_flags "-idlFile" "${_pgw_appgen_idl_name}")
        if(IS_ABSOLUTE "${_APPGEN_IDL_FILE}" OR EXISTS "${_APPGEN_IDL_FILE}")
            list(APPEND _pgw_appgen_idl_depends "${_APPGEN_IDL_FILE}")
        endif()
    endif()

    # Create the referenced files arguments
    foreach(file ${_APPGEN_REFERENCED_FILES})
        list(APPEND appgen_flags "-referencedFile" "${file}")
    endforeach()

    # Add the default values flag if it is specified
    if(_APPGEN_NO_DEFAULT_VALUES)
        list(APPEND appgen_flags "-dontUseDefaultValues")
    endif()

    # Add the resource limits flag if it is specified
    if(_APPGEN_LEAVE_RESOURCE_LIMITS)
        list(APPEND appgen_flags "-dontUpdateResourceLimits")
    endif()

    # Add the static endpoint discovery flag if it is specified
    if(_APPGEN_NO_DPSE_OPTIMIZATION)
        list(APPEND appgen_flags "-dontOptimizeSE")
    endif()

    # Add the XML locations flag if it is specified
    if(_APPGEN_NO_XML_LOCATIONS)
        list(APPEND appgen_flags "-dontAddLocations")
    endif()

    # Add the DPDE name if it is specified
    if(_APPGEN_DPDE_NAME)
        list(APPEND appgen_flags "-dpdeName" "${_APPGEN_DPDE_NAME}")
    endif()

    # Add the DPSE name if it is specified
    if(_APPGEN_DPSE_NAME)
        list(APPEND appgen_flags "-dpseName" "${_APPGEN_DPSE_NAME}")
    endif()

    # Add the UDP name if it is specified
    if(_APPGEN_UDP_NAME)
        list(APPEND appgen_flags "-udpName" "${_APPGEN_UDP_NAME}")
    endif()

    # Add the SHMEM name if it is specified
    if(_APPGEN_SHMEM_NAME)
        list(APPEND appgen_flags "-shmemName" "${_APPGEN_SHMEM_NAME}")
    endif()

    # Add the ZCOPY name if it is specified
    if(_APPGEN_ZCOPY_NAME)
        list(APPEND appgen_flags "-zCopyName" "${_APPGEN_ZCOPY_NAME}")
    endif()

    # Add the extra arguments
    list(APPEND appgen_flags ${_APPGEN_EXTRA_ARGS})

    # Call AppGen
    add_custom_command(
        OUTPUT
            ${XML_APPGEN_SOURCES}
            ${XML_APPGEN_HEADERS}
        COMMAND
            ${CMAKE_COMMAND} -E make_directory ${_APPGEN_OUTPUT_DIRECTORY}
        COMMAND
            ${APPGEN_COMMAND}
                ${appgen_flags}
                -language ${_APPGEN_LANG}
                -d ${_APPGEN_OUTPUT_DIRECTORY}
                ${_APPGEN_XML_FILE}
        DEPENDS
            "${_APPGEN_XML_FILE}"
            ${_APPGEN_REFERENCED_FILES}
            ${_APPGEN_DEPENDS}
            ${_pgw_appgen_idl_depends}
            "${APPGEN_PATH}"
            "${PGW_GENERATOR_WRAPPER}"
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            "${RTIMEHOME}/rtiddsmag/class/rtiddsmag.jar"
        VERBATIM
    )

    # Get the name of the language variables (i.e.: C++/CLI -> CXXCLI).
    connextdds_sanitize_language(LANG ${_APPGEN_LANG} VAR lang_var)

    if(_APPGEN_VAR)
        set(var_prefix "${_APPGEN_VAR}")
    else()
        set(var_prefix "${xml_basename}")
    endif()

    # Exports the files generated by Appgen
    set(${var_prefix}_${lang_var}_APPGEN_SOURCES ${XML_APPGEN_SOURCES} PARENT_SCOPE)
    set(${var_prefix}_${lang_var}_APPGEN_HEADERS ${XML_APPGEN_HEADERS} PARENT_SCOPE)
endfunction()