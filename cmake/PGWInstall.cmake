include_guard(GLOBAL)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    message(FATAL_ERROR "Relocatable PGW installation requires relative lib/include destinations")
endif()

set(_pgw_install_targets "")
set(PGW_INSTALLED_COMPONENTS "")
foreach(_component IN ITEMS core diagnostics_local
        adapter_can can_memory can_socketcan adapter_dds_micro binding_signal
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
endif()
if(TARGET pgw_adapter_dds_micro)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/adapters/dds_micro/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
endif()
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
