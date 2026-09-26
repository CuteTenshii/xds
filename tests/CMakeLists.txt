add_executable(tst_xds tst_xds.cpp check.h synthetic.h)
target_link_libraries(tst_xds PRIVATE xds::xds ZLIB::ZLIB)
add_test(NAME tst_xds COMMAND tst_xds)

# Checks the library against real game files; set XDS_SAMPLE_DIR to a
# directory of .xds files to run it, otherwise it reports as skipped.
add_executable(tst_realdata tst_realdata.cpp)
target_link_libraries(tst_realdata PRIVATE xds::xds)
add_test(NAME tst_realdata COMMAND tst_realdata)
set_tests_properties(tst_realdata PROPERTIES SKIP_RETURN_CODE 77)

if(TARGET xds_imageformat)
    find_package(Qt6 REQUIRED COMPONENTS Gui Test)
    add_executable(tst_qtplugin tst_qtplugin.cpp synthetic.h)
    set_target_properties(tst_qtplugin PROPERTIES AUTOMOC ON)
    target_compile_definitions(tst_qtplugin PRIVATE XDS_TEST_PLUGIN_DIR="${PROJECT_BINARY_DIR}/plugins")
    target_link_libraries(tst_qtplugin PRIVATE xds::xds Qt6::Gui Qt6::Test)
    add_dependencies(tst_qtplugin xds_imageformat)
    add_test(NAME tst_qtplugin COMMAND tst_qtplugin)
endif()
