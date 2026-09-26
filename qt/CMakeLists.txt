find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui)

set(CMAKE_AUTOMOC ON)

set(XDS_QT_PLUGIN_DIR "${QT6_INSTALL_PLUGINS}" CACHE PATH
    "Directory, relative to the install prefix, that holds Qt's plugin folders")

qt_add_plugin(xds_imageformat
    CLASS_NAME XdsPlugin
    PLUGIN_TYPE imageformats
    xdsimageplugin.cpp
    xdsimageplugin.h
    xds.json
)
set_target_properties(xds_imageformat PROPERTIES
    OUTPUT_NAME xds
    # Tests load the plugin from here, laid out the way Qt expects.
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/plugins/imageformats
)
target_link_libraries(xds_imageformat PRIVATE xds::xds Qt6::Gui)

install(TARGETS xds_imageformat LIBRARY DESTINATION ${XDS_QT_PLUGIN_DIR}/imageformats)
install(FILES x-xds.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
