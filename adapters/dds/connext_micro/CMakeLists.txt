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

add_library(pgw_adapter_dds_connext_micro src/connext_micro.c)
add_library(PGW::adapter_dds_connext_micro ALIAS pgw_adapter_dds_connext_micro)
set_property(TARGET pgw_adapter_dds_connext_micro PROPERTY PGW_ADAPTER_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter.xml")
pgw_generate_adapter_control_idl(pgw_adapter_dds_connext_micro)
target_include_directories(pgw_adapter_dds_connext_micro PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(pgw_adapter_dds_connext_micro PUBLIC PGW::core
    "$<LINK_GROUP:RESCAN,RTIConnextMicroDDS::c_api,RTIConnextMicroDDS::core,RTIConnextMicroDDS::osapi,RTIConnextMicroDDS::netio,RTIConnextMicroDDS::rhsm,RTIConnextMicroDDS::whsm,RTIConnextMicroDDS::appgen,RTIConnextMicroDDS::dpde,RTIConnextMicroDDS::dpse>"
    PRIVATE PGW::build_options)
