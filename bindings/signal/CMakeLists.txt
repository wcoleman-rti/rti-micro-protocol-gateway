add_library(pgw_binding_signal INTERFACE)
add_library(PGW::binding_signal ALIAS pgw_binding_signal)
target_include_directories(pgw_binding_signal INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(pgw_binding_signal INTERFACE c_std_11)
