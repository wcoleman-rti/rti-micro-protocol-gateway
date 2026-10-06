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
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    message(FATAL_ERROR "Relocatable PGW installation requires relative lib/include destinations")
endif()

set(_pgw_install_targets "")
set(PGW_INSTALLED_COMPONENTS "")
foreach(_component IN ITEMS core diagnostics_local
        adapter_can can_memory can_socketcan adapter_dds_connext_micro binding_signal
        micro_infrastructure micro_dds build_options)
    if(TARGET pgw_${_component})
        set_target_properties(pgw_${_component} PROPERTIES EXPORT_NAME "${_component}")
        get_target_property(_includes pgw_${_component} INTERFACE_INCLUDE_DIRECTORIES)
        if(_includes)
            string(REPLACE "$<INSTALL_INTERFACE:include>"
                "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>" _includes "${_includes}")
            set_target_properties(pgw_${_component} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_includes}")
        endif()
        list(APPEND _pgw_install_targets pgw_${_component})
        list(APPEND PGW_INSTALLED_COMPONENTS "${_component}")
    endif()
endforeach()
if(NOT TARGET pgw_core)
    return()
endif()
install(TARGETS ${_pgw_install_targets} EXPORT PGWTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
foreach(_headers IN ITEMS core/include)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/${_headers}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
endforeach()
if(TARGET pgw_adapter_can)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/adapters/can/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
    install(FILES "${PROJECT_SOURCE_DIR}/adapters/can/adapter.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/pgw/adapters/can")
    if(PGW_ENABLE_REMOTE_CONTROL)
        get_target_property(_can_control_idl pgw_adapter_can PGW_ADAPTER_CONTROL_IDL)
        install(FILES "${_can_control_idl}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/pgw/adapters/can")
    endif()
endif()
if(TARGET pgw_adapter_dds_connext_micro)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/adapters/dds/connext_micro/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
    install(FILES "${PROJECT_SOURCE_DIR}/adapters/dds/connext_micro/adapter.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/pgw/adapters/connext_micro")
    if(PGW_ENABLE_REMOTE_CONTROL)
        get_target_property(_dds_control_idl
            pgw_adapter_dds_connext_micro PGW_ADAPTER_CONTROL_IDL)
        install(FILES "${_dds_control_idl}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/pgw/adapters/connext_micro")
    endif()
    if(PGW_ENABLE_REMOTE_CONTROL)
        install(FILES "${PROJECT_SOURCE_DIR}/core/control/idl/control_common.idl"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/pgw/idl")
    endif()
endif()
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/pgw")
if(TARGET pgw_binding_signal)
    install(FILES "${PROJECT_SOURCE_DIR}/bindings/signal/include/pgw/signal.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pgw")
endif()

set(_pgw_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/PGW")
install(EXPORT PGWTargets NAMESPACE PGW::
    DESTINATION "${_pgw_package_dir}" FILE PGWTargets.cmake)
set(PGW_INSTALL_MICRO_CONFIGURATION "$<IF:$<CONFIG:Debug>,Debug,Release>")
set(PGW_INSTALL_TARGET_NAME "${RTIME_TARGET_NAME}")
set(PGW_INSTALL_CONFIGURATION "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,NOCONFIG>")
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/PGWConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/package/PGWConfig.cmake.in"
    INSTALL_DESTINATION "${_pgw_package_dir}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/package/$<CONFIG>/PGWConfig.cmake"
    INPUT "${CMAKE_BINARY_DIR}/package/PGWConfig.cmake.in")
write_basic_package_version_file("${CMAKE_BINARY_DIR}/package/PGWConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_BINARY_DIR}/package/$<CONFIG>/PGWConfig.cmake"
    "${CMAKE_BINARY_DIR}/package/PGWConfigVersion.cmake"
    DESTINATION "${_pgw_package_dir}")
install(FILES
    "${PROJECT_SOURCE_DIR}/cmake/FindRTIConnextMicroDDS.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/ConnextDdsCodegen.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/ConnextDdsArgumentChecks.cmake"
    DESTINATION "${_pgw_package_dir}/modules")
