if(PGW_BUILD_TESTS)
    add_test(NAME config.strict_xml
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_config.py")
endif()
