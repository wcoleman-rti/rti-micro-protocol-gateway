add_library(pgw_adapter_dds_micro src/dds_micro.c)
add_library(PGW::adapter_dds_micro ALIAS pgw_adapter_dds_micro)
target_include_directories(pgw_adapter_dds_micro PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(pgw_adapter_dds_micro PUBLIC PGW::core
    "$<LINK_GROUP:RESCAN,RTIConnextMicroDDS::c_api,RTIConnextMicroDDS::core,RTIConnextMicroDDS::osapi,RTIConnextMicroDDS::netio,RTIConnextMicroDDS::rhsm,RTIConnextMicroDDS::whsm,RTIConnextMicroDDS::appgen,RTIConnextMicroDDS::dpde,RTIConnextMicroDDS::dpse>"
    PRIVATE PGW::build_options)
