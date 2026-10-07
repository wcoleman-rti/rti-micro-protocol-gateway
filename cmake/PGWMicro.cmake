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
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(RTIMEHOME "" CACHE PATH "Explicit Connext Micro 4.3.0 SDK root")
set(RTIME_PIL_ARCH "" CACHE STRING "Micro platform-independent architecture (auto-detected when empty)")
set(RTIME_PSL_ARCH "" CACHE STRING "Micro platform-specific architecture (auto-detected when empty)")
set(RTIME_TARGET_NAME "" CACHE STRING "Micro target architecture (auto-detected when empty)")
set(PGW_JREHOME "" CACHE PATH "Explicit Java 17 runtime for host RTI generators")
if(NOT RTIMEHOME AND DEFINED ENV{RTIMEHOME})
    set(RTIMEHOME "$ENV{RTIMEHOME}" CACHE PATH "Explicit Connext Micro 4.3.0 SDK root" FORCE)
endif()
if(NOT PGW_JREHOME AND DEFINED ENV{JAVA_HOME})
    set(PGW_JREHOME "$ENV{JAVA_HOME}" CACHE PATH "Explicit Java 17 runtime for host RTI generators" FORCE)
endif()
if(NOT RTIMEHOME)
    message(FATAL_ERROR "Set -DRTIMEHOME to the installed Connext Micro 4.3.0 SDK root")
endif()
if(NOT PGW_JREHOME)
    message(FATAL_ERROR "Set -DPGW_JREHOME to the installed Java 17 runtime")
endif()
if(NOT RTIME_PIL_ARCH OR NOT RTIME_PSL_ARCH OR NOT RTIME_TARGET_NAME)
    message(FATAL_ERROR
        "Set RTIME_PIL_ARCH, RTIME_PSL_ARCH and RTIME_TARGET_NAME for the selected Micro SDK target")
endif()
set(RTIME_LIBS_BUILD_TYPE "Auto" CACHE STRING "Micro archive configuration: Auto, Release, Debug")
set_property(CACHE RTIME_LIBS_BUILD_TYPE PROPERTY STRINGS Auto Release Debug)
if(CMAKE_CONFIGURATION_TYPES AND NOT RTIME_LIBS_BUILD_TYPE STREQUAL "Auto")
    message(FATAL_ERROR "Multi-configuration generators require RTIME_LIBS_BUILD_TYPE=Auto")
endif()
if(CMAKE_BUILD_TYPE)
    if((CMAKE_BUILD_TYPE STREQUAL "Debug" AND RTIME_LIBS_BUILD_TYPE STREQUAL "Release") OR
       (NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND RTIME_LIBS_BUILD_TYPE STREQUAL "Debug"))
        message(FATAL_ERROR "Gateway and Micro archive debug/release configurations must agree")
    endif()
endif()

if(BUILD_SHARED_LIBS)
    message(FATAL_ERROR "This prototype requires static Micro archives; disable BUILD_SHARED_LIBS")
endif()
if(NOT EXISTS "${PGW_JREHOME}/bin/java")
    message(FATAL_ERROR "PGW_JREHOME must name a Java runtime containing bin/java")
endif()
execute_process(COMMAND "${PGW_JREHOME}/bin/java" -version
    OUTPUT_VARIABLE _java_out ERROR_VARIABLE _java_err RESULT_VARIABLE _java_result)
if(NOT _java_result EQUAL 0)
    message(FATAL_ERROR "Cannot execute configured Java runtime: ${_java_err}")
endif()
if(NOT "${_java_out}${_java_err}" MATCHES "version \"17\\.")
    message(FATAL_ERROR "Connext Micro MAG requires the explicitly configured Java 17 runtime")
endif()
string(REGEX MATCH "version \"([^\"]+)\"" _java_match "${_java_out}${_java_err}")
set(PGW_JRE_VERSION "${CMAKE_MATCH_1}")

set(_pgw_micro_components c)
if(PGW_ENABLE_DDS)
    list(APPEND _pgw_micro_components dpde appgen)
