set(target Modeling_pCDM)

message(STATUS "Plugin ${target}")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(pluginInterfaceSources
    plugin.cpp
)

set(sources
    pCDM_types.h
    pCDM_types.cpp
    PCDMBackend.h
    PCDMBackend.cpp
    PCDMModel.h
    PCDMModel.cpp
    PCDMPlugin.h
    PCDMPlugin.cpp
    PCDMProject.h
    PCDMProject.cpp
    PCDMVisualizationGenerator.h
    PCDMVisualizationGenerator.cpp
    PCDMWidget.h
    PCDMWidget.cpp
    PCDMWidget_StateHelper.h
)

set(UIs
    PCDMWidget.ui
)

qt5_wrap_ui(UI_Srcs ${UIs})

source_group_by_path_and_type(${CMAKE_CURRENT_SOURCE_DIR} ${pluginInterfaceSources} ${sources} ${UIs})
source_group_by_path(${CMAKE_CURRENT_BINARY_DIR} ".*" "Generated" ${UI_Srcs} ${QRC_Srcs})
source_group_by_path(${CMAKE_CURRENT_SOURCE_DIR} ".*" "" "CMakeLists.txt")


find_package(Eigen3 REQUIRED)


# The plugin does only export the plugin interface but not actual library functions.
# Static linking makes these functions available to tests.

set(staticTarget "${target}Static")

add_library(${staticTarget} STATIC ${sources} ${UI_Srcs} ${QRC_Srcs})
target_include_directories(${staticTarget}
    PUBLIC
        ${GEOHAZARDVIS_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${EIGEN3_INCLUDE_DIR}
)
target_link_libraries(${staticTarget}
    PUBLIC
        gui
)
configure_cxx_target(${staticTarget} AUX_PLUGIN_TARGET)


# Create a plugin library and statically link the actual plugin logic
add_library(${target} SHARED ${pluginInterfaceSources})
target_link_libraries(${target} PUBLIC ${staticTarget})
configure_cxx_target(${target} PLUGIN_TARGET)


install(TARGETS ${target}
    RUNTIME DESTINATION ${INSTALL_PLUGINS_BIN}
    LIBRARY DESTINATION ${INSTALL_PLUGINS_SHARED}
    # ARCHIVE DESTINATION ${INSTALL_PLUGINS_LIB}
)


if (OPTION_BUILD_TESTS AND GTEST_FOUND)
    add_subdirectory("tests")
endif()
