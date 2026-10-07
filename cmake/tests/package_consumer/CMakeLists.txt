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

cmake_minimum_required(VERSION 3.24)
project(pgw_package_consumer LANGUAGES C)
find_package(PGW 0.1 CONFIG REQUIRED
    COMPONENTS core adapter_can can_memory adapter_dds_connext_micro binding_signal
    OPTIONAL_COMPONENTS diagnostics_local can_socketcan)
add_executable(pgw_package_consumer main.c)
target_link_libraries(pgw_package_consumer PRIVATE
    PGW::core PGW::adapter_can PGW::can_memory
    PGW::adapter_dds_connext_micro PGW::binding_signal)
if(TARGET PGW::diagnostics_local)
    target_link_libraries(pgw_package_consumer PRIVATE PGW::diagnostics_local)
    target_compile_definitions(pgw_package_consumer PRIVATE PGW_HAS_DIAGNOSTICS_LOCAL=1)
endif()
if(TARGET PGW::can_socketcan)
    target_link_libraries(pgw_package_consumer PRIVATE PGW::can_socketcan)
    target_compile_definitions(pgw_package_consumer PRIVATE PGW_HAS_CAN_SOCKETCAN=1)
endif()