endif()
file(REAL_PATH "${RTIMEHOME}" _pgw_requested_root)
set(_pgw_requested_pil "${RTIME_PIL_ARCH}")
set(_pgw_requested_psl "${RTIME_PSL_ARCH}")
set(_pgw_requested_target "${RTIME_TARGET_NAME}")
find_package(RTIConnextMicroDDS 4.3.0 REQUIRED COMPONENTS ${_pgw_micro_components})
file(REAL_PATH "${RTIMEHOME}" _pgw_found_root)
if(NOT _pgw_found_root STREQUAL _pgw_requested_root OR
   (_pgw_requested_pil AND NOT RTIME_PIL_ARCH STREQUAL _pgw_requested_pil) OR
   (_pgw_requested_psl AND NOT RTIME_PSL_ARCH STREQUAL _pgw_requested_psl) OR
   (_pgw_requested_target AND NOT RTIME_TARGET_NAME STREQUAL _pgw_requested_target))
    message(FATAL_ERROR "Micro finder changed the explicitly selected SDK root/PIL/PSL; refusing fallback")
endif()
if(NOT "${RTIME_VERSION}" MATCHES "^4\\.3\\.0(\\.0)?$")
    message(FATAL_ERROR "Expected Micro 4.3.0, found ${RTIME_VERSION}")
endif()
if(NOT RTIAPPGEN_VERSION STREQUAL "4.3.0" OR NOT RTICODEGEN_VERSION)
    message(FATAL_ERROR "Expected Micro MAG 4.3.0 and a recognized Codegen version")
endif()
if(NOT CMAKE_C_LINK_GROUP_USING_RESCAN_SUPPORTED AND NOT CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    message(FATAL_ERROR "Micro infrastructure requires linker RESCAN support on this platform")
endif()

add_library(pgw_micro_infrastructure INTERFACE)
add_library(PGW::micro_infrastructure ALIAS pgw_micro_infrastructure)
target_link_libraries(pgw_micro_infrastructure INTERFACE
    "$<LINK_GROUP:RESCAN,RTIConnextMicroDDS::c_api,RTIConnextMicroDDS::core,RTIConnextMicroDDS::osapi>")
if(PGW_ENABLE_DDS)
    add_library(pgw_micro_dds INTERFACE)
    add_library(PGW::micro_dds ALIAS pgw_micro_dds)
    target_link_libraries(pgw_micro_dds INTERFACE RTIConnextMicroDDS::c
        RTIConnextMicroDDS::dpde RTIConnextMicroDDS::appgen PGW::micro_infrastructure)
endif()
message(STATUS "PGW Micro ${RTIME_VERSION}: ${RTIMEHOME}")
message(STATUS "PGW PIL=${RTIME_PIL_ARCH}; PSL=${RTIME_PSL_ARCH}; libraries=${RTIME_LIBS_BUILD_TYPE}")
message(STATUS "PGW host JRE=${PGW_JREHOME} (${PGW_JRE_VERSION}); Codegen=${RTICODEGEN_VERSION}; MAG=${RTIAPPGEN_VERSION}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/pgw-build-info-$<CONFIG>.txt" CONTENT
"PGW ${PROJECT_VERSION}
Micro=${RTIME_VERSION}
SDK=${RTIMEHOME}
PIL=${RTIME_PIL_ARCH}
PSL=${RTIME_PSL_ARCH}
Micro archives=$<IF:$<CONFIG:Debug>,Debug,Release>
Compiler=${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}
Compiler path=${CMAKE_C_COMPILER}
CMake=${CMAKE_VERSION}
JRE=${PGW_JREHOME}
Java=${PGW_JRE_VERSION}
Codegen=${RTICODEGEN_VERSION}
MAG=${RTIAPPGEN_VERSION}
CAN=${PGW_ENABLE_CAN}
DDS=${PGW_ENABLE_DDS}
Remote control=${PGW_ENABLE_REMOTE_CONTROL}
Core runtime=event-driven sessions
")
