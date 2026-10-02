cmake_minimum_required(VERSION 3.24)
project(pgw_package_consumer LANGUAGES C)
find_package(PGW 0.1 CONFIG REQUIRED COMPONENTS
    core diagnostics_local adapter_can can_memory can_socketcan
    adapter_dds_micro binding_signal)
add_executable(pgw_package_consumer main.c)
target_link_libraries(pgw_package_consumer PRIVATE
    PGW::core PGW::diagnostics_local
    PGW::adapter_can PGW::can_memory PGW::can_socketcan
    PGW::adapter_dds_micro PGW::binding_signal)
