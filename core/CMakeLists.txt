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

add_library(pgw_core src/core.c src/sequence.c src/diagnostics.c src/runtime.c)
add_library(PGW::core ALIAS pgw_core)
target_include_directories(pgw_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(pgw_core PUBLIC PGW::micro_infrastructure)
target_compile_features(pgw_core PUBLIC c_std_11)
target_compile_options(pgw_core PRIVATE -Wall -Wextra -Wpedantic)
if(PGW_BUILD_TESTS)
    add_executable(pgw_runtime_test tests/runtime_test.c)
    target_link_libraries(pgw_runtime_test PRIVATE PGW::core PGW::build_options)
    add_test(NAME core.runtime_lifecycle COMMAND pgw_runtime_test)
    set_tests_properties(core.runtime_lifecycle PROPERTIES TIMEOUT 10)
endif()
add_library(pgw_diagnostics_local src/local_sink.c)
add_library(PGW::diagnostics_local ALIAS pgw_diagnostics_local)
find_package(Threads REQUIRED)
target_link_libraries(pgw_diagnostics_local PUBLIC PGW::core PRIVATE Threads::Threads)
target_compile_options(pgw_diagnostics_local PRIVATE -Wall -Wextra -Wpedantic)
